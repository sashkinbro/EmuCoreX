// SPDX-License-Identifier: GPL-3.0+
extern "C" u64 EmuCoreXOracleNormalizeGSAlpha(u64 value, bool reference);

void GSAlphaSelectorTests()
{
    u32 random = 0x753198ab;
    const auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    constexpr u64 edges[] = {0, ~0ull, 0x8000000000000000ull, 0x000000ff00000000ull,
        0xffffff00ffffff00ull, 0xaaaaaaaaaaaaaaaaull, 0x5555555555555555ull};
    bool exact = true, upper = true;
    for (u32 batch = 0; batch < 4096; ++batch)
    {
        const u64 high = (batch < std::size(edges) ? edges[batch] : (static_cast<u64>(next()) << 32) | next()) & ~0xffull;
        for (u32 low = 0; low < 256; ++low)
        {
            const u64 input = high | low;
            u64 expected = high;
            for (u32 shift = 0; shift < 8; shift += 2)
                expected |= static_cast<u64>(std::min((low >> shift) & 3u, 2u)) << shift;
            const u64 actual = EmuCoreXOracleNormalizeGSAlpha(input, false);
            exact &= actual == expected && actual == EmuCoreXOracleNormalizeGSAlpha(input, true);
            upper &= (actual & ~0xffull) == high;
        }
    }
    Check(exact, "GS ALPHA selectors match the old bitfield clamps for every combination across 1048576 raw registers");
    Check(upper, "GS ALPHA normalization preserves FIX and every reserved high bit");
    for (u32 count : {256u, 4096u, 65536u})
    {
        std::vector<u64> input(count), current(count), reference(count);
        for (u64& value : input) value = (static_cast<u64>(next()) << 32) | next();
        const u32 repeats = (1u << 20) / count;
        double samples[2][9]{};
        for (u32 round = 0; round < 9; ++round)
            for (u32 j = 0; j < 2; ++j)
            {
                const u32 old = (round + j) & 1;
                auto& output = old ? reference : current;
                const auto start = std::chrono::steady_clock::now();
                for (u32 rep = 0; rep < repeats; ++rep)
                    for (u32 i = 0; i < count; ++i)
                        output[i] = EmuCoreXOracleNormalizeGSAlpha(input[i], old != 0);
                samples[old][round] = std::chrono::duration<double, std::nano>(
                    std::chrono::steady_clock::now() - start).count() / (repeats * count);
            }
        for (auto& sample : samples) std::sort(std::begin(sample), std::end(sample));
        std::printf("GS_ALPHA_SELECTORS count=%u current_ns=%.4f reference_ns=%.4f ratio=%.4f\n",
            count, samples[0][4], samples[1][4], samples[0][4] / samples[1][4]);
        Check(current == reference, "GS ALPHA benchmark preserves every selector, FIX and reserved bit");
    }
}
