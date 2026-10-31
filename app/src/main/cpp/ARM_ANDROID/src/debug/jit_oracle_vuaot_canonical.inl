// SPDX-License-Identifier: GPL-3.0+
void RunVuAotCanonicalEntryTests()
{
    const auto branch=[](u32 pc,u32 target,u32 op,u32 is=0,u32 it=0) {
        return (op<<25)|(it<<16)|(is<<11)|(((target-(pc+8))/8)&0x7ffu);
    };
    const u32 add=(15u<<21)|(2u<<16)|(1u<<11)|(3u<<6)|0x28u;
    for(u32 internal=0; internal<2; ++internal)
    {
        std::vector<u32> code;
        for(u32 pc=0x80;pc<0xf0;pc+=8) code.insert(code.end(),{NOP_L,NOP_U});
        const auto put=[&](u32 pc,u32 lower,u32 upper=NOP_U) {
            code[(pc-0x80)/4]=lower; code[(pc-0x80)/4+1]=upper;
        };
        vuaot::PipeState raw;
        if(internal)
        {
            put(0x80,branch(0x80,0xa0,0x20));
            put(0x88,(8u<<25)|(1u<<16)|(3u<<11)|1u); // delay-slot write leaves a dead backup label
            put(0xa0,branch(0xa0,0xd0,0x28,1,4));
        }
        else
        {
            raw.viBackUp=1; raw.flagInfo=0x30; // stale fields baseline lookup normalizes
            put(0x80,branch(0x80,0xd0,0x28,1,0));
            put(0x90,branch(0x90,0xb0,0x20));
        }
        put(0xb0,NOP_L,add);
        put(0xb8,branch(0xb8,0xe0,0x20));
        put(0xd0,NOP_L,(add&~0x3fu)|0x2cu);
        put(0xe0,NOP_L,NOP_U|0x40000000u);
        mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(true);
        mvu_aot::SetTierUpRuns(100000); mvu_aot::SetJumpTierUpRuns(100000);
        std::array<u32,12> expectedRings{},rings{};
        const auto expected=RunPersistedVU(1,code,0x80,128,true,expectedRings,0x80,&raw,0);
        CpuMicroVU1.Reset();
        const auto payloadHits=mVUProgCache::GetStats(1).payloadHits;
        const auto entered=mvu_aot::EntryCount();
        mvu_aot::ForceSynchronous(true);
        const auto actual=RunPersistedVU(1,code,0x80,128,false,rings,0x80,&raw,0);
        Check(mVUProgCache::GetStats(1).payloadHits>payloadHits,
            "canonical fixture loads its baseline graph from the native cache");
        Check(mvu_aot::EntryCount()>entered,"canonical fixture actually enters the new AOT region");
        Compare(expected,actual,internal ? "internal region edge uses canonical VI backup state" :
            "hydrated raw entry uses baseline canonical backup and flag state");
        Check(expected.regs.cycle==actual.regs.cycle && expectedRings==rings,
            "canonical entry preserves exact cycles and flag rings");
    }
    mvu_aot::SetTierUpRuns(200); mvu_aot::SetJumpTierUpRuns(32);
}
