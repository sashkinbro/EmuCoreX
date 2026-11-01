// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unlike the reset-based cold fixtures, the writer must persist BOTH baseline
// payload and native sidecar during ordinary owner dispatches before any Reset.
void RunVuAotQuietCacheSafetyTests(const vuaot::persist::File& original,const std::vector<u32>& code,u64 nonce);
void RunVuAotQuietCacheTests(bool writer)
{
    using namespace VuCompileBranchOracle;
    cpuinfo_initialize();
    const auto savedConfig = EmuConfig; const auto savedCache = EmuFolders::Cache;
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Cpu.Recompiler.EnableVUAot = true; EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
    EmuFolders::Cache = "/data/local/tmp/emucorex-vu-cold-cache/quiet-dispatch";
    const ScopedGuard restore([&] { EmuConfig = savedConfig; EmuFolders::Cache = savedCache; });
    if (!FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(), true) || !SysMemory::Allocate())
    {
        Check(false, "allocate isolated quiet-dispatch native cache"); return;
    }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    mVUPersist::SetProcessDisable(false); mVUPersist::SetTestManualRecording(false);
    mvu_aot::CountEntries(true); mvu_aot::ForceSynchronous(writer); mvu_aot::SetTierUpRuns(1);
    const void* const symbol = reinterpret_cast<const void*>(&mVUPersist::GetStats);
    mVUPersist::AddressRegion region{}; s64 offset = 0;
    const bool image = mVUPersist::ClassifyAddressRegion(symbol, region, offset) && region == mVUPersist::AddressRegion::Image;
    Check(image, "quiet cache classifies its actual module image");
    if (!image) return;
    const u64 base = reinterpret_cast<uintptr_t>(symbol) - offset;
    const auto code = Program(boundaries[0], true);
    const std::string expectedPath = EmuFolders::Cache + "/expected.bin";
    VUColdCacheHeader header; Snapshot expected{};
    if (writer)
    {
        header.nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        header.imageBase = base; header.vu = 1; header.aot = 1;
    }
    else
    {
        const bool loaded = VUColdCacheFile(expectedPath, false, header, expected);
        Check(loaded && header.vu == 1 && header.aot == 1, "quiet reader loads prior independent full expected state");
        if (!loaded || header.vu != 1 || header.aot != 1) return;
        Check(header.imageBase != base, "quiet reader runs at a different module ASLR base");
    }
    mVUProgCache::SetTitle("ORACLE-QUIET-DISPATCH-" + std::to_string(header.nonce), 0xc01dca41);
    std::array<u32, 12> rings{};
    if (writer)
    {
        EmuConfig.Cpu.Recompiler.EnableVUAot = false; EmuConfig.Cpu.Recompiler.EnableVUProgramCache = false;
        expected = RunPersistedVU(1, code, 0x80, 128, true, header.rings);
        Check(Model(boundaries[0], expected) && DistinctPQ(expected),
            "quiet writer expected state satisfies independent control goldens and real ESUM DIV PQ");
        EmuConfig.Cpu.Recompiler.EnableVUAot = true; EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
        mvu_aot::HoldWorker(false);
        RunPersistedVU(1, code, 0x80, 128, true, rings); // sole reset before any native region exists
        const auto before = mVUProgCache::GetStats(1);
        const auto entered = mvu_aot::NativeRegionEntryCount(0xc0), ends = mvu_aot::NativeEndCount();
        for (u32 run = 0; run < 8; ++run)
        {
            rings = {}; RunPersistedVU(1, code, 0x80, 128, false, rings);
        }
        Check(mvu_aot::NativeRegionEntryCount(0xc0) > entered && mvu_aot::NativeEndCount() > ends,
            "quiet writer really publishes and enters automatic C native region before its quiet interval");
        vuaot::persist::File early;
        Check(mVUProgCache::GetStats(1).payloadWrites==before.payloadWrites && !mvu_aot::ReadSavedForTest(early),
            "native publication alone does not checkpoint before the quiet interval");
        const auto compiles = mVUPersist::GetBlockCompileCount(1), jobs = mvu_aot::GetStats().automaticJobs;
        Snapshot actual{};
        for (u32 run = 0; run < 1152; ++run)
        {
            rings = {}; actual = RunPersistedVU(1, code, 0x80, 128, false, rings);
        }
        Check(mVUPersist::GetBlockCompileCount(1) == compiles && mvu_aot::GetStats().automaticJobs == jobs &&
            mvu_aot::PendingCount() == 0, "quiet interval performs no baseline compilation or new automatic worker job");
        Compare(expected, actual, "quiet-dispatch writer preserves complete architecture");
        Check(expected.regs.cycle == actual.regs.cycle && header.rings == rings && DistinctPQ(actual),
            "quiet-dispatch writer preserves exact cycles real pending PQ and all flag rings");
        // Existing writer only: this must not itself create either snapshot.
        // Flush precedes ReadSavedForTest so a read cannot build a queued file.
        mVUProgCache::FlushSidecars(1);
        const auto after = mVUProgCache::GetStats(1);
        vuaot::persist::File saved;
        const bool read = mvu_aot::ReadSavedForTest(saved);
        const bool native = read && std::any_of(saved.records.begin(), saved.records.end(), [](const auto& record) {
            return (record.flags & vuaot::persist::kHasCode) != 0;
        });
        Check(after.payloadWrites > before.payloadWrites && after.sidecarWrites > before.sidecarWrites && native,
            "ordinary quiet dispatch persists a coherent baseline payload and native sidecar before any reset");
        std::printf("AOT_QUIET_WRITER payload_writes=%llu sidecar_writes=%llu readable=%d native=%d\n",
            static_cast<unsigned long long>(after.payloadWrites - before.payloadWrites),
            static_cast<unsigned long long>(after.sidecarWrites - before.sidecarWrites), read, native);
        if(native) RunVuAotQuietCacheSafetyTests(saved,code,header.nonce);
        const auto pack = mVUProgCache::TestPackPath(1);
        mVUProgCache::TestClose(1);
        Check(pack.find(EmuFolders::Cache + "/") == 0 && FileSystem::FileExists(pack.c_str()),
            "quiet writer closes only the already checkpointed isolated pack without resetting its owner");
        header.memorySize = expected.memory.size(); header.gifSize = expected.gif.size();
        header.busy = expected.busy; header.fallback = expected.fallback;
        expected.regs.Micro = nullptr; expected.regs.Mem = nullptr;
        Check(VUColdCacheFile(expectedPath, true, header, expected), "quiet writer saves independent expected state");
    }
    else
    {
        CpuMicroVU1.Reset(); mVUProgCache::TestWaitForPreload(1); mvu_aot::HoldWorker(true);
        const auto before = mVUProgCache::GetStats(1);
        Check(before.loadedRecords > 0, "quiet reader preloads the prior process baseline graph");
        const auto compiles = mVUPersist::GetBlockCompileCount(1), jobs = mvu_aot::GetStats().automaticJobs;
        const auto entered = mvu_aot::NativeRegionEntryCount(0xc0), ends = mvu_aot::NativeEndCount();
        const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings);
        Check(mVUProgCache::GetStats(1).payloadHits > before.payloadHits && mVUPersist::GetBlockCompileCount(1) == compiles,
            "quiet-saved baseline hydrates at fresh ASLR without baseline compilation");
        Check(mvu_aot::NativeRegionEntryCount(0xc0) > entered && mvu_aot::NativeEndCount() > ends &&
            mvu_aot::PendingCount() == 0 && mvu_aot::GetStats().automaticJobs == jobs,
            "quiet-saved native C executes with held worker and zero new job");
        Compare(expected, actual, "fresh-ASLR quiet-saved C preserves complete architecture");
        Check(Model(boundaries[0], actual) && DistinctPQ(actual) && expected.regs.cycle == actual.regs.cycle &&
            header.rings == rings && expected.regs.pending_p == actual.regs.pending_p && expected.regs.pending_q == actual.regs.pending_q,
            "quiet-saved C preserves literal goldens exact cycles PQ and every flag ring");
        const auto clean=mVUProgCache::GetStats(1);
        const auto cleanQueues=mvu_aot::GetStats().quietSaveQueues;
        for(u32 run=0;run<1152;++run) { rings={}; RunPersistedVU(1,code,0x80,128,false,rings); }
        mVUProgCache::FlushSidecars(1);
        Check(!mvu_aot::QuietSaveDirtyForTest() && mvu_aot::GetStats().quietSaveQueues==cleanQueues &&
            mVUProgCache::GetStats(1).sidecarWrites==clean.sidecarWrites &&
            mVUProgCache::GetStats(1).payloadWrites==clean.payloadWrites &&
            mVUPersist::GetBlockCompileCount(1)==compiles && mvu_aot::PendingCount()==0,
            "unchanged imported plans remain clean beyond another quiet interval without rewrite compile or job");
        mvu_aot::HoldWorker(false); mVUProgCache::TestClose(1);
    }
    mvu_aot::SetTierUpRuns(200); mvu_aot::HoldWorker(false);
    mvu_aot::CountEntries(false); mvu_aot::ForceSynchronous(false);
}

