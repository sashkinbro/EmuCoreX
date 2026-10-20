// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleMaskedFill(GSVector4i* data, u32 vectors, u32 mask, u32 color, bool reference);

void GSMaskedFillTests()
{
    u32 random = 0x76543210;
    const auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    bool exact = true, guards = true;
    for (u32 test = 0; test < 4096; ++test)
    {
        const u32 count = test % 513, offset = test % 8;
        std::vector<GSVector4i> initial(count + 16), output;
        for (auto& vector : initial)
            for (u32& word : vector.U32) word = next();
        output = initial;
        u32 mask = next(), color = next();
        if (test < 32) mask = 1u << test;
        if ((test & 63) == 32) mask = 0;
        if ((test & 63) == 33) mask = 0xffffffffu;
        if ((test & 63) == 34) color = 0;
        if ((test & 63) == 35) color = 0xffffffffu;
        EmuCoreXOracleMaskedFill(output.data() + offset, count, mask, color, false);
        for (u32 i = 0; i < initial.size(); ++i)
            for (u32 lane = 0; lane < 4; ++lane)
            {
                const bool active = i >= offset && i < offset + count;
                const u32 old = initial[i].U32[lane];
                const u32 expected = active ? (old & mask) | color : old;
                exact &= output[i].U32[lane] == expected;
                if (!active) guards &= output[i].U32[lane] == old;
            }
    }
    Check(exact, "GS masked fill preserves arbitrary color/mask bits across empty, bulk and tail lengths");
    Check(guards, "GS masked fill preserves guards at every vector offset and tail length");
    for (u32 bytes : {8192u, 32768u, 65536u, 131072u, 524288u, 2097152u})
    {
        const u32 count = bytes / 16, repeats = (32 * 1024 * 1024) / bytes;
        std::vector<GSVector4i> memory(count, GSVector4i(0x12345678));
        double samples[2][9]{};
        for (u32 round = 0; round < 9; ++round)
            for (u32 j = 0; j < 2; ++j)
            {
                const u32 reference = (round + j) & 1;
                const auto start = std::chrono::steady_clock::now();
                for (u32 rep = 0; rep < repeats; ++rep)
                    EmuCoreXOracleMaskedFill(memory.data(), count, 0xff000000u, 0x00aabbccu, reference != 0);
                samples[reference][round] = std::chrono::duration<double, std::nano>(
                    std::chrono::steady_clock::now() - start).count() / (repeats * count);
            }
        for (auto& values : samples) std::sort(std::begin(values), std::end(values));
        std::printf("GS_MASKED_FILL bytes=%u current_ns_per16=%.4f reference_ns_per16=%.4f ratio=%.4f\n",
            bytes, samples[0][4], samples[1][4], samples[0][4] / samples[1][4]);
        Check(std::all_of(memory.begin(), memory.end(), [](const GSVector4i& value) {
            return value.U32[0] == 0x12aabbccu && value.U32[1] == 0x12aabbccu &&
                value.U32[2] == 0x12aabbccu && value.U32[3] == 0x12aabbccu;
        }), "GS masked fill benchmark preserves all output bits");
    }
}
