// SPDX-License-Identifier: GPL-3.0+
// EE Load32 fastmem contract tests. Include after the EE program,
// snapshot, and comparison helpers in jit_oracle.cpp.
extern "C" void EmuCoreXOracleSetEeIrFastLoads(int mode);
extern "C" u64 EmuCoreXOracleGetEeIrFastLoadCounts();
extern "C" u64 EmuCoreXOracleGetEeIrDirectLoadCounts();
extern "C" void EmuCoreXEEOracleSetCompileCallback(void (*callback)(u32));
extern "C" void EmuCoreXEEForceExitAfterFirstBlock();

namespace
{
    constexpr u32 EEFastmemAlias = 0x42000000u;
    constexpr u32 EEFastmemHighAlias = 0xc2000000u;
    constexpr u32 EEFastmemRam = 0x00200000u;
    constexpr u32 EEFastmemMmio = 0x1fe00000u;
    constexpr u32 EEFastmemPage = vtlb_private::VTLB_PAGE_SIZE;

    // The chosen virtual alias is in the core's explicitly-unmapped range.
    // Restore it through the VTLB API and restore the physical page through
    // the corresponding mapping API; tests never manufacture vmap entries.
    struct EEFastmemRestore
    {
        Pcsx2Config::RecompilerOptions config;
        R5900cpu* cpu;
        vtlb_private::VTLBPhysical physical;

        EEFastmemRestore()
            : config(EmuConfig.Cpu.Recompiler), cpu(Cpu),
              physical(vtlb_private::vtlbdata.pmap[EEFastmemMmio >> 12])
        {
        }

        ~EEFastmemRestore()
        {
            EmuCoreXOracleSetEeIrFastLoads(-1);
            EmuCoreXOracleSetEeIrMode(-1);
            vtlb_VMapUnmap(EEFastmemAlias, EEFastmemPage * 2);
            vtlb_VMapUnmap(EEFastmemHighAlias, EEFastmemPage * 2);
            if (physical.isHandler())
                vtlb_MapHandler(physical.assumeHandler(), EEFastmemMmio, EEFastmemPage);
            else
                vtlb_MapBlock(reinterpret_cast<void*>(physical.assumePtr()), EEFastmemMmio, EEFastmemPage);
            EmuConfig.Cpu.Recompiler = config;
            Cpu = cpu;
        }
    };

    struct EEFastmemTrace
    {
        u32 calls = 0;
        std::array<u32, 4> address{};
        std::array<u32, 4> pc{};
        std::array<u32, 4> bd{};
        std::array<u32, 4> cycle_before{};
        std::array<u32, 4> cycle_after{};
    };

    EEFastmemTrace s_ee_fastmem_trace;

    u32 EEFastmemRead32(u32 address)
    {
        auto& trace = s_ee_fastmem_trace;
        const u32 slot = std::min<u32>(trace.calls, trace.address.size() - 1);
        trace.address[slot] = address;
        trace.pc[slot] = cpuRegs.pc;
        trace.bd[slot] = cpuRegs.IsDelaySlot;
        trace.cycle_before[slot] = cpuRegs.cycle;
        ++trace.calls;
        cpuRegs.cycle += 7;
        cpuRegs.nextEventCycle = cpuRegs.cycle + 4096;
        trace.cycle_after[slot] = cpuRegs.cycle;
#if defined(__aarch64__)
        // Clobber caller-saved vector registers to exercise the scalar helper
        // boundary. This does not claim to test a live vector value.
        asm volatile(
            "movi v0.16b, #0xa5\n\t"
            "movi v1.16b, #0xa5\n\t"
            "movi v2.16b, #0xa5\n\t"
            "movi v3.16b, #0xa5\n\t"
            "movi v4.16b, #0xa5\n\t"
            "movi v5.16b, #0xa5\n\t"
            "movi v6.16b, #0xa5\n\t"
            "movi v7.16b, #0xa5\n\t"
            ::: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "memory");
#endif
        return 0x76543210u ^ address;
    }

    std::array<GPR_reg, 32> EEFastmemGprs(u32 base, u32 base_register = 8)
    {
        std::array<GPR_reg, 32> regs{};
        for (u32 r = 1; r < regs.size(); ++r)
        {
            regs[r].UD[0] = 0x1020304000000000ull | (0x10203040u + r * 0x101u);
            regs[r].UD[1] = 0x5060708000000000ull | (0x50607080u + r * 0x103u);
        }
        regs[0] = {};
        if (base_register != 0)
            regs[base_register].UD[0] = base;
        return regs;
    }

