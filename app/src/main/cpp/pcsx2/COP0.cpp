// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "COP0.h"

// Updates the CPU's mode of operation (either, Kernel, Supervisor, or User modes).
// Currently the different modes are not implemented.
// Given this function is called so much, it's commented out for now. (rama)
__ri void cpuUpdateOperationMode()
{

	//u32 value = cpuRegs.CP0.n.Status.val;

	//if (value & 0x06 ||
	//	(value & 0x18) == 0) { // Kernel Mode (KSU = 0 | EXL = 1 | ERL = 1)*/
	//	memSetKernelMode();	// Kernel memory always
	//} else { // User Mode
	//	memSetUserMode();
	//}
}

void WriteCP0Status(u32 value)
{
	COP0_UpdatePCCR();
	cpuRegs.CP0.n.Status.val = value;
	cpuSetNextEventDelta(4);
}

void WriteCP0Config(u32 value)
{
	// Protect the read-only ICacheSize (IC) and DataCacheSize (DC) bits
	cpuRegs.CP0.n.Config = value & ~0xFC0;
	cpuRegs.CP0.n.Config |= 0x440;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Performance Counters Update Stuff!
//
// Note regarding updates of PERF and TIMR registers: never allow increment to be 0.
// That happens when a game loads the MFC0 twice in the same recompiled block (before the
// cpuRegs.cycles update), and can cause games to lock up since it's an unexpected result.
//
// PERF Overflow exceptions:  The exception is raised when the MSB of the Performance
// Counter Register is set.  I'm assuming the exception continues to re-raise until the
// app clears the bit manually (needs testing).
//
// PERF Events:
//  * Event 0 on PCR 0 is unused (counter disable)
//  * Event 16 is usable as a specific counter disable bit (since CTE affects both counters)
//  * Events 17-31 are reserved (act as counter disable)
//
// Most event mode aren't supported, and issue a warning and do a standard instruction
// count.  But only mode 1 (instruction counter) has been found to be used by games thus far.
//

static __fi bool PERF_ShouldCountEvent(uint evt)
{
	switch (evt)
	{
			// This is a rough table of actions for various PCR modes.  Some of these
			// can be implemented more accurately later.  Others (WBBs in particular)
			// probably cannot without some severe complications.

			// left sides are PCR0 / right sides are PCR1

		case 1: // cpu cycle counter.
		case 2: // single/dual instruction issued
		case 3: // Branch issued / Branch mispredicated
			return true;

		case 4: // BTAC/TLB miss
		case 5: // ITLB/DTLB miss
		case 6: // Data/Instruction cache miss
			return false;

		case 7: // Access to DTLB / WBB single request fail
		case 8: // Non-blocking load / WBB burst request fail
		case 9:
		case 10:
			return false;

		case 11: // CPU address bus busy / CPU data bus busy
			return false;

		case 12: // Instruction completed
		case 13: // non-delayslot instruction completed
		case 14: // COP2/COP1 instruction complete
		case 15: // Load/Store completed
			return true;
	}

	return false;
}

// Diagnostics for event modes that we just ignore for now.  Using these perf units could
// cause compat issues in some very odd/rare games, so if this msg comes up who knows,
// might save some debugging effort. :)
void COP0_DiagnosticPCCR()
{
	if (cpuRegs.PERF.n.pccr.b.Event0 >= 7 && cpuRegs.PERF.n.pccr.b.Event0 <= 10)
		Console.Warning("PERF/PCR0 Unsupported Update Event Mode = 0x%x", cpuRegs.PERF.n.pccr.b.Event0);

	if (cpuRegs.PERF.n.pccr.b.Event1 >= 7 && cpuRegs.PERF.n.pccr.b.Event1 <= 10)
		Console.Warning("PERF/PCR1 Unsupported Update Event Mode = 0x%x", cpuRegs.PERF.n.pccr.b.Event1);
}
extern int branch;
__fi void COP0_UpdatePCCR()
{
	// Counting and counter exceptions are not performed if we are currently executing a Level 2 exception (ERL)
	// or the counting function is not enabled (CTE)
	if (cpuRegs.CP0.n.Status.b.ERL || !cpuRegs.PERF.n.pccr.b.CTE)
	{
		cpuRegs.lastPERFCycle[0] = cpuRegs.cycle;
		cpuRegs.lastPERFCycle[1] = cpuRegs.lastPERFCycle[0];
		return;
	}

	// Implemented memory mode check (kernel/super/user)

	if (cpuRegs.PERF.n.pccr.val & ((1 << (cpuRegs.CP0.n.Status.b.KSU + 2)) | (cpuRegs.CP0.n.Status.b.EXL << 1)))
	{
		// ----------------------------------
		//    Update Performance Counter 0
		// ----------------------------------

		if (PERF_ShouldCountEvent(cpuRegs.PERF.n.pccr.b.Event0))
		{
			u32 incr = cpuRegs.cycle - cpuRegs.lastPERFCycle[0];
			if (incr == 0)
				incr++;

			// use prev/XOR method for one-time exceptions (but likely less correct)
			//u32 prev = cpuRegs.PERF.n.pcr0;
			cpuRegs.PERF.n.pcr0 += incr;
			//DevCon.Warning("PCR VAL %x", cpuRegs.PERF.n.pccr.val);
			//prev ^= (1UL<<31);		// XOR is fun!
			//if( (prev & cpuRegs.PERF.n.pcr0) & (1UL<<31) )
			if ((cpuRegs.PERF.n.pcr0 & 0x80000000))
			{
				// TODO: Vector to the appropriate exception here.
				// This code *should* be correct, but is untested (and other parts of the emu are
				// not prepared to handle proper Level 2 exception vectors yet)

				//branch == 1 is probably not the best way to check for the delay slot, but it beats nothing! (Refraction)
				/*	if( branch == 1 )
				{
					cpuRegs.CP0.n.ErrorEPC = cpuRegs.pc - 4;
					cpuRegs.CP0.n.Cause |= 0x40000000;
				}
				else
				{
					cpuRegs.CP0.n.ErrorEPC = cpuRegs.pc;
					cpuRegs.CP0.n.Cause &= ~0x40000000;
				}

				if( cpuRegs.CP0.n.Status.b.DEV )
				{
					// Bootstrap vector
					cpuRegs.pc = 0xbfc00280;
				}
				else
				{
					cpuRegs.pc = 0x80000080;
				}
				cpuRegs.CP0.n.Status.b.ERL = 1;
				cpuRegs.CP0.n.Cause |= 0x20000;*/
			}
		}
	}

	if (cpuRegs.PERF.n.pccr.val & ((1 << (cpuRegs.CP0.n.Status.b.KSU + 12)) | (cpuRegs.CP0.n.Status.b.EXL << 11)))
	{
		// ----------------------------------
		//    Update Performance Counter 1
		// ----------------------------------

		if (PERF_ShouldCountEvent(cpuRegs.PERF.n.pccr.b.Event1))
		{
			u32 incr = cpuRegs.cycle - cpuRegs.lastPERFCycle[1];
			if (incr == 0)
				incr++;

			cpuRegs.PERF.n.pcr1 += incr;

			if ((cpuRegs.PERF.n.pcr1 & 0x80000000))
			{
				// TODO: Vector to the appropriate exception here.
				// This code *should* be correct, but is untested (and other parts of the emu are
				// not prepared to handle proper Level 2 exception vectors yet)

				//branch == 1 is probably not the best way to check for the delay slot, but it beats nothing! (Refraction)

				/*if( branch == 1 )
				{
					cpuRegs.CP0.n.ErrorEPC = cpuRegs.pc - 4;
					cpuRegs.CP0.n.Cause |= 0x40000000;
				}
				else
				{
					cpuRegs.CP0.n.ErrorEPC = cpuRegs.pc;
					cpuRegs.CP0.n.Cause &= ~0x40000000;
				}

				if( cpuRegs.CP0.n.Status.b.DEV )
				{
					// Bootstrap vector
					cpuRegs.pc = 0xbfc00280;
				}
				else
				{
					cpuRegs.pc = 0x80000080;
				}
				cpuRegs.CP0.n.Status.b.ERL = 1;
				cpuRegs.CP0.n.Cause |= 0x20000;*/
			}
		}
	}
	cpuRegs.lastPERFCycle[0] = cpuRegs.cycle;
	cpuRegs.lastPERFCycle[1] = cpuRegs.cycle;
}

//////////////////////////////////////////////////////////////////////////////////////////
//

static bool TryMapScratchpadTlbEntry(const tlbs& entry)
{
	// According to the manual:
	// 'It [SPR] must be mapped into a contiguous 16 KB of virtual address space that is
	// aligned on a 16KB boundary. Results are not guaranteed if this restriction is not followed.'
	// Assume the game maps it sanely and bind directly to eeMem->Scratch.
	if (!entry.isSPR())
		return false;

	if (entry.VPN2() != 0x70000000)
		Console.Warning("COP0: Mapping Scratchpad to non-default address 0x%08X", entry.VPN2());

	vtlb_VMapBuffer(entry.VPN2(), eeMem->Scratch, Ps2MemSize::Scratch);
	return true;
}

static bool TryUnmapScratchpadTlbEntry(const tlbs& entry)
{
	if (!entry.isSPR())
		return false;

	vtlb_VMapUnmap(entry.VPN2(), Ps2MemSize::Scratch);
	return true;
}

static bool GetTlbPageRange(const tlbs& entry, bool upper_half, u32* mask, u32* start, u32* end)
{
	const bool valid = upper_half ? entry.EntryLo1.V : entry.EntryLo0.V;
	if (!valid)
		return false;

	*mask = ((~entry.Mask()) << 1) & 0xfffff;
	*start = (entry.VPN2() >> 12) + (upper_half ? (entry.Mask() + 1) : 0);
	*end = *start + entry.Mask() + 1;
	return true;
}

static void MapPhysicalTlbPageRange(const tlbs& entry, bool upper_half)
{
	u32 mask, start, end;
	if (!GetTlbPageRange(entry, upper_half, &mask, &start, &end))
		return;

	const u32 vpn2_page = entry.VPN2() >> 12;
	const u32 base_pfn = upper_half ? entry.PFN1() : entry.PFN0();
	for (u32 addr = start; addr < end; addr++)
	{
		if ((addr & mask) == (vpn2_page & mask))
		{
			memSetPageAddr(addr << 12, base_pfn + ((addr - start) << 12));
			Cpu->Clear(addr << 12, 0x400);
		}
	}
}

static void UnmapPhysicalTlbPageRange(const tlbs& entry, bool upper_half)
{
	u32 mask, start, end;
	if (!GetTlbPageRange(entry, upper_half, &mask, &start, &end))
		return;

	const u32 vpn2_page = entry.VPN2() >> 12;
	for (u32 addr = start; addr < end; addr++)
	{
		if ((addr & mask) == (vpn2_page & mask))
		{
			memClearPageAddr(addr << 12);
			Cpu->Clear(addr << 12, 0x400);
		}
	}
}

void MapTLB(const tlbs& t, int i)
{
	COP0_LOG("MAP TLB %d: 0x%08X-> [0x%08X 0x%08X] S=%d G=%d ASID=%d Mask=0x%03X EntryLo0 PFN=%x EntryLo0 Cache=%x EntryLo1 PFN=%x EntryLo1 Cache=%x VPN2=%x",
		i, t.VPN2(), t.PFN0(), t.PFN1(), t.isSPR() >> 31, t.isGlobal(), t.EntryHi.ASID,
		t.Mask(), t.EntryLo0.PFN, t.EntryLo0.C, t.EntryLo1.PFN, t.EntryLo1.C, t.VPN2());

	if (TryMapScratchpadTlbEntry(t))
		return;

	MapPhysicalTlbPageRange(t, false);
	MapPhysicalTlbPageRange(t, true);
}

__inline u32 ConvertPageMask(const u32 PageMask)
{
	const u32 mask = std::popcount(PageMask >> 13);

	pxAssertMsg(!((mask & 1) || mask > 12), "Invalid page mask for this TLB entry. EE cache doesn't know what to do here.");

	return (1 << (12 + mask)) - 1;
}

static void RemoveCachedTlbEntry(const tlbs& entry)
{
	const u32 page_mask = ConvertPageMask(entry.PageMask.UL);
	for (size_t i = 0; i < cachedTlbs.count; i++)
	{
		if (cachedTlbs.PFN0s[i] == entry.PFN0() && cachedTlbs.PFN1s[i] == entry.PFN1() && cachedTlbs.PageMasks[i] == page_mask)
		{
			for (size_t j = i; j < cachedTlbs.count - 1; j++)
			{
				cachedTlbs.CacheEnabled0[j] = cachedTlbs.CacheEnabled0[j + 1];
				cachedTlbs.CacheEnabled1[j] = cachedTlbs.CacheEnabled1[j + 1];
				cachedTlbs.PFN0s[j] = cachedTlbs.PFN0s[j + 1];
				cachedTlbs.PFN1s[j] = cachedTlbs.PFN1s[j + 1];
				cachedTlbs.PageMasks[j] = cachedTlbs.PageMasks[j + 1];
			}
			cachedTlbs.count--;
			break;
		}
	}
}

static void AppendCachedTlbEntry(const tlbs& entry)
{
	const size_t idx = cachedTlbs.count;
	cachedTlbs.CacheEnabled0[idx] = entry.EntryLo0.isCached() ? ~0 : 0;
	cachedTlbs.CacheEnabled1[idx] = entry.EntryLo1.isCached() ? ~0 : 0;
	cachedTlbs.PFN1s[idx] = entry.PFN1();
	cachedTlbs.PFN0s[idx] = entry.PFN0();
	cachedTlbs.PageMasks[idx] = ConvertPageMask(entry.PageMask.UL);
	cachedTlbs.count++;
}

static void RunPostRestoreTlbFixups(bool apply_goemon_fix)
{
	if (apply_goemon_fix)
		GoemonPreloadTlb();
}

static void ClearSingleTlbMappingFromSnapshot(const tlbs& previous)
{
	ClearTLBMappingsFromSnapshot(&previous, 1);
}

static void RewriteTlbEntryFromCop0State(int index)
{
	ClearSingleTlbMappingFromSnapshot(tlb[index]);
	WriteTLB(index);
}

void UnmapTLB(const tlbs& t, int i)
{
	//Console.WriteLn("Clear TLB %d: %08x-> [%08x %08x] S=%d G=%d ASID=%d Mask= %03X", i,t.VPN2,t.PFN0,t.PFN1,t.S,t.G,t.ASID,t.Mask);
	if (TryUnmapScratchpadTlbEntry(t))
		return;

	UnmapPhysicalTlbPageRange(t, false);
	UnmapPhysicalTlbPageRange(t, true);

	RemoveCachedTlbEntry(t);
}

static void RestoreSingleTlbEntryMappingFromSnapshot(const tlbs& previous, int index)
{
	UnmapTLB(previous, index);
	MapTLB(tlb[index], index);
}

static bool ShouldRestoreTlbEntryMappingFromSnapshot(const tlbs& previous, size_t index, bool only_changed_entries)
{
	return !only_changed_entries || std::memcmp(&previous, &tlb[index], sizeof(tlbs)) != 0;
}

void RestoreTLBMappingsFromSnapshot(const tlbs* previous, size_t count, bool only_changed_entries, bool apply_goemon_fix)
{
	const size_t restore_count = std::min(count, std::size(tlb));
	for (size_t i = 0; i < restore_count; i++)
	{
		if (ShouldRestoreTlbEntryMappingFromSnapshot(previous[i], i, only_changed_entries))
			RestoreSingleTlbEntryMappingFromSnapshot(previous[i], static_cast<int>(i));
	}

	RunPostRestoreTlbFixups(apply_goemon_fix);
}

void ClearTLBMappingsFromSnapshot(const tlbs* entries, size_t count)
{
	const size_t clear_count = std::min(count, std::size(tlb));
	for (size_t i = 0; i < clear_count; i++)
		UnmapTLB(entries[i], static_cast<int>(i));
}

void WriteTLB(int i)
{
	tlb[i].PageMask.UL = cpuRegs.CP0.n.PageMask;
	tlb[i].EntryHi.UL = cpuRegs.CP0.n.EntryHi;
	tlb[i].EntryLo0.UL = cpuRegs.CP0.n.EntryLo0;
	tlb[i].EntryLo1.UL = cpuRegs.CP0.n.EntryLo1;

	// Setting the cache mode to reserved values is vaguely defined in the manual.
	// I found that SPR is set to cached regardless.
	// Non-SPR entries default to uncached on reserved cache modes.
	if (tlb[i].isSPR())
	{
		tlb[i].EntryLo0.C = 3;
		tlb[i].EntryLo1.C = 3;
	}
	else
	{
		if (!tlb[i].EntryLo0.isValidCacheMode())
			tlb[i].EntryLo0.C = 2;
		if (!tlb[i].EntryLo1.isValidCacheMode())
			tlb[i].EntryLo1.C = 2;
	}

	if (!tlb[i].isSPR() && ((tlb[i].EntryLo0.V && tlb[i].EntryLo0.isCached()) || (tlb[i].EntryLo1.V && tlb[i].EntryLo1.isCached())))
		AppendCachedTlbEntry(tlb[i]);

	MapTLB(tlb[i], i);
}

namespace R5900 {
namespace Interpreter {
namespace OpcodeImpl {
namespace COP0 {

	void TLBR()
	{
		COP0_LOG("COP0_TLBR %d:%x,%x,%x,%x",
			cpuRegs.CP0.n.Index, cpuRegs.CP0.n.PageMask, cpuRegs.CP0.n.EntryHi,
			cpuRegs.CP0.n.EntryLo0, cpuRegs.CP0.n.EntryLo1);

		const u8 i = cpuRegs.CP0.n.Index & 0x3f;

		if (i > 47)
		{
			Console.Warning("TLBR with index > 47! (%d)", i);
			return;
		}

		cpuRegs.CP0.n.PageMask = tlb[i].PageMask.Mask << 13;
		cpuRegs.CP0.n.EntryHi = tlb[i].EntryHi.UL & ~((tlb[i].PageMask.Mask << 13) | 0x1f00);
		cpuRegs.CP0.n.EntryLo0 = tlb[i].EntryLo0.UL & ~(0xFC000000) & ~1;
		cpuRegs.CP0.n.EntryLo1 = tlb[i].EntryLo1.UL & ~(0x7C000000) & ~1;
		// "If both the Global bit of EntryLo0 and EntryLo1 are set to 1, the processor ignores the ASID during TLB lookup."
		// This is reflected during TLBR, where G is only set if both EntryLo0 and EntryLo1 are global.
		cpuRegs.CP0.n.EntryLo0 |= (tlb[i].EntryLo0.UL & 1) & (tlb[i].EntryLo1.UL & 1);
		cpuRegs.CP0.n.EntryLo1 |= (tlb[i].EntryLo0.UL & 1) & (tlb[i].EntryLo1.UL & 1);
	}

	void TLBWI()
	{
		const u8 j = cpuRegs.CP0.n.Index & 0x3f;

		if (j > 47)
		{
			Console.Warning("TLBWI with index > 47! (%d)", j);
			return;
		}

		COP0_LOG("COP0_TLBWI %d:%x,%x,%x,%x",
			cpuRegs.CP0.n.Index, cpuRegs.CP0.n.PageMask, cpuRegs.CP0.n.EntryHi,
			cpuRegs.CP0.n.EntryLo0, cpuRegs.CP0.n.EntryLo1);

		RewriteTlbEntryFromCop0State(j);
	}

	void TLBWR()
	{
		const u8 j = cpuRegs.CP0.n.Random & 0x3f;

		if (j > 47)
		{
			Console.Warning("TLBWR with random > 47! (%d)", j);
			return;
		}

		DevCon.Warning("COP0_TLBWR %d:%x,%x,%x,%x\n",
			cpuRegs.CP0.n.Random, cpuRegs.CP0.n.PageMask, cpuRegs.CP0.n.EntryHi,
			cpuRegs.CP0.n.EntryLo0, cpuRegs.CP0.n.EntryLo1);

		RewriteTlbEntryFromCop0State(j);
	}

	void TLBP()
	{
		int i;

		union
		{
			struct
			{
				u32 VPN2 : 19;
				u32 VPN2X : 2;
				u32 G : 3;
				u32 ASID : 8;
			} s;
			u32 u;
		} EntryHi32;

		EntryHi32.u = cpuRegs.CP0.n.EntryHi;

		cpuRegs.CP0.n.Index = 0xFFFFFFFF;
		for (i = 0; i < 48; i++)
		{
			if (tlb[i].VPN2() == ((~tlb[i].Mask()) & (EntryHi32.s.VPN2)) && ((tlb[i].isGlobal()) || ((tlb[i].EntryHi.ASID & 0xff) == EntryHi32.s.ASID)))
			{
				cpuRegs.CP0.n.Index = i;
				break;
			}
		}
		if (cpuRegs.CP0.n.Index == 0xFFFFFFFF)
			cpuRegs.CP0.n.Index = 0x80000000;
	}

	void MFC0()
	{
		// Note on _Rd_ Condition 9: CP0.Count should be updated even if _Rt_ is 0.
		if ((_Rd_ != 9) && !_Rt_)
			return;

		//if(bExecBIOS == FALSE && _Rd_ == 25) Console.WriteLn("MFC0 _Rd_ %x = %x", _Rd_, cpuRegs.CP0.r[_Rd_]);
		switch (_Rd_)
		{
			case 12:
				cpuRegs.GPR.r[_Rt_].SD[0] = (s32)(cpuRegs.CP0.r[_Rd_] & 0xf0c79c1f);
				break;

			case 25:
				if (0 == (_Imm_ & 1)) // MFPS, register value ignored
				{
					cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.PERF.n.pccr.val;
				}
				else if (0 == (_Imm_ & 2)) // MFPC 0, only LSB of register matters
				{
					COP0_UpdatePCCR();
					cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.PERF.n.pcr0;
				}
				else // MFPC 1
				{
					COP0_UpdatePCCR();
					cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.PERF.n.pcr1;
				}
				/*Console.WriteLn("MFC0 PCCR = %x PCR0 = %x PCR1 = %x IMM= %x",  params
cpuRegs.PERF.n.pccr, cpuRegs.PERF.n.pcr0, cpuRegs.PERF.n.pcr1, _Imm_ & 0x3F);*/
				break;

			case 24:
				COP0_LOG("MFC0 Breakpoint debug Registers code = %x", cpuRegs.code & 0x3FF);
				break;

			case 9:
			{
				s64 incr = cpuRegs.cycle - cpuRegs.lastCOP0Cycle;
				if (incr == 0)
					incr++;
				cpuRegs.CP0.n.Count += incr;
				cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
				if (!_Rt_)
					break;
			}
				[[fallthrough]];

			default:
				cpuRegs.GPR.r[_Rt_].SD[0] = (s32)cpuRegs.CP0.r[_Rd_];
		}
	}

	void MTC0()
	{
		//if(bExecBIOS == FALSE && _Rd_ == 25) Console.WriteLn("MTC0 _Rd_ %x = %x", _Rd_, cpuRegs.CP0.r[_Rd_]);
		const u32 mtc0_value = cpuRegs.GPR.r[_Rt_].UL[0];

		switch (_Rd_)
		{
			case 9:
				cpuRegs.lastCOP0Cycle = cpuRegs.cycle;
				cpuRegs.CP0.r[9] = mtc0_value;
				break;

			case 12:
				WriteCP0Status(mtc0_value);
				break;

			case 16:
				WriteCP0Config(mtc0_value);
				break;

			case 24:
				COP0_LOG("MTC0 Breakpoint debug Registers code = %x", cpuRegs.code & 0x3FF);
				break;

			case 25:
				/*if(bExecBIOS == FALSE && _Rd_ == 25) Console.WriteLn("MTC0 PCCR = %x PCR0 = %x PCR1 = %x IMM= %x", params
	cpuRegs.PERF.n.pccr, cpuRegs.PERF.n.pcr0, cpuRegs.PERF.n.pcr1, _Imm_ & 0x3F);*/
				if (0 == (_Imm_ & 1)) // MTPS
				{
					if (0 != (_Imm_ & 0x3E)) // only effective when the register is 0
						break;
					// Updates PCRs and sets the PCCR.
					COP0_UpdatePCCR();
					cpuRegs.PERF.n.pccr.val = mtc0_value;
					COP0_DiagnosticPCCR();
				}
				else if (0 == (_Imm_ & 2)) // MTPC 0, only LSB of register matters
				{
					cpuRegs.PERF.n.pcr0 = mtc0_value;
					cpuRegs.lastPERFCycle[0] = cpuRegs.cycle;
				}
				else // MTPC 1
				{
					cpuRegs.PERF.n.pcr1 = mtc0_value;
					cpuRegs.lastPERFCycle[1] = cpuRegs.cycle;
				}
				break;

			default:
				cpuRegs.CP0.r[_Rd_] = mtc0_value;
				break;
		}
	}

	int CPCOND0()
	{
		return (((dmacRegs.stat.CIS | ~dmacRegs.pcr.CPC) & 0x3FF) == 0x3ff);
	}

	//#define CPCOND0	1

	void BC0F()
	{
		if (CPCOND0() == 0)
			intDoBranch(_BranchTarget_);
	}

	void BC0T()
	{
		if (CPCOND0() == 1)
			intDoBranch(_BranchTarget_);
	}

	void BC0FL()
	{
		if (CPCOND0() == 0)
			intDoBranch(_BranchTarget_);
		else
			cpuRegs.pc += 4;
	}

	void BC0TL()
	{
		if (CPCOND0() == 1)
			intDoBranch(_BranchTarget_);
		else
			cpuRegs.pc += 4;
	}

	void ERET()
	{
#ifdef ENABLE_VTUNE
		// Allow to stop vtune in a predictable way to compare runs
		// Of course, the limit will depend on the game.
		const u32 million = 1000 * 1000;
		static u32 vtune = 0;
		vtune++;

		// quick_exit vs exit: quick_exit won't call static storage destructor (OS will manage). It helps
		// avoiding the race condition between threads destruction.
		if (vtune > 30 * million)
		{
			Console.WriteLn("VTUNE: quick_exit");
			std::quick_exit(EXIT_SUCCESS);
		}
		else if (!(vtune % million))
		{
			Console.WriteLn("VTUNE: ERET was called %uM times", vtune / million);
		}

#endif

		if (cpuRegs.CP0.n.Status.b.ERL)
		{
			cpuRegs.pc = cpuRegs.CP0.n.ErrorEPC;
			cpuRegs.CP0.n.Status.b.ERL = 0;
		}
		else
		{
			cpuRegs.pc = cpuRegs.CP0.n.EPC;
			cpuRegs.CP0.n.Status.b.EXL = 0;
		}
		cpuUpdateOperationMode();
		cpuSetNextEventDelta(4);
		intSetBranch();
	}

	void DI()
	{
		if (cpuRegs.CP0.n.Status.b._EDI || cpuRegs.CP0.n.Status.b.EXL ||
			cpuRegs.CP0.n.Status.b.ERL || (cpuRegs.CP0.n.Status.b.KSU == 0))
		{
			cpuRegs.CP0.n.Status.b.EIE = 0;
			// IRQs are disabled so no need to do a cpu exception/event test...
			//cpuSetNextEventDelta();
		}
	}

	void EI()
	{
		if (cpuRegs.CP0.n.Status.b._EDI || cpuRegs.CP0.n.Status.b.EXL ||
			cpuRegs.CP0.n.Status.b.ERL || (cpuRegs.CP0.n.Status.b.KSU == 0))
		{
			cpuRegs.CP0.n.Status.b.EIE = 1;
			// schedule an event test, which will check for and raise pending IRQs.
			cpuSetNextEventDelta(4);
		}
	}

} // namespace COP0
} // namespace OpcodeImpl
} // namespace Interpreter
} // namespace R5900
