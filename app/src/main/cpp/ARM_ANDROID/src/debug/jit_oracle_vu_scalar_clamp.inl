// SPDX-License-Identifier: GPL-3.0+
extern "C" u32 EmuCoreXOracleEmitScalarClamp(u32 mode);

void VUScalarClampTests()
{
    if (!SysMemory::Allocate())
    {
        Check(false, "allocate VU scalar clamp code memory");
        return;
    }
    using Kernel = void (*)(const u32*, u32*, u32);
    VUOperandClampTests();
    VUInputFlushCodegenTests();
    VUMaxMinSelectTests();
    for (u32 mode = 0; mode < 3; ++mode)
    {
        auto compile = [&](u32 offset, bool reference, u32 unroll, u32& body_bytes, bool reload = true) {
            oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000 + offset, 0x10000);
            u8* entry = oakStartBlock();
            oakAsm->SUB(oak::util::SP, oak::util::SP, 16);
            oakAsm->STR(oak::util::X26, oak::util::SP);
            oakMoveAddressToReg(OAK_XVUCLAMP, &g_cpuRegistersPack.mVUglob);
            if (!reload)
                oakAsm->LDP(oak::QReg(0), oak::QReg(1), oak::util::X0);
            oak::Label loop;
            oakAsm->l(loop);
            body_bytes = 0;
            for (u32 operation = 0; operation < unroll; ++operation)
            {
                if (reload)
                    oakAsm->LDP(oak::QReg(0), oak::QReg(1), oak::util::X0);
                if (reference)
                {
                    for (u32 pass = 0; pass < (mode ? 2u : 1u); ++pass)
                    {
                        const oak::QReg reg = oak::QReg(pass && mode == 1 ? 1 : 0);
                        oakAsm->MOV(OAK_QSCRATCH2.B16(), reg.B16());
                        oakAsm->LDP(OAK_QSCRATCH3, OAK_QSCRATCH, OAK_XVUCLAMP,
                            oak::SOffset<11, 4>(static_cast<s64>(offsetof(mVU_Globals, minvals))));
                        oakAsm->SMIN(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), OAK_QSCRATCH.S4());
                        oakAsm->UMIN(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4());
                        oakAsm->MOV(reg.Selem()[0], OAK_QSCRATCH2.Selem()[0]);
                    }
                }
                else
                    body_bytes += EmuCoreXOracleEmitScalarClamp(mode);
                if (reload)
                    oakAsm->STP(oak::QReg(0), oak::QReg(1), oak::util::X1);
            }
            oakAsm->SUBS(oak::util::W2, oak::util::W2, 1);
            oakAsm->B(oak::Cond::NE, loop);
            if (!reload)
                oakAsm->STP(oak::QReg(0), oak::QReg(1), oak::util::X1);
            oakAsm->LDR(oak::util::X26, oak::util::SP);
            oakAsm->ADD(oak::util::SP, oak::util::SP, 16);
            oakAsm->RET();
            oakEndBlock();
            return reinterpret_cast<Kernel>(entry);
        };
        u32 bytes = 0, unused = 0;
        const Kernel current = compile(0, false, 1, bytes);
        const Kernel reference = compile(0x1000, true, 1, unused);
        Check(bytes == (mode == 1 ? 28u : 16u), "VU scalar clamp removes operand copies and reuses paired limits");
        constexpr u32 edges[] = {0u, 0x80000000u, 1u, 0x80000001u, 0x007fffffu, 0x807fffffu,
            0x00800000u, 0x80800000u, 0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
            0x7f800000u, 0xff800000u, 0x7fc12345u, 0xffc12345u, 0x7f800001u, 0xff800001u,
            0x7fffffffu, 0xffffffffu};
        u32 random = 0x13579bdf;
        bool exact = true;
        for (u32 sample = 0; sample < 262144; ++sample)
        {
            std::array<u32, 8> input{}, output{}, old{};
            for (u32& lane : input) { random = random * 1664525u + 1013904223u; lane = random; }
            if (sample < std::size(edges) * std::size(edges))
            {
                input[0] = edges[sample % std::size(edges)];
                input[4] = edges[sample / std::size(edges)];
            }
            auto expected = input;
            for (u32 lane : {0u, 4u})
                if (!lane || mode == 1)
                    if ((expected[lane] & 0x7fffffffu) > 0x7f7fffffu)
                        expected[lane] = (expected[lane] & 0x80000000u) | 0x7f7fffffu;
            current(input.data(), output.data(), 1);
            reference(input.data(), old.data(), 1);
            exact &= output == expected && old == expected;
        }
        Check(exact, "VU scalar clamp matches independent signed overflow bits and preserves other SIMD lanes including aliased operands");
        const Kernel current_loop = compile(0x2000, false, 16, unused);
        const Kernel reference_loop = compile(0x3000, true, 16, unused);
        const std::array<u32, 8> input = {0xffc12345u, 0x80000000u, 0x7f800001u, 0xffffffffu,
            0x7fc12345u, 1u, 0x7f800000u, 0x807fffffu};
        std::array<u32, 8> output{}, old{};
        std::array<double, 9> current_times{}, reference_times{};
        auto timed = [&](Kernel kernel, std::array<u32, 8>& result) {
            const auto start = std::chrono::steady_clock::now();
            kernel(input.data(), result.data(), 65536);
            return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (65536 * 16);
        };
        for (u32 batch = 0; batch < current_times.size(); ++batch)
        {
            if (batch & 1) { reference_times[batch] = timed(reference_loop, old); current_times[batch] = timed(current_loop, output); }
            else { current_times[batch] = timed(current_loop, output); reference_times[batch] = timed(reference_loop, old); }
        }
        std::sort(current_times.begin(), current_times.end());
        std::sort(reference_times.begin(), reference_times.end());
        std::printf("VUSCALARCLAMP mode=%u bytes=%u vectors=262144 current_median_ns=%.3f reference_median_ns=%.3f\n",
            mode, bytes, current_times[4], reference_times[4]);
        Check(output == old, "VU scalar clamp benchmark preserves exact output lanes");
        // Also isolate the dependent clamp chain from per-operation memory I/O.
        // Use both code positions for each version to reduce placement bias.
        const Kernel current_chain[] = {compile(0x4000, false, 16, unused, false),
            compile(0x7000, false, 16, unused, false)};
        const Kernel reference_chain[] = {compile(0x5000, true, 16, unused, false),
            compile(0x6000, true, 16, unused, false)};
        for (u32 batch = 0; batch < current_times.size(); ++batch)
        {
            const u32 position = (batch / 2) & 1;
            if (batch & 1) { reference_times[batch] = timed(reference_chain[position], old); current_times[batch] = timed(current_chain[position], output); }
            else { current_times[batch] = timed(current_chain[position], output); reference_times[batch] = timed(reference_chain[position], old); }
        }
        std::sort(current_times.begin(), current_times.end());
        std::sort(reference_times.begin(), reference_times.end());
        std::printf("VUSCALARCLAMP_CHAIN mode=%u current_median_ns=%.3f reference_median_ns=%.3f\n",
            mode, current_times[4], reference_times[4]);
        Check(output == old, "VU dependent scalar clamp chain preserves exact output lanes");
    }
}
