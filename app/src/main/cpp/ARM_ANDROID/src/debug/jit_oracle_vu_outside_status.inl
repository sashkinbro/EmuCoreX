// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after RunPersistedVU; VuAotLive.h must be included at TU scope.
extern "C" u64 EmuCoreXOracleVUWaitMTVUCalls();

void RunVUOutsideStatusReadTests()
{
    cpuinfo_initialize();EmuConfig=Pcsx2Config();
    EmuConfig.Speedhacks.vuThread=false;EmuConfig.Speedhacks.vuFlagHack=false;
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache=false;
    if(!SysMemory::Allocate()){Check(false,"allocate outside STATUS read test memory");return;}
    CpuVU0=&CpuMicroVU0;CpuVU1=&CpuMicroVU1;CpuMicroVU0.Reserve();CpuMicroVU1.Reserve();
    const auto saved=EmuConfig;
    mvu_aot::AllowStickyElisionForTest(false);mvu_aot::CountEntries(true);mvu_aot::ForceSynchronous(true);
    const u32 sub=(15u<<21)|(2u<<16)|(1u<<11)|(3u<<6)|0x2cu;
    const u32 add=(15u<<21)|(2u<<16)|(1u<<11)|(4u<<6)|0x28u;
    const std::vector<u32> history{(0x20u<<25)|3u,sub,NOP_L,NOP_U,NOP_L,NOP_U,NOP_L,NOP_U,
        NOP_L,NOP_U,NOP_L,NOP_U,NOP_L,NOP_U,NOP_L,NOP_U,
        NOP_L,add|0x40000000u,NOP_L,NOP_U};
    for(bool flagHack:{false,true}) for(bool mtvu:{false,true}) for(u32 address:{0x42fu,0x430u,0x431u,0x470u})
    {
        EmuConfig.Speedhacks.vuThread=false;EmuConfig.Speedhacks.vuFlagHack=flagHack;
        EmuConfig.Cpu.Recompiler.EnableVUAot=false;
        std::array<u32,12> referenceRings{},tieredRings{};
        const auto baseline=RunPersistedVU(1,history,0x80,128,true,referenceRings);
        Check(flagHack||baseline.regs.VI[REG_STATUS_FLAG].UL==0x80,
            "strict outside reader fixture independently retains negative intermediate sticky STATUS history");
        EmuConfig.Cpu.Recompiler.EnableVUAot=true;
        vuaot::live::SetTitle({},0);vuaot::live::ResetStickyTripwire();
        const u64 entries=mvu_aot::EntryCount();
        const auto tiered=RunPersistedVU(1,history,0x80,128,true,tieredRings);
        Check(mvu_aot::EntryCount()>entries,"outside reader producer executes generated AOT code before its first reader");
        Compare(baseline,tiered,"exact default AOT retains STATUS history before an outside reader is known");
        Check(referenceRings==tieredRings&&baseline.regs.cycle==tiered.regs.cycle,
            "first-read producer retains exact cycles and all flag rings");
        std::array<u32,16> initialVI{};initialVI[1]=0xdead0000u|address;
        std::vector<u32> reader{0x800003bcu|(2u<<11)|(3u<<16),NOP_U,0x800003bfu,NOP_U};
        for(u32 vf=2;vf<26;++vf)
            reader.insert(reader.end(),{0x8000033cu|(1u<<11)|(vf<<16)|(15u<<21),NOP_U});
        reader.insert(reader.end(),{(1u<<11)|(26u<<16)|(15u<<21),NOP_U}); // LQ vf26,0(vi1)
        for(u32 vf=2;vf<26;++vf)
            reader.insert(reader.end(),{NOP_L,(15u<<21)|(vf<<11)|(vf<<6)|0x28u});
        reader.insert(reader.end(),{NOP_L,(15u<<21)|(2u<<11)|(27u<<6)|0x1cu, // MULq consumes preserved PQ
            NOP_L,NOP_U|0x40000000u,NOP_L,NOP_U});
        const u32 selected=address&0x3f;
        const u32 expectedWord=selected<32?baseline.regs.VF[selected].UL[0]:baseline.regs.VI[selected-32].UL;
        EmuConfig.Speedhacks.vuThread=mtvu;
        EmuCoreXOracleSetVUIR(0,0);
        EmuConfig.Cpu.Recompiler.EnableVUAot=false;
        const auto reference=RunVU(0,true,reader,0x80,0,256,false,nullptr,nullptr,nullptr,initialVI.data());
        EmuConfig.Cpu.Recompiler.EnableVUAot=true;
        vuaot::live::SetTitle({},0);vuaot::live::ResetStickyTripwire();
        const auto before=mvu_aot::GetStats();const u64 calls=EmuCoreXOracleVUWaitMTVUCalls();
        const auto actual=RunVU(0,true,reader,0x80,0,256,false,nullptr,nullptr,nullptr,initialVI.data());
        const bool status=selected==48;
        Check(actual.regs.VF[26].UL[0]==expectedWord&&(!status||flagHack||expectedWord==0x80),
            "first actual VU0 window load sees baseline numerical STATUS including prior sticky history");
        Compare(reference,actual,"VU0 outside notification preserves dirty cached VF and memory state");
        Check(reference.regs.cycle==actual.regs.cycle&&reference.regs.q.UL==actual.regs.q.UL&&
            reference.regs.pending_q==actual.regs.pending_q,
            "outside STATUS notification preserves exact cycles and live Q pipeline");
        Check(vuaot::live::StickyTripped()==status&&
            (!status||vuaot::live::StickyTripCause()==vuaot::live::TripCause::OutsideRead)&&
            mvu_aot::GetStats().stickyTrips-before.stickyTrips==(status?1u:0u),
            "only STATUS-window accesses report an outside sticky reader");
        Check(EmuCoreXOracleVUWaitMTVUCalls()-calls==((mtvu||status)?1u:0u),
            "non-MTVU neighboring window loads avoid the preservation thunk");
        std::printf("VU_OUTSIDE_STATUS flaghack=%u mtvu=%u address=%x expected=%08x actual=%08x trips=%llu calls=%llu\n",
            flagHack,mtvu,address,expectedWord,actual.regs.VF[26].UL[0],
            (unsigned long long)(mvu_aot::GetStats().stickyTrips-before.stickyTrips),
            (unsigned long long)(EmuCoreXOracleVUWaitMTVUCalls()-calls));
    }
    EmuConfig.Speedhacks.vuThread=false;EmuConfig.Cpu.Recompiler.EnableVUAot=false;
    CpuMicroVU1.Reset();mvu_aot::ForceSynchronous(false);mvu_aot::CountEntries(false);
    EmuCoreXOracleSetVUIR(0,-1);EmuConfig=saved;
}
