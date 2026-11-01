// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// The previous process saved an accepted-ready GENERATION as its original
// plan, before publication. A held reader must hydrate baseline, not claim
// native C; releasing the real worker must then rebuild and enter native C.
void RunVuAotQuietGenerationColdReadTests()
{
    using namespace VuCompileBranchOracle;
    namespace wire=vuaot::persist;
    cpuinfo_initialize(); EmuConfig=Pcsx2Config();
    EmuConfig.Speedhacks.vuThread=false; EmuConfig.Speedhacks.vuFlagHack=false;
    EmuConfig.Cpu.Recompiler.EnableVUAot=true; EmuConfig.Cpu.Recompiler.EnableVUProgramCache=true;
    const auto previousCache=EmuFolders::Cache;
    EmuFolders::Cache="/data/local/tmp/emucorex-vu-cold-cache/quiet-generation";
    const ScopedGuard restore([&] { EmuFolders::Cache=previousCache; });
    if(!SysMemory::Allocate()) { Check(false,"allocate fresh ready-generation reader memory");return; }
    CpuVU0=&CpuMicroVU0; CpuVU1=&CpuMicroVU1; CpuMicroVU0.Reserve();CpuMicroVU1.Reserve();
    const ScopedGuard cleanup([&] {
        mvu_aot::HoldWorker(false);mvu_aot::TestHoldReadyPublication(false);
        mvu_aot::SetTierUpRuns(200);mvu_aot::CountEntries(false);mvu_aot::ForceSynchronous(false);
    });
    VUColdCacheHeader header;Snapshot expected{};
    const bool loaded=VUColdCacheFile(EmuFolders::Cache+"/ready-only-expected.bin",false,header,expected);
    Check(loaded && header.vu==1 && header.aot==1,"fresh reader loads independent ready-generation expected state");
    if(!loaded || header.vu!=1 || header.aot!=1)return;
    mVUPersist::AddressRegion region{};s64 offset=0;
    const void* symbol=reinterpret_cast<const void*>(&mVUPersist::GetStats);
    const bool image=mVUPersist::ClassifyAddressRegion(symbol,region,offset) && region==mVUPersist::AddressRegion::Image;
    Check(image && header.imageBase!=reinterpret_cast<uintptr_t>(symbol)-offset,
        "ready-generation reader uses a different actual module ASLR base");
    mVUPersist::SetProcessDisable(false);mVUPersist::SetTestManualRecording(false);
    mvu_aot::CountEntries(true);mvu_aot::ForceSynchronous(false);mvu_aot::SetTierUpRuns(100000);
    mVUProgCache::SetTitle("ORACLE-QUIET-GENERATION-"+std::to_string(header.nonce),0xc01dca43);
    CpuMicroVU1.Reset();mVUProgCache::TestWaitForPreload(1);mvu_aot::HoldWorker(true);
    const auto before=mVUProgCache::GetStats(1);
    Check(before.loadedRecords>0,"ready-generation reader preloads the old process baseline payload");
    const auto code=Program(boundaries[0],true);
    const auto compiles=mVUPersist::GetBlockCompileCount(1),jobs=mvu_aot::GetStats().savedPlanJobs;
    const auto entered=mvu_aot::NativeRegionEntryCount(0xc0),ends=mvu_aot::NativeEndCount();
    std::array<u32,12> rings{};
    const auto held=RunPersistedVU(1,code,0x80,128,false,rings);
    wire::File saved;
    Check(mvu_aot::ReadSavedForTest(saved) && saved.records.size()==1 &&
        saved.records.front().kind==wire::kGeneration && !saved.records.front().HasCode(),
        "fresh cache really contains the preserved ready-only generation plan instead of the original native sidecar");
    Check(mVUProgCache::GetStats(1).payloadHits>before.payloadHits && mVUPersist::GetBlockCompileCount(1)==compiles,
        "fresh-ASLR plan-only generation hydrates baseline with zero baseline compilation");
    Check(mvu_aot::PendingCount()==1 && mvu_aot::GetStats().savedPlanJobs==jobs+1 &&
        mvu_aot::NativeRegionEntryCount(0xc0)==entered,
        "held reader submits the preserved generation plan and executes baseline without pretending native C ran");
    Compare(expected,held,"held ready-generation reader preserves complete baseline architecture");
    Check(expected.regs.cycle==held.regs.cycle && header.rings==rings &&
        expected.regs.pending_p==held.regs.pending_p && expected.regs.pending_q==held.regs.pending_q,
        "held ready-generation reader preserves exact cycles pending PQ and all rings");
    mvu_aot::HoldWorker(false);mvu_aot::WaitWorker();
    Snapshot actual{};
    for(u32 run=0;run<12;++run)
    {
        rings={};actual=RunPersistedVU(1,code,0x80,128,false,rings);mvu_aot::WaitWorker();
    }
    Check(mvu_aot::NativeRegionEntryCount(0xc0)>entered && mvu_aot::NativeEndCount()>ends &&
        mvu_aot::PendingCount()==0 && mVUPersist::GetBlockCompileCount(1)==compiles,
        "releasing the real worker publishes generation and native C without replacing any hydrated baseline block");
    Compare(expected,actual,"fresh ready-generation replay preserves complete native architecture");
    Check(Model(boundaries[0],actual) && DistinctPQ(actual) && expected.regs.cycle==actual.regs.cycle &&
        header.rings==rings && expected.regs.pending_p==actual.regs.pending_p && expected.regs.pending_q==actual.regs.pending_q,
        "fresh replay preserves literal control goldens real nonzero PQ cycles and all rings");
    std::printf("AOT_QUIET_GENERATION_COLD compiles=%llu region=%llu ends=%llu plans=%llu pending=%u\n",
        static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(1)-compiles),
        static_cast<unsigned long long>(mvu_aot::NativeRegionEntryCount(0xc0)-entered),
        static_cast<unsigned long long>(mvu_aot::NativeEndCount()-ends),
        static_cast<unsigned long long>(mvu_aot::GetStats().savedPlanJobs-jobs),mvu_aot::PendingCount());
    mVUProgCache::TestClose(1);
}
