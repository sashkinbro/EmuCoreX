// SPDX-License-Identifier: GPL-3.0+
// Exercise real microVU invalidation without allocating executable emulator memory.
extern "C" void EmuCoreXOracleClearVUQuick(microVU*, u32, u32);
extern "C" void EmuCoreXOracleSetVUQuick(microVU*, u32, microProgram*);
extern "C" void EmuCoreXOracleResetVUQuick(microVU*);

// Preserve the old full-slot walk as an independent reference and timing baseline.
__attribute__((noinline)) void VUQuickReferenceClear(microVU* vu, u32 addr, u32 size)
{
    ++vu->microMemWriteGeneration;
    for (u32 slot = 0; slot < vu->progSize / 2; ++slot)
    {
        microProgram*& prog = vu->prog.quick[slot].prog;
        if (!prog)
            continue;
        bool overlap = !prog->ranges || prog->ranges->empty();
        if (!overlap)
        {
            for (const microRange& range : *prog->ranges)
            {
                if (range.start < 0 || range.end <= range.start ||
                    (static_cast<u64>(range.start) < static_cast<u64>(addr) + size &&
                        static_cast<u64>(addr) < static_cast<u64>(range.end)))
                {
                    overlap = true;
                    break;
                }
            }
        }
        if (overlap)
            prog = nullptr;
    }
    std::memset(&vu->prog.lpState, 0, sizeof(vu->prog.lpState));
}

