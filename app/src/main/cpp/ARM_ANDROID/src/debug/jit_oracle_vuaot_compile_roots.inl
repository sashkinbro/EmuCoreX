// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after RunPersistedVU. A direct baseline edge is deliberately never
// offered as an external dispatch or a JR/JALR target.
void RunVuAotCompileRootsTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate compile-root coverage memory"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache;
    EmuFolders::Cache.clear();
    std::vector<u32> code;
    for (u32 pc = 0x80; pc < 0xe0; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
    };
    const auto branch = [](u32 pc, u32 target) {
        return (0x20u << 25) | (((target - (pc + 8)) / 8) & 0x7ffu);
    };
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const u32 mul = (15u << 21) | (2u << 16) | (1u << 11) | (4u << 6) | 0x2au;
    put(0x80, NOP_L, add);
    put(0x88, branch(0x88, 0xa0));
    // FBRST is zero, so this T-bit check continues normally. It nevertheless
    // prevents the AOT region walker from swallowing the following baseline
    // branch/child into A. VU1 ignores M-bit, which would not test this gap.
    put(0xa0, NOP_L, NOP_U | 0x08000000u);
    put(0xa8, branch(0xa8, 0xc0));
    put(0xc0, NOP_L, mul);
    put(0xc8, NOP_L, NOP_U | 0x40000000u);
    vuaot::PipeState late;
    late.p = 17; late.q = 7;
    std::array<u32, 12> expectedColdRings{}, expectedLateRings{}, rings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    const auto expectedCold = RunPersistedVU(1, code, 0x80, 128, true, expectedColdRings);
    const auto expectedLate = RunPersistedVU(1, code, 0x80, 128, true, expectedLateRings, 0x80, &late);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::ForceSynchronous(true); mvu_aot::CountEntries(true);
    const auto bodies = mvu_aot::GetStats().bodyBlocks;
    const auto cold = RunPersistedVU(1, code, 0x80, 128, true, rings);
    Compare(expectedCold, cold, "compile-root fixture cold BodyTier preserves architecture");
    Check(mvu_aot::GetStats().bodyBlocks > bodies,
        "runtime-boundary child forces the root through genuine BodyTier fallback");
    const auto childVariants = [] {
        std::vector<JitBlockProfile> blocks;
        VU1_JitGetBlockProfiles(blocks);
        return std::count_if(blocks.begin(), blocks.end(), [](const auto& block) { return block.startpc == 0xc0; });
    };
    const auto variants = childVariants();
    const auto compiled = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
    Check(childVariants() > variants,
        "same tiered image compiles a fresh direct child pipeline variant after tier-up");
    Compare(expectedLate, compiled, "compiling late direct child preserves baseline architecture");
    // A compile hook may only enqueue while oaknut owns the emission cursor.
    // Let such a job finish, then give the next owner dispatch its publication
    // opportunity before testing actual direct-link reachability.
    mvu_aot::WaitWorker();
    const auto before = mvu_aot::GetStats();
    const auto entries = mvu_aot::EntryCount();
    const auto ends = mvu_aot::NativeEndCount();
    const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
    Check(mvu_aot::GetStats().jumpEntries == before.jumpEntries &&
        mvu_aot::GetStats().jumpResolverCalls == before.jumpResolverCalls &&
        mvu_aot::GetStats().dispatches == before.dispatches + 1,
        "direct child is never offered through Dispatch or AfterJump");
    Compare(expectedLate, actual, "late direct child preserves baseline architecture");
    Check(expectedLate.regs.cycle == actual.regs.cycle && expectedLateRings == rings &&
        expectedLate.regs.pending_p == actual.regs.pending_p && expectedLate.regs.pending_q == actual.regs.pending_q,
        "late direct child preserves exact cycles pending PQ and all flag rings");
    // The outer root branches; only C has an E-bit end. This catches a queued
    // or manager-published C that existing machine direct links still bypass.
    Check(mvu_aot::NativeEndCount() > ends && mvu_aot::EntryCount() > entries + 1,
        "late direct child executes its generated AOT entry and native program end");
    std::printf("AOT_COMPILE_ROOT variants_before=%zu variants_after=%zu entries=%llu ends=%llu dispatch=%llu jr=%llu\n",
        static_cast<size_t>(variants), static_cast<size_t>(childVariants()),
        static_cast<unsigned long long>(mvu_aot::EntryCount() - entries),
        static_cast<unsigned long long>(mvu_aot::NativeEndCount() - ends),
        static_cast<unsigned long long>(mvu_aot::GetStats().dispatches - before.dispatches),
        static_cast<unsigned long long>(mvu_aot::GetStats().jumpEntries - before.jumpEntries));
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    CpuMicroVU1.Reset(); mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false);
    EmuFolders::Cache = savedCache;
}

