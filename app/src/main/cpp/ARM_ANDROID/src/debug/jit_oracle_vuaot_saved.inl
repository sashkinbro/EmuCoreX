// SPDX-License-Identifier: GPL-3.0+
#include "jit_oracle_vuaot_canonical.inl"
void RunVuAotSavedTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate saved AOT test memory"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache;
    EmuFolders::Cache = "/data/local/tmp/emucorex-vuaot-saved-" + std::to_string(getpid());
    mVUProgCache::ResetForTest(1);
    mVUProgCache::ClearVuAot(true);
    mVUProgCache::SetTitle("ORACLE-AOT-SAVED", 0x1234);
    mVUPersist::SetTestManualRecording(false);
    const u32 add=(15u<<21)|(2u<<16)|(1u<<11)|(3u<<6)|0x28u;
    const std::vector<u32> code{
        (0x20u<<25)|3u,add,NOP_L,NOP_U,NOP_L,NOP_U,NOP_L,NOP_U,
        NOP_L,NOP_U|0x40000000u,NOP_L,NOP_U};
    std::array<u32,12> beforeRings{},afterRings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot=false;
    const auto baseline=RunPersistedVU(1,code,0x80,128,true,beforeRings);
    EmuConfig.Cpu.Recompiler.EnableVUAot=true;
    mvu_aot::ForceSynchronous(true); mvu_aot::CountEntries(true);
    const u64 firstEntered=mvu_aot::EntryCount();
    auto tiered=RunPersistedVU(1,code,0x80,128,true,afterRings);
    Check(mvu_aot::EntryCount()>firstEntered,"saved fixture actually enters its initial region");
    Compare(baseline,tiered,"initial saved fixture region preserves architecture");
    mVUProgCache::TestWaitForPreload(1);
    const auto beforeSave=mVUProgCache::GetStats(1);
    CpuMicroVU1.Reset();
    Check(mVUProgCache::GetStats(1).payloadWrites>beforeSave.payloadWrites,
        "AOT reset restores baseline entries before saving the native graph");
    mVUProgCache::FlushSidecars(1);
    Check(mVUProgCache::GetStats(1).sidecarWrites>beforeSave.sidecarWrites,
        "AOT reset exports native region code and its read-set sidecar");
    mvu_aot::ForceSynchronous(false); mvu_aot::HoldWorker(true);
    const auto compiles=mVUPersist::GetBlockCompileCount(1);
    const auto beforeLoad=mVUProgCache::GetStats(1);
    const u64 regions=mvu_aot::RegionCount(), entries=mvu_aot::EntryCount();
    auto loaded=RunPersistedVU(1,code,0x80,128,false,afterRings);
    Check(mVUProgCache::GetStats(1).payloadHits>beforeLoad.payloadHits &&
        mVUPersist::GetBlockCompileCount(1)==compiles,
        "AOT reset reloads the baseline graph without recompiling");
    Check(mvu_aot::RegionCount()>regions && mvu_aot::EntryCount()>entries && mvu_aot::PendingCount()==0,
        "saved native region installs and executes while compiler worker is held");
    Compare(baseline,loaded,"saved native AOT code preserves architectural state");
    Check(baseline.regs.cycle==loaded.regs.cycle && beforeRings==afterRings &&
        baseline.regs.pending_p==loaded.regs.pending_p && baseline.regs.pending_q==loaded.regs.pending_q,
        "saved native AOT code preserves exact cycles PQ and flag rings");
    mvu_aot::HoldWorker(false);

    // A compiler decline is cacheable even when this program has no AOT jobs.
    // IbitHack rejects both tiers globally; this fixture contains no I-bit and
    // therefore keeps the independently captured baseline arithmetic unchanged.
    EmuConfig.Gamefixes.IbitHack = true;
    mvu_aot::ForceSynchronous(true);
    const auto declineStart = mvu_aot::GetStats();
    auto declined = RunPersistedVU(1,code,0x80,128,true,afterRings);
    Check(mvu_aot::GetStats().unsupportedDeclines>declineStart.unsupportedDeclines,
        "decline cache fixture reaches the unsupported compiler path");
    Compare(baseline,declined,"declined AOT root retains exact baseline execution");
    const auto declinedSave = mVUProgCache::GetStats(1);
    CpuMicroVU1.Reset(); mVUProgCache::FlushSidecars(1);
    Check(mVUProgCache::GetStats(1).sidecarWrites>declinedSave.sidecarWrites,
        "a program containing only declined roots persists its keyed declines");
    const auto declinedLoad = mVUProgCache::GetStats(1);
    const auto loadedDeclines = mvu_aot::GetStats().unsupportedDeclines;
    const auto declinedCompiles = mVUPersist::GetBlockCompileCount(1);
    declined = RunPersistedVU(1,code,0x80,128,false,afterRings);
    Check(mVUProgCache::GetStats(1).sidecarReads>declinedLoad.sidecarReads &&
        mvu_aot::GetStats().unsupportedDeclines==loadedDeclines,
        "saved declined entry skips recompilation at its exact pipeline key");
    Check(mVUPersist::GetBlockCompileCount(1)==declinedCompiles &&
        baseline.regs.cycle==declined.regs.cycle && beforeRings==afterRings,
        "declined root reload preserves baseline graph cycles and rings");
    Compare(baseline,declined,"saved declined root retains exact baseline execution");
    vuaot::persist::File declineFile;
    Check(mvu_aot::ReadSavedForTest(declineFile) && !declineFile.declinedRoots.empty(),
        "decline sidecar retains explicit pipeline keys");
    // Inject a valid sidecar for a different pipeline state at the same PC.
    // The actual dispatch must still try its own root; a PC-only negative cache
    // would incorrectly suppress it. No generated code is injected here.
    if (declineFile.declinedRoots.empty()) declineFile.declinedRoots.push_back({0x80,{}});
    for (auto& root : declineFile.declinedRoots) root.key[95] ^= 0x5a;
    CpuMicroVU1.Reset();
    Check(mvu_aot::QueueSavedForTest(declineFile),"queue alternate-key decline sidecar");
    const auto alternateDeclines=mvu_aot::GetStats().unsupportedDeclines;
    declined=RunPersistedVU(1,code,0x80,128,false,afterRings);
    Check(mvu_aot::GetStats().unsupportedDeclines>alternateDeclines,
        "a declined PC with another pipeline key does not suppress compilation");
    Compare(baseline,declined,"alternate-key decline preserves baseline state");
    EmuConfig.Gamefixes.IbitHack = false;
    RunVuAotCanonicalEntryTests();
    RunVuAotWarmBeforePublishTests(true);
    CpuMicroVU1.Reset(); mVUProgCache::TestClose(1);
    mVUProgCache::ClearVuAot(true);
    EmuConfig.Cpu.Recompiler.EnableVUAot=false;
    mvu_aot::CountEntries(false);
    EmuFolders::Cache=savedCache;
}
