// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after the branch fixture and the existing VU cold-cache helpers.
// A fresh process must execute the inline branch body, not rebuild its root.
void RunVuAotCompileBranchColdTests(bool writer, bool actualPQ = false)
{
    using namespace VuCompileBranchOracle;
    cpuinfo_initialize();
    const auto savedConfig = EmuConfig;
    const auto savedCache = EmuFolders::Cache;
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
    EmuFolders::Cache = actualPQ ? "/data/local/tmp/emucorex-vu-cold-cache/compile-branch-pq" :
        "/data/local/tmp/emucorex-vu-cold-cache/compile-branch";
    const ScopedGuard restore([&] { EmuConfig = savedConfig; EmuFolders::Cache = savedCache; });
    if (!FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(), true) || !SysMemory::Allocate())
    {
        Check(false, "allocate independent branch-tail cold cache"); return;
    }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    mVUPersist::SetProcessDisable(false); mVUPersist::SetTestManualRecording(false);
    mvu_aot::CountEntries(true); mvu_aot::ForceSynchronous(writer);
    mvu_aot::SetTierUpRuns(1);
    const void* symbol = reinterpret_cast<const void*>(&mVUPersist::GetStats);
    mVUPersist::AddressRegion region{}; s64 offset = 0;
    const bool image = mVUPersist::ClassifyAddressRegion(symbol, region, offset) &&
        region == mVUPersist::AddressRegion::Image;
    Check(image, "branch cold cache classifies its actual module image");
    if (!image) return;
    const u64 base = reinterpret_cast<uintptr_t>(symbol) - offset;
    vuaot::PipeState late; late.p = 17; late.q = 7;
    // B, BAL, equality, signed negative, JR and JALR cover distinct frame tails.
    for (u32 index : {0u, 1u, 2u, 6u, 18u, 19u})
    {
        const auto& c = boundaries[index];
        const auto code = Program(c, actualPQ);
        const std::string expectedPath = EmuFolders::Cache + "/expected-" + std::to_string(index) + ".bin";
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
                "branch cold reader loads its independent prior-process expected snapshot");
            if (!loaded || header.vu != 1 || header.aot != 1) return;
            Check(header.imageBase != base, "branch-tail native cache reader has a different module ASLR base");
        }
        mVUProgCache::SetTitle("ORACLE-COLD-BRANCH-" + std::to_string(index) + "-" + std::to_string(header.nonce),
            0xc01db100 + index);
        std::array<u32, 12> rings{};
        if (writer)
        {
            EmuConfig.Cpu.Recompiler.EnableVUAot = false;
            EmuConfig.Cpu.Recompiler.EnableVUProgramCache = false;
            expected = RunPersistedVU(1, code, 0x80, 128, true, header.rings, 0x80, &late);
            Check(Model(c, expected), "cold writer expected snapshot satisfies independent branch control goldens");
            if (actualPQ) Check(DistinctPQ(expected), "cold writer expected state has actual nonzero distinct ESUM and DIV lanes");
            EmuConfig.Cpu.Recompiler.EnableVUAot = true;
            EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
            mvu_aot::HoldWorker(false);
            RunPersistedVU(1, code, 0x80, 128, true, rings);
            rings = {};
            RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
            const auto entered = mvu_aot::InlineBodyEntryCount(0xc0);
            rings = {};
            const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
            Check(mvu_aot::InlineBodyEntryCount(0xc0) > entered,
                "cold writer executes the actual late inline branch body before persistence");
            Compare(expected, actual, "branch-tail cold writer preserves complete architecture");
            Check(expected.regs.cycle == actual.regs.cycle && header.rings == rings,
                "branch-tail cold writer preserves exact cycles and all flag rings");
            mVUProgCache::TestWaitForPreload(1);
            const auto before = mVUProgCache::GetStats(1);
            CpuMicroVU1.Reset(); mVUProgCache::FlushSidecars(1);
            const auto after = mVUProgCache::GetStats(1);
            Check(after.payloadWrites > before.payloadWrites && after.sidecarWrites > before.sidecarWrites,
                "branch writer saves the inline baseline graph and external-root native sidecar");
            const auto pack = mVUProgCache::TestPackPath(1);
            mVUProgCache::TestClose(1);
            Check(pack.find(EmuFolders::Cache + "/") == 0 && FileSystem::FileExists(pack.c_str()),
                "branch writer closes an isolated native pack on disk");
            header.memorySize = expected.memory.size(); header.gifSize = expected.gif.size();
            header.busy = expected.busy; header.fallback = expected.fallback;
            expected.regs.Micro = nullptr; expected.regs.Mem = nullptr;
            Check(VUColdCacheFile(expectedPath, true, header, expected),
                "branch writer persists its independent expected snapshot");
        }
        else
        {
            CpuMicroVU1.Reset(); mVUProgCache::TestWaitForPreload(1);
            mvu_aot::HoldWorker(true);
            const auto before = mVUProgCache::GetStats(1);
            Check(before.loadedRecords > 0, "branch cold reader preloads its prior-process native graph");
            const auto compiles = mVUPersist::GetBlockCompileCount(1);
            const auto entered = mvu_aot::InlineBodyEntryCount(0xc0);
            const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
            Check(mVUProgCache::GetStats(1).payloadHits > before.payloadHits &&
                mVUPersist::GetBlockCompileCount(1) == compiles,
                "fresh ASLR branch reader uses native payload with zero baseline recompiles");
            Check(mvu_aot::InlineBodyEntryCount(0xc0) > entered && mvu_aot::PendingCount() == 0,
                "fresh held-worker reader executes the persisted inline branch body and rebased per-PC counter");
            Compare(expected, actual, "fresh ASLR branch body preserves complete architecture");
            Check(Model(c, actual) && expected.regs.cycle == actual.regs.cycle && header.rings == rings &&
                expected.regs.pending_p == actual.regs.pending_p && expected.regs.pending_q == actual.regs.pending_q,
                "fresh ASLR branch body preserves independent control goldens exact cycles PQ and rings");
            if (actualPQ) Check(DistinctPQ(actual), "fresh ASLR reader retains nonzero distinct ESUM and DIV lanes");
            std::printf("AOT_COMPILE_BRANCH_COLD case=%u compiles=%llu inline_entries=%llu pending=%u\n", index,
                static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(1) - compiles),
                static_cast<unsigned long long>(mvu_aot::InlineBodyEntryCount(0xc0) - entered), mvu_aot::PendingCount());
            mvu_aot::HoldWorker(false); mVUProgCache::TestClose(1);
        }
    }
    mvu_aot::SetTierUpRuns(200); mvu_aot::HoldWorker(false);
    mvu_aot::CountEntries(false); mvu_aot::ForceSynchronous(false);
}
