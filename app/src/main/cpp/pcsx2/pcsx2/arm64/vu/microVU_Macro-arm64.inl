// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include "arm64/OaknutHelpers-arm64.h"

extern void _vu0WaitMicro();
extern void _vu0FinishMicro();
extern void vu0ExecMicro(u32 addr);

static constexpr bool VU0_FORCE_INTERP_COP2_MACRO_TEST = false;
static constexpr bool VU0_FORCE_INTERP_COP2_TRANSFER_TEST = false;
static constexpr bool VU0_FORCE_COP2_SYNC_TEST = false;

//static VURegs& vu0Regs = g_cpuRegistersPack.vuRegs[0];

//------------------------------------------------------------------
// Macro VU - Helper Macros / Functions
//------------------------------------------------------------------

using namespace R5900::Dynarec;

#define printCOP2(...) (void)0
//#define printCOP2 DevCon.Status

// For now, we need to free all ARM64 vector regs. Because we're not saving the nonvolatile registers when
// we enter micro mode, they will get overriden otherwise...
#define FLUSH_FOR_POSSIBLE_MICRO_EXEC (FLUSH_FREE_XMM | FLUSH_FREE_VU0)

static __fi OakMemOperand mVUOakCpuMem(s64 offset)
{
	return {oak::util::X27, offset};
}

static __fi OakMemOperand mVUOakCpuMemQ()
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_Q].UL)));
}

static __fi OakMemOperand mVUOakCpuMemVu0Status()
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_STATUS_FLAG].UL)));
}

static __fi OakMemOperand mVUOakCpuMemVu0VpuStat()
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_VPU_STAT].UL)));
}

static __fi OakMemOperand mVUOakCpuMemCycle()
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle)));
}

static __fi OakMemOperand mVUOakCpuMemVu0Cycle()
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].cycle)));
}

static __fi OakMemOperand mVUOakCpuMemVu0NextBlockCycles()
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].nextBlockCycles)));
}

static __fi OakMemOperand mVUOakCpuMemVu0VfLane(int vf, int lane)
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VF[0].UL[0])) +
		static_cast<s64>(vf) * static_cast<s64>(sizeof(VECTOR)) +
		static_cast<s64>(lane) * static_cast<s64>(sizeof(u32)));
}

static __fi OakMemOperand mVUOakCpuMemVu0Vf128(int vf)
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VF[0].UL[0])) +
		static_cast<s64>(vf) * static_cast<s64>(sizeof(VECTOR)));
}

static __fi OakMemOperand mVUOakCpuMemVu0Mac()
{
	return mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_MAC_FLAG].UL)));
}

static void mVUAddBlockCyclesToCpuCycle_emit_oaknut(oak::WReg dst)
{
	recBeginOaknutEmit();
	recFlushReccycle();
	oakLoad32(dst, mVUOakCpuMemCycle());
	oakAsm->MOV(OAK_WSCRATCH, scaleblockcycles_clear());
	oakAsm->ADD(dst, dst, OAK_WSCRATCH);
	oakStore32(dst, mVUOakCpuMemCycle());
	recReloadReccycle();
	recEndOaknutEmit();
}

static void mVUTestVu0Running_emit_oaknut(u32 mask)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, mVUOakCpuMemVu0VpuStat());
	oakAsm->TST(OAK_WSCRATCH, mask);
	recEndOaknutEmit();
}

static void mVUDenormalizeStatusFlagForMacro_emit_oaknut()
{
	recBeginOaknutEmit();
	oakLoad32(oak::util::W1, mVUOakCpuMemVu0Status());
	oakAsm->LSR(oakWRegister(VU_HOST_F0), oak::util::W1, 3);
	oakAsm->AND(oakWRegister(VU_HOST_F0), oakWRegister(VU_HOST_F0), 0x18);
	oakAsm->LSL(oak::util::W0, oak::util::W1, 11);
	oakAsm->AND(oak::util::W0, oak::util::W0, 0x1800);
	oakAsm->ORR(oakWRegister(VU_HOST_F0), oakWRegister(VU_HOST_F0), oak::util::W0);
	oakAsm->LSL(oak::util::W1, oak::util::W1, 14);
	oakAsm->MOV(oak::util::W0, 0x03cf0000);
	oakAsm->AND(oak::util::W1, oak::util::W1, oak::util::W0);
	oakAsm->ORR(oakWRegister(VU_HOST_F0), oakWRegister(VU_HOST_F0), oak::util::W1);
	recEndOaknutEmit();
}

static void mVUNormalizeStatusFlagForMacro_emit_oaknut()
{
	recBeginOaknutEmit();
	const oak::WReg normalized = oakWRegister(VU_HOST_T1);
	const oak::WReg denormalized = oakWRegister(VU_HOST_F0);
	pxAssert(normalized.index() != denormalized.index());

	mVUNormalizeSFLAGGroups_emit_oaknut(normalized, denormalized);
	oakAsm->AND(denormalized, denormalized, 0xffff0000);
	oakAsm->LSR(denormalized, denormalized, 14);
	oakAsm->ORR(normalized, normalized, denormalized);
	oakStore32(normalized, mVUOakCpuMemVu0Status());
	recEndOaknutEmit();
}

void setupMacroOp(int mode, const char* opName)
{
	// Set up reg allocation
	microVU0.regAlloc->reset(true);

	if (mode & 0x03) // Q will be read/written
		_freeXMMreg(VU_HOST_XMMPQ);

	// Set up MicroVU ready for new op
	printCOP2(opName);
	microVU0.cop2 = 1;
	microVU0.prog.IRinfo.curPC = 0;
	microVU0.code = cpuRegs.code;
	memset(&microVU0.prog.IRinfo.info[0], 0, sizeof(microVU0.prog.IRinfo.info[0]));

	if (mode & 0x01) // Q-Reg will be Read
	{
		recBeginOaknutEmit();
		oakLoad32(OAK_WSCRATCH, mVUOakCpuMemQ());
		oakAsm->FMOV(oakSRegister(VU_HOST_XMMPQ), OAK_WSCRATCH);
		recEndOaknutEmit();
	}
	if (mode & 0x08 && (!CHECK_VU_FLAGHACK || g_pCurInstInfo->info & EEINST_COP2_CLIP_FLAG)) // Clip Instruction
	{
		microVU0.prog.IRinfo.info[0].cFlag.write     = 0xff;
		microVU0.prog.IRinfo.info[0].cFlag.lastWrite = 0xff;
	}
	if (mode & 0x10 && (!CHECK_VU_FLAGHACK || g_pCurInstInfo->info & EEINST_COP2_STATUS_FLAG)) // Update Status Flag
	{
		microVU0.prog.IRinfo.info[0].sFlag.doFlag      = true;
		microVU0.prog.IRinfo.info[0].sFlag.doNonSticky = true;
		microVU0.prog.IRinfo.info[0].sFlag.write       = 0;
		microVU0.prog.IRinfo.info[0].sFlag.lastWrite   = 0;
	}
	if (mode & 0x10 && (!CHECK_VU_FLAGHACK || g_pCurInstInfo->info & EEINST_COP2_MAC_FLAG)) // Update Mac Flags
	{
		microVU0.prog.IRinfo.info[0].mFlag.doFlag      = true;
		microVU0.prog.IRinfo.info[0].mFlag.write       = 0xff;
	}
	if (mode & 0x10 && (!CHECK_VU_FLAGHACK || g_pCurInstInfo->info & (EEINST_COP2_STATUS_FLAG | EEINST_COP2_DENORMALIZE_STATUS_FLAG)))
	{
		_freeX86reg(VU_HOST_F0);

		if (!CHECK_VU_FLAGHACK || (g_pCurInstInfo->info & EEINST_COP2_DENORMALIZE_STATUS_FLAG))
		{
			// flags are normalized, so denormalize before running the first instruction
			mVUDenormalizeStatusFlagForMacro_emit_oaknut();
		}
		else
		{
			// load denormalized status flag
			// ideally we'd keep this in a register, but 32-bit...
			recBeginOaknutEmit();
			oakLoad32(oakWRegister(VU_HOST_F0), mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[g_cpuRegistersPack.vuRegs->idx].VI[REG_STATUS_FLAG].UL))));
			recEndOaknutEmit();
		}
	}
}

