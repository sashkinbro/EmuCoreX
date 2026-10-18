// SPDX-License-Identifier: GPL-3.0+
// Actual microVU exit emitter coverage; requires initialized emulator memory.
extern "C" u32 EmuCoreXOracleEmitSavePQ(u32 index, int qInst, int pInst);
extern "C" u32 EmuCoreXOracleEmitLoadPQ(u32 index);
extern "C" u32 EmuCoreXOracleEmitShufflePQ(u32 q, u32 p);

void VUShufflePQTests()
{
    using Kernel = void (*)(const u32*, u32*, u32);
    for (u32 q = 0; q < 2; ++q)
    for (u32 p = 0; p < 2; ++p)
    {
        auto compile = [&](u32 offset, u32 reference, u32 unroll, u32& body_bytes) {
            oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000 + offset, 0x10000);
            u8* entry = oakStartBlock();
            oakAsm->SUB(oak::util::SP, oak::util::SP, 32);
            oakAsm->STR(oak::util::X27, oak::util::SP);
            oakAsm->STR(oak::DReg(15), oak::util::SP, oak::POffset<15, 3>(8));
            oakMoveAddressToReg(oak::util::X27, &g_cpuRegistersPack);
            oakAsm->LDR(oak::QReg(15), oak::util::X0);
            oak::Label loop;
            oakAsm->l(loop);
            body_bytes = 0;
            for (u32 operation = 0; operation < unroll; ++operation)
            {
                if (reference)
                {
                    // Mode 1 is the former branch shuffle. Mode 2 is the
                    // former exit's consecutive Q and P table permutations.
                    for (u32 pass = 0; pass < (reference == 2 ? 2u : 1u); ++pass)
                    {
                        const u32 shuffle = reference == 2 ? (pass ? 0xb4 : 0xe1) :
                            (p ? 0xb0 : 0xe0) | (q ? 1 : 4);
                        oakLoad128(OAK_QSCRATCH3, {oak::util::X27,
                            static_cast<s64>(offsetof(cpuRegistersPack, shuffle.data[shuffle][0]))});
                        oakAsm->TBL(oak::QReg(15).B16(), oak::List(oak::QReg(15).B16()), OAK_QSCRATCH3.B16());
                    }
                }
                else
                    body_bytes += EmuCoreXOracleEmitShufflePQ(q, p);
            }
            oakAsm->SUBS(oak::util::W2, oak::util::W2, 1);
            oakAsm->B(oak::Cond::NE, loop);
            oakAsm->STR(oak::QReg(15), oak::util::X1);
            oakAsm->LDR(oak::DReg(15), oak::util::SP, oak::POffset<15, 3>(8));
            oakAsm->LDR(oak::util::X27, oak::util::SP);
            oakAsm->ADD(oak::util::SP, oak::util::SP, 32);
            oakAsm->RET();
            oakEndBlock();
            return reinterpret_cast<Kernel>(entry);
        };
        u32 bytes = 0, unused = 0;
        const Kernel single = compile(0, false, 1, bytes);
        bool same = true;
        u32 random = 0x12345678;
        for (u32 sample = 0; sample < 1024; ++sample)
        {
            std::array<u32, 4> input{}, output{};
            for (u32& lane : input) { random = random * 1664525u + 1013904223u; lane = random; }
            single(input.data(), output.data(), 1);
            same &= output == std::array<u32, 4>{input[q], input[q ^ 1], input[2 + p], input[2 + (p ^ 1)]};
        }
        Check(same, "microVU P/Q branch shuffle preserves all four raw-bit lanes");
        Check(bytes == (q && p ? 4u : q || p ? 8u : 0u),
            "microVU P/Q branch shuffle combines both swaps into one instruction");
        if (!q && !p)
            continue;
        const Kernel current = compile(0x1000, false, 16, unused);
        const Kernel reference = compile(0x2000, true, 16, unused);
        const std::array<u32, 4> input = {0x80000000u, 0x7fc12345u, 0xffffffffu, 0x3f800000u};
        std::array<u32, 4> output{};
        std::array<double, 9> current_times{}, reference_times{};
        auto timed = [&](Kernel kernel) {
            const auto start = std::chrono::steady_clock::now();
            kernel(input.data(), output.data(), 65536);
            same &= output == input; // Sixteen swaps per iteration restore the input.
            return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (65536 * 16);
        };
        for (u32 batch = 0; batch < current_times.size(); ++batch)
        {
            if (batch & 1) { reference_times[batch] = timed(reference); current_times[batch] = timed(current); }
            else { current_times[batch] = timed(current); reference_times[batch] = timed(reference); }
        }
        std::sort(current_times.begin(), current_times.end());
        std::sort(reference_times.begin(), reference_times.end());
        std::printf("VUPQSHUFFLE q=%u p=%u bytes=%u current_median_ns=%.3f reference_table_median_ns=%.3f\n",
            q, p, bytes, current_times[4], reference_times[4]);
        Check(same, "microVU P/Q branch shuffle benchmark preserves exact repeated-swap results");
        if (q && p)
        {
            const Kernel old_exit = compile(0x3000, 2, 16, unused);
            std::array<double, 9> exit_times{};
            for (u32 batch = 0; batch < exit_times.size(); ++batch)
            {
                if (batch & 1) { exit_times[batch] = timed(old_exit); current_times[batch] = timed(current); }
                else { current_times[batch] = timed(current); exit_times[batch] = timed(old_exit); }
            }
            std::sort(current_times.begin(), current_times.end());
            std::sort(exit_times.begin(), exit_times.end());
            std::printf("VUPQEXITSHUFFLE current_median_ns=%.3f reference_two_tables_median_ns=%.3f\n",
                current_times[4], exit_times[4]);
            Check(same, "microVU combined P/Q exit permutation matches both original table swaps");
        }
    }
}

