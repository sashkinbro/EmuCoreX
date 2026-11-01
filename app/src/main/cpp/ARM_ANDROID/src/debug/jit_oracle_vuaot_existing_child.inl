// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unreferenced until the preceding frame checkpoint is committed. The callback
// must count executed REGION entries at a PC, not inline body compilation.
void RunVuAotExistingChildTests(u64 (*regionEntriesAt)(u32))
{
    using namespace VuCompileBranchOracle;
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate unchanged-key cold child fixture"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache; EmuFolders::Cache.clear();
    const auto code = Program(boundaries[0]);
    std::array<u32, 12> expectedRings{}, rings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::ForceSynchronous(true); mvu_aot::CountEntries(true);
    auto regionControl = code;
    regionControl[(0xa0 - 0x80) / 4 + 1] = NOP_U; // remove only the runtime boundary; graph is fully lowerable
    const auto controlEntries = regionEntriesAt(0x80);
    const auto positive = RunPersistedVU(1, regionControl, 0x80, 128, true, rings);
    Check(Model(boundaries[0], expected) && Model(boundaries[0], positive) && regionEntriesAt(0x80) > controlEntries,
        "independent control goldens and a real positive native region prove the per-PC counter is active");
    const auto bodies = mvu_aot::GetStats().bodyBlocks;
    const auto outerRegionEntries = regionEntriesAt(0x80);
    struct CompileCapture
    {
        bool automatic = false;
        std::vector<std::pair<bool, vuaot::MvuBlockTrace>> traces;
    } capture;
    const auto previousHook = vuaot::g_mvuBlockHook;
    void* const previousHookUser = vuaot::g_mvuBlockHookUser;
    vuaot::g_mvuBlockHook = +[](const vuaot::MvuBlockTrace& trace, void* user) {
        auto& log = *static_cast<CompileCapture*>(user);
        log.traces.emplace_back(log.automatic, trace);
    };
    vuaot::g_mvuBlockHookUser = &capture;
    const ScopedGuard restoreHook([&] { vuaot::g_mvuBlockHook = previousHook; vuaot::g_mvuBlockHookUser = previousHookUser; });
    rings = {};
    const auto initial = RunPersistedVU(1, code, 0x80, 128, true, rings);
    Compare(expected, initial, "initial tiered root preserves its already compiled baseline children");
    Check(mvu_aot::GetStats().bodyBlocks > bodies && regionEntriesAt(0x80) == outerRegionEntries,
        "baseline T-boundary forces a genuine outer BodyTier root and is excluded by the region-only counter");
    const auto variants = [] {
        std::vector<JitBlockProfile> blocks; VU1_JitGetBlockProfiles(blocks);
        return std::count_if(blocks.begin(), blocks.end(), [](const auto& b) { return b.startpc == 0xc0; });
    };
    const auto coldVariants = variants();
    Check(coldVariants > 0, "the direct child already has a baseline variant before any new pipeline key is introduced");
    const auto before = mvu_aot::GetStats();
    const auto compiles = mVUPersist::GetBlockCompileCount(1);
    const auto regionEntries = regionEntriesAt(0xc0), ends = mvu_aot::NativeEndCount(), bails = mvu_aot::BailCount();
    capture.automatic = true;
    for (u32 run = 0; run < 4; ++run)
    {
        rings = {};
        const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings);
        Compare(expected, actual, "unchanged-key direct child preserves complete architecture");
        Check(expected.regs.cycle == actual.regs.cycle && expectedRings == rings &&
            expected.regs.pending_p == actual.regs.pending_p && expected.regs.pending_q == actual.regs.pending_q,
            "unchanged-key direct child preserves exact cycles PQ and flag rings");
    }
    for (const auto& [automatic, trace] : capture.traces)
    {
        std::printf("AOT_EXISTING_COMPILE phase=%s pc=%x cycles=%u key=", automatic ? "auto" : "initial", trace.startPC, trace.cycles);
        for (u32 offset = 0; offset < 96; offset += 4)
        {
            u32 word; std::memcpy(&word, trace.entryState + offset, 4); std::printf("%08x", word);
        }
        std::printf("\n");
    }
    Check(mvu_aot::GetStats().dispatches == before.dispatches + 4 &&
        mvu_aot::GetStats().jumpEntries == before.jumpEntries &&
        mvu_aot::GetStats().jumpResolverCalls == before.jumpResolverCalls &&
        mVUPersist::GetBlockCompileCount(1) == compiles && variants() == coldVariants &&
        mvu_aot::GetStats().automaticJobs == before.automaticJobs + 1,
        "unchanged child receives no external offer recompile or new pipeline variant and builds exactly one automatic C job");
    Check(regionEntriesAt(0xc0) > regionEntries && mvu_aot::NativeEndCount() > ends && mvu_aot::BailCount() == bails,
        "preexisting direct child executes its real native region and end without substituting an inline body or budget fallback");
    std::printf("AOT_EXISTING_CHILD variants=%zu region_entries=%llu ends=%llu bails=%llu compiles=%llu automatic_jobs=%llu\n",
        static_cast<size_t>(variants()), static_cast<unsigned long long>(regionEntriesAt(0xc0) - regionEntries),
        static_cast<unsigned long long>(mvu_aot::NativeEndCount() - ends),
        static_cast<unsigned long long>(mvu_aot::BailCount() - bails),
        static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(1) - compiles),
        static_cast<unsigned long long>(mvu_aot::GetStats().automaticJobs - before.automaticJobs));
    EmuConfig.Cpu.Recompiler.EnableVUAot = false; CpuMicroVU1.Reset();
    mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false); EmuFolders::Cache = savedCache;
}
