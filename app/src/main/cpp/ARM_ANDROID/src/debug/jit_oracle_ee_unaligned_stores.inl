// SPDX-License-Identifier: GPL-3.0+
// Include after jit_oracle_ee_fastmem.inl. This is a Debug-native fixture;
// it deliberately uses the production legacy EE compiler, not the IR stores.
extern "C" bool EmuCoreXOracleGetEeCompiledBlock(u32, const void**, u32*);

namespace
{
    // Byte selection is independent of the generated shift/mask formula and
    // never shifts by 64. Little-endian SDL writes source's high bytes first.
    u64 EeUnalignedMerge(bool left, u32 alignment, u64 old, u64 source)
    {
        for (u32 byte = 0; byte < 8; ++byte)
        {
            if ((left && byte <= alignment) || (!left && byte >= alignment))
            {
                const u32 from = left ? byte + 7 - alignment : byte - alignment;
                old = (old & ~(0xffull << (byte * 8))) |
                    (((source >> (from * 8)) & 0xffull) << (byte * 8));
            }
        }
        return old;
    }

    u32 EeUnalignedEqualityBranches()
    {
        const void* host = nullptr;
        u32 bytes = 0;
        if (!EmuCoreXOracleGetEeCompiledBlock(EE_TEST_PC, &host, &bytes) || !host || bytes == 0)
            return ~0u;
        u32 count = 0;
        for (u32 offset = 0; offset + 4 <= bytes; offset += 4)
        {
            u32 instruction;
            std::memcpy(&instruction, static_cast<const u8*>(host) + offset, 4);
            count += (instruction & 0xff00001fu) == 0x54000000u; // B.EQ only
        }
        return count;
    }

    struct EeUnalignedTrace
    {
        u64 value = 0;
        u32 count = 0;
        std::array<char, 4> kind{};
        std::array<u32, 4> address{}, pc{}, bd{};
    };
    EeUnalignedTrace s_ee_unaligned_trace;

    void EeUnalignedRecord(char kind, u32 address)
    {
        auto& t = s_ee_unaligned_trace;
        const u32 slot = std::min<u32>(t.count++, 3);
        t.kind[slot] = kind;
        t.address[slot] = address;
        t.pc[slot] = cpuRegs.pc;
        t.bd[slot] = cpuRegs.IsDelaySlot;
    }

    mem64_t EeUnalignedRead64(u32 address)
    {
        EeUnalignedRecord('r', address);
        // A visible side effect makes accidental repeated/read-on-full-width
        // accesses observable even if the final write happens to be identical.
        const u64 value = s_ee_unaligned_trace.value;
        s_ee_unaligned_trace.value ^= 0x0102040810204080ull;
        return value;
    }

    void EeUnalignedWrite64(u32 address, mem64_t value)
    {
        EeUnalignedRecord('w', address);
        s_ee_unaligned_trace.value = value;
    }

    bool EeUnalignedGprs(const EESnapshot& state, const std::array<GPR_reg, 32>& expected)
    {
        for (u32 reg = 0; reg < expected.size(); ++reg)
            if (state.gpr[reg] != expected[reg].UD[0] || state.gpr_upper[reg] != expected[reg].UD[1])
            {
                std::fprintf(stderr,
                    "EE_UNALIGNED_GPR_DIFF r=%u actual=%016llx/%016llx expected=%016llx/%016llx\n",
                    reg, static_cast<unsigned long long>(state.gpr[reg]),
                    static_cast<unsigned long long>(state.gpr_upper[reg]),
                    static_cast<unsigned long long>(expected[reg].UD[0]),
                    static_cast<unsigned long long>(expected[reg].UD[1]));
                return false;
            }
        return true;
    }

