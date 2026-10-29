// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetVULocalNatural(bool enabled);
extern "C" void EmuCoreXOracleVULocalFlagRings(u32 index, u32* words, bool seed);

void VULocalNaturalParityTests()
{
    const auto saved_config = EmuConfig;
    EmuCoreXOracleSetVUIR(1, 0);
    constexpr std::array<u32, 24> edges = {0, 0x80000000, 1, 0x80000001,
        0x007fffff, 0x807fffff, 0x00800000, 0x80800000,
        0x3f7fffff, 0x3f800000, 0x3f800001, 0xbf800000,
        0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
        0x7fc12345, 0xffc54321, 0x7fa00001, 0xffa00001,
        0x40000000, 0xc0000000, 0x7fffffff, 0xffffffff};
    // Vector, x/w broadcast, I and Q shapes; ACC opcodes include their
    // FD subtable selector and must not receive an Fd field.
    constexpr std::array<u32, 40> opcodes = {0x28,0x2c,0x2a,0x29,0x2d,
        0x00,0x04,0x18,0x08,0x0c, 0x03,0x07,0x1b,0x0b,0x0f,
        0x22,0x26,0x1e,0x23,0x27, 0x20,0x24,0x1c,0x21,0x25,
        0x2bc,0x2fc,0x2be,0x2bd,0x2fd, 0x3f,0x7f,0x1bf,0xbf,0xff,
        0x23e,0x27e,0x1fe,0x23f,0x27f};
    u64 baseline_bytes = 0, natural_bytes = 0;
    const auto compiled_bytes = [] {
        std::vector<JitBlockProfile> blocks;
        VU1_JitGetBlockProfiles(blocks);
        u64 bytes = 0;
        for (const auto& block : blocks) bytes += block.host_size;
        return bytes;
    };
    u32 cases = 0;
    for (u32 clamp = 0; clamp < 3; ++clamp)
    for (bool flag_hack : {false, true})
    for (bool flush : {false, true})
    for (FPRoundMode rounding : {FPRoundMode::Nearest, FPRoundMode::ChopZero})
    {
        EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
        EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
        EmuConfig.Cpu.Recompiler.vu1SignOverflow = false;
        EmuConfig.Speedhacks.vuFlagHack = flag_hack;
        EmuConfig.Cpu.VU1FPCR.SetRoundMode(rounding);
        EmuConfig.Cpu.VU1FPCR.SetFlushToZero(flush);
        EmuConfig.Cpu.FPUFPCR = EmuConfig.Cpu.VU1FPCR;
        for (u32 opcode : opcodes)
        for (u32 sample = 0; sample < 3; ++sample)
        {
            std::array<u32, 128> input{};
            input[3] = 0x3f800000;
            for (u32 reg = 1; reg < 32; ++reg)
                for (u32 lane = 0; lane < 4; ++lane)
                    input[4 * reg + lane] = edges[(sample * 8 + reg + lane * 5) % edges.size()];
            input[120] = 0x40000000; input[124] = 0x3f800000; // DIV -> Q=2.
            std::array<u32, 4> acc{};
            for (u32 lane = 0; lane < 4; ++lane) acc[lane] = edges[(sample * 7 + lane * 3) % edges.size()];
            const u32 fd = sample == 0 ? 3 : sample; // Separate, Fs alias, Ft alias.
            std::vector<u32> program = {0x800003bcu | (30u << 11) | (31u << 16), NOP_U,
                0x800003bfu, NOP_U, 0x3f800001u, NOP_U | 0x80000000u};
            for (u32 mask : {4u, 2u, 1u})
            {
                const u32 upper = opcode | ((opcode < 0x3c ? fd : 0) << 6) |
                    (1u << 11) | (2u << 16) | (mask << 21);
                program.insert(program.end(), {NOP_L, upper});
            }
            for (u32 i = 0; i < 5; ++i) program.insert(program.end(), {NOP_L, NOP_U});
            program.insert(program.end(), {(0x1bu << 25) | (1u << 16), NOP_U,
                (0x17u << 25) | (2u << 16), NOP_U,
                NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
            std::array<u32, 12> old_rings{}, new_rings{};
            EmuCoreXOracleSetVULocalNatural(false);
            EmuCoreXOracleVULocalFlagRings(1, old_rings.data(), true);
            const auto reference = RunVU(1, true, program, 0x80, 0, 128, false, nullptr, input.data(), acc.data());
            baseline_bytes += compiled_bytes();
            EmuCoreXOracleVULocalFlagRings(1, old_rings.data(), false);
            EmuCoreXOracleSetVULocalNatural(true);
            EmuCoreXOracleVULocalFlagRings(1, new_rings.data(), true);
            const u64 natural_before = EmuCoreXOracleVULocalNaturalEmits();
            const auto actual = RunVU(1, true, program, 0x80, 0, 128, false, nullptr, input.data(), acc.data());
            natural_bytes += compiled_bytes();
            if (clamp == 0 && !flag_hack && flush && rounding == FPRoundMode::Nearest && sample == 0)
                Check(EmuCoreXOracleVULocalNaturalEmits() > natural_before,
                    "all eligible VU1 FMAC families emit the natural-lane route");
            EmuCoreXOracleVULocalFlagRings(1, new_rings.data(), false);
            const bool ring_match = old_rings == new_rings;
            if (!ring_match) std::printf("NATURAL_RING op=%x sample=%u clamp=%u flags=%u flush=%u\n",
                opcode, sample, clamp, flag_hack, flush);
            Check(ring_match, "natural VU1 FMAC preserves all four STATUS/MAC/CLIP ring instances");
            if (std::memcmp(reference.regs.VF, actual.regs.VF, sizeof(actual.regs.VF)) ||
                std::memcmp(reference.regs.VI, actual.regs.VI, sizeof(actual.regs.VI)) ||
                std::memcmp(&reference.regs.ACC, &actual.regs.ACC, sizeof(actual.regs.ACC)))
                std::printf("NATURALCASE op=%x sample=%u clamp=%u flags=%u flush=%u round=%u\n",
                    opcode, sample, clamp, flag_hack, flush, static_cast<u32>(rounding));
            Compare(reference, actual, "natural VU1 FMAC matches baseline raw VF/ACC/VI/PQ/flags/memory");
            Check(reference.regs.cycle == actual.regs.cycle, "natural VU1 FMAC preserves baseline cycle accounting");
            ++cases;
        }
    }
    std::printf("VU_LOCAL_NATURAL cases=%u baseline_bytes=%llu natural_bytes=%llu\n", cases,
        static_cast<unsigned long long>(baseline_bytes), static_cast<unsigned long long>(natural_bytes));
    Check(natural_bytes < baseline_bytes, "natural VU1 FMAC reduces aggregate actual compiled host code bytes");
    EmuCoreXOracleSetVULocalNatural(true);
    EmuCoreXOracleSetVUIR(1, -1);
    EmuConfig = saved_config;
}
