// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSyncEventVUs();

// Observe the scheduler's actual calls without executing a VU program. The
// wrapper still computes the real signed cycle delta and minimum run budget.
class OracleEventVU final : public BaseVUmicroCPU
{
public:
    u32 calls = 0, budget = 0, otherStatus = 0;
    bool changeOtherStatus = false;
    explicit OracleEventVU(int index) { m_Idx = index; }
    const char* GetShortName() const override { return "oracle"; }
    const char* GetLongName() const override { return "event sync oracle"; }
    void Shutdown() override {}
    void Reset() override {}
    void SetStartPC(u32) override {}
    void Step() override {}
    void Clear(u32, u32) override {}
    void Execute(u32 cycles) override
    {
        ++calls;
        budget = cycles;
        (m_Idx ? VU1 : VU0).cycle += cycles;
        if (changeOtherStatus)
            VU0.VI[REG_VPU_STAT].UL = otherStatus;
    }
};

static __attribute__((noinline)) void OracleReferenceEventVUs()
{
    CpuVU0->ExecuteBlock();
    CpuVU1->ExecuteBlock();
}

static void EeVUSyncTests()
{
    EmuConfig = Pcsx2Config();
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate event sync oracle memory");
        return;
    }
    SysMemory::Reset();
    Check(!vu1Thread.IsOpen(), "event fixture has no concurrent VU worker");
    const auto old0 = CpuVU0, old1 = CpuVU1;
    const auto restore = ScopedGuard([&]() { CpuVU0 = old0; CpuVU1 = old1; });
    OracleEventVU vu0(0), vu1(1);
    CpuVU0 = &vu0;
    CpuVU1 = &vu1;
    EmuConfig.Cpu.Recompiler.EnableVU1 = true;

    const auto reset = [&](bool threaded, u32 status, u32 ee, u32 cycle0, u32 cycle1) {
        vu0.calls = vu1.calls = vu0.budget = vu1.budget = 0;
        vu0.changeOtherStatus = vu1.changeOtherStatus = false;
        EmuConfig.Speedhacks.vuThread = threaded;
        cpuRegs.cycle = ee;
        cpuRegs.CP0.n.Status.val = 0;
        cpuRegs.interrupt = 0;
        VU0.VI[REG_VPU_STAT].UL = status;
        VU0.cycle = cycle0;
        VU1.cycle = cycle1;
        vu1Thread.mtvuInterrupts.store(0);
    };

    // Include zero, short/long grants, an ahead-of-EE VU, and cycle wrap.
    const u32 deltas[] = {0, 1, 15, 16, 17, 511, 0xffffffffu, 0x80000000u};
    for (bool threaded : {false, true})
        for (u32 status : {0u, 1u, 0x100u, 0x101u})
            for (u32 ee : {7u, 0x100000u, 0xfffffff8u})
                for (u32 delta0 : deltas)
                    for (u32 delta1 : deltas)
                    {
                        reset(threaded, status, ee, ee - delta0, ee - delta1);
                        EmuCoreXOracleSyncEventVUs();
                        const bool run0 = (status & 1) && s32(delta0) > 0;
                        const bool run1 = !threaded && (status & 0x100) && s32(delta1) > 0;
                        const bool same = vu0.calls == u32(run0) && vu1.calls == u32(run1) &&
                            vu0.budget == (run0 ? std::max(16u, delta0) : 0u) &&
                            vu1.budget == (run1 ? std::max(16u, delta1) : 0u) &&
                            VU0.cycle == ee - delta0 + vu0.budget &&
                            VU1.cycle == ee - delta1 + vu1.budget &&
                            VU0.VI[REG_VPU_STAT].UL == status;
                        Check(same, "event VU activity, signed delta, budget and clocks");
                    }

    for (u32 nextStatus : {0u, 0x100u})
    {
        reset(false, nextStatus ^ 0x101u, 1000, 900, 900);
        vu0.changeOtherStatus = true;
        vu0.otherStatus = nextStatus;
        EmuCoreXOracleSyncEventVUs();
        Check(vu0.calls == 1 && vu1.calls == u32(nextStatus != 0),
            "VU1 observes status changed by VU0 in this event");
    }

    // Exercise all combinations of asynchronous flags, including an idle VU1.
    // Keep FINISH pending behind path 2 to check that sync does not deliver it
    // prematurely, and separately test immediate FINISH with all paths idle.
    constexpr u64 signal = 0x00ff00ff12345678ull, label = 0xff00ff0087654321ull;
    for (bool instant : {false, true})
        for (bool queued : {false, true})
            for (bool masked : {false, true})
                for (u32 flags = 0; flags < 32; ++flags)
                {
                    reset(true, 0x5a00, 1000, 900, 900);
                    EmuConfig.Speedhacks.vu1Instant = instant;
                    std::memset(eeHw, 0, Ps2MemSize::Hardware);
                    std::memset(g_RealGSMem, 0, sizeof(g_RealGSMem));
                    gifUnit.gsSIGNAL.Reset();
                    gifUnit.gsFINISH.Reset();
                    gifUnit.stat.P2Q = 1;
                    CSRreg.SIGNAL = queued;
                    GSIMR.SIGMSK = masked;
                    GSSIGLBLID.SIGID = 0xaabbccdd;
                    GSSIGLBLID.LBLID = 0x11223344;
                    vu1Thread.gsSignal.store(signal);
                    vu1Thread.gsLabel.store(label);
                    vu1Thread.mtvuInterrupts.store(flags);
                    EmuCoreXOracleSyncEventVUs();
                    Check(vu0.calls == 0 && vu1.calls == 0 && vu1Thread.mtvuInterrupts.load() == 0,
                        "idle MTVU consumes every pending flag without executing a VU");
                    u32 expectedStatus = 0x5a00;
                    if ((flags & VU_Thread::InterruptFlagVUEBit) && instant) expectedStatus &= ~0xff00;
                    if (flags & VU_Thread::InterruptFlagVUTBit) expectedStatus = (expectedStatus & ~0xff00) | 0x400;
                    Check(VU0.VI[REG_VPU_STAT].UL == expectedStatus, "MTVU E/T-bit status semantics");
                    const bool hasSignal = flags & VU_Thread::InterruptFlagSignal;
                    const bool hasLabel = flags & VU_Thread::InterruptFlagLabel;
                    Check(bool(CSRreg.SIGNAL) == (queued || hasSignal) &&
                        gifUnit.gsSIGNAL.queued == (queued && hasSignal) &&
                        GSSIGLBLID.SIGID == (hasSignal && !queued ? 0xaa34cc78u : 0xaabbccddu) &&
                        (!queued || !hasSignal || (gifUnit.gsSIGNAL.data[0] == u32(signal) &&
                            gifUnit.gsSIGNAL.data[1] == u32(signal >> 32))), "MTVU masked SIGNAL and queued SIGNAL");
                    Check(GSSIGLBLID.LBLID == (hasLabel ? 0x87224344u : 0x11223344u) &&
                        vu1Thread.gsLabel.load() == (hasLabel ? 0 : label), "MTVU LABEL merge and acknowledgement");
                    const u32 expectedIrqs = (hasSignal && !queued && !masked ? 1u : 0u) |
                        (flags & VU_Thread::InterruptFlagVUTBit ? 0x80u : 0u);
                    Check(psHu32(INTC_STAT) == expectedIrqs, "MTVU GS and VU1 interrupt causes");
                    Check(!CSRreg.FINISH && !gifUnit.gsFINISH.gsFINISHFired &&
                        gifUnit.gsFINISH.gsFINISHPending == bool(flags & VU_Thread::InterruptFlagFinish),
                        "MTVU FINISH stays pending while GIF path is queued");
                }
    for (bool masked : {false, true})
    {
        reset(true, 0, 1000, 900, 900);
        std::memset(eeHw, 0, Ps2MemSize::Hardware);
        std::memset(g_RealGSMem, 0, sizeof(g_RealGSMem));
        GSIMR.FINISHMSK = masked;
        gifUnit.stat.P2Q = 0;
        gifUnit.gsFINISH.Reset();
        vu1Thread.mtvuInterrupts.store(VU_Thread::InterruptFlagFinish);
        EmuCoreXOracleSyncEventVUs();
        Check(CSRreg.FINISH && !gifUnit.gsFINISH.gsFINISHPending &&
            gifUnit.gsFINISH.gsFINISHFired == !masked && psHu32(INTC_STAT) == (masked ? 0u : 1u),
            "MTVU FINISH delivery and IRQ mask with idle GIF paths");
    }

    for (bool threaded : {false, true})
    {
        reset(threaded, 0, 1000, 900, 900);
        std::array<double, 9> current{}, reference{};
        const auto timed = [](auto sync) {
            const auto start = std::chrono::steady_clock::now();
            for (u32 n = 0; n < 1048576; ++n) sync();
            return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 1048576;
        };
        for (u32 n = 0; n < current.size(); ++n)
            if (n & 1) { reference[n] = timed(OracleReferenceEventVUs); current[n] = timed(EmuCoreXOracleSyncEventVUs); }
            else { current[n] = timed(EmuCoreXOracleSyncEventVUs); reference[n] = timed(OracleReferenceEventVUs); }
        std::sort(current.begin(), current.end());
        std::sort(reference.begin(), reference.end());
        std::printf("EEVUSYNC threaded=%u current_median_ns=%.3f reference_median_ns=%.3f\n", threaded, current[4], reference[4]);
    }
}
