// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/ee/iR5900-arm64.h"
#include "arm64/ee/iR5900LoadStore-arm64.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

#define REC_STORES
#define REC_LOADS

static int RETURN_READ_IN_RAX()
{
	return EE_HOST_RAX;
}

namespace R5900::Dynarec::OpcodeImpl
{

/*********************************************************
* Load and store for GPR                                 *
* Format:  OP rt, offset(base)                           *
*********************************************************/
#ifndef LOADSTORE_RECOMPILE

namespace Interp = R5900::Interpreter::OpcodeImpl;

REC_FUNC_DEL(LB, _Rt_);
REC_FUNC_DEL(LBU, _Rt_);
REC_FUNC_DEL(LH, _Rt_);
REC_FUNC_DEL(LHU, _Rt_);
REC_FUNC_DEL(LW, _Rt_);
REC_FUNC_DEL(LWU, _Rt_);
REC_FUNC_DEL(LWL, _Rt_);
REC_FUNC_DEL(LWR, _Rt_);
REC_FUNC_DEL(LD, _Rt_);
REC_FUNC_DEL(LDR, _Rt_);
REC_FUNC_DEL(LDL, _Rt_);
REC_FUNC_DEL(LQ, _Rt_);
REC_FUNC(SB);
REC_FUNC(SH);
REC_FUNC(SW);
REC_FUNC(SWL);
REC_FUNC(SWR);
REC_FUNC(SD);
REC_FUNC(SDL);
REC_FUNC(SDR);
REC_FUNC(SQ);
REC_FUNC(LWC1);
REC_FUNC(SWC1);
REC_FUNC(LQC2);
REC_FUNC(SQC2);

#else

using namespace Interpreter::OpcodeImpl;

static void recMoveGPRtoOakW(oak::WReg to, int fromgpr)
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

static void recMoveGPRtoOakX(oak::XReg to, int fromgpr)
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

static void recLoadAddressToECX_emit_oaknut(bool align16)
{
	_freeX86reg(EE_HOST_RCX);

	recBeginOaknutEmit();

	const oak::WReg addr = oakWRegister(EE_HOST_RCX);
	if (_Rs_ != 0)
	{
		const int base = !GPR_IS_CONST1(_Rs_) && _Imm_ >= -4095 && _Imm_ <= 4095
			? _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ) : -1;
		if (base >= 0)
		{
			oakAddSignedImm(addr, oakWRegister(base), _Imm_, oak::util::W4);
		}
		else
		{
			recMoveGPRtoOakW(addr, _Rs_);
			oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
		}
	}
	else
	{
		oakAsm->MOV(addr, static_cast<u32>(_Imm_));
	}

	if (align16)
		oakAsm->AND(addr, addr, oak::BitImm32(~0x0fu));

	recEndOaknutEmit();
}

static u32 recLoadStoreGuestPC()
{
	return g_recompilingDelaySlot ? pc : (pc - 4);
}

// EE timer/counter count reads can require an immediate event test to deliver overflow IRQs.
static bool recLoadNeedsEEHwCounterEventTest()
{
	if (!GPR_IS_CONST1(_Rs_))
		return false;

	const u32 srcadr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
	switch (srcadr)
	{
		case 0x10000000u: // RCNT0_COUNT
		case 0x10000800u: // RCNT1_COUNT
		case 0x10001000u: // RCNT2_COUNT
		case 0x10001800u: // RCNT3_COUNT
			return true;
		default:
			return false;
	}
}

static void recForceEEHwCounterEventTest()
{
	iFlushCall(FLUSH_INTERPRETER);
	g_branch = 2;
}

//////////////////////////////////////////////////////////////////////////////////////////
//

static void recStoreAddressToECX_emit_oaknut(bool align16)
{
	recBeginOaknutEmit();

	const oak::WReg addr = oakWRegister(EE_HOST_RCX);
	if (_Rs_ != 0)
	{
		recMoveGPRtoOakW(addr, _Rs_);
		oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
	}
	else
	{
		oakAsm->MOV(addr, static_cast<u32>(_Imm_));
	}

	if (align16)
		oakAsm->AND(addr, addr, oak::BitImm32(~0x0fu));

	recEndOaknutEmit();
}

static int recPrepareStoreAddressToECX_emit_oaknut(bool align16, int value_reg = -1)
{
	if (value_reg == EE_HOST_RCX)
	{
		_freeX86reg(EE_HOST_RDX);
		recBeginOaknutEmit();
		oakAsm->MOV(oakXRegister(EE_HOST_RDX), oakXRegister(EE_HOST_RCX));
		recEndOaknutEmit();
		value_reg = EE_HOST_RDX;
	}

	_freeX86reg(EE_HOST_RCX);
	recStoreAddressToECX_emit_oaknut(align16);
	return value_reg;
}


//////////////////////////////////////////////////////////////////////////////////////////
//
static void recLB_emit_oaknut()
{
	const bool needs_event_test = recLoadNeedsEEHwCounterEventTest();
	vtlb_ReadRegAllocCallback alloc_cb = _Rt_ ? []() { return _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE); } : nullptr;
	const int x86reg = GPR_IS_CONST1(_Rs_) ?
		vtlb_DynGenReadNonQuad_Const(8, true, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, alloc_cb) :
		(recLoadAddressToECX_emit_oaknut(false), vtlb_DynGenReadNonQuad(8, true, false, EE_HOST_RCX, alloc_cb));
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeX86reg(x86reg);
	if (needs_event_test)
		recForceEEHwCounterEventTest();
}

