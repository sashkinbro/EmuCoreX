// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Real compiler/worker/cache events; no copied implementation of eligibility.
void RunVuAotFastDispatchGuardTests()
{
    std::vector<u32> code;
    for (u32 pc = 0x80; pc <= 0x100; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
    };
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const u32 mul = (15u << 21) | (2u << 16) | (1u << 11) | (4u << 6) | 0x2au;
    put(0x80, 0x8000077eu | (1u << 11), add);
    put(0x88, 0x800003bcu | (1u << 11) | (2u << 16), mul);
    put(0x90, (0x20u << 25) | 3u, add);
    put(0x98, (8u << 25) | (11u << 16) | 0x333u);
    put(0xb0, (8u << 25) | (10u << 16) | 0x222u, add);
    put(0xb8, NOP_L, NOP_U | 0x40000000u);
    // A second, initially unreachable two-node region. The real owner compiler
    // must Remember it after the old PC80 entry has been classified warm.
    put(0xd0, NOP_L, mul); put(0xd8, (0x20u << 25) | 2u);
    put(0xe0, (8u << 25) | (12u << 16) | 0x444u);
    put(0xf0, (8u << 25) | (13u << 16) | 0x555u, add);
    put(0xf8, NOP_L, NOP_U | 0x40000000u);
    std::array<u32, 12> expectedRings{}, childRings{}, rings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot = false; mvu_aot::ForceSynchronous(false);
    const auto child = RunPersistedVU(1, code, 0x80, 128, true, childRings, 0xd0, nullptr, 0, 0x80);
    constexpr u32 budgets[] = {1, 3, 5, 7};
    std::array<Snapshot, std::size(budgets)> budgetExpected;
    std::array<std::array<u32, 12>, std::size(budgets)> budgetRings{};
    for (u32 index = 0; index < std::size(budgets); ++index)
        budgetExpected[index] = RunPersistedVU(1, code, 0x80, budgets[index], true, budgetRings[index]);
    const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
    Check(child.regs.VI[12].UL == 0x444u && child.regs.VI[13].UL == 0x555u &&
        expected.regs.VI[10].UL == 0x222u && expected.regs.VI[11].UL == 0x333u,
        "independent root and unreachable-child delay/leaf goldens identify both real paths");
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::CountEntries(true); mvu_aot::ForceSynchronous(true);
    const auto initialEntries = mvu_aot::NativeRegionEntryCount(0x80);
    RunPersistedVU(1, code, 0x80, 128, true, rings);
    Check(mvu_aot::NativeRegionEntryCount(0x80) > initialEntries,
        "event controls start from a genuine native two-node root");
    mvu_aot::ForceSynchronous(false);
    const auto root = [&] {
        rings = {};
        return RunPersistedVU(1, code, 0x80, 128, false, rings);
    };
    const auto checkRoot = [&](const char* note) {
        const auto actual = root(); Compare(expected, actual, note);
        Check(actual.regs.cycle == expected.regs.cycle && rings == expectedRings &&
            actual.regs.pending_p == expected.regs.pending_p && actual.regs.pending_q == expected.regs.pending_q,
            "event control preserves exact cycles pending PQ and all flag rings");
    };
    for (u32 run = 0; run < 8; ++run) root();
    auto before = mvu_aot::GetStats(); root();
    Check(mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries + 1,
        "a real fast root exists before mode and new-work negative controls");
    mvu_aot::ForceSynchronous(true); before = mvu_aot::GetStats();
    checkRoot("force-sync native execution preserves full architecture");
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries,
        "force-sync mode cannot bypass the dispatcher");
    mvu_aot::ForceSynchronous(false); before = mvu_aot::GetStats(); root(); root();
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries + 1,
        "force mode transition reclassifies once before the next warm return");
    mvu_aot::SetTierUpRuns(1); before = mvu_aot::GetStats(); root(); root();
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries + 1,
        "changed initial-tier threshold invalidates only the first native classification");
    mvu_aot::SetTierUpRuns(200); root();
    mvu_aot::SetJumpTierUpRuns(1); before = mvu_aot::GetStats(); root(); root();
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries + 1,
        "changed jump-tier threshold invalidates only the first native classification");
    mvu_aot::SetJumpTierUpRuns(32); root();
    mvu_aot::CountEntries(false); before = mvu_aot::GetStats(); root(); root();
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 2 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries,
        "an old instrumented native job cannot be reclassified as a new codegen mode without reset");
    mvu_aot::CountEntries(true); root(); before = mvu_aot::GetStats(); root();
    Check(mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries + 1,
        "matching original native instrumentation regains the warm path after slow classification");
    const bool fastCDVD = EmuConfig.Speedhacks.fastCDVD;
    EmuConfig.Speedhacks.fastCDVD = !fastCDVD; before = mvu_aot::GetStats();
    checkRoot("benign option mismatch preserves architecture on the conservative slow path"); root();
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 2 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries,
        "captured option mismatch forbids a warm return even when baseline entry stays identical");
    EmuConfig.Speedhacks.fastCDVD = fastCDVD; root();
    mvu_aot::HoldWorker(true); mvu_aot::TestHoldReadyPublication(true);
    const ScopedGuard releaseEvents([&] {
        mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false); mvu_aot::WaitWorker();
    });
    const u64 owner = mvu_aot::CurrentOwnerForTest();
    const u64 compileBefore = mVUPersist::GetBlockCompileCount(1);
    vuaot::PipeState childKey;
    Check(mvu_aot::TestCompileWarmRoot(0xd0, childKey.bytes) &&
        mvu_aot::CurrentOwnerForTest() == owner && mVUPersist::GetBlockCompileCount(1) > compileBefore,
        "real same-owner baseline compilation queues a fresh remembered child behind the existing warm root");
    before = mvu_aot::GetStats(); checkRoot("held worker does not change root architecture");
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries &&
        mvu_aot::GetStats().automaticJobs == before.automaticJobs + 1 && mvu_aot::PendingCount() == 1,
        "unchanged old entry offers actual automatic-root work while the compiler worker is held");
    before = mvu_aot::GetStats(); root();
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries && mvu_aot::PendingCount() == 1,
        "a real pending held-worker job prevents the warm bypass");
    mvu_aot::HoldWorker(false); mvu_aot::WaitWorker();
    before = mvu_aot::GetStats(); root();
    const auto completed = mvu_aot::GetStats();
    Check(completed.dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        completed.dispatchFastEntries == before.dispatchFastEntries &&
        mvu_aot::PendingCount() == 1 && mvu_aot::ReadyCountForTest() == 1 &&
        completed.automaticJobs == before.automaticJobs + 1 &&
        completed.readyStale == before.readyStale && completed.publishRejects == before.publishRejects,
        "global completion retains accepted F0 ready ownership and legitimately offers the next actual D0 job");
    // Recursive Baseline compiled F0 before D0. The ready F0 job and the next
    // pending D0 job are distinct valid ownership, not a failed collection.
    mvu_aot::WaitWorker(); before = mvu_aot::GetStats(); root();
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries &&
        mvu_aot::PendingCount() == 0 && mvu_aot::ReadyCountForTest() == 2,
        "second real completion drains pending D0 into accepted ready ownership before the isolated ready-only control");
    before = mvu_aot::GetStats(); checkRoot("retained ready native root preserves full architecture");
    Check(mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries && mvu_aot::ReadyCountForTest() > 0,
        "ready work forces slow retry without another worker completion");
    mvu_aot::TestHoldReadyPublication(false);
    for (u32 run = 0; run < 8; ++run) { mvu_aot::WaitWorker(); root(); }
    Check(mvu_aot::PendingCount() == 0 && mvu_aot::ReadyCountForTest() == 0 &&
        mvu_aot::GetStats().readyPublished > before.readyPublished,
        "accepted child work publishes and the bounded remembered queue drains");
    const auto childEntries = mvu_aot::NativeRegionEntryCount(0xd0), childEnds = mvu_aot::NativeEndCount();
    rings = {};
    const auto actualChild = RunPersistedVU(1, code, 0x80, 128, false, rings, 0xd0, nullptr, 0, 0x80);
    Compare(child, actualChild, "published newly remembered region executes exact child architecture");
    Check(mvu_aot::NativeRegionEntryCount(0xd0) > childEntries && mvu_aot::NativeEndCount() > childEnds &&
        child.regs.cycle == actualChild.regs.cycle && childRings == rings,
        "new automatic D0 region and native end actually execute after the held-worker controls");
    for (u32 run = 0; run < 8; ++run) root();
    // Cache is disabled here, so the real SaveProgram acknowledgement fails.
    // This must remain dirty and retry only after another full quiet interval.
    mvu_aot::TestDirtyQuietSave(); before = mvu_aot::GetStats();
    for (u32 run = 0; run < 1023; ++run) root();
    Check(mvu_aot::GetStats().quietSaveAttempts == before.quietSaveAttempts &&
        mvu_aot::GetStats().dispatchFastEntries > before.dispatchFastEntries,
        "dirty quiet interval still uses warm entries and performs no early save attempt");
    auto boundary = mvu_aot::GetStats(); checkRoot("quiet boundary preserves native architecture");
    Check(mvu_aot::GetStats().quietSaveAttempts == before.quietSaveAttempts + 1 &&
        mvu_aot::GetStats().quietSaveQueues == before.quietSaveQueues && mvu_aot::QuietSaveDirtyForTest() &&
        mvu_aot::GetStats().dispatchSlowKeys == boundary.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == boundary.dispatchFastEntries,
        "exact quiet boundary takes slow path and retains dirty state after actual disabled-cache acknowledgement failure");
    before = mvu_aot::GetStats();
    for (u32 run = 0; run < 1023; ++run) root();
    Check(mvu_aot::GetStats().quietSaveAttempts == before.quietSaveAttempts,
        "failed quiet acknowledgement never retries during the next1023 warm calls");
    boundary = mvu_aot::GetStats(); root();
    Check(mvu_aot::GetStats().quietSaveAttempts == before.quietSaveAttempts + 1 &&
        mvu_aot::GetStats().dispatchSlowKeys == boundary.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == boundary.dispatchFastEntries,
        "failed quiet acknowledgement retries exactly at the next1024 boundary");
    // Read actual state, not a duplicate eligibility helper. The next real
    // native entry at an exact bucket boundary must execute the old slow
    // checkpoint even when no new target has made harvest necessary.
    u32 runs = 0, jumps = 0, bucket = 0;
    Check(mvu_aot::ReadDispatchCadenceForTest(runs, jumps, bucket),
        "native retier cadence reads the real current owner counters");
    const u32 remaining = (1023u - ((runs + jumps) & 1023u)) & 1023u;
    for (u32 run = 0; run < remaining; ++run) root();
    Check(mvu_aot::ReadDispatchCadenceForTest(runs, jumps, bucket) &&
        ((runs + jumps) & 1023u) == 1023u && bucket == ((runs + jumps) >> 10),
        "real warm native execution reaches the final entry before the next1024 retier bucket");
    const u32 oldRuns = runs, oldJumps = jumps, oldBucket = bucket;
    boundary = mvu_aot::GetStats();
    const auto bucketEntries = mvu_aot::NativeRegionEntryCount(0x80), bucketEnds = mvu_aot::NativeEndCount();
    checkRoot("exact native retier bucket preserves full architecture");
    Check(mvu_aot::ReadDispatchCadenceForTest(runs, jumps, bucket) &&
        runs == oldRuns + 1 && jumps == oldJumps && bucket == oldBucket + 1 &&
        mvu_aot::GetStats().dispatchSlowKeys == boundary.dispatchSlowKeys + 1 &&
        mvu_aot::GetStats().dispatchFastEntries == boundary.dispatchFastEntries &&
        mvu_aot::NativeRegionEntryCount(0x80) == bucketEntries + 1 &&
        mvu_aot::NativeEndCount() == bucketEnds + 1,
        "exact1023 to1024 native transition advances the actual retier checkpoint on slow path exactly once");
    before = mvu_aot::GetStats(); root();
    Check(mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries + 1 &&
        mvu_aot::GetStats().dispatchSlowKeys == before.dispatchSlowKeys,
        "native fast dispatch resumes immediately after the real retier checkpoint");
    for (u32 index = 0; index < std::size(budgets); ++index)
    {
        // Any resumption pipeline descriptors from the preceding short run
        // are processed outside this measured old-root budget dispatch.
        for (u32 run = 0; run < 8; ++run) { mvu_aot::WaitWorker(); root(); }
        before = mvu_aot::GetStats();
        const auto native = mvu_aot::NativeRegionEntryCount(0x80), bails = mvu_aot::BailCount();
        const auto budgetEnds = mvu_aot::NativeEndCount();
        rings = {};
        const auto actual = RunPersistedVU(1, code, 0x80, budgets[index], false, rings);
        Compare(budgetExpected[index], actual, "warm native short-budget dispatch preserves complete architecture");
        Check(budgetExpected[index].regs.cycle == actual.regs.cycle && budgetRings[index] == rings &&
            budgetExpected[index].regs.pending_p == actual.regs.pending_p &&
            budgetExpected[index].regs.pending_q == actual.regs.pending_q,
            "warm native short-budget dispatch preserves exact cycles pending PQ and every flag ring");
        // Baseline tests for at least ONE remaining cycle before spending a
        // whole block. This acyclic path costs4 at80 then needs1 atB0, hence
        // exact native need5, not its full physical path cost7.
        Check(mvu_aot::GetStats().dispatchFastEntries > before.dispatchFastEntries &&
            mvu_aot::NativeRegionEntryCount(0x80) > native &&
            (budgets[index] < 5 ? mvu_aot::BailCount() > bails :
                (mvu_aot::NativeEndCount() > budgetEnds && mvu_aot::BailCount() == bails)),
            "warm native entry bails below the exact need5 threshold and ends natively with no bail at5 or7");
        std::printf("VU_FAST_BUDGET budget=%u fast=%llu native=%llu bails=%llu ends=%llu busy=%x cycles=%llu\n", budgets[index],
            static_cast<unsigned long long>(mvu_aot::GetStats().dispatchFastEntries - before.dispatchFastEntries),
            static_cast<unsigned long long>(mvu_aot::NativeRegionEntryCount(0x80) - native),
            static_cast<unsigned long long>(mvu_aot::BailCount() - bails),
            static_cast<unsigned long long>(mvu_aot::NativeEndCount() - budgetEnds), actual.busy,
            static_cast<unsigned long long>(actual.regs.cycle));
    }
    // Reset without any intervening mode setter. The first cold lookup must
    // not use a cursor whose owner allocation or code address can be reused.
    mvu_aot::SetTierUpRuns(1); root(); root();
    u32 observedReuse = 0;
    u64 previousOwner = mvu_aot::CurrentOwnerForTest();
    for (u32 reset = 0; reset < 4; ++reset)
    {
        before = mvu_aot::GetStats(); CpuMicroVU1.Reset();
        checkRoot("real cache reset and possible owner-address reuse preserve architecture");
        Check(mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries &&
            mvu_aot::GetStats().dispatchSlowKeys > before.dispatchSlowKeys,
            "first native lookup after actual Reset never uses the discarded owner cursor");
        observedReuse += mvu_aot::CurrentOwnerForTest() == previousOwner;
        previousOwner = mvu_aot::CurrentOwnerForTest();
        for (u32 run = 0; run < 8; ++run) { mvu_aot::WaitWorker(); root(); }
        before = mvu_aot::GetStats(); root();
        Check(mvu_aot::GetStats().dispatchFastEntries == before.dispatchFastEntries + 1,
            "new generation regains native fast dispatch only after actual publication and classification");
    }
    mvu_aot::SetTierUpRuns(200);
    std::printf("VU_FAST_GUARDS observed_owner_address_reuse=%u\n", observedReuse);
}
