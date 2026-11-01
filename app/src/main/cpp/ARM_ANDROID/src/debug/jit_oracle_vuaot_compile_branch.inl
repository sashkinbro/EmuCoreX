// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after RunPersistedVU. C is reached only by a baseline direct edge;
// its branch body must execute native code, in addition to D's plain-E body.
void RunVuAotCompileBranchTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate late branch-body VU memory"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache; EmuFolders::Cache.clear();
    std::vector<u32> code;
    for (u32 pc = 0x80; pc < 0x100; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
    };
    const auto branch = [](u32 pc, u32 target) {
        return (0x20u << 25) | (((target - (pc + 8)) / 8) & 0x7ffu);
    };
    const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
    const u32 mul = (15u << 21) | (2u << 16) | (1u << 11) | (4u << 6) | 0x2au;
    put(0x80, NOP_L, add); put(0x88, branch(0x88, 0xa0));
    put(0xa0, NOP_L, NOP_U | 0x08000000u); // baseline-only runtime boundary
    put(0xa8, branch(0xa8, 0xc0));
    put(0xc0, NOP_L, mul); put(0xc8, branch(0xc8, 0xe0));
    put(0xe0, NOP_L, add); put(0xe8, NOP_L, NOP_U | 0x40000000u);
    vuaot::PipeState late; late.p = 17; late.q = 7;
    std::array<u32, 12> expectedColdRings{}, expectedLateRings{}, rings{};
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    const auto expectedCold = RunPersistedVU(1, code, 0x80, 128, true, expectedColdRings);
    const auto expectedLate = RunPersistedVU(1, code, 0x80, 128, true, expectedLateRings, 0x80, &late);
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::ForceSynchronous(true); mvu_aot::CountEntries(true);
    const auto bodies = mvu_aot::GetStats().bodyBlocks;
    const auto cold = RunPersistedVU(1, code, 0x80, 128, true, rings);
    Compare(expectedCold, cold, "cold branch-child fixture preserves baseline architecture");
    Check(mvu_aot::GetStats().bodyBlocks > bodies,
        "runtime-boundary child forces the external root through actual BodyTier fallback");
    const auto variants = [] {
        std::vector<JitBlockProfile> blocks; VU1_JitGetBlockProfiles(blocks);
        return std::count_if(blocks.begin(), blocks.end(), [](const auto& b) { return b.startpc == 0xc0; });
    };
    const auto beforeVariants = variants();
    const auto compiled = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
    Check(variants() > beforeVariants, "tiered image compiles a fresh direct branch-child pipeline variant");
    Compare(expectedLate, compiled, "late branch-child compilation preserves baseline architecture");
    mvu_aot::WaitWorker();
    const auto before = mvu_aot::GetStats();
    const auto entries = mvu_aot::EntryCount(), ends = mvu_aot::NativeEndCount();
    const auto compiles = mVUPersist::GetBlockCompileCount(1);
    const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
    Check(mvu_aot::GetStats().dispatches == before.dispatches + 1 &&
        mvu_aot::GetStats().jumpEntries == before.jumpEntries &&
        mvu_aot::GetStats().jumpResolverCalls == before.jumpResolverCalls &&
        mVUPersist::GetBlockCompileCount(1) == compiles,
        "direct branch child receives no external dispatch or indirect-target offer and needs no recompilation");
    Compare(expectedLate, actual, "late direct native branch child preserves complete architecture");
    Check(expectedLate.regs.cycle == actual.regs.cycle && expectedLateRings == rings &&
        expectedLate.regs.pending_p == actual.regs.pending_p && expectedLate.regs.pending_q == actual.regs.pending_q,
        "late direct branch child preserves exact cycles pending PQ and flag rings");
    Check(mvu_aot::NativeEndCount() > ends && mvu_aot::EntryCount() > entries + 2,
        "external root native entry and plain-E child cannot substitute for the actual late branch-body native entry");
    std::printf("AOT_COMPILE_BRANCH variants=%zu entries=%llu ends=%llu compiles=%llu dispatch=%llu jr=%llu\n",
        static_cast<size_t>(variants()), static_cast<unsigned long long>(mvu_aot::EntryCount() - entries),
        static_cast<unsigned long long>(mvu_aot::NativeEndCount() - ends),
        static_cast<unsigned long long>(mVUPersist::GetBlockCompileCount(1) - compiles),
        static_cast<unsigned long long>(mvu_aot::GetStats().dispatches - before.dispatches),
        static_cast<unsigned long long>(mvu_aot::GetStats().jumpEntries - before.jumpEntries));
    EmuConfig.Cpu.Recompiler.EnableVUAot = false; CpuMicroVU1.Reset();
    mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false); EmuFolders::Cache = savedCache;
}