    void EeUnalignedRamTests()
    {
        EEFastmemRestore restore;
        Cpu = &recCpu;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableEECache = false;
        EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
        vtlb_VMap(EEFastmemAlias, EEFastmemRam, EEFastmemPage);
        std::array<u32, 32> cp0{};
        cp0[12] = 0x20000000u;
        bool bytes_ok = true, registers_ok = true, fast_shape = true, soft_shape = true;
        u32 cases = 0;
        for (bool fast : {false, true})
        for (bool left : {false, true})
        for (u32 alignment = 0; alignment < 8; ++alignment)
        for (u32 home = 0; home < 7; ++home)
        {
            EmuConfig.Cpu.Recompiler.EnableFastmem = fast;
            auto initial = EEFastmemGprs(EEFastmemAlias + 128);
            initial[9].UD[0] = initial[10].UD[0] = 0x8192a3b4c5d6e7f8ull;
            auto expected = initial;
            u32 rt = home == 1 ? 0 : home == 2 ? 8 : 9;
            std::vector<u32> code;
            if (home == 3)
                code.push_back(MipsR(10, 0, 9, 0, 0x2d)); // dirty scalar DADDU
            if (home == 4)
            {
                code.push_back((0x1cu << 26) | MipsR(10, 0, 9, 18, 41)); // dirty SIMD POR
                expected[9] = initial[10];
            }
            if (home == 5)
            {
                code.push_back(MipsI(13, 0, 9, 0x9abc)); // compiler constant
                expected[9].UD[0] = 0x9abc;
            }
            if (home == 6)
            {
                code.push_back(MipsR(8, 0, 8, 0, 0x2d)); // dirty aliased address
                rt = 8;
            }
            code.push_back(MipsI(left ? 44 : 45, 8, rt, alignment));
            std::array<u8, 32> model{};
            for (u32 i = 0; i < model.size(); ++i)
            {
                model[i] = static_cast<u8>(0x87 + i * 31);
                memWrite8(EEFastmemRam + 120 + i, model[i]);
            }
            u64 old = 0;
            for (u32 i = 0; i < 8; ++i)
                old |= static_cast<u64>(model[8 + i]) << (i * 8);
            const u64 merged = EeUnalignedMerge(left, alignment, old, expected[rt].UD[0]);
            for (u32 i = 0; i < 8; ++i)
                model[8 + i] = static_cast<u8>(merged >> (i * 8));
            const auto actual = RunEEProgramWithIrMode(0, code, initial.data(), cp0.data(), nullptr, nullptr);
            bool same = true;
            for (u32 i = 0; i < model.size(); ++i)
                same &= memRead8(EEFastmemRam + 120 + i) == model[i];
            bytes_ok &= same;
            const bool gprs_match = EeUnalignedGprs(actual, expected);
            if (!gprs_match)
                std::fprintf(stderr, "EE_UNALIGNED_RAM_DIFF fast=%u left=%u align=%u home=%u\n",
                    fast, left, alignment, home);
            registers_ok &= gprs_match;
            if (home == 0)
            {
                const u32 branches = EeUnalignedEqualityBranches();
                if (fast) fast_shape &= branches == 0;
                else soft_shape &= branches == 1;
            }
            ++cases;
        }
        Check(bytes_ok, "EE SDL/SDR all alignments/source homes preserve merged bytes and canaries");
        Check(registers_ok, "EE SDL/SDR zero/alias/constant/dirty scalar/SIMD preserve complete GPR128");
        Check(soft_shape, "EE SDL/SDR softmem retains full-width alignment read-skip branch");
        Check(fast_shape, "EE SDL/SDR dynamic fastmem merge has no alignment equality branch");
        std::printf("EE_UNALIGNED_RAM cases=%u\n", cases);
    }

