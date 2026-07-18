// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "Cache.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/ee/iR5900-arm64.h"
#include "R5900OpcodeTables.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

namespace R5900 {
namespace Dynarec {

void recDoBranchImmOaknut(u32 branchTo, oak::Cond skipCond, bool isLikely, bool swappedDelaySlot)
{
	u8* skip_branch = nullptr;
	recBeginOaknutEmit();
	skip_branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();

	if (!swappedDelaySlot)
	{
		SaveBranchState();
		recompileNextInstruction(true, false);
	}

	SetBranchImm(branchTo);

	u8* skip_target = oakGetCurrentCodePointer();
	oakPatchCondBranch(skip_branch, skip_target, skipCond, false);

	if (!swappedDelaySlot)
	{
		LoadBranchState();
		if (!isLikely)
		{
			pc -= 4;
			recompileNextInstruction(true, false);
		}
	}

	SetBranchImm(pc);
}

namespace OpcodeImpl {

static void recMiscMoveGPRtoOakW(oak::WReg to, int fromgpr)
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

static void recMiscMoveGPRtoOakX(oak::XReg to, int fromgpr)
{
	if (fromgpr == 0)
	{
		oakAsm->MOV(to, 0);
		return;
	}

	if (GPR_IS_CONST1(fromgpr))
	{
		oakAsm->MOV(to, g_cpuConstRegs[fromgpr].UD[0]);
		return;
	}

	const int gprreg = _checkX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
	if (gprreg >= 0)
	{
		oakAsm->MOV(to, oakXRegister(gprreg));
		return;
	}

	const int xmmreg = _checkXMMreg(XMMTYPE_GPRREG, fromgpr, MODE_READ);
	if (xmmreg >= 0)
	{
		oakAsm->FMOV(to, oakDRegister(xmmreg));
		return;
	}

	oakLoad64(to, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[fromgpr].UD[0]))});
}

static void recTrapScheduleImmediateTest_emit_oaknut()
{
	recBeginOaknutEmit();
	recFlushReccycle();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	recReloadReccycle();
	recEndOaknutEmit();
}

static void recTrapException_emit_oaknut()
{
	recBeginOaknutEmit();
	if (!g_recompilingDelaySlot)
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
		oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, 4);
		oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	}
	oakAsm->MOV(OAK_WARG1, 0x34);
	oakLoad32(OAK_WARG2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.branch))});
	oakEmitCall(reinterpret_cast<void*>(cpuException));
	recEndOaknutEmit();
}

static void recTrapTakenPath_emit_oaknut(oak::Label& no_trap)
{
	_x86regs saved_x86regs[iREGCNT_GPR];
	_xmmregs saved_xmmregs[iREGCNT_XMM];
	memcpy(saved_x86regs, x86regs, sizeof(saved_x86regs));
	memcpy(saved_xmmregs, xmmregs, sizeof(saved_xmmregs));
	const u32 saved_has_const = g_cpuHasConstReg;
	const u32 saved_flushed_const = g_cpuFlushedConstReg;
	const bool saved_flushed_pc = g_cpuFlushedPC;
	const bool saved_flushed_code = g_cpuFlushedCode;

	iFlushCall(FLUSH_INTERPRETER);
	recTrapScheduleImmediateTest_emit_oaknut();
	recTrapException_emit_oaknut();
	recEmitExceptionExit();

	memcpy(x86regs, saved_x86regs, sizeof(saved_x86regs));
	memcpy(xmmregs, saved_xmmregs, sizeof(saved_xmmregs));
	g_cpuHasConstReg = saved_has_const;
	g_cpuFlushedConstReg = saved_flushed_const;
	g_cpuFlushedPC = saved_flushed_pc;
	g_cpuFlushedCode = saved_flushed_code;

	recBeginOaknutEmit();
	oakAsm->l(no_trap);
	recEndOaknutEmit();
}

static void recTrapReg_emit_oaknut(oak::Cond skip_cond)
{
	oak::Label no_trap;

	recBeginOaknutEmit();
	recMiscMoveGPRtoOakX(OAK_XSCRATCH, _Rs_);
	recMiscMoveGPRtoOakX(OAK_XSCRATCH2, _Rt_);
	oakAsm->CMP(OAK_XSCRATCH, OAK_XSCRATCH2);
	oakAsm->B(skip_cond, no_trap);
	recEndOaknutEmit();

	recTrapTakenPath_emit_oaknut(no_trap);
}

static void recTrapImm_emit_oaknut(oak::Cond skip_cond)
{
	oak::Label no_trap;

	recBeginOaknutEmit();
	recMiscMoveGPRtoOakX(OAK_XSCRATCH, _Rs_);
	oakAsm->MOV(OAK_XSCRATCH2, static_cast<u64>(static_cast<s64>(_Imm_)));
	oakAsm->CMP(OAK_XSCRATCH, OAK_XSCRATCH2);
	oakAsm->B(skip_cond, no_trap);
	recEndOaknutEmit();

	recTrapTakenPath_emit_oaknut(no_trap);
}

