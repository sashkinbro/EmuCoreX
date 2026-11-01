// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// A newly compiled conditional taken child immediately reads a VI written in
// the outer branch's delay slot. Ordinary VI latency zero does not kill backup.
void RunVuAotInternalVITakenTests()
{
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false; EmuConfig.Speedhacks.vuFlagHack = false;
    if (!SysMemory::Allocate()) { Check(false, "allocate internal conditional VI-backup fixture"); return; }
    CpuVU0 = &CpuMicroVU0; CpuVU1 = &CpuMicroVU1;
    CpuMicroVU0.Reserve(); CpuMicroVU1.Reserve();
    const auto savedCache = EmuFolders::Cache; EmuFolders::Cache.clear();
    std::vector<u32> code;
    for (u32 pc = 0x80; pc < 0x158; pc += 8) code.insert(code.end(), {NOP_L, NOP_U});
    const auto put = [&](u32 pc, u32 lower, u32 upper = NOP_U) {
        code[(pc - 0x80) / 4] = lower; code[(pc - 0x80) / 4 + 1] = upper;
    };
    const auto branch = [](u32 pc, u32 target, u32 op = 0x20, u32 is = 0, u32 it = 0) {
        return (op << 25) | (it << 16) | (is << 11) | (((target - (pc + 8)) / 8) & 0x7ffu);
    };
    put(0x80, NOP_L); put(0x88, branch(0x88, 0xa0));
    put(0xa0, NOP_L, NOP_U | 0x08000000u); put(0xa8, branch(0xa8, 0xc0));
    put(0xc0, (8u << 25) | (1u << 16) | 1u);
    put(0xc8, (8u << 25) | (12u << 16) | 0x333u);
    put(0xd0, branch(0xd0, 0x100, 0x28, 1, 1));
    put(0xd8, (8u << 25) | (11u << 16) | 0x333u);
    put(0xe0, (8u << 25) | (10u << 16) | 0x111u); put(0xe8, NOP_L, NOP_U | 0x40000000u);
    put(0x100, branch(0x100, 0x140, 0x28, 11, 12));
    put(0x110, (8u << 25) | (10u << 16) | 0x222u); put(0x118, NOP_L, NOP_U | 0x40000000u);
    put(0x140, (8u << 25) | (10u << 16) | 0x444u); put(0x148, NOP_L, NOP_U | 0x40000000u);
    const auto model = [](const Snapshot& s) {
        return s.regs.VI[10].UL == 0x222 && s.regs.VI[11].UL == 0x333 && s.regs.VI[12].UL == 0x333;
    };
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    const auto expected = RunVU(1, false, code, 0x80);
    Check(model(expected), "independent literal and interpreter require the old delay-slot VI value at the taken child");
    struct Capture { bool seen = false; vuaot::PipeState entry{}; } captured;
    const auto previousHook = vuaot::g_mvuBlockHook;
    void* const previousUser = vuaot::g_mvuBlockHookUser;
    vuaot::g_mvuBlockHook = +[](const vuaot::MvuBlockTrace& trace, void* user) {
        if (trace.startPC != 0x100) return;
        auto& c = *static_cast<Capture*>(user); c.seen = true;
        std::memcpy(c.entry.bytes, trace.entryState, sizeof(c.entry.bytes));
    };
    vuaot::g_mvuBlockHookUser = &captured;
    const ScopedGuard restoreHook([&] { vuaot::g_mvuBlockHook = previousHook; vuaot::g_mvuBlockHookUser = previousUser; });
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    mvu_aot::ForceSynchronous(false); mvu_aot::CountEntries(true);
    mvu_aot::SetTierUpRuns(100000); mvu_aot::SetJumpTierUpRuns(100000);
    const auto entered = mvu_aot::EntryCount();
    std::array<u32, 12> rings{};
    const auto cold = RunPersistedVU(1, code, 0x80, 128, true, rings);
    Check(captured.seen && captured.entry.viBackUp == 11 && captured.entry.VI[11] == 0,
        "new internal taken child retains an active VI-backup marker even with zero ordinary VI latency");
    Check(model(cold), "cold AOT-enabled baseline taken edge obeys the literal delayed-VI branch result");
    Compare(expected, cold, "cold internal conditional backup edge matches interpreter architecture");
    rings = {};
    const auto warm = RunPersistedVU(1, code, 0x80, 128, false, rings);
    Check(model(warm), "warmed internal taken edge keeps the same literal delayed-VI branch result");
    Compare(expected, warm, "warm internal conditional backup edge matches interpreter architecture");
    Check(mvu_aot::EntryCount() == entered && mvu_aot::PendingCount() == 0,
        "internal edge oracle exercises baseline code before any AOT tier can mask its result");
    std::printf("AOT_INTERNAL_VI_TAKEN backup=%u latency=%u cold=%x warm=%x\n", captured.entry.viBackUp,
        captured.entry.VI[11], cold.regs.VI[10].UL, warm.regs.VI[10].UL);
    mvu_aot::SetTierUpRuns(200); mvu_aot::SetJumpTierUpRuns(32);
    EmuConfig.Cpu.Recompiler.EnableVUAot = false; CpuMicroVU1.Reset();
    mvu_aot::CountEntries(false); EmuFolders::Cache = savedCache;
}
