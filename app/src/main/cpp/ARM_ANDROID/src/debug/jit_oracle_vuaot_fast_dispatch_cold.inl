// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Fresh-ASLR native importer calls this only after its original first-reader
// held-worker/zero-compile/state assertions; initial cache proof is preserved.
void CheckVuAotWarmImportedChild(const std::vector<u32>& code, const Snapshot& expected,
    const std::array<u32, 12>& expectedRings)
{
    std::array<u32, 12> rings{};
    // Allow actual bounded remembered/saved work to run before measuring.
    // Nothing is manually removed from a root/plan/ready container.
    mvu_aot::HoldWorker(false);
    const auto drainBefore = mvu_aot::GetStats();
    const auto drainCompiles = mVUPersist::GetBlockCompileCount(1);
    for (u32 run = 0; run < 16; ++run)
    {
        mvu_aot::WaitWorker(); rings = {};
        RunPersistedVU(1, code, 0x80, 128, false, rings);
    }
    mvu_aot::WaitWorker();
    rings = {}; RunPersistedVU(1, code, 0x80, 128, false, rings);
    Check(mvu_aot::PendingCount() == 0 && mvu_aot::ReadyCountForTest() == 0,
        "fresh-ASLR native warm measurement begins after actual bounded deferred work drains");
    mvu_aot::HoldWorker(true);
    const auto before = mvu_aot::GetStats();
    const auto compiles = mVUPersist::GetBlockCompileCount(1);
    const auto entered = mvu_aot::NativeRegionEntryCount(0xc0), ends = mvu_aot::NativeEndCount();
    for (u32 run = 0; run < 4; ++run)
    {
        rings = {};
        const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings);
        Compare(expected, actual, "warm fresh-ASLR native importer preserves complete architecture");
        Check(actual.regs.cycle == expected.regs.cycle && rings == expectedRings &&
            actual.regs.pending_p == expected.regs.pending_p && actual.regs.pending_q == expected.regs.pending_q &&
            VuCompileBranchOracle::Model(VuCompileBranchOracle::boundaries[0], actual) &&
            VuCompileBranchOracle::DistinctPQ(actual),
            "warm imported native regions preserve independent goldens exact cycles actual PQ and all rings");
    }
    const auto after = mvu_aot::GetStats();
    Check(after.dispatchFastEntries == before.dispatchFastEntries + 4 &&
        after.dispatchSlowKeys == before.dispatchSlowKeys,
        "fresh-ASLR native import regains four real warm returns without slow key copies");
    Check(mVUPersist::GetBlockCompileCount(1) == compiles &&
        after.automaticJobs == before.automaticJobs && after.savedPlanJobs == before.savedPlanJobs &&
        mvu_aot::PendingCount() == 0 && mvu_aot::ReadyCountForTest() == 0,
        "held-worker warm importer uses no additional baseline compilation or deferred compiler jobs");
    Check(mvu_aot::NativeRegionEntryCount(0xc0) == entered + 4 && mvu_aot::NativeEndCount() == ends + 4,
        "all four measured imported calls actually enter saved C and finish its native end");
    std::printf("VU_FAST_IMPORTED slow=%llu fast=%llu drain_jobs=%llu drain_compiles=%llu\n",
        static_cast<unsigned long long>(after.dispatchSlowKeys - before.dispatchSlowKeys),
        static_cast<unsigned long long>(after.dispatchFastEntries - before.dispatchFastEntries),
        static_cast<unsigned long long>(before.automaticJobs - drainBefore.automaticJobs),
        static_cast<unsigned long long>(compiles - drainCompiles));
    mvu_aot::HoldWorker(false);
}