    bool EEFastmemMatchesLoad(const EESnapshot& state, const std::array<GPR_reg, 32>& initial,
        u32 target, u64 expected)
    {
        for (u32 reg = 0; reg < 32; ++reg)
        {
            const u64 low = reg == target && target != 0 ? expected : initial[reg].UD[0];
            if (state.gpr[reg] != low || state.gpr_upper[reg] != initial[reg].UD[1])
                return false;
        }
        return true;
    }

    u64 s_ee_fastmem_compile_count = 0;
    void EEFastmemCompileCallback(u32 pc)
    {
        if (pc == EE_TEST_PC)
            ++s_ee_fastmem_compile_count;
    }

    void EEFastmemResidentExecute(const std::array<GPR_reg, 32>& gpr, u32 initial_bd = 0)
    {
        std::memset(&cpuRegs, 0, sizeof(cpuRegs));
        std::memset(&fpuRegs, 0, sizeof(fpuRegs));
        std::memcpy(cpuRegs.GPR.r, gpr.data(), sizeof(cpuRegs.GPR.r));
        cpuRegs.CP0.n.Status.val = 0x20000000u;
        cpuRegs.pc = EE_TEST_PC;
        cpuRegs.IsDelaySlot = initial_bd;
        cpuRegs.nextEventCycle = 0x7fffffffu;
        EEsCycle = EEoCycle = 0;
        EmuCoreXEEForceExitAfterFirstBlock();
        Cpu->Execute();
    }

    void EEFastmemRamLoadTests()
    {
        EEFastmemRestore restore;
        Cpu = &recCpu;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableEECache = false;
        EmuConfig.Cpu.Recompiler.EnableFastmem = true;
        EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
        vtlb_VMap(EEFastmemAlias, EEFastmemRam, EEFastmemPage * 2);
        vtlb_VMap(EEFastmemHighAlias, EEFastmemRam, EEFastmemPage * 2);

        // Unaligned and page-crossing values make the expected result
        // independent of the guest's configured fastmem representation.
        std::array<u8, EEFastmemPage * 2> bytes{};
        for (u32 i = 0; i < bytes.size(); ++i)
        {
            bytes[i] = static_cast<u8>(0x81u + i * 37u);
            memWrite8(EEFastmemRam + i, bytes[i]);
        }
        const auto raw32 = [&bytes](u32 offset) {
            return static_cast<u32>(bytes[offset]) |
                (static_cast<u32>(bytes[offset + 1]) << 8) |
                (static_cast<u32>(bytes[offset + 2]) << 16) |
                (static_cast<u32>(bytes[offset + 3]) << 24);
        };
        struct LoadCase { u32 opcode, base_register, target, base, imm, offset; bool sign; };
        const std::array<LoadCase, 7> cases = {{
            {35, 8, 9, EEFastmemAlias, 3, 3, true}, // LW unaligned
            {39, 8, 9, EEFastmemAlias, 0xfff, 0xfff, false}, // LWU crosses page
            {35, 8, 9, EEFastmemAlias, 0xffc, 0xffc, true},
            {39, 8, 9, EEFastmemHighAlias, 7, 7, false}, // high virtual alias
            {35, 8, 8, EEFastmemAlias + 0x20, 0, 0x20, true}, // base aliases target
            {39, 8, 0, EEFastmemAlias + 0x24, 0, 0x24, false}, // rt=0
            {35, 8, 9, EEFastmemAlias + 4, 0xfffc, 0, true}, // signed displacement
        }};
        for (u32 i = 0; i < cases.size(); ++i)
        {
            const auto& item = cases[i];
            const auto gpr = EEFastmemGprs(item.base, item.base_register);
            const std::vector<u32> code = {MipsI(item.opcode, item.base_register, item.target, item.imm)};
            const EESnapshot legacy = RunEEProgramWithIrMode(0, code, gpr.data(), nullptr, nullptr, nullptr, false);
            EmuCoreXOracleSetEeIrFastLoads(0);
            const EESnapshot slow = RunEEProgramWithIrMode(1, code, gpr.data(), nullptr, nullptr, nullptr, false);
            const u64 before = EmuCoreXOracleGetEeIrFastLoadCounts();
            EmuCoreXOracleSetEeIrFastLoads(1);
            const EESnapshot fast = RunEEProgramWithIrMode(1, code, gpr.data(), nullptr, nullptr, nullptr, false);
            const u64 fast_sites = EmuCoreXOracleGetEeIrFastLoadCounts() - before;
            const u32 word = raw32(item.offset);
            const u64 expected = item.sign ? static_cast<u64>(static_cast<s64>(static_cast<s32>(word))) : word;
            CompareEE(legacy, slow, "EE Load32 legacy/shared slow state");
            CompareEE(legacy, fast, "EE Load32 legacy/shared fast state");
            Check(EEFastmemMatchesLoad(fast, gpr, item.target, expected), "EE LW/LWU independent raw bytes");
            Check(legacy.cycle == slow.cycle && slow.cycle == fast.cycle,
                "EE Load32 exact legacy cycles");
            Check(fast_sites == 1, "EE Load32 compiled fast site");
        }
        EmuCoreXOracleSetEeIrFastLoads(-1);
    }

