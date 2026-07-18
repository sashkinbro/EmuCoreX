// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "R5900OpcodeTables.h"
#include "arm64/ee/iR5900-arm64.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

namespace R5900::Dynarec::OpcodeImpl
{
/*********************************************************
* Register branch logic                                  *
* Format:  OP rs, rt, offset                             *
*********************************************************/
#ifndef BRANCH_RECOMPILE

namespace Interp = R5900::Interpreter::OpcodeImpl;

REC_SYS(BEQ);
REC_SYS(BEQL);
REC_SYS(BNE);
REC_SYS(BNEL);
REC_SYS(BLTZ);
REC_SYS(BGTZ);
REC_SYS(BLEZ);
REC_SYS(BGEZ);
REC_SYS(BGTZL);
REC_SYS(BLTZL);
REC_SYS_DEL(BLTZAL, 31);
REC_SYS_DEL(BLTZALL, 31);
REC_SYS(BLEZL);
REC_SYS(BGEZL);
REC_SYS_DEL(BGEZAL, 31);
REC_SYS_DEL(BGEZALL, 31);

#else

static void recStoreBranchLinkRA_emit_oaknut(u32 link)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_XSCRATCH, link);
	oakStore64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[31].UD[0]))});
	recEndOaknutEmit();
}

static void recSetBranchZeroCompare_emit_oaknut(int regs)
{
	recBeginOaknutEmit();
	if (regs >= 0)
	{
		oakAsm->CMP(oakXRegister(regs), 0);
	}
	else
	{
		oakLoad64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});
		oakAsm->CMP(OAK_XSCRATCH, 0);
	}
	recEndOaknutEmit();
}

static void recSetBranchSignXmmCompare_emit_oaknut(int regsxmm)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_XSCRATCH, 0x0000000200000001ULL);
	oakAsm->INS(OAK_QSCRATCH2.Delem()[0], OAK_XSCRATCH);
	oakAsm->MOV(OAK_XSCRATCH, 0x0000000800000004ULL);
	oakAsm->INS(OAK_QSCRATCH2.Delem()[1], OAK_XSCRATCH);
	oakAsm->SSHR(OAK_QSCRATCH.S4(), oakQRegister(regsxmm).S4(), 31);
	oakAsm->AND(OAK_QSCRATCH.B16(), OAK_QSCRATCH2.B16(), OAK_QSCRATCH.B16());
	oakAsm->ADDV(OAK_SSCRATCH, OAK_QSCRATCH.S4());
	oakAsm->FMOV(OAK_WSCRATCH, OAK_SSCRATCH);
	oakAsm->TST(OAK_WSCRATCH, 2);
	recEndOaknutEmit();
}

struct RecBranchPatchpoint
{
	u8* branch;
	oak::Cond cond;
};

static u8* recBranchPatchpoint_emit_oaknut()
{
	u8* branch = nullptr;
	recBeginOaknutEmit();
	branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return branch;
}

template <int Process>
static u8* recSetBranchEQExact_emit_oaknut()
{
	// TODO(Stenzek): This is suboptimal if the registers are in ARM64 vector regs.
	// If the constant register is already in a host register, we don't need the immediate...

	if constexpr ((Process & PROCESS_CONSTS) != 0)
	{
		_eeFlushAllDirty();

		_deleteGPRtoXMMreg(_Rt_, DELETE_REG_FLUSH_AND_FREE);
		const int regt = _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
		recBeginOaknutEmit();
		oakAsm->MOV(OAK_XSCRATCH2, g_cpuConstRegs[_Rs_].UD[0]);
		if (regt >= 0)
		{
			oakAsm->CMP(oakXRegister(regt), OAK_XSCRATCH2);
		}
		else
		{
			oakLoad64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UD[0]))});
			oakAsm->CMP(OAK_XSCRATCH, OAK_XSCRATCH2);
		}
		recEndOaknutEmit();
	}
	else if constexpr ((Process & PROCESS_CONSTT) != 0)
	{
		_eeFlushAllDirty();

		_deleteGPRtoXMMreg(_Rs_, DELETE_REG_FLUSH_AND_FREE);
		const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
		recBeginOaknutEmit();
		oakAsm->MOV(OAK_XSCRATCH2, g_cpuConstRegs[_Rt_].UD[0]);
		if (regs >= 0)
		{
			oakAsm->CMP(oakXRegister(regs), OAK_XSCRATCH2);
		}
		else
		{
			oakLoad64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});
			oakAsm->CMP(OAK_XSCRATCH, OAK_XSCRATCH2);
		}
		recEndOaknutEmit();
	}
	else
	{
		// force S into register, since we need to load it, may as well cache.
		_deleteGPRtoXMMreg(_Rt_, DELETE_REG_FLUSH_AND_FREE);
		const int regs = _allocX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
		const int regt = _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
		_eeFlushAllDirty();

		recBeginOaknutEmit();
		if (regt >= 0)
		{
			oakAsm->CMP(oakXRegister(regs), oakXRegister(regt));
		}
		else
		{
			oakLoad64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UD[0]))});
			oakAsm->CMP(oakXRegister(regs), OAK_XSCRATCH);
		}
		recEndOaknutEmit();
	}

	return recBranchPatchpoint_emit_oaknut();
}