namespace VuCompileBranchOracle
{
    struct Case
    {
        u32 opcode, is, it, input;
        bool taken;
    };
    const Case boundaries[] = {
        {0x20, 0, 0, 1, true}, {0x21, 0, 6, 1, true},
        {0x28, 1, 1, 1, true}, {0x28, 1, 2, 1, false},
        {0x29, 1, 2, 1, true}, {0x29, 1, 1, 1, false},
        {0x2c, 1, 0, 0xffff, true}, {0x2c, 1, 0, 0, false}, {0x2c, 1, 0, 1, false},
        {0x2d, 1, 0, 0xffff, false}, {0x2d, 1, 0, 0, false}, {0x2d, 1, 0, 1, true},
        {0x2e, 1, 0, 0xffff, true}, {0x2e, 1, 0, 0, true}, {0x2e, 1, 0, 1, false},
        {0x2f, 1, 0, 0xffff, false}, {0x2f, 1, 0, 0, true}, {0x2f, 1, 0, 1, true},
        {0x24, 7, 0, 1, true}, {0x25, 7, 6, 1, true},
    };

    std::vector<u32> Program(const Case& c, bool actualPQ = false)
    {
        std::vector<u32> code;
        for (u32 pc = 0x80; pc < 0x118; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
        const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
            code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
        };
        const auto branch = [](u32 pc, u32 target) {
            return (0x20u << 25) | (((target - (pc + 8)) / 8) & 0x7ffu);
        };
        put(0x80, (8u << 25) | (7u << 16) | 0x20u); // vi7 = 0x100 / 8
        put(0x88, branch(0x88, 0xa0));
        const u32 esum = 0x8000077eu | (1u << 11); // ESUM P, vf1
        const u32 div = 0x800003bcu | (1u << 11) | (2u << 16); // DIV Q, vf1.x, vf2.x
        put(0xa0, actualPQ ? esum : NOP_L, NOP_U | 0x08000000u);
        put(0xa8, branch(0xa8, 0xc0));
        const u32 input = (c.input == 0xffff ? (9u << 25) | 1u : (8u << 25) | c.input) | (1u << 16);
        const u32 mul = (15u << 21) | (2u << 16) | (1u << 11) | (4u << 6) | 0x2au;
        const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
        put(0xc0, input, mul); put(0xc8, actualPQ ? div : NOP_L, add); // drain the VI write before its branch reader
        put(0xd0, (c.opcode << 25) | (c.it << 16) | (c.is << 11) |
            (c.opcode == 0x24 || c.opcode == 0x25 ? 0u : 5u));
        put(0xd8, (8u << 25) | (11u << 16) | 0x333u); // every branch executes this delay marker
        put(0xe0, (8u << 25) | (10u << 16) | 0x111u, mul);
        put(0xe8, NOP_L, NOP_U | 0x40000000u);
        put(0x100, (8u << 25) | (10u << 16) | 0x222u, add);
        put(0x108, NOP_L, NOP_U | 0x40000000u);
        return code;
    }

    bool Model(const Case& c, const Snapshot& snapshot)
    {
        // Literal control-flow goldens independent of either emulator engine:
        // the selected leaf marker, exactly executed delay slot, and link PC.
        return snapshot.regs.VI[10].UL == (c.taken ? 0x222u : 0x111u) &&
            snapshot.regs.VI[11].UL == 0x333u &&
            snapshot.regs.VI[6].UL == (c.opcode == 0x21 || c.opcode == 0x25 ? 0x1cu : 6u);
    }

    bool DistinctPQ(const Snapshot& snapshot)
    {
        const u32 p = snapshot.regs.VI[REG_P].UL, q = snapshot.regs.VI[REG_Q].UL;
        return (p | snapshot.regs.pending_p) != 0 && (q | snapshot.regs.pending_q) != 0 &&
            p != snapshot.regs.pending_p && q != snapshot.regs.pending_q &&
            (p != q || snapshot.regs.pending_p != snapshot.regs.pending_q);
    }
}