extern "C" void EmuCoreXOracleTestVUQuick(void (*check)(bool, const char*))
{
    auto manager = std::make_unique<microProgManager>();
    auto reference = std::make_unique<microProgManager>();
    microVU vu(*manager), ref(*reference);
    std::array<std::deque<microRange>, 7> ranges{{
        {{0x100, 0x200}}, {{0x200, 0x300}, {0x380, 0x400}},
        {{0x3ff8, 0x4000}, {0, 8}}, {}, {{-1, 8}}, {{8, 8}}, {{16, 8}}
    }};
    std::array<std::unique_ptr<microProgram>, 8> programs;
    for (u32 i = 0; i < programs.size(); ++i)
    {
        programs[i] = std::make_unique<microProgram>();
        programs[i]->ranges = i < ranges.size() ? &ranges[i] : nullptr;
    }
    for (u32 index = 0; index < 2; ++index)
    {
        vu.index = ref.index = index;
        vu.progSize = ref.progSize = (index ? 0x4000 : 0x1000) / 4;
        const u32 slots = vu.progSize / 2;
        auto reset = [&]() {
            EmuCoreXOracleResetVUQuick(&vu);
            std::memset(ref.prog.quick, 0, sizeof(ref.prog.quick));
        };
        auto set = [&](u32 slot, u32 program) {
            EmuCoreXOracleSetVUQuick(&vu, slot, programs[program].get());
            ref.prog.quick[slot].prog = programs[program].get();
        };
        auto clear = [&](u32 addr, u32 size) {
            std::memset(&vu.prog.lpState, 0xa5, sizeof(vu.prog.lpState));
            std::memset(&ref.prog.lpState, 0xa5, sizeof(ref.prog.lpState));
            EmuCoreXOracleClearVUQuick(&vu, addr, size);
            VUQuickReferenceClear(&ref, addr, size);
            std::array<bool, mProgSizeHalf> seen{};
            bool index_valid = vu.prog.activeQuickCount <= slots;
            for (u32 i = 0; index_valid && i < vu.prog.activeQuickCount; ++i)
            {
                const u32 slot = vu.prog.activeQuick[i];
                if (slot >= slots)
                {
                    index_valid = false;
                    break;
                }
                index_valid = !seen[slot] && vu.prog.quick[slot].prog;
                seen[slot] = true;
            }
            for (u32 i = 0; i < slots; ++i)
                index_valid &= seen[i] == (vu.prog.quick[i].prog != nullptr);
            return std::memcmp(vu.prog.quick, ref.prog.quick, sizeof(vu.prog.quick)) == 0 &&
                std::memcmp(&vu.prog.lpState, &ref.prog.lpState, sizeof(vu.prog.lpState)) == 0 &&
                vu.microMemWriteGeneration == ref.microMemWriteGeneration && vu.prog.cleared == ref.prog.cleared && index_valid;
        };
        reset();
        for (u32 i = 0; i < 8; ++i)
            set((i * 71) % slots, i);
        bool same = clear(0x200, 8) && clear(0x200, 8) && clear(0x80, 8);
        check(same && vu.prog.quick[0].prog == programs[0].get(),
            "microVU quick invalidation preserves disjoint programs and half-open range ends");
        same = clear(0x1f8, 8) && clear(0x3ff8, 8) && clear(0, 8);
        check(same && vu.prog.quick[0].prog == nullptr,
            "microVU quick invalidation covers range starts, multiple ranges and wrapped program coverage");
        reset();
        for (u32 i = 0; i < 8; ++i)
            set(i, i);
        check(clear(0x800, 8) && vu.prog.quick[0].prog && !vu.prog.quick[3].prog &&
            !vu.prog.quick[4].prog && !vu.prog.quick[5].prog && !vu.prog.quick[6].prog && !vu.prog.quick[7].prog,
            "microVU quick invalidation conservatively removes empty, missing and malformed coverage");

        // Repeated replacement, aliasing, removal of swapped entries and refills.
        reset();
        u32 seed = 0x9e3779b9;
        same = true;
        for (u32 i = 0; i < 6000; ++i)
        {
            seed = seed * 1664525 + 1013904223;
            if ((i % 37) == 0)
                reset();
            const u32 slot = (seed >> 11) % slots;
            set(slot, (seed >> 24) % programs.size());
            if (i % 5 == 0)
                set(slot, (seed >> 20) % programs.size());
            if (i % 3 == 0)
            {
                const std::array<u32, 9> addresses{0, 8, 0x100, 0x1ff, 0x200, 0x2ff, 0x800, 0x3ff8, 0xfffffff8u};
                same &= clear(addresses[(seed >> 16) % addresses.size()], (seed & 15));
            }
        }
        check(same, "microVU sparse quick index matches a full scan through replacements, resets and random writes");
        reset();
        for (u32 i = 0; i < slots; ++i)
        {
            set(i, 1);
            set(i, 0);
        }
        check(clear(0x800, 8) && clear(0x100, 8) && clear(0x100, 8),
            "microVU quick invalidation handles full occupancy and consecutive complete invalidations");
        reset();
        set(slots - 1, 0);
        check(clear(0x100, 8) && !vu.prog.quick[slots - 1].prog,
            "microVU quick index reuses the last slot after reset");
    }

    // Same VU1 memory and write, with zero/one/16/all occupied slots. Time real
    // Clear against the independent old scan in alternating batches.
    vu.progSize = ref.progSize = 0x1000;
    for (u32 population : {0u, 1u, 16u, 2048u})
    {
        EmuCoreXOracleResetVUQuick(&vu);
        std::memset(ref.prog.quick, 0, sizeof(ref.prog.quick));
        for (u32 i = 0; i < population; ++i)
        {
            const u32 slot = 2047 - i;
            EmuCoreXOracleSetVUQuick(&vu, slot, programs[0].get());
            ref.prog.quick[slot].prog = programs[0].get();
        }
        auto measure = [&](auto function, microVU* target) {
            const auto start = std::chrono::steady_clock::now();
            for (u32 i = 0; i < 1024; ++i)
                function(target, 0x800, 8);
            return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count()) / 1024;
        };
        std::array<double, 11> actual{}, baseline{};
        for (u32 i = 0; i < actual.size(); ++i)
        {
            if (i % 2)
            {
                baseline[i] = measure(VUQuickReferenceClear, &ref);
                actual[i] = measure(EmuCoreXOracleClearVUQuick, &vu);
            }
            else
            {
                actual[i] = measure(EmuCoreXOracleClearVUQuick, &vu);
                baseline[i] = measure(VUQuickReferenceClear, &ref);
            }
        }
        std::sort(actual.begin(), actual.end());
        std::sort(baseline.begin(), baseline.end());
        const double current_ns = actual[5], reference_ns = baseline[5];
        std::printf("VUQUICK_CLEAR occupied=%u current_median_ns=%.1f full_scan_median_ns=%.1f\n",
            population, current_ns, reference_ns);
        if (population <= 1)
            check(current_ns * 2 < reference_ns,
                "microVU sparse quick invalidation avoids scanning empty slots");
        check(std::memcmp(vu.prog.quick, ref.prog.quick, sizeof(vu.prog.quick)) == 0 &&
            vu.microMemWriteGeneration == ref.microMemWriteGeneration,
            "microVU quick invalidation benchmark preserves slots and write generations");
    }
}
