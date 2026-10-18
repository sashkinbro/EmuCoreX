// SPDX-License-Identifier: GPL-3.0+
static void EeIRQEventTests()
{
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate full EE event oracle memory");
        return;
    }
    SysMemory::Reset();
    const auto old0 = CpuVU0, old1 = CpuVU1;
    const auto restore = ScopedGuard([&]() { CpuVU0 = old0; CpuVU1 = old1; });
    OracleEventVU vu0(0), vu1(1);
    CpuVU0 = &vu0;
    CpuVU1 = &vu1;
    // Keep the real IOP/counter schedulers initialized but not due. This runs
    // the complete production EE event body, rather than skipping its events.
    constexpr u32 all = 0x10c01;
    const u32 statuses[] = {0, 1, 0x10000, all, all | 2, all | 4,
        all & ~0x400u, all & ~0x800u, all | 0x400000};
    for (u32 intcStat : {0u, 2u, 4u, 6u})
        for (u32 intcMask : {0u, 2u, 4u})
            for (u32 dmacStat : {0u, 4u, 0x8000u, 0x8004u})
                for (u32 dmacMask : {0u, 4u})
                    for (bool enabled : {false, true})
                        for (u32 suspended : {0u, 1u, 2u})
                            for (u32 status : statuses)
                                for (bool delaySlot : {false, true})
                                {
                                    std::memset(&cpuRegs, 0, sizeof(cpuRegs));
                                    std::memset(&psxRegs, 0, sizeof(psxRegs));
                                    std::memset(eeHw, 0, Ps2MemSize::Hardware);
                                    std::memset(iopHw, 0, Ps2MemSize::IopHardware);
                                    cpuRegs.cycle = 0x100000;
                                    cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
                                    cpuRegs.pc = 0x00100004;
                                    cpuRegs.branch = delaySlot;
                                    cpuRegs.CP0.n.Status.val = status;
                                    cpuRegs.CP0.n.Cause = 0x13579bdf;
                                    cpuRegs.CP0.n.EPC = 0x11223344;
                                    EEsCycle = -64;
                                    EEoCycle = cpuRegs.cycle;
                                    iopEventAction = false;
                                    nextStartCounter = cpuRegs.cycle;
                                    nextDeltaCounter = 0x40000000;
                                    psxNextStartCounter = psxRegs.cycle;
                                    psxNextDeltaCounter = 0x40000000;
                                    VU0.VI[REG_VPU_STAT].UL = 0;
                                    vu1Thread.mtvuInterrupts.store(0);
                                    for (u32 i = 0; i < 4; ++i)
                                    {
                                        counters[i] = {};
                                        counters[i].count = 123 + 333 * i;
                                        counters[i].rate = 1;
                                        counters[i].hold = 0xffffffff;
                                    }
                                    psHu32(INTC_STAT) = intcStat;
                                    psHu32(INTC_MASK) = intcMask;
                                    psHu16(DMAC_STAT) = dmacStat;
                                    psHu16(DMAC_STAT + 2) = dmacMask;
                                    dmacRegs.ctrl.DMAE = enabled;
                                    psHu8(DMAC_ENABLER + 2) = suspended;

                                    _cpuEventTest_Shared();

                                    const bool pendingIntc = (intcStat & intcMask) != 0;
                                    const bool pendingDmac = ((dmacStat & dmacMask) || (dmacStat & 0x8000)) &&
                                        enabled && suspended != 1;
                                    const u32 cause = (pendingIntc ? 0x400u : 0u) | (pendingDmac ? 0x800u : 0u);
                                    const bool exception = (status & cause) && (status & 1) &&
                                        (status & 0x10000) && !(status & 6);
                                    const bool hold = pendingIntc && (intcStat & 2);
                                    Check(counters[0].hold == (hold ? 123u : 0xffffffffu) &&
                                        counters[1].hold == (hold ? 456u : 0xffffffffu),
                                        "INTC HOLD latches before CP0 enable checks, including masked bit 1");
                                    Check(cpuRegs.CP0.n.Status.val == (exception ? status | 2u : status) &&
                                        cpuRegs.CP0.n.Cause == (exception ? cause | (delaySlot ? 0x80000000u : 0u) : 0x13579bdfu) &&
                                        cpuRegs.CP0.n.EPC == (exception ? 0x00100004u - (delaySlot ? 4u : 0u) : 0x11223344u) &&
                                        cpuRegs.pc == (exception ? (status & 0x400000 ? 0xbfc00400u : 0x80000200u) : 0x00100004u) &&
                                        cpuRegs.branch == (exception ? 0u : u32(delaySlot)),
                                        "full EE event interrupt enable, cause, branch delay and vector semantics");
                                    Check(psHu32(INTC_STAT) == intcStat && psHu32(INTC_MASK) == intcMask &&
                                        psHu16(DMAC_STAT) == dmacStat && psHu16(DMAC_STAT + 2) == dmacMask,
                                        "event test preserves INTC/DMAC pending causes and masks");
                                    Check(cpuRegs.cycle == 0x100000 && EEsCycle == -64 && EEoCycle == cpuRegs.cycle &&
                                        !iopEventAction && !eeEventTestIsActive && psxRegs.cycle == 0 &&
                                        vu0.calls == 0 && vu1.calls == 0,
                                        "quiet full event preserves clocks and avoids idle IOP/VU execution");
                                }
}