    void EEFastmemFaultReplayTests()
    {
        EEFastmemRestore restore;
        Cpu = &recCpu;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableEECache = false;
        EmuConfig.Cpu.Recompiler.EnableFastmem = true;
        EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;

        const vtlbHandler handler = vtlb_RegisterHandler(nullptr, nullptr, EEFastmemRead32,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        vtlb_MapHandler(handler, EEFastmemMmio, EEFastmemPage);
        vtlb_VMap(EEFastmemAlias, EEFastmemMmio, EEFastmemPage);

        const std::vector<u32> code = {
            MipsI(35, 8, 10, 0),
            MipsI(35, 8, 11, 4),
            MipsR(10, 11, 12, 0, 0x21), // ADDU
            0x1000ffffu, // BEQ r0,r0,-1
            0,
        };
        for (u32 i = 0; i < code.size(); ++i)
            memWrite32(EE_TEST_PC + i * 4, code[i]);
        std::array<u32, 32> cp0{};
        cp0[12] = 0x20000000u;
        const auto gpr = EEFastmemGprs(EEFastmemAlias);
        EmuCoreXOracleSetEeIrMode(1);
        EmuCoreXOracleSetEeIrFastLoads(1);
        Cpu->Reset();
        s_ee_fastmem_compile_count = 0;
        EmuCoreXEEOracleSetCompileCallback(EEFastmemCompileCallback);

        s_ee_fastmem_trace = {};
        const u64 sites_before = EmuCoreXOracleGetEeIrFastLoadCounts();
        EEFastmemResidentExecute(gpr);
        const u64 sites = EmuCoreXOracleGetEeIrFastLoadCounts() - sites_before;
        const u32 first = 0x76543210u ^ EEFastmemMmio;
        const u32 second = 0x76543210u ^ (EEFastmemMmio + 4);
        Check(s_ee_fastmem_trace.calls == 2 &&
            s_ee_fastmem_trace.address[0] == EEFastmemMmio &&
            s_ee_fastmem_trace.address[1] == EEFastmemMmio + 4 &&
            s_ee_fastmem_trace.pc[0] == EE_TEST_PC &&
            s_ee_fastmem_trace.pc[1] == EE_TEST_PC + 4 &&
            s_ee_fastmem_trace.bd[0] == 0 && s_ee_fastmem_trace.bd[1] == 0 &&
            s_ee_fastmem_trace.cycle_after[0] == s_ee_fastmem_trace.cycle_before[0] + 7 &&
            s_ee_fastmem_trace.cycle_after[1] == s_ee_fastmem_trace.cycle_before[1] + 7,
            "EE first-fault MMIO handler order/address/PC/BD/cycle");
        Check(sites == 2 && vtlb_IsFaultingPC(EE_TEST_PC) && vtlb_IsFaultingPC(EE_TEST_PC + 4),
            "EE two first-fault load sites compile and backpatch independently");
        Check(cpuRegs.GPR.r[10].UD[0] == static_cast<u64>(static_cast<s64>(static_cast<s32>(first))) &&
            cpuRegs.GPR.r[11].UD[0] == static_cast<u64>(static_cast<s64>(static_cast<s32>(second))) &&
            cpuRegs.GPR.r[12].UD[0] == static_cast<u64>(static_cast<s64>(static_cast<s32>(first + second))),
            "EE first-fault two-load result against independent handler model");

        const u64 compile_before_replay = s_ee_fastmem_compile_count;
        const u64 sites_before_replay = EmuCoreXOracleGetEeIrFastLoadCounts();
        s_ee_fastmem_trace = {};
        EEFastmemResidentExecute(gpr);
        Check(s_ee_fastmem_compile_count == compile_before_replay + 1 &&
            EmuCoreXOracleGetEeIrFastLoadCounts() == sites_before_replay &&
            s_ee_fastmem_trace.calls == 2,
            "EE same-entry replay after first fault recompiles to helper path");

        alignas(16) std::array<u8, EEFastmemPage> buffer_a{};
        alignas(16) std::array<u8, EEFastmemPage> buffer_b{};
        const std::array<u8, 8> data_a = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
        const std::array<u8, 8> data_b = {0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x10};
        std::copy(data_a.begin(), data_a.end(), buffer_a.begin());
        std::copy(data_b.begin(), data_b.end(), buffer_b.begin());
        vtlb_VMapBuffer(EEFastmemAlias, buffer_a.data(), EEFastmemPage);
        EEFastmemResidentExecute(gpr);
        Check(cpuRegs.GPR.r[12].UD[0] == static_cast<u64>(static_cast<s64>(static_cast<s32>(0x44332211u + 0x88776655u))),
            "EE compiled helper follows VTLB API remap to buffer A");
        vtlb_VMapBuffer(EEFastmemAlias, buffer_b.data(), EEFastmemPage);
        EEFastmemResidentExecute(gpr);
        Check(cpuRegs.GPR.r[12].UD[0] == static_cast<u64>(static_cast<s64>(static_cast<s32>(0xccbbaa99u + 0x10ffeedd))),
            "EE same entry follows VTLB API remap to buffer B");
        vtlb_VMap(EEFastmemAlias, EEFastmemRam, EEFastmemPage);
        memWrite32(EEFastmemRam, 0x10203040u);
        memWrite32(EEFastmemRam + 4, 0x50607080u);
        EEFastmemResidentExecute(gpr);
        Check(cpuRegs.GPR.r[12].UD[0] == static_cast<u64>(static_cast<s64>(static_cast<s32>(0x6080a0c0u))),
            "EE same entry follows VTLB API remap back to RAM");

        // Exercise a real fastmem fault inside a branch delay slot, then a
        // normal access later in the same guest program. The likely-branch
        // case checks that an annulled delay load does not reach the handler.
        vtlb_VMap(EEFastmemAlias, EEFastmemMmio, EEFastmemPage);
        struct BranchCase { u32 opcode, rs, rt; bool annul_delay; const char* name; };
        const std::array<BranchCase, 3> branches = {{
            {4, 0, 0, false, "taken BEQ"},
            {5, 0, 0, false, "untaken BNE"},
            {20, 0, 8, true, "annulled untaken BEQL"},
        }};
        for (const BranchCase& branch : branches)
        {
            const std::vector<u32> branch_code = {
                MipsI(branch.opcode, branch.rs, branch.rt, 1),
                MipsI(35, 8, 9, 0), // delay-slot load
                MipsI(35, 8, 10, 4), // following non-delay load
            };
            EmuCoreXOracleSetEeIrFastLoads(0);
            s_ee_fastmem_trace = {};
            const EESnapshot legacy = RunEEProgramWithIrMode(0, branch_code, gpr.data(), cp0.data(), nullptr, nullptr, true);
            const EEFastmemTrace legacy_trace = s_ee_fastmem_trace;

            EmuCoreXOracleSetEeIrFastLoads(1);
            s_ee_fastmem_trace = {};
            const EESnapshot fast = RunEEProgramWithIrMode(1, branch_code, gpr.data(), cp0.data(), nullptr, nullptr, true);
            const EEFastmemTrace fast_trace = s_ee_fastmem_trace;
            CompareEE(legacy, fast, "EE fastmem delay-slot legacy/shared full state");
            char branch_label[96];
            std::snprintf(branch_label, sizeof(branch_label), "EE fastmem delay-slot exact cycles %s", branch.name);
            Check(legacy.cycle == fast.cycle, branch_label);
            const u32 expected_calls = branch.annul_delay ? 1u : 2u;
            bool trace_ok = fast_trace.calls == expected_calls;
            if (branch.annul_delay)
            {
                trace_ok &= fast_trace.pc[0] == EE_TEST_PC + 8 && fast_trace.bd[0] == 0 &&
                    fast_trace.address[0] == EEFastmemMmio + 4;
            }
            else
            {
                trace_ok &= fast_trace.pc[0] == EE_TEST_PC + 4 && fast_trace.bd[0] == 1 &&
                    fast_trace.address[0] == EEFastmemMmio &&
                    fast_trace.pc[1] == EE_TEST_PC + 8 && fast_trace.bd[1] == 0 &&
                    fast_trace.address[1] == EEFastmemMmio + 4;
            }
            trace_ok &= legacy_trace.calls == expected_calls;
            std::snprintf(branch_label, sizeof(branch_label), "EE fastmem delay-slot handler PC/BD/order %s", branch.name);
            Check(trace_ok, branch_label);
            Check(vtlb_IsFaultingPC(EE_TEST_PC + 8) &&
                (branch.annul_delay ? !vtlb_IsFaultingPC(EE_TEST_PC + 4) : vtlb_IsFaultingPC(EE_TEST_PC + 4)),
                "EE fastmem delay-slot fault marking and annul");
        }

        EmuCoreXEEOracleSetCompileCallback(nullptr);
        EmuCoreXOracleSetEeIrFastLoads(-1);
        EmuCoreXOracleSetEeIrMode(-1);
    }

    void EEFastmemAdmissionTests()
    {
        EEFastmemRestore restore;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableFastmem = true;
        const u32 word = MipsI(35, 8, 9, 0);
        ir::Function fn;
        u32 end = 0;
        std::string error;
        Check(EeIr::LiftBlock(&word, {EE_TEST_PC + 0x400, 1}, fn, &end, &error),
            "EE fastmem admission fixture lifts");
        ir::Function iop_fn;
        u32 accepted = 0;
        Check(IopIr::LiftSequence(&word, EE_TEST_PC + 0x400, 1, iop_fn, &accepted, &error) && accepted == 1,
            "IOP Load32 domain-exclusion fixture lifts through its own frontend");
        for (u32 mode = 0; mode < 10; ++mode)
        {
            EeIr::LowerHooks hooks;
            hooks.ee_fastmem_abi = mode != 1;
            hooks.before_memory = mode == 2 ? nullptr : +[](void*, u32, bool) {};
            hooks.after_memory = mode == 3 ? nullptr : +[](void*) {};
            hooks.guest_exit = +[](void*, u32, bool) { oakAsm->RET(); };
            EeIr::LowerOptions options;
            options.fastmem_ee_loads = mode != 0;
            options.inline_body = mode != 7;
            options.optimize_ir = mode != 4;
            options.direct_ee_loads = mode != 5;
            if (mode == 9)
                options.guest_state = Arm64Ir::GuestState::IOP;
            options.hooks = &hooks;
            EmuConfig.Cpu.Recompiler.EnableFastmem = mode != 6;
            EmuConfig.Cpu.Recompiler.EnableEE = mode != 8;
            u8* const code = SysMemory::GetEERec() + 0x2000000;
            if (options.inline_body)
            {
                oakSetAsmPtr(code, 0x10000);
                oakStartBlock();
            }
            EeIr::LowerOutput out;
            ir::Function& fixture = mode == 9 ? iop_fn : fn;
            const bool lowered = EeIr::LowerBlock(fixture, options, code, 0x10000, &out, &error);
            if (options.inline_body)
                oakEndBlock();
            Check(lowered && out.host_size != 0 && out.fastmem_ee_load_operations == 0,
                "EE fastmem rejects disabled option/ABI/hooks/optimization/direct/fastmem/standalone/EE-disabled/IOP gates");
        }
    }

    void EEFastmemIntegrationTests()
    {
        EEFastmemRamLoadTests();
        EEFastmemFaultReplayTests();
        EEFastmemAdmissionTests();
    }
}