void recLB()
{
	recLB_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LB);
}

static void recLBU_emit_oaknut()
{
	const bool needs_event_test = recLoadNeedsEEHwCounterEventTest();
	vtlb_ReadRegAllocCallback alloc_cb = _Rt_ ? []() { return _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE); } : nullptr;
	const int x86reg = GPR_IS_CONST1(_Rs_) ?
		vtlb_DynGenReadNonQuad_Const(8, false, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, alloc_cb) :
		(recLoadAddressToECX_emit_oaknut(false), vtlb_DynGenReadNonQuad(8, false, false, EE_HOST_RCX, alloc_cb));
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeX86reg(x86reg);
	if (needs_event_test)
		recForceEEHwCounterEventTest();
}

void recLBU()
{
	recLBU_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LBU);
}

static void recLH_emit_oaknut()
{
	const bool needs_event_test = recLoadNeedsEEHwCounterEventTest();
	vtlb_ReadRegAllocCallback alloc_cb = _Rt_ ? []() { return _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE); } : nullptr;
	const int x86reg = GPR_IS_CONST1(_Rs_) ?
		vtlb_DynGenReadNonQuad_Const(16, true, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, alloc_cb) :
		(recLoadAddressToECX_emit_oaknut(false), vtlb_DynGenReadNonQuad(16, true, false, EE_HOST_RCX, alloc_cb));
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeX86reg(x86reg);
	if (needs_event_test)
		recForceEEHwCounterEventTest();
}

void recLH()
{
	recLH_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LH);
}

static void recLHU_emit_oaknut()
{
	const bool needs_event_test = recLoadNeedsEEHwCounterEventTest();
	vtlb_ReadRegAllocCallback alloc_cb = _Rt_ ? []() { return _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE); } : nullptr;
	const int x86reg = GPR_IS_CONST1(_Rs_) ?
		vtlb_DynGenReadNonQuad_Const(16, false, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, alloc_cb) :
		(recLoadAddressToECX_emit_oaknut(false), vtlb_DynGenReadNonQuad(16, false, false, EE_HOST_RCX, alloc_cb));
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeX86reg(x86reg);
	if (needs_event_test)
		recForceEEHwCounterEventTest();
}

void recLHU()
{
	recLHU_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LHU);
}

static void recLW_emit_oaknut()
{
	const bool needs_event_test = recLoadNeedsEEHwCounterEventTest();
	vtlb_ReadRegAllocCallback alloc_cb = _Rt_ ? []() { return _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE); } : nullptr;
	const int x86reg = GPR_IS_CONST1(_Rs_) ?
		vtlb_DynGenReadNonQuad_Const(32, true, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, alloc_cb) :
		(recLoadAddressToECX_emit_oaknut(false), vtlb_DynGenReadNonQuad(32, true, false, EE_HOST_RCX, alloc_cb));
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeX86reg(x86reg);
	if (needs_event_test)
		recForceEEHwCounterEventTest();
}

void recLW()
{
	recLW_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LW);
}

static void recLWU_emit_oaknut()
{
	const bool needs_event_test = recLoadNeedsEEHwCounterEventTest();
	vtlb_ReadRegAllocCallback alloc_cb = _Rt_ ? []() { return _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE); } : nullptr;
	const int x86reg = GPR_IS_CONST1(_Rs_) ?
		vtlb_DynGenReadNonQuad_Const(32, false, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, alloc_cb) :
		(recLoadAddressToECX_emit_oaknut(false), vtlb_DynGenReadNonQuad(32, false, false, EE_HOST_RCX, alloc_cb));
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeX86reg(x86reg);
	if (needs_event_test)
		recForceEEHwCounterEventTest();
}

void recLWU()
{
	recLWU_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LWU);
}

static void recLD_emit_oaknut()
{
	vtlb_ReadRegAllocCallback alloc_cb = _Rt_ ? []() { return _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE); } : nullptr;
	const int x86reg = GPR_IS_CONST1(_Rs_) ?
		vtlb_DynGenReadNonQuad_Const(64, false, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, alloc_cb) :
		(recLoadAddressToECX_emit_oaknut(false), vtlb_DynGenReadNonQuad(64, false, false, EE_HOST_RCX, alloc_cb));
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeX86reg(x86reg);
}

void recLD()
{
	recLD_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LD);
}

static void recLQ_emit_oaknut()
{
	vtlb_ReadRegAllocCallback alloc_cb = _Rt_ ? []() { return _allocGPRtoXMMreg(_Rt_, MODE_WRITE); } : nullptr;
	const int xmmreg = GPR_IS_CONST1(_Rs_) ?
		vtlb_DynGenReadQuad_Const(128, (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0x0f, _Rt_ ? alloc_cb : nullptr) :
		(recLoadAddressToECX_emit_oaknut(true), vtlb_DynGenReadQuad(128, EE_HOST_RCX, _Rt_ ? alloc_cb : nullptr));
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeXMMreg(xmmreg);
}

void recLQ()
{
	recLQ_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LQ);
}

static void recSB_emit_oaknut()
{
	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
	if (GPR_IS_CONST1(_Rs_))
		vtlb_DynGenWrite_Const(8, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, regt);
	else
	{
		const int value_reg = recPrepareStoreAddressToECX_emit_oaknut(false, regt);
		vtlb_DynGenWrite(8, false, EE_HOST_RCX, value_reg);
	}
}

