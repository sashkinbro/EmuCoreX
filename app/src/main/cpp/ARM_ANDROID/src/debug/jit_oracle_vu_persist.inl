// SPDX-License-Identifier: GPL-3.0+
// Include inside jit_oracle.cpp's anonymous namespace after RunVU/Compare.
// Include microVU_Persist-arm64.h and microVU_ProgCache-arm64.h at TU scope.
// Standalone suite: no real VM, GS or device cache directory is required.
extern "C" void EmuCoreXOracleVULocalFlagRings(u32 index, u32* words, bool seed);

// RunVU always Reset()s the recompiler. This deliberately does not: a reset
// between TestHydrate and Execute would silently turn this into a compile test.
Snapshot RunPersistedVU(u32 index, const std::vector<u32>& program, u32 pc,
    u32 budget, bool reset, std::array<u32, 12>& rings, u32 entryPC = UINT32_MAX,
    const vuaot::PipeState* pipeline = nullptr, u32 viBackup = 0)
{
    const FPControlRegisterBackup callerFPCR(EmuConfig.Cpu.FPUFPCR);
    VURegs& vu = index ? VU1 : VU0;
    u8* const micro = vu.Micro;
    u8* const mem = vu.Mem;
    std::memset(&vu, 0, sizeof(vu));
    vu.Micro = micro;
    vu.Mem = mem;
    vu.idx = index;
    vu.VF[0].f.w = 1.0f;
    for (u32 r = 1; r < 32; ++r)
        for (u32 lane = 0; lane < 4; ++lane)
            vu.VF[r].UL[lane] = 0x3f800000 + (r * 4 + lane) * 0x1000;
    for (u32 r = 1; r < 16; ++r)
        vu.VI[r].UL = r;
    const u32 size = index ? 0x4000 : 0x1000;
    for (u32 i = 0; i < size; i += 8)
    {
        std::memcpy(micro + i, &NOP_L, 4);
        std::memcpy(micro + i + 4, &NOP_U, 4);
    }
    std::memcpy(micro + pc, program.data(), program.size() * sizeof(u32));
    for (u32 i = 0; i < size; ++i)
        mem[i] = static_cast<u8>(i * 13 + 7);
    VU0.VI[REG_VPU_STAT].UL = index ? 0x100 : 1;
    if (entryPC == UINT32_MAX) entryPC = pc;
    vu.VI[REG_TPC].UL = entryPC / 8;
    BaseVUmicroCPU* const cpu = index ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuMicroVU0;
    if (reset) cpu->Reset();
    EmuCoreXOracleVULocalFlagRings(index, rings.data(), true);
    OpcodeFamilies::SetFamilyMask(index ? OpcodeFamilies::CORE_VU1 : OpcodeFamilies::CORE_VU0, 0);
    cpu->SetStartPC(entryPC);
    if (pipeline) mvu_aot::SetPipelineForTest(pipeline->bytes, viBackup);
    const bool fallback = (vu.flags & VUFLAG_INTERPRETER) != 0;
    std::vector<u8> gif;
    gifOutput = &gif;
    gifOverflow = false;
    for (u32 call = 0; call < 128 && (VU0.VI[REG_VPU_STAT].UL & (index ? 0x100 : 1)); ++call)
        cpu->Execute(budget);
    gifOutput = nullptr;
    EmuCoreXOracleVULocalFlagRings(index, rings.data(), false);
    return {vu, std::vector<u8>(mem, mem + size), VU0.VI[REG_VPU_STAT].UL,
        fallback, std::vector<u8>(micro, micro + size), std::move(gif), gifOverflow};
}

