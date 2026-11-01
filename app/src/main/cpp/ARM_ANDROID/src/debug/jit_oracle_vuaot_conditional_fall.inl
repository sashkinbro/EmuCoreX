// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// A new baseline conditional caller finds its FALL block already compiled and
// published. The old near B.cond19 bypasses the static B26 inbound registry.
void RunVuAotConditionalFallTests()
{
    cpuinfo_initialize();EmuConfig=Pcsx2Config();
    EmuConfig.Speedhacks.vuThread=false;EmuConfig.Speedhacks.vuFlagHack=false;
    if(!SysMemory::Allocate()) { Check(false,"allocate preexisting conditional-fall VU fixture");return; }
    CpuVU0=&CpuMicroVU0;CpuVU1=&CpuMicroVU1;CpuMicroVU0.Reserve();CpuMicroVU1.Reserve();
    const auto savedCache=EmuFolders::Cache;EmuFolders::Cache.clear();
    const ScopedGuard cleanup([&] {
        mvu_aot::HoldWorker(false);mvu_aot::SetTierUpRuns(200);mvu_aot::ForceSynchronous(false);
        mvu_aot::CountEntries(false);EmuConfig.Cpu.Recompiler.EnableVUAot=false;CpuMicroVU1.Reset();EmuFolders::Cache=savedCache;
    });
    const auto variants=[](u32 pc) {
        std::vector<JitBlockProfile> blocks;VU1_JitGetBlockProfiles(blocks);
        return std::count_if(blocks.begin(),blocks.end(),[pc](const auto& block) { return block.startpc==pc; });
    };
    for(bool taken:{false,true})
    {
        std::vector<u32> code;
        for(u32 pc=0x80;pc<0x1f0;pc+=8)code.insert(code.end(),{NOP_L,NOP_U});
        const auto put=[&](u32 pc,u32 lower,u32 upper=NOP_U) {
            code[(pc-0x80)/4]=lower;code[(pc-0x80)/4+1]=upper;
        };
        const auto branch=[](u32 pc,u32 target) { return (0x20u<<25)|(((target-(pc+8))/8)&0x7ffu); };
        put(0x80,(8u<<25)|(7u<<16)|0x20u);put(0x88,branch(0x88,0xa0));
        put(0xa0,NOP_L,NOP_U|0x08000000u);put(0xa8,branch(0xa8,0x180));
        put(0x160,(8u<<25)|(7u<<16)|0x20u);put(0x168,NOP_L,NOP_U|0x08000000u);
        put(0x170,(0x29u<<25)|((taken?2u:1u)<<16)|(1u<<11)|(((0x1c0-(0x170+8))/8)&0x7ffu));
        // 178 is a real NOP delay: an external precompiled child key must not
        // be confused with an active branch-delay VI-backup key.
        put(0x180,(8u<<25)|(10u<<16)|0x222u,(15u<<21)|(2u<<16)|(1u<<11)|(3u<<6)|0x28u);
        put(0x188,branch(0x188,0x1a0));put(0x1a0,NOP_L,NOP_U|0x40000000u);
        put(0x1c0,(8u<<25)|(10u<<16)|0x444u,(15u<<21)|(2u<<16)|(1u<<11)|(4u<<6)|0x2cu);
        put(0x1c8,branch(0x1c8,0x1e0));put(0x1e0,NOP_L,NOP_U|0x40000000u);
        const auto literal=[&](const Snapshot& state) {
            if(state.regs.VI[7].UL!=0x20 || state.regs.VI[10].UL!=(taken?0x444u:0x222u))return false;
            for(u32 lane=0;lane<4;++lane)
                if(taken?state.regs.VF[4].UL[lane]!=0xbb000000u:
                    state.regs.VF[3].UL[lane]!=(0x40006000u+lane*0x1000u))return false;
            return true;
        };
        EmuConfig.Cpu.Recompiler.EnableVUAot=false;
        std::array<u32,12> expectedRings{},rootRings{},rings{};
        const auto expected=RunPersistedVU(1,code,0x80,128,true,expectedRings,0x160,nullptr,0,0x80);
        const auto expectedRoot=RunPersistedVU(1,code,0x80,128,true,rootRings);
        Check(literal(expected),"independent literal branch marker and raw ADD or SUB lane goldens match the baseline");
        constexpr u32 budgets[]={1,3,5,7,128};
        std::vector<Snapshot> expectedBudget;
        std::vector<std::array<u32,12>> expectedBudgetRings;
        for(u32 budget:budgets)
        {
            std::array<u32,12> r{};
            expectedBudget.push_back(RunPersistedVU(1,code,0x80,budget,true,r,0x160,nullptr,0,0x80));
            expectedBudgetRings.push_back(r);
        }
        EmuConfig.Cpu.Recompiler.EnableVUAot=true;
        mvu_aot::CountEntries(true);mvu_aot::ForceSynchronous(false);mvu_aot::SetTierUpRuns(100000);
        rings={};RunPersistedVU(1,code,0x80,128,true,rings,0x180,nullptr,0,0x80);
        const auto owner=mvu_aot::CurrentOwnerForTest();
        rings={};RunPersistedVU(1,code,0x80,128,false,rings,0x1c0,nullptr,0,0x80);
        Check(owner && mvu_aot::CurrentOwnerForTest()==owner && variants(0x180)==1 && variants(0x1c0)==1,
            "both unchanged child keys are already compiled inside the same owner before the conditional caller exists");
        mvu_aot::ForceSynchronous(true);mvu_aot::SetTierUpRuns(1);
        const auto positive=mvu_aot::NativeRegionEntryCount(0x180);
        Snapshot warm{};
        for(u32 run=0;run<8;++run) { rings={};warm=RunPersistedVU(1,code,0x80,128,false,rings); }
        Check(mvu_aot::CurrentOwnerForTest()==owner && mvu_aot::NativeRegionEntryCount(0x180)>positive,
            "a real preexisting C region executes from the ordinary B26 caller before new conditional compilation");
        Compare(expectedRoot,warm,"ordinary direct native child remains architecturally exact");
        const auto compiled=mVUPersist::GetBlockCompileCount(1);
        rings={};const auto cold=RunPersistedVU(1,code,0x80,128,false,rings,0x160,nullptr,0,0x80);
        Compare(expected,cold,"new conditional caller preserves complete architecture at its first execution");
        Check(mvu_aot::CurrentOwnerForTest()==owner && variants(0x180)==1 && variants(0x1c0)==1 &&
            mVUPersist::GetBlockCompileCount(1)==compiled+1,
            "new conditional caller compiles exactly itself without fabricating child pipeline variants");
        mvu_aot::ForceSynchronous(false);mvu_aot::SetTierUpRuns(100000);mvu_aot::HoldWorker(true);
        const u32 target=taken?0x1c0:0x180;
        const auto entered=mvu_aot::NativeRegionEntryCount(target);
        const auto before=mvu_aot::GetStats();
        const auto steadyCompiles=mVUPersist::GetBlockCompileCount(1);
        rings={};const auto actual=RunPersistedVU(1,code,0x80,128,false,rings,0x160,nullptr,0,0x80);
        Check(mvu_aot::NativeRegionEntryCount(target)>entered && mVUPersist::GetBlockCompileCount(1)==steadyCompiles &&
            mvu_aot::GetStats().dispatches==before.dispatches+1 && mvu_aot::GetStats().jumpEntries==before.jumpEntries,
            "cached conditional fall or opposite taken edge reaches its actual child region with zero compile or extra entry offer");
        Compare(expected,actual,"cached conditional native child preserves complete architecture");
        Check(literal(actual) && expected.regs.cycle==actual.regs.cycle && expectedRings==rings,
            "cached conditional child preserves literal control arithmetic cycles and every flag ring");
        for(u32 i=0;i<std::size(budgets);++i)
        {
            rings={};const auto split=RunPersistedVU(1,code,0x80,budgets[i],false,rings,0x160,nullptr,0,0x80);
            Compare(expectedBudget[i],split,"conditional child budget boundary preserves complete architecture");
            Check(expectedBudget[i].regs.cycle==split.regs.cycle && expectedBudgetRings[i]==rings &&
                expectedBudget[i].regs.pending_p==split.regs.pending_p && expectedBudget[i].regs.pending_q==split.regs.pending_q,
                "conditional child private short-budget exits preserve exact cycles PQ and all rings");
        }
        std::printf("AOT_CONDITIONAL_FALL taken=%d region=%llu compiles=%llu Cvariants=%zu Dvariants=%zu\n",taken,
            static_cast<unsigned long long>(mvu_aot::NativeRegionEntryCount(target)-entered),
            static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(1)-steadyCompiles),
            static_cast<size_t>(variants(0x180)),static_cast<size_t>(variants(0x1c0)));
        mvu_aot::HoldWorker(false);
    }
}
