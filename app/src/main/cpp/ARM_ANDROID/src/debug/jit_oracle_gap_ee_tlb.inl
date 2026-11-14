// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Suite ee-cop0-tlb: random guest programs that program the TLB (MTC0 Index/PageMask/EntryHi/EntryLo0/EntryLo1, TLBWI/TLBWR/TLBP/TLBR),
// access memory through the new mapping in the same block and after a remap (fastmem must see the new map), and mix in MTC0/MFC0 of
// Status and Config, CACHE operations and SYNC.L/SYNC.P. Reference = EE interpreter. Compared: all GPRs, the whole TLB array, COP0
// (except Random/Count/Compare/PERF), the scratch window and the final PC.

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)

namespace gapee
{
constexpr u32 TLBT_PA_BASE = 0x00300000u;   // 16 candidate 64K-aligned physical pages
constexpr u32 TLBT_PA_STEP = 0x00010000u;
static u32 tlbtSalt = 0;

static void tlbTestPrep(void*)
{
	tlbClearAll();
	for (u32 k = 0; k < 16; ++k)
		fillMem(TLBT_PA_BASE + k * TLBT_PA_STEP, 0x100, 20 + k + tlbtSalt);
}

struct TlbProgram
{
	std::vector<u32> code;
	GPR_reg init[32];
	u32 cp0[32];
	u32 outSlots = 0;
};

static u32 TLBINS(u32 fn) { return 0x42000000u | fn; }
static u32 CACHEOP(u32 op, u32 base, int off) { return iI(0x2f, base, op, static_cast<u32>(off)); }
static u32 SYNCI(u32 stype) { return iR(0, 0, 0, stype, 0x0f); }

static void genTlbProgram(Rng& r, TlbProgram& p)
{
	std::memset(p.init, 0, sizeof(p.init));
	std::memset(p.cp0, 0, sizeof(p.cp0));
	p.init[16].UD[0] = EE_TEST_SCRATCH;
	for (u32 reg = 17; reg < 24; ++reg) rand128(r, p.init[reg]);
	p.cp0[1] = r.below(48);   // Random, used by TLBWR
	p.cp0[6] = r.below(8);    // Wired
	std::vector<u32>& c = p.code;
	c.assign(PAD_WORDS, NOP);
	u32 slot = 0;
	const auto out = [&](u32 reg) { if (slot < 60) c.push_back(iI(0x2b, 16, reg, 4 * slot)); ++slot; };
	const auto setCp0 = [&](u32 cp0reg, u32 value) {
		emitConst64(c, 8, static_cast<u64>(static_cast<s64>(static_cast<s32>(value))), 29);
		c.push_back(MTC0(8, cp0reg));
	};
	struct Entry { bool valid; u32 idx, va, mask, pa0, pa1, flags0, flags1; bool d0, d1; u32 lo0, lo1; };
	std::vector<Entry> entries(8);
	for (u32 i = 0; i < 8; ++i) entries[i].valid = false;
	static const u32 kMasks[] = {0, 0, 0, 0x6000, 0x1e000};
	const auto pageSize = [](u32 mask) { return mask == 0 ? 0x1000u : mask == 0x6000 ? 0x4000u : 0x10000u; };
	const auto writeEntry = [&](u32 idx, bool opWr) {
		Entry e{};
		e.valid = true;
		e.idx = idx;
		e.mask = kMasks[r.below(5)];
		e.va = (opWr ? 0x50000000u + 0x01000000u * r.below(4) : 0x40000000u + 0x01000000u * idx); // TLBWR lands in an unknown slot: keep its VA unique and never access it
		e.pa0 = TLBT_PA_BASE + TLBT_PA_STEP * r.below(16);
		e.pa1 = TLBT_PA_BASE + TLBT_PA_STEP * r.below(16);
		// flags: G(1) V(2) D(4) C(0x38); mostly valid, cached/uncached, global or not
		const u32 c0 = r.below(5) == 0 ? 2u : (r.below(2) ? 3u : 7u);
		const u32 c1 = r.below(5) == 0 ? 2u : (r.below(2) ? 3u : 7u);
		e.d0 = r.below(5) != 0; e.d1 = r.below(5) != 0;
		const bool v0 = r.below(8) != 0, v1 = r.below(8) != 0;
		e.flags0 = (r.below(2) ? 1u : 0u) | (v0 ? 2u : 0u) | (e.d0 ? 4u : 0u) | (c0 << 3);
		e.flags1 = (r.below(2) ? 1u : 0u) | (v1 ? 2u : 0u) | (e.d1 ? 4u : 0u) | (c1 << 3);
		e.lo0 = ((e.pa0 >> 12) << 6) | e.flags0;
		e.lo1 = ((e.pa1 >> 12) << 6) | e.flags1;
		const u32 asid = r.below(3) == 0 ? r.below(256) : 0;
		setCp0(0, idx);
		setCp0(5, e.mask);
		setCp0(10, e.va | asid);
		setCp0(2, e.lo0);
		setCp0(3, e.lo1);
		if (opWr) setCp0(1, r.below(48) > 7 ? 8 + r.below(40) : 8); // Random is not writable on hardware but this implementation stores it
		c.push_back(TLBINS(opWr ? 0x06 : 0x02));
		// access the entry only through pages it validly maps (checked below)
		if (!opWr) entries[idx % 8] = e;
		(void)pageSize;
		return e;
	};
	const auto accessEntry = [&](const Entry& e) {
		// choose a valid page of the entry; a TLBWR may have landed in another index, so the access is simply skipped when unsure
		const bool use1 = (e.flags1 & 2) && (r.below(2) || !(e.flags0 & 2));
		if (!(e.flags0 & 2) && !(e.flags1 & 2)) return;
		const u32 pg = pageSize(e.mask);
		const u32 va = e.va + (use1 ? pg : 0) + 0x40 + 4 * r.below(16);
		emitConst64(c, 4, va, 29);
		c.push_back(iI(r.below(2) ? 0x23u : 0x37u, 4, 5, 0));
		out(5);
		// a store into a dirty page, read back through a second window
		if ((use1 ? e.d1 : e.d0) && r.below(2))
		{
			c.push_back(iI(0x2b, 4, 17 + r.below(7), 0x80));
			c.push_back(iI(0x23, 4, 6, 0x80));
			out(6);
		}
	};

	const u32 nsteps = 6 + r.below(9);
	u32 written = 0;
	for (u32 s = 0; s < nsteps; ++s)
	{
		const u32 kind = r.below(10);
		if (written == 0 || kind < 3)
		{
			const u32 idx = r.below(8);
			const bool wr = r.below(6) == 0;
			const Entry e = writeEntry(idx, wr);
			++written;
			// TLBWR picks the slot from Random: its VA/PA only count when it landed in the slot we recorded (idx), so skip accesses
			if (!wr && r.below(4) != 0) accessEntry(e);
		}
		else if (kind < 5)
		{
			// TLBP: probe an existing VA or a missing one
			const Entry& e = entries[r.below(8)];
			const u32 va = e.valid ? e.va : 0x45000000u;
			setCp0(10, va | (r.below(4) == 0 ? r.below(256) : 0));
			c.push_back(TLBINS(0x08));
			c.push_back(MFC0(9, 0));
			out(9);
		}
		else if (kind < 7)
		{
			// TLBR then read the CP0 registers back
			setCp0(0, r.below(8));
			c.push_back(TLBINS(0x01));
			for (u32 reg : {10u, 2u, 3u, 5u})
			{
				c.push_back(MFC0(9, reg));
				out(9);
			}
		}
		else if (kind < 8)
		{
			// rewrite an existing entry with a different page, then access it again in the same block
			for (u32 tries = 0; tries < 8; ++tries)
			{
				const u32 idx = r.below(8);
				if (!entries[idx].valid) continue;
				const Entry e = writeEntry(idx, false);
				accessEntry(e);
				break;
			}
		}
		else if (kind < 9)
		{
			// Status / Config round trips (no interrupt-enabling bits that could become pending)
			static const u32 kStatus[] = {0x20000000u, 0x30000000u, 0x20010000u, 0x2000ff00u, 0x20400000u & ~0x400000u, 0x70000000u};
			setCp0(12, kStatus[r.below(6)] | (r.next() & 0x0000ff00u));
			c.push_back(MFC0(9, 12));
			out(9);
			setCp0(16, r.below(4) == 0 ? r.next() & 0x7fffffffu : (r.next() & 0x00070000u) | 0x440u);
			c.push_back(MFC0(9, 16));
			out(9);
		}
		else
		{
			// CACHE / SYNC
			emitConst64(c, 4, 0x41000000u + 0x40 * r.below(8), 29);
			static const u32 kCacheOps[] = {0x00, 0x01, 0x02, 0x06, 0x07, 0x0b, 0x0e, 0x10, 0x11, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e};
			c.push_back(CACHEOP(kCacheOps[r.below(15)], 4, static_cast<int>(r.below(4)) * 16));
			c.push_back(SYNCI(r.below(2) ? 0 : 0x10));
			c.push_back(SYNCI(0));
		}
	}
	// final read-back of the TLB-visible COP0 state
	for (u32 reg : {0u, 5u, 6u, 10u, 2u, 3u})
	{
		c.push_back(MFC0(9, reg));
		out(9);
	}
	appendSentinel(c);
	p.outSlots = slot;
}

static void runCop0Tlb()
{
	const u32 n = knob("GAP_N", 1500);
	if (!onlyMatch("ee-cop0-tlb random programs")) return;
	gBegin("ee-cop0-tlb random programs");
	const auto saved = EmuConfig.Cpu.Recompiler;
	if (knob("GAP_FASTMEM", 1) == 0) EmuConfig.Cpu.Recompiler.EnableFastmem = false; // diagnostic: separates fastmem remap bugs from vtlb/TLB bugs
	for (u32 variant = 0; variant < 2; ++variant)
	{
		for (u32 k = 0; k < n / 2; ++k)
		{
			Rng r(baseSeed() * 0x9e3779b97f4a7c15ull + variant * 977ull + k * 40503ull + 123);
			TlbProgram p;
			genTlbProgram(r, p);
			tlbtSalt = k & 7;
			RunOpt ro;
			ro.prep = tlbTestPrep;
			ro.realEvents = true;
			ro.interpSteps = static_cast<u32>(p.code.size()) + 16;
			ro.jitVariant = variant ? 1 : 0; // 1: warm (the same JIT instance re-runs the compiled blocks)
			const EESnapshot ref = gapRun(false, p.code, p.init, p.cp0, ro);
			u8 refTlb[sizeof(g_lastTlb)];
			std::memcpy(refTlb, g_lastTlb, sizeof(refTlb));
			const EESnapshot got = gapRun(true, p.code, p.init, p.cp0, ro);
			gCase();
			DiffOpt d;
			d.cp0Mask = 0xffffffffu & ~((1u << 1) | (1u << 9) | (1u << 11) | (1u << 13) | (1u << 14) | (1u << 25) | (1u << 24)); // Cause: only the JIT run services the timer (IP7), the interpreter run does not
			std::vector<Dif> dd;
			snapDiff(ref, got, d, dd);
			if (dd.empty() && std::memcmp(refTlb, g_lastTlb, sizeof(refTlb)) != 0)
			{
				u32 i = 0;
				while (refTlb[i] == g_lastTlb[i]) ++i;
				dd.push_back({"TLB-array[" + std::to_string(i / static_cast<u32>(sizeof(tlbs))) + "]+" + std::to_string(i % static_cast<u32>(sizeof(tlbs))), refTlb[i], g_lastTlb[i]});
			}
			if (!dd.empty())
				gFail(diffFamily(dd[0].f).substr(0, 9) == "TLB-array" ? "TLB-array" : diffFamily(dd[0].f), std::string(variant ? "warm" : "cold") + " k=" + std::to_string(k) + " " + dd[0].f + " interp=" + hx(dd[0].a) + " jit=" + hx(dd[0].b) + " pc interp=" + hx(ref.pc) + " jit=" + hx(got.pc) + " status interp=" + hx(ref.cp0[12]) + " jit=" + hx(got.cp0[12]) + " cause interp=" + hx(ref.cp0[13]) + " jit=" + hx(got.cp0[13]) + " cp0init{random=" + hx(p.cp0[1]) + "} init{" + gprHex(p.init) + "} code{" + codeHex(p.code) + "}");
		}
	}
	EmuConfig.Cpu.Recompiler = saved;
	tlbClearAll();
	gEnd();
}

} // namespace gapee

#endif // EMUCOREX_ENABLE_NATIVE_SELF_TESTS