void VULoadPQTests()
{
    for (u32 vu = 0; vu < 2; ++vu)
    {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        oakAsm->SUB(oak::util::SP, oak::util::SP, 32);
        oakAsm->STR(oak::util::X27, oak::util::SP);
        oakAsm->STR(oak::DReg(15), oak::util::SP, oak::POffset<15, 3>(8));
        oakMoveAddressToReg(oak::util::X27, &g_cpuRegistersPack);
        // Dispatcher X0 holds the compiled entry and must survive P/Q setup.
        oakAsm->MOV(oak::util::X0, 0x123456789abcdef0ull);
        const u32 bytes = EmuCoreXOracleEmitLoadPQ(vu);
        oakAsm->STR(oak::QReg(15), oak::util::X1);
        oakAsm->STR(oak::util::X0, oak::util::X1, oak::POffset<15, 3>(16));
        oakAsm->LDR(oak::DReg(15), oak::util::SP, oak::POffset<15, 3>(8));
        oakAsm->LDR(oak::util::X27, oak::util::SP);
        oakAsm->ADD(oak::util::SP, oak::util::SP, 32);
        oakAsm->RET();
        oakEndBlock();
        bool same = true;
        for (u32 seed : {0u, 0x80000000u, 0x7fc12345u, 0xffffffffu})
        {
            VURegs& regs = vu ? VU1 : VU0;
            regs.VI[REG_Q].UL = seed;
            regs.pending_q = seed ^ 0x12345678u;
            regs.VI[REG_P].UL = seed ^ 0x9abcdef0u;
            regs.pending_p = seed ^ 0xfedcba98u;
            std::array<u32, 8> output{};
            reinterpret_cast<void (*)(void*, u32*)>(entry)(nullptr, output.data());
            same &= output[0] == regs.VI[REG_Q].UL && output[1] == regs.pending_q;
            same &= output[2] == regs.VI[REG_P].UL;
            same &= output[3] == (vu ? regs.pending_p : regs.VI[REG_P].UL);
            same &= output[4] == 0x9abcdef0u && output[5] == 0x12345678u;
        }
        Check(same, "microVU P/Q dispatcher load preserves raw bits, VU0 P duplication and entry target");
        Check(bytes == (vu ? 28u : 24u), "microVU P/Q dispatcher load uses direct scalar lane inserts");
        std::printf("VUPQLOAD core=%u native_bytes=%u\n", vu, bytes);
    }
}

void VUSavePQTests()
{
    for (u32 vu = 0; vu < 2; ++vu)
    for (u32 q = 0; q < 2; ++q)
    for (u32 p = 0; p < 2; ++p)
    {
        oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x10000);
        u8* entry = oakStartBlock();
        oakAsm->SUB(oak::util::SP, oak::util::SP, 32);
        oakAsm->STR(oak::util::X27, oak::util::SP);
        oakAsm->STR(oak::DReg(15), oak::util::SP, oak::POffset<15, 3>(8));
        oakMoveAddressToReg(oak::util::X27, &g_cpuRegistersPack);
        oakAsm->LDR(oak::QReg(15), oak::util::X0);
        const u32 bytes = EmuCoreXOracleEmitSavePQ(vu, q, p);
        oakAsm->STR(oak::QReg(15), oak::util::X1);
        oakAsm->LDR(oak::DReg(15), oak::util::SP, oak::POffset<15, 3>(8));
        oakAsm->LDR(oak::util::X27, oak::util::SP);
        oakAsm->ADD(oak::util::SP, oak::util::SP, 32);
        oakAsm->RET();
        oakEndBlock();
        bool same = true;
        for (u32 seed : {0u, 0x80000000u, 0x7fc12345u, 0xffffffffu})
        {
            const std::array<u32, 4> input = {seed, seed ^ 0x12345678u,
                seed ^ 0x9abcdef0u, seed ^ 0xfedcba98u};
            std::array<u32, 4> output{};
            VURegs& regs = vu ? VU1 : VU0;
            regs.VI[REG_P].UL = regs.pending_p = 0xdeadbeefu;
            reinterpret_cast<void (*)(const u32*, u32*)>(entry)(input.data(), output.data());
            same &= regs.VI[REG_Q].UL == input[q] && regs.pending_q == input[q ^ 1];
            same &= regs.VI[REG_P].UL == (vu ? input[2 + p] : 0xdeadbeefu);
            same &= regs.pending_p == (vu ? input[2 + (p ^ 1)] : 0xdeadbeefu);
            // Existing exits leave the selected instance in each pair's first lane.
            same &= output == std::array<u32, 4>{input[q], input[q ^ 1],
                input[vu ? 2 + p : 2], input[vu ? 2 + (p ^ 1) : 3]};
        }
        Check(same, "microVU P/Q save preserves raw bits, pending instances and outgoing SIMD layout");
        // Two scalar extracts/stores per unit. Instance selection may still shuffle once.
        Check(bytes == (vu ? 32u : 16u) + (q && vu && p ? 4u : (q || (vu && p)) ? 8u : 0u),
            "microVU P/Q save eliminates reversible table shuffles");
        std::printf("VUPQSAVE core=%u q=%u p=%u native_bytes=%u\n", vu, q, p, bytes);
    }
}