void endMacroOp(int mode)
{
	if (mode & 0x02) // Q-Reg was Written To
	{
		recBeginOaknutEmit();
		oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(VU_HOST_XMMPQ));
		oakStore32(OAK_WSCRATCH, mVUOakCpuMemQ());
		recEndOaknutEmit();
	}

	microVU0.regAlloc->flushPartialForCOP2();

	if (mode & 0x10)
	{
		if (!CHECK_VU_FLAGHACK || g_pCurInstInfo->info & EEINST_COP2_NORMALIZE_STATUS_FLAG)
		{
			// Normalize
			mVUNormalizeStatusFlagForMacro_emit_oaknut();
		}
		else if (g_pCurInstInfo->info & (EEINST_COP2_STATUS_FLAG | EEINST_COP2_DENORMALIZE_STATUS_FLAG))
		{
			// backup denormalized flags for the next instruction
			// this is fine, because we'll normalize them again before this reg is accessed
			recBeginOaknutEmit();
			oakStore32(oakWRegister(VU_HOST_F0), mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[g_cpuRegistersPack.vuRegs->idx].VI[REG_STATUS_FLAG].UL))));
			recEndOaknutEmit();
		}
	}

	microVU0.cop2 = 0;
	microVU0.regAlloc->reset(false);
}

void mVUFreeCOP2XmmReg(int hostreg)
{
	microVU0.regAlloc->clearRegCOP2(hostreg);
}

void mVUFreeCOP2GPR(int hostreg)
{
	microVU0.regAlloc->clearGPRCOP2(hostreg);
}

bool mVUIsReservedCOP2(int hostreg)
{
	// Only the first status flag host register is reserved in COP2 mode.
	return (hostreg == VU_HOST_T1 || hostreg == VU_HOST_T2 || hostreg == VU_HOST_F0);
}