template <bool BranchOnLessThanZero, bool FlushBeforeCompare = true>
static RecBranchPatchpoint recSetBranchSignExact_emit_oaknut()
{
	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	const int regsxmm = _checkXMMreg(XMMTYPE_GPRREG, _Rs_, MODE_READ);
	if constexpr (FlushBeforeCompare)
		_eeFlushAllDirty();

	if (regsxmm >= 0)
	{
		recSetBranchSignXmmCompare_emit_oaknut(regsxmm);
		return {recBranchPatchpoint_emit_oaknut(), BranchOnLessThanZero ? oak::Cond::EQ : oak::Cond::NE};
	}

	recBeginOaknutEmit();
	if (regs >= 0)
	{
		oakAsm->CMP(oakXRegister(regs), 0);
	}
	else
	{
		oakLoad64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});
		oakAsm->CMP(OAK_XSCRATCH, 0);
	}
	recEndOaknutEmit();

	return {recBranchPatchpoint_emit_oaknut(), BranchOnLessThanZero ? oak::Cond::GE : oak::Cond::LT};
}

static bool recResolveTakenBeqDelayChain(u32 delay_pc, u32* branchTo)
{
	u32 cursor = delay_pc;
	bool resolved = false;

	for (int depth = 0; depth < 8; depth++)
	{
		const u32 code = *reinterpret_cast<u32*>(PSM(cursor));
		const u32 opcode = code >> 26;
		const u32 rs = (code >> 21) & 0x1f;
		const u32 rt = (code >> 16) & 0x1f;
		if (opcode != 4 || rs != rt)
			break;

		*branchTo = cursor + 4 + (static_cast<s32>(static_cast<s16>(code & 0xffff)) * 4);
		cursor += 4;
		resolved = true;
	}

	return resolved && (*reinterpret_cast<u32*>(PSM(cursor)) == 0);
}

static void recBEQ_const()
{
	u32 branchTo;

	if (g_cpuConstRegs[_Rs_].SD[0] == g_cpuConstRegs[_Rt_].SD[0])
	{
		branchTo = ((s32)_Imm_ * 4) + pc;
		if (recResolveTakenBeqDelayChain(pc, &branchTo))
		{
			SetBranchImm(branchTo);
			return;
		}
	}
	else
	{
		branchTo = pc + 4;
	}

	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);
}

template <int Process>
static void recBEQ_process()
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (_Rs_ == _Rt_)
	{
		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
	}
	else
	{
		const bool swap = TrySwapDelaySlot(_Rs_, _Rt_, 0, true);

		u8* j32Ptr = recSetBranchEQExact_emit_oaknut<Process>();

		if (!swap)
		{
			SaveBranchState();
			recompileNextInstruction(true, false);
		}

		SetBranchImm(branchTo);

		oakPatchCondBranch(j32Ptr, oakGetCurrentCodePointer(), oak::Cond::NE, false);

		if (!swap)
		{
			// recopy the next inst
			pc -= 4;
			LoadBranchState();
			recompileNextInstruction(true, false);
		}

		SetBranchImm(pc);
	}
}

void recBEQ()
{
	// prefer using the host register over an immediate, it'll be smaller code.
	if (GPR_IS_CONST2(_Rs_, _Rt_))
		recBEQ_const();
	else if (GPR_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) < 0)
		recBEQ_process<PROCESS_CONSTS>();
	else if (GPR_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ) < 0)
		recBEQ_process<PROCESS_CONSTT>();
	else
		recBEQ_process<0>();
}

//// BNE
static void recBNE_const()
{
	u32 branchTo;

	if (g_cpuConstRegs[_Rs_].SD[0] != g_cpuConstRegs[_Rt_].SD[0])
		branchTo = ((s32)_Imm_ * 4) + pc;
	else
		branchTo = pc + 4;

	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);
}

