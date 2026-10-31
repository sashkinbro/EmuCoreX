// SPDX-License-Identifier: GPL-3.0+
void RunVuAotBodyTests()
{
    for (u32 clamp = 0; clamp < 4; ++clamp)
        for (u32 budget : {1u, 128u})
            for (u32 op : {0x28u, 0x2cu, 0x2au, 0x29u, 0x2du})
            {
                EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
                EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
                EmuConfig.Cpu.Recompiler.vu1SignOverflow = clamp >= 3;
                const u32 upper = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | op;
                const std::vector<u32> code{NOP_L, upper, NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
                std::array<u32, 12> baselineRings{}, rings{};
                EmuConfig.Cpu.Recompiler.EnableVUAot = false;
                const auto expected = RunPersistedVU(1, code, 0x80, budget, true, baselineRings);
                EmuConfig.Cpu.Recompiler.EnableVUAot = true;
                mvu_aot::ForceSynchronous(true);
                const auto entered = mvu_aot::EntryCount();
                const auto ended = mvu_aot::NativeEndCount();
                const auto actual = RunPersistedVU(1, code, 0x80, budget, true, rings);
                Check(mvu_aot::EntryCount() > entered && (budget == 1 || mvu_aot::NativeEndCount() > ended),
                    "single-block BodyTier enters generated code and executes its native end");
                Compare(expected, actual, "single-block BodyTier preserves architecture");
                Check(expected.regs.cycle == actual.regs.cycle && baselineRings == rings &&
                    expected.regs.pending_p == actual.regs.pending_p && expected.regs.pending_q == actual.regs.pending_q,
                    "single-block BodyTier preserves exact cycles, PQ and flag rings");
            }
    for (u32 clamp = 0; clamp < 4; ++clamp)
        for (bool region : {false, true})
            for (u32 immediate : {0x7fffffffu, 0x7fc12345u, 0xff800000u, 1u, 0x80000001u, 0x3f800000u})
            {
                EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
                EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
                EmuConfig.Cpu.Recompiler.vu1SignOverflow = clamp >= 3;
                const u32 addI = (15u << 21) | (1u << 11) | (3u << 6) | 0x22u;
                std::vector<u32> code{immediate, NOP_U | 0x80000000u};
                if (region) code.insert(code.end(), {(0x20u << 25) | 2u, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U});
                code.insert(code.end(), {NOP_L, addI, NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U});
                std::array<u32, 12> expectedRings{}, rings{};
                EmuConfig.Cpu.Recompiler.EnableVUAot = false;
                const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
                EmuConfig.Cpu.Recompiler.EnableVUAot = true;
                const auto entered = mvu_aot::EntryCount();
                const auto actual = RunPersistedVU(1, code, 0x80, 128, true, rings);
                Check(mvu_aot::EntryCount() > entered, "I-bit test executes generated body or region");
                Compare(expected, actual, "I-bit immediate clamping matches local baseline in every mode");
                Check(expected.regs.cycle == actual.regs.cycle && expectedRings == rings,
                    "I-bit immediate execution preserves exact cycles and rings");
            }
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    CpuMicroVU1.Reset();
    mvu_aot::ForceSynchronous(false);
}
