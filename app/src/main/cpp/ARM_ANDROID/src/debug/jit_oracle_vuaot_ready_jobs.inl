// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after RunPersistedVU. The sole accepted job finishes while another
// overlay occupies its required words; a held worker proves later reuse.
void RunVuAotReadySafetyTests();
void RunVuAotReadyBodyExitTests();
void RunVuAotReadyJobsTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate deferred-ready VU memory"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache; EmuFolders::Cache.clear();
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const std::vector<u32> a{NOP_L, add, NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
    auto b = a; b[1] = (add & ~0x3fu) | 0x2cu;
    std::array<u32, 12> expectedARings{}, expectedBRings{}, rings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    const auto expectedA = RunPersistedVU(1, a, 0x80, 128, true, expectedARings);
    const auto expectedB = RunPersistedVU(1, b, 0x80, 128, true, expectedBRings);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(true);
    mvu_aot::SetTierUpRuns(1); mvu_aot::HoldWorker(true);
    const auto queuedEntries = mvu_aot::EntryCount();
    const auto coldA = RunPersistedVU(1, a, 0x80, 128, true, rings);
    Check(mvu_aot::PendingCount() == 1 && mvu_aot::EntryCount() == queuedEntries,
        "exactly one A job is queued while its compiler worker is held");
    Compare(expectedA, coldA, "pending A retains exact baseline execution");
    mvu_aot::SetTierUpRuns(100000); // B must not introduce a second job.
    CpuMicroVU1.Clear(0x80, b.size() * sizeof(u32));
    const auto coldB = RunPersistedVU(1, b, 0x80, 128, false, rings);
    Check(mvu_aot::PendingCount() == 1 && rings == expectedBRings,
        "distinct B overlay executes while only A remains pending");
    Compare(expectedB, coldB, "B overlay preserves its own arithmetic");
    mvu_aot::HoldWorker(false); mvu_aot::WaitWorker();
    const auto beforeDrain = mvu_aot::GetStats();
    const auto regions = mvu_aot::RegionCount();
    const auto drainedB = RunPersistedVU(1, b, 0x80, 128, false, rings);
    Check(mvu_aot::RegionCount() == regions && mvu_aot::PendingCount() == 0 &&
        mvu_aot::GetStats().unsupportedDeclines == beforeDrain.unsupportedDeclines,
        "accepted A completion never publishes while B occupies its read image");
    Compare(expectedB, drainedB, "draining A completion cannot alter live B execution");
    Check(expectedB.regs.cycle == drainedB.regs.cycle && expectedBRings == rings,
        "deferred A completion preserves exact B cycles and all rings");
    // No new compilation can produce the success below: the worker is held,
    // the threshold is high, and A's original baseline graph is already cached.
    mvu_aot::HoldWorker(true);
    CpuMicroVU1.Clear(0x80, a.size() * sizeof(u32));
    const auto compiles = mVUPersist::GetBlockCompileCount(1);
    const auto entries = mvu_aot::EntryCount(), ends = mvu_aot::NativeEndCount();
    const auto restoredA = RunPersistedVU(1, a, 0x80, 128, false, rings);
    Check(mvu_aot::EntryCount() > entries && mvu_aot::NativeEndCount() > ends &&
        mvu_aot::RegionCount() > regions && mvu_aot::PendingCount() == 0 &&
        mVUPersist::GetBlockCompileCount(1) == compiles,
        "restored A immediately reuses its completed native job with worker held and zero recompilation");
    Compare(expectedA, restoredA, "restored accepted A job preserves architecture");
    Check(expectedA.regs.cycle == restoredA.regs.cycle && expectedARings == rings &&
        expectedA.regs.pending_p == restoredA.regs.pending_p && expectedA.regs.pending_q == restoredA.regs.pending_q,
        "restored accepted A job preserves exact cycles pending PQ and all rings");
    std::printf("AOT_READY_JOB entries=%llu ends=%llu compiles=%llu pending=%u publish_rejects=%llu\n",
        static_cast<unsigned long long>(mvu_aot::EntryCount() - entries),
        static_cast<unsigned long long>(mvu_aot::NativeEndCount() - ends),
        static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(1) - compiles),
        mvu_aot::PendingCount(),
        static_cast<unsigned long long>(mvu_aot::GetStats().publishRejects - beforeDrain.publishRejects));
    RunVuAotReadySafetyTests();
    RunVuAotReadyBodyExitTests();
    mvu_aot::HoldWorker(false); mvu_aot::SetTierUpRuns(200);
    EmuConfig.Cpu.Recompiler.EnableVUAot = false; CpuMicroVU1.Reset();
    mvu_aot::CountEntries(false); EmuFolders::Cache = savedCache;
}

