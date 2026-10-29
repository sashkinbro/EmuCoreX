// SPDX-License-Identifier: GPL-3.0+
// Independent lane/alias and full-state regressions for local emitter rewrites.
extern "C" u64 EmuCoreXOracleVULocalNaturalEmits();
void VULocalArchitecturalTests()
{
    const auto move = [](u32 fs, u32 ft, u32 mask, bool rotate = false) {
        return 0x8000033cu | static_cast<u32>(rotate) | (fs << 11) | (ft << 16) | (mask << 21);
    };
    std::array<u32, 128> input{};
    input[3] = 0x3f800000;
    for (u32 r = 1; r < 32; ++r)
        for (u32 lane = 0; lane < 4; ++lane)
            input[r * 4 + lane] = 0xdeadbeefu ^ (r * 0x1020304u) ^ (lane * 0x12345678u);
    for (u32 vu = 0; vu < 2; ++vu)
    {
        EmuCoreXOracleSetVUIR(vu, 0);
        for (bool rotate : {false, true})
        for (u32 mask = 1; mask < 16; ++mask)
        for (bool alias : {false, true})
        {
            const u32 fs = alias ? 2 : 1;
            std::vector<u32> program = {
                move(2, 20, 15), NOP_U, // Cache the whole destination before its lane write.
                move(fs, 2, mask, rotate), NOP_U,
                NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
            auto expected = input;
            for (u32 lane = 0; lane < 4; ++lane)
            {
                expected[80 + lane] = input[8 + lane];
                if (mask & (8u >> lane))
                    expected[8 + lane] = input[fs * 4 + ((lane + static_cast<u32>(rotate)) & 3)];
            }
            const auto actual = RunVU(vu, true, program, 0x80, 0, 128, false, nullptr, input.data());
            Check(std::memcmp(actual.regs.VF, expected.data(), sizeof(expected)) == 0,
                "local MOVE/MR32 preserves exact all-VF lane bits with cached destination and aliasing");
            Compare(RunVU(vu, false, program, 0x80, 0, 128, false, nullptr, input.data()), actual,
                "local MOVE/MR32 interpreter parity with cached destination");
        }

        // Dirty memory high halves are architectural sentinels: JIT VI
        // writes touch only the low halfword, just as the interpreter does.
        std::array<u32, 16> vi{};
        for (u32 r = 1; r < 16; ++r) vi[r] = 0xa5a50000u | ((r * 0x9e37u) & 0xffffu);
        constexpr std::array<u32, 4> ops = {0x30, 0x31, 0x34, 0x35};
        for (u32 op : ops)
        for (u32 dst : {1u, 2u, 3u})
        {
            const u32 low = 0x80000000u | op | (1u << 11) | (2u << 16) | (dst << 6);
            const std::vector<u32> program = {low, NOP_U,
                NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
            const auto actual = RunVU(vu, true, program, 0x80, 0, 128, false, nullptr, nullptr, nullptr, vi.data());
            u32 value = op == 0x30 ? vi[1] + vi[2] : op == 0x31 ? vi[1] - vi[2] :
                op == 0x34 ? vi[1] & vi[2] : vi[1] | vi[2];
            Check(actual.regs.VI[dst].UL == ((vi[dst] & 0xffff0000u) | (value & 0xffffu)),
                "local VI arithmetic preserves dirty memory upper halfword across destination aliases");
            Compare(RunVU(vu, false, program, 0x80, 0, 128, false, nullptr, nullptr, nullptr, vi.data()), actual,
                "local VI arithmetic interpreter parity across source/destination aliases");
        }
        // Normal finite operands make exact FMAC results independent of host
        // rounding while the matrix still exercises all clamp and flag paths.
        std::array<u32, 128> finite{};
        finite[3] = 0x3f800000;
        for (u32 r = 1; r < 32; ++r)
            for (u32 lane = 0; lane < 4; ++lane)
                finite[4 * r + lane] = r == 2 ? ((lane & 1) ? 0xc0800000u : 0x40400000u) :
                    ((lane & 1) ? 0xc0000000u : 0x3f800000u);
        constexpr std::array<u32, 4> acc = {0x40800000, 0x40800000, 0x40800000, 0x40800000};
        const auto saved_recompiler = EmuConfig.Cpu.Recompiler;
        const auto saved_fpcr = vu ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
        const auto saved_ee_fpcr = EmuConfig.Cpu.FPUFPCR;
        for (u32 clamp = 0; clamp < 4; ++clamp)
        for (FPRoundMode rounding : {FPRoundMode::Nearest, FPRoundMode::PositiveInfinity,
            FPRoundMode::NegativeInfinity, FPRoundMode::ChopZero})
        {
            if (vu)
            {
                EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
                EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
                EmuConfig.Cpu.Recompiler.vu1SignOverflow = clamp >= 3;
            }
            else
            {
                EmuConfig.Cpu.Recompiler.vu0Overflow = clamp != 0;
                EmuConfig.Cpu.Recompiler.vu0ExtraOverflow = clamp >= 2;
                EmuConfig.Cpu.Recompiler.vu0SignOverflow = clamp >= 3;
            }
            auto& fpcr = vu ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
            fpcr.SetRoundMode(rounding);
            EmuConfig.Cpu.FPUFPCR = fpcr;
            for (u32 mask : {1u, 2u, 4u, 8u, 14u, 15u})
            for (u32 op : {0x28u, 0x2cu, 0x2au}) // ADD/SUB/MUL
            {
                const u32 upper = op | (1u << 11) | (2u << 16) | (3u << 6) | (mask << 21);
                const std::vector<u32> program = {move(1, 20, 15), NOP_U,
                    move(2, 21, 15), NOP_U, NOP_L, upper,
                    NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
                const u64 natural_before = EmuCoreXOracleVULocalNaturalEmits();
                const auto actual = RunVU(vu, true, program, 0x80, 0, 128, false, nullptr, finite.data(), acc.data());
                if (vu == 1 && clamp < 2 && (mask == 1 || mask == 2 || mask == 4))
                    Check(EmuCoreXOracleVULocalNaturalEmits() > natural_before,
                        "local one-lane VU1 FMAC emits arithmetic in its natural lane below extra clamp mode");
                const auto expected = RunVU(vu, false, program, 0x80, 0, 128, false, nullptr, finite.data(), acc.data());
                Compare(expected, actual, "local FMAC all lane/clamp/rounding combinations preserve flags and architectural state");
                bool bits = true;
                for (u32 lane = 0; lane < 4; ++lane)
                    bits &= actual.regs.VF[3].UL[lane] == ((mask & (8u >> lane)) ?
                        (op == 0x28 ? ((lane & 1) ? 0xc0c00000u : 0x40800000u) :
                         op == 0x2c ? ((lane & 1) ? 0x40000000u : 0xc0000000u) :
                             ((lane & 1) ? 0x41000000u : 0x40400000u)) : finite[12 + lane]);
                Check(bits, "local FMAC finite result lanes match independent expected IEEE words");
            }
        }
        EmuConfig.Cpu.Recompiler = saved_recompiler;
        (vu ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR) = saved_fpcr;
        EmuConfig.Cpu.FPUFPCR = saved_ee_fpcr;
        EmuCoreXOracleSetVUIR(vu, -1);
    }
}
