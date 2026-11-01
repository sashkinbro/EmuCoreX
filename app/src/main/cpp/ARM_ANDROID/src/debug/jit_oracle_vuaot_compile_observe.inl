// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after RunPersistedVU. Observe real compilation, not a copied graph model.
void RunVuAotCompileObservationTests()
{
    cpuinfo_initialize(); EmuConfig=Pcsx2Config();
    EmuConfig.Speedhacks.vuThread=false; EmuConfig.Speedhacks.vuFlagHack=false;
    if(!SysMemory::Allocate()){Check(false,"allocate compile-observation fixture");return;}
    CpuVU0=&CpuMicroVU0;CpuVU1=&CpuMicroVU1;CpuMicroVU0.Reserve();CpuMicroVU1.Reserve();
    const auto cache=EmuFolders::Cache;EmuFolders::Cache.clear();
    const ScopedGuard restore([&]{
        mvu_aot::HoldWorker(false);mvu_aot::TestHoldReadyPublication(false);mvu_aot::WaitWorker();
        mvu_aot::SetCompileObservationForTest(false);
        EmuConfig.Cpu.Recompiler.EnableVUAot=false;CpuMicroVU1.Reset();
        mvu_aot::ForceSynchronous(false);mvu_aot::CountEntries(false);EmuFolders::Cache=cache;
    });
    std::vector<u32> code;for(u32 pc=0x80;pc<=0x100;pc+=8)code.insert(code.end(),{NOP_L,NOP_U});
    const auto put=[&](u32 pc,u32 lower,u32 upper=NOP_U){code[(pc-0x80)/4]=lower;code[(pc-0x80)/4+1]=upper;};
    const u32 add=(15u<<21)|(2u<<16)|(1u<<11)|(3u<<6)|0x28u;
    const u32 mul=(15u<<21)|(2u<<16)|(1u<<11)|(4u<<6)|0x2au;
    put(0x80,NOP_L,add);put(0x88,(0x20u<<25)|3u); // B toA8
    put(0x90,(8u<<25)|(11u<<16)|0x333u);
    put(0xa8,(8u<<25)|(10u<<16)|0x222u,mul);put(0xb0,NOP_L,NOP_U|0x40000000u);
    put(0xd0,NOP_L,mul);put(0xd8,(0x20u<<25)|2u);
    put(0xe0,(8u<<25)|(12u<<16)|0x444u);
    put(0xf0,(8u<<25)|(13u<<16)|0x555u,add);put(0xf8,NOP_L,NOP_U|0x40000000u);
    std::array<u32,12> expectedRings{},rings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot=false;
    const auto expected=RunPersistedVU(1,code,0x80,128,true,expectedRings);
    Check(expected.regs.VI[10].UL==0x222&&expected.regs.VI[11].UL==0x333&&expected.regs.VF[3].UL[0]==0x40006000,
        "independent delay leaf and exact ADD goldens identify real measured native work");
    EmuConfig.Cpu.Recompiler.EnableVUAot=true;mvu_aot::CountEntries(true);mvu_aot::ForceSynchronous(true);
    mvu_aot::SetCompileObservationForTest(false);
    auto entered=mvu_aot::NativeRegionEntryCount(0x80);
    auto actual=RunPersistedVU(1,code,0x80,128,true,rings);
    Compare(expected,actual,"disabled observer actual compilation preserves full architecture");
    auto capture=mvu_aot::GetCompileObservationForTest();
    Check(mvu_aot::NativeRegionEntryCount(0x80)>entered&&capture.stats.scopes==0&&capture.stats.emittedNodes==0&&
        capture.stats.sinkAllocations==0&&capture.stats.ownerCpuNs==0&&capture.stats.workerCpuNs==0&&capture.nodes.empty(),
        "explicitly disabled real native compiler has zero observer counters clocks and samples");
    mvu_aot::SetCompileObservationForTest(true);entered=mvu_aot::NativeRegionEntryCount(0x80);
    rings={};actual=RunPersistedVU(1,code,0x80,128,true,rings);
    Compare(expected,actual,"enabled observer leaves native architecture unchanged");
    Check(mvu_aot::NativeRegionEntryCount(0x80)>entered&&actual.regs.cycle==expected.regs.cycle&&rings==expectedRings,
        "enabled measurement still executes a genuine two-node native region with exact cycles and rings");
    capture=mvu_aot::GetCompileObservationForTest();
    Check(capture.stats.ownerJobs>0&&capture.stats.acceptedScopes>0&&capture.stats.emittedNodes>=2&&capture.stats.bodyBytes>0&&
        capture.stats.sinkAllocations>0&&capture.stats.sinkRequestedBytes>=16u*1024u*1024u&&
        capture.stats.ownerCpuNs>0&&capture.stats.workerCpuNs==0,
        "actual owner job body emissions scratch allocation and thread CPU are observed only when enabled");
    bool root=false,leaf=false,spans=!capture.nodes.empty();
    for(const auto& n:capture.nodes){root|=n.pc==0x80;leaf|=n.pc==0xa8;
        spans&=n.bytes>0&&!(n.offset&3)&&!(n.bytes&3)&&u64(n.offset)+n.bytes<=n.codeBytes;}
    Check(root&&leaf&&spans,"real node body samples have exact PCs and bounded aligned body offsets not entry-stub guesses");
    mvu_aot::ForceSynchronous(false);mvu_aot::HoldWorker(true);mvu_aot::TestHoldReadyPublication(true);
    vuaot::PipeState key;
    Check(mvu_aot::TestCompileWarmRoot(0xd0,key.bytes),"actual owner compiler remembers a new D0 toF0 path");
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);
    Check(mvu_aot::PendingCount()==1,"first real automatic child job waits on the held worker");
    mvu_aot::HoldWorker(false);mvu_aot::WaitWorker();
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);
    Check(mvu_aot::ReadyCountForTest()==1&&mvu_aot::PendingCount()==1,
        "accepted first region and next actual root job provide a real compatible cross-root overlap opportunity");
    mvu_aot::WaitWorker();rings={};RunPersistedVU(1,code,0x80,128,false,rings);
    capture=mvu_aot::GetCompileObservationForTest();
    bool overlapSample=false;for(const auto& n:capture.nodes)overlapSample|=n.worker&&n.rootPC==0xf0&&n.pc==0xf0&&(n.overlap&2)&&!(n.overlap&4)&&n.bytes>0;
    Check(mvu_aot::ReadyCountForTest()==2&&capture.stats.workerJobs>=2&&capture.stats.workerCpuNs>0&&
        capture.stats.readyOverlapNodes>0&&capture.stats.readyOverlapBytes>0&&overlapSample,
        "actual later F0 worker emission overlaps the earlier ready D0 region F0 with exact identity bytes and thread CPU");
    bool inlineUnknown=false;
    for(const auto& n:capture.nodes)inlineUnknown|=!n.worker&&(n.pc==0xd0||n.pc==0xf0)&&(n.overlap&4);
    Check(capture.stats.inlineBodies>0&&capture.stats.unknownOverlapNodes>0&&inlineUnknown,
        "real inline compiler scopes mark unsnapshotted graph overlap unknown");
    Check(mvu_aot::RetainedCompileObservationBytesForTest()==0,
        "actual ready and published job handoffs retain no per-job observation payload");
    mvu_aot::TestHoldReadyPublication(false);
    for(u32 run=0;run<8;++run){mvu_aot::WaitWorker();rings={};RunPersistedVU(1,code,0x80,128,false,rings);}
    rings={};actual=RunPersistedVU(1,code,0x80,128,false,rings);
    Compare(expected,actual,"observed worker publication preserves original native architecture");
    // A real different PQ key at the same PC must not be counted reusable.
    CpuMicroVU1.Reset();mvu_aot::ForceSynchronous(true);
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);mvu_aot::ForceSynchronous(false);
    mvu_aot::TestHoldReadyPublication(true);vuaot::PipeState late;late.p=17;late.q=7;
    Check(mvu_aot::TestCompileWarmRoot(0xf0,late.bytes),"different-key real F0 compiler root is remembered");
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);mvu_aot::WaitWorker();
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);
    Check(mvu_aot::ReadyCountForTest()==1&&mvu_aot::PendingCount()==0,
        "different-PQ F0 is accepted-ready before another root compiles a distinct F0 key");
    Check(mvu_aot::TestCompileWarmRoot(0xd0,key.bytes),"default-key D0 creates an actual distinct F0 pipeline variant");
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);mvu_aot::WaitWorker();
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);
    capture=mvu_aot::GetCompileObservationForTest();
    bool lateF0=false,defaultD0F0=false;
    for(const auto& n:capture.nodes)
    {
        lateF0|=n.worker&&n.rootPC==0xf0&&n.pc==0xf0&&n.key[2]==7&&n.key[3]==17;
        // Measure the D0 scope against only the prior late-PQ ready F0.
        // A later default F0 job may legitimately overlap D0; global totals
        // cannot be used as a different-key negative for that later scope.
        defaultD0F0|=n.worker&&n.rootPC==0xd0&&n.pc==0xf0&&n.key[2]==0&&n.key[3]==0&&n.overlap==0;
    }
    Check(capture.stats.workerJobs>=2&&lateF0&&defaultD0F0,
        "actual D0 toF0 default-key scope never labels the prior incompatible ready PQ key as reusable");
    // Hold a real additional pipeline job, then disable its observation epoch
    // before completion. Its stale handoff must free payload without reviving data.
    mvu_aot::WaitWorker();rings={};RunPersistedVU(1,code,0x80,128,false,rings);
    mvu_aot::HoldWorker(true);vuaot::PipeState stale;stale.p=9;stale.q=5;
    const bool staleRoot=mvu_aot::TestCompileWarmRoot(0xd0,stale.bytes);
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);
    Check(staleRoot&&mvu_aot::PendingCount()==1,
        "actual distinct pipeline job is held before observation epoch disable");
    mvu_aot::SetCompileObservationForTest(false);mvu_aot::HoldWorker(false);mvu_aot::WaitWorker();
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);
    capture=mvu_aot::GetCompileObservationForTest();
    Check(mvu_aot::RetainedCompileObservationBytesForTest()==0&&capture.stats.scopes==0&&capture.nodes.empty(),
        "disabled stale-epoch worker handoff releases payload and cannot revive aggregate samples");
    mvu_aot::SetCompileObservationForTest(true);
    mvu_aot::HoldWorker(false);mvu_aot::TestHoldReadyPublication(false);CpuMicroVU1.Reset();
    capture=mvu_aot::GetCompileObservationForTest();
    Check(capture.stats.scopes==0&&capture.stats.emittedNodes==0&&capture.nodes.empty(),
        "actual reset stops the worker then clears generation-owned observation state");
    mvu_aot::SetCompileObservationForTest(false);mvu_aot::ForceSynchronous(true);
    rings={};RunPersistedVU(1,code,0x80,128,false,rings);capture=mvu_aot::GetCompileObservationForTest();
    Check(capture.stats.scopes==0&&capture.stats.sinkAllocations==0&&capture.nodes.empty(),
        "explicit disable after real worker activity retains zero observer state on new compilation");
}
