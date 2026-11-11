// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

#include "IopThread/IopPlacement.h"
#include "cpuinfo.h"

#include <algorithm>
#include <bit>

namespace
{
	enum class CoreKind { Unknown, Little, Performance };
	CoreKind ClassifyCore(u32 uarch)
	{
		switch (uarch)
		{
			case cpuinfo_uarch_cortex_a5:
			case cpuinfo_uarch_cortex_a7:
			case cpuinfo_uarch_cortex_a32:
			case cpuinfo_uarch_cortex_a35:
			case cpuinfo_uarch_cortex_a53:
			case cpuinfo_uarch_cortex_a55r0:
			case cpuinfo_uarch_cortex_a55:
			case cpuinfo_uarch_cortex_a510:
			case cpuinfo_uarch_cortex_a520:
			case cpuinfo_uarch_cortex_a320:
			case cpuinfo_uarch_lumex_c1_nano:
				return CoreKind::Little;
			case cpuinfo_uarch_cortex_a57:
			case cpuinfo_uarch_cortex_a65:
			case cpuinfo_uarch_cortex_a72:
			case cpuinfo_uarch_cortex_a73:
			case cpuinfo_uarch_cortex_a75:
			case cpuinfo_uarch_cortex_a76:
			case cpuinfo_uarch_cortex_a77:
			case cpuinfo_uarch_cortex_a78:
			case cpuinfo_uarch_cortex_a710:
			case cpuinfo_uarch_cortex_a715:
			case cpuinfo_uarch_cortex_a720:
			case cpuinfo_uarch_cortex_a725:
			case cpuinfo_uarch_cortex_x1:
			case cpuinfo_uarch_cortex_x2:
			case cpuinfo_uarch_cortex_x3:
			case cpuinfo_uarch_cortex_x4:
			case cpuinfo_uarch_cortex_x925:
			case cpuinfo_uarch_lumex_c1_ultra:
			case cpuinfo_uarch_lumex_c1_premium:
			case cpuinfo_uarch_lumex_c1_pro:
			case cpuinfo_uarch_oryon:
			case cpuinfo_uarch_oryon_v3:
			case cpuinfo_uarch_exynos_m1:
			case cpuinfo_uarch_exynos_m2:
			case cpuinfo_uarch_exynos_m3:
			case cpuinfo_uarch_exynos_m4:
			case cpuinfo_uarch_exynos_m5:
			case cpuinfo_uarch_denver:
			case cpuinfo_uarch_denver2:
			case cpuinfo_uarch_carmel:
				return CoreKind::Performance;
			default:
				return CoreKind::Unknown;
		}
	}

	bool IsUsableCore(const IopThread::CpuCapacity& cpu)
	{
		return cpu.first_thread && cpu.processor_id < 64;
	}
}

u64 IopThread::PerformanceMask(const std::vector<CpuCapacity>& cpus)
{
	u64 peak_capacity = 0, peak_frequency = 0;
	bool complete_capacity = true, complete_frequency = true, all_little = true;
	for (const CpuCapacity& cpu : cpus)
	{
		if (!IsUsableCore(cpu))
			continue;
		peak_capacity = std::max(peak_capacity, static_cast<u64>(cpu.capacity));
		peak_frequency = std::max(peak_frequency, cpu.frequency_hz);
		complete_capacity &= cpu.capacity != 0;
		complete_frequency &= cpu.frequency_hz != 0;
		all_little &= ClassifyCore(cpu.microarchitecture) == CoreKind::Little;
	}
	const bool use_capacity = complete_capacity && peak_capacity != 0;
	const bool use_frequency = !use_capacity && complete_frequency && peak_frequency != 0;
	const long double threshold = use_capacity ? peak_capacity * kPerformanceClusterRatio :
		peak_frequency * kFrequencyClusterRatio;

	u64 mask = 0;
	for (const CpuCapacity& cpu : cpus)
	{
		if (!IsUsableCore(cpu))
			continue;
		const CoreKind kind = ClassifyCore(cpu.microarchitecture);
		const bool identified = kind == CoreKind::Performance || (kind == CoreKind::Little && all_little);
		const bool inferred = kind == CoreKind::Unknown &&
			((use_capacity && cpu.capacity >= threshold) || (use_frequency && cpu.frequency_hz >= threshold));
		if (identified || inferred)
			mask |= static_cast<u64>(1) << cpu.processor_id;
	}
	return mask;
}

std::vector<u32> IopThread::OrderProcessors(const std::vector<CpuCapacity>& cpus)
{
	std::vector<CpuCapacity> cores;
	for (const CpuCapacity& cpu : cpus)
	{
		if (cpu.first_thread)
			cores.push_back(cpu);
	}
	std::stable_sort(cores.begin(), cores.end(), [](const CpuCapacity& lhs, const CpuCapacity& rhs) {
		if (lhs.capacity != rhs.capacity)
			return lhs.capacity > rhs.capacity;
		if (lhs.frequency_hz != rhs.frequency_hz)
			return lhs.frequency_hz > rhs.frequency_hz;
		return lhs.processor_id < rhs.processor_id;
	});

	std::vector<u32> order;
	order.reserve(cores.size());
	for (const CpuCapacity& core : cores)
		order.push_back(core.processor_id);
	return order;
}

IopThread::HostCpuInfo IopThread::MakeHostInfo(bool is_arm64, u32 total_cores, u64 performance_mask)
{
	HostCpuInfo host;
	host.is_arm64 = is_arm64;
	host.total_cores = total_cores;
	host.performance_cores = static_cast<u32>(std::popcount(performance_mask));
	return host;
}

