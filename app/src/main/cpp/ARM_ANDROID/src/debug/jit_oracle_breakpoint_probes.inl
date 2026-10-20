// SPDX-License-Identifier: GPL-3.0+
static void BreakpointProbeTests()
{
    if (!SysMemory::Allocate()) {
        Check(false, "allocate debugger probe oracle memory");
        return;
    }
    SysMemory::Reset();
    const auto oldEE = Cpu;
    const auto oldIOP = psxCpu;
    const bool oldSkipEvents = s_oracleSkipEvents;
    const bool oldPaused = CBreakPoints::GetCorePaused();
    R5900cpu fakeEE{};
    R3000Acpu fakeIOP{};
    fakeEE.Reset = []() {};
    fakeIOP.Reset = []() {};
    fakeEE.Clear = [](u32, u32) {};
    fakeIOP.Clear = [](u32, u32) {};
    Cpu = &fakeEE;
    psxCpu = &fakeIOP;
    const auto restore = ScopedGuard([&]() {
        // Updates invalidate host code; no guest VM is running in this fixture.
        Cpu = &fakeEE;
        psxCpu = &fakeIOP;
        CBreakPoints::ClearAllBreakPoints();
        CBreakPoints::SetCorePaused(oldPaused);
        EmuCoreXOracleSetSkipEvents(oldSkipEvents);
        Cpu = oldEE;
        psxCpu = oldIOP;
    });
    constexpr u32 pc = 0x00010000;
    const std::pair<u32, bool> instructions[] = {
        {0u, false}, {0x10000000u, true}, {0x08000000u, true},
        {0x03e00008u, true}, {0x0000000cu, false}, {0x42000018u, false}
    };
    for (const bool iop : {false, true}) {
        const auto which = iop ? BREAKPOINT_IOP : BREAKPOINT_EE;
        for (const auto [op, branch] : instructions) {
            // Preserve the existing IOP classification of syscall/eret as branches.
            const bool delaySlot = branch || (iop && (op == 0xcu || op == 0x42000018u));
            memWrite32(pc, op);
            memWrite32(pc + 4, 0);
            iopMemWrite32(pc, op);
            iopMemWrite32(pc + 4, 0);
            for (const auto owner : {BREAKPOINT_EE, BREAKPOINT_IOP, BREAKPOINT_IOP_AND_EE})
                for (u32 mask = 0; mask < 4; ++mask)
                    for (const bool temporary : {false, true})
                        for (const bool enabled : {false, true}) {
                            CBreakPoints::ClearAllBreakPoints();
                            if (mask & 1) CBreakPoints::AddBreakPoint(owner, pc, temporary, enabled);
                            if (mask & 2) CBreakPoints::AddBreakPoint(owner, pc + 4, temporary, enabled);
                            const int expected = (owner == which) && enabled ?
                                ((mask & 1 ? 1 : 0) | (delaySlot && (mask & 2) ? 2 : 0)) : 0;
                            const int actual = iop ? psxIsBreakpointNeeded(pc) : isBreakpointNeeded(pc);
                            Check(actual == expected, iop ? "IOP breakpoint CPU/enabled/temporary/delay-slot matrix" :
                                "EE breakpoint CPU/enabled/temporary/delay-slot matrix");
                        }
        }
        CBreakPoints::ClearAllBreakPoints();
        memWrite32(pc, 0x10000000u);
        iopMemWrite32(pc, 0x10000000u);
        CBreakPoints::AddBreakPoint(which, pc, false, false);
        CBreakPoints::AddBreakPoint(which, pc, true, true);
        Check((iop ? psxIsBreakpointNeeded(pc) : isBreakpointNeeded(pc)) == 1,
            "enabled temporary breakpoint overlaps disabled permanent breakpoint");
        CBreakPoints::ClearAllBreakPoints();
        CBreakPoints::AddBreakPoint(which, pc + 4, true, true);
        Check(CBreakPoints::GetNumBreakpoints() == 0 &&
            (iop ? psxIsBreakpointNeeded(pc) : isBreakpointNeeded(pc)) == 2,
            "temporary-only breakpoint remains visible in the delay slot");
    }
    CBreakPoints::ClearAllBreakPoints();
    for (u32 n = 0; n < 16; ++n) {
        memWrite32(pc + n * 4, instructions[n % 6].first);
        iopMemWrite32(pc + n * 4, instructions[n % 6].first);
    }
    for (const bool iop : {false, true}) {
        int (*volatile probe)(u32) = iop ? psxIsBreakpointNeeded : isBreakpointNeeded;
        std::array<double, 7> times;
        u32 sum = 0;
        for (double& ns : times) {
            const auto start = std::chrono::steady_clock::now();
            for (u32 n = 0; n < 262144; ++n) sum += probe(pc + (n & 15) * 4);
            ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 262144;
        }
        std::sort(times.begin(), times.end());
        Check(sum == 0, "empty debugger probe benchmark has no breakpoint hits");
        std::printf("BREAKPOINT_PROBE core=%s median_ns=%.3f min_ns=%.3f max_ns=%.3f\n",
            iop ? "IOP" : "EE", times[3], times.front(), times.back());
    }
    EmuCoreXOracleSetSkipEvents(1);
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    Cpu = &recCpu;
    Cpu->Reserve();
    for (const u32 count : {32u, 128u, 512u}) {
        std::vector<u32> code(count, MipsI(9, 8, 8, 1));
        const EESnapshot expected = RunEEProgram(false, code);
        std::array<double, 9> times;
        for (double& us : times) {
            const auto start = std::chrono::steady_clock::now();
            const EESnapshot actual = RunEEProgram(true, code);
            us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            CompareEE(expected, actual, "cold EE block compile preserves interpreter state");
        }
        std::sort(times.begin(), times.end());
        std::printf("BREAKPOINT_COLD guest_instructions=%u median_us=%.3f min_us=%.3f max_us=%.3f\n",
            count, times[4], times.front(), times.back());
    }
}
