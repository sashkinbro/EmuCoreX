// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// A native generation has not yet published. Its original generation record
// must precede queued root records when pending/ready ownership is snapshotted.
void RunVuAotQuietGenerationTests(bool coldWriter=false)
{
    using namespace VuCompileBranchOracle;
    namespace wire=vuaot::persist;
    cpuinfo_initialize(); EmuConfig=Pcsx2Config();
    EmuConfig.Speedhacks.vuThread=false; EmuConfig.Speedhacks.vuFlagHack=false;
    EmuConfig.Cpu.Recompiler.EnableVUAot=true; EmuConfig.Cpu.Recompiler.EnableVUProgramCache=true;
    const auto previousCache=EmuFolders::Cache;
    EmuFolders::Cache="/data/local/tmp/emucorex-vu-cold-cache/quiet-generation";
    const ScopedGuard restore([&] { EmuFolders::Cache=previousCache; });
    if(!FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(),true) || !SysMemory::Allocate())
    { Check(false,"allocate isolated pending generation cache");return; }
    CpuVU0=&CpuMicroVU0; CpuVU1=&CpuMicroVU1; CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const ScopedGuard cleanup([&] {
        mvu_aot::TestClearQuietWorkerQueue(); // remove only our held identity, never real jobs
        mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false);
        mvu_aot::SetTierUpRuns(200); mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false);
        CpuMicroVU1.Reset();
    });
    mVUPersist::SetProcessDisable(false); mVUPersist::SetTestManualRecording(false);
    const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
    mVUProgCache::SetTitle("ORACLE-QUIET-GENERATION-"+std::to_string(nonce),0xc01dca43);
    const auto code=Program(boundaries[0],true);
    std::array<u32,12> rings{},expectedRings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot=false; EmuConfig.Cpu.Recompiler.EnableVUProgramCache=false;
    const auto expected=RunPersistedVU(1,code,0x80,128,true,expectedRings);
    EmuConfig.Cpu.Recompiler.EnableVUAot=true; EmuConfig.Cpu.Recompiler.EnableVUProgramCache=true;
    mvu_aot::CountEntries(true); mvu_aot::ForceSynchronous(true); mvu_aot::SetTierUpRuns(1);
    RunPersistedVU(1,code,0x80,128,true,rings);
    for(u32 run=0;run<1160;++run) { rings={};RunPersistedVU(1,code,0x80,128,false,rings); }
    mVUProgCache::FlushSidecars(1);
    wire::File original;
    const bool have=mvu_aot::ReadSavedForTest(original);
    Check(have && original.records.size()>=2 && original.records.front().kind==wire::kGeneration,
        "generation preservation starts from actual quiet-saved native generation and root records");
    if(!have || original.records.size()<2) return;
    const XXH128_hash_t hash{original.id.contentHashLo,original.id.contentHashHi};
    wire::File readyOnlySnapshot;
    for(u32 mode=0;mode<4;++mode)
    {
        auto plans=original;
        for(auto& record:plans.records) { record.flags&=~wire::kHasCode;record.code={}; }
        if(mode==2) plans.records.resize(1); // ready-only generation, no queued roots
        mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false);
        Check(mvu_aot::QueueSavedForTest(plans),"install valid plan-only generation through the real sidecar cache");
        CpuMicroVU1.Reset(); mvu_aot::ForceSynchronous(false); mvu_aot::SetTierUpRuns(100000);
        mvu_aot::HoldWorker(mode==0 || mode==3); mvu_aot::TestHoldReadyPublication(mode==1 || mode==2);
        const auto submitted=mvu_aot::GetStats().savedPlanJobs;
        if(mode==3) Check(mvu_aot::TestFillQuietWorkerQueue()==256,"real held worker queue reaches its actual submission capacity");
        rings={}; const auto cold=RunPersistedVU(1,code,0x80,128,false,rings);
        Compare(expected,cold,"plan-only generation baseline execution preserves complete architecture");
        Check(expected.regs.cycle==cold.regs.cycle && rings==expectedRings,
            "unpublished generation preserves exact cycles and all rings");
        if(mode==3)
        {
            Check(mvu_aot::PendingCount()==0 && mvu_aot::SavedPlanCountForTest()==plans.records.size() &&
                mvu_aot::GetStats().savedPlanJobs==submitted,
                "actual full-queue Submit failure retains every unsubmitted loaded record");
            mvu_aot::TestClearQuietWorkerQueue();
            rings={};RunPersistedVU(1,code,0x80,128,false,rings);
            Check(mvu_aot::PendingCount()==1 && mvu_aot::GetStats().savedPlanJobs==submitted+1,
                "the same preserved generation submits after real worker queue capacity is restored");
        }
        else if(mode)
        {
            mvu_aot::WaitWorker();
            rings={};RunPersistedVU(1,code,0x80,128,false,rings);
            mvu_aot::HoldWorker(true);
            Check(mvu_aot::ReadyCountForTest()>=1,"actual accepted generation remains ready while owner publication is held");
        }
        else Check(mvu_aot::PendingCount()==1,"actual plan-only generation remains pending in a held worker");
        const auto queues=mvu_aot::GetStats().quietSaveQueues;
        mvu_aot::TestDirtyQuietSave(); CpuMicroVU1.Reset(); mVUProgCache::FlushSidecars(1);
        Check(mvu_aot::GetStats().quietSaveQueues==queues+1,
            "dirty reset acknowledges a snapshot even when the only accepted generation is unpublished");
        std::vector<u8> bytes; wire::File snapshot;
        const bool read=mVUProgCache::ReadSidecar(1,hash,bytes);
        const auto parsed=read?wire::Parse(bytes.data(),bytes.size(),original.id,snapshot):wire::ParseResult::Malformed;
        bool complete=parsed==wire::ParseResult::Ok && !snapshot.records.empty() && snapshot.records.front().kind==wire::kGeneration;
        for(const auto& wanted:plans.records)
            complete &= std::any_of(snapshot.records.begin(),snapshot.records.end(),[&](const auto& record) {
                return record.kind==wanted.kind && record.pc==wanted.pc && std::memcmp(record.key,wanted.key,96)==0;
            });
        Check(complete,"reset snapshot keeps a valid generation-first file and every pending ready or unsubmitted root plan");
        if(mode==2 && complete && coldWriter) readyOnlySnapshot=snapshot;
        std::printf("AOT_QUIET_GENERATION mode=%u queued=%llu parsed=%u records=%zu expected=%zu\n",mode,
            static_cast<unsigned long long>(mvu_aot::GetStats().quietSaveQueues-queues),static_cast<u32>(parsed),
            snapshot.records.size(),plans.records.size());
        mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false);
        Check(mvu_aot::QueueSavedForTest(original),"restore the complete native generation before the next plan lifecycle case");
    }
    if(coldWriter)
    {
        Check(readyOnlySnapshot.records.size()==1 && !readyOnlySnapshot.records.front().HasCode() &&
            mvu_aot::QueueSavedForTest(readyOnlySnapshot),
            "cold writer persists the actual accepted-ready generation plan as its final sidecar");
        VUColdCacheHeader header;
        header.nonce=nonce;header.vu=1;header.aot=1;header.rings=expectedRings;
        const void* symbol=reinterpret_cast<const void*>(&mVUPersist::GetStats);
        mVUPersist::AddressRegion region{};s64 offset=0;
        const bool image=mVUPersist::ClassifyAddressRegion(symbol,region,offset) && region==mVUPersist::AddressRegion::Image;
        Check(image,"cold ready-generation writer records its actual module ASLR base");
        header.imageBase=reinterpret_cast<uintptr_t>(symbol)-offset;
        auto persisted=expected;
        header.memorySize=persisted.memory.size();header.gifSize=persisted.gif.size();
        header.busy=persisted.busy;header.fallback=persisted.fallback;
        persisted.regs.Micro=nullptr;persisted.regs.Mem=nullptr;
        Check(VUColdCacheFile(EmuFolders::Cache+"/ready-only-expected.bin",true,header,persisted),
            "cold ready-generation writer saves independent full expected architecture and rings");
    }
    mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false);
    mvu_aot::SetTierUpRuns(200); mvu_aot::ForceSynchronous(false);mvu_aot::CountEntries(false);
    mVUProgCache::TestClose(1);
}
