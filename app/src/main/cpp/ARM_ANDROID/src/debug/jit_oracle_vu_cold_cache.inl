// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after jit_oracle_vu_persist.inl. Each CLI phase is a fresh process.
// Only this dedicated temporary tree is touched; no user cache is opened.
struct VUColdCacheHeader
{
    u64 magic = 0x5643434f4c443031ull;
    u64 nonce = 0, imageBase = 0;
    u32 version = 1, regsSize = sizeof(VURegs), vu = 0, aot = 0;
    u32 memorySize = 0, gifSize = 0, busy = 0, fallback = 0;
    std::array<u32, 12> rings{};
};

bool VUColdCacheFile(const std::string& path, bool write, VUColdCacheHeader& header, Snapshot& snapshot)
{
    FILE* file = std::fopen(path.c_str(), write ? "wb" : "rb");
    if (!file) return false;
    const auto io = [&](void* data, size_t size) {
        return (write ? std::fwrite(data, 1, size, file) : std::fread(data, 1, size, file)) == size;
    };
    bool ok = io(&header, sizeof(header));
    if (!write)
    {
        ok = ok && header.magic == 0x5643434f4c443031ull && header.version == 1 &&
            header.regsSize == sizeof(VURegs) && header.vu < 2 && header.aot < 2 &&
            header.memorySize == (header.vu ? 0x4000u : 0x1000u) && header.gifSize <= 1024 * 1024;
        if (ok)
        {
            snapshot.memory.resize(header.memorySize);
            snapshot.gif.resize(header.gifSize);
            snapshot.busy = header.busy;
            snapshot.fallback = header.fallback != 0;
            snapshot.gifOverflowed = false;
        }
    }
    if (ok) ok = io(&snapshot.regs, sizeof(snapshot.regs)) &&
        io(snapshot.memory.data(), snapshot.memory.size()) && io(snapshot.gif.data(), snapshot.gif.size());
    if (write) ok = std::fflush(file) == 0 && ok;
    else ok = ok && std::fgetc(file) == EOF;
    return std::fclose(file) == 0 && ok;
}

