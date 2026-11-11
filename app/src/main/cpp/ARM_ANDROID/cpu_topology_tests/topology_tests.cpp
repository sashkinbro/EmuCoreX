// SPDX-FileCopyrightText: 2026 EmuCoreX Contributors
// SPDX-License-Identifier: GPL-3.0+

#include "IopThread/IopPlacement.h"
#include "cpuinfo.h"

#include <cstdio>
#include <vector>

using namespace IopThread;

static int checks = 0, failures = 0;
static void Check(bool result, const char* name)
{
    ++checks;
    if (!result)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
}

static std::vector<CpuCapacity> Cores(std::initializer_list<u32> capacities, u32 uarch = cpuinfo_uarch_unknown)
{
    std::vector<CpuCapacity> cpus;
    for (u32 capacity : capacities)
        cpus.push_back({static_cast<u32>(cpus.size()), capacity, true, 0, uarch});
    return cpus;
}

static bool IopAllowed(const std::vector<CpuCapacity>& cpus, bool mtvu = true)
{
    return HasIopThreadCores(MakeHostInfo(true, static_cast<u32>(cpus.size()), PerformanceMask(cpus)), mtvu);
}

int main()
{
    auto elite5 = Cores({762, 762, 762, 762, 762, 762, 1024, 1024}, cpuinfo_uarch_oryon_v3);
    Check(PerformanceMask(elite5) == 0xff, "8 Elite Gen 5 keeps all eight Oryon cores");
    Check(IopAllowed(elite5), "8 Elite Gen 5 enables IOP alongside MTVU");
    for (auto& cpu : elite5) cpu.microarchitecture = cpuinfo_uarch_unknown;
    Check(PerformanceMask(elite5) == 0xff, "capacity fallback also recognizes an unidentified SM8850");
    Check(PlanTier(PerformanceMask(elite5), true, true).tier_fits, "affinity and IOP share the expanded tier");

    auto elite = Cores({700, 700, 700, 700, 700, 700, 1024, 1024}, cpuinfo_uarch_oryon);
    Check(IopAllowed(elite), "8 Elite performance cores remain eligible below the former 75 percent cutoff");
    for (auto& cpu : elite) cpu.capacity = 0;
    Check(IopAllowed(elite), "known Oryon core types survive unavailable scheduler capacities");

    auto dimensity = Cores({560, 560, 560, 560, 820, 820, 820, 1024});
    Check(PerformanceMask(dimensity) == 0xff, "all-big Dimensity capacity topology includes the slower big cluster");
    Check(IopAllowed(dimensity), "all-big Dimensity can run IOP and MTVU");
    for (auto& cpu : dimensity) cpu.microarchitecture = cpuinfo_uarch_cortex_a720;
    dimensity[7].microarchitecture = cpuinfo_uarch_cortex_x4;
    for (auto& cpu : dimensity) cpu.capacity = 0;
    Check(IopAllowed(dimensity), "known Cortex-A720/X4 topology does not need readable capacity files");

    auto flagship = Cores({300, 300, 300, 300, 800, 800, 800, 1024});
    Check(PerformanceMask(flagship) == 0xf0, "unidentified 1+3+4 topology still excludes the little cluster");
    Check(!IopAllowed(flagship) && IopAllowed(flagship, false), "four performance cores leave no spare for the GPU driver with MTVU");
    for (u32 i = 0; i < flagship.size(); ++i)
    {
        flagship[i].microarchitecture = i < 4 ? cpuinfo_uarch_cortex_a520 : cpuinfo_uarch_cortex_a720;
        flagship[i].capacity = 1024;
    }
    Check(PerformanceMask(flagship) == 0xf0, "core types exclude little cores even when the kernel reports identical capacities");
    for (u32 i = 0; i < 4; ++i) flagship[i].capacity = 700;
    Check(PerformanceMask(flagship) == 0xf0, "a high-rated A520 is never promoted to a big core");

    auto rg556 = Cores({336, 336, 336, 336, 869, 869, 869, 1024});
    Check(PerformanceMask(rg556) == 0xf0 && !IopAllowed(rg556), "RG556 keeps its four-core tier but rejects IOP with MTVU");
    auto tablet = Cores({379, 867, 867, 867, 867, 1024});
    Check(PerformanceMask(tablet) == 0x3e && IopAllowed(tablet), "six-core tablet retains its five-core performance tier");

    auto two_big = Cores({650, 650, 650, 650, 650, 650, 1024, 1024});
    for (u32 i = 0; i < two_big.size(); ++i)
        two_big[i].microarchitecture = i < 6 ? cpuinfo_uarch_cortex_a55 : cpuinfo_uarch_cortex_a75;
    Check(PerformanceMask(two_big) == 0xc0, "known 2+6 budget CPU keeps only the two big cores");
    Check(!IopAllowed(two_big) && !IopAllowed(two_big, false), "two big cores cannot run threaded IOP, even without MTVU");

    auto a523 = Cores({1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024}, cpuinfo_uarch_cortex_a55);
    Check(PerformanceMask(a523) == 0xff && IopAllowed(a523), "homogeneous Allwinner A523 keeps all eight A55 cores");
    for (auto& cpu : a523) cpu.capacity = 0;
    Check(IopAllowed(a523), "a known homogeneous A55 CPU remains usable without sysfs readings");
    auto four = Cores({1024, 1024, 1024, 1024}, cpuinfo_uarch_cortex_a55);
    Check(!IopAllowed(four) && !IopAllowed(four, false), "quad-core CPU still reserves room for OS and audio");

    auto missing = Cores({0, 0, 0, 0, 0, 0, 0, 0});
    Check(PerformanceMask(missing) == 0 && !IopAllowed(missing), "completely unknown topology fails closed");
    for (u32 i = 0; i < missing.size(); ++i)
        missing[i].frequency_hz = i < 4 ? 1800000000ull : 2800000000ull;
    Check(PerformanceMask(missing) == 0xf0 && !IopAllowed(missing), "complete maximum-frequency data is a fallback (four big cores reject IOP with MTVU)");
    missing[0].capacity = 1024;
    Check(PerformanceMask(missing) == 0xf0, "partial capacities are never mixed with frequency units");
    missing[0].frequency_hz = 0;
    Check(PerformanceMask(missing) == 0, "unidentified incomplete capacity and frequency data cannot invent a tier");

    auto sparse = Cores({700, 700, 1024});
    sparse[0].processor_id = 1;
    sparse[1].processor_id = 5;
    sparse[2].processor_id = 9;
    Check(PerformanceMask(sparse) == 0x222, "mask uses real OS processor IDs rather than array indices");
    sparse.push_back({64, 99999, true, 0, cpuinfo_uarch_unknown});
    sparse.push_back({10, 99999, false, 0, cpuinfo_uarch_unknown});
    Check(PerformanceMask(sparse) == 0x222, "out-of-range IDs and SMT siblings cannot distort the threshold");
    Check(PerformanceMask({}) == 0, "empty topology has no performance mask");
    Check(!HasIopThreadCores(MakeHostInfo(true, 8, 0x07), true), "three performance cores reject IOP with MTVU");
    Check(!HasIopThreadCores(MakeHostInfo(true, 8, 0x07), false), "three performance cores reject IOP without MTVU");
    Check(!HasIopThreadCores(MakeHostInfo(true, 8, 0x0f), true), "4 perf cores with MTVU reject IOP");
    Check(HasIopThreadCores(MakeHostInfo(true, 8, 0x0f), false), "4 perf cores without MTVU allow IOP");
    Check(HasIopThreadCores(MakeHostInfo(true, 8, 0x1f), true), "5 perf cores with MTVU allow IOP (Tensor G3 style)");
    Check(!HasIopThreadCores(MakeHostInfo(true, 5, 0x1f), true), "five total cores reject four workers plus two spare cores");
    Check(HasIopThreadCores(MakeHostInfo(true, 5, 0x1f), false), "five total cores can support three workers plus two spare cores");

    std::printf("CPU topology: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