    void EeUnalignedMmioTests()
    {
        EEFastmemRestore restore;
        Cpu = &recCpu;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableEECache = false;
        EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
        const auto handler = vtlb_RegisterHandler(nullptr, nullptr, nullptr, EeUnalignedRead64, nullptr,
            nullptr, nullptr, nullptr, EeUnalignedWrite64, nullptr);
        vtlb_MapHandler(handler, EEFastmemMmio, EEFastmemPage);
        vtlb_VMap(EEFastmemAlias, EEFastmemMmio, EEFastmemPage);
        bool side_effects = true, contexts = true, state_ok = true, fault_marked = true;
        bool replay_no_new_compiles = true;
        u32 cases = 0;
        for (bool fast : {false, true})
        for (bool left : {false, true})
        for (u32 alignment = 0; alignment < 8; ++alignment)
        for (bool delay : {false, true})
        {
            EmuConfig.Cpu.Recompiler.EnableFastmem = fast;
            auto initial = EEFastmemGprs(EEFastmemAlias + 128);
            initial[9].UD[0] = 0xf1e2d3c4b5a69788ull;
            std::vector<u32> code;
            if (delay) code.push_back(MipsI(4, 0, 0, 1));
            code.push_back(MipsI(left ? 44 : 45, 8, 9, alignment));
            code.push_back(0x1000ffffu);
            code.push_back(0);
            for (u32 i = 0; i < code.size(); ++i)
                memWrite32(EE_TEST_PC + i * 4, code[i]);
            EmuCoreXOracleSetEeIrMode(0);
            Cpu->Reset();
            s_ee_fastmem_compile_count = 0;
            EmuCoreXEEOracleSetCompileCallback(EEFastmemCompileCallback);
            const bool full = alignment == (left ? 7u : 0u);
            const u32 guest_pc = EE_TEST_PC + (delay ? 4 : 0);
            for (u32 phase = 0; phase < 3; ++phase)
            {
                constexpr u64 old = 0xa1b2c3d4e5f60718ull;
                s_ee_unaligned_trace = {};
                s_ee_unaligned_trace.value = old;
                const u64 compiles = s_ee_fastmem_compile_count;
                EEFastmemResidentExecute(initial);
                const auto& t = s_ee_unaligned_trace;
                bool effects = t.count == (full ? 1u : 2u) &&
                    t.kind[0] == (full ? 'w' : 'r') && (full || t.kind[1] == 'w');
                side_effects &= effects;
                for (u32 i = 0; i < std::min<u32>(t.count, t.kind.size()); ++i)
                    contexts &= t.address[i] == EEFastmemMmio + 128 &&
                        t.pc[i] == guest_pc && t.bd[i] == (delay ? 1u : 0u);
                state_ok &= t.value == EeUnalignedMerge(left, alignment, old, initial[9].UD[0]) &&
                    std::memcmp(cpuRegs.GPR.r, initial.data(), sizeof(cpuRegs.GPR.r)) == 0;
                if (fast) fault_marked &= vtlb_IsFaultingPC(guest_pc);
                // First replay may compile the queued helper replacement. The
                // third execution must use its already-warmed native block.
                if (phase == 2) replay_no_new_compiles &= s_ee_fastmem_compile_count == compiles;
                if (!effects)
                    std::printf("EE_UNALIGNED_MMIO_DIFF fast=%u left=%u align=%u delay=%u phase=%u calls=%u\n",
                        fast, left, alignment, delay, phase, t.count);
            }
            ++cases;
        }
        EmuCoreXEEOracleSetCompileCallback(nullptr);
        EmuCoreXOracleSetEeIrMode(-1);
        Check(side_effects, "EE SDL/SDR first-fault/replay/MMIO has exactly one merge-read and one write; full width skips read");
        Check(contexts, "EE SDL/SDR MMIO handlers retain aligned address and precise fault PC/delay slot");
        Check(state_ok, "EE SDL/SDR MMIO first fault and warmed fallback preserve source/merge and all GPR128");
        Check(fault_marked, "EE SDL/SDR first-fault sites register precise guest PC");
        Check(replay_no_new_compiles, "EE SDL/SDR warmed helper replay has no additional compilation");
        std::printf("EE_UNALIGNED_MMIO cases=%u executions=%u\n", cases, cases * 3);
    }

