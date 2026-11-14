// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// EE coverage-gap oracle suites umbrella. Suites: ee-int-rand, ee-block-rand, ee-exc-mid-block, ee-mem-matrix, ee-cop0-tlb.
// Included from jit_oracle.cpp after jit_oracle_x86_parity.inl (needs RunEEProgram helpers, x86p::Rng, snapshots).

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
#include "jit_oracle_gap_ee_common.inl"
#include "jit_oracle_gap_ee_int.inl"
#include "jit_oracle_gap_ee_block.inl"
#include "jit_oracle_gap_ee_mem.inl"
#include "jit_oracle_gap_ee_tlb.inl"

namespace gapee
{
static bool GapEeIsSuite(const char* s)
{
	return !std::strcmp(s, "ee-int-rand") || !std::strcmp(s, "ee-block-rand") || !std::strcmp(s, "ee-exc-mid-block") || !std::strcmp(s, "ee-mem-matrix") || !std::strcmp(s, "ee-cop0-tlb");
}

static void GapEeTests(const char* s)
{
	const auto t0 = std::chrono::steady_clock::now();
	if (!std::strcmp(s, "ee-int-rand")) runIntRand();
	else if (!std::strcmp(s, "ee-block-rand")) runBlockRand();
	else if (!std::strcmp(s, "ee-exc-mid-block")) runExcMidBlock();
	else if (!std::strcmp(s, "ee-mem-matrix")) runMemMatrix();
	else if (!std::strcmp(s, "ee-cop0-tlb")) runCop0Tlb();
	std::printf("GAPEE SUMMARY suite=%s cases=%llu mismatches=%llu seed=%llu runtime=%.1fs\n", s, static_cast<unsigned long long>(g_totalCases),
		static_cast<unsigned long long>(g_totalFails), static_cast<unsigned long long>(baseSeed()),
		std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}
} // namespace gapee
#endif