void recSB()
{
	recSB_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SB);
}

static void recSH_emit_oaknut()
{
	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
	if (GPR_IS_CONST1(_Rs_))
		vtlb_DynGenWrite_Const(16, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, regt);
	else
	{
		const int value_reg = recPrepareStoreAddressToECX_emit_oaknut(false, regt);
		vtlb_DynGenWrite(16, false, EE_HOST_RCX, value_reg);
	}
}

void recSH()
{
	recSH_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SH);
}

static void recSW_emit_oaknut()
{
	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
	if (GPR_IS_CONST1(_Rs_))
		vtlb_DynGenWrite_Const(32, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, regt);
	else
	{
		const int value_reg = recPrepareStoreAddressToECX_emit_oaknut(false, regt);
		vtlb_DynGenWrite(32, false, EE_HOST_RCX, value_reg);
	}
}

void recSW()
{
	recSW_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SW);
}

static void recSD_emit_oaknut()
{
	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
	if (GPR_IS_CONST1(_Rs_))
		vtlb_DynGenWrite_Const(64, false, g_cpuConstRegs[_Rs_].UL[0] + _Imm_, regt);
	else
	{
		const int value_reg = recPrepareStoreAddressToECX_emit_oaknut(false, regt);
		vtlb_DynGenWrite(64, false, EE_HOST_RCX, value_reg);
	}
}

void recSD()
{
	recSD_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SD);
}

static void recSQ_emit_oaknut()
{
	const int regt = _allocGPRtoXMMreg(_Rt_, MODE_READ);
	if (GPR_IS_CONST1(_Rs_))
		vtlb_DynGenWrite_Const(128, true, (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0x0f, regt);
	else
	{
		recPrepareStoreAddressToECX_emit_oaknut(true);
		vtlb_DynGenWrite(128, true, EE_HOST_RCX, regt);
	}
}

void recSQ()
{
	recSQ_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SQ);
}

////////////////////////////////////////////////////

static void recLWL_emit_oaknut()
{
#ifdef REC_LOADS
	_freeX86reg(EE_HOST_RAX);
	_freeX86reg(EE_HOST_RCX);
	_freeX86reg(EE_HOST_RDX);

	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);
	if (_Rs_)
		_addNeededX86reg(X86TYPE_GPR, _Rs_);

	const int temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);

	recBeginOaknutEmit();

	const oak::WReg addr = oakWRegister(EE_HOST_RCX);
	const oak::WReg temp_w = oakWRegister(temp);
	recMoveGPRtoOakW(addr, _Rs_);
	oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);

	oakAsm->MOV(temp_w, addr);
	oakAsm->AND(temp_w, temp_w, oak::BitImm32(3));
	oakAsm->LSL(temp_w, temp_w, 3);
	oakAsm->AND(addr, addr, oak::BitImm32(~3u));

	recEndOaknutEmit();

	vtlb_DynGenReadNonQuad(32, false, false, EE_HOST_RCX, RETURN_READ_IN_RAX);

	if (!_Rt_)
	{
		_freeX86reg(temp);
		return;
	}

	// mask off bytes loaded
	const int treg = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE);
	recBeginOaknutEmit();

	oakAsm->MOV(oakWRegister(EE_HOST_RCX), temp_w);
	oakAsm->MOV(oakWRegister(EE_HOST_RDX), 0x00ffffffu);
	oakAsm->LSR(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RCX));
	oakAsm->AND(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(treg));

	// OR in bytes loaded
	oakAsm->NEG(oakWRegister(EE_HOST_RCX), oakWRegister(EE_HOST_RCX));
	oakAsm->ADD(oakWRegister(EE_HOST_RCX), oakWRegister(EE_HOST_RCX), 24);
	oakAsm->LSL(oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RCX));
	oakAsm->ORR(oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RDX));
	oakAsm->SXTW(oakXRegister(treg), oakWRegister(EE_HOST_RAX));

	recEndOaknutEmit();
	_freeX86reg(temp);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);

	recCall(LWL);
#endif
}

void recLWL()
{
	recLWL_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LWL);
}

