// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// An explicit ownerPC models resumption inside an existing owner's start-PC
// partition. It does not change the production policy for external entries.
void RunVuAotStaticLinkSafetyTests()
{
    using namespace VuCompileBranchOracle;
    cpuinfo_initialize(); EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate same-owner static-link fixture"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache; EmuFolders::Cache.clear();
    auto code = Program(boundaries[0], true);
    while (code.size() < (0x1a0 - 0x80) / 4) code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
    };
    for (u32 pc : {0x160u, 0x180u})
    {
        put(pc, (8u << 25) | (7u << 16) | 0x20u);
        put(pc + 8, 0x8000077eu | (1u << 11), NOP_U | 0x08000000u); // ESUM and baseline T boundary
        put(pc + 16, (0x20u << 25) | (((0xc0 - (pc + 24)) / 8) & 0x7ffu));
    }
    constexpr u32 budgets[] = {1, 3, 7, 13, 128};
    const auto variants = [] {
        std::vector<JitBlockProfile> blocks; VU1_JitGetBlockProfiles(blocks);
        return std::count_if(blocks.begin(), blocks.end(), [](const auto& b) { return b.startpc == 0xc0; });
    };
    for (bool recording : {false, true})
    {
        mVUPersist::SetTestManualRecording(true); mVUPersist::SetRecordingEnabled(recording);
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        std::array<u32, 12> expectedRings{}, firstRings{}, newRings{}, rings{};
        const auto expected = RunPersistedVU(1, code, 0x80, 128, true, expectedRings);
        const auto expectedFirst = RunPersistedVU(1, code, 0x80, 128, true, firstRings, 0x160, nullptr, 0, 0x80);
        const auto expectedNew = RunPersistedVU(1, code, 0x80, 128, true, newRings, 0x180, nullptr, 0, 0x80);
        std::vector<Snapshot> expectedBudget;
        std::vector<std::array<u32, 12>> expectedBudgetRings;
        for (u32 budget : budgets)
        {
            std::array<u32, 12> r{};
            expectedBudget.push_back(RunPersistedVU(1, code, 0x80, budget, true, r));
            expectedBudgetRings.push_back(r);
        }
        Check(Model(boundaries[0], expected) && Model(boundaries[0], expectedFirst) && Model(boundaries[0], expectedNew),
            "same-owner caller baselines satisfy independent taken delay-slot and link goldens");
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        mvu_aot::CountEntries(true); mvu_aot::ForceSynchronous(false); mvu_aot::SetTierUpRuns(100000);
        rings = {};
        const auto first = RunPersistedVU(1, code, 0x80, 128, true, rings, 0x160, nullptr, 0, 0x80);
        const auto owner = mvu_aot::CurrentOwnerForTest();
        Check(owner && mVUPersist::IsRecordingEnabled() == recording,
            "caller fixture owns an actual program under the requested recorder policy");
        Compare(expectedFirst, first, "preexisting baseline caller preserves architecture before native publication");
        mvu_aot::ForceSynchronous(true); mvu_aot::SetTierUpRuns(1);
        for (u32 run = 0; run < 7; ++run)
        {
            rings = {}; RunPersistedVU(1, code, 0x80, 128, false, rings);
        }
        const auto entered = mvu_aot::NativeRegionEntryCount(0xc0);
        const auto count = mvu_aot::BoundStaticLinksForTest();
        rings = {};
        const auto warm = RunPersistedVU(1, code, 0x80, 128, false, rings);
        Check(mvu_aot::CurrentOwnerForTest() == owner && mvu_aot::NativeRegionEntryCount(0xc0) > entered &&
            count >= 2 && mvu_aot::TestStaticLinkWords(false),
            "multiple preexisting baseline callers have real C native entries and valid original and live B26 words");
        Compare(expected, warm, "preexisting static inbound C preserves complete architecture");
        Check(expected.regs.cycle == warm.regs.cycle && expectedRings == rings,
            "preexisting static inbound C preserves exact cycles and all rings");
        const auto oldVariants = variants();
        const auto beforeNew = mvu_aot::NativeRegionEntryCount(0xc0);
        mvu_aot::SetTierUpRuns(100000); mvu_aot::ForceSynchronous(false); mvu_aot::HoldWorker(true);
        rings = {};
        const auto added = RunPersistedVU(1, code, 0x80, 128, false, rings, 0x180, nullptr, 0, 0x80);
        Check(mvu_aot::CurrentOwnerForTest() == owner && mvu_aot::BoundStaticLinksForTest() > count &&
            variants() == oldVariants && mvu_aot::NativeRegionEntryCount(0xc0) > beforeNew &&
            mvu_aot::TestStaticLinkWords(false),
            "new caller retains owner and C key then binds after its immutable baseline compiler snapshot closes");
        Compare(expectedNew, added, "new caller of already native C preserves complete architecture");
        Check(expectedNew.regs.cycle == added.regs.cycle && newRings == rings,
            "new caller of already native C preserves exact cycles and rings");
        const auto beforeBudget = mvu_aot::BailCount();
        for (u32 i = 0; i < std::size(budgets); ++i)
        {
            rings = {};
            const auto split = RunPersistedVU(1, code, 0x80, budgets[i], false, rings);
            Compare(expectedBudget[i], split, "bound C private budget fallback preserves complete architecture");
            Check(expectedBudget[i].regs.cycle == split.regs.cycle && expectedBudgetRings[i] == rings &&
                expectedBudget[i].regs.pending_p == split.regs.pending_p && expectedBudget[i].regs.pending_q == split.regs.pending_q,
                "bound C short-budget fallback preserves cycles pending PQ and every flag ring");
        }
        Check(mvu_aot::BailCount() > beforeBudget && mvu_aot::TestStaticLinkWords(false),
            "small budgets really use private native bails while preserving static inbound instructions");
        Check(mvu_aot::TestStaticLinkWords(true),
            "production restore writes every original B26 while all baseline owners are still alive");
        CpuMicroVU1.Reset();
        Check(mvu_aot::BoundStaticLinksForTest() == 0 && mvu_aot::CurrentOwnerForTest() == 0,
            "actual reset releases all old static bindings and the current owner lifetime");
        mvu_aot::HoldWorker(false);
        std::printf("AOT_STATIC_LINK_SAFETY recording=%d old_bound=%u variants=%zu\n", recording, count,
            static_cast<size_t>(oldVariants));
    }
    mVUPersist::SetTestManualRecording(false);
    mvu_aot::HoldWorker(false); mvu_aot::SetTierUpRuns(200); mvu_aot::ForceSynchronous(false);
    mvu_aot::CountEntries(false); EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    CpuMicroVU1.Reset(); EmuFolders::Cache = savedCache;
}