template <int Process>
static void recBNE_process()
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (_Rs_ == _Rt_)
	{
		recompileNextInstruction(true, false);
		SetBranchImm(pc);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, _Rt_, 0, true);

	u8* j32Ptr = recSetBranchEQExact_emit_oaknut<Process>();

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr, oakGetCurrentCodePointer(), oak::Cond::EQ, false);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

void recBNE()
{
	if (GPR_IS_CONST2(_Rs_, _Rt_))
		recBNE_const();
	else if (GPR_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) < 0)
		recBNE_process<PROCESS_CONSTS>();
	else if (GPR_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ) < 0)
		recBNE_process<PROCESS_CONSTT>();
	else
		recBNE_process<0>();
}

//// BEQL
static void recBEQL_const()
{
	if (g_cpuConstRegs[_Rs_].SD[0] == g_cpuConstRegs[_Rt_].SD[0])
	{
		u32 branchTo = ((s32)_Imm_ * 4) + pc;
		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
	}
	else
	{
		SetBranchImm(pc + 4);
	}
}

template <int Process>
static void recBEQL_process()
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	u8* j32Ptr = recSetBranchEQExact_emit_oaknut<Process>();

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr, oakGetCurrentCodePointer(), oak::Cond::NE, false);

	LoadBranchState();
	SetBranchImm(pc);
}

void recBEQL()
{
	if (GPR_IS_CONST2(_Rs_, _Rt_))
		recBEQL_const();
	else if (GPR_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) < 0)
		recBEQL_process<PROCESS_CONSTS>();
	else if (GPR_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ) < 0)
		recBEQL_process<PROCESS_CONSTT>();
	else
		recBEQL_process<0>();
}

//// BNEL
static void recBNEL_const()
{
	if (g_cpuConstRegs[_Rs_].SD[0] != g_cpuConstRegs[_Rt_].SD[0])
	{
		u32 branchTo = ((s32)_Imm_ * 4) + pc;
		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
	}
	else
	{
		SetBranchImm(pc + 4);
	}
}

template <int Process>
static void recBNEL_process()
{
	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	u8* j32Ptr = recSetBranchEQExact_emit_oaknut<Process>();

	SaveBranchState();
	SetBranchImm(pc + 4);

	oakPatchCondBranch(j32Ptr, oakGetCurrentCodePointer(), oak::Cond::NE, false);

	// recopy the next inst
	LoadBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);
}

void recBNEL()
{
	if (GPR_IS_CONST2(_Rs_, _Rt_))
		recBNEL_const();
	else if (GPR_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) < 0)
		recBNEL_process<PROCESS_CONSTS>();
	else if (GPR_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ) < 0)
		recBNEL_process<PROCESS_CONSTT>();
	else
		recBNEL_process<0>();
}

/*********************************************************
* Register branch logic                                  *
* Format:  OP rs, offset                                 *
*********************************************************/

////////////////////////////////////////////////////
//void recBLTZAL()
//{
//	_eeFlushAllUnused();
//	iFlushCall(FLUSH_EVERYTHING);
//	branch = 2;
//}

////////////////////////////////////////////////////
void recBLTZAL()
{
	EE::Profiler.EmitOp(eeOpcode::BLTZAL);

	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	_eeOnWriteReg(31, 0);
	_eeFlushAllDirty();

	_deleteEEreg(31, 0);
	recStoreBranchLinkRA_emit_oaknut(pc + 4);

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] < 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);

	const RecBranchPatchpoint j32Ptr = recSetBranchSignExact_emit_oaknut<true>();

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr.branch, oakGetCurrentCodePointer(), j32Ptr.cond, false);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBGEZAL()
{
	EE::Profiler.EmitOp(eeOpcode::BGEZAL);

	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	_eeOnWriteReg(31, 0);
	_eeFlushAllDirty();

	_deleteEEreg(31, 0);
	recStoreBranchLinkRA_emit_oaknut(pc + 4);

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] >= 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);

	const RecBranchPatchpoint j32Ptr = recSetBranchSignExact_emit_oaknut<false>();

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr.branch, oakGetCurrentCodePointer(), j32Ptr.cond, false);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBLTZALL()
{
	EE::Profiler.EmitOp(eeOpcode::BLTZALL);

	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	_eeOnWriteReg(31, 0);
	_eeFlushAllDirty();

	_deleteEEreg(31, 0);
	recStoreBranchLinkRA_emit_oaknut(pc + 4);

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] < 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	const RecBranchPatchpoint j32Ptr = recSetBranchSignExact_emit_oaknut<true, false>();

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr.branch, oakGetCurrentCodePointer(), j32Ptr.cond, false);

	LoadBranchState();
	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBGEZALL()
{
	EE::Profiler.EmitOp(eeOpcode::BGEZALL);

	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	_eeOnWriteReg(31, 0);
	_eeFlushAllDirty();

	_deleteEEreg(31, 0);
	recStoreBranchLinkRA_emit_oaknut(pc + 4);

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] >= 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	const RecBranchPatchpoint j32Ptr = recSetBranchSignExact_emit_oaknut<false, false>();

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr.branch, oakGetCurrentCodePointer(), j32Ptr.cond, false);

	LoadBranchState();
	SetBranchImm(pc);
}


