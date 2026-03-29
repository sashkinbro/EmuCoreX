// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "arm64/VixlHelpers.h"
#include <array>

// --------------------------------------------------------------------------------------
//  EE Bios function name tables.
// --------------------------------------------------------------------------------------
namespace R5900 {
extern const char* const bios[256];
}

extern s32 EEsCycle;
extern u32 EEoCycle;

// used for optimization
union GPR_reg64 {
	u64 UD[1];      //64 bits
	s64 SD[1];
	u32 UL[2];
	s32 SL[2];
	u16 US[4];
	s16 SS[4];
	u8  UC[8];
	s8  SC[8];
};

union PageMask_t
{
	struct
	{
		u32 : 13;
		u32 Mask : 12;
		u32 : 7;
	};
	u32 UL;
};

union EntryHi_t
{
	struct
	{
		u32 ASID:8;
		u32 : 5;
		u32 VPN2:19;
	};
	u32 UL;
};

union EntryLo_t
{
	struct
	{
		u32 G:1;
		u32 V:1;
		u32 D:1;
		u32 C:3;
		u32 PFN:20;
		u32 : 5;
		u32 S : 1; // Only used in EntryLo0
	};
	u32 UL;

	constexpr bool isCached() const { return C == 0x3; }
	constexpr bool isValidCacheMode() const { return C == 0x2 || C == 0x3 || C == 0x7; }
};

struct tlbs
{
	PageMask_t PageMask;
	EntryHi_t EntryHi;
	EntryLo_t EntryLo0;
	EntryLo_t EntryLo1;

	// (((cpuRegs.CP0.n.EntryLo0 >> 6) & 0xFFFFF) & (~tlb[i].Mask())) << 12;
	constexpr u32 PFN0() const { return (EntryLo0.PFN & ~Mask()) << 12; }
	constexpr u32 PFN1() const { return (EntryLo1.PFN & ~Mask()) << 12; }
	constexpr u32 VPN2() const {return ((EntryHi.VPN2) & (~Mask())) << 13; }
	constexpr u32 Mask() const { return PageMask.Mask; }
	constexpr bool isGlobal() const { return EntryLo0.G && EntryLo1.G; }
	constexpr bool isSPR() const { return EntryLo0.S; }

	constexpr bool operator==(const tlbs& other) const
	{
		return PageMask.UL == other.PageMask.UL &&
			   EntryHi.UL == other.EntryHi.UL &&
			   EntryLo0.UL == other.EntryLo0.UL &&
			   EntryLo1.UL == other.EntryLo1.UL;
	}
};

#ifndef _PC_

////////////////////////////////////////////////////////////////////
// R5900 Instruction Macros

#define _PC_       cpuRegs.pc       // The next PC to be executed - only used in this header and R3000A.h

#define _Funct_          ((cpuRegs.code      ) & 0x3F)  // The funct part of the instruction register
#define _Rd_             ((cpuRegs.code >> 11) & 0x1F)  // The rd part of the instruction register
#define _Rt_             ((cpuRegs.code >> 16) & 0x1F)  // The rt part of the instruction register
#define _Rs_             ((cpuRegs.code >> 21) & 0x1F)  // The rs part of the instruction register
#define _Sa_             ((cpuRegs.code >>  6) & 0x1F)  // The sa part of the instruction register
#define _Im_             ((u16)cpuRegs.code) // The immediate part of the instruction register
#define _InstrucTarget_  (cpuRegs.code & 0x03ffffff)    // The target part of the instruction register

#define _Imm_	((s16)cpuRegs.code) // sign-extended immediate
#define _ImmU_	(cpuRegs.code&0xffff) // zero-extended immediate
#define _ImmSB_	(cpuRegs.code&0x8000) // gets the sign-bit of the immediate value

#define _Opcode_ (cpuRegs.code >> 26 )

