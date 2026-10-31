// SPDX-License-Identifier: GPL-3.0+
#include "jit_oracle_vuaot_retier.inl"
#include "jit_oracle_vuaot_jump_fast.inl"
#include "jit_oracle_vuaot_body.inl"
#include "jit_oracle_vuaot_sticky.inl"
void RunVuAotJumpTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate AOT indirect test memory"); return; }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache;
    EmuFolders::Cache.clear();
    std::vector<u32> code;
    for (u32 pc = 0x80; pc < 0x130; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    code[0] = (8u << 25) | (1u << 16) | 0x20u; // vi1 = 0x100/8
    code[2] = (0x24u << 25) | (1u << 11); // JR vi1
    code[(0x100 - 0x80) / 4] = (0x20u << 25) | 3u;
    code[(0x100 - 0x80) / 4 + 1] = add;
    code[(0x120 - 0x80) / 4 + 1] = NOP_U | (1u << 30);
    auto replaced = code;
    replaced[(0x100 - 0x80) / 4 + 1] = (add & ~0x3fu) | 0x2cu; // SUB
    std::array<u32, 12> expectedRings{}, changedRings{}, actualRings{};
    const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
    const auto changed = RunPersistedVU(1, replaced, 0x80, 128, true, changedRings);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::ForceSynchronous(false);
    mvu_aot::CountEntries(true);
    mvu_aot::SetTierUpRuns(200);
    mvu_aot::SetJumpTierUpRuns(32);
    mvu_aot::HoldWorker(true);
    const auto before = mvu_aot::GetStats();
    const u64 entered = mvu_aot::EntryCount();
    for (u32 run = 0; run < 32; ++run)
    {
        if (run) CpuMicroVU1.Clear(0x100, 0x30);
        RunPersistedVU(1, code, 0x80, 128, run == 0, actualRings);
        if (run == 30) Check(mvu_aot::PendingCount() == 0,
            "JR-only overlay waits for its 32-entry threshold");
    }
    Check(mvu_aot::GetStats().jumpEntries - before.jumpEntries == 32 &&
        mvu_aot::PendingCount() == 1 && mvu_aot::EntryCount() == entered,
        "32 slow JR entries queue the overlay independently of stub dispatches");
    mvu_aot::HoldWorker(false);
    mvu_aot::WaitWorker();
    CpuMicroVU1.Clear(0x100, 0x30);
    const auto actual = RunPersistedVU(1, code, 0x80, 128, false, actualRings);
    Check(mvu_aot::EntryCount() > entered && mvu_aot::GetStats().jumpTierUps > before.jumpTierUps,
        "JR slow path publishes and enters its hot overlay");
    Compare(expected, actual, "JR-tiered overlay preserves architectural state");
    Check(expected.regs.cycle == actual.regs.cycle && expectedRings == actualRings &&
        expected.regs.pending_p == actual.regs.pending_p && expected.regs.pending_q == actual.regs.pending_q,
        "JR-tiered overlay preserves exact cycles, PQ and internal rings");

    // The stub's recorded target belongs to another program. It must never
    // become an unguarded direct edge in the stub's region.
    mvu_aot::SetTierUpRuns(34);
    CpuMicroVU1.Clear(0x100, 0x30);
    RunPersistedVU(1, code, 0x80, 128, false, actualRings);
    mvu_aot::WaitWorker();
    CpuMicroVU1.Clear(0x100, 0x30);
    RunPersistedVU(1, code, 0x80, 128, false, actualRings);
    const auto profiled = mvu_aot::GetStats();
    Check(profiled.crossProgramTargets > before.crossProgramTargets &&
        profiled.targetsRecorded == before.targetsRecorded,
        "target harvest excludes cross-program jump-cache entries");
    const u64 beforeUploadCompiles = mVUPersist::GetBlockCompileCount(1);
    CpuMicroVU1.Clear(0x100, 0x30);
    const auto uploaded = RunPersistedVU(1, replaced, 0x80, 128, false, actualRings);
    Check(mVUPersist::GetBlockCompileCount(1) > beforeUploadCompiles,
        "micro upload compiles a replacement overlay before entering it");
    Compare(changed, uploaded, "JR program-identity guard executes the replacement microcode");
    Check(changed.regs.cycle == uploaded.regs.cycle && changedRings == actualRings,
        "replacement overlay retains exact cycles and flags");
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    CpuMicroVU1.Reset();
    mvu_aot::CountEntries(false);
    mvu_aot::SetTierUpRuns(200);
    mvu_aot::SetJumpTierUpRuns(32);
    RunVuAotRetierTests();
    RunVuAotJumpFastTests();
    RunVuAotWarmBeforePublishTests();
    RunVuAotBodyTests();
    RunVuAotStickyTests();
    EmuFolders::Cache = savedCache;
}