////////////////////////////////////////////////////
static void recLWR_emit_oaknut()
{
#ifdef REC_LOADS
	_freeX86reg(EE_HOST_RAX);
	_freeX86reg(EE_HOST_RCX);
	_freeX86reg(EE_HOST_RDX);

	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);
	if (_Rs_)
		_addNeededX86reg(X86TYPE_GPR, _Rs_);

	const int temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);

	recBeginOaknutEmit();

	const oak::WReg addr = oakWRegister(EE_HOST_RCX);
	const oak::WReg temp_w = oakWRegister(temp);
	recMoveGPRtoOakW(addr, _Rs_);
	oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);

	oakAsm->MOV(temp_w, addr);
	oakAsm->AND(addr, addr, oak::BitImm32(~3u));

	recEndOaknutEmit();

	vtlb_DynGenReadNonQuad(32, false, false, EE_HOST_RCX, RETURN_READ_IN_RAX);

	if (!_Rt_)
	{
		_freeX86reg(temp);
		return;
	}

	const int treg = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE);

	recBeginOaknutEmit();

	oakAsm->AND(temp_w, temp_w, oak::BitImm32(3));

	oak::Label nomask;
	oakAsm->CBZ(temp_w, nomask);
	oakAsm->LSL(temp_w, temp_w, 3);
	// mask off bytes loaded
	oakAsm->MOV(oakWRegister(EE_HOST_RCX), 24);
	oakAsm->SUB(oakWRegister(EE_HOST_RCX), oakWRegister(EE_HOST_RCX), temp_w);
	oakAsm->MOV(oakWRegister(EE_HOST_RDX), 0xffffff00u);
	oakAsm->LSL(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RCX));
	oakAsm->AND(oakWRegister(EE_HOST_RDX), oakWRegister(treg), oakWRegister(EE_HOST_RDX));

	// OR in bytes loaded
	oakAsm->MOV(oakWRegister(EE_HOST_RCX), temp_w);
	oakAsm->LSR(oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RCX));
	oakAsm->ORR(oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RDX));
	oakAsm->BFI(oakXRegister(treg), oakXRegister(EE_HOST_RAX), 0, 32);

	oak::Label end;
	oakAsm->B(end);
	oakAsm->l(nomask);
	// NOTE: This might look wrong, but it's correct - see interpreter.
	oakAsm->SXTW(oakXRegister(treg), oakWRegister(EE_HOST_RAX));
	oakAsm->l(end);

	recEndOaknutEmit();
	_freeX86reg(temp);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);

	recCall(LWR);
#endif
}

void recLWR()
{
	recLWR_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LWR);
}

////////////////////////////////////////////////////

static void recSWL_emit_oaknut()
{
#ifdef REC_STORES
	_addNeededX86reg(X86TYPE_GPR, _Rs_);

	if (!GPR_IS_CONST1(_Rt_))
		_allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
	else
		_addNeededX86reg(X86TYPE_GPR, _Rt_);

	const int temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	_freeX86reg(EE_HOST_RAX);
	_freeX86reg(EE_HOST_RCX);
	_freeX86reg(EE_HOST_RDX);

	recBeginOaknutEmit();

	const oak::WReg addr = oakWRegister(EE_HOST_RCX);
	const oak::WReg temp_w = oakWRegister(temp);
	recMoveGPRtoOakW(addr, _Rs_);
	oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
	oakAsm->MOV(temp_w, addr);
	oakAsm->AND(addr, addr, oak::BitImm32(~3u));
	oakAsm->AND(temp_w, temp_w, oak::BitImm32(3));
	oakAsm->CMP(temp_w, 3);
	u8* skip_branch = oakGetCurrentCodePointer();
	oakAsm->NOP();

	recEndOaknutEmit();

	if (!CHECK_FASTMEM || vtlb_IsFaultingPC(recLoadStoreGuestPC()))
		iFlushCall(FLUSH_FULLVTLB);

	recBeginOaknutEmit();
	oakAsm->LSL(temp_w, temp_w, 3);
	recEndOaknutEmit();

	vtlb_DynGenReadNonQuad(32, false, false, EE_HOST_RCX, RETURN_READ_IN_RAX);

	recBeginOaknutEmit();

	oakAsm->MOV(oakWRegister(EE_HOST_RCX), temp_w);
	oakAsm->MOV(oakWRegister(EE_HOST_RDX), 0xffffff00u);
	oakAsm->LSL(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RCX));
	oakAsm->AND(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RAX));

	if (_Rt_)
	{
		oakAsm->NEG(oakWRegister(EE_HOST_RCX), oakWRegister(EE_HOST_RCX));
		oakAsm->ADD(oakWRegister(EE_HOST_RCX), oakWRegister(EE_HOST_RCX), 24);
		recMoveGPRtoOakW(oakWRegister(EE_HOST_RAX), _Rt_);
		oakAsm->LSR(oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RCX));
		oakAsm->ORR(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RAX));
	}

	recMoveGPRtoOakW(addr, _Rs_);
	oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
	oakAsm->AND(addr, addr, oak::BitImm32(~3u));

	recEndOaknutEmit();

	u8* end_branch = nullptr;
	recBeginOaknutEmit();
	end_branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	u8* skip_target = oakGetCurrentCodePointer();
	recMoveGPRtoOakW(oakWRegister(EE_HOST_RDX), _Rt_);
	recEndOaknutEmit();
	u8* end_target = oakGetCurrentCodePointer();
	oakPatchCondBranch(skip_branch, skip_target, oak::Cond::EQ, false);
	oakEmitJmpPtr(end_branch, end_target, false);

	_freeX86reg(temp);
	vtlb_DynGenWrite(32, false, EE_HOST_RCX, EE_HOST_RDX);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(SWL);
#endif
}

void recSWL()
{
	recSWL_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SWL);
}

