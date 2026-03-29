// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "arm64/VixlHelpers.h"

#ifndef _PC_

#define _i32(x) (s32)x //R3000A
#define _u32(x) (u32)x //R3000A

#define _i16(x) (s16)x // Not used
#define _u16(x) (u16)x // Not used

#define _i8(x) (s8)x // Not used
#define _u8(x) (u8)x //R3000A - once

/**** R3000A Instruction Macros ****/
#define _PC_       psxRegs.pc       // The next PC to be executed

#define _Funct_          ((psxRegs.code      ) & 0x3F)  // The funct part of the instruction register
#define _Rd_             ((psxRegs.code >> 11) & 0x1F)  // The rd part of the instruction register
#define _Rt_             ((psxRegs.code >> 16) & 0x1F)  // The rt part of the instruction register
#define _Rs_             ((psxRegs.code >> 21) & 0x1F)  // The rs part of the instruction register
#define _Sa_             ((psxRegs.code >>  6) & 0x1F)  // The sa part of the instruction register
#define _Im_             ((u16)psxRegs.code) // The immediate part of the instruction register
#define _InstrucTarget_  (psxRegs.code & 0x03ffffff)    // The target part of the instruction register

#define _Imm_	((short)psxRegs.code) // sign-extended immediate
#define _ImmU_	(psxRegs.code&0xffff) // zero-extended immediate

#define _rRs_   psxRegs.GPR.r[_Rs_]   // Rs register
#define _rRt_   psxRegs.GPR.r[_Rt_]   // Rt register
#define _rRd_   psxRegs.GPR.r[_Rd_]   // Rd register
#define _rSa_   psxRegs.GPR.r[_Sa_]   // Sa register
#define _rFs_   psxRegs.CP0.r[_Rd_]   // Fs register

#define _c2dRs_ psxRegs.CP2D.r[_Rs_]  // Rs cop2 data register
#define _c2dRt_ psxRegs.CP2D.r[_Rt_]  // Rt cop2 data register
#define _c2dRd_ psxRegs.CP2D.r[_Rd_]  // Rd cop2 data register
#define _c2dSa_ psxRegs.CP2D.r[_Sa_]  // Sa cop2 data register

#define _rHi_   psxRegs.GPR.n.hi   // The HI register
#define _rLo_   psxRegs.GPR.n.lo   // The LO register

#define _JumpTarget_    ((_InstrucTarget_ << 2) + (_PC_ & 0xf0000000))   // Calculates the target during a jump instruction
#define _BranchTarget_  (((s32)(s16)_Imm_ * 4) + _PC_)                 // Calculates the target during a branch instruction

#define _SetLink(x)     psxRegs.GPR.r[x] = _PC_ + 4;       // Sets the return address in the link register

extern s32 EEsCycle;
extern u32 EEoCycle;

#endif

extern s32 psxNextDeltaCounter;
extern u32 psxNextStartCounter;
extern bool iopEventAction;
extern bool iopEventTestIsActive;

// Branching status used when throwing exceptions.
extern bool iopIsDelaySlot;

// --------------------------------------------------------------------------------------
//  R3000Acpu
// --------------------------------------------------------------------------------------

struct R3000Acpu {
	void (*Reserve)();
	void (*Reset)();
	s32 (*ExecuteBlock)( s32 eeCycles );		// executes the given number of EE cycles.
	void (*Clear)(u32 Addr, u32 Size);
	void (*Shutdown)();
};

extern R3000Acpu *psxCpu;
extern R3000Acpu psxInt;
extern R3000Acpu psxRec;

extern void psxReset();
extern void psxException(u32 code, u32 step);
extern void iopEventTest();

int psxIsBreakpointNeeded(u32 addr);
int psxIsMemcheckNeeded(u32 pc);

// Subsets
extern void (*psxBSC[64])();
extern void (*psxSPC[64])();
extern void (*psxREG[32])();
extern void (*psxCP0[32])();
extern void (*psxCP2[64])();
extern void (*psxCP2BSC[32])();

extern void psxBiosReset();
extern bool psxBiosCall();
