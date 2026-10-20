// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOraclePackSTQRGBA(const GIFPackedReg* input, GSVector4i* output, u32 vertices, bool reference);

void GSSTQPackTests()
{
    u32 random = 0x576098ab;
    const auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    constexpr u32 edges[] = {0, 0x80000000u, 1, 0x80000001u, 0x007fffff, 0x807fffffu,
        0x00800000, 0x80800000u, 0x3f800000, 0x7f800000, 0xff800000u, 0x7f800001, 0xff800001u, 0x7fffffffu, 0xffffffffu};
    bool exact = true, unchanged = true, guards = true;
    for (u32 batch = 0; batch < 512; ++batch)
    {
        const u32 count = batch % 257, offset = batch % 4;
        std::vector<GIFPackedReg> input((count + 8) * 3);
        for (auto& reg : input) for (u32& word : reg.U32) word = next();
        for (u32 vertex = 0; vertex < count; ++vertex)
            if ((vertex & 1) == 0) input[(vertex + offset) * 3].U32[2] = edges[(vertex / 2 + batch) % std::size(edges)];
        const auto original = input;
        std::vector<GSVector4i> output(count + 8, GSVector4i(0x12345678)), reference = output;
        EmuCoreXOraclePackSTQRGBA(input.data() + offset * 3, output.data() + offset, count, false);
        EmuCoreXOraclePackSTQRGBA(input.data() + offset * 3, reference.data() + offset, count, true);
        unchanged &= std::memcmp(input.data(), original.data(), input.size() * sizeof(GIFPackedReg)) == 0;
        for (u32 vertex = 0; vertex < count + 8; ++vertex)
        {
            if (vertex < offset || vertex >= offset + count)
                guards &= output[vertex].eq(GSVector4i(0x12345678));
            else
            {
                const auto* regs = &input[vertex * 3];
                const u32 color = (regs[1].U32[0] & 255) | ((regs[1].U32[1] & 255) << 8) |
                    ((regs[1].U32[2] & 255) << 16) | ((regs[1].U32[3] & 255) << 24);
                const u32 q = regs[0].U32[2] ? regs[0].U32[2] : 0x00800000;
                exact &= output[vertex].eq(reference[vertex]) && output[vertex].U32[0] == regs[0].U32[0] &&
                    output[vertex].U32[1] == regs[0].U32[1] && output[vertex].U32[2] == color && output[vertex].U32[3] == q;
            }
        }
    }
    Check(exact, "GS STQ/RGBA packing matches the old SIMD path and scalar bits, including signed zero, denormals and NaNs");
    Check(unchanged, "GS STQ/RGBA packing preserves all source and padding words");
    Check(guards, "GS STQ/RGBA packing preserves output guards at empty, bulk and offset lengths");
    for (u32 count : {256u, 4096u, 65536u})
    {
        std::vector<GIFPackedReg> input(count * 3);
        for (auto& reg : input) for (u32& word : reg.U32) word = next();
        std::vector<GSVector4i> output(count), reference(count);
        const u32 repeats = (1u << 21) / count;
        double samples[2][9]{};
        for (u32 round = 0; round < 9; ++round)
            for (u32 j = 0; j < 2; ++j)
            {
                const u32 old = (round + j) & 1;
                const auto start = std::chrono::steady_clock::now();
                for (u32 rep = 0; rep < repeats; ++rep)
                    EmuCoreXOraclePackSTQRGBA(input.data(), old ? reference.data() : output.data(), count, old != 0);
                samples[old][round] = std::chrono::duration<double, std::nano>(
                    std::chrono::steady_clock::now() - start).count() / (repeats * count);
            }
        for (auto& values : samples) std::sort(std::begin(values), std::end(values));
        std::printf("GS_STQ_PACK vertices=%u current_ns=%.4f reference_ns=%.4f ratio=%.4f\n",
            count, samples[0][4], samples[1][4], samples[0][4] / samples[1][4]);
        Check(std::memcmp(output.data(), reference.data(), count * sizeof(GSVector4i)) == 0,
            "GS STQ/RGBA benchmark preserves every packed vertex bit");
    }
}