// Exercise the real reset/save and mVUsearchProg RAM-lookup hooks, separately
// from the direct image codec tests below. This process owns only this /tmp
// directory; the caller restores the user's Cache path afterwards.
void RunVuPersistPackTests(const std::vector<u32>& program)
{
    for (u32 index = 0; index < 2; ++index) mVUProgCache::ResetForTest(index);
    const std::string temp = "/data/local/tmp/emucorex-vu-persist-pack-" + std::to_string(getpid());
    EmuFolders::Cache = temp;
    mVUProgCache::ClearVuAot(true);
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
    mVUPersist::SetTestManualRecording(false); // exercise the production config sync
    for (u32 index = 0; index < 2; ++index)
    {
        mVUProgCache::SetTitle("ORACLE-NATIVE-PACK", 0x12345678);
        mVUProgCache::ResetForTest(index);
        BaseVUmicroCPU* const cpu = index ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuMicroVU0;
        std::array<u32, 12> baselineRings{}, hitRings{};
        const auto baseline = RunPersistedVU(index, program, 0x80, 128, true, baselineRings);
        mVUProgCache::TestWaitForPreload(index);
        const auto initial = mVUProgCache::GetStats(index);
        Check(initial.enabled && initial.initialized && mVUPersist::IsRecordingEnabled() &&
            !EmuConfig.Cpu.Recompiler.EnableVUAot,
            "native program cache opens through its own production setting with AOT off");
        const auto initialPath = mVUProgCache::TestPackPath(index);
        Check(initialPath.find(temp + "/") == 0,
            "native pack test writes only inside its isolated temporary cache");
        cpu->Reset(); // production SaveAllPrograms, no explicit save test hook
        const auto saved = mVUProgCache::GetStats(index);
        Check(saved.payloadWrites > initial.payloadWrites,
            "native reset saves the baseline program graph into its RAM pack");
        u64 compiles = mVUPersist::GetBlockCompileCount(index);
        auto hit = RunPersistedVU(index, program, 0x80, 128, false, hitRings);
        Check(mVUProgCache::GetStats(index).payloadHits > saved.payloadHits &&
            mVUPersist::GetBlockCompileCount(index) == compiles,
            "mVUsearchProg hydrates the RAM pack and executes without compiling");
        Compare(baseline, hit, "RAM pack dispatch preserves baseline architectural state");
        Check(baseline.regs.cycle == hit.regs.cycle && baselineRings == hitRings,
            "RAM pack dispatch preserves exact baseline cycles and internal flag rings");

        // Persist, close, drop native ownership, then reopen via production
        // Reset/Init. The next lookup must use the disk-preloaded RAM table.
        mVUProgCache::TestClose(index);
        Check(FileSystem::FileExists(initialPath.c_str()), "native close writes a title/options pack file");
        cpu->Reset();
        mVUProgCache::TestWaitForPreload(index);
        const auto preloaded = mVUProgCache::GetStats(index);
        Check(preloaded.loadedRecords > 0, "native cache reopens and preloads saved graph records");
        compiles = mVUPersist::GetBlockCompileCount(index);
        hit = RunPersistedVU(index, program, 0x80, 128, false, hitRings);
        Check(mVUProgCache::GetStats(index).payloadHits > preloaded.payloadHits &&
            mVUPersist::GetBlockCompileCount(index) == compiles,
            "disk-preloaded RAM pack dispatch executes without compiling");
        Compare(baseline, hit, "disk pack dispatch preserves baseline architectural state");
        Check(baseline.regs.cycle == hit.regs.cycle && baselineRings == hitRings,
            "disk pack dispatch preserves exact baseline cycles and internal flag rings");

        mVUProgCache::SetTitle("ORACLE-NATIVE-OTHER", 0x87654321);
        cpu->Reset();
        mVUProgCache::TestWaitForPreload(index);
        Check(mVUProgCache::TestPackPath(index) != initialPath,
            "production reset rekeys the native pack for another title");
        auto misses = mVUProgCache::GetStats(index).payloadHits;
        compiles = mVUPersist::GetBlockCompileCount(index);
        RunPersistedVU(index, program, 0x80, 128, false, hitRings);
        Check(mVUPersist::GetBlockCompileCount(index) > compiles &&
            mVUProgCache::GetStats(index).payloadHits == misses,
            "another title cannot hydrate the previous title's native graph");
        const auto titlePath = mVUProgCache::TestPackPath(index);
        const bool oldOverflow = index ? EmuConfig.Cpu.Recompiler.vu1Overflow : EmuConfig.Cpu.Recompiler.vu0Overflow;
        if (index) EmuConfig.Cpu.Recompiler.vu1Overflow = !oldOverflow;
        else EmuConfig.Cpu.Recompiler.vu0Overflow = !oldOverflow;
        cpu->Reset();
        mVUProgCache::TestWaitForPreload(index);
        Check(mVUProgCache::TestPackPath(index) != titlePath,
            "production reset rekeys the native pack for changed compiler options");
        misses = mVUProgCache::GetStats(index).payloadHits;
        compiles = mVUPersist::GetBlockCompileCount(index);
        RunPersistedVU(index, program, 0x80, 128, false, hitRings);
        Check(mVUPersist::GetBlockCompileCount(index) > compiles &&
            mVUProgCache::GetStats(index).payloadHits == misses,
            "changed compiler options cannot hydrate the previous native graph");
        mVUProgCache::TestClose(index);
        if (index) EmuConfig.Cpu.Recompiler.vu1Overflow = oldOverflow;
        else EmuConfig.Cpu.Recompiler.vu0Overflow = oldOverflow;
    }
    mVUProgCache::ClearVuAot(true);
    for (u32 index = 0; index < 2; ++index) mVUProgCache::ResetForTest(index);
    rmdir((temp + "/vu_jit").c_str());
    rmdir(temp.c_str());
    mVUProgCache::SetTitle("", 0);
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache = false;
}

