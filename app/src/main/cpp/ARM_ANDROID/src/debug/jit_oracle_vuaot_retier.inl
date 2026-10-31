// SPDX-License-Identifier: GPL-3.0+
void RunVuAotRetierTests()
{
    const auto make=[](u32 choice) {
        std::vector<u32> code;
        for(u32 pc=0x80;pc<0x310;pc+=8) code.insert(code.end(),{NOP_L,NOP_U});
        const auto put=[&](u32 pc,u32 lower,u32 upper=NOP_U) {code[(pc-0x80)/4]=lower;code[(pc-0x80)/4+1]=upper;};
        const auto branch=[](u32 pc,u32 target,u32 op,u32 is=0,u32 it=0) {
            return (op<<25)|(it<<16)|(is<<11)|(((target-(pc+8))/8)&0x7ffu);
        };
        put(0x80,(8u<<25)|(1u<<16)|0x20u); // jump target100
        put(0x88,(8u<<25)|(2u<<16)|4u); // loop count
        put(0x90,(8u<<25)|(3u<<16)|choice);
        put(0x98,(0x24u<<25)|(1u<<11));
        put(0x100,(9u<<25)|(2u<<16)|(2u<<11)|1u); // ISUBIU vi2,vi2,1
        put(0x108,branch(0x108,0x300,0x28,2));
        put(0x118,branch(0x118,0x200,0x28,3));
        put(0x128,(8u<<25)|(4u<<16)|1u);
        put(0x130,branch(0x130,0x240,0x28,3,4));
        put(0x140,(8u<<25)|(4u<<16)|2u);
        put(0x148,branch(0x148,0x280,0x28,3,4));
        put(0x158,branch(0x158,0x2c0,0x20));
        for(u32 n=0;n<4;++n)
            put(0x200+n*0x40,(0x24u<<25)|(1u<<11),
                (15u<<21)|(2u<<16)|(1u<<11)|((5u+n)<<6)|0x28u);
        put(0x300,NOP_L,NOP_U|0x40000000u);
        return code;
    };
    std::array<std::vector<u32>,4> programs;
    std::array<Snapshot,4> expected;
    std::array<std::array<u32,12>,4> expectedRings{};
    for(u32 i=0;i<4;++i) {
        programs[i]=make(i);
        EmuConfig.Cpu.Recompiler.EnableVUAot=false;
        expected[i]=RunPersistedVU(1,programs[i],0x80,128,true,expectedRings[i]);
    }
    EmuConfig.Cpu.Recompiler.EnableVUAot=true;
    mvu_aot::CountEntries(true);
    mvu_aot::ForceSynchronous(false);
    mvu_aot::SetTierUpRuns(100000); mvu_aot::SetJumpTierUpRuns(100000);
    std::array<u32,12> rings{};
    for(u32 i=0;i<4;++i) {
        if(i) CpuMicroVU1.Clear(0x100,0x210);
        RunPersistedVU(1,programs[0],0x80,128,i==0,rings);
    }
    const auto before=mvu_aot::GetStats();
    const auto entered=mvu_aot::EntryCount();
    mvu_aot::SetJumpTierUpRuns(1); mvu_aot::ForceSynchronous(true);
    CpuMicroVU1.Clear(0x100,0x210);
    auto initial=RunPersistedVU(1,programs[0],0x80,128,false,rings);
    Check(mvu_aot::EntryCount()>entered && mvu_aot::GetStats().targetsRecorded>before.targetsRecorded,
        "retier fixture enters a region with a learned same-program JR target");
    Compare(expected[0],initial,"initial profiled loop preserves architecture");
    Check(expected[0].regs.cycle==initial.regs.cycle && expectedRings[0]==rings,
        "initial profiled loop preserves exact cycles and rings");
    const auto afterInitial=mvu_aot::GetStats();
    for(u32 phase=1;phase<4;++phase)
    {
        CpuMicroVU1.Clear(0x80,0x28); // change only the stub's runtime selector
        for(u32 run=0;run<2200;++run)
        {
            CpuMicroVU1.Clear(0x100,0x210); // preserve code, force overlay slow entry
            auto actual=RunPersistedVU(1,programs[phase],0x80,128,false,rings);
            if(run==0 || run==2199) {
                Compare(expected[phase],actual,"retier loop branch preserves architecture");
                Check(expected[phase].regs.cycle==actual.regs.cycle && expectedRings[phase]==rings,
                    "retier loop branch preserves exact cycles and rings");
            }
        }
        const auto stats=mvu_aot::GetStats();
        Check(stats.retiers-afterInitial.retiers==std::min(phase,2u),
            "new same-program targets cause at most two bounded retiers");
    }
    Check(mvu_aot::GetStats().retierChecks>afterInitial.retierChecks,
        "live retier fixture exercises the periodic miss-gated harvest");
    EmuConfig.Cpu.Recompiler.EnableVUAot=false;
    CpuMicroVU1.Reset(); mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false);
    mvu_aot::SetTierUpRuns(200);mvu_aot::SetJumpTierUpRuns(32);
}