void RunVUColdCacheTests(bool aot, bool writer)
{
    cpuinfo_initialize();
    const auto savedConfig = EmuConfig;
    const auto savedCache = EmuFolders::Cache;
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.vuFlagHack = false;
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
    EmuConfig.Cpu.Recompiler.EnableVUAot = aot;
    mVUPersist::SetProcessDisable(false);
    mVUPersist::SetTestManualRecording(false);
    const std::string root = "/data/local/tmp/emucorex-vu-cold-cache";
    EmuFolders::Cache = root + (aot ? "/aot" : "/clip");
    if (!FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(), true))
    {
        Check(false, "create dedicated cold-cache test directory");
        EmuFolders::Cache = savedCache; EmuConfig = savedConfig; return;
    }
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate cold-cache VU memory");
        EmuFolders::Cache = savedCache; EmuConfig = savedConfig; return;
    }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    mvu_aot::CountEntries(aot);
    mvu_aot::ForceSynchronous(aot && writer);
    mvu_aot::HoldWorker(aot && !writer);
    mvu_aot::SetTierUpRuns(1);
    EmuCoreXOracleSetVUIR(0, 0); EmuCoreXOracleSetVUIR(1, 0);
    EmuCoreXOracleSetVULocalReadonlyClip(true);
    EmuCoreXOracleSetVULocalIdentity(true);

    const void* const symbol = reinterpret_cast<const void*>(&mVUPersist::GetStats);
    mVUPersist::AddressRegion region{};
    s64 offset = 0;
    const bool classified = mVUPersist::ClassifyAddressRegion(symbol, region, offset);
    Check(classified && region == mVUPersist::AddressRegion::Image, "classify cold-cache module image address");
    const u64 base = reinterpret_cast<uintptr_t>(symbol) - offset;
    const u64 nonce = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const std::vector<u32> aotProgram{
        (0x20u << 25) | 3u, add, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
        NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
    // Canonical Ft.w=1 makes Fs.xyz>1 yield nonzero weighted CLIP bits (0x15).
    // FCGET observes the delayed history; DIV/WAITQ exercises the Q pipeline.
    const std::vector<u32> clipProgram{
        0x800003bcu | (1u << 11) | (2u << 16), NOP_U,
        0x800003bfu, NOP_U,
        NOP_L, 0x1ffu | (1u << 11),
        NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U, NOP_L, NOP_U,
        (0x1cu << 25) | (5u << 16), NOP_U,
        NOP_L, NOP_U | 0x40000000u, NOP_L, NOP_U};
    const auto& program = aot ? aotProgram : clipProgram;
    for (u32 index = aot ? 1u : 0u; index < 2; ++index)
    {
        VUColdCacheHeader header;
        Snapshot expected{};
        const std::string expectedPath = EmuFolders::Cache + "/expected-vu" + std::to_string(index) + ".bin";
        if (writer)
        {
            header.nonce = nonce; header.imageBase = base; header.vu = index; header.aot = aot;
        }
        else
        {
            const bool read = VUColdCacheFile(expectedPath, false, header, expected);
            Check(read && header.vu == index && header.aot == aot, "read prior-process expected VU state");
            if (!read || header.vu != index || header.aot != aot) continue;
            Check(base != header.imageBase, "reader module image uses a different ASLR base from writer");
        }
        std::printf("VU_COLD phase=%s kind=%s vu=%u writer_base=%llx reader_base=%llx nonce=%llu\n",
            writer ? "write" : "read", aot ? "aot" : "clip", index,
            static_cast<unsigned long long>(header.imageBase), static_cast<unsigned long long>(base),
            static_cast<unsigned long long>(header.nonce));
        mVUProgCache::SetTitle("ORACLE-COLD-" + std::to_string(header.nonce), aot ? 0xc01da071 : 0xc01dc11f);
        BaseVUmicroCPU* const cpu = index ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : &CpuMicroVU0;
        if (writer)
        {
            EmuConfig.Cpu.Recompiler.EnableVUAot = false;
            EmuConfig.Cpu.Recompiler.EnableVUProgramCache = false;
            expected = RunPersistedVU(index, program, 0x80, 128, true, header.rings);
            if (!aot)
            {
                Compare(RunVU(index, false, program, 0x80), expected,
                    "cold CLIP writer baseline matches interpreter");
                Check(expected.regs.VI[5].UL == 0x15 && expected.regs.VI[REG_CLIP_FLAG].UL == 0x15,
                    "cold CLIP fixture independently exercises all three positive weight lanes");
            }
            EmuConfig.Cpu.Recompiler.EnableVUAot = aot;
            EmuConfig.Cpu.Recompiler.EnableVUProgramCache = true;
            std::array<u32, 12> actualRings{};
            const u64 entries = mvu_aot::EntryCount();
            const auto recorded = RunPersistedVU(index, program, 0x80, 128, true, actualRings);
            Compare(expected, recorded, "cold writer recorded code preserves architecture");
            Check(expected.regs.cycle == recorded.regs.cycle && header.rings == actualRings,
                "cold writer preserves exact cycles and flag rings");
            if (aot) Check(mvu_aot::EntryCount() > entries, "cold AOT writer enters real generated region");
            mVUProgCache::TestWaitForPreload(index);
            const auto beforeSave = mVUProgCache::GetStats(index);
            cpu->Reset();
            mVUProgCache::FlushSidecars(index);
            const auto saved = mVUProgCache::GetStats(index);
            Check(saved.payloadWrites > beforeSave.payloadWrites, "cold writer saves baseline native graph");
            if (aot) Check(saved.sidecarWrites > beforeSave.sidecarWrites, "cold writer saves AOT native sidecar");
            const std::string packPath = mVUProgCache::TestPackPath(index);
            Check(packPath.find(EmuFolders::Cache + "/") == 0, "cold writer pack remains inside dedicated test tree");
            mVUProgCache::TestClose(index);
            Check(FileSystem::FileExists(packPath.c_str()), "cold writer closes pack on disk for next process");
            header.memorySize = static_cast<u32>(expected.memory.size());
            header.gifSize = static_cast<u32>(expected.gif.size());
            header.busy = expected.busy; header.fallback = expected.fallback;
            expected.regs.Micro = nullptr; expected.regs.Mem = nullptr;
            Check(VUColdCacheFile(expectedPath, true, header, expected), "cold writer commits independent expected state");
        }
        else
        {
            cpu->Reset();
            mVUProgCache::TestWaitForPreload(index);
            const auto loaded = mVUProgCache::GetStats(index);
            Check(loaded.loadedRecords > 0, "fresh process preloads previously written native pack");
            const u64 compiles = mVUPersist::GetBlockCompileCount(index);
            const u64 entries = mvu_aot::EntryCount();
            const auto runtime = mvu_aot::GetStats();
            std::array<u32, 12> actualRings{};
            const auto actual = RunPersistedVU(index, program, 0x80, 128, false, actualRings);
            const auto after = mVUProgCache::GetStats(index);
            Check(after.payloadHits > loaded.payloadHits && mVUPersist::GetBlockCompileCount(index) == compiles,
                "fresh process executes relocated native payload with zero baseline recompiles");
            if (aot)
                Check(mvu_aot::GetStats().savedRecords > runtime.savedRecords &&
                    mvu_aot::EntryCount() > entries && mvu_aot::PendingCount() == 0,
                    "fresh process installs saved AOT code and enters it while compiler worker is held");
            Compare(expected, actual, "fresh process relocated code matches writer architectural state");
            Check(expected.regs.cycle == actual.regs.cycle && expected.regs.nextBlockCycles == actual.regs.nextBlockCycles &&
                expected.regs.q.UL == actual.regs.q.UL && expected.regs.p.UL == actual.regs.p.UL &&
                expected.regs.pending_q == actual.regs.pending_q && expected.regs.pending_p == actual.regs.pending_p &&
                header.rings == actualRings, "fresh process preserves exact cycles PQ pending state and all flag rings");
            std::printf("VU_COLD_HIT vu=%u payload_hits=%llu compiles=%llu saved_records=%llu entries=%llu pending=%u\n", index,
                static_cast<unsigned long long>(after.payloadHits - loaded.payloadHits),
                static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(index) - compiles),
                static_cast<unsigned long long>(mvu_aot::GetStats().savedRecords - runtime.savedRecords),
                static_cast<unsigned long long>(mvu_aot::EntryCount() - entries), mvu_aot::PendingCount());
            mVUProgCache::TestClose(index);
        }
    }
    mvu_aot::HoldWorker(false); mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false);
    EmuCoreXOracleSetVUIR(0, -1); EmuCoreXOracleSetVUIR(1, -1);
    EmuFolders::Cache = savedCache; EmuConfig = savedConfig;
}