//// BLEZ
void recBLEZ()
{
	EE::Profiler.EmitOp(eeOpcode::BLEZ);

	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] <= 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);
	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	recSetBranchZeroCompare_emit_oaknut(regs);

	u8* j32Ptr = recBranchPatchpoint_emit_oaknut();

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr, oakGetCurrentCodePointer(), oak::Cond::GT, false);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

//// BGTZ
void recBGTZ()
{
	EE::Profiler.EmitOp(eeOpcode::BGTZ);

	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] > 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);
	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	recSetBranchZeroCompare_emit_oaknut(regs);

	u8* j32Ptr = recBranchPatchpoint_emit_oaknut();

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr, oakGetCurrentCodePointer(), oak::Cond::LE, false);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBLTZ()
{
	EE::Profiler.EmitOp(eeOpcode::BLTZ);

	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] < 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);

	const RecBranchPatchpoint j32Ptr = recSetBranchSignExact_emit_oaknut<true>();

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr.branch, oakGetCurrentCodePointer(), j32Ptr.cond, false);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBGEZ()
{
	EE::Profiler.EmitOp(eeOpcode::BGEZ);

	u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] >= 0))
			branchTo = pc + 4;

		recompileNextInstruction(true, false);
		SetBranchImm(branchTo);
		return;
	}

	const bool swap = TrySwapDelaySlot(_Rs_, 0, 0, true);

	const RecBranchPatchpoint j32Ptr = recSetBranchSignExact_emit_oaknut<false>();

	if (!swap)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr.branch, oakGetCurrentCodePointer(), j32Ptr.cond, false);

	if (!swap)
	{
		// recopy the next inst
		pc -= 4;
		LoadBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBLTZL()
{
	EE::Profiler.EmitOp(eeOpcode::BLTZL);

	const u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] < 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	const RecBranchPatchpoint j32Ptr = recSetBranchSignExact_emit_oaknut<true>();

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr.branch, oakGetCurrentCodePointer(), j32Ptr.cond, false);

	LoadBranchState();
	SetBranchImm(pc);
}


////////////////////////////////////////////////////
void recBGEZL()
{
	EE::Profiler.EmitOp(eeOpcode::BGEZL);

	const u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] >= 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	const RecBranchPatchpoint j32Ptr = recSetBranchSignExact_emit_oaknut<false>();

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr.branch, oakGetCurrentCodePointer(), j32Ptr.cond, false);

	LoadBranchState();
	SetBranchImm(pc);
}



/*********************************************************
* Register branch logic  Likely                          *
* Format:  OP rs, offset                                 *
*********************************************************/

////////////////////////////////////////////////////
void recBLEZL()
{
	EE::Profiler.EmitOp(eeOpcode::BLEZL);

	const u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] <= 0))
			SetBranchImm(pc + 4);
		else
		{
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	recSetBranchZeroCompare_emit_oaknut(regs);

	u8* j32Ptr = recBranchPatchpoint_emit_oaknut();

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr, oakGetCurrentCodePointer(), oak::Cond::GT, false);

	LoadBranchState();
	SetBranchImm(pc);
}

////////////////////////////////////////////////////
void recBGTZL()
{
	EE::Profiler.EmitOp(eeOpcode::BGTZL);

	const u32 branchTo = ((s32)_Imm_ * 4) + pc;

	if (GPR_IS_CONST1(_Rs_))
	{
		if (!(g_cpuConstRegs[_Rs_].SD[0] > 0))
			SetBranchImm(pc + 4);
		else
		{
			_clearNeededXMMregs();
			recompileNextInstruction(true, false);
			SetBranchImm(branchTo);
		}
		return;
	}

	const int regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	_eeFlushAllDirty();

	recSetBranchZeroCompare_emit_oaknut(regs);

	u8* j32Ptr = recBranchPatchpoint_emit_oaknut();

	SaveBranchState();
	recompileNextInstruction(true, false);
	SetBranchImm(branchTo);

	oakPatchCondBranch(j32Ptr, oakGetCurrentCodePointer(), oak::Cond::LE, false);

	LoadBranchState();
	SetBranchImm(pc);
}

#endif

} // namespace R5900::Dynarec::OpcodeImpl
