// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// UNWIRED production-route RED draft. Include after the bound MAX/MINI oracle
// in jit_oracle.cpp's anonymous namespace; call VUSharedMaxMinIntegrationTests.
// A zero-return counter stub makes missing common-IR dispatch observable even
// when the legacy fallback computes every architectural result correctly.
static u32 VuMaxMinExpected(u32 a, u32 b, bool maximum);
extern "C" u64 EmuCoreXOracleVUIRMaxMinInstructions(u32 vu);

void VUSharedMaxMinIntegrationTests()
{
    const auto upper = [](bool maximum, u32 fs, u32 ft, u32 fd, u32 mask) {
        return (maximum ? 0x2bu : 0x2fu) | (fd << 6) | (fs << 11) |
            (ft << 16) | ((mask & 15u) << 21);
    };
    const auto move = [](u32 fs, u32 ft, u32 mask) {
        return 0x8000033cu | (fs << 11) | (ft << 16) | ((mask & 15u) << 21);
    };
    const auto mr32 = [](u32 fs, u32 ft, u32 mask) {
        return 0x8000033du | (fs << 11) | (ft << 16) | ((mask & 15u) << 21);
    };
    const auto single = [&](bool maximum, u32 fs, u32 ft, u32 fd, u32 mask) {
        return std::vector<u32>{NOP_L, upper(maximum, fs, ft, fd, mask),
            NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
    };
    using VfWords = std::array<u32, 128>;
    const auto model = [](VfWords& expected, const VfWords& input,
        bool maximum, u32 fs, u32 ft, u32 fd, u32 mask) {
        if (fd == 0) return;
        for (u32 lane = 0; lane < 4; ++lane)
            if (mask & (8u >> lane))
                expected[fd * 4 + lane] = VuMaxMinExpected(
                    input[fs * 4 + lane], input[ft * 4 + lane], maximum);
    };
    constexpr std::array<u32, 24> edges = {
        0, 0x80000000u, 1, 0x80000001u, 0x007fffffu, 0x807fffffu,
        0x00800000u, 0x80800000u, 0x3f800000u, 0xbf800000u,
        0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
        0x7f800001u, 0xff800001u, 0x7fa12345u, 0xffa54321u,
        0x7fc12345u, 0xffc54321u, 0x7fffffffu, 0xffffffffu,
        0x40000000u, 0xc0000000u};
    VfWords initial{};
    initial[3] = 0x3f800000u; // Canonical VF0 = (0,0,0,1).
    for (u32 reg = 1; reg < 32; ++reg)
        for (u32 lane = 0; lane < 4; ++lane)
            initial[reg * 4 + lane] = 0x5a13579bu ^ (reg * 0x01020408u) ^ (lane * 0x137abdeu);
    constexpr std::array<u32, 4> acc = {0x3fa00000u, 0x40200000u, 0xc0700000u, 0x40900000u};
    std::array<u32, 16> vi{};
    for (u32 reg = 1; reg < 16; ++reg) vi[reg] = 0x1200u + reg;
    vi[1] = vi[2]; // Taken IBEQ delay-slot fixture.

    for (u32 vu = 0; vu < 2; ++vu)
    {
        FPControlRegister& core_fpcr = vu ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR;
        const FPControlRegister saved_core = core_fpcr, saved_ee = EmuConfig.Cpu.FPUFPCR;
        core_fpcr.SetRoundMode(FPRoundMode::Nearest);
        EmuConfig.Cpu.FPUFPCR = core_fpcr;
        const auto run_case = [&](const std::vector<u32>& program, const VfWords& input,
            const VfWords& expected, u32 admitted, const char* label, bool interpreter = true) {
            std::printf("MAXMINCASE core=%u %s\n", vu, label);
            EmuCoreXOracleSetVUIR(vu, 0);
            const u64 before = EmuCoreXOracleVUIRMaxMinInstructions(vu);
            const u64 other_before = EmuCoreXOracleVUIRMaxMinInstructions(vu ^ 1u);
            Snapshot interp{};
            if (interpreter)
                interp = RunVU(vu, false, program, 0x80, 0, 128, false,
                    nullptr, input.data(), acc.data(), vi.data());
            const Snapshot legacy = RunVU(vu, true, program, 0x80, 0, 128, false,
                nullptr, input.data(), acc.data(), vi.data());
            Check(EmuCoreXOracleVUIRMaxMinInstructions(vu) == before,
                "forced legacy MAX/MINI leaves the shared compile counter unchanged");
            EmuCoreXOracleSetVUIR(vu, 1);
            const Snapshot shared = RunVU(vu, true, program, 0x80, 0, 128, false,
                nullptr, input.data(), acc.data(), vi.data());
            if (interpreter) Compare(interp, legacy, "MAX/MINI interpreter/legacy full VF/VI/ACC/memory parity");
            Compare(legacy, shared, "MAX/MINI shared/legacy full VF/VI/ACC/memory parity");
            Check((!interpreter || std::memcmp(interp.regs.VF, expected.data(), sizeof(expected)) == 0) &&
                std::memcmp(legacy.regs.VF, expected.data(), sizeof(expected)) == 0 &&
                std::memcmp(shared.regs.VF, expected.data(), sizeof(expected)) == 0,
                "MAX/MINI full VF array matches independent raw ordering and masked-lane expectations");
            Check(shared.regs.cycle == legacy.regs.cycle && !legacy.fallback && !shared.fallback,
                "MAX/MINI stays in the native dispatcher and preserves exact legacy cycles");
            Check(EmuCoreXOracleVUIRMaxMinInstructions(vu) == before + admitted &&
                EmuCoreXOracleVUIRMaxMinInstructions(vu ^ 1u) == other_before,
                "MAX/MINI production admission is exact and isolated to the executing VU core");
        };

        // Both source/destination aliases, repeated operands, and canonical VF0
        // are exercised at every nonempty mask on both production VU cores.
        for (bool maximum : {false, true})
        for (u32 mask = 1; mask < 16; ++mask)
        for (const std::array<u32, 3> regs : {std::array<u32, 3>{7, 8, 9}, {7, 8, 7},
            {7, 8, 8}, {7, 7, 9}, {7, 7, 7}, {0, 8, 9}, {7, 0, 9}, {0, 0, 9}})
        {
            VfWords input = initial;
            for (u32 lane = 0; lane < 4; ++lane)
            {
                input[7 * 4 + lane] = edges[(mask + lane * 5) % edges.size()];
                input[8 * 4 + lane] = edges[(mask * 3 + lane * 7) % edges.size()];
            }
            VfWords expected = input;
            model(expected, input, maximum, regs[0], regs[1], regs[2], mask);
            run_case(single(maximum, regs[0], regs[1], regs[2], mask), input, expected, 1,
                "register form masks/aliases/VF0");
        }
        for (bool maximum : {false, true})
        {
            run_case(single(maximum, 7, 8, 0, 15), initial, initial, 0, "VF0 destination fallback");
            run_case(single(maximum, 7, 8, 9, 0), initial, initial, 0, "empty mask fallback");
        }

        // Integer ordering must ignore rounding/FZ/DN, including signed NaNs.
        for (FPRoundMode round : {FPRoundMode::Nearest, FPRoundMode::PositiveInfinity,
            FPRoundMode::NegativeInfinity, FPRoundMode::ChopZero})
        for (bool flush : {false, true})
        for (bool dn : {false, true})
        {
            core_fpcr.SetRoundMode(round);
            core_fpcr.SetFlushToZero(flush);
            core_fpcr.SetDenormalsAreZero(flush);
            if (dn) core_fpcr.bitmask |= (1ull << 25);
            else core_fpcr.bitmask &= ~(1ull << 25);
            EmuConfig.Cpu.FPUFPCR = core_fpcr;
            VfWords input = initial;
            constexpr std::array<u32, 4> a = {0xff812345u, 0x7fc12345u, 1, 0x80000000u};
            constexpr std::array<u32, 4> b = {0xff800000u, 0x7f800000u, 0, 0};
            for (u32 lane = 0; lane < 4; ++lane) { input[28 + lane] = a[lane]; input[32 + lane] = b[lane]; }
            for (bool maximum : {false, true})
            {
                VfWords expected = input;
                model(expected, input, maximum, 7, 8, 9, 15);
                run_case(single(maximum, 7, 8, 9, 15), input, expected, 1, "FPCR rounding/FZ/DN raw ordering");
            }
        }
        core_fpcr = saved_core;
        EmuConfig.Cpu.FPUFPCR = saved_ee;

        for (bool maximum : {false, true})
        {
            // Lower MOVE produces a source for the next upper instruction.
            VfWords expected = initial;
            for (u32 lane = 0; lane < 4; ++lane) expected[32 + lane] = initial[28 + lane];
            const VfWords after_move = expected;
            model(expected, after_move, maximum, 8, 6, 9, 15);
            run_case({move(7, 8, 15), NOP_U, NOP_L, upper(maximum, 8, 6, 9, 15),
                NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1, "lower producer then upper consumer");

            // Same-pair lower reads the OLD destination despite the upper write.
            expected = initial;
            for (u32 lane = 0; lane < 4; ++lane) expected[40 + lane] = initial[36 + lane];
            model(expected, initial, maximum, 7, 8, 9, 5);
            run_case({move(9, 10, 15), upper(maximum, 7, 8, 9, 5), NOP_L,
                NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1, "same-pair lower reads old upper destination");

            // Same-pair lower writes a source only after the upper samples it.
            expected = initial;
            for (u32 lane = 0; lane < 4; ++lane) expected[28 + lane] = initial[24 + lane];
            model(expected, initial, maximum, 7, 8, 9, 10);
            run_case({move(6, 7, 15), upper(maximum, 7, 8, 9, 10), NOP_L,
                NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1, "same-pair upper reads old lower destination");

            // Existing microVU semantics suppress the whole lower VF write on
            // same-register WAW; the unselected upper lanes retain their seed.
            expected = initial;
            model(expected, initial, maximum, 7, 8, 9, 5);
            run_case({mr32(6, 9, 15), upper(maximum, 7, 8, 9, 5), NOP_L,
                NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1, "same-pair masked upper WAW priority", false);

            expected = initial;
            model(expected, initial, maximum, 7, 8, 9, 10);
            const u32 branch = (0x28u << 25) | (1u << 11) | (2u << 16) | 2u;
            run_case({branch, NOP_U, NOP_L, upper(maximum, 7, 8, 9, 10), NOP_L,
                NOP_U, NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U}, initial, expected, 1,
                "taken branch delay slot");
        }

        // Generate nonzero architectural flags, drain the flag pipeline, then
        // resume each engine into MAX/MINI. Auxiliary JIT flag caches are not
        // substituted for the canonical VI[MAC/STATUS/CLIP] architectural state.
        for (bool maximum : {false, true})
        {
            VfWords input = initial;
            constexpr std::array<u32, 4> a = {0x40000000u, 0xc0000000u, 0x40000000u, 0xc0000000u};
            constexpr std::array<u32, 4> b = {0xbf800000u, 0xbf800000u, 0xbf800000u, 0x3f800000u};
            for (u32 lane = 0; lane < 4; ++lane) { input[28 + lane] = a[lane]; input[32 + lane] = b[lane]; }
            const u32 add = 0x28u | (12u << 6) | (7u << 11) | (8u << 16) | (15u << 21);
            const u32 clip = 0x3fu | (7u << 6) | (7u << 11) | (8u << 16);
            const std::vector<u32> seed_program = {NOP_L, add, NOP_L, clip,
                NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
                NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
            VfWords expected = input;
            constexpr std::array<u32, 4> sum = {0x3f800000u, 0xc0400000u, 0x3f800000u, 0xbf800000u};
            for (u32 lane = 0; lane < 4; ++lane) expected[48 + lane] = sum[lane];
            model(expected, input, maximum, 7, 8, 10, 15);
            std::array<Snapshot, 3> results{};
            for (u32 route = 0; route < 3; ++route)
            {
                EmuCoreXOracleSetVUIR(vu, route == 2 ? 1 : 0);
                const u64 before = EmuCoreXOracleVUIRMaxMinInstructions(vu);
                const Snapshot seeded = RunVU(vu, route != 0, seed_program, 0x80, 0, 128,
                    false, nullptr, input.data(), acc.data(), vi.data());
                Check(seeded.regs.VI[REG_MAC_FLAG].UL != 0 && seeded.regs.VI[REG_STATUS_FLAG].UL != 0 &&
                    seeded.regs.VI[REG_CLIP_FLAG].UL != 0 && std::memcmp(&seeded.regs.ACC, acc.data(), sizeof(acc)) == 0,
                    "MAX/MINI fixture seeds nonzero canonical VI flags and ACC");
                results[route] = RunVU(vu, route != 0, single(maximum, 7, 8, 10, 15), 0x100, 0, 128, true);
                const auto& result = results[route];
                Check(result.regs.VI[REG_MAC_FLAG].UL == seeded.regs.VI[REG_MAC_FLAG].UL &&
                    result.regs.VI[REG_STATUS_FLAG].UL == seeded.regs.VI[REG_STATUS_FLAG].UL &&
                    result.regs.VI[REG_CLIP_FLAG].UL == seeded.regs.VI[REG_CLIP_FLAG].UL &&
                    std::memcmp(&result.regs.ACC, acc.data(), sizeof(acc)) == 0 &&
                    std::memcmp(result.regs.VF, expected.data(), sizeof(expected)) == 0,
                    "MAX/MINI preserves seeded canonical flags and ACC with independent full VF results");
                Check(EmuCoreXOracleVUIRMaxMinInstructions(vu) == before + (route == 2 ? 1u : 0u),
                    "seeded MAX/MINI admission follows the selected production route");
            }
            Compare(results[0], results[1], "seeded MAX/MINI interpreter/legacy full state parity");
            Compare(results[1], results[2], "seeded MAX/MINI shared/legacy full state parity");
            Check(results[1].regs.cycle == results[2].regs.cycle,
                "seeded MAX/MINI preserves exact legacy cycles");
        }
        EmuCoreXOracleSetVUIR(vu, -1);
        core_fpcr = saved_core;
        EmuConfig.Cpu.FPUFPCR = saved_ee;
    }
}