////////////////////////////////////////////////////
static void recSWR_emit_oaknut()
{
#ifdef REC_STORES
	_addNeededX86reg(X86TYPE_GPR, _Rs_);

	if (!GPR_IS_CONST1(_Rt_))
		_allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
	else
		_addNeededX86reg(X86TYPE_GPR, _Rt_);

	const int temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	_freeX86reg(EE_HOST_RCX);
	_freeX86reg(EE_HOST_RDX);

	recBeginOaknutEmit();

	const oak::WReg addr = oakWRegister(EE_HOST_RCX);
	const oak::WReg temp_w = oakWRegister(temp);
	recMoveGPRtoOakW(addr, _Rs_);
	oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
	oakAsm->MOV(temp_w, addr);
	oakAsm->AND(addr, addr, oak::BitImm32(~3u));
	oakAsm->ANDS(temp_w, temp_w, oak::BitImm32(3));
	u8* skip_branch = oakGetCurrentCodePointer();
	oakAsm->NOP();

	recEndOaknutEmit();

	if (!CHECK_FASTMEM || vtlb_IsFaultingPC(recLoadStoreGuestPC()))
		iFlushCall(FLUSH_FULLVTLB);

	recBeginOaknutEmit();
	oakAsm->LSL(temp_w, temp_w, 3);
	recEndOaknutEmit();

	vtlb_DynGenReadNonQuad(32, false, false, EE_HOST_RCX, RETURN_READ_IN_RAX);

	recBeginOaknutEmit();

	oakAsm->MOV(oakWRegister(EE_HOST_RCX), 24);
	oakAsm->SUB(oakWRegister(EE_HOST_RCX), oakWRegister(EE_HOST_RCX), temp_w);
	oakAsm->MOV(oakWRegister(EE_HOST_RDX), 0x00ffffffu);
	oakAsm->LSR(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RCX));
	oakAsm->AND(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RAX));

	if (_Rt_)
	{
		oakAsm->MOV(oakWRegister(EE_HOST_RCX), temp_w);
		recMoveGPRtoOakW(oakWRegister(EE_HOST_RAX), _Rt_);
		oakAsm->LSL(oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RAX), oakWRegister(EE_HOST_RCX));
		oakAsm->ORR(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RAX));
	}

	recMoveGPRtoOakW(addr, _Rs_);
	oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
	oakAsm->AND(addr, addr, oak::BitImm32(~3u));

	recEndOaknutEmit();

	u8* end_branch = nullptr;
	recBeginOaknutEmit();
	end_branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	u8* skip_target = oakGetCurrentCodePointer();
	recMoveGPRtoOakW(oakWRegister(EE_HOST_RDX), _Rt_);
	recEndOaknutEmit();
	u8* end_target = oakGetCurrentCodePointer();
	oakPatchCondBranch(skip_branch, skip_target, oak::Cond::EQ, false);
	oakEmitJmpPtr(end_branch, end_target, false);

	_freeX86reg(temp);
	vtlb_DynGenWrite(32, false, EE_HOST_RCX, EE_HOST_RDX);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(SWR);
#endif
}

void recSWR()
{
	recSWR_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SWR);
}

////////////////////////////////////////////////////

namespace
{
    enum class SHIFTV
    {
        xSHL,
        xSHR,
        xSAR
    };
} // namespace

template <SHIFTV Direction>
static void oakShiftX(oak::XReg dst, oak::XReg src, int amount)
{
	if constexpr (Direction == SHIFTV::xSHR)
		oakAsm->LSR(dst, src, amount);
	else if constexpr (Direction == SHIFTV::xSAR)
		oakAsm->ASR(dst, src, amount);
	else
		oakAsm->LSL(dst, src, amount);
}

template <SHIFTV Direction>
static void oakShiftX(oak::XReg dst, oak::XReg src, oak::XReg amount)
{
	if constexpr (Direction == SHIFTV::xSHR)
		oakAsm->LSR(dst, src, amount);
	else if constexpr (Direction == SHIFTV::xSAR)
		oakAsm->ASR(dst, src, amount);
	else
		oakAsm->LSL(dst, src, amount);
}

template <SHIFTV MaskShift, SHIFTV Shift>
static void ldlrhelper_const_emit_oaknut(int maskamt, int amt, oak::XReg value, oak::XReg rt)
{
	oakAsm->MOV(OAK_XSCRATCH, UINT64_C(0xffffffffffffffff));
	oakShiftX<MaskShift>(OAK_XSCRATCH, OAK_XSCRATCH, maskamt);
	oakAsm->AND(rt, rt, OAK_XSCRATCH);
	oakShiftX<Shift>(value, value, amt);
	oakAsm->ORR(rt, rt, value);
}

template <SHIFTV MaskShift, SHIFTV Shift>
static void ldlrhelper_emit_oaknut(oak::XReg maskamt, oak::XReg amt, oak::XReg value, oak::XReg rt)
{
	oakAsm->MOV(OAK_XSCRATCH, UINT64_C(0xffffffffffffffff));
	oakShiftX<MaskShift>(OAK_XSCRATCH, OAK_XSCRATCH, maskamt);
	oakAsm->AND(rt, rt, OAK_XSCRATCH);
	oakShiftX<Shift>(value, value, amt);
	oakAsm->ORR(rt, rt, value);
}

