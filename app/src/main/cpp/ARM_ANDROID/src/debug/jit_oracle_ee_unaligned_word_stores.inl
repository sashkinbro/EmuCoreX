// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Debug native fixture: include after jit_oracle_ee_unaligned_stores.inl,
// and invoke only through the initialized EETests direct_suite dispatcher.
// It tests existing SWL/SWR; no test compiler substitutes for production.
namespace
{
    void EeUnalignedWordStoreCases()
    {
        EEFastmemRestore restore;
        Cpu = &recCpu;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableEECache = false;
        EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
        vtlb_VMap(EEFastmemAlias, EEFastmemRam, EEFastmemPage);
        std::array<u32, 32> cp0{};
        cp0[12] = 0x20000000u;
        bool bytes_ok = true, source_control = true, unrelated_ok = true, address_ok = true;
        u32 cases = 0;
        for (bool fast : {false, true})
        for (bool left : {false, true})
        for (u32 alignment = 0; alignment < 4; ++alignment)
        for (bool delay : {false, true})
        for (u32 home = 0; home < 6; ++home)
        {
            EmuConfig.Cpu.Recompiler.EnableFastmem = fast;
            auto initial = EEFastmemGprs(EEFastmemAlias + 128);
            initial[9].UD[0] = 0x8192a3b4c5d6e7f8ull;
            initial[10].UD[0] = 0x1023456789abcdefull;
            initial[14].UD[0] = EEFastmemAlias + 128;
            auto expected = initial;
            std::vector<u32> code;
            const auto por = [&](u32 from, u32 to) {
                code.push_back((0x1cu << 26) | MipsR(from, 0, to, 18, 41));
                expected[to] = initial[from];
            };
            // Source itself is allocated into a scalar host reg by SWL/SWR,
            // flushing its SIMD home before the alignment branch. That is a
            // deliberate control, not an assumption that SDL's bug repeats.
            if (home == 1 || home == 4) por(10, 9);
            // Other live dirty SIMD homes have no source-allocation flush.
            if (home == 2 || home == 4) por(13, 12);
            if (home == 3) por(14, 8);
            if (delay) code.push_back(MipsI(4, 0, 0, 1));
            const u32 rt = home == 5 ? 0u : 9u;
            code.push_back(MipsI(left ? 42 : 46, 8, rt, alignment));
            std::array<u8, 32> model{};
            for (u32 i = 0; i < model.size(); ++i)
            {
                model[i] = static_cast<u8>(0x87 + i * 31);
                memWrite8(EEFastmemRam + 120 + i, model[i]);
            }
            // Four independent byte selections, including full-width and
            // partial cases. No variable shifts/masks from the emitter.
            for (u32 byte = 0; byte < 4; ++byte)
                if ((left && byte <= alignment) || (!left && byte >= alignment))
                {
                    const u32 from = left ? byte + 3 - alignment : byte - alignment;
                    model[8 + byte] = static_cast<u8>(expected[rt].UD[0] >> (from * 8));
                }
            const auto actual = RunEEProgramWithIrMode(0, code, initial.data(), cp0.data(), nullptr, nullptr);
            bool memory = true;
            for (u32 i = 0; i < model.size(); ++i)
                memory &= memRead8(EEFastmemRam + 120 + i) == model[i];
            const bool registers = EeUnalignedGprs(actual, expected);
            bytes_ok &= memory;
            if (home == 2 || home == 4) unrelated_ok &= registers;
            else if (home == 3) address_ok &= registers;
            else source_control &= registers;
            if (!memory || !registers)
                std::fprintf(stderr,
                    "EE_UNALIGNED_WORD_DIFF fast=%u left=%u align=%u delay=%u home=%u memory=%u regs=%u\n",
                    fast, left, alignment, delay, home, memory, registers);
            ++cases;
        }
        Check(bytes_ok, "EE SWL/SWR complete byte merge and canaries across full/partial fast/soft/plain/delay");
        Check(source_control, "EE SWL/SWR memory zero and dirty-POR source controls preserve complete GPR128");
        Check(unrelated_ok, "EE SWL/SWR full-width branches preserve unrelated dirty SIMD register state");
        Check(address_ok, "EE SWL/SWR full-width branches preserve dirty SIMD address register state");
        std::printf("EE_UNALIGNED_WORD cases=%u\n", cases);
    }

    u32 s_ee_word_exit_mode = 0;

    mem32_t EeWordRead32(u32 address)
    {
        EeUnalignedRecord('r', address);
        if (s_ee_word_exit_mode == 1 || s_ee_word_exit_mode == 3)
            EeUnalignedRaise(false, address);
        const u32 value = static_cast<u32>(s_ee_unaligned_trace.value);
        s_ee_unaligned_trace.value ^= 0x10204080u;
        return value;
    }

    void EeWordWrite32(u32 address, mem32_t value)
    {
        EeUnalignedRecord('w', address);
        if (s_ee_word_exit_mode == 2 || s_ee_word_exit_mode == 3)
            EeUnalignedRaise(true, address);
        s_ee_unaligned_trace.value = value;
    }

    u32 EeWordMergedBytes(bool left, u32 alignment, u32 old, u32 source)
    {
        std::array<u8, 4> bytes{};
        for (u32 byte = 0; byte < 4; ++byte)
        {
            bytes[byte] = static_cast<u8>(old >> (byte * 8));
            if ((left && byte <= alignment) || (!left && byte >= alignment))
            {
                const u32 from = left ? byte + 3 - alignment : byte - alignment;
                bytes[byte] = static_cast<u8>(source >> (from * 8));
            }
        }
        return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
            (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24);
    }