static void recMFSA_emit_oaknut();
static void recMTSA_emit_oaknut();
static void recMTSAB_emit_oaknut();
static void recMTSAH_emit_oaknut();

void recPREF()
{
}

void recSYNC()
{
}

void recMFSA()
{
	recMFSA_emit_oaknut();
}

static void recMFSA_emit_oaknut()
{
	if (!_Rd_)
		return;

	const int mmreg = _allocGPRtoXMMreg(_Rd_, MODE_READ | MODE_WRITE);
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.sa))});
	oakAsm->INS(oakQRegister(mmreg).Delem()[0], OAK_XSCRATCH);
	recEndOaknutEmit();
}

void recMTSA()
{
	recMTSA_emit_oaknut();
}

static void recMTSA_emit_oaknut()
{
	recBeginOaknutEmit();
	recMiscMoveGPRtoOakW(OAK_WSCRATCH, _Rs_);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0xf);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.sa))});
	recEndOaknutEmit();
}

void recMTSAB()
{
	recMTSAB_emit_oaknut();
}

static void recMTSAB_emit_oaknut()
{
	recBeginOaknutEmit();
	recMiscMoveGPRtoOakW(OAK_WSCRATCH, _Rs_);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0xf);
	if ((_Imm_ & 0xf) != 0)
	{
		oakAsm->MOV(OAK_WSCRATCH2, _Imm_ & 0xf);
		oakAsm->EOR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	}
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.sa))});
	recEndOaknutEmit();
}

void recMTSAH()
{
	recMTSAH_emit_oaknut();
}

static void recMTSAH_emit_oaknut()
{
	recBeginOaknutEmit();
	recMiscMoveGPRtoOakW(OAK_WSCRATCH, _Rs_);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x7);
	if ((_Imm_ & 0x7) != 0)
	{
		oakAsm->MOV(OAK_WSCRATCH2, _Imm_ & 0x7);
		oakAsm->EOR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	}
	oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, 1);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.sa))});
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recNULL()
{
	Console.Error("EE: Unimplemented op %x", cpuRegs.code);
}

////////////////////////////////////////////////////
void recUnknown()
{
	// TODO : Unknown ops should throw an exception.
	Console.Error("EE: Unrecognized op %x", cpuRegs.code);
}

void recMMI_Unknown()
{
	// TODO : Unknown ops should throw an exception.
	Console.Error("EE: Unrecognized MMI op %x", cpuRegs.code);
}

void recCOP0_Unknown()
{
	// TODO : Unknown ops should throw an exception.
	Console.Error("EE: Unrecognized COP0 op %x", cpuRegs.code);
}

void recCOP1_Unknown()
{
	// TODO : Unknown ops should throw an exception.
	Console.Error("EE: Unrecognized FPU/COP1 op %x", cpuRegs.code);
}

/**********************************************************
*    UNHANDLED YET OPCODES
*
**********************************************************/

static void recCACHE_emit_oaknut()
{
	iFlushCall(FLUSH_INTERPRETER);
	recBeginOaknutEmit();

	if (_Rs_ == 0)
	{
		oakAsm->MOV(oak::util::W0, static_cast<u32>(_Imm_));
	}
	else
	{
		oakLoad32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UL[0]))});
		if (_Imm_ != 0)
		{
			oakAsm->MOV(oak::util::W4, static_cast<u32>(_Imm_));
			oakAsm->ADD(oak::util::W0, oak::util::W0, oak::util::W4);
		}
	}

	oakAsm->MOV(oak::util::W1, _Rt_);
	oakEmitCall(reinterpret_cast<void*>(eeExecuteCacheInstruction));
	recEndOaknutEmit();
}

void recCACHE()
{
	recCACHE_emit_oaknut();
}

void recTGE()
{
	recTrapReg_emit_oaknut(oak::Cond::LT);
}

void recTGEU()
{
	recTrapReg_emit_oaknut(oak::Cond::CC);
}

void recTLT()
{
	recTrapReg_emit_oaknut(oak::Cond::GE);
}

void recTLTU()
{
	recTrapReg_emit_oaknut(oak::Cond::CS);
}

void recTEQ()
{
	recTrapReg_emit_oaknut(oak::Cond::NE);
}

void recTNE()
{
	recTrapReg_emit_oaknut(oak::Cond::EQ);
}

void recTGEI()
{
	recTrapImm_emit_oaknut(oak::Cond::LT);
}

void recTGEIU()
{
	recTrapImm_emit_oaknut(oak::Cond::CC);
}

void recTLTI()
{
	recTrapImm_emit_oaknut(oak::Cond::GE);
}

void recTLTIU()
{
	recTrapImm_emit_oaknut(oak::Cond::CS);
}

void recTEQI()
{
	recTrapImm_emit_oaknut(oak::Cond::NE);
}

void recTNEI()
{
	recTrapImm_emit_oaknut(oak::Cond::EQ);
}

} // namespace OpcodeImpl
} // namespace Dynarec
} // namespace R5900