// Deliberately not called by the first eight-check route. The owner enables
// this after the unconditional hook's native GREEN and per-PC counter gate.
void RunVuAotCompileBranchBoundaryTests(u64 (*inlineEntries)(u32), bool actualPQ = false)
{
    using namespace VuCompileBranchOracle;
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate branch-tail boundary oracle memory"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache; EmuFolders::Cache.clear();
    vuaot::PipeState late; late.p = 17; late.q = 7;
    constexpr u32 budgets[] = {1, 3, 7, 128};
    u32 cases = 0;
    for (const auto& c : boundaries)
    {
        const auto code = Program(c, actualPQ);
        bool pendingTail = false;
        const auto previousHook = vuaot::g_mvuBlockHook;
        void* const previousUser = vuaot::g_mvuBlockHookUser;
        if (actualPQ)
        {
            vuaot::g_mvuBlockHook = +[](const vuaot::MvuBlockTrace& trace, void* user) {
                if (trace.startPC != 0xc0) return;
                vuaot::PipeState end; std::memcpy(end.bytes, trace.endState, sizeof(end.bytes));
                *static_cast<bool*>(user) |= end.p > 0 && end.q > 0;
            };
            vuaot::g_mvuBlockHookUser = &pendingTail;
        }
        const ScopedGuard restoreHook([&] {
            vuaot::g_mvuBlockHook = previousHook; vuaot::g_mvuBlockHookUser = previousUser;
        });
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        std::array<u32, 12> expectedColdRings{}, expectedLateRings{}, rings{};
        const auto expectedCold = RunPersistedVU(1, code, 0x80, 128, true, expectedColdRings);
        const auto expectedLate = RunPersistedVU(1, code, 0x80, 128, true, expectedLateRings, 0x80, &late);
        Check(Model(c, expectedCold) && Model(c, expectedLate),
            "independent literal taken delay-slot and link-PC goldens agree with baseline branch execution");
        if (actualPQ)
            Check(pendingTail && DistinctPQ(expectedCold) && DistinctPQ(expectedLate),
                "actual ESUM and DIV create nonzero distinct PQ lanes with both results pending at the branch-body tail");
        std::vector<Snapshot> expectedBudget;
        std::vector<std::array<u32, 12>> expectedBudgetRings;
        for (u32 budget : budgets)
        {
            std::array<u32, 12> r{};
            expectedBudget.push_back(RunPersistedVU(1, code, 0x80, budget, true, r, 0x80, &late));
            expectedBudgetRings.push_back(r);
        }
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        mvu_aot::ForceSynchronous(true); mvu_aot::CountEntries(true);
        const auto bodyCount = mvu_aot::GetStats().bodyBlocks;
        rings = {};
        const auto cold = RunPersistedVU(1, code, 0x80, 128, true, rings);
        Compare(expectedCold, cold, "branch-tail cold external root preserves architecture");
        Check(mvu_aot::GetStats().bodyBlocks > bodyCount,
            "branch-tail fixture uses actual external-root BodyTier fallback before its late child");
        rings = {};
        const auto compiled = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
        Compare(expectedLate, compiled, "late branch-tail compilation preserves complete architecture");
        const auto entered = inlineEntries(0xc0);
        const auto compiledBlocks = mVUPersist::GetBlockCompileCount(1);
        rings = {};
        const auto actual = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x80, &late);
        Check(inlineEntries(0xc0) > entered && mVUPersist::GetBlockCompileCount(1) == compiledBlocks,
            "the actual directly reached conditional or indirect child executes its inline native body");
        Compare(expectedLate, actual, "late branch-tail native execution preserves complete architecture");
        Check(Model(c, actual) && (!actualPQ || DistinctPQ(actual)) &&
            expectedLate.regs.cycle == actual.regs.cycle && expectedLateRings == rings &&
            expectedLate.regs.pending_p == actual.regs.pending_p && expectedLate.regs.pending_q == actual.regs.pending_q,
            "native branch tails preserve independent control goldens exact cycles PQ and all flag rings");
        for (u32 i = 0; i < std::size(budgets); ++i)
        {
            rings = {};
            const auto split = RunPersistedVU(1, code, 0x80, budgets[i], false, rings, 0x80, &late);
            Compare(expectedBudget[i], split, "branch-tail budget boundary preserves complete baseline architecture");
            Check(expectedBudget[i].regs.cycle == split.regs.cycle && expectedBudgetRings[i] == rings &&
                expectedBudget[i].regs.pending_p == split.regs.pending_p && expectedBudget[i].regs.pending_q == split.regs.pending_q,
                "branch-tail short budget preserves exact cycles PQ and flag rings including nonprogress exits");
        }
        ++cases;
    }
    std::printf("AOT_COMPILE_BRANCH_BOUNDARY cases=%u budgets=%zu actual_pq=%d\n", cases, std::size(budgets), actualPQ);
    EmuConfig.Cpu.Recompiler.EnableVUAot = false; CpuMicroVU1.Reset();
    mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(false); EmuFolders::Cache = savedCache;
}