void RunVuAotQuietCacheSafetyTests(const vuaot::persist::File& original,const std::vector<u32>& code,u64 nonce)
{
    namespace wire=vuaot::persist;
    std::array<u32,12> rings{};
    const auto run=[&](u32 count) {
        for(u32 i=0;i<count;++i) { rings={}; RunPersistedVU(1,code,0x80,128,false,rings); }
    };
    const auto clean=mVUProgCache::GetStats(1);
    const auto cleanQueues=mvu_aot::GetStats().quietSaveQueues;
    run(1152); mVUProgCache::FlushSidecars(1);
    Check(!mvu_aot::QuietSaveDirtyForTest() && mvu_aot::GetStats().quietSaveQueues==cleanQueues &&
        mVUProgCache::GetStats(1).sidecarWrites==clean.sidecarWrites,
        "acknowledged quiet snapshots remain clean and are not queued repeatedly");
    mVUProgCache::TestRejectSidecarQueue(true); mvu_aot::TestDirtyQuietSave();
    const auto attempts=mvu_aot::GetStats().quietSaveAttempts;
    run(1024);
    Check(mvu_aot::QuietSaveDirtyForTest() && mvu_aot::GetStats().quietSaveAttempts==attempts+1 &&
        mvu_aot::GetStats().quietSaveQueues==cleanQueues,
        "a real cache queue rejection leaves the owner dirty instead of acknowledging a lost snapshot");
    run(1023);
    Check(mvu_aot::QuietSaveDirtyForTest() && mvu_aot::GetStats().quietSaveAttempts==attempts+1,
        "failed snapshot is not retried on hot dispatches inside the bounded retry interval");
    mVUProgCache::TestRejectSidecarQueue(false); run(1); mVUProgCache::FlushSidecars(1);
    Check(!mvu_aot::QuietSaveDirtyForTest() && mvu_aot::GetStats().quietSaveAttempts==attempts+2 &&
        mvu_aot::GetStats().quietSaveQueues==cleanQueues+1,
        "the next bounded retry queues successfully and only then clears dirty ownership");

    auto plans=original;
    const auto root=std::find_if(plans.records.begin(),plans.records.end(),[](const auto& r) { return r.pc==0xc0; });
    Check(root!=plans.records.end(),"plan-preservation fixture starts from a real exported native C record");
    if(root==plans.records.end()) return;
    root->flags&=~wire::kHasCode; root->code={};
    auto later=*root; vuaot::PipeState laterKey; std::memcpy(laterKey.bytes,later.key,96);
    laterKey.p=17; laterKey.q=7; std::memcpy(later.key,laterKey.bytes,96); plans.records.push_back(later);
    Check(mvu_aot::QueueSavedForTest(plans),"valid native generation and two plan-only C roots are installed through the real sidecar cache");
    CpuMicroVU1.Reset(); mvu_aot::ForceSynchronous(false); mvu_aot::HoldWorker(true); mvu_aot::SetTierUpRuns(100000);
    const auto submitted=mvu_aot::GetStats().savedPlanJobs;
    run(1);
    Check(mvu_aot::PendingCount()==1 && mvu_aot::GetStats().savedPlanJobs==submitted+1,
        "loaded C source plan is actually submitted to a held worker while another loaded root remains unsubmitted");
    // Deliberately mark metadata dirty to test the snapshot acknowledgment
    // state machine; the plans and pending worker ownership are entirely real.
    mvu_aot::TestDirtyQuietSave(); CpuMicroVU1.Reset(); mVUProgCache::FlushSidecars(1);
    const XXH128_hash_t hash{original.id.contentHashLo,original.id.contentHashHi};
    std::vector<u8> bytes; wire::File restored;
    const bool loaded=mVUProgCache::ReadSidecar(1,hash,bytes) &&
        wire::Parse(bytes.data(),bytes.size(),original.id,restored)==wire::ParseResult::Ok;
    const auto contains=[&](const wire::Record& wanted) {
        return loaded && std::any_of(restored.records.begin(),restored.records.end(),[&](const auto& r) {
            return r.pc==wanted.pc && std::memcmp(r.key,wanted.key,96)==0 && !r.HasCode();
        });
    };
    const auto first=std::find_if(plans.records.begin(),plans.records.end(),[](const auto& r) { return r.pc==0xc0; });
    Check(loaded && contains(*first) && contains(later) && restored.records.size()>=plans.records.size(),
        "dirty reset snapshot retains both the held submitted root and the richer unsubmitted loaded plan");
    mvu_aot::HoldWorker(false);
    Check(mvu_aot::QueueSavedForTest(original),"restore original complete native records for the separate ASLR reader");

    // The existing cache drops a late builder's result after its sidecar
    // generation changes. The builder owns bytes only, never a VU owner.
    const auto serialized=std::make_shared<const std::vector<u8>>(wire::Serialize(original));
    const auto generation=mVUProgCache::TestSidecarGeneration(1);
    const auto entered=std::make_shared<std::atomic<bool>>(false), release=std::make_shared<std::atomic<bool>>(false);
    const bool queued=mVUProgCache::QueueSidecar(1,hash,[serialized,entered,release] {
        entered->store(true,std::memory_order_release);
        while(!release->load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return *serialized;
    });
    std::thread flush([] { mVUProgCache::FlushSidecars(1); });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!entered->load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool building=entered->load(std::memory_order_acquire);
    bool changed=false;
    std::thread clear;
    if(building)
    {
        clear=std::thread([] { mVUProgCache::ClearVuAot(false); });
        const auto clearDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(mVUProgCache::TestSidecarGeneration(1)==generation && std::chrono::steady_clock::now()<clearDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        changed=mVUProgCache::TestSidecarGeneration(1)!=generation;
    }
    release->store(true,std::memory_order_release); flush.join(); if(clear.joinable()) clear.join();
    bytes.clear();
    Check(queued && building && changed && !mVUProgCache::ReadSidecar(1,hash,bytes),
        "cache clear generation rejects an already executing immutable snapshot instead of reviving cleared native code");
    Check(mvu_aot::QueueSavedForTest(original),"new explicit native checkpoint remains valid after generation rejection");
    const auto oldPath=mVUProgCache::TestPackPath(1);
    mVUProgCache::SetTitle("ORACLE-QUIET-OTHER-"+std::to_string(nonce),0xc01dca42); CpuMicroVU1.Reset();
    mVUProgCache::TestWaitForPreload(1); bytes.clear();
    Check(mVUProgCache::TestPackPath(1)!=oldPath && !mVUProgCache::ReadSidecar(1,hash,bytes),
        "a different title cannot read a quiet snapshot from the previous title pack");
    mVUProgCache::TestClose(1);
    bool called=false;
    Check(!mVUProgCache::QueueSidecar(1,hash,[&] { called=true; return std::vector<u8>{}; }) && !called,
        "disabled cache rejects the snapshot request without running its builder");
    mVUProgCache::SetTitle("ORACLE-QUIET-DISPATCH-"+std::to_string(nonce),0xc01dca41); CpuMicroVU1.Reset();
    mVUProgCache::TestWaitForPreload(1);
    Check(mvu_aot::QueueSavedForTest(original),"final original native checkpoint is restored in its original title pack");
}
