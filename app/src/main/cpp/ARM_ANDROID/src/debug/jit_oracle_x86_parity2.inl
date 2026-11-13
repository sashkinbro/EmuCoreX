// SPDX-License-Identifier: GPL-3.0+
// x86-parity oracle extension: umbrella for the additional coverage suites.
// Suites: x86-parity-ee-full, x86-parity-efu, x86-parity-regcache, x86-parity-flags, x86-parity-vu-int,
// x86-parity-vu-branch, x86-parity-vu-mem, x86-parity-vu-misc, x86-parity-cop2, x86-parity-ee-sync.
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
#include "jit_oracle_x86_parity_ee_full.inl"
#include "jit_oracle_x86_parity_efu.inl"
#include "jit_oracle_x86_parity_regcache.inl"
#include "jit_oracle_x86_parity_ref.inl"
#include "jit_oracle_x86_parity_cop2.inl"

namespace x86p
{
static void X86Parity2Tests(const std::string& s, bool all)
{
	const u32 refN = envKnob("X86P_REFN", 0);
	const auto n = [&](u32 def) { return refN ? refN : def; };
	if (all || s == "x86-parity-ee-full") { X86ParityEEFull(); }
	if (all || s == "x86-parity-efu") { X86ParityEfu(); }
	if (all || s == "x86-parity-regcache") { X86ParityRegCache(); }
	if (all || s == "x86-parity-flags") { for (int i = 0; i < 2; ++i) runFlagsSuite(i, n(250)); }
	if (all || s == "x86-parity-vu-int") { for (int i = 0; i < 2; ++i) runIntSuite(i, n(1500)); }
	if (all || s == "x86-parity-vu-mem") { for (int i = 0; i < 2; ++i) runMemSuite(i, n(150)); }
	if (all || s == "x86-parity-vu-branch") { for (int i = 0; i < 2; ++i) runBranchSuite(i, n(150)); }
	if (all || s == "x86-parity-cop2") { runCop2Suite(n(200)); }
	if (all || s == "x86-parity-ee-sync") { runEeSyncSuite(n(150)); }
	if (all || s == "x86-parity-vu-misc") { for (int i = 0; i < 2; ++i) runMiscSuite(i, n(120)); }
}
} // namespace x86p
#endif
