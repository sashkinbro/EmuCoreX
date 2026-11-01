// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after RunPersistedVU. Initial RED exercises real native execution;
// passive production counters sit at the actual dispatch key-copy seam.
#include "jit_oracle_vuaot_fast_dispatch_guards.inl"
void RunVuAotFastDispatchTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate warm VU dispatch fixture"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache; EmuFolders::Cache.clear();
    const ScopedGuard cleanup([&] {
        mvu_aot::HoldWorker(false); mvu_aot::TestHoldReadyPublication(false); mvu_aot::WaitWorker();
        EmuConfig.Cpu.Recompiler.EnableVUAot = false; CpuMicroVU1.Reset();
        mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false);
        EmuFolders::Cache = savedCache;
    });
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const u32 mul = (15u << 21) | (2u << 16) | (1u << 11) | (4u << 6) | 0x2au;
    const u32 esum = 0x8000077eu | (1u << 11);
    const u32 div = 0x800003bcu | (1u << 11) | (2u << 16);
    // RegionTier deliberately declines a single non-loop block. A real B tail
    // and distinct E-bit leaf prove this is a two-node native Region fixture.
    std::vector<u32> code;
    for (u32 pc = 0x80; pc <= 0xc0; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
    };
    put(0x80, esum, add); put(0x88, div, mul);
    put(0x90, (0x20u << 25) | 3u, add); // target 0xb0
    put(0x98, (8u << 25) | (11u << 16) | 0x333u); // mandatory delay marker
    put(0xb0, (8u << 25) | (10u << 16) | 0x222u, add);
    put(0xb8, NOP_L, NOP_U | 0x40000000u);
    vuaot::PipeState late, backup;
    late.p = 17; late.q = 7;
    backup = late; backup.viBackUp = 11; backup.VI[11] = 2; backup.needExactMatch = 1;
    const vuaot::PipeState* keys[] = {nullptr, &late, &backup};
    for (u32 mode = 0; mode < std::size(keys); ++mode)
    {
        const auto* key = keys[mode];
        std::array<u32, 12> expectedRings{}, rings{};
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        mvu_aot::ForceSynchronous(false);
        const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings, 0x80, key, 0x321);
        Check(expected.regs.VI[10].UL == 0x222u && expected.regs.VI[11].UL == 0x333u &&
            expected.regs.VF[3].UL[0] == 0x40006000u,
            "literal integer marker and exact ADD bits prove nontrivial warm fixture architecture");
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        mvu_aot::ForceSynchronous(true); mvu_aot::CountEntries(true);
        const auto startEntries = mvu_aot::NativeRegionEntryCount(0x80);
        const auto initial = RunPersistedVU(1, code, 0x80, 128, true, rings, 0x80, key, 0x321);
        Compare(expected, initial, "cold native dispatch preserves full architecture");
        Check(mvu_aot::NativeRegionEntryCount(0x80) > startEntries && !initial.fallback &&
            expected.regs.cycle == initial.regs.cycle && expectedRings == rings,
            "positive native region counter and exact cycles/rings establish genuine native dispatch");
        mvu_aot::ForceSynchronous(false);
        for (u32 run = 0; run < 8; ++run)
            RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, key, 0x321);
        Check(mvu_aot::PendingCount() == 0 && mvu_aot::ReadyCountForTest() == 0,
            "warm batch starts with no pending or accepted ready worker job");
        const auto before = mvu_aot::GetStats();
        const auto entries = mvu_aot::NativeRegionEntryCount(0x80), ends = mvu_aot::NativeEndCount();
        const auto compiles = mVUPersist::GetBlockCompileCount(1), bails = mvu_aot::BailCount();
        for (u32 run = 0; run < 8; ++run)
        {
            rings = {};
            const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, key, 0x321);
            Compare(expected, actual, "warm dispatch preserves VF VI ACC memory GIF and flag state");
            Check(expected.regs.cycle == actual.regs.cycle && expectedRings == rings &&
                expected.regs.pending_p == actual.regs.pending_p && expected.regs.pending_q == actual.regs.pending_q,
                "warm dispatch preserves exact budget pending PQ and every flag ring");
        }
        const auto after = mvu_aot::GetStats();
        Check(after.dispatches == before.dispatches + 8 && after.jumpEntries == before.jumpEntries &&
            after.jumpResolverCalls == before.jumpResolverCalls &&
            mVUPersist::GetBlockCompileCount(1) == compiles && after.automaticJobs == before.automaticJobs,
            "warm external accounting is exact without JR work new roots or baseline compilation");
        Check(mvu_aot::NativeRegionEntryCount(0x80) == entries + 8 &&
            mvu_aot::NativeEndCount() == ends + 8 && mvu_aot::BailCount() == bails,
            "every measured warm call actually executes the native region and native end");
        Check(after.dispatchSlowKeys - before.dispatchSlowKeys +
            after.dispatchFastEntries - before.dispatchFastEntries == 8,
            "passive real dispatch seam accounts for every measured entry exactly once");
        Check(after.dispatchFastEntries == before.dispatchFastEntries + 8 &&
            after.dispatchSlowKeys == before.dispatchSlowKeys,
            "unchanged warm native entries bypass the actual 96-byte slow key-copy seam");
        std::printf("VU_FAST_DISPATCH mode=%u slow=%llu fast=%llu native=%llu ends=%llu\n", mode,
            static_cast<unsigned long long>(after.dispatchSlowKeys - before.dispatchSlowKeys),
            static_cast<unsigned long long>(after.dispatchFastEntries - before.dispatchFastEntries),
            static_cast<unsigned long long>(mvu_aot::NativeRegionEntryCount(0x80) - entries),
            static_cast<unsigned long long>(mvu_aot::NativeEndCount() - ends));
    }
    RunVuAotFastDispatchGuardTests();
}
