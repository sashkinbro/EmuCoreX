// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Real COP0 transitions and native block execution, without test-only scheduling.
static void EeStatusGateTests()
{
    namespace Cop0 = R5900::Interpreter::OpcodeImpl::COP0;
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.WaitLoop = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate EE status gate machine");
        return;
    }
    SysMemory::Reset();
    Check(!IopThread::g_thread.IsRunning() && !IopThread::g_thread.MayHaveI2E(),
        "EE gate fixture has a quiescent empty production IOP queue");
    const auto savedMode = IopThread::GetMode();
    const auto savedCpu = Cpu;
    const bool savedSkip = s_oracleSkipEvents;
    const auto restore = ScopedGuard([&] {
        IopThread::g_thread.DrainI2E();
        IopThread::s_mode.store(savedMode, std::memory_order_relaxed);
        Cpu = savedCpu;
        EmuCoreXOracleSetSkipEvents(savedSkip);
        EmuCoreXOracleSetEeIrMode(-1);
        vu1Thread.mtvuInterrupts.store(0);
    });
    constexpr u32 enabled = 0x10c01;
    struct Cause { u32 intc, mask, dmac, dmask; };
    const Cause causes[] = {{}, {4,0,0,0}, {4,4,0,0}, {2,2,0,0}, {6,4,0,0},
        {0,0,4,0}, {0,0,4,4}, {0,0,0x8000,0}, {0,0,4,8}};
    const u32 statuses[] = {0, enabled, enabled | 2, enabled | 4, enabled | 0x8000,
        0x8000, 0x18, 0x20018};
    u64 directCases = 0;
    for (const auto mode : {IopThreadMode::Off, IopThreadMode::Lockstep, IopThreadMode::Threaded})
    {
        IopThread::s_mode.store(mode, std::memory_order_relaxed);
        for (u32 op = 0; op < 4; ++op)
        {
            bool schedule = true, architecture = true;
            for (const auto c : causes)
                for (u32 status : statuses)
                    for (u32 cycle : {100u, 0xfffffff0u, 0xfffffffeu})
                        for (u32 prior : {0u, 2u, 1000u})
                            for (u32 flags : {0u, 0xffffffffu})
                            {
                                std::memset(&cpuRegs, 0, sizeof(cpuRegs));
                                std::memset(eeHw, 0, Ps2MemSize::Hardware);
                                cpuRegs.cycle = cpuRegs.lastCOP0Cycle = cycle;
                                cpuRegs.nextEventCycle = cycle + prior;
                                cpuRegs.CP0.n.Status.val = status;
                                cpuRegs.CP0.n.EPC = 0x00106000;
                                cpuRegs.CP0.n.ErrorEPC = 0x00107000;
                                cpuRegs.pc = 0x00100004;
                                psHu32(INTC_STAT) = c.intc;
                                psHu32(INTC_MASK) = c.mask;
                                psHu16(DMAC_STAT) = c.dmac;
                                psHu16(DMAC_STAT + 2) = c.dmask;
                                vu1Thread.mtvuInterrupts.store(flags);
                                const bool allowed = op != 1 || (status & 0x20006) || !(status & 0x18);
                                const u32 finalStatus = op == 1 && allowed ? status | 0x10000 :
                                    op >= 2 ? status & ~(status & 4 ? 4u : 2u) : status;
                                const bool pending = (c.intc & c.mask) || (c.dmac & c.dmask) || (c.dmac & 0x8000);
                                const u32 expected = cycle + (allowed && (pending || (finalStatus & 0x8000)) ?
                                    std::min(prior, 4u) : prior);
                                if (op == 0) WriteCP0Status(status);
                                else if (op == 1) Cop0::EI();
                                else Cop0::ERET();
                                schedule &= cpuRegs.nextEventCycle == expected;
                                architecture &= cpuRegs.CP0.n.Status.val == finalStatus &&
                                    cpuRegs.cycle == cycle && psHu32(INTC_STAT) == c.intc &&
                                    psHu32(INTC_MASK) == c.mask && psHu16(DMAC_STAT) == c.dmac &&
                                    psHu16(DMAC_STAT + 2) == c.dmask && vu1Thread.mtvuInterrupts.load() == flags &&
                                    cpuRegs.pc == (op >= 2 ? (status & 4 ? 0x00107000u : 0x00106000u) : 0x00100004u);
                                ++directCases;
                            }
            Check(schedule, "COP0 status transitions schedule only actionable causes, timer or existing earlier deadlines");
            Check(architecture, "COP0 gate preserves wrapped clocks, status, return PC, raw causes and asynchronous VU flags");
        }
    }
    // A real release-published I2E item, not a shadow 'pending' Boolean. No worker
    // is running, so this deterministic fixture owns both queue endpoints.
    Check(IopThread::g_thread.PostI2E(IopThread::I2EKind::SbusIrq), "publish actual IOP SBUS queue item");
    for (const auto mode : {IopThreadMode::Off, IopThreadMode::Lockstep, IopThreadMode::Threaded})
    {
        IopThread::s_mode.store(mode, std::memory_order_relaxed);
        std::memset(eeHw, 0, Ps2MemSize::Hardware);
        std::memset(&cpuRegs, 0, sizeof(cpuRegs));
        cpuRegs.cycle = 100;
        cpuRegs.nextEventCycle = 1100;
        WriteCP0Status(2); // EXL cannot suppress the later Status+ERET delivery.
        Check(cpuRegs.nextEventCycle == (mode == IopThreadMode::Threaded ? 104u : 1100u) &&
            IopThread::g_thread.MayHaveI2E(), "only threaded queued SBUS schedules a status event without draining it");
    }
    Check(IopThread::g_thread.DrainI2E() == 1, "status gate leaves actual SBUS queue entry intact");

    IopThread::s_mode.store(IopThreadMode::Off, std::memory_order_relaxed);
    EmuConfig.Speedhacks.vuThread = true;
    std::memset(&cpuRegs, 0, sizeof(cpuRegs));
    std::memset(eeHw, 0, Ps2MemSize::Hardware);
    cpuRegs.nextEventCycle = 1000;
    vu1Thread.mtvuInterrupts.store(0xffffffffu);
    WriteCP0Status(0);
    Check(cpuRegs.nextEventCycle == 1000 && vu1Thread.mtvuInterrupts.load() == 0xffffffffu,
        "enabled MTVU asynchronous flags do not create host-arrival-dependent status scheduling");
    EmuConfig.Speedhacks.vuThread = false;

    EmuCoreXOracleSetSkipEvents(1);
    EmuCoreXOracleSetEeIrMode(0);
    cpuinfo_initialize();
    Cpu = &recCpu;
    Cpu->Reserve();
    u64 nativeCases = 0;
    for (u32 op = 0; op < 4; ++op)
        for (u32 kind = 0; kind < 5; ++kind)
        {
            IopThread::s_mode.store(IopThreadMode::Off, std::memory_order_relaxed);
            std::memset(eeHw, 0, Ps2MemSize::Hardware);
            std::array<u32,32> cp0{};
            std::array<u64,32> gpr{};
            std::array<GPR_reg,32> fullGpr{};
            for (u32 r = 1; r < 32; ++r)
            {
                gpr[r] = 0x8123456700000000ull + r * 0x10203ull;
                fullGpr[r].UD[0] = gpr[r];
                fullGpr[r].UD[1] = 0xfedcba9800000000ull + r * 0x30001ull;
            }
            const u32 status = enabled | (kind == 3 ? 0x8000 : 0);
            cp0[12] = op >= 2 ? status | (op == 3 ? 4u : 2u) :
                op == 1 ? status & ~0x10000u : status;
            cp0[14] = cp0[30] = EE_TEST_PC + 8;
            gpr[8] = fullGpr[8].UD[0] = status;
            if (kind == 1) psHu32(INTC_STAT) = psHu32(INTC_MASK) = 4;
            if (kind == 2) psHu16(DMAC_STAT) = 0x8000;
            if (kind == 4) psHu32(INTC_STAT) = 4; // masked cause remains quiet
            vu1Thread.mtvuInterrupts.store(0xffffffffu);
            const u32 opcode = op == 0 ? MipsI(16,4,8,12 << 11) :
                op == 1 ? 0x42000038u : 0x42000018u;
            // Dirty a resident guest register before the new native helper call.
            // Upper halves and every unrelated GPR must survive its flush/ABI.
            const auto result = RunEEProgram(true, {MipsI(9,10,10,1), opcode, MipsI(9,9,9,1), 0}, true,
                gpr.data(), cp0.data(), fullGpr.data());
            const bool pending = kind >= 1 && kind <= 3;
            Check(pending ? cpuRegs.nextEventCycle < 64u : cpuRegs.nextEventCycle == 4096u,
                "actual native Status/EI/ERET gates empty scheduling and retains pending/timer deadlines");
            Check(result.cp0[12] == status, "actual native gate preserves Status update and ERET exception-level clearing");
            bool gprs = true;
            for (u32 r = 1; r < 32; ++r)
            {
                const u64 expected = r == 10 ? static_cast<u64>(static_cast<s64>(static_cast<s32>(gpr[r] + 1))) : gpr[r];
                if (r != 9) gprs &= result.gpr[r] == expected;
                gprs &= result.gpr_upper[r] == fullGpr[r].UD[1];
            }
            Check(gprs, "native status helper preserves dirty resident GPRs, unrelated registers and upper halves");
            ++nativeCases;
        }
    std::printf("EE_STATUS_GATE direct_cases=%llu native_blocks=%llu\n",
        static_cast<unsigned long long>(directCases), static_cast<unsigned long long>(nativeCases));
}
