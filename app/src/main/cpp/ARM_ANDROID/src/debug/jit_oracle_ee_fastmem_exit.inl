// SPDX-License-Identifier: GPL-3.0+
// Proof for a nonreturning EE fastmem helper entered from the real
// recompiler execution frame. Include after jit_oracle_ee_fastmem.inl.
namespace
{
    constexpr u32 EEFastmemExitAlias = 0x48000000u;
    constexpr u32 EEFastmemExitHandlerPaddr = 0x1fe80000u;
    constexpr u32 EEFastmemExitPage = vtlb_private::VTLB_PAGE_SIZE;

    struct EEFastmemExitTrace
    {
        bool exit_immediately = false;
        u32 calls = 0;
        u32 address = 0;
        u32 pc = 0;
        u32 bd = 0;
        u32 cycle_before = 0;
        u32 cycle_after = 0;
    };

    EEFastmemExitTrace s_ee_fastmem_exit_trace;

    u32 EEFastmemExitRead32(u32 address)
    {
        auto& trace = s_ee_fastmem_exit_trace;
        ++trace.calls;
        trace.address = address;
        trace.pc = cpuRegs.pc;
        trace.bd = cpuRegs.IsDelaySlot;
        trace.cycle_before = cpuRegs.cycle;
        cpuRegs.cycle += 7;
        cpuRegs.nextEventCycle = cpuRegs.cycle + 4096;
        trace.cycle_after = cpuRegs.cycle;

        if (trace.exit_immediately)
        {
            // This uses the same recExecute setjmp/longjmp exit as production
            // immediate exits, from a real VTLB dispatcher call inside the
            // EE recompiled frame. The test must declare this symbol at TU
            // scope, outside this anonymous namespace.
            recExitExecutionImmediate();
        }
        return 0x89abcdefu;
    }

    struct EEFastmemExitRestore
    {
        Pcsx2Config::RecompilerOptions config = EmuConfig.Cpu.Recompiler;
        R5900cpu* cpu = Cpu;
        vtlb_private::VTLBPhysical physical =
            vtlb_private::vtlbdata.pmap[EEFastmemExitHandlerPaddr >> 12];

        ~EEFastmemExitRestore()
        {
            EmuCoreXOracleSetEeIrFastLoads(-1);
            EmuCoreXOracleSetEeIrRegisterAllocation(-1);
            EmuCoreXOracleSetEeIrMode(-1);
            vtlb_VMapUnmap(EEFastmemExitAlias, EEFastmemExitPage);
            if (physical.isHandler())
                vtlb_MapHandler(physical.assumeHandler(), EEFastmemExitHandlerPaddr, EEFastmemExitPage);
            else
                vtlb_MapBlock(reinterpret_cast<void*>(physical.assumePtr()),
                    EEFastmemExitHandlerPaddr, EEFastmemExitPage);
            EmuConfig.Cpu.Recompiler = config;
            Cpu = cpu;
        }
    };

    void EEFastmemNonreturningHelperTests()
    {
        EEFastmemExitRestore restore;
        Cpu = &recCpu;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableEECache = false;
        EmuConfig.Cpu.Recompiler.EnableFastmem = true;
        EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;

        const vtlbHandler handler = vtlb_RegisterHandler(nullptr, nullptr, EEFastmemExitRead32,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        vtlb_MapHandler(handler, EEFastmemExitHandlerPaddr, EEFastmemExitPage);
        vtlb_VMap(EEFastmemExitAlias, EEFastmemExitHandlerPaddr, EEFastmemExitPage);

        // Taken BEQ executes the LW as a delay instruction. Its target writes
        // r10, which must remain untouched when the helper exits before return.
        const std::vector<u32> code = {
            MipsI(4, 0, 0, 1),
            MipsI(35, 8, 9, 0),
            MipsI(9, 0, 10, 0x1234),
        };
        for (u32 i = 0; i < code.size(); ++i)
            memWrite32(EE_TEST_PC + i * 4, code[i]);

        std::array<GPR_reg, 32> gpr{};
        gpr[8].UD[0] = EEFastmemExitAlias;
        gpr[9].UD[0] = 0x1122334455667788ull;
        gpr[10].UD[0] = 0x8877665544332211ull;

        // Legacy reference ensures the test handler observes the same guest
        // address, delay PC/BD, helper cycle update, and completed load result.
        s_ee_fastmem_exit_trace = {};
        const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), nullptr,
            nullptr, nullptr, false);
        const EEFastmemExitTrace legacy_trace = s_ee_fastmem_exit_trace;