#define _JumpTarget_     ((_InstrucTarget_ << 2) + (_PC_ & 0xf0000000))   // Calculates the target during a jump instruction
#define _BranchTarget_   (((s32)(s16)_Im_ * 4) + _PC_)                 // Calculates the target during a branch instruction
#define _TrapCode_       ((u16)cpuRegs.code >> 6)	// error code for non-immediate trap instructions.

#define _SetLink(x)     (cpuRegs.GPR.r[x].UD[0] = _PC_ + 4)       // Sets the return address in the link register

#endif

alignas(16) extern tlbs tlb[48];

struct cachedTlbs_t
{
	u32 count;

	alignas(16) std::array<u32, 48> PageMasks;
	alignas(16) std::array<u32, 48> PFN1s;
	alignas(16) std::array<u32, 48> CacheEnabled1;
	alignas(16) std::array<u32, 48> PFN0s;
	alignas(16) std::array<u32, 48> CacheEnabled0;
};

extern cachedTlbs_t cachedTlbs;
extern bool eeEventTestIsActive;

void intUpdateCPUCycles();
void intSetBranch();

// This is a special form of the interpreter's doBranch that is run from various
// parts of the Recs (namely COP0's branch codes and stuff).
void intDoBranch(u32 target);

// modules loaded at hardcoded addresses by the kernel
const u32 EEKERNEL_START	= 0;
const u32 EENULL_START		= 0x81FC0;
const u32 EELOAD_START		= 0x82000;
const u32 EELOAD_SIZE		= 0x20000; // overestimate for searching
extern u32 g_eeloadMain, g_eeloadExec;

extern void eeloadHook();
extern void eeloadHook2();

// --------------------------------------------------------------------------------------
//  R5900cpu
// --------------------------------------------------------------------------------------
// [TODO] : This is on the list to get converted to a proper C++ class.  I'm putting it
// off until I get my new IOPint and IOPrec re-merged. --air
//
struct R5900cpu
{
	// Memory allocation function, for allocating virtual memory spaces needed by
	// the virtual cpu provider.  Allocating additional heap memory from this method is
	// NOT recommended.  Heap allocations should be performed by Reset only.  This
	// maximizes the likeliness of reservations claiming addresses they prefer.
	void (*Reserve)();

	// Deallocates ram allocated by Allocate, Reserve, and/or by runtime code execution.
	void (*Shutdown)();

	// Initializes / Resets code execution states. Typically implementation is only
	// needed for recompilers, as interpreters have no internal execution states and
	// rely on the CPU/VM states almost entirely.
	void (*Reset)();

	// Steps a single instruction.  Meant to be used by debuggers.  Is currently unused
	// and unimplemented.  Future note: recompiler "step" should *always* fall back
	// on interpreters.
	void (*Step)();

	// Executes code until a break is signaled.  Execution can be paused or suspended
	// via thread-style signals that are handled by CheckExecutionState callbacks.
	// Execution Breakages are handled the same way, where-by a signal causes the Execute
	// call to return at the nearest state check (typically handled internally using
	// either C++ exceptions or setjmp/longjmp).
	void (*Execute)();

	// Immediately exits execution of recompiled code if we are in a state to do so, or
	// queues an exit as soon as it is safe. Safe in this case refers to whether we are
	// currently executing events or not.
	void (*ExitExecution)();

	// Cancels the currently-executing instruction, returning to the main loop.
	// Currently only works for the interpreter.
	void (*CancelInstruction)();

	// Manual recompiled code cache clear; typically useful to recompilers only.  Size is
	// in MIPS words (32 bits).  Dev note: this callback is nearly obsolete, and might be
	// better off replaced with some generic API callbacks from VTLB block protection.
	// Also: the calls from COP0's TLB remap code should be replaced with full recompiler
	// resets, since TLB remaps affect more than just the code they contain (code that
	// may reference the remapped blocks via memory loads/stores, for example).
	void (*Clear)(u32 Addr, u32 Size);
};