// Each phase runs in a fresh process. Reuse the existing canonical snapshot
// format, but use a separate cache tree/title from generic AOT/CLIP fixtures.
void RunVuAotCompileRootsColdTests(bool writer)
{
    cpuinfo_initialize();
    const auto savedConfig = EmuConfig;
    const auto savedCache = EmuFolders::Cache;
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
    EmuFolders::Cache = "/data/local/tmp/emucorex-vu-cold-cache/compile-roots";
    struct Restore
    {
        const Pcsx2Config& config; const std::string& cache;
        ~Restore() { EmuConfig = config; EmuFolders::Cache = cache; }
    } restore{savedConfig, savedCache};
    if (!FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(), true) || !SysMemory::Allocate())
    {
        Check(false, "allocate dedicated late-child cold-cache fixture"); return;
    }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    mVUPersist::SetProcessDisable(false); mVUPersist::SetTestManualRecording(false);
    mvu_aot::CountEntries(true); mvu_aot::ForceSynchronous(writer);
    mvu_aot::HoldWorker(!writer); mvu_aot::SetTierUpRuns(1);
    VUColdCacheHeader header; Snapshot expected{};
    const std::string expectedPath = EmuFolders::Cache + "/expected.bin";
    const void* symbol = reinterpret_cast<const void*>(&mVUPersist::GetStats);
    mVUPersist::AddressRegion region{}; s64 offset = 0;
    const bool classified = mVUPersist::ClassifyAddressRegion(symbol, region, offset);
    Check(classified && region == mVUPersist::AddressRegion::Image,
        "classify late-child cache module image");
    const u64 base = reinterpret_cast<uintptr_t>(symbol) - offset;
    if (writer)
    {
        header.nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        header.imageBase = base; header.vu = 1; header.aot = 1;
    }
    else
    {
        const bool loaded = VUColdCacheFile(expectedPath, false, header, expected);
        Check(loaded && header.vu == 1 && header.aot == 1, "read prior-process late-child expected state");
        if (!loaded || header.vu != 1 || header.aot != 1) return;
        Check(header.imageBase != base, "late-child native graph loads at a different module ASLR base");
    }
    mVUProgCache::SetTitle("ORACLE-COLD-COMPILE-ROOTS-" + std::to_string(header.nonce), 0xc01d1707);
    std::vector<u32> code;
    for (u32 pc = 0x80; pc < 0xe0; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
    };
    put(0x80, NOP_L, (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u);
    put(0x88, (0x20u << 25) | 2u);
    put(0xa0, NOP_L, NOP_U | 0x08000000u);
    put(0xa8, (0x20u << 25) | 2u);
    put(0xc0, NOP_L, (15u << 21) | (2u << 16) | (1u << 11) | (4u << 6) | 0x2au);
    put(0xc8, NOP_L, NOP_U | 0x40000000u);
    vuaot::PipeState late; late.p = 17; late.q = 7;
    std::array<u32, 12> rings{};
    if (writer)
    {
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        EmuConfig.Cpu.Recompiler.EnableVUProgramCache = false;
        expected = RunPersistedVU(1, code, 0x80, 128, true, header.rings, 0x80, &late);
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
        RunPersistedVU(1, code, 0x80, 128, true, rings);
        RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
        mvu_aot::WaitWorker();
        const auto entries = mvu_aot::EntryCount(), ends = mvu_aot::NativeEndCount();
        const auto recorded = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
        Check(mvu_aot::EntryCount() > entries + 1 && mvu_aot::NativeEndCount() > ends,
            "cold writer executes the new late-child native body before saving it");
        Compare(expected, recorded, "late-child cold writer preserves baseline architecture");
        Check(expected.regs.cycle == recorded.regs.cycle && header.rings == rings,
            "late-child cold writer preserves exact cycles and flag rings");
        mVUProgCache::TestWaitForPreload(1);
        const auto before = mVUProgCache::GetStats(1);
        CpuMicroVU1.Reset(); mVUProgCache::FlushSidecars(1);
        const auto after = mVUProgCache::GetStats(1);
        Check(after.payloadWrites > before.payloadWrites && after.sidecarWrites > before.sidecarWrites,
            "late-child native body is retained in baseline graph with the root sidecar");
        const auto pack = mVUProgCache::TestPackPath(1);
        mVUProgCache::TestClose(1);
        Check(pack.find(EmuFolders::Cache + "/") == 0 && FileSystem::FileExists(pack.c_str()),
            "late-child writer closes its isolated native pack on disk");
        header.memorySize = expected.memory.size(); header.gifSize = expected.gif.size();
        header.busy = expected.busy; header.fallback = expected.fallback;
        expected.regs.Micro = nullptr; expected.regs.Mem = nullptr;
        Check(VUColdCacheFile(expectedPath, true, header, expected), "save independent late-child expected snapshot");
    }
    else
    {
        CpuMicroVU1.Reset(); mVUProgCache::TestWaitForPreload(1);
        const auto before = mVUProgCache::GetStats(1);
        Check(before.loadedRecords > 0, "fresh late-child reader preloads the prior-process native pack");
        const auto compiles = mVUPersist::GetBlockCompileCount(1);
        const auto entries = mvu_aot::EntryCount(), ends = mvu_aot::NativeEndCount();
        const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
        Check(mVUProgCache::GetStats(1).payloadHits > before.payloadHits &&
            mVUPersist::GetBlockCompileCount(1) == compiles,
            "fresh ASLR process executes relocated late-child native graph with zero baseline recompiles");
        Check(mvu_aot::EntryCount() > entries + 1 && mvu_aot::NativeEndCount() > ends && mvu_aot::PendingCount() == 0,
            "fresh held-worker process reaches saved root and late-child native entries and end");
        Compare(expected, actual, "fresh ASLR late-child native body preserves complete architecture");
        Check(expected.regs.cycle == actual.regs.cycle && header.rings == rings &&
            expected.regs.pending_p == actual.regs.pending_p && expected.regs.pending_q == actual.regs.pending_q,
            "fresh ASLR late-child native body preserves exact cycles PQ and all rings");
        std::printf("AOT_COMPILE_ROOT_COLD compiles=%llu entries=%llu ends=%llu pending=%u\n",
            static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(1) - compiles),
            static_cast<unsigned long long>(mvu_aot::EntryCount() - entries),
            static_cast<unsigned long long>(mvu_aot::NativeEndCount() - ends), mvu_aot::PendingCount());
    }
    std::printf("AOT_COMPILE_ROOT_ASLR phase=%s writer_base=%llx reader_base=%llx nonce=%llu\n",
        writer ? "write" : "read", static_cast<unsigned long long>(header.imageBase),
        static_cast<unsigned long long>(base), static_cast<unsigned long long>(header.nonce));
    mvu_aot::HoldWorker(false); mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false);
    mvu_aot::SetTierUpRuns(200); CpuMicroVU1.Reset(); mVUProgCache::TestClose(1);
}
