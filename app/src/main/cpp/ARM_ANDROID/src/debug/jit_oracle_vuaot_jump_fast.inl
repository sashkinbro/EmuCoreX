// SPDX-License-Identifier: GPL-3.0+
void RunVuAotJumpFastTests()
{
    std::vector<u32> code;
    for (u32 pc = 0x80; pc < 0x118; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    code[0] = (8u << 25) | (1u << 16) | 0x20u;
    code[2] = (0x20u << 25) | 2u; // B from88 toA0
    code[(0xa0 - 0x80) / 4] = (0x24u << 25) | (1u << 11);
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    code[(0x100 - 0x80) / 4 + 1] = add;
    code[(0x108 - 0x80) / 4 + 1] = NOP_U | 0x40000000u;
    auto replacement = code;
    replacement[(0x100 - 0x80) / 4 + 1] = (add & ~0x3fu) | 0x2cu;
    std::array<u32, 12> baselineRings{}, changedRings{}, rings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    const auto expected = RunPersistedVU(1, code, 0x80, 128, true, baselineRings);
    const auto changed = RunPersistedVU(1, replacement, 0x80, 128, true, changedRings);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::ForceSynchronous(true);
    mvu_aot::CountEntries(true);
    const auto entered = mvu_aot::EntryCount();
    const auto coldCalls = mvu_aot::GetStats().jumpResolverCalls;
    RunPersistedVU(1, code, 0x80, 128, true, rings);
    Check(mvu_aot::EntryCount() > entered && mvu_aot::GetStats().jumpResolverCalls > coldCalls,
        "native JR fixture enters its region and resolves its cold target");
    const auto warmCalls = mvu_aot::GetStats().jumpResolverCalls;
    for (u32 run = 0; run < 4; ++run)
    {
        const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings);
        Compare(expected, actual, "native JR cache hit preserves architecture");
        Check(expected.regs.cycle == actual.regs.cycle && baselineRings == rings &&
            expected.regs.pending_p == actual.regs.pending_p && expected.regs.pending_q == actual.regs.pending_q,
            "native JR cache hit preserves exact cycles, PQ and flag rings");
    }
    Check(mvu_aot::GetStats().jumpResolverCalls == warmCalls,
        "warm region JR hits never call the C++ resolver");
    CpuMicroVU1.Clear(0x100, 0x18);
    const auto actual = RunPersistedVU(1, replacement, 0x80, 128, false, rings);
    Check(mvu_aot::GetStats().jumpResolverCalls > warmCalls,
        "micro upload invalidates the native JR program-identity guard");
    Compare(changed, actual, "native JR guard resolves replacement microcode");
    Check(changed.regs.cycle == actual.regs.cycle && changedRings == rings,
        "native JR replacement preserves exact cycles and flag rings");
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    CpuMicroVU1.Reset();
    mvu_aot::ForceSynchronous(false);
    mvu_aot::CountEntries(false);
}