static void recLDL_emit_oaknut()
{
	if (!_Rt_)
		return;

#ifdef REC_LOADS
	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);
	if (_Rs_)
		_addNeededX86reg(X86TYPE_GPR, _Rs_);

	const int temp1 = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	_freeX86reg(EE_HOST_RAX);
	_freeX86reg(EE_HOST_RCX);
	_freeX86reg(EE_HOST_RDX);

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 srcadr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		srcadr &= ~0x07;
		vtlb_DynGenReadNonQuad_Const(64, false, false, srcadr, RETURN_READ_IN_RAX);
	}
	else
	{
		_freeX86reg(EE_HOST_RCX);
		recBeginOaknutEmit();

		const oak::WReg addr = oakWRegister(EE_HOST_RCX);
		recMoveGPRtoOakW(addr, _Rs_);
		oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
		oakAsm->MOV(oakWRegister(temp1), addr);
		oakAsm->AND(addr, addr, oak::BitImm32(~0x07u));

		recEndOaknutEmit();

		vtlb_DynGenReadNonQuad(64, false, false, EE_HOST_RCX, RETURN_READ_IN_RAX);
	}

	const int treg = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE);
	recBeginOaknutEmit();

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 shift = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		shift = ((shift & 0x7) + 1) * 8;
		if (shift != 64)
		{
			ldlrhelper_const_emit_oaknut<SHIFTV::xSHR, SHIFTV::xSHL>(shift, 64 - shift, oakXRegister(EE_HOST_RAX), oakXRegister(treg));
		}
		else
		{
			oakAsm->MOV(oakXRegister(treg), oakXRegister(EE_HOST_RAX));
		}
	}
	else
	{
		const oak::WReg temp1_w = oakWRegister(temp1);
		const oak::XReg temp1_x = oakXRegister(temp1);
		oakAsm->AND(temp1_w, temp1_w, oak::BitImm32(0x7));
		oakAsm->CMP(temp1_w, 7);
		oakAsm->CSEL(oakXRegister(treg), oakXRegister(EE_HOST_RAX), oakXRegister(treg), oak::Cond::EQ);
		oak::Label skip;
		oakAsm->B(oak::Cond::EQ, skip);
		// Calculate the shift from top bit to lowest.
		oakAsm->ADD(temp1_w, temp1_w, 1);
		oakAsm->MOV(oakWRegister(EE_HOST_RDX), 64);
		oakAsm->LSL(temp1_w, temp1_w, 3);
		oakAsm->SUB(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), temp1_w);
		ldlrhelper_emit_oaknut<SHIFTV::xSHR, SHIFTV::xSHL>(temp1_x, oakXRegister(EE_HOST_RDX), oakXRegister(EE_HOST_RAX), oakXRegister(treg));
		oakAsm->l(skip);
	}

	recEndOaknutEmit();
	_freeX86reg(temp1);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(LDL);
#endif

}

static void recLDR_emit_oaknut();

void recLDR()
{
	recLDR_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LDR);
}

void recLDL()
{
	recLDL_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LDL);
}

////////////////////////////////////////////////////
static void recLDR_emit_oaknut()
{
	if (!_Rt_)
		return;

#ifdef REC_LOADS
	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);
	if (_Rs_)
		_addNeededX86reg(X86TYPE_GPR, _Rs_);

	const int temp1 = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	_freeX86reg(EE_HOST_RAX);
	_freeX86reg(EE_HOST_RCX);
	_freeX86reg(EE_HOST_RDX);

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 srcadr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		srcadr &= ~0x07;
		vtlb_DynGenReadNonQuad_Const(64, false, false, srcadr, RETURN_READ_IN_RAX);
	}
	else
	{
		_freeX86reg(EE_HOST_RCX);
		recBeginOaknutEmit();

		const oak::WReg addr = oakWRegister(EE_HOST_RCX);
		recMoveGPRtoOakW(addr, _Rs_);
		oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
		oakAsm->MOV(oakWRegister(temp1), addr);
		oakAsm->AND(addr, addr, oak::BitImm32(~0x07u));

		recEndOaknutEmit();

		vtlb_DynGenReadNonQuad(64, false, false, EE_HOST_RCX, RETURN_READ_IN_RAX);
	}

	const int treg = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE);
	recBeginOaknutEmit();

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 shift = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		shift = (shift & 0x7) * 8;
		if (shift != 0)
		{
			ldlrhelper_const_emit_oaknut<SHIFTV::xSHL, SHIFTV::xSHR>(64 - shift, shift, oakXRegister(EE_HOST_RAX), oakXRegister(treg));
		}
		else
		{
			oakAsm->MOV(oakXRegister(treg), oakXRegister(EE_HOST_RAX));
		}
	}
	else
	{
		const oak::WReg temp1_w = oakWRegister(temp1);
		const oak::XReg temp1_x = oakXRegister(temp1);
		oakAsm->ANDS(temp1_w, temp1_w, oak::BitImm32(0x7));
		oakAsm->CSEL(oakXRegister(treg), oakXRegister(EE_HOST_RAX), oakXRegister(treg), oak::Cond::EQ);
		oak::Label skip;
		oakAsm->B(oak::Cond::EQ, skip);
		// Calculate the shift from top bit to lowest.
		oakAsm->MOV(oakWRegister(EE_HOST_RDX), 64);
		oakAsm->LSL(temp1_w, temp1_w, 3);
		oakAsm->SUB(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), temp1_w);
		ldlrhelper_emit_oaknut<SHIFTV::xSHL, SHIFTV::xSHR>(oakXRegister(EE_HOST_RDX), temp1_x, oakXRegister(EE_HOST_RAX), oakXRegister(treg));
		oakAsm->l(skip);
	}

	recEndOaknutEmit();
	_freeX86reg(temp1);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(LDR);
#endif

}

////////////////////////////////////////////////////

template <SHIFTV MaskShift, SHIFTV Shift>
static void sdlrhelper_const_emit_oaknut(int maskamt, int amt, oak::XReg value, oak::XReg rt)
{
	oakAsm->MOV(OAK_XSCRATCH, UINT64_C(0xffffffffffffffff));
	oakShiftX<MaskShift>(OAK_XSCRATCH, OAK_XSCRATCH, maskamt);
	oakAsm->AND(OAK_XSCRATCH, OAK_XSCRATCH, value);
	oakShiftX<Shift>(rt, rt, amt);
	oakAsm->ORR(rt, rt, OAK_XSCRATCH);
}