bool IopThread::HasIopThreadCores(const HostCpuInfo& host, bool mtvu)
{
	const u32 workers = 3 + (mtvu ? 1 : 0);
	// One performance core stays free for the GPU driver / GS back thread. On a 4+4 SoC (Unisoc T820, MTVU on) the IOP
	// thread shared the four big cores with EE/GS/MTVU and the Mali driver, and its 500 us WFE idle watch counts as
	// runnable load; keeping IOP on the EE thread there measured NFS MW 33.9 -> 42.3 FPS, GoW2 neutral. Matches
	// PlanTier's back_thread_fits (tier >= threads_to_place + 1).
	return host.performance_cores >= workers + 1 && host.total_cores >= workers + 2;
}

IopThread::TierPlan IopThread::PlanTier(u64 performance_mask, bool mtvu, bool iop_thread)
{
	TierPlan plan;
	plan.threads_to_place = 2 + (mtvu ? 1 : 0) + (iop_thread ? 1 : 0);
	const u32 tier = static_cast<u32>(std::popcount(performance_mask));
	plan.tier_fits = performance_mask != 0 && tier >= plan.threads_to_place;
	plan.back_thread_fits = tier >= plan.threads_to_place + 1;
	return plan;
}

IopThread::RankPlan IopThread::PlanRanks(int affinity_mode, bool mtvu, bool iop_thread, size_t list_size)
{
	// Rows are {ee_rank, vu_rank, gs_rank}; the row order matches the UI order.
	//   1 EE>VU>GS   2 EE>GS>VU   3 VU>EE>GS   4 VU>GS>EE   5 GS>EE>VU   6 GS>VU>EE
	static constexpr int kRanks[6][3] = {
		{0, 1, 2},
		{0, 2, 1},
		{1, 0, 2},
		{2, 0, 1},
		{1, 2, 0},
		{2, 1, 0},
	};
	const int row = std::clamp(affinity_mode, 1, 6) - 1;

	RankPlan plan;
	plan.ee = kRanks[row][0];
	plan.vu = kRanks[row][1];
	plan.gs = kRanks[row][2];

	// With MTVU off there is no VU thread to place, so GS inherits VU's slot when VU ranked higher;
	// otherwise the faster core sits idle.
	if (!mtvu && plan.vu < plan.gs)
		plan.gs = plan.vu;

	if (iop_thread)
	{
		int rank = 0;
		while (rank == plan.ee || rank == plan.gs || (mtvu && rank == plan.vu))
			rank++;
		if (static_cast<size_t>(rank) < list_size)
			plan.iop = rank;
	}
	return plan;
}

IopThread::OrderedPlan IopThread::PlanOrdered(size_t list_size, bool mtvu, bool iop_thread)
{
	OrderedPlan plan;
	plan.ee = 0;
	plan.vu = 1;
	plan.gs = mtvu ? 2 : 1; // without MTVU the GS takes the VU thread's slot

	const u32 used = mtvu ? 3 : 2;
	plan.iop = (iop_thread && list_size > used) ? static_cast<int>(used) : -1;
	plan.first_software_thread = used + (plan.iop >= 0 ? 1 : 0);
	return plan;
}

IopThread::PrimeSplit IopThread::PrimeCoreSplit(const std::vector<CpuCapacity>& cpus)
{
	PrimeSplit split;
	const u64 tier = PerformanceMask(cpus);
	if (std::popcount(tier) < 3)
		return split;

	u32 top = 0;
	for (const CpuCapacity& cpu : cpus)
	{
		if (cpu.first_thread && cpu.processor_id < 64 && (tier >> cpu.processor_id & 1))
			top = std::max(top, cpu.capacity);
	}
	u64 at_top = 0;
	u32 second = 0;
	for (const CpuCapacity& cpu : cpus)
	{
		if (!cpu.first_thread || cpu.processor_id >= 64 || !(tier >> cpu.processor_id & 1))
			continue;
		if (cpu.capacity == top)
			at_top |= static_cast<u64>(1) << cpu.processor_id;
		else
			second = std::max(second, cpu.capacity);
	}
	if (std::popcount(at_top) != 1 || second == 0 || static_cast<double>(top) < static_cast<double>(second) * kPrimeCoreRatio)
		return split;

	split.prime = at_top;
	split.rest = tier & ~at_top;
	return split;
}

int IopThread::PrimeArbiterUpdate(PrimeArbiter& arbiter, const PrimeLoad& load)
{
	int best = -1;
	for (int i = 0; i < kPrimeContenderCount; i++)
	{
		if (load.present[i] && (best < 0 || load.busy[i] > load.busy[best]))
			best = i;
	}
	if (best < 0)
		return arbiter.owner;

	if (arbiter.owner < 0 || !load.present[arbiter.owner])
	{
		arbiter.owner = best;
		arbiter.challenger = -1;
		arbiter.streak = 0;
		return arbiter.owner;
	}

	if (best == arbiter.owner || load.busy[best] < load.busy[arbiter.owner] + kPrimeMarginPercent)
	{
		arbiter.challenger = -1;
		arbiter.streak = 0;
		return arbiter.owner;
	}

	arbiter.streak = (arbiter.challenger == best) ? arbiter.streak + 1 : 1;
	arbiter.challenger = best;
	if (arbiter.streak >= kPrimeWindows)
	{
		arbiter.owner = best;
		arbiter.challenger = -1;
		arbiter.streak = 0;
	}
	return arbiter.owner;
}
