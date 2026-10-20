// SPDX-License-Identifier: GPL-3.0+
extern "C" u32 EmuCoreXOracleEmitMaxMinSelect(bool maximum, bool scalar, bool alias);

void VUMaxMinSelectTests()
{
    using Kernel = void (*)(const u32*, u32*, u32);
    constexpr u32 count = 4096;
    std::vector<u32> input(count * 8), output(count * 8);
    constexpr u32 edges[] = {0u, 0x80000000u, 1u, 0x80000001u, 0x007fffffu, 0x807fffffu,
        0x00800000u, 0x80800000u, 0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu,
        0x7f800000u, 0xff800000u, 0x7f800001u, 0xff800001u, 0x7fffffffu, 0xffffffffu};
    u32 random = 0x13579bdf;
    const auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    for (u32 i = 0; i < count; ++i)
        for (u32 lane = 0; lane < 4; ++lane)
        {
            const u32 a = i < 512 ? edges[(i + lane) % std::size(edges)] : next();
            u32 b = i < 512 ? edges[((i / std::size(edges)) + lane) % std::size(edges)] : next();
            if (i >= 512 && (i & 7) < 3)
                b = (i & 7) == 0 ? a : ((i & 7) == 1 ? a ^ 0x80000000u : a + 1);
            input[i * 8 + lane] = a;
            input[i * 8 + lane + 4] = b;
        }
    for (bool maximum : {false, true})
        for (bool scalar : {false, true})
            for (bool alias : {false, true})
            {
                oakSetAsmPtr(SysMemory::GetEERec() + 0x2000000, 0x1000);
                u8* entry = oakStartBlock();
                oak::Label loop;
                oakAsm->l(loop);
                oakAsm->LDP(oak::util::Q0, oak::util::Q1, oak::util::X0);
                const u32 bytes = EmuCoreXOracleEmitMaxMinSelect(maximum, scalar, alias);
                oakAsm->STP(oak::util::Q0, oak::util::Q1, oak::util::X1);
                oakAsm->ADD(oak::util::X0, oak::util::X0, 32);
                oakAsm->ADD(oak::util::X1, oak::util::X1, 32);
                oakAsm->SUBS(oak::util::W2, oak::util::W2, 1);
                oakAsm->B(oak::Cond::NE, loop);
                oakAsm->RET();
                oakEndBlock();
                Check(bytes == 20, "VU MAX/MINI raw selection emits five integer SIMD instructions");
                reinterpret_cast<Kernel>(entry)(input.data(), output.data(), count);
                bool exact = true, source_preserved = true;
                for (u32 i = 0; i < count; ++i)
                    for (u32 lane = 0; lane < 4; ++lane)
                    {
                        const u32 a = input[i * 8 + lane];
                        const u32 b = input[i * 8 + lane + (alias ? 0 : 4)];
                        // Independent sign/magnitude ordering, including nonfinite bit patterns.
                        const bool greater = ((a ^ b) >> 31) ? (a >> 31) == 0 :
                            ((a >> 31) ? a < b : a > b);
                        const u32 expected = greater == maximum ? a : b;
                        exact &= output[i * 8 + lane] == expected;
                        source_preserved &= output[i * 8 + lane + 4] == input[i * 8 + lane + 4];
                    }
                Check(exact, "VU MAX/MINI scalar/vector and aliased selectors preserve exact edge/random operand bits");
                Check(source_preserved, "VU MAX/MINI selection preserves the read-only source register");
            }
}