void RunVuPersistTests()
{
    cpuinfo_initialize();
    const auto savedConfig = EmuConfig;
    const auto savedCache = EmuFolders::Cache;
    const bool savedRecording = mVUPersist::IsRecordingEnabled();
    const bool savedDisable = mVUPersist::IsProcessDisabled();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    // Isolate direct image tests from automatic pack preload/rehydration.
    EmuFolders::Cache.clear();
    mVUPersist::SetProcessDisable(false);
    mVUPersist::SetTestManualRecording(true);
    mVUPersist::SetRecordingEnabled(true);
    const auto cleanup = [&] {
        for (u32 index = 0; index < 2; ++index) mVUProgCache::ResetForTest(index);
        mVUPersist::SetRecordingEnabled(savedRecording);
        mVUPersist::SetTestManualRecording(false);
        mVUPersist::SetProcessDisable(savedDisable);
        EmuFolders::Cache = savedCache;
        EmuConfig = savedConfig;
    };
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate baseline VU cache oracle memory");
        cleanup();
        return;
    }
    CpuVU0 = &CpuMicroVU0;
    CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve();
    CpuMicroVU1.Reserve();
    for (u32 index = 0; index < 2; ++index) mVUProgCache::ResetForTest(index);

    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const u32 mul = (15u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | 0x2au;
    const u32 sub = (15u << 21) | (4u << 16) | (3u << 11) | (5u << 6) | 0x2cu;
    const std::vector<u32> branch{
        (0x20u << 25) | 3u, add, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
        (0x20u << 25) | 3u, mul, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
        NOP_L, sub | 0x40000000u, NOP_L, NOP_U};
    const std::vector<u32> quotient{
        0x800003bcu | (1u << 11) | (2u << 16), add,
        0x800003bfu, NOP_U, // WAITQ
        NOP_L, (15u << 21) | (3u << 11) | (5u << 6) | 0x20u, // ADDq
        NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
        NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
    const std::vector<u32> efu{
        0x8000077eu | (1u << 11), add, // ESUM p, vf1
        0x800007bfu, mul, // WAITP
        NOP_L, sub, NOP_L, NOP_U, NOP_L, NOP_U,
        NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
    auto clip = branch;
    clip[1] = 0x1ffu | (1u << 11) | (2u << 16);
    u32 cases = 0;
    for (u32 index = 0; index < 2; ++index)
        for (u32 kind = 0; kind < (index ? 4u : 3u); ++kind)
            for (u32 budget : {128u, 1u})
    {
        const auto& program = kind == 0 ? branch : kind == 1 ? quotient : kind == 2 && index ? efu : clip;
        char label[120];
        std::snprintf(label, sizeof(label), "baseline cache VU%u kind=%u budget=%u", index, kind, budget);
        std::printf("PERSIST_CASE %s\n", label);
        const auto interpreted = RunVU(index, false, program, 0x80, 0, budget);
        std::array<u32, 12> beforeRings{}, afterRings{};
        const auto baseline = RunPersistedVU(index, program, 0x80, budget, true, beforeRings);
        Compare(interpreted, baseline, "recorded baseline matches interpreter architectural state");
        Check(!baseline.fallback, "baseline cache fixture executes native code");
        std::vector<u8> image;
        const bool serialized = mVUPersist::TestSerializeNewestProgram(index, image);
        Check(serialized && !image.empty(), "baseline native graph is completely recorded and serializes");
        if (!serialized || image.empty()) continue;
        BaseVUmicroCPU* const cpu = index ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuMicroVU0;
        cpu->Reset();
        auto corrupt = image;
        corrupt.back() ^= 0x80;
        Check(!mVUPersist::TestHydrate(index, corrupt.data(), corrupt.size()), "corrupt native graph is rejected");
        const auto beforeStats = mVUPersist::GetStats(index);
        const bool hydrated = mVUPersist::TestHydrate(index, image.data(), image.size());
        Check(hydrated && mVUPersist::GetStats(index).programsHydrated == beforeStats.programsHydrated + 1,
            "native graph hydrates after program reset");
        if (!hydrated) continue;
        const u64 compiles = mVUPersist::GetBlockCompileCount(index);
        const auto restored = RunPersistedVU(index, program, 0x80, budget, false, afterRings);
        Check(mVUPersist::GetBlockCompileCount(index) == compiles,
            "hydrated native program executes without compiling any block");
        Compare(baseline, restored, "hydrated graph preserves native VF VI ACC memory and GIF");
        Check(!restored.fallback, "hydrated graph executes native code");
        Check(baseline.regs.cycle == restored.regs.cycle &&
            baseline.regs.nextBlockCycles == restored.regs.nextBlockCycles,
            "hydrated graph preserves exact cycles and next block budget");
        Check(baseline.regs.q.UL == restored.regs.q.UL && baseline.regs.p.UL == restored.regs.p.UL &&
            baseline.regs.pending_q == restored.regs.pending_q && baseline.regs.pending_p == restored.regs.pending_p,
            "hydrated graph preserves Q P and pending pipeline results");
        Check(!std::memcmp(baseline.regs.micro_statusflags, restored.regs.micro_statusflags, sizeof(baseline.regs.micro_statusflags)) &&
            !std::memcmp(baseline.regs.micro_macflags, restored.regs.micro_macflags, sizeof(baseline.regs.micro_macflags)) &&
            !std::memcmp(baseline.regs.micro_clipflags, restored.regs.micro_clipflags, sizeof(baseline.regs.micro_clipflags)) && beforeRings == afterRings,
            "hydrated graph preserves all architectural and internal flag ring instances");

        // The serialized options bind recording's fixed instruction shapes.
        // A disabled recording run must reject the old image, then compile
        // normally without producing a new serializable log.
        mVUPersist::SetRecordingEnabled(false);
        cpu->Reset();
        Check(!mVUPersist::TestHydrate(index, image.data(), image.size()),
            "recording disabled rejects an image of recorded instruction forms");
        const u64 disabledCompiles = mVUPersist::GetBlockCompileCount(index);
        const auto uncached = RunPersistedVU(index, program, 0x80, budget, false, afterRings);
        Check(mVUPersist::GetBlockCompileCount(index) > disabledCompiles && !mVUPersist::IsRecordingEnabled(),
            "recording disabled uses normal compile fallback");
        std::vector<u8> disabledImage;
        Check(!mVUPersist::TestSerializeNewestProgram(index, disabledImage),
            "recording disabled never serializes unrecorded native code");
        Compare(baseline, uncached, "recording disabled fallback preserves program results");
        mVUPersist::SetRecordingEnabled(true);
        ++cases;
    }
    Check(cases == 14, "all VU0 VU1 branch Q P CLIP and budget fixtures complete cache round trips");
    std::printf("VU_PERSIST_NATIVE cases=%u\n", cases);
    RunVuPersistPackTests(branch);
    // Both stub and target program must survive a hot indirect cache. The
    // pack test saves all graphs, then starts at the stub without recompiling.
    std::vector<u32> indirect;
    for (u32 pc=0x80; pc<0x130; pc+=8) indirect.insert(indirect.end(), {NOP_L,NOP_U});
    indirect[0]=(8u<<25)|(1u<<16)|0x20u;
    indirect[2]=(0x24u<<25)|(1u<<11);
    indirect[(0x100-0x80)/4]=(0x20u<<25)|3u;
    indirect[(0x100-0x80)/4+1]=add;
    indirect[(0x120-0x80)/4+1]=NOP_U|0x40000000u;
    RunVuPersistPackTests(indirect);
    cleanup();
}