// Independent source blocks can retain different cached pointers to the same
// target program. Publishing the target must update both, even after the first
// caller has made that program's quick slot valid again.
void RunVuAotWarmBeforePublishTests(bool persist = false)
{
    std::vector<u32> code;
    for (u32 pc=0x80; pc<0x218; pc+=8) code.insert(code.end(), {NOP_L,NOP_U});
    const auto put=[&](u32 pc,u32 lower,u32 upper=NOP_U) {
        code[(pc-0x80)/4]=lower; code[(pc-0x80)/4+1]=upper;
    };
    for (u32 source : {0x80u,0xc0u})
    {
        put(source,(8u<<25)|(1u<<16)|0x40u);
        put(source+0x18,(0x24u<<25)|(1u<<11));
    }
    put(0x200,NOP_L,(15u<<21)|(2u<<16)|(1u<<11)|(3u<<6)|0x28u);
    put(0x208,NOP_L,NOP_U|0x40000000u);
    std::array<std::array<u32,12>,2> expectedRings{};
    std::array<Snapshot,2> expected;
    EmuConfig.Cpu.Recompiler.EnableVUAot=false;
    for(u32 i=0;i<2;++i)
        expected[i]=RunPersistedVU(1,code,0x80,128,true,expectedRings[i],0x80+i*0x40);
    std::array<u32,12> expectedBudgetRings{};
    const auto expectedBudget=RunPersistedVU(1,code,0x80,1,true,expectedBudgetRings,0x80);
    EmuConfig.Cpu.Recompiler.EnableVUAot=true;
    mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(true);
    mvu_aot::SetTierUpRuns(100000); mvu_aot::SetJumpTierUpRuns(1);
    mvu_aot::TestRelinkJumpCaches(std::getenv("EMUCOREX_AOT_SKIP_RELINK")==nullptr);
    mvu_aot::HoldWorker(true);
    std::array<u32,12> rings{};
    const auto coldEntries=mvu_aot::EntryCount();
    for(u32 i=0;i<2;++i)
        RunPersistedVU(1,code,0x80,128,i==0,rings,0x80+i*0x40);
    Check(mvu_aot::EntryCount()==coldEntries,"both source JR caches warm before any target AOT publication");
    Check(mvu_aot::CachedTargetsMatchForTest(0x80,0xc0,0x200),
        "both warmed cells name the exact same target program and baseline block");
    Check(mvu_aot::PendingCount()==1,"both callers share one pending target job with the JR pipeline key");
    mvu_aot::HoldWorker(false); mvu_aot::WaitWorker();
    const auto published=mvu_aot::RegionCount();
    for(u32 pass=0;pass<2;++pass)
        for(u32 i=0;i<2;++i)
        {
            const auto before=mvu_aot::NativeEndCount();
            const auto slow=mvu_aot::GetStats().jumpEntries;
            const auto actual=RunPersistedVU(1,code,0x80,128,false,rings,0x80+i*0x40);
            Check(mvu_aot::NativeEndCount()>before,
                "each prewarmed JR caller reaches the newly published AOT target");
            Check(mvu_aot::GetStats().jumpEntries==slow,
                "relinked prewarmed JR remains a native cache hit");
            Compare(expected[i],actual,"prewarmed JR publication preserves architectural state");
            Check(expected[i].regs.cycle==actual.regs.cycle && expectedRings[i]==rings,
                "prewarmed JR publication preserves exact cycles and flag rings");
        }
    Check(mvu_aot::RegionCount()>published && mvu_aot::PendingCount()==0,
        "the exact warmed target job publishes on its next owner dispatch");
    const auto restored=mvu_aot::GetStats().jumpCellsRelinked;
    const auto written=mVUProgCache::GetStats(1).payloadWrites;
    const auto sidecarsWritten=mVUProgCache::GetStats(1).sidecarWrites;
    CpuMicroVU1.Reset();
    Check(mvu_aot::GetStats().jumpCellsRelinked>=restored+2,
        "reset restores both inbound AOT pointers to original baseline entries");
    if (persist)
    {
        Check(mVUProgCache::GetStats(1).payloadWrites>written,
            "reset serializes baseline graphs after restoring warmed AOT jump cells");
        mVUProgCache::FlushSidecars(1);
        Check(mVUProgCache::GetStats(1).sidecarWrites>sidecarsWritten,
            "warmed framed body exports a valid native sidecar");
        mvu_aot::HoldWorker(true);
        const auto compiles=mVUPersist::GetBlockCompileCount(1);
        const auto savedBefore=mvu_aot::GetStats().savedRecords;
        for(u32 i=0;i<2;++i)
        {
            const auto ends=mvu_aot::NativeEndCount();
            const auto actual=RunPersistedVU(1,code,0x80,128,false,rings,0x80+i*0x40);
            Check(mvu_aot::NativeEndCount()>ends && mvu_aot::PendingCount()==0,
                "saved warmed JR target executes AOT with its worker held");
            if(i==0)
            {
                vuaot::persist::File file;
                const bool loaded=mvu_aot::ReadSavedForTest(file);
                bool counterPointer=false;
                for(const auto& record:file.records)
                    if(record.HasCode())
                        for(const auto& fixup:record.code.fixups)
                            counterPointer|=fixup.target==vuaot::persist::kTargetProgram &&
                                fixup.form==vuaot::persist::kFormMovRun && fixup.width==4;
                Check(loaded&&counterPointer,"saved framed body retains its relocatable counter pointer");
                for(u32 kind=0;kind<5;++kind)
                {
                    auto malformed=file;
                    bool mutated=false;
                    for(auto& record:malformed.records)
                        if(record.HasCode())
                            for(auto& fixup:record.code.fixups)
                                if(!mutated && fixup.target==vuaot::persist::kTargetProgram &&
                                    fixup.form==vuaot::persist::kFormMovRun)
                                {
                                    switch(kind)
                                    {
                                        case 0:fixup.width=3;break;
                                        case 1:fixup.value=16;break;
                                        case 2:fixup.value=-1;break;
                                        case 3:fixup.index=1;break;
                                        case 4:fixup.form=vuaot::persist::kFormRel26;break;
                                    }
                                    mutated=true;
                                }
                    Check(mutated&&vuaot::persist::Serialize(malformed).empty(),
                        "saved body rejects malformed counter relocation contracts");
                }
            }
            Compare(expected[i],actual,"saved warmed JR graph preserves architecture");
            Check(expected[i].regs.cycle==actual.regs.cycle && expectedRings[i]==rings,
                "saved warmed JR graph preserves exact cycles and rings");
        }
        Check(mVUPersist::GetBlockCompileCount(1)==compiles,
            "saved two-caller JR graph reloads without baseline recompilation");
        Check(mvu_aot::GetStats().savedRecords>savedBefore,
            "warmed body executes imported native code rather than rebuilding its plan");
        const auto beforeBail=mvu_aot::BailCount();
        const auto budgetActual=RunPersistedVU(1,code,0x80,1,false,rings,0x80);
        Check(mvu_aot::BailCount()>beforeBail && mvu_aot::PendingCount()==0,
            "saved body budget bail updates the imported job's relocated counters with worker held");
        Compare(expectedBudget,budgetActual,"saved body budget bail preserves architecture");
        Check(expectedBudget.regs.cycle==budgetActual.regs.cycle && expectedBudgetRings==rings,
            "saved body budget bail preserves exact cycles and flag rings");
        mvu_aot::HoldWorker(false);
    }
    EmuConfig.Cpu.Recompiler.EnableVUAot=false;
    CpuMicroVU1.Reset(); mvu_aot::CountEntries(false); mvu_aot::TestRelinkJumpCaches(true);
    mvu_aot::SetTierUpRuns(200); mvu_aot::SetJumpTierUpRuns(32);
}