void RunVuAotReadyBodyExitTests()
{
    std::vector<u32> code;
    for (u32 pc = 0x80; pc < 0xc0; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
    };
    put(0x80, NOP_L, (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u);
    put(0x88, (0x20u << 25) | 2u);
    put(0xa0, NOP_L, NOP_U | 0x08000000u); // runtime T boundary rejects RegionTier, BodyTier accepts root
    put(0xa8, NOP_L, (15u << 21) | (2u << 16) | (1u << 11) | (4u << 6) | 0x2au);
    put(0xb0, NOP_L, NOP_U | 0x40000000u);
    std::array<u8, 96> fallbackKey{};
    for (u32 mode : {0u, 1u, 2u})
    {
        mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false);
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        std::array<u32, 12> expectedRings{}, rings{};
        const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(true);
        mvu_aot::SetTierUpRuns(1); mvu_aot::HoldWorker(true);
        mvu_aot::TestHoldReadyPublication(true);
        RunPersistedVU(1, code, 0x80, 128, true, rings);
        Check(mvu_aot::PendingCount() == 1, "branch-body fixture queues one actual held worker job");
        mvu_aot::SetTierUpRuns(100000); mvu_aot::HoldWorker(false); mvu_aot::WaitWorker();
        RunPersistedVU(1, code, 0x80, 128, false, rings);
        mvu_aot::HoldWorker(true); mvu_aot::TestHoldReadyPublication(false);
        Check(mvu_aot::ReadyCountForTest() == 1 && mvu_aot::PendingCount() == 0,
            "accepted branch body is retained with its baseline child already compiled");
        std::vector<vuaot::MvuBlockTrace> publicationTraces;
        const auto previousHook = vuaot::g_mvuBlockHook;
        void* const previousHookUser = vuaot::g_mvuBlockHookUser;
        vuaot::g_mvuBlockHook = +[](const vuaot::MvuBlockTrace& trace, void* user) {
            static_cast<std::vector<vuaot::MvuBlockTrace>*>(user)->push_back(trace);
        };
        vuaot::g_mvuBlockHookUser = &publicationTraces;
        const ScopedGuard restoreHook([&] {
            vuaot::g_mvuBlockHook = previousHook; vuaot::g_mvuBlockHookUser = previousHookUser;
        });
        const auto before = mvu_aot::GetStats();
        const auto publicationCompiles = mVUPersist::GetBlockCompileCount(1);
        u32 targetPC = 0;
        const bool published = mvu_aot::TestReadyBodyExit(mode, targetPC);
        Check(targetPC == 0xa0, "actual accepted BodyTier static exit lies outside job reads but inside cached owner ranges");
        Check(mode == 0 ? (!published && mvu_aot::ReadyCountForTest() == 1) :
            (published && mvu_aot::ReadyCountForTest() == 0),
            "known unread static-child live conflict defers publication while matching unread snapshot and unknown words allow it");
        const auto entries = mvu_aot::EntryCount();
        const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings);
        if (mode == 0 && publicationTraces.size() == 1)
            std::memcpy(fallbackKey.data(), publicationTraces.front().entryState, fallbackKey.size());
        Check(publicationTraces.size() == 1 && publicationTraces.front().startPC == targetPC &&
            publicationTraces.front().cycles == 4 &&
            std::memcmp(fallbackKey.data(), publicationTraces.front().entryState, fallbackKey.size()) == 0 &&
            mVUPersist::GetBlockCompileCount(1) == publicationCompiles + 1 && mvu_aot::PendingCount() == 0,
            "all compatibility modes compile exactly the same one private baseline exit variant during publication without a new worker job");
        Check(mvu_aot::EntryCount() > entries && mvu_aot::ReadyCountForTest() == 0 &&
            mvu_aot::GetStats().bodyBlocks > before.bodyBlocks,
            "compatible restored child executes the retained actual native branch body with worker held");
        Compare(expected, actual, "retained branch body and its cached static exit preserve complete architecture");
        Check(expected.regs.cycle == actual.regs.cycle && expectedRings == rings,
            "retained branch body and static exit preserve exact cycles and rings");
        const auto steadyCompiles = mVUPersist::GetBlockCompileCount(1), steadyEntries = mvu_aot::EntryCount();
        const auto repeated = RunPersistedVU(1, code, 0x80, 128, false, rings);
        Check(mvu_aot::EntryCount() > steadyEntries && mvu_aot::PendingCount() == 0 &&
            mvu_aot::ReadyCountForTest() == 0 && mVUPersist::GetBlockCompileCount(1) == steadyCompiles &&
            publicationTraces.size() == 1 && expected.regs.cycle == repeated.regs.cycle && expectedRings == rings,
            "repeated native branch body adds no baseline compile or worker job and preserves cycles and rings");
        Compare(expected, repeated, "repeated retained branch body preserves complete architecture");
    }
    mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false);
}

