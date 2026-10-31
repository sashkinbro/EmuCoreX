// SPDX-License-Identifier: GPL-3.0+
// Include after the persistence oracle's no-reset execution helper.
void RunVuAotLifecycleTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate AOT lifecycle memory"); return; }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache;
    EmuFolders::Cache.clear();
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const std::vector<u32> code{
        (0x20u << 25) | 3u, add, NOP_L, NOP_U,
        NOP_L, NOP_U, NOP_L, NOP_U,
        NOP_L, add | (1u << 30), NOP_L, NOP_U};
    std::array<u32, 12> baselineRings{}, firstRings{}, workerRings{};
    const auto baseline = RunPersistedVU(1, code, 0x80, 128, true, baselineRings);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::ForceSynchronous(false);
    mvu_aot::CountEntries(true);
    mvu_aot::SetTierUpRuns(1);
    mvu_aot::HoldWorker(true);
    const u64 entered = mvu_aot::EntryCount();
    const auto queued = RunPersistedVU(1, code, 0x80, 128, true, firstRings);
    Check(mvu_aot::PendingCount() == 1 && mvu_aot::EntryCount() == entered,
        "held worker queues region while baseline executes");
    Compare(baseline, queued, "pending worker preserves baseline state");
    mvu_aot::HoldWorker(false);
    mvu_aot::WaitWorker();
    const u64 published = mvu_aot::RegionCount();
    const auto completed = RunPersistedVU(1, code, 0x80, 128, false, workerRings);
    std::printf("WORKER_COUNTS published=%llu->%llu entered=%llu->%llu pending=%u\n",
        static_cast<unsigned long long>(published), static_cast<unsigned long long>(mvu_aot::RegionCount()),
        static_cast<unsigned long long>(entered), static_cast<unsigned long long>(mvu_aot::EntryCount()),
        mvu_aot::PendingCount());
    Check(mvu_aot::RegionCount() > published && mvu_aot::EntryCount() > entered,
        "owner publishes completed worker region and executes it");
    Compare(baseline, completed, "background region preserves baseline state");
    Check(baseline.regs.cycle == completed.regs.cycle && baselineRings == workerRings &&
        baseline.regs.pending_q == completed.regs.pending_q &&
        baseline.regs.pending_p == completed.regs.pending_p,
        "background region preserves exact cycles, PQ and internal flag rings");

    mvu_aot::HoldWorker(true);
    RunPersistedVU(1, code, 0x80, 128, true, firstRings);
    Check(mvu_aot::PendingCount() == 1, "reset fixture has queued region");
    const u64 beforeReset = mvu_aot::RegionCount();
    CpuMicroVU1.Reset();
    mvu_aot::HoldWorker(false);
    mvu_aot::WaitWorker();
    Check(mvu_aot::PendingCount() == 0 && mvu_aot::RegionCount() == beforeReset,
        "reset cancels queued work before deleting its program");
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    CpuMicroVU1.Reset();
    mvu_aot::CountEntries(false);
    mvu_aot::SetTierUpRuns(200);
    EmuFolders::Cache = savedCache;
}