#define INTERPRETATE_COP2_FUNC(f) \
	void recV##f() \
	{ \
		iFlushCall(FLUSH_FOR_POSSIBLE_MICRO_EXEC); \
		mVUAddBlockCyclesToCpuCycle_emit_oaknut(oak::util::W0); \
		recCall(V##f); \
	}

//------------------------------------------------------------------
// Macro VU - Instructions
//------------------------------------------------------------------

//------------------------------------------------------------------
// Macro VU - Redirect Upper Instructions
//------------------------------------------------------------------

/* Mode information
0x1  reads Q reg
0x2  writes Q reg
0x4  requires analysis pass
0x8  write CLIP
0x10 writes status/mac
0x100 requires ARM64 host GPRs
*/

void recVABS()
{
	setupMacroOp(0x0, "ABS");
	mVU_ABS_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVITOF0()
{
	setupMacroOp(0x0, "ITOF0");
	mVU_ITOF0_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVITOF4()
{
	setupMacroOp(0x0, "ITOF4");
	mVU_ITOF4_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVITOF12()
{
	setupMacroOp(0x0, "ITOF12");
	mVU_ITOF12_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVITOF15()
{
	setupMacroOp(0x0, "ITOF15");
	mVU_ITOF15_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVFTOI0()
{
	setupMacroOp(0x0, "FTOI0");
	mVU_FTOI0_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVFTOI4()
{
	setupMacroOp(0x0, "FTOI4");
	mVU_FTOI4_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVFTOI12()
{
	setupMacroOp(0x0, "FTOI12");
	mVU_FTOI12_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVFTOI15()
{
	setupMacroOp(0x0, "FTOI15");
	mVU_FTOI15_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVADD()
{
	setupMacroOp(0x110, "ADD");
	mVU_ADD_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDi()
{
	setupMacroOp(0x110, "ADDi");
	mVU_ADDi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDq()
{
	setupMacroOp(0x111, "ADDq");
	mVU_ADDq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVADDx()
{
	setupMacroOp(0x110, "ADDx");
	mVU_ADDx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDy()
{
	setupMacroOp(0x110, "ADDy");
	mVU_ADDy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDz()
{
	setupMacroOp(0x110, "ADDz");
	mVU_ADDz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDw()
{
	setupMacroOp(0x110, "ADDw");
	mVU_ADDw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDA()
{
	setupMacroOp(0x110, "ADDA");
	mVU_ADDA_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDAi()
{
	setupMacroOp(0x110, "ADDAi");
	mVU_ADDAi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDAq()
{
	setupMacroOp(0x111, "ADDAq");
	mVU_ADDAq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVADDAx()
{
	setupMacroOp(0x110, "ADDAx");
	mVU_ADDAx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDAy()
{
	setupMacroOp(0x110, "ADDAy");
	mVU_ADDAy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDAz()
{
	setupMacroOp(0x110, "ADDAz");
	mVU_ADDAz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVADDAw()
{
	setupMacroOp(0x110, "ADDAw");
	mVU_ADDAw_emit(microVU0, 1);
	endMacroOp(0x110);
}

static void recVSUB_emit_oaknut()
{
	mVU_SUB_emit(microVU0, 1);
}

void recVSUB()
{
	setupMacroOp(0x110, "SUB");
	recVSUB_emit_oaknut();
	endMacroOp(0x110);
}

void recVSUBi()
{
	setupMacroOp(0x110, "SUBi");
	mVU_SUBi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBq()
{
	setupMacroOp(0x111, "SUBq");
	mVU_SUBq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVSUBx()
{
	setupMacroOp(0x110, "SUBx");
	mVU_SUBx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBy()
{
	setupMacroOp(0x110, "SUBy");
	mVU_SUBy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBz()
{
	setupMacroOp(0x110, "SUBz");
	mVU_SUBz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBw()
{
	setupMacroOp(0x110, "SUBw");
	mVU_SUBw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBA()
{
	setupMacroOp(0x110, "SUBA");
	mVU_SUBA_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBAi()
{
	setupMacroOp(0x110, "SUBAi");
	mVU_SUBAi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBAq()
{
	setupMacroOp(0x111, "SUBAq");
	mVU_SUBAq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVSUBAx()
{
	setupMacroOp(0x110, "SUBAx");
	mVU_SUBAx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBAy()
{
	setupMacroOp(0x110, "SUBAy");
	mVU_SUBAy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBAz()
{
	setupMacroOp(0x110, "SUBAz");
	mVU_SUBAz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVSUBAw()
{
	setupMacroOp(0x110, "SUBAw");
	mVU_SUBAw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMUL()
{
	setupMacroOp(0x110, "MUL");
	mVU_MUL_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULi()
{
	setupMacroOp(0x110, "MULi");
	mVU_MULi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULq()
{
	setupMacroOp(0x111, "MULq");
	mVU_MULq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVMULx()
{
	setupMacroOp(0x110, "MULx");
	mVU_MULx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULy()
{
	setupMacroOp(0x110, "MULy");
	mVU_MULy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULz()
{
	setupMacroOp(0x110, "MULz");
	mVU_MULz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULw()
{
	setupMacroOp(0x110, "MULw");
	mVU_MULw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULA()
{
	setupMacroOp(0x110, "MULA");
	mVU_MULA_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULAi()
{
	setupMacroOp(0x110, "MULAi");
	mVU_MULAi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULAq()
{
	setupMacroOp(0x111, "MULAq");
	mVU_MULAq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVMULAx()
{
	setupMacroOp(0x110, "MULAx");
	mVU_MULAx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULAy()
{
	setupMacroOp(0x110, "MULAy");
	mVU_MULAy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULAz()
{
	setupMacroOp(0x110, "MULAz");
	mVU_MULAz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMULAw()
{
	setupMacroOp(0x110, "MULAw");
	mVU_MULAw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMAX()
{
	setupMacroOp(0x0, "MAX");
	mVU_MAX_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMAXi()
{
	setupMacroOp(0x0, "MAXi");
	mVU_MAXi_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMAXx()
{
	setupMacroOp(0x0, "MAXx");
	mVU_MAXx_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMAXy()
{
	setupMacroOp(0x0, "MAXy");
	mVU_MAXy_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMAXz()
{
	setupMacroOp(0x0, "MAXz");
	mVU_MAXz_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMAXw()
{
	setupMacroOp(0x0, "MAXw");
	mVU_MAXw_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMINI()
{
	setupMacroOp(0x0, "MINI");
	mVU_MINI_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMINIi()
{
	setupMacroOp(0x0, "MINIi");
	mVU_MINIi_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMINIx()
{
	setupMacroOp(0x0, "MINIx");
	mVU_MINIx_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMINIy()
{
	setupMacroOp(0x0, "MINIy");
	mVU_MINIy_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMINIz()
{
	setupMacroOp(0x0, "MINIz");
	mVU_MINIz_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMINIw()
{
	setupMacroOp(0x0, "MINIw");
	mVU_MINIw_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMADD()
{
	setupMacroOp(0x110, "MADD");
	mVU_MADD_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDi()
{
	setupMacroOp(0x110, "MADDi");
	mVU_MADDi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDq()
{
	setupMacroOp(0x111, "MADDq");
	mVU_MADDq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVMADDx()
{
	setupMacroOp(0x110, "MADDx");
	mVU_MADDx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDy()
{
	setupMacroOp(0x110, "MADDy");
	mVU_MADDy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDz()
{
	setupMacroOp(0x110, "MADDz");
	mVU_MADDz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDw()
{
	setupMacroOp(0x110, "MADDw");
	mVU_MADDw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDA()
{
	setupMacroOp(0x110, "MADDA");
	mVU_MADDA_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDAi()
{
	setupMacroOp(0x110, "MADDAi");
	mVU_MADDAi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDAq()
{
	setupMacroOp(0x111, "MADDAq");
	mVU_MADDAq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVMADDAx()
{
	setupMacroOp(0x110, "MADDAx");
	mVU_MADDAx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDAy()
{
	setupMacroOp(0x110, "MADDAy");
	mVU_MADDAy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDAz()
{
	setupMacroOp(0x110, "MADDAz");
	mVU_MADDAz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMADDAw()
{
	setupMacroOp(0x110, "MADDAw");
	mVU_MADDAw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUB()
{
	setupMacroOp(0x110, "MSUB");
	mVU_MSUB_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBi()
{
	setupMacroOp(0x110, "MSUBi");
	mVU_MSUBi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBq()
{
	setupMacroOp(0x111, "MSUBq");
	mVU_MSUBq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVMSUBx()
{
	setupMacroOp(0x110, "MSUBx");
	mVU_MSUBx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBy()
{
	setupMacroOp(0x110, "MSUBy");
	mVU_MSUBy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBz()
{
	setupMacroOp(0x110, "MSUBz");
	mVU_MSUBz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBw()
{
	setupMacroOp(0x110, "MSUBw");
	mVU_MSUBw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBA()
{
	setupMacroOp(0x110, "MSUBA");
	mVU_MSUBA_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBAi()
{
	setupMacroOp(0x110, "MSUBAi");
	mVU_MSUBAi_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBAq()
{
	setupMacroOp(0x111, "MSUBAq");
	mVU_MSUBAq_emit(microVU0, 1);
	endMacroOp(0x111);
}

void recVMSUBAx()
{
	setupMacroOp(0x110, "MSUBAx");
	mVU_MSUBAx_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBAy()
{
	setupMacroOp(0x110, "MSUBAy");
	mVU_MSUBAy_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBAz()
{
	setupMacroOp(0x110, "MSUBAz");
	mVU_MSUBAz_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVMSUBAw()
{
	setupMacroOp(0x110, "MSUBAw");
	mVU_MSUBAw_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVOPMULA()
{
	setupMacroOp(0x110, "OPMULA");
	mVU_OPMULA_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVOPMSUB()
{
	setupMacroOp(0x110, "OPMSUB");
	mVU_OPMSUB_emit(microVU0, 1);
	endMacroOp(0x110);
}

void recVCLIP()
{
	setupMacroOp(0x108, "CLIP");
	mVU_CLIP_emit(microVU0, 1);
	endMacroOp(0x108);
}


//------------------------------------------------------------------
// Macro VU - Redirect Lower Instructions
//------------------------------------------------------------------

void recVDIV()
{
	setupMacroOp(0x112, "DIV");
	mVU_DIV_emit(microVU0, 1);
	endMacroOp(0x112);
}

void recVSQRT()
{
	setupMacroOp(0x112, "SQRT");
	mVU_SQRT_emit(microVU0, 1);
	endMacroOp(0x112);
}

void recVRSQRT()
{
	setupMacroOp(0x112, "RSQRT");
	mVU_RSQRT_emit(microVU0, 1);
	endMacroOp(0x112);
}

static void recVIADD_emit_oaknut()
{
	mVU_IADD_emit(microVU0, 1);
}

static void recVIADDI_emit_oaknut()
{
	mVU_IADDI_emit(microVU0, 1);
}

static void recVIAND_emit_oaknut()
{
	mVU_IAND_emit(microVU0, 1);
}

static void recVIOR_emit_oaknut()
{
	mVU_IOR_emit(microVU0, 1);
}

static void recVISUB_emit_oaknut()
{
	mVU_ISUB_emit(microVU0, 1);
}

static void recVISWR_emit_oaknut()
{
	mVU_ISWR_emit(microVU0, 1);
}

static void recVSQI_emit_oaknut()
{
	mVU_SQI_emit(microVU0, 1);
}

void recVIADD()
{
	setupMacroOp(0x104, "IADD");
	recVIADD_emit_oaknut();
	endMacroOp(0x104);
}

void recVIADDI()
{
	setupMacroOp(0x104, "IADDI");
	recVIADDI_emit_oaknut();
	endMacroOp(0x104);
}

void recVIAND()
{
	setupMacroOp(0x104, "IAND");
	recVIAND_emit_oaknut();
	endMacroOp(0x104);
}

void recVIOR()
{
	setupMacroOp(0x104, "IOR");
	recVIOR_emit_oaknut();
	endMacroOp(0x104);
}

void recVISUB()
{
	setupMacroOp(0x104, "ISUB");
	recVISUB_emit_oaknut();
	endMacroOp(0x104);
}

void recVILWR()
{
	setupMacroOp(0x104, "ILWR");
	mVU_ILWR_emit(microVU0, 1);
	endMacroOp(0x104);
}

void recVISWR()
{
	setupMacroOp(0x100, "ISWR");
	recVISWR_emit_oaknut();
	endMacroOp(0x100);
}

void recVLQI()
{
	setupMacroOp(0x104, "LQI");
	mVU_LQI_emit(microVU0, 1);
	endMacroOp(0x104);
}

void recVLQD()
{
	setupMacroOp(0x104, "LQD");
	mVU_LQD_emit(microVU0, 1);
	endMacroOp(0x104);
}

void recVSQI()
{
	setupMacroOp(0x100, "SQI");
	recVSQI_emit_oaknut();
	endMacroOp(0x100);
}

void recVSQD()
{
	setupMacroOp(0x100, "SQD");
	mVU_SQD_emit(microVU0, 1);
	endMacroOp(0x100);
}

void recVMFIR()
{
	setupMacroOp(0x104, "MFIR");
	mVU_MFIR_emit(microVU0, 1);
	endMacroOp(0x104);
}

void recVMTIR()
{
	setupMacroOp(0x104, "MTIR");
	mVU_MTIR_emit(microVU0, 1);
	endMacroOp(0x104);
}

void recVMOVE()
{
	setupMacroOp(0x0, "MOVE");
	mVU_MOVE_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVMR32()
{
	setupMacroOp(0x0, "MR32");
	mVU_MR32_emit(microVU0, 1);
	endMacroOp(0x0);
}

void recVRINIT()
{
	setupMacroOp(0x100, "RINIT");
	mVU_RINIT_emit(microVU0, 1);
	endMacroOp(0x100);
}

void recVRGET()
{
	setupMacroOp(0x104, "RGET");
	mVU_RGET_emit(microVU0, 1);
	endMacroOp(0x104);
}

void recVRNEXT()
{
	setupMacroOp(0x104, "RNEXT");
	mVU_RNEXT_emit(microVU0, 1);
	endMacroOp(0x104);
}

void recVRXOR()
{
	setupMacroOp(0x100, "RXOR");
	mVU_RXOR_emit(microVU0, 1);
	endMacroOp(0x100);
}

//------------------------------------------------------------------
// Macro VU - Misc...
//------------------------------------------------------------------

void recVNOP() {}
void recVWAITQ() {}

static void recVU0CallMicro_emit_oaknut(u32 addr)
{
	recBeginOaknutEmit();
	recFlushReccycle();
	oakEmitCall(reinterpret_cast<const void*>(_vu0FinishMicro));
	oakAsm->MOV(OAK_WARG1, addr);
	oakEmitCall(reinterpret_cast<const void*>(vu0ExecMicro));
	recReloadReccycle();
	recEndOaknutEmit();
}

static void recVU0CallMicroFromCmsar0_emit_oaknut()
{
	recBeginOaknutEmit();
	recFlushReccycle();
	oakEmitCall(reinterpret_cast<const void*>(_vu0FinishMicro));
	oakLoad16(OAK_WARG1, mVUOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_CMSAR0].US[0]))));
	oakEmitCall(reinterpret_cast<const void*>(vu0ExecMicro));
	recReloadReccycle();
	recEndOaknutEmit();
}

void recVCALLMS()
{
	iFlushCall(FLUSH_FOR_POSSIBLE_MICRO_EXEC);
	mVUAddBlockCyclesToCpuCycle_emit_oaknut(oak::util::W0);
	recVU0CallMicro_emit_oaknut((cpuRegs.code >> 6) & 0x7fff);
}

void recVCALLMSR()
{
	iFlushCall(FLUSH_FOR_POSSIBLE_MICRO_EXEC);
	mVUAddBlockCyclesToCpuCycle_emit_oaknut(oak::util::W0);
	recVU0CallMicroFromCmsar0_emit_oaknut();
}

static void recCOP2InterpreterCall(void (*func)())
{
	iFlushCall(FLUSH_FOR_POSSIBLE_MICRO_EXEC);
	mVUAddBlockCyclesToCpuCycle_emit_oaknut(oak::util::W0);
	recCall(func);
}

static bool recCOP2TryInterpSPEC1()
{
	if (!VU0_FORCE_INTERP_COP2_MACRO_TEST)
		return false;

	if (_Funct_ >= 0x3c)
	{
		recCOP2InterpreterCall(Int_COP2SPECIAL2PrintTable[(cpuRegs.code & 3) | ((cpuRegs.code >> 4) & 0x7c)]);
		return true;
	}

	recCOP2InterpreterCall(Int_COP2SPECIAL1PrintTable[_Funct_]);
	return true;
}

static bool recCOP2TryInterpSPEC2()
{
	if (!VU0_FORCE_INTERP_COP2_MACRO_TEST)
		return false;

	recCOP2InterpreterCall(Int_COP2SPECIAL2PrintTable[(cpuRegs.code & 3) | ((cpuRegs.code >> 4) & 0x7c)]);
	return true;
}

static bool recCOP2TryInterpTransfer(void (*func)())
{
	if (!VU0_FORCE_INTERP_COP2_TRANSFER_TEST)
		return false;

	recCOP2InterpreterCall(func);
	return true;
}

//------------------------------------------------------------------
// Macro VU - Branches
//------------------------------------------------------------------

static void _setupBranchTest(oak::Cond skipCond, bool isLikely)
{
	printCOP2("COP2 Branch");
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	const bool swap = isLikely ? false : TrySwapDelaySlot(0, 0, 0, false);
	_eeFlushAllDirty();
	mVUTestVu0Running_emit_oaknut(0x100);

//	recDoBranchImm(branchTo, jmpType(0), isLikely, swap);
	recDoBranchImmOaknut(branchTo, skipCond, isLikely, swap);
}

void recBC2F()
{
//    _setupBranchTest(JNZ32, false);
	_setupBranchTest(oak::Cond::NE, false);
}
void recBC2T()
{
//    _setupBranchTest(JZ32,  false);
	_setupBranchTest(oak::Cond::EQ, false);
}
void recBC2FL()
{
//    _setupBranchTest(JNZ32, true);
	_setupBranchTest(oak::Cond::NE, true);
}
void recBC2TL()
{
//    _setupBranchTest(JZ32,  true);
	_setupBranchTest(oak::Cond::EQ, true);
}

//------------------------------------------------------------------
// Macro VU - COP2 Transfer Instructions
//------------------------------------------------------------------

static void COP2_Interlock(bool mBitSync)
{
	if (cpuRegs.code & 1)
	{
		s_nBlockInterlocked = true;

		// We can safely skip the _vu0FinishMicro() call, when there's nothing
		// that can trigger a VU0 program between CFC2/CTC2/COP2 instructions.
		if (g_pCurInstInfo->info & EEINST_COP2_SYNC_VU0)
		{
			iFlushCall(FLUSH_FOR_POSSIBLE_MICRO_EXEC);
			_freeX86reg(VU_HOST_T1);
			mVUAddBlockCyclesToCpuCycle_emit_oaknut(oakWRegister(VU_HOST_T1));

			mVUTestVu0Running_emit_oaknut(0x1);
			recBeginOaknutEmit();
			oak::Label skipvuidle;
			oakAsm->B(oak::Cond::EQ, skipvuidle);
			if (mBitSync)
			{
				oakLoad32(OAK_WSCRATCH, mVUOakCpuMemVu0Cycle());
				oakAsm->SUB(oakWRegister(VU_HOST_T1), oakWRegister(VU_HOST_T1), OAK_WSCRATCH);

				// Why do we check this here? Ratchet games, maybe others end up with flickering polygons
				// when we use lazy COP2 sync, otherwise. The micro resumption getting deferred an extra
				// EE block is apparently enough to cause issues.
				if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack) {
					oakLoad32(OAK_WSCRATCH, mVUOakCpuMemVu0NextBlockCycles());
					oakAsm->SUB(oakWRegister(VU_HOST_T1), oakWRegister(VU_HOST_T1), OAK_WSCRATCH);
				}
				oakAsm->CMP(oakWRegister(VU_HOST_T1), 4);
				oak::Label skip;
				oakAsm->B(oak::Cond::LT, skip);
				oakMoveAddressToReg(oak::util::X0, CpuVU0);
				oakAsm->MOV(oak::util::W1, s_nBlockInterlocked ? 1 : 0);
				oakEmitCall(reinterpret_cast<void*>(BaseVUmicroCPU::ExecuteBlockJIT));
//				skip.SetTarget();
				oakAsm->l(skip);

				oakEmitCall(reinterpret_cast<void*>(_vu0WaitMicro));
			}
			else {
				oakEmitCall(reinterpret_cast<void*>(_vu0FinishMicro));
			}
//			skipvuidle.SetTarget();
			oakAsm->l(skipvuidle);
			recEndOaknutEmit();
		}
	}
}

static void mVUSyncVU0()
{
	iFlushCall(FLUSH_FOR_POSSIBLE_MICRO_EXEC);
	_freeX86reg(VU_HOST_T1);
	mVUAddBlockCyclesToCpuCycle_emit_oaknut(oakWRegister(VU_HOST_T1));

	mVUTestVu0Running_emit_oaknut(0x1);
	recBeginOaknutEmit();
	oak::Label skipvuidle;
	oakAsm->B(oak::Cond::EQ, skipvuidle);
	oakLoad32(OAK_WSCRATCH, mVUOakCpuMemVu0Cycle());
	oakAsm->SUB(oakWRegister(VU_HOST_T1), oakWRegister(VU_HOST_T1), OAK_WSCRATCH);
	if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack) {
		oakLoad32(OAK_WSCRATCH, mVUOakCpuMemVu0NextBlockCycles());
		oakAsm->SUB(oakWRegister(VU_HOST_T1), oakWRegister(VU_HOST_T1), OAK_WSCRATCH);
	}
	oakAsm->CMP(oakWRegister(VU_HOST_T1), 4);
	oak::Label skip;
	oakAsm->B(oak::Cond::LT, skip);
	oakMoveAddressToReg(oak::util::X0, CpuVU0);
	oakAsm->MOV(oak::util::W1, s_nBlockInterlocked ? 1 : 0);
	oakEmitCall(reinterpret_cast<void*>(BaseVUmicroCPU::ExecuteBlockJIT));
//	skip.SetTarget();
	oakAsm->l(skip);
//	skipvuidle.SetTarget();
	oakAsm->l(skipvuidle);
	recEndOaknutEmit();
}

static void mVUFinishVU0()
{
	iFlushCall(FLUSH_FOR_POSSIBLE_MICRO_EXEC);
	mVUTestVu0Running_emit_oaknut(0x1);
	recBeginOaknutEmit();
	oak::Label skipvuidle;
	oakAsm->B(oak::Cond::EQ, skipvuidle);
	oakEmitCall(reinterpret_cast<void*>(_vu0FinishMicro));
//	skipvuidle.SetTarget();
	oakAsm->l(skipvuidle);
	recEndOaknutEmit();
}

static void TEST_FBRST_RESET(int flagreg, void(*resetFunct)(), int vuIndex)
{
	recBeginOaknutEmit();
	oakAsm->TST(oakWRegister(flagreg), (vuIndex) ? 0x200 : 0x002);
	oak::Label skip;
	oakAsm->B(oak::Cond::EQ, skip);
	oakEmitCall(reinterpret_cast<void*>(resetFunct));
//	skip.SetTarget();
	oakAsm->l(skip);
	recEndOaknutEmit();
}

static void recCFC2LoadSigned32FromCpu_emit_oaknut(int regt, s64 offset)
{
	recBeginOaknutEmit();
	const oak::WReg regt_w = oakWRegister(regt);
	const oak::XReg regt_x = oakXRegister(regt);
	oakLoad32(regt_w, {oak::util::X27, offset});
	oakAsm->SXTW(regt_x, regt_w);
	recEndOaknutEmit();
}

static void recCFC2LoadUnsigned16FromCpu_emit_oaknut(int regt, s64 offset)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(offset));
	oakAsm->ADD(OAK_XSCRATCH, oak::util::X27, OAK_XSCRATCH);
	oakAsm->LDRH(oakWRegister(regt), OAK_XSCRATCH);
	recEndOaknutEmit();
}

static void recMoveGPRtoOakWForMacroVUControl(oak::WReg to, int fromgpr)
{
	if (fromgpr == 0)
	{
		oakAsm->MOV(to, 0);
		return;
	}

	if (GPR_IS_CONST1(fromgpr))
	{
		oakAsm->MOV(to, g_cpuConstRegs[fromgpr].UL[0]);
		return;
	}

	const int gprreg = _checkX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
	if (gprreg >= 0)
	{
		oakAsm->MOV(to, oakWRegister(gprreg));
		return;
	}

	const int xmmreg = _checkXMMreg(XMMTYPE_GPRREG, fromgpr, MODE_READ);
	if (xmmreg >= 0)
	{
		oakAsm->FMOV(to, oakSRegister(xmmreg));
		return;
	}

	oakLoad32(to, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[fromgpr].UL[0]))});
}

static void recCFC2_emit_oaknut()
{
	printCOP2("CFC2");

	COP2_Interlock(false);

	if (!_Rt_)
		return;

	if (!(cpuRegs.code & 1))
	{
		if (g_pCurInstInfo->info & EEINST_COP2_SYNC_VU0)
			mVUSyncVU0();
		else if (g_pCurInstInfo->info & EEINST_COP2_FINISH_VU0)
			mVUFinishVU0();
	}

	// VI0..VI15 and R only replace the low 32-bit word. The EE keeps the
	// remaining 96 bits of the 128-bit GPR intact for these CFC2 forms.
	if (_Rd_ < REG_STATUS_FLAG || _Rd_ == REG_R)
	{
		const int xmmregt = _allocGPRtoXMMreg(_Rt_, MODE_READ | MODE_WRITE);
		recBeginOaknutEmit();
		if (_Rd_ == 0)
		{
			oakAsm->MOV(OAK_WSCRATCH, 0);
		}
		else if (_Rd_ == REG_R)
		{
			oakLoad32(OAK_WSCRATCH, {oak::util::X27,
				static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_R].UL))});
			oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x7fffff);
		}
		else
		{
			const int vireg = _checkX86reg(X86TYPE_VIREG, _Rd_, MODE_READ);
			if (vireg >= 0)
				oakAsm->UXTH(OAK_WSCRATCH, oakWRegister(vireg));
			else
			{
				oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(offsetof(cpuRegistersPack, vuRegs[0].VI[_Rd_].UL)));
				oakAsm->ADD(OAK_XSCRATCH, oak::util::X27, OAK_XSCRATCH);
				oakAsm->LDRH(OAK_WSCRATCH, OAK_XSCRATCH);
			}
		}
		oakAsm->MOV(oakQRegister(xmmregt).Selem()[0], OAK_WSCRATCH);
		recEndOaknutEmit();
		return;
	}

	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
	pxAssert(!GPR_IS_CONST1(_Rt_));

	if (_Rd_ == 0) // why would you read vi00?
	{
		recBeginOaknutEmit();
		oakAsm->EOR(oakWRegister(regt), oakWRegister(regt), oakWRegister(regt));
		recEndOaknutEmit();
	}
	else if (_Rd_ == REG_I)
	{
		const int xmmreg = _checkXMMreg(XMMTYPE_VFREG, 33, MODE_READ);
		if (xmmreg >= 0)
		{
			recBeginOaknutEmit();
			oakAsm->FMOV(oakWRegister(regt), oakSRegister(xmmreg));
			oakAsm->SXTW(oakXRegister(regt), oakWRegister(regt));
			recEndOaknutEmit();
		}
		else
		{
			recCFC2LoadSigned32FromCpu_emit_oaknut(regt, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[_Rd_].UL)));
		}
	}
	else if (_Rd_ == REG_R)
	{
		recBeginOaknutEmit();
		oakLoad32(oakWRegister(regt), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_R].UL))});
		oakAsm->SXTW(oakXRegister(regt), oakWRegister(regt));
		oakAsm->AND(oakXRegister(regt), oakXRegister(regt), 0x7FFFFF);
		recEndOaknutEmit();
	}
	else if (_Rd_ >= REG_STATUS_FLAG) // FixMe: Should R-Reg have upper 9 bits 0?
	{
		recCFC2LoadSigned32FromCpu_emit_oaknut(regt, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[_Rd_].UL)));
	}
	else
	{
		const int vireg = _allocIfUsedVItoX86(_Rd_, MODE_READ);
		if (vireg >= 0)
		{
			recBeginOaknutEmit();
			oakAsm->UXTH(oakWRegister(regt), oakWRegister(vireg));
			recEndOaknutEmit();
		}
		else
		{
			recCFC2LoadUnsigned16FromCpu_emit_oaknut(regt, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[_Rd_].UL)));
		}
	}
}

static void recCFC2()
{
	if (recCOP2TryInterpTransfer(CFC2))
		return;

	recCFC2_emit_oaknut();
}

static void recCTC2Store32_emit_oaknut(oak::WReg value, s64 offset)
{
	oakStore32(value, {oak::util::X27, offset});
}

static void recCTC2MoveRtToW_emit_oaknut(oak::WReg dst)
{
	recMoveGPRtoOakWForMacroVUControl(dst, _Rt_);
}

static void recCTC2WriteR_emit_oaknut()
{
	recBeginOaknutEmit();
	recCTC2MoveRtToW_emit_oaknut(OAK_WSCRATCH2);
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x7FFFFF);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x3F800000);
	recCTC2Store32_emit_oaknut(OAK_WSCRATCH2, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_R].UL)));
	recEndOaknutEmit();
}

static void recCTC2WriteStatus_emit_oaknut(int xmmtemp)
{
	recBeginOaknutEmit();

	recCTC2MoveRtToW_emit_oaknut(OAK_WSCRATCH2);
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0xFC0);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_STATUS_FLAG].UL))});
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x3F);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, OAK_WSCRATCH);
	recCTC2Store32_emit_oaknut(OAK_WSCRATCH2, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_STATUS_FLAG].UL)));

	oakAsm->MOV(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->LSR(OAK_WSCRATCH2, OAK_WSCRATCH, 3);
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x18);
	oakAsm->LSL(oak::util::W4, OAK_WSCRATCH, 11);
	oakAsm->AND(oak::util::W4, oak::util::W4, 0x1800);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, oak::util::W4);
	oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, 14);
	oakAsm->MOV(oak::util::W4, 0x3cf0000);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W4);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, OAK_WSCRATCH);

	oakAsm->DUP(oakQRegister(xmmtemp).S4(), OAK_WSCRATCH2);
	oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(offsetof(cpuRegistersPack, vuRegs[0].micro_statusflags)));
	oakAsm->ADD(OAK_XSCRATCH, oak::util::X27, OAK_XSCRATCH);
	oakAsm->STR(oakQRegister(xmmtemp), OAK_XSCRATCH);

	recEndOaknutEmit();
}

static void recCTC2WriteFbrst_emit_oaknut(int flagreg)
{
	recBeginOaknutEmit();

	const oak::WReg flag = oakWRegister(flagreg);
	oak::Label skip_vu0_reset;
	oak::Label skip_vu1_reset;

	oakAsm->TST(flag, 0x2);
	oakAsm->B(oak::util::EQ, skip_vu0_reset);
	oakEmitCall(reinterpret_cast<const void*>(vu0ResetRegs));
	oakAsm->l(skip_vu0_reset);

	oakAsm->TST(flag, 0x200);
	oakAsm->B(oak::util::EQ, skip_vu1_reset);
	oakEmitCall(reinterpret_cast<const void*>(vu1ResetRegs));
	oakAsm->l(skip_vu1_reset);

	oakAsm->MOV(OAK_WSCRATCH2, 0x0C0C);
	oakAsm->AND(flag, flag, OAK_WSCRATCH2);
	recCTC2Store32_emit_oaknut(flag, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_FBRST].UL)));

	recEndOaknutEmit();
}

static void recCTC2WriteNormalVI_emit_oaknut(int vireg)
{
	recBeginOaknutEmit();
	if (vireg >= 0)
	{
		recCTC2MoveRtToW_emit_oaknut(oakWRegister(vireg));
		oakAsm->UXTH(oakWRegister(vireg), oakWRegister(vireg));
	}
	else
	{
		recCTC2MoveRtToW_emit_oaknut(OAK_WSCRATCH2);
		oakAsm->UXTH(OAK_WSCRATCH2, OAK_WSCRATCH2);
		recCTC2Store32_emit_oaknut(OAK_WSCRATCH2, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[_Rd_].UL)));
	}
	recEndOaknutEmit();
}

static void recCTC2WriteI_emit_oaknut(int xmmreg)
{
	recBeginOaknutEmit();
	const oak::QReg dst = oakQRegister(xmmreg);

	if (_Rt_ == 0)
	{
		oakAsm->EOR(dst.B16(), dst.B16(), dst.B16());
	}
	else
	{
		const int xmmgpr = _checkXMMreg(XMMTYPE_GPRREG, _Rt_, MODE_READ);
		if (xmmgpr >= 0)
		{
			oakAsm->DUP(dst.S4(), oakQRegister(xmmgpr).Selem()[0]);
		}
		else
		{
			recCTC2MoveRtToW_emit_oaknut(OAK_WSCRATCH2);
			oakAsm->DUP(dst.S4(), OAK_WSCRATCH2);
		}
	}

	recEndOaknutEmit();
}

static void recCTC2WriteControl32_emit_oaknut()
{
	recBeginOaknutEmit();
	recCTC2MoveRtToW_emit_oaknut(OAK_WSCRATCH2);
	recCTC2Store32_emit_oaknut(OAK_WSCRATCH2, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[_Rd_].UL)));
	recEndOaknutEmit();
}

static void recCTC2_emit_oaknut()
{
	printCOP2("CTC2");

	COP2_Interlock(1);

	if (!_Rd_)
		return;

	if (!(cpuRegs.code & 1))
	{
		if (g_pCurInstInfo->info & EEINST_COP2_SYNC_VU0)
			mVUSyncVU0();
		else if (g_pCurInstInfo->info & EEINST_COP2_FINISH_VU0)
			mVUFinishVU0();
	}

	switch (_Rd_)
	{
		case REG_MAC_FLAG:
		case REG_TPC:
		case REG_VPU_STAT:
			break; // Read Only Regs
		case REG_R:
			recCTC2WriteR_emit_oaknut();
			break;
		case REG_STATUS_FLAG:
		{
			const int xmmtemp = _allocTempXMMreg(XMMT_INT);
			recCTC2WriteStatus_emit_oaknut(xmmtemp);
			_freeXMMreg(xmmtemp);
			break;
		}
		case REG_CMSAR1: // Execute VU1 Micro SubRoutine
			iFlushCall(FLUSH_NONE);
			recBeginOaknutEmit();
			oakAsm->MOV(oak::util::W0, 1);
			oakEmitCall(reinterpret_cast<const void*>(vu1Finish));
			recCTC2MoveRtToW_emit_oaknut(oak::util::W0);
			oakAsm->UXTH(oak::util::W0, oak::util::W0);
			oakEmitCall(reinterpret_cast<const void*>(vu1ExecMicro));
			recEndOaknutEmit();
			break;
		case REG_FBRST:
			{
				if (!_Rt_)
				{
					recBeginOaknutEmit();
					oakAsm->MOV(OAK_WSCRATCH2, 0);
					recCTC2Store32_emit_oaknut(OAK_WSCRATCH2, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_FBRST].UL)));
					recEndOaknutEmit();
					return;
				}

				const int flagreg = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
				recBeginOaknutEmit();
				recCTC2MoveRtToW_emit_oaknut(oakWRegister(flagreg));
				recEndOaknutEmit();

				iFlushCall(FLUSH_FREE_VU0);
				recCTC2WriteFbrst_emit_oaknut(flagreg);
				_freeX86reg(flagreg);
			}
			break;
		case 0:
			// Ignore writes to vi00.
			break;
		default:
			// Executing vu0 block here fixes the intro of Ratchet and Clank
			// sVU's COP2 has a comment that "Donald Duck" needs this too...
			if (_Rd_ < REG_STATUS_FLAG)
			{
				// Little bit nasty, but optimal codegen.
				const int vireg = _allocIfUsedVItoX86(_Rd_, MODE_WRITE);
				recCTC2WriteNormalVI_emit_oaknut(vireg);
			}
			else
			{
				// Move I direct to FPR if used.
				if (_Rd_ == REG_I)
				{
					const int xmmreg = _allocVFtoXMMreg(33, MODE_WRITE);
					recCTC2WriteI_emit_oaknut(xmmreg);
				}
				else
				{
					recCTC2WriteControl32_emit_oaknut();
				}
			}
			break;
	}
}

static void recCTC2()
{
	if (recCOP2TryInterpTransfer(CTC2))
		return;

	recCTC2_emit_oaknut();
}

static void recMoveQ_emit_oaknut(int dst, int src)
{
	recBeginOaknutEmit();
	const oak::QReg dst_q = oakQRegister(dst);
	const oak::QReg src_q = oakQRegister(src);
	oakAsm->ORR(dst_q.B16(), src_q.B16(), src_q.B16());
	recEndOaknutEmit();
}

static void recLoadQFromCpu_emit_oaknut(int dst, s64 offset)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(offset));
	oakAsm->ADD(OAK_XSCRATCH, oak::util::X27, OAK_XSCRATCH);
	oakAsm->LDR(oakQRegister(dst), OAK_XSCRATCH);
	recEndOaknutEmit();
}

static void recStoreQToCpu_emit_oaknut(int src, s64 offset)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(offset));
	oakAsm->ADD(OAK_XSCRATCH, oak::util::X27, OAK_XSCRATCH);
	oakAsm->STR(oakQRegister(src), OAK_XSCRATCH);
	recEndOaknutEmit();
}

static void recZeroQ_emit_oaknut(int dst)
{
	recBeginOaknutEmit();
	const oak::QReg dst_q = oakQRegister(dst);
	oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
	recEndOaknutEmit();
}

static void recQMFC2_emit_oaknut()
{

	printCOP2("QMFC2");

	COP2_Interlock(false);

	if (!_Rt_)
		return;

	if (!(cpuRegs.code & 1))
	{
		if (g_pCurInstInfo->info & EEINST_COP2_SYNC_VU0)
			mVUSyncVU0();
		else if (g_pCurInstInfo->info & EEINST_COP2_FINISH_VU0)
			mVUFinishVU0();
	}

	const bool vf_used = EEINST_VFUSEDTEST(_Rd_);
	const int ftreg = _allocVFtoXMMreg(_Rd_, MODE_READ);
	_deleteEEreg128(_Rt_);

	// const flag should've been cleared, but sanity check..
	pxAssert(!GPR_IS_CONST1(_Rt_));

	if (vf_used)
	{
		// store direct to state if rt is not used
		const int rtreg = _allocIfUsedGPRtoXmmReg(_Rt_, MODE_WRITE);
		if (rtreg >= 0)
			recMoveQ_emit_oaknut(rtreg, ftreg);
		else
			recStoreQToCpu_emit_oaknut(ftreg, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UQ)));

		// don't cache vf00, microvu doesn't like it
		if (_Rd_ == 0)
			_freeXMMreg(ftreg);
	}
	else
	{
		_reallocateXMMreg(ftreg, XMMTYPE_GPRREG, _Rt_, MODE_WRITE, true);
	}
}

static void recQMFC2()
{
	if (recCOP2TryInterpTransfer(QMFC2))
		return;

	recQMFC2_emit_oaknut();
}

static void recQMTC2_emit_oaknut()
{
	printCOP2("QMTC2");
	COP2_Interlock(true);

	if (!_Rd_)
		return;

	if (!(cpuRegs.code & 1))
	{
		if (g_pCurInstInfo->info & EEINST_COP2_SYNC_VU0)
			mVUSyncVU0();
		else if (g_pCurInstInfo->info & EEINST_COP2_FINISH_VU0)
			mVUFinishVU0();
	}

	if (_Rt_)
	{
		// if we have to flush to memory anyway (has a constant or is already in a host GPR), force load.
		[[maybe_unused]] const bool vf_used = EEINST_VFUSEDTEST(_Rd_);
		const bool can_rename = EEINST_RENAMETEST(_Rt_);
		const int rtreg = (GPR_IS_DIRTY_CONST(_Rt_) || _hasX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE)) ?
							  _allocGPRtoXMMreg(_Rt_, MODE_READ) :
                              _checkXMMreg(XMMTYPE_GPRREG, _Rt_, MODE_READ);

		// NOTE: can't transfer xmm15 to VF, it's reserved for PQ.
		int vfreg = _checkXMMreg(XMMTYPE_VFREG, _Rd_, MODE_WRITE);
		if (can_rename && rtreg >= 0 && rtreg != VU_HOST_XMMPQ)
		{
			// rt is no longer needed, so transfer to VF.
			if (vfreg >= 0)
				_freeXMMregWithoutWriteback(vfreg);
			_reallocateXMMreg(rtreg, XMMTYPE_VFREG, _Rd_, MODE_WRITE, true);
		}
		else
		{
			// copy to VF.
			if (vfreg < 0)
				vfreg = _allocVFtoXMMreg(_Rd_, MODE_WRITE);
			if (rtreg >= 0)
				recMoveQ_emit_oaknut(vfreg, rtreg);
			else
				recLoadQFromCpu_emit_oaknut(vfreg, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UQ)));
		}
	}
	else
	{
		const int vfreg = _allocVFtoXMMreg(_Rd_, MODE_WRITE);
		recZeroQ_emit_oaknut(vfreg);
	}
}

static void recQMTC2()
{
	if (recCOP2TryInterpTransfer(QMTC2))
		return;

	recQMTC2_emit_oaknut();
}

//------------------------------------------------------------------
// Macro VU - Tables
//------------------------------------------------------------------

void recCOP2();
void recCOP2_BC2();
void recCOP2_SPEC1();
void recCOP2_SPEC2();
void rec_C2UNK()
{
	Console.Error("Cop2 bad opcode: %x", cpuRegs.code);
}

// Recompilation
void (*recCOP2t[32])() = {
	rec_C2UNK,     recQMFC2,      recCFC2,       rec_C2UNK,     rec_C2UNK,     recQMTC2,      recCTC2,       rec_C2UNK,
	recCOP2_BC2,   rec_C2UNK,     rec_C2UNK,     rec_C2UNK,     rec_C2UNK,     rec_C2UNK,     rec_C2UNK,     rec_C2UNK,
	recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1,
	recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1, recCOP2_SPEC1,
};

void (*recCOP2_BC2t[32])() = {
	recBC2F,   recBC2T,   recBC2FL,  recBC2TL,  rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK,
	rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK,
	rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK,
	rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK, rec_C2UNK,
};

void (*recCOP2SPECIAL1t[64])() = {
	recVADDx,   recVADDy,   recVADDz,  recVADDw,  recVSUBx,      recVSUBy,      recVSUBz,      recVSUBw,
	recVMADDx,  recVMADDy,  recVMADDz, recVMADDw, recVMSUBx,     recVMSUBy,     recVMSUBz,     recVMSUBw,
	recVMAXx,   recVMAXy,   recVMAXz,  recVMAXw,  recVMINIx,     recVMINIy,     recVMINIz,     recVMINIw,
	recVMULx,   recVMULy,   recVMULz,  recVMULw,  recVMULq,      recVMAXi,      recVMULi,      recVMINIi,
	recVADDq,   recVMADDq,  recVADDi,  recVMADDi, recVSUBq,      recVMSUBq,     recVSUBi,      recVMSUBi,
	recVADD,    recVMADD,   recVMUL,   recVMAX,   recVSUB,       recVMSUB,      recVOPMSUB,    recVMINI,
	recVIADD,   recVISUB,   recVIADDI, rec_C2UNK, recVIAND,      recVIOR,       rec_C2UNK,     rec_C2UNK,
	recVCALLMS, recVCALLMSR,rec_C2UNK, rec_C2UNK, recCOP2_SPEC2, recCOP2_SPEC2, recCOP2_SPEC2, recCOP2_SPEC2,
};

void (*recCOP2SPECIAL2t[128])() = {
	recVADDAx,  recVADDAy, recVADDAz,  recVADDAw,  recVSUBAx,  recVSUBAy,  recVSUBAz,  recVSUBAw,
	recVMADDAx,recVMADDAy, recVMADDAz, recVMADDAw, recVMSUBAx, recVMSUBAy, recVMSUBAz, recVMSUBAw,
	recVITOF0,  recVITOF4, recVITOF12, recVITOF15, recVFTOI0,  recVFTOI4,  recVFTOI12, recVFTOI15,
	recVMULAx,  recVMULAy, recVMULAz,  recVMULAw,  recVMULAq,  recVABS,    recVMULAi,  recVCLIP,
	recVADDAq,  recVMADDAq,recVADDAi,  recVMADDAi, recVSUBAq,  recVMSUBAq, recVSUBAi,  recVMSUBAi,
	recVADDA,   recVMADDA, recVMULA,   rec_C2UNK,  recVSUBA,   recVMSUBA,  recVOPMULA, recVNOP,
	recVMOVE,   recVMR32,  rec_C2UNK,  rec_C2UNK,  recVLQI,    recVSQI,    recVLQD,    recVSQD,
	recVDIV,    recVSQRT,  recVRSQRT,  recVWAITQ,  recVMTIR,   recVMFIR,   recVILWR,   recVISWR,
	recVRNEXT,  recVRGET,  recVRINIT,  recVRXOR,   rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,
	rec_C2UNK,  rec_C2UNK, rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,
	rec_C2UNK,  rec_C2UNK, rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,
	rec_C2UNK,  rec_C2UNK, rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,
	rec_C2UNK,  rec_C2UNK, rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,
	rec_C2UNK,  rec_C2UNK, rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,
	rec_C2UNK,  rec_C2UNK, rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,
	rec_C2UNK,  rec_C2UNK, rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,  rec_C2UNK,
};

namespace R5900 {
namespace Dynarec {
namespace OpcodeImpl {
void recCOP2()
{
	if (VU0_FORCE_COP2_SYNC_TEST)
		mVUFinishVU0();

	recCOP2t[_Rs_]();
}

#if defined(LOADSTORE_RECOMPILE) && defined(CP2_RECOMPILE)

/*********************************************************
* Load and store for COP2 (VU0 unit)                     *
* Format:  OP rt, offset(base)                           *
*********************************************************/

static void recMoveGPRtoOakWForMacroVU(oak::WReg to, int fromgpr)
{
	if (fromgpr == 0)
	{
		oakAsm->MOV(to, 0);
		return;
	}

	if (GPR_IS_CONST1(fromgpr))
	{
		oakAsm->MOV(to, g_cpuConstRegs[fromgpr].UL[0]);
		return;
	}

	const int gprreg = _checkX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
	if (gprreg >= 0)
	{
		oakAsm->MOV(to, oakWRegister(gprreg));
		return;
	}

	const int xmmreg = _checkXMMreg(XMMTYPE_GPRREG, fromgpr, MODE_READ);
	if (xmmreg >= 0)
	{
		oakAsm->FMOV(to, oakSRegister(xmmreg));
		return;
	}

	oakLoad32(to, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[fromgpr].UL[0]))});
}

static void recLQC2AddressToECX_emit_oaknut()
{
	_freeX86reg(VU_HOST_T2);

	recBeginOaknutEmit();

	const oak::WReg addr = oakWRegister(VU_HOST_T2);
	recMoveGPRtoOakWForMacroVU(addr, _Rs_);
	if (_Imm_ != 0)
	{
		oakAsm->MOV(oak::util::W4, static_cast<u32>(_Imm_));
		oakAsm->ADD(addr, addr, oak::util::W4);
	}
	oakAsm->AND(addr, addr, oak::BitImm32(~0x0fu));

	recEndOaknutEmit();
}

static void recSQC2AddressToECX_emit_oaknut()
{
	_freeX86reg(VU_HOST_T2);

	recBeginOaknutEmit();

	const oak::WReg addr = oakWRegister(VU_HOST_T2);
	if (_Rs_ != 0)
	{
		recMoveGPRtoOakWForMacroVU(addr, _Rs_);
		if (_Imm_ != 0)
		{
			oakAsm->MOV(oak::util::W4, static_cast<u32>(_Imm_));
			oakAsm->ADD(addr, addr, oak::util::W4);
		}
	}
	else
	{
		oakAsm->MOV(addr, static_cast<u32>(_Imm_));
	}
	oakAsm->AND(addr, addr, oak::BitImm32(~0x0fu));

	recEndOaknutEmit();
}

static void recSQC2VF00ToTemp_emit_oaknut(int ftreg)
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(offsetof(cpuRegistersPack, vuRegs[0].VF[0].F)));
	oakAsm->ADD(OAK_XSCRATCH, oak::util::X27, OAK_XSCRATCH);
	oakAsm->LDR(oakQRegister(ftreg), OAK_XSCRATCH);

	recEndOaknutEmit();
}

static void recLQC2_emit_oaknut()
{
	if (g_pCurInstInfo->info & EEINST_COP2_SYNC_VU0)
		mVUSyncVU0();
	else if (g_pCurInstInfo->info & EEINST_COP2_FINISH_VU0)
		mVUFinishVU0();

	vtlb_ReadRegAllocCallback alloc_cb = nullptr;
	if (_Rt_)
	{
		// init regalloc after flush
		alloc_cb = []() { return _allocVFtoXMMreg(_Rt_, MODE_WRITE); };
	}

	int xmmreg;
	if (GPR_IS_CONST1(_Rs_))
	{
		const u32 addr = (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0xFu;
		xmmreg = vtlb_DynGenReadQuad_Const(128, addr, alloc_cb);
	}
	else
	{
		recLQC2AddressToECX_emit_oaknut();
		xmmreg = vtlb_DynGenReadQuad(128, VU_HOST_T2, alloc_cb);
	}

	// toss away if loading to vf00
	if (!_Rt_)
		_freeXMMreg(xmmreg);
}

void recLQC2()
{
	if (VU0_FORCE_COP2_SYNC_TEST)
		mVUSyncVU0();

	if (recCOP2TryInterpTransfer(R5900::Interpreter::OpcodeImpl::LQC2))
	{
		EE::Profiler.EmitOp(eeOpcode::LQC2);
		return;
	}

	recLQC2_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LQC2);
}

////////////////////////////////////////////////////

static void recSQC2_emit_oaknut()
{
	if (g_pCurInstInfo->info & EEINST_COP2_SYNC_VU0)
		mVUSyncVU0();
	else if (g_pCurInstInfo->info & EEINST_COP2_FINISH_VU0)
		mVUFinishVU0();

	// vf00 has to be special cased here, because of the microvu temps...
	const int ftreg = _Rt_ ? _allocVFtoXMMreg(_Rt_, MODE_READ) : _allocTempXMMreg(XMMT_FPS);
	if (!_Rt_)
	{
		recSQC2VF00ToTemp_emit_oaknut(ftreg);
	}

	if (GPR_IS_CONST1(_Rs_))
	{
		const u32 addr = (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0xFu;
		vtlb_DynGenWrite_Const(128, true, addr, ftreg);
	}
	else
	{
		recSQC2AddressToECX_emit_oaknut();
		vtlb_DynGenWrite(128, true, VU_HOST_T2, ftreg);
	}

	if (!_Rt_)
		_freeXMMreg(ftreg);
}

void recSQC2()
{
	if (VU0_FORCE_COP2_SYNC_TEST)
		mVUSyncVU0();

	if (recCOP2TryInterpTransfer(R5900::Interpreter::OpcodeImpl::SQC2))
	{
		EE::Profiler.EmitOp(eeOpcode::SQC2);
		return;
	}

	recSQC2_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SQC2);
}

#else
//namespace Interp = R5900::Interpreter::OpcodeImpl;

//REC_FUNC(LQC2);
//REC_FUNC(SQC2);

#endif

} // namespace OpcodeImpl
} // namespace Dynarec
} // namespace R5900
void recCOP2_BC2() { recCOP2_BC2t[_Rt_](); }
void recCOP2_SPEC1()
{
	if (g_pCurInstInfo->info & (EEINST_COP2_SYNC_VU0 | EEINST_COP2_FINISH_VU0))
		mVUFinishVU0();

	if (recCOP2TryInterpSPEC1())
		return;

	recCOP2SPECIAL1t[_Funct_]();

}
void recCOP2_SPEC2()
{
	if (recCOP2TryInterpSPEC2())
		return;

	recCOP2SPECIAL2t[(cpuRegs.code & 3) | ((cpuRegs.code >> 4) & 0x7c)]();
}
