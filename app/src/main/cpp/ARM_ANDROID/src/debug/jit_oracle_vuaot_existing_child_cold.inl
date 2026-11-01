// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after existing-child and the independent VU cold-cache helpers.
// The reader must hydrate the old baseline caller, rebuild its inbound B26,
// install the saved C region and run it without any compiler worker.
void RunVuAotExistingChildColdTests(bool writer)
{
    using namespace VuCompileBranchOracle;
    cpuinfo_initialize();
    const auto savedConfig = EmuConfig;
    const auto savedCache = EmuFolders::Cache;
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
    EmuFolders::Cache = "/data/local/tmp/emucorex-vu-cold-cache/existing-child";
    const ScopedGuard restore([&] { EmuConfig = savedConfig; EmuFolders::Cache = savedCache; });
    if (!FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(), true) || !SysMemory::Allocate())
    {
        Check(false, "allocate isolated static-inbound cold cache"); return;
    }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    mVUPersist::SetProcessDisable(false); mVUPersist::SetTestManualRecording(false);
    mvu_aot::CountEntries(true); mvu_aot::ForceSynchronous(writer); mvu_aot::SetTierUpRuns(1);
    const void* const symbol = reinterpret_cast<const void*>(&mVUPersist::GetStats);
    mVUPersist::AddressRegion region{}; s64 offset = 0;
    const bool image = mVUPersist::ClassifyAddressRegion(symbol, region, offset) &&
        region == mVUPersist::AddressRegion::Image;
    Check(image, "static-inbound cold cache classifies its real module image");
    if (!image) return;
    const u64 base = reinterpret_cast<uintptr_t>(symbol) - offset;
    const auto code = Program(boundaries[0], true); // real ESUM/DIV plus the runtime boundary
    const std::string expectedPath = EmuFolders::Cache + "/expected.bin";
    VUColdCacheHeader header;
    Snapshot expected{};
    if (writer)
    {
        header.nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        header.imageBase = base; header.vu = 1; header.aot = 1;
    }
    else
    {
        const bool loaded = VUColdCacheFile(expectedPath, false, header, expected);
        Check(loaded && header.vu == 1 && header.aot == 1,
            "static-inbound reader loads the prior independent expected snapshot");
        if (!loaded || header.vu != 1 || header.aot != 1) return;
        Check(header.imageBase != base, "static-inbound reader has a different actual module ASLR base");
    }
    mVUProgCache::SetTitle("ORACLE-COLD-STATIC-INBOUND-" + std::to_string(header.nonce), 0xc01dc001);
    std::array<u32, 12> rings{};
    if (writer)
    {
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        EmuConfig.Cpu.Recompiler.EnableVUProgramCache = false;
        expected = RunPersistedVU(1, code, 0x80, 128, true, header.rings);
        Check(Model(boundaries[0], expected) && DistinctPQ(expected),
            "static-inbound expected snapshot has independent control goldens and nonzero distinct PQ");
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
        mvu_aot::HoldWorker(false);
        RunPersistedVU(1, code, 0x80, 128, true, rings);
        const auto entered = mvu_aot::NativeRegionEntryCount(0xc0);
        Snapshot actual{};
        for (u32 run = 0; run < 6; ++run)
        {
            rings = {}; actual = RunPersistedVU(1, code, 0x80, 128, false, rings);
        }
        Check(mvu_aot::NativeRegionEntryCount(0xc0) > entered,
            "static-inbound writer executes the automatically published real C region before saving");
        Compare(expected, actual, "static-inbound writer preserves complete architecture");
        Check(expected.regs.cycle == actual.regs.cycle && header.rings == rings && DistinctPQ(actual),
            "static-inbound writer preserves exact cycles pending PQ and all flag rings");
        mVUProgCache::TestWaitForPreload(1);
        const auto before = mVUProgCache::GetStats(1);
        CpuMicroVU1.Reset(); mVUProgCache::FlushSidecars(1);
        const auto after = mVUProgCache::GetStats(1);
        Check(after.payloadWrites > before.payloadWrites && after.sidecarWrites > before.sidecarWrites,
            "static-inbound reset saves original baseline caller bytes and native root sidecars");
        const auto pack = mVUProgCache::TestPackPath(1);
        mVUProgCache::TestClose(1);
        Check(pack.find(EmuFolders::Cache + "/") == 0 && FileSystem::FileExists(pack.c_str()),
            "static-inbound writer closes its isolated native pack");
        header.memorySize = expected.memory.size(); header.gifSize = expected.gif.size();
        header.busy = expected.busy; header.fallback = expected.fallback;
        expected.regs.Micro = nullptr; expected.regs.Mem = nullptr;
        Check(VUColdCacheFile(expectedPath, true, header, expected),
            "static-inbound writer saves the independent expected snapshot");
    }
    else
    {
        CpuMicroVU1.Reset(); mVUProgCache::TestWaitForPreload(1); mvu_aot::HoldWorker(true);
        const auto before = mVUProgCache::GetStats(1);
        Check(before.loadedRecords > 0, "static-inbound reader preloads its prior-process native graph");
        const auto compiles = mVUPersist::GetBlockCompileCount(1);
        const auto entered = mvu_aot::NativeRegionEntryCount(0xc0), ends = mvu_aot::NativeEndCount();
        const auto jobs = mvu_aot::GetStats().automaticJobs;
        const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings);
        Check(mVUProgCache::GetStats(1).payloadHits > before.payloadHits &&
            mVUPersist::GetBlockCompileCount(1) == compiles,
            "fresh-ASLR caller hydration compiles no replacement baseline blocks");
        Check(mvu_aot::NativeRegionEntryCount(0xc0) > entered && mvu_aot::NativeEndCount() > ends &&
            mvu_aot::PendingCount() == 0 && mvu_aot::GetStats().automaticJobs == jobs,
            "fresh held-worker reader reaches saved C region through rebuilt baseline static inbound links");
        Compare(expected, actual, "fresh-ASLR static-inbound region preserves complete architecture");
        Check(Model(boundaries[0], actual) && DistinctPQ(actual) && expected.regs.cycle == actual.regs.cycle &&
            header.rings == rings && expected.regs.pending_p == actual.regs.pending_p &&
            expected.regs.pending_q == actual.regs.pending_q,
            "fresh-ASLR static inbound execution preserves literal goldens exact cycles PQ and rings");
        std::printf("AOT_STATIC_INBOUND_COLD compiles=%llu entries=%llu ends=%llu jobs=%llu pending=%u\n",
            static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(1) - compiles),
            static_cast<unsigned long long>(mvu_aot::NativeRegionEntryCount(0xc0) - entered),
            static_cast<unsigned long long>(mvu_aot::NativeEndCount() - ends),
            static_cast<unsigned long long>(mvu_aot::GetStats().automaticJobs - jobs), mvu_aot::PendingCount());
        mvu_aot::HoldWorker(false); mVUProgCache::TestClose(1);
    }
    mvu_aot::SetTierUpRuns(200); mvu_aot::HoldWorker(false);
    mvu_aot::CountEntries(false); mvu_aot::ForceSynchronous(false);
}