extern R5900cpu *Cpu;
extern R5900cpu intCpu;
extern R5900cpu recCpu;
#ifdef PCSX2_ARM64_DYNAREC
extern R5900cpu jitA64Cpu;
#endif

enum EE_intProcessStatus
{
	INT_NOT_RUNNING = 0,
	INT_RUNNING,
	INT_REQ_LOOP
};

enum EE_EventType
{
	DMAC_VIF0	= 0,
	DMAC_VIF1,
	DMAC_GIF,
	DMAC_FROM_IPU,
	DMAC_TO_IPU,
	DMAC_SIF0,
	DMAC_SIF1,
	DMAC_SIF2,
	DMAC_FROM_SPR,
	DMAC_TO_SPR,

	DMAC_MFIFO_VIF,
	DMAC_MFIFO_GIF,

	// We're setting error conditions through hwDmacIrq, so these correspond to the conditions above.
	DMAC_STALL_SIS		= 13, // SIS
	DMAC_MFIFO_EMPTY	= 14, // MEIS
	DMAC_BUS_ERROR	= 15,      // BEIS

	DMAC_GIF_UNIT,
	VIF_VU0_FINISH,
	VIF_VU1_FINISH,
	IPU_PROCESS,
	VU_MTVU_BUSY
};

extern void CPU_INT( EE_EventType n, s32 ecycle );
extern void CPU_SET_DMASTALL(EE_EventType n, bool set);
extern uint intcInterrupt();
extern uint dmacInterrupt();

extern void cpuReset();
extern void cpuException(u32 code, u32 bd);
extern void cpuTlbMissR(u32 addr, u32 bd);
extern void cpuTlbMissW(u32 addr, u32 bd);
extern void cpuTestHwInts();
extern void cpuClearInt(uint n);
extern void GoemonPreloadTlb();
extern void GoemonUnloadTlb(u32 key);

extern void cpuSetNextEvent( u32 startCycle, s32 delta );
extern void cpuSetNextEventDelta( s32 delta );
extern int  cpuTestCycle( u32 startCycle, s32 delta );
extern void cpuSetEvent();
extern int cpuGetCycles(int interrupt);

extern void _cpuEventTest_Shared();		// for internal use by the Dynarecs and Ints inside R5900:

extern void cpuTestINTCInts();
extern void cpuTestDMACInts();
extern void cpuTestTIMRInts();

// breakpoint code shared between interpreter and recompiler
int isMemcheckNeeded(u32 pc);
int isBreakpointNeeded(u32 addr);

////////////////////////////////////////////////////////////////////
// Exception Codes

#define EXC_CODE(x)     ((x)<<2)

#define EXC_CODE_Int    EXC_CODE(0)
#define EXC_CODE_Mod    EXC_CODE(1)     /* TLB Modification exception */
#define EXC_CODE_TLBL   EXC_CODE(2)     /* TLB Miss exception (load or instruction fetch) */
#define EXC_CODE_TLBS   EXC_CODE(3)     /* TLB Miss exception (store) */
#define EXC_CODE_AdEL   EXC_CODE(4)
#define EXC_CODE_AdES   EXC_CODE(5)
#define EXC_CODE_IBE    EXC_CODE(6)
#define EXC_CODE_DBE    EXC_CODE(7)
#define EXC_CODE_Sys    EXC_CODE(8)
#define EXC_CODE_Bp     EXC_CODE(9)
#define EXC_CODE_Ri     EXC_CODE(10)
#define EXC_CODE_CpU    EXC_CODE(11)
#define EXC_CODE_Ov     EXC_CODE(12)
#define EXC_CODE_Tr     EXC_CODE(13)
#define EXC_CODE_FPE    EXC_CODE(15)
#define EXC_CODE_WATCH  EXC_CODE(23)
#define EXC_CODE__MASK  0x0000007c
#define EXC_CODE__SHIFT 2