    struct EeUnalignedExitTrace
    {
        bool exit_read = false;
        u32 exceptions = 0;
        u32 pc = 0, bd = 0, epc = 0, cause = 0, bad_address = 0, vector_pc = 0;
    };
    EeUnalignedExitTrace s_ee_unaligned_exit;

    void EeUnalignedRaise(bool write, u32 address)
    {
        auto& t = s_ee_unaligned_exit;
        ++t.exceptions;
        t.pc = cpuRegs.pc;
        t.bd = cpuRegs.IsDelaySlot;
        // The real MMIO handler receives a physical address. Its injected
        // guest exception attributes the corresponding aligned virtual access.
        cpuRegs.CP0.n.BadVAddr = EEFastmemAlias + (address - EEFastmemMmio);
        cpuException(write ? EXC_CODE_TLBS : EXC_CODE_TLBL, t.bd);
        t.epc = cpuRegs.CP0.n.EPC;
        t.cause = cpuRegs.CP0.n.Cause;
        t.bad_address = cpuRegs.CP0.n.BadVAddr;
        t.vector_pc = cpuRegs.pc;
        recExitExecutionImmediate();
    }

    mem64_t EeUnalignedExitRead64(u32 address)
    {
        EeUnalignedRecord('r', address);
        if (s_ee_unaligned_exit.exit_read)
            EeUnalignedRaise(false, address);
        return s_ee_unaligned_trace.value;
    }

    void EeUnalignedExitWrite64(u32 address, mem64_t value)
    {
        EeUnalignedRecord('w', address);
        // Exception before committing MMIO state: a returning write after
        // this point would be an observable test failure.
        EeUnalignedRaise(true, address);
        s_ee_unaligned_trace.value = value;
    }