    void EeUnalignedWordMmioCases()
    {
        EEFastmemRestore restore;
        Cpu = &recCpu;
        EmuConfig.Cpu.Recompiler.EnableEE = true;
        EmuConfig.Cpu.Recompiler.EnableEECache = false;
        EmuConfig.Cpu.Recompiler.PauseOnTLBMiss = false;
        const auto handler = vtlb_RegisterHandler(nullptr, nullptr, EeWordRead32, nullptr, nullptr,
            nullptr, nullptr, EeWordWrite32, nullptr, nullptr);
        vtlb_MapHandler(handler, EEFastmemMmio, EEFastmemPage);
        vtlb_VMap(EEFastmemAlias, EEFastmemMmio, EEFastmemPage);
        bool returning_access = true, returning_state = true, exit_access = true, exit_state = true;
        u32 cases = 0;
        for (bool fast : {false, true})
        for (bool left : {false, true})
        for (u32 alignment = 0; alignment < 4; ++alignment)
        for (bool delay : {false, true})
        for (u32 mode = 0; mode < 4; ++mode)
        {
            const bool full = alignment == (left ? 3u : 0u);
            if ((full && (mode == 1 || mode == 2)) || (!full && mode == 3))
                continue;
            EmuConfig.Cpu.Recompiler.EnableFastmem = fast;
            const auto initial = EEFastmemGprs(EEFastmemAlias + 128);
            std::vector<u32> code;
            if (delay) code.push_back(MipsI(4, 0, 0, 1));
            code.push_back(MipsI(left ? 42 : 46, 8, 9, alignment));
            if (mode) code.push_back(MipsI(9, 0, 11, 0x1234)); // Forbidden after exception.
            code.push_back(0x1000ffffu);
            code.push_back(0);
            for (u32 i = 0; i < code.size(); ++i)
                memWrite32(EE_TEST_PC + i * 4, code[i]);
            EmuCoreXOracleSetEeIrMode(0);
            Cpu->Reset();
            const u32 store_pc = EE_TEST_PC + (delay ? 4u : 0u);
            for (u32 phase = 0; phase < 2; ++phase)
            {
                constexpr u32 old = 0xdcbd9e7fu;
                s_ee_unaligned_trace = {};
                s_ee_unaligned_trace.value = old;
                s_ee_unaligned_exit = {};
                s_ee_word_exit_mode = mode;
                EEFastmemResidentExecute(initial);
                const auto& t = s_ee_unaligned_trace;
                const auto& e = s_ee_unaligned_exit;
                const bool read_exit = mode == 1;
                bool access = t.count == (full || read_exit ? 1u : 2u) &&
                    t.kind[0] == (full ? 'w' : 'r') && (t.count != 2 || t.kind[1] == 'w');
                for (u32 i = 0; i < std::min<u32>(t.count, t.kind.size()); ++i)
                    access &= t.address[i] == EEFastmemMmio + 128 &&
                        t.pc[i] == store_pc && t.bd[i] == static_cast<u32>(delay);
                bool state = std::memcmp(cpuRegs.GPR.r, initial.data(), sizeof(cpuRegs.GPR.r)) == 0;
                if (fast) access &= vtlb_IsFaultingPC(store_pc);
                if (!mode)
                {
                    state &= e.exceptions == 0 && t.value == EeWordMergedBytes(left, alignment, old,
                        static_cast<u32>(initial[9].UD[0]));
                    returning_access &= access;
                    returning_state &= state;
                }
                else
                {
                    const u32 cause = (read_exit ? EXC_CODE_TLBL : EXC_CODE_TLBS) |
                        (delay ? 0x80000000u : 0u);
                    state &= t.value == (mode == 2 ? old ^ 0x10204080u : old) &&
                        e.exceptions == 1 && e.pc == store_pc && e.bd == static_cast<u32>(delay) &&
                        e.epc == EE_TEST_PC && e.cause == cause && e.bad_address == EEFastmemAlias + 128 &&
                        e.vector_pc == 0x80000000u && cpuRegs.pc == 0x80000000u &&
                        cpuRegs.CP0.n.EPC == EE_TEST_PC && cpuRegs.CP0.n.Cause == cause &&
                        cpuRegs.CP0.n.BadVAddr == EEFastmemAlias + 128 &&
                        cpuRegs.CP0.n.Status.b.EXL && cpuRegs.IsDelaySlot == 0;
                    exit_access &= access;
                    exit_state &= state;
                }
                if (!access || !state)
                    std::fprintf(stderr,
                        "EE_WORD_MMIO_DIFF fast=%u left=%u align=%u delay=%u mode=%u phase=%u calls=%u access=%u state=%u epc=%08x cause=%08x value=%016llx\n",
                        fast, left, alignment, delay, mode, phase, t.count, access, state,
                        e.epc, e.cause, static_cast<unsigned long long>(t.value));
            }
            ++cases;
        }
        s_ee_word_exit_mode = 0;
        EmuCoreXOracleSetEeIrMode(-1);
        Check(returning_access, "EE SWL/SWR MMIO firstfault/replay preserves exact read/write count address PC and BD");
        Check(returning_state, "EE SWL/SWR returning MMIO merges independent bytes and preserves complete GPR128");
        Check(exit_access, "EE SWL/SWR nonreturning MMIO suppresses later accesses and full-width reads");
        Check(exit_state, "EE SWL/SWR helper exceptions preserve EPC Cause BadVAddr EXL BD and skip later write/instruction");
        std::printf("EE_WORD_MMIO cases=%u executions=%u\n", cases, cases * 2);
    }
}

void EEUnalignedWordStoreTests()
{
    EeUnalignedWordStoreCases();
    EeUnalignedWordMmioCases();
}