void RunVuAotReadySafetyTests()
{
    constexpr u64 budget = 64ull << 20;
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const std::vector<u32> a{NOP_L, add, NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
    auto b = a; b[1] = (add & ~0x3fu) | 0x2cu;
    Snapshot expectedA{}, expectedB{};
    std::array<u32, 12> ar{}, br{}, rings{};
    const auto baseline = [&] {
        mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false);
        mvu_aot::TestReadyLimits(budget, 256, 8);
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        expectedA = RunPersistedVU(1, a, 0x80, 128, true, ar);
        expectedB = RunPersistedVU(1, b, 0x80, 128, true, br);
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(true);
    };
    const auto setup = [&](u64 bytes = budget) {
        baseline(); mvu_aot::TestReadyLimits(bytes, 256, 8);
        mvu_aot::SetTierUpRuns(1); mvu_aot::HoldWorker(true);
        RunPersistedVU(1, a, 0x80, 128, true, rings);
        mvu_aot::SetTierUpRuns(100000);
        CpuMicroVU1.Clear(0x80, 32);
        RunPersistedVU(1, b, 0x80, 128, false, rings);
        mvu_aot::HoldWorker(false); mvu_aot::WaitWorker();
        RunPersistedVU(1, b, 0x80, 128, false, rings);
        mvu_aot::HoldWorker(true);
        Check(mvu_aot::ReadyCountForTest() == (bytes == 1 ? 0u : 1u) &&
            (bytes == 1 ? mvu_aot::ReadyBytesForTest() == 0 : mvu_aot::ReadyBytesForTest() > 0),
            "real accepted overlay job has exact bounded ready ownership and byte accounting");
    };
    const auto restoreA = [&] {
        CpuMicroVU1.Clear(0x80, 32);
        return RunPersistedVU(1, a, 0x80, 128, false, rings);
    };
    setup();
    const auto duplicate = mvu_aot::GetStats().readyDuplicates;
    const auto bytes = mvu_aot::ReadyBytesForTest();
    Check(mvu_aot::TestDuplicateReady() && mvu_aot::ReadyCountForTest() == 1 &&
        mvu_aot::ReadyBytesForTest() == bytes && mvu_aot::GetStats().readyDuplicates == duplicate + 1,
        "duplicate admission of an actual accepted job adds no ready ownership or bytes");
    auto entries = mvu_aot::EntryCount();
    auto actual = restoreA();
    Check(mvu_aot::EntryCount() > entries && mvu_aot::ReadyCountForTest() == 0 && mvu_aot::ReadyBytesForTest() == 0,
        "duplicate suppression preserves the original held-worker native reuse");
    Compare(expectedA, actual, "duplicate-ready native reuse preserves architecture");

    for (u32 kind : {0u, 1u})
    {
        setup();
        const auto stale = mvu_aot::GetStats().readyStale;
        const auto declines = mvu_aot::GetStats().unsupportedDeclines;
        Check(mvu_aot::TestStaleReady(kind), "make a real retained epoch or CPU snapshot stale");
        entries = mvu_aot::EntryCount(); actual = restoreA();
        Check(mvu_aot::EntryCount() == entries && mvu_aot::ReadyCountForTest() == 0 &&
            mvu_aot::ReadyBytesForTest() == 0 && mvu_aot::GetStats().readyStale == stale + 1 &&
            mvu_aot::GetStats().unsupportedDeclines == declines,
            "stale epoch and CPU snapshots are released without native execution or false decline cache");
        Compare(expectedA, actual, "stale snapshot rejection preserves baseline architecture");
        mvu_aot::HoldWorker(false); mvu_aot::SetTierUpRuns(1);
        RunPersistedVU(1, a, 0x80, 128, false, rings);
        Check(mvu_aot::PendingCount() == 1, "stale rejection allows a fresh legitimate request at the same root key");
        mvu_aot::WaitWorker(); entries = mvu_aot::EntryCount();
        actual = RunPersistedVU(1, a, 0x80, 128, false, rings);
        Check(mvu_aot::EntryCount() > entries, "fresh accepted job runs after stale rejection");
        Compare(expectedA, actual, "fresh replacement after stale rejection preserves architecture");
    }
    setup(); CpuMicroVU1.Reset(); mvu_aot::HoldWorker(true);
    Check(mvu_aot::ReadyCountForTest() == 0 && mvu_aot::ReadyBytesForTest() == 0 && mvu_aot::PendingCount() == 0,
        "actual cache reset releases all retained owners and byte accounting");
    entries = mvu_aot::EntryCount(); actual = restoreA();
    Check(mvu_aot::EntryCount() == entries, "reset cannot revive an old generation while worker is held");
    Compare(expectedA, actual, "post-reset held-worker execution preserves baseline architecture");
    const auto evicted = mvu_aot::GetStats().readyEvicted;
    setup(1); entries = mvu_aot::EntryCount(); actual = restoreA();
    Check(mvu_aot::GetStats().readyEvicted > evicted && mvu_aot::EntryCount() == entries,
        "tiny retained-code budget evicts the actual accepted job and keeps safe baseline fallback");
    Compare(expectedA, actual, "code-budget eviction preserves baseline architecture");

    // Publication can be unavailable while the owner is emitting code. Hold
    // only that event in selftests so real worker jobs exercise both caps.
    for (bool perProgram : {false, true})
    {
        baseline();
        std::array<std::vector<u32>, 3> programs;
        std::array<vuaot::PipeState, 3> keys;
        std::array<Snapshot, 3> expected;
        std::array<std::array<u32, 12>, 3> expectedRings{};
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        for (u32 i = 0; i < 3; ++i)
        {
            programs[i] = a;
            if (perProgram) keys[i].p = i + 1;
            else programs[i].insert(programs[i].begin(), {0x3f800000u + i * 0x1000u, NOP_U | 0x80000000u});
            expected[i] = RunPersistedVU(1, programs[i], 0x80, 128, true, expectedRings[i], 0x80, &keys[i]);
        }
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        mvu_aot::TestReadyLimits(budget, perProgram ? 256 : 2, perProgram ? 2 : 8);
        mvu_aot::TestHoldReadyPublication(true);
        const auto capEvictions = mvu_aot::GetStats().readyEvicted;
        bool bounded = true;
        for (u32 i = 0; i < 3; ++i)
        {
            mvu_aot::SetTierUpRuns(1); mvu_aot::HoldWorker(true);
            if (i) CpuMicroVU1.Clear(0x80, 32);
            RunPersistedVU(1, programs[i], 0x80, 128, i == 0, rings, 0x80, &keys[i]);
            mvu_aot::SetTierUpRuns(100000); CpuMicroVU1.Clear(0x80, 32);
            RunPersistedVU(1, b, 0x80, 128, false, rings);
            mvu_aot::HoldWorker(false); mvu_aot::WaitWorker();
            RunPersistedVU(1, b, 0x80, 128, false, rings);
            bounded &= mvu_aot::ReadyCountForTest() == std::min(i + 1, 2u) &&
                mvu_aot::ReadyBytesForTest() <= budget;
        }
        Check(bounded && mvu_aot::GetStats().readyEvicted == capEvictions + 1,
            "global and per-program count limits evict the oldest of three actual worker jobs");
        mvu_aot::HoldWorker(true); mvu_aot::TestHoldReadyPublication(false);
        CpuMicroVU1.Clear(0x80, 32); entries = mvu_aot::EntryCount();
        actual = RunPersistedVU(1, programs[2], 0x80, 128, false, rings, 0x80, &keys[2]);
        Check(mvu_aot::EntryCount() > entries && expectedRings[2] == rings,
            "count cap preserves immediate held-worker execution of the newest retained job");
        Compare(expected[2], actual, "newest retained capped job preserves architecture");
        CpuMicroVU1.Clear(0x80, 32); entries = mvu_aot::EntryCount();
        actual = RunPersistedVU(1, programs[0], 0x80, 128, false, rings, 0x80, &keys[0]);
        Check(mvu_aot::EntryCount() == entries && expectedRings[0] == rings,
            "oldest count-evicted variant remains a safe baseline fallback");
        Compare(expected[0], actual, "oldest evicted capped job preserves architecture");
    }
    for (bool observed : {false, true})
    {
        setup();
        Check(mvu_aot::TestExpandReadyOwner(observed),
            "real retained job fixture creates a conflicting owner word with explicit partial-range observation");
        const auto regions = mvu_aot::RegionCount();
        const auto published = mvu_aot::TestRetryReadySnapshot();
        entries = mvu_aot::EntryCount(); actual = restoreA();
        Check(observed ? (!published && mvu_aot::RegionCount() == regions && mvu_aot::EntryCount() == entries) :
            (published && mvu_aot::RegionCount() > regions && mvu_aot::EntryCount() > entries),
            "known required owner-word conflict blocks publication while unobserved words permit real native reuse");
        Compare(expectedA, actual, "owner-range compatibility decision preserves complete architecture");
        Check(expectedA.regs.cycle == actual.regs.cycle && ar == rings,
            "owner-range compatibility decision preserves exact cycles and rings");
    }
    mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false);
    mvu_aot::TestReadyLimits(budget, 256, 8);
}