        EmuCoreXOracleSetEeIrMode(1);
        EmuCoreXOracleSetEeIrFastLoads(1);
        EmuCoreXOracleSetEeIrRegisterAllocation(0); // Force a real native frame.
        Cpu->Reset();

        s_ee_fastmem_exit_trace = {};
        s_ee_fastmem_exit_trace.exit_immediately = true;
        const u64 fast_sites_before = EmuCoreXOracleGetEeIrFastLoadCounts();
        EEFastmemResidentExecute(gpr);
        const u64 fast_sites = EmuCoreXOracleGetEeIrFastLoadCounts() - fast_sites_before;
        const EEFastmemExitTrace exit_trace = s_ee_fastmem_exit_trace;
        const u32 load_pc = EE_TEST_PC + 4;
        Check(fast_sites == 1 && vtlb_IsFaultingPC(load_pc) &&
            EmuCoreXOracleGetEeIrLastFrameSize() > 0,
            "EE fastmem helper exit reaches a compiled fault site with a real IR frame");
        Check(exit_trace.calls == 1 && exit_trace.address == EEFastmemExitHandlerPaddr &&
            exit_trace.pc == load_pc && exit_trace.bd == 1 &&
            exit_trace.cycle_after == exit_trace.cycle_before + 7,
            "EE nonreturning VTLB callback observes exact delay PC/BD and flushed cycles");
        Check(cpuRegs.pc == load_pc && cpuRegs.IsDelaySlot == 1 &&
            cpuRegs.GPR.r[9].UD[0] == gpr[9].UD[0] &&
            cpuRegs.GPR.r[10].UD[0] == gpr[10].UD[0] && cpuRegs.cycle == exit_trace.cycle_after,
            "EE immediate helper exit preserves fault context and skips post-access clear");

        // Re-enter the same execution path after the nonlocal exit. The first
        // fault marked this PC, so replay must use the helper path, return
        // normally, complete LW, and clear BD before block exit.
        s_ee_fastmem_exit_trace = {};
        const u64 replay_sites_before = EmuCoreXOracleGetEeIrFastLoadCounts();
        EEFastmemResidentExecute(gpr);
        const u64 replay_sites = EmuCoreXOracleGetEeIrFastLoadCounts() - replay_sites_before;
        const u64 expected_load = static_cast<u64>(static_cast<s64>(static_cast<s32>(0x89abcdefu)));
        Check(replay_sites == 0 && s_ee_fastmem_exit_trace.calls == 1 &&
            s_ee_fastmem_exit_trace.pc == load_pc && s_ee_fastmem_exit_trace.bd == 1 &&
            cpuRegs.GPR.r[9].UD[0] == expected_load && cpuRegs.IsDelaySlot == 0,
            "EE same-entry replay returns through helper, completes LW, and clears BD");
        Check(s_ee_fastmem_exit_trace.cycle_after == s_ee_fastmem_exit_trace.cycle_before + 7 &&
            cpuRegs.cycle == legacy.cycle && legacy_trace.pc == load_pc && legacy_trace.bd == 1 &&
            legacy_trace.cycle_after == legacy_trace.cycle_before + 7,
            "EE helper exit/replay preserves legacy cycle accounting and frame recovery");
    }
}
