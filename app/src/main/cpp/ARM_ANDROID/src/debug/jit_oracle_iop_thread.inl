// SPDX-FileCopyrightText: 2026 EmuCoreX Contributors
// SPDX-License-Identifier: GPL-3.0+
// Included after the IOP fixture helpers in jit_oracle.cpp. Its parent includes
// IopThread/IopThread.h outside the oracle's anonymous namespace.
void IopThreadNativeTests()
{
    EmuCoreXOracleSetSkipEvents(1);
    cpuinfo_initialize();
    EmuConfig = Pcsx2Config();
    EmuConfig.Speedhacks.vuThread = false;
    EmuConfig.Speedhacks.WaitLoop = false;
    if (!SysMemory::Allocate())
    {
        Check(false, "IOP worker oracle allocates emulator memory");
        return;
    }
    SysMemory::Reset();
    psxCpu = &psxRec;
    psxCpu->Reserve();
    const auto saved_cpu = psxCpu;
    const auto saved_fpcr = FPControlRegister::GetCurrent();
    float existing_iop_usage = 0.0f;
    float existing_iop_time = 0.0f;
    const bool can_check_iop_metric_lifecycle =
        !PerformanceMetrics::GetIOPThreadMetrics(existing_iop_usage, existing_iop_time);
    bool checked_iop_metric_lifecycle = false;
    for (const auto mode : {IopThreadMode::Lockstep, IopThreadMode::Threaded})
    {
        for (const bool jit : {false, true})
        {
            for (const s32 budget : {1, 7, 8, 17, 48, 4096})
            {
                auto code = IOPValueSetup();
                code.push_back(MipsR(8, 9, 10, 0, 33));
                code.push_back(MipsR(8, 9, 0, 0, 24));
                code.push_back(MipsR(0, 0, 11, 0, 18));
                code.push_back(MipsI(43, 22, 11, 0));
                const IOPSnapshot expected = RunIOPProgram(jit, code, budget);
                const auto expected_regs = psxRegs;
                IOPSnapshot actual{};
                psxRegisters actual_regs{};
                IopThread::Thread worker;
                IopThread::Params params;
                params.mode = mode;
                params.fpcr = EmuConfig.Cpu.FPUFPCR;
                params.run_slice = [&](u64 from, u64 target) {
                    actual = RunIOPProgram(jit, code, budget);
                    actual_regs = psxRegs;
                    return target;
                };
                const bool started = worker.Start(std::move(params));
                Check(started, "IOP reference starts a real worker");
                if (!started) continue;
                if (mode == IopThreadMode::Lockstep)
                    worker.LockstepRunSlice(budget);
                else
                {
                    worker.PublishWatermark(budget);
                    // Catch up to the published watermark and acquire the CPU state before inspecting it.
                    IopThread::ScopedHold hold(worker);
                }
                if (can_check_iop_metric_lifecycle && !checked_iop_metric_lifecycle)
                {
                    PerformanceMetrics::SetIOPThread(worker.GetThreadHandle());
                    float usage = -1.0f;
                    float average_time = -1.0f;
                    const bool active_metric = PerformanceMetrics::GetIOPThreadMetrics(usage, average_time);
                    Check(active_metric, "IOP metric exposes an active real worker handle");
                    Check(std::isfinite(usage) && usage >= 0.0f && std::isfinite(average_time) && average_time >= 0.0f,
                        "IOP active worker metric snapshot is finite and non-negative");
                    Check(usage == 0.0f && average_time == 0.0f,
                        "new IOP worker resets the previous sample before collection");
                    PerformanceMetrics::SetIOPThread(Threading::ThreadHandle());
                    usage = -1.0f;
                    average_time = -1.0f;
                    const bool stopped_metric = PerformanceMetrics::GetIOPThreadMetrics(usage, average_time);
                    Check(!stopped_metric && usage == 0.0f && average_time == 0.0f,
                        "IOP metric clears the stopped worker row and stale sample");
                    checked_iop_metric_lifecycle = true;
                }
                worker.Stop();
                CompareIOP(expected, actual, jit ? "IOP JIT inline/worker equality" : "IOP interpreter inline/worker equality");
                Check(actual_regs.cycle == expected_regs.cycle &&
                        actual_regs.iopCycleEE == expected_regs.iopCycleEE &&
                        actual_regs.iopCycleEECarry == expected_regs.iopCycleEECarry &&
                        actual_regs.iopBreak == expected_regs.iopBreak,
                    "IOP worker preserves exact cycle budget, overshoot and carry");
                Check(FPControlRegister::GetCurrent().bitmask == saved_fpcr.bitmask,
                    "IOP worker preserves caller FPCR");
            }
        }
    }
    psxCpu = saved_cpu;
}
