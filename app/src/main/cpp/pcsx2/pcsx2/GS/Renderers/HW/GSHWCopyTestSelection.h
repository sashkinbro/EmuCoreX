// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once

// Test-only, allocation-free selection policy, also evaluated by host tests.
namespace HWSWTestSelection
{
struct Range { unsigned first = 0, count = 0; };
constexpr bool Equal(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
constexpr Range OriginalRange(const char* name)
{
    if (Equal(name, "GSSwPaletteBlockCopy.MatchesTheRasterizerOnRandomDraws")) return {1000, 800};
    if (Equal(name, "GSSwPaletteBlockCopy.MatchesTheRasterizerOnRandomDrawsFrom32BitTextures")) return {5000, 800};
    if (Equal(name, "GSSwPaletteBlockCopy.MatchesTheRasterizerOnRandomBilinearDraws")) return {9000, 1500};
    return {};
}
constexpr bool ParseDecimal(const char* text, unsigned& value)
{
    if (!text || !*text) return false;
    unsigned parsed = 0;
    for (; *text; ++text)
    {
        if (*text < '0' || *text > '9') return false;
        const unsigned digit = static_cast<unsigned>(*text - '0');
        if (parsed > (4294967295u - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
    }
    value = parsed;
    return true;
}
struct Selection
{
    bool valid = false;
    bool full_corpus = false;
    const char* test = nullptr;
    unsigned seed = 0;
    const char* error = nullptr;
    constexpr Range Cases(unsigned first, unsigned count) const
    {
        return full_corpus ? Range{first, count} : Range{seed, 1};
    }
};
constexpr Selection Parse(const char* test, const char* seed, const char* full, bool host, bool exact_test_exists)
{
    if (full && *full)
    {
        if (!host || !Equal(full, "1")) return {false, false, test, 0, "full corpus is host-only and requires EMUCOREX_HW_COPY_FULL_CORPUS=1"};
        if ((test && *test) || (seed && *seed)) return {false, false, test, 0, "full corpus cannot be combined with test or seed selectors"};
        return {true, true, nullptr, 0, nullptr};
    }
    if (!test || !*test || !exact_test_exists)
        return {false, false, test, 0, "EMUCOREX_HW_COPY_TEST must name exactly one registered test"};
    const Range range = OriginalRange(test);
    if (!range.count)
    {
        if (seed && *seed) return {false, false, test, 0, "a seed is only valid for a randomized test"};
        return {true, false, test, 0, nullptr};
    }
    unsigned parsed = 0;
    if (!ParseDecimal(seed, parsed) || parsed < range.first || parsed >= range.first + range.count)
        return {false, false, test, 0, "EMUCOREX_HW_COPY_SEED must be decimal and inside this test's original range"};
    return {true, false, test, parsed, nullptr};
}
}