    void EeUnalignedNonreturningTests()
    {
        EEFastmemRestore restore;
        Cpu = &recCpu;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableEECache = false;
        EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
        const auto handler = vtlb_RegisterHandler(nullptr, nullptr, nullptr, EeUnalignedExitRead64, nullptr,
            nullptr, nullptr, nullptr, EeUnalignedExitWrite64, nullptr);
        vtlb_MapHandler(handler, EEFastmemMmio, EEFastmemPage);
        vtlb_VMap(EEFastmemAlias, EEFastmemMmio, EEFastmemPage);
        bool accesses_ok = true, exception_ok = true, architecture_ok = true, faults_ok = true;
        u32 cases = 0;
        for (bool fast : {false, true})
        for (bool left : {false, true})
        for (u32 alignment = 0; alignment < 8; ++alignment)
        for (bool delay : {false, true})
        {
            const bool full = alignment == (left ? 7u : 0u);
            for (bool read_exit : {false, true})
            {
                if (full && !read_exit)
                    continue; // Both callbacks armed: prove full-width skips read.
                EmuConfig.Cpu.Recompiler.EnableFastmem = fast;
                auto initial = EEFastmemGprs(EEFastmemAlias + 128);
                initial[9].UD[0] = 0x8192a3b4c5d6e7f8ull;
                std::vector<u32> code;
                if (delay) code.push_back(MipsI(4, 0, 0, 1));
                code.push_back(MipsI(left ? 44 : 45, 8, 9, alignment));
                code.push_back(MipsI(9, 0, 11, 0x1234)); // Must not run after fault.
                code.push_back(0x1000ffffu);
                code.push_back(0);
                for (u32 i = 0; i < code.size(); ++i)
                    memWrite32(EE_TEST_PC + i * 4, code[i]);
                EmuCoreXOracleSetEeIrMode(0);
                Cpu->Reset();
                const u32 store_pc = EE_TEST_PC + (delay ? 4u : 0u);
                const bool expect_read_exit = read_exit && !full;
                for (u32 phase = 0; phase < 2; ++phase)
                {
                    constexpr u64 old = 0xa1b2c3d4e5f60718ull;
                    s_ee_unaligned_trace = {};
                    s_ee_unaligned_trace.value = old;
                    s_ee_unaligned_exit = {};
                    s_ee_unaligned_exit.exit_read = read_exit;
                    EEFastmemResidentExecute(initial);
                    const auto& t = s_ee_unaligned_trace;
                    const auto& e = s_ee_unaligned_exit;
                    const u32 expected_calls = full || expect_read_exit ? 1u : 2u;
                    bool access = t.count == expected_calls && t.kind[0] == (full ? 'w' : 'r') &&
                        (t.count != 2 || t.kind[1] == 'w');
                    for (u32 i = 0; i < std::min<u32>(t.count, t.kind.size()); ++i)
                        access &= t.address[i] == EEFastmemMmio + 128 &&
                            t.pc[i] == store_pc && t.bd[i] == static_cast<u32>(delay);
                    const u32 cause = (expect_read_exit ? EXC_CODE_TLBL : EXC_CODE_TLBS) |
                        (delay ? 0x80000000u : 0u);
                    const bool exception = e.exceptions == 1 && e.pc == store_pc &&
                        e.bd == static_cast<u32>(delay) && e.epc == EE_TEST_PC && e.cause == cause &&
                        e.bad_address == EEFastmemAlias + 128 && e.vector_pc == 0x80000000u &&
                        cpuRegs.CP0.n.EPC == EE_TEST_PC && cpuRegs.CP0.n.Cause == cause &&
                        cpuRegs.CP0.n.BadVAddr == EEFastmemAlias + 128 &&
                        cpuRegs.CP0.n.Status.b.EXL && cpuRegs.pc == 0x80000000u && cpuRegs.IsDelaySlot == 0;
                    const bool architecture = t.value == old &&
                        std::memcmp(cpuRegs.GPR.r, initial.data(), sizeof(cpuRegs.GPR.r)) == 0;
                    accesses_ok &= access;
                    exception_ok &= exception;
                    architecture_ok &= architecture;
                    if (fast) faults_ok &= vtlb_IsFaultingPC(store_pc);
                    if (!access || !exception || !architecture)
                        std::fprintf(stderr,
                            "EE_UNALIGNED_EXIT_DIFF fast=%u left=%u align=%u delay=%u readExit=%u phase=%u calls=%u ex=%u pc=%08x bd=%u epc=%08x cause=%08x bad=%08x vector=%08x access=%u exception=%u arch=%u\n",
                            fast, left, alignment, delay, read_exit, phase, t.count, e.exceptions,
                            e.pc, e.bd, e.epc, e.cause, e.bad_address, e.vector_pc,
                            access, exception, architecture);
                }
                ++cases;
            }
        }
        EmuCoreXOracleSetEeIrMode(-1);
        Check(accesses_ok, "EE SDL/SDR nonreturning read/write MMIO has exact order/PC/BD and no later access");
        Check(exception_ok, "EE SDL/SDR nonreturning helpers preserve exact EPC Cause BadVAddr BD EXL and exception vector");
        Check(architecture_ok, "EE SDL/SDR helper exceptions skip later write/instruction and preserve complete GPR128");
        Check(faults_ok, "EE SDL/SDR nonlocal exits retain precise fault-site marking through same-entry replay");
        std::printf("EE_UNALIGNED_EXIT cases=%u executions=%u\n", cases, cases * 2);
    }
}

void EEUnalignedStoreTests()
{
    // Routed through EETests' direct_suite dispatcher AFTER its common
    // SysMemory/recCpu/microVU reservations and quiescent IOP initialization.
    // The mapping-restoration guards below require allocated VTLB tables.
    EeUnalignedRamTests();
    EeUnalignedMmioTests();
    EeUnalignedNonreturningTests();
}