template <SHIFTV MaskShift, SHIFTV Shift>
static void sdlrhelper_emit_oaknut(oak::XReg maskamt, oak::XReg amt, oak::XReg value, oak::XReg rt)
{
	oakAsm->MOV(OAK_XSCRATCH, UINT64_C(0xffffffffffffffff));
	oakShiftX<MaskShift>(OAK_XSCRATCH, OAK_XSCRATCH, maskamt);
	oakAsm->AND(OAK_XSCRATCH, OAK_XSCRATCH, value);
	oakShiftX<Shift>(rt, rt, amt);
	oakAsm->ORR(rt, rt, OAK_XSCRATCH);
}

static void recSDL_emit_oaknut()
{
#ifdef REC_STORES
	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);

	_freeX86reg(EE_HOST_RCX);
	_freeX86reg(EE_HOST_RDX);

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 adr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		u32 aligned = adr & ~0x07;
		u32 shift = ((adr & 0x7) + 1) * 8;
		recBeginOaknutEmit();
		if (shift == 64)
		{
			recMoveGPRtoOakX(oakXRegister(EE_HOST_RDX), _Rt_);
		}
		else
		{
			recEndOaknutEmit();
			vtlb_DynGenReadNonQuad_Const(64, false, false, aligned, RETURN_READ_IN_RAX);
			recBeginOaknutEmit();
			recMoveGPRtoOakX(oakXRegister(EE_HOST_RDX), _Rt_);
			sdlrhelper_const_emit_oaknut<SHIFTV::xSHL, SHIFTV::xSHR>(shift, 64 - shift, oakXRegister(EE_HOST_RAX), oakXRegister(EE_HOST_RDX));
		}
		recEndOaknutEmit();
		vtlb_DynGenWrite_Const(64, false, aligned, EE_HOST_RDX);
	}
	else
	{
		if (_Rs_)
			_addNeededX86reg(X86TYPE_GPR, _Rs_);

		_freeX86reg(EE_HOST_RCX);
		_freeX86reg(EE_HOST_RDX);

		const int temp1 = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
		const int temp2 = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);

		recBeginOaknutEmit();

		const oak::WReg addr = oakWRegister(EE_HOST_RCX);
		const oak::WReg temp1_w = oakWRegister(temp1);
		const oak::XReg temp1_x = oakXRegister(temp1);
		const oak::XReg temp2_x = oakXRegister(temp2);
		recMoveGPRtoOakW(addr, _Rs_);
		oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
		recMoveGPRtoOakX(oakXRegister(EE_HOST_RDX), _Rt_);
		oakAsm->MOV(temp1_w, addr);
		oakAsm->MOV(temp2_x, oakXRegister(EE_HOST_RDX));
		oakAsm->AND(addr, addr, oak::BitImm32(~0x07u));
		oakAsm->AND(temp1_w, temp1_w, oak::BitImm32(0x7));
		oakAsm->CMP(temp1_w, 7);
		u8* skip_branch = oakGetCurrentCodePointer();
		oakAsm->NOP();

		recEndOaknutEmit();

		// If we're not using fastmem, we need to flush early. Because the first read
		// (which would flush) happens inside a branch.
		if (!CHECK_FASTMEM || vtlb_IsFaultingPC(recLoadStoreGuestPC()))
			iFlushCall(FLUSH_FULLVTLB);

		recBeginOaknutEmit();
		oakAsm->ADD(temp1_w, temp1_w, 1);
		recEndOaknutEmit();

		vtlb_DynGenReadNonQuad(64, false, false, EE_HOST_RCX, RETURN_READ_IN_RAX);

		recBeginOaknutEmit();

		oakAsm->MOV(oakWRegister(EE_HOST_RDX), 64);
		oakAsm->LSL(temp1_w, temp1_w, 3);
		oakAsm->SUB(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), temp1_w);
		sdlrhelper_emit_oaknut<SHIFTV::xSHL, SHIFTV::xSHR>(temp1_x, oakXRegister(EE_HOST_RDX), oakXRegister(EE_HOST_RAX), temp2_x);
		recMoveGPRtoOakW(addr, _Rs_);
		oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
		oakAsm->AND(addr, addr, oak::BitImm32(~0x07u));

		recEndOaknutEmit();

		u8* skip_target = oakGetCurrentCodePointer();
		oakPatchCondBranch(skip_branch, skip_target, oak::Cond::EQ, false);

		vtlb_DynGenWrite(64, false, EE_HOST_RCX, temp2);
		_freeX86reg(temp2);
		_freeX86reg(temp1);
	}
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(SDL);
#endif
}

void recSDL()
{
	recSDL_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SDL);
}

////////////////////////////////////////////////////
static void recSDR_emit_oaknut()
{
#ifdef REC_STORES
	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);

	_freeX86reg(EE_HOST_RCX);
	_freeX86reg(EE_HOST_RDX);

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 adr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		u32 aligned = adr & ~0x07;
		u32 shift = (adr & 0x7) * 8;
		recBeginOaknutEmit();
		if (shift == 0)
		{
			recMoveGPRtoOakX(oakXRegister(EE_HOST_RDX), _Rt_);
		}
		else
		{
			recEndOaknutEmit();
			vtlb_DynGenReadNonQuad_Const(64, false, false, aligned, RETURN_READ_IN_RAX);
			recBeginOaknutEmit();
			recMoveGPRtoOakX(oakXRegister(EE_HOST_RDX), _Rt_);
			sdlrhelper_const_emit_oaknut<SHIFTV::xSHR, SHIFTV::xSHL>(64 - shift, shift, oakXRegister(EE_HOST_RAX), oakXRegister(EE_HOST_RDX));
		}
		recEndOaknutEmit();

		vtlb_DynGenWrite_Const(64, false, aligned, EE_HOST_RDX);
	}
	else
	{
		if (_Rs_)
			_addNeededX86reg(X86TYPE_GPR, _Rs_);

		_freeX86reg(EE_HOST_RCX);
		_freeX86reg(EE_HOST_RDX);

		const int temp1 = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
		const int temp2 = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);

		recBeginOaknutEmit();

		const oak::WReg addr = oakWRegister(EE_HOST_RCX);
		const oak::WReg temp1_w = oakWRegister(temp1);
		const oak::XReg temp1_x = oakXRegister(temp1);
		const oak::XReg temp2_x = oakXRegister(temp2);
		recMoveGPRtoOakW(addr, _Rs_);
		oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
		recMoveGPRtoOakX(oakXRegister(EE_HOST_RDX), _Rt_);
		oakAsm->MOV(temp1_w, addr);
		oakAsm->MOV(temp2_x, oakXRegister(EE_HOST_RDX));
		oakAsm->AND(addr, addr, oak::BitImm32(~0x07u));
		oakAsm->ANDS(temp1_w, temp1_w, oak::BitImm32(0x7));
		u8* skip_branch = oakGetCurrentCodePointer();
		oakAsm->NOP();

		recEndOaknutEmit();

		// If we're not using fastmem, we need to flush early. Because the first read
		// (which would flush) happens inside a branch.
		if (!CHECK_FASTMEM || vtlb_IsFaultingPC(recLoadStoreGuestPC()))
			iFlushCall(FLUSH_FULLVTLB);

		vtlb_DynGenReadNonQuad(64, false, false, EE_HOST_RCX, RETURN_READ_IN_RAX);

		recBeginOaknutEmit();

		oakAsm->MOV(oakWRegister(EE_HOST_RDX), 64);
		oakAsm->LSL(temp1_w, temp1_w, 3);
		oakAsm->SUB(oakWRegister(EE_HOST_RDX), oakWRegister(EE_HOST_RDX), temp1_w);
		sdlrhelper_emit_oaknut<SHIFTV::xSHR, SHIFTV::xSHL>(oakXRegister(EE_HOST_RDX), temp1_x, oakXRegister(EE_HOST_RAX), temp2_x);
		recMoveGPRtoOakW(addr, _Rs_);
		oakAddSignedImm(addr, addr, _Imm_, oak::util::W4);
		oakAsm->AND(addr, addr, oak::BitImm32(~0x07u));
		oakAsm->MOV(oakXRegister(EE_HOST_RDX), temp2_x);

		recEndOaknutEmit();

		u8* skip_target = oakGetCurrentCodePointer();
		oakPatchCondBranch(skip_branch, skip_target, oak::Cond::EQ, false);

		vtlb_DynGenWrite(64, false, EE_HOST_RCX, temp2);
		_freeX86reg(temp2);
		_freeX86reg(temp1);
	}
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(SDR);
#endif
}

void recSDR()
{
	recSDR_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SDR);
}

//////////////////////////////////////////////////////////////////////////////////////////
/*********************************************************
* Load and store for COP1                                *
* Format:  OP rt, offset(base)                           *
*********************************************************/

////////////////////////////////////////////////////

static void recLWC1_emit_oaknut()
{
#ifndef FPU_RECOMPILE
	recCall(::R5900::Interpreter::OpcodeImpl::LWC1);
#else
	const vtlb_ReadRegAllocCallback alloc_cb = []() { return _allocFPtoXMMreg(_Rt_, MODE_WRITE); };
	if (GPR_IS_CONST1(_Rs_))
	{
		const u32 addr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		vtlb_DynGenReadNonQuad_Const(32, false, true, addr, alloc_cb);
	}
	else
	{
		_freeX86reg(EE_HOST_RCX);
		recLoadAddressToECX_emit_oaknut(false);

		vtlb_DynGenReadNonQuad(32, false, true, EE_HOST_RCX, alloc_cb);
	}

#endif
}

void recLWC1()
{
	recLWC1_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::LWC1);
}

//////////////////////////////////////////////////////

static void recSWC1_emit_oaknut()
{
#ifndef FPU_RECOMPILE
	recCall(::R5900::Interpreter::OpcodeImpl::SWC1);
#else
	const int regt = _allocFPtoXMMreg(_Rt_, MODE_READ);
	if (GPR_IS_CONST1(_Rs_))
	{
		const u32 addr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		vtlb_DynGenWrite_Const(32, true, addr, regt);
	}
	else
	{
		recPrepareStoreAddressToECX_emit_oaknut(false);

		vtlb_DynGenWrite(32, true, EE_HOST_RCX, regt);
	}

#endif
}

void recSWC1()
{
	recSWC1_emit_oaknut();
	EE::Profiler.EmitOp(eeOpcode::SWC1);
}

#endif

} // namespace R5900::Dynarec::OpcodeImpl
