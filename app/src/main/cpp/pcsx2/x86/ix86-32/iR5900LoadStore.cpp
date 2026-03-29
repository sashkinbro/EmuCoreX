// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "x86/iR5900.h"
#include "x86/iR5900LoadStore.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

#define REC_STORES
#define REC_LOADS

static int RETURN_READ_IN_RAX()
{
	return RAX.GetCode();
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

//////////////////////////////////////////////////////////////////////////////////////////
//
static void recLoadQuad(u32 bits, bool sign)
{
	pxAssume(bits == 128);

	// This mess is so we allocate *after* the vtlb flush, not before.
	vtlb_ReadRegAllocCallback alloc_cb = nullptr;
	if (_Rt_)
		alloc_cb = []() { return _allocGPRtoXMMreg(_Rt_, MODE_WRITE); };

	int xmmreg;
	if (GPR_IS_CONST1(_Rs_))
	{
		const u32 srcadr = (g_cpuConstRegs[_Rs_].UL[0] + _Imm_) & ~0x0f;
		xmmreg = vtlb_DynGenReadQuad_Const(bits, srcadr, _Rt_ ? alloc_cb : nullptr);
	}
	else
	{
		// Load ECX with the source memory address that we're reading from.
        _freeX86reg(ECX);
//		_eeMoveGPRtoR(arg1reg, _Rs_);
        _eeMoveGPRtoR(RCX, _Rs_);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }

		// force 16 byte alignment on 128 bit reads
//		xAND(arg1regd, ~0x0F);
        armAsm->And(ECX, ECX, ~0x0F);

		xmmreg = vtlb_DynGenReadQuad(bits, ECX.GetCode(), _Rt_ ? alloc_cb : nullptr);
	}

	// if there was a constant, it should have been invalidated.
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeXMMreg(xmmreg);
}

//////////////////////////////////////////////////////////////////////////////////////////
//
static void recLoad(u32 bits, bool sign)
{
	pxAssume(bits <= 64);

	// This mess is so we allocate *after* the vtlb flush, not before.
	// TODO(Stenzek): If not live, save directly to state, and delete constant.
	vtlb_ReadRegAllocCallback alloc_cb = nullptr;
	if (_Rt_)
		alloc_cb = []() { return _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE); };

	int x86reg;
	if (GPR_IS_CONST1(_Rs_))
	{
		const u32 srcadr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		x86reg = vtlb_DynGenReadNonQuad_Const(bits, sign, false, srcadr, alloc_cb);
	}
	else
	{
		// Load arg1 with the source memory address that we're reading from.
		_freeX86reg(ECX);
//		_eeMoveGPRtoR(arg1regd, _Rs_);
        _eeMoveGPRtoR(ECX, _Rs_);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }

		x86reg = vtlb_DynGenReadNonQuad(bits, sign, false, ECX.GetCode(), alloc_cb);
	}

	// if there was a constant, it should have been invalidated.
	pxAssert(!_Rt_ || !GPR_IS_CONST1(_Rt_));
	if (!_Rt_)
		_freeX86reg(x86reg);
}

//////////////////////////////////////////////////////////////////////////////////////////
//

static void recStore(u32 bits)
{
	// Performance note: Const prop for the store address is good, always.
	// Constprop for the value being stored is not really worthwhile (better to use register
	// allocation -- simpler code and just as fast)

	int regt;
	bool xmm;
	if (bits < 128)
	{
		regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
		xmm = false;
	}
	else
	{
		regt = _allocGPRtoXMMreg(_Rt_, MODE_READ);
		xmm = true;
	}

	// Load ECX with the destination address, or issue a direct optimized write
	// if the address is a constant propagation.

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 dstadr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		if (bits == 128)
			dstadr &= ~0x0f;

		vtlb_DynGenWrite_Const(bits, xmm, dstadr, regt);
	}
	else
	{
		if (_Rs_ != 0)
		{
			// TODO(Stenzek): Preload Rs when it's live. Turn into LEA.
//			_eeMoveGPRtoR(arg1regd, _Rs_);
            _eeMoveGPRtoR(ECX, _Rs_);
			if (_Imm_ != 0) {
//                xADD(arg1regd, _Imm_);
                armAsm->Add(ECX, ECX, _Imm_);
            }
		}
		else
		{
//			xMOV(arg1regd, _Imm_);
            armAsm->Mov(ECX, _Imm_);
		}

		if (bits == 128) {
//            xAND(arg1regd, ~0x0F);
            armAsm->And(ECX, ECX, ~0x0F);
        }

		// TODO(Stenzek): Use Rs directly if imm=0. But beware of upper bits.
		vtlb_DynGenWrite(bits, xmm, ECX.GetCode(), regt);
	}
}


//////////////////////////////////////////////////////////////////////////////////////////
//
void recLB()
{
	recLoad(8, true);
	EE::Profiler.EmitOp(eeOpcode::LB);
}
void recLBU()
{
	recLoad(8, false);
	EE::Profiler.EmitOp(eeOpcode::LBU);
}
void recLH()
{
	recLoad(16, true);
	EE::Profiler.EmitOp(eeOpcode::LH);
}
void recLHU()
{
	recLoad(16, false);
	EE::Profiler.EmitOp(eeOpcode::LHU);
}
void recLW()
{
	recLoad(32, true);
	EE::Profiler.EmitOp(eeOpcode::LW);
}
void recLWU()
{
	recLoad(32, false);
	EE::Profiler.EmitOp(eeOpcode::LWU);
}
void recLD()
{
	recLoad(64, false);
	EE::Profiler.EmitOp(eeOpcode::LD);
}
void recLQ()
{
	recLoadQuad(128, false);
	EE::Profiler.EmitOp(eeOpcode::LQ);
}

void recSB()
{
	recStore(8);
	EE::Profiler.EmitOp(eeOpcode::SB);
}
void recSH()
{
	recStore(16);
	EE::Profiler.EmitOp(eeOpcode::SH);
}
void recSW()
{
	recStore(32);
	EE::Profiler.EmitOp(eeOpcode::SW);
}
void recSD()
{
	recStore(64);
	EE::Profiler.EmitOp(eeOpcode::SD);
}
void recSQ()
{
	recStore(128);
	EE::Profiler.EmitOp(eeOpcode::SQ);
}

////////////////////////////////////////////////////

void recLWL()
{
#ifdef REC_LOADS
	_freeX86reg(EAX);
	_freeX86reg(ECX);
	_freeX86reg(EDX);
//	_freeX86reg(arg1regd);

	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);
	if (_Rs_)
		_addNeededX86reg(X86TYPE_GPR, _Rs_);

	const a64::WRegister temp(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));

//	_eeMoveGPRtoR(arg1regd, _Rs_);
    _eeMoveGPRtoR(ECX, _Rs_);
	if (_Imm_ != 0) {
//        xADD(arg1regd, _Imm_);
        armAsm->Add(ECX, ECX, _Imm_);
    }

	// calleeSavedReg1 = bit offset in word
//	xMOV(temp, arg1regd);
    armAsm->Mov(temp, ECX);
//	xAND(temp, 3);
    armAsm->And(temp, temp, 3);
//	xSHL(temp, 3);
    armAsm->Lsl(temp, temp, 3);

//	xAND(arg1regd, ~3);
    armAsm->And(ECX, ECX, ~3);
	vtlb_DynGenReadNonQuad(32, false, false, ECX.GetCode(), RETURN_READ_IN_RAX);

	if (!_Rt_)
	{
		_freeX86reg(temp);
		return;
	}

	// mask off bytes loaded
//	xMOV(ecx, temp);
    armAsm->Mov(ECX, temp);
	_freeX86reg(temp);

	const int treg = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE);
//	xMOV(edx, 0xffffff);
    armAsm->Mov(EDX, 0xffffff);
//	xSHR(edx, cl);
    armAsm->Lsr(EDX, EDX, ECX);
//	xAND(edx, xRegister32(treg));
    armAsm->And(EDX, EDX, a64::WRegister(treg));

	// OR in bytes loaded
//	xNEG(ecx);
    armAsm->Neg(ECX, ECX);
//	xADD(ecx, 24);
    armAsm->Add(ECX, ECX, 24);
//	xSHL(eax, cl);
    armAsm->Lsl(EAX, EAX, ECX);
//	xOR(eax, edx);
    armAsm->Orr(EAX, EAX, EDX);
//	xMOVSX(xRegister64(treg), eax);
    armAsm->Sxtw(a64::XRegister(treg), EAX);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);

	recCall(LWL);
#endif

	EE::Profiler.EmitOp(eeOpcode::LWL);
}

////////////////////////////////////////////////////
void recLWR()
{
#ifdef REC_LOADS
	_freeX86reg(EAX);
	_freeX86reg(ECX);
	_freeX86reg(EDX);
//	_freeX86reg(arg1regd);

	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);
	if (_Rs_)
		_addNeededX86reg(X86TYPE_GPR, _Rs_);

    const a64::WRegister temp(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));

//	_eeMoveGPRtoR(arg1regd, _Rs_);
    _eeMoveGPRtoR(ECX, _Rs_);
	if (_Imm_ != 0) {
//        xADD(arg1regd, _Imm_);
        armAsm->Add(ECX, ECX, _Imm_);
    }

	// edi = bit offset in word
//	xMOV(temp, arg1regd);
    armAsm->Mov(temp, ECX);

//	xAND(arg1regd, ~3);
    armAsm->And(ECX, ECX, ~3);
	vtlb_DynGenReadNonQuad(32, false, false, ECX.GetCode(), RETURN_READ_IN_RAX);

	if (!_Rt_)
	{
		_freeX86reg(temp);
		return;
	}

	const int treg = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE);
    auto reg32 = a64::WRegister(treg);

//	xAND(temp, 3);
    armAsm->And(temp, temp, 3);

//	xForwardJE8 nomask;
    a64::Label nomask;
    armAsm->Cbz(temp, &nomask);
//	xSHL(temp, 3);
    armAsm->Lsl(temp, temp, 3);
	// mask off bytes loaded
//	xMOV(ecx, 24);
    armAsm->Mov(ECX, 24);
//	xSUB(ecx, temp);
    armAsm->Sub(ECX, ECX, temp);
//	xMOV(edx, 0xffffff00);
    armAsm->Mov(EDX, 0xffffff00);
//	xSHL(edx, cl);
    armAsm->Lsl(EDX, EDX, ECX);
//	xAND(xRegister32(treg), edx);
    armAsm->And(reg32, reg32, EDX);

	// OR in bytes loaded
//	xMOV(ecx, temp);
    armAsm->Mov(ECX, temp);
//	xSHR(eax, cl);
    armAsm->Lsr(EAX, EAX, ECX);
//	xOR(xRegister32(treg), eax);
    armAsm->Orr(reg32, reg32, EAX);

//	xForwardJump8 end;
    a64::Label end;
    armAsm->B(&end);
//	nomask.SetTarget();
    armBind(&nomask);
	// NOTE: This might look wrong, but it's correct - see interpreter.
//	xMOVSX(xRegister64(treg), eax);
    armAsm->Sxtw(a64::XRegister(treg), EAX);
//	end.SetTarget();
    armBind(&end);
	_freeX86reg(temp);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);

	recCall(LWR);
#endif

	EE::Profiler.EmitOp(eeOpcode::LWR);
}

////////////////////////////////////////////////////

void recSWL()
{
#ifdef REC_STORES
	// avoid flushing and immediately reading back
	_addNeededX86reg(X86TYPE_GPR, _Rs_);

	// preload Rt, since we can't do so inside the branch
	if (!GPR_IS_CONST1(_Rt_))
		_allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
	else
		_addNeededX86reg(X86TYPE_GPR, _Rt_);

    const a64::WRegister temp(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));
	_freeX86reg(EAX);
	_freeX86reg(ECX);
    _freeX86reg(EDX);
//	_freeX86reg(arg1regd);
//	_freeX86reg(arg2regd);

//	_eeMoveGPRtoR(arg1regd, _Rs_);
    _eeMoveGPRtoR(ECX, _Rs_);
	if (_Imm_ != 0) {
//        xADD(arg1regd, _Imm_);
        armAsm->Add(ECX, ECX, _Imm_);
    }

	// edi = bit offset in word
//	xMOV(temp, arg1regd);
    armAsm->Mov(temp, ECX);
//	xAND(arg1regd, ~3);
    armAsm->And(ECX, ECX, ~3);
//	xAND(temp, 3);
    armAsm->And(temp, temp, 3);
//	xCMP(temp, 3);
    armAsm->Cmp(temp, 3);

	// If we're not using fastmem, we need to flush early. Because the first read
	// (which would flush) happens inside a branch.
	if (!CHECK_FASTMEM || vtlb_IsFaultingPC(pc))
		iFlushCall(FLUSH_FULLVTLB);

//	xForwardJE8 skip;
    a64::Label skip;
    armAsm->B(&skip, a64::Condition::eq);
//	xSHL(temp, 3);
    armAsm->Lsl(temp, temp, 3);

	vtlb_DynGenReadNonQuad(32, false, false, ECX.GetCode(), RETURN_READ_IN_RAX);

	// mask read -> arg2
//	xMOV(ecx, temp);
    armAsm->Mov(ECX, temp);
//	xMOV(arg2regd, 0xffffff00);
    armAsm->Mov(EDX, 0xffffff00);
//	xSHL(arg2regd, cl);
    armAsm->Lsl(EDX, EDX, ECX);
//	xAND(arg2regd, eax);
    armAsm->And(EDX, EDX, EAX);

	if (_Rt_)
	{
		// mask write and OR -> edx
//		xNEG(ecx);
        armAsm->Neg(ECX, ECX);
//		xADD(ecx, 24);
        armAsm->Add(ECX, ECX, 24);
//		_eeMoveGPRtoR(eax, _Rt_, false);
        _eeMoveGPRtoR(EAX, _Rt_, false);
//		xSHR(eax, cl);
        armAsm->Lsr(EAX, EAX, ECX);
//		xOR(arg2regd, eax);
        armAsm->Orr(EDX, EDX, EAX);
	}

//	_eeMoveGPRtoR(arg1regd, _Rs_, false);
    _eeMoveGPRtoR(ECX, _Rs_, false);
	if (_Imm_ != 0) {
//        xADD(arg1regd, _Imm_);
        armAsm->Add(ECX, ECX, _Imm_);
    }
//	xAND(arg1regd, ~3);
    armAsm->And(ECX, ECX, ~3);

//	xForwardJump8 end;
    a64::Label end;
    armAsm->B(&end);
//	skip.SetTarget();
    armBind(&skip);
//	_eeMoveGPRtoR(arg2regd, _Rt_, false);
    _eeMoveGPRtoR(EDX, _Rt_, false);
//	end.SetTarget();
    armBind(&end);

	_freeX86reg(temp);
//	vtlb_DynGenWrite(32, false, arg1regd.GetId(), arg2regd.GetId());
    vtlb_DynGenWrite(32, false, ECX.GetCode(), EDX.GetCode());
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(SWL);
#endif

	EE::Profiler.EmitOp(eeOpcode::SWL);
}

////////////////////////////////////////////////////
void recSWR()
{
#ifdef REC_STORES
	// avoid flushing and immediately reading back
	_addNeededX86reg(X86TYPE_GPR, _Rs_);

	// preload Rt, since we can't do so inside the branch
	if (!GPR_IS_CONST1(_Rt_))
		_allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
	else
		_addNeededX86reg(X86TYPE_GPR, _Rt_);

    const a64::WRegister temp(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));
	_freeX86reg(ECX);
    _freeX86reg(EDX);
//	_freeX86reg(arg1regd);
//	_freeX86reg(arg2regd);

//	_eeMoveGPRtoR(arg1regd, _Rs_);
    _eeMoveGPRtoR(ECX, _Rs_);
	if (_Imm_ != 0) {
//        xADD(arg1regd, _Imm_);
        armAsm->Add(ECX, ECX, _Imm_);
    }

	// edi = bit offset in word
//	xMOV(temp, arg1regd);
    armAsm->Mov(temp, ECX);
//	xAND(arg1regd, ~3);
    armAsm->Ands(ECX, ECX, ~3);
//	xAND(temp, 3);
    armAsm->Ands(temp, temp, 3);

	// If we're not using fastmem, we need to flush early. Because the first read
	// (which would flush) happens inside a branch.
	if (!CHECK_FASTMEM || vtlb_IsFaultingPC(pc))
		iFlushCall(FLUSH_FULLVTLB);

//	xForwardJE8 skip;
    a64::Label skip;
    armAsm->B(&skip, a64::Condition::eq);
//	xSHL(temp, 3);
    armAsm->Lsl(temp, temp, 3);

	vtlb_DynGenReadNonQuad(32, false, false, ECX.GetCode(), RETURN_READ_IN_RAX);

	// mask read -> edx
//	xMOV(ecx, 24);
    armAsm->Mov(ECX, 24);
//	xSUB(ecx, temp);
    armAsm->Sub(ECX, ECX, temp);
//	xMOV(arg2regd, 0xffffff);
    armAsm->Mov(EDX, 0xffffff);
//	xSHR(arg2regd, cl);
    armAsm->Lsr(EDX, EDX, ECX);
//	xAND(arg2regd, eax);
    armAsm->And(EDX, EDX, EAX);

	if (_Rt_)
	{
		// mask write and OR -> edx
//		xMOV(ecx, temp);
        armAsm->Mov(ECX, temp);
//		_eeMoveGPRtoR(eax, _Rt_, false);
        _eeMoveGPRtoR(EAX, _Rt_, false);
//		xSHL(eax, cl);
        armAsm->Lsl(EAX, EAX, ECX);
//		xOR(arg2regd, eax);
        armAsm->Orr(EDX, EDX, EAX);
	}

//	_eeMoveGPRtoR(arg1regd, _Rs_, false);
    _eeMoveGPRtoR(ECX, _Rs_, false);
	if (_Imm_ != 0) {
//        xADD(arg1regd, _Imm_);
        armAsm->Add(ECX, ECX, _Imm_);
    }
//	xAND(arg1regd, ~3);
    armAsm->And(ECX, ECX, ~3);

//	xForwardJump8 end;
    a64::Label end;
    armAsm->B(&end);
//	skip.SetTarget();
    armBind(&skip);
//	_eeMoveGPRtoR(arg2regd, _Rt_, false);
    _eeMoveGPRtoR(EDX, _Rt_, false);
//	end.SetTarget();
    armBind(&end);

	_freeX86reg(temp);
//	vtlb_DynGenWrite(32, false, arg1regd.GetId(), arg2regd.GetId());
    vtlb_DynGenWrite(32, false, ECX.GetCode(), EDX.GetCode());
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(SWR);
#endif

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

/// Masks rt with (0xffffffffffffffff maskshift maskamt), merges with (value shift amt), leaves result in value
//static void ldlrhelper_const(int maskamt, const xImpl_Group2& maskshift, int amt, const xImpl_Group2& shift, const xRegister64& value, const xRegister64& rt)
static void ldlrhelper_const(int maskamt, const SHIFTV maskshift, int amt, const SHIFTV shift, const a64::XRegister& value, const a64::XRegister& rt)
{
	pxAssert(rt.GetCode() != ECX.GetCode() && value.GetCode() != ECX.GetCode());

	// Would xor rcx, rcx; not rcx be better here?
//	xMOV(rcx, -1);
    armAsm->Mov(RCX, -1);

//	maskshift(rcx, maskamt);
    switch (maskshift)
    {
        case SHIFTV::xSHR:
            armAsm->Lsr(RCX, RCX, maskamt);
            break;
        case SHIFTV::xSAR:
            armAsm->Asr(RCX, RCX, maskamt);
            break;
        case SHIFTV::xSHL:
            armAsm->Lsl(RCX, RCX, maskamt);
            break;
    }

//	xAND(rt, rcx);
    armAsm->And(rt, rt, RCX);

//	shift(value, amt);
    switch (shift)
    {
        case SHIFTV::xSHR:
            armAsm->Lsr(value, value, amt);
            break;
        case SHIFTV::xSAR:
            armAsm->Asr(value, value, amt);
            break;
        case SHIFTV::xSHL:
            armAsm->Lsl(value, value, amt);
            break;
    }

//	xOR(rt, value);
    armAsm->Orr(rt, rt, value);
}

/// Masks rt with (0xffffffffffffffff maskshift maskamt), merges with (value shift amt), leaves result in value
//static void ldlrhelper(const xRegister32& maskamt, const xImpl_Group2& maskshift, const xRegister32& amt, const xImpl_Group2& shift, const xRegister64& value, const xRegister64& rt)
static void ldlrhelper(const a64::Register& maskamt, const SHIFTV maskshift, const a64::Register& amt, const SHIFTV shift, const a64::Register& value, const a64::Register& rt)
{
	pxAssert(rt.GetCode() != ECX.GetCode() && amt.GetCode() != ECX.GetCode() && value.GetCode() != ECX.GetCode());

	// Would xor rcx, rcx; not rcx be better here?
    const a64::XRegister maskamt64(maskamt.GetCode());
//	xMOV(ecx, maskamt);
    armAsm->Mov(ECX, maskamt);
//	xMOV(maskamt64, -1);
    armAsm->Mov(maskamt64, -1);
//	maskshift(maskamt64, cl);
    switch (maskshift)
    {
        case SHIFTV::xSHR:
            armAsm->Lsr(maskamt64, maskamt64, RCX);
            break;
        case SHIFTV::xSAR:
            armAsm->Asr(maskamt64, maskamt64, RCX);
            break;
        case SHIFTV::xSHL:
            armAsm->Lsl(maskamt64, maskamt64, RCX);
            break;
    }

//	xAND(rt, maskamt64);
    armAsm->And(rt, rt, maskamt64);

//	xMOV(ecx, amt);
    armAsm->Mov(ECX, amt);

//	shift(value, cl);
    switch (shift)
    {
        case SHIFTV::xSHR:
            armAsm->Lsr(value, value, RCX);
            break;
        case SHIFTV::xSAR:
            armAsm->Asr(value, value, RCX);
            break;
        case SHIFTV::xSHL:
            armAsm->Lsl(value, value, RCX);
            break;
    }

//	xOR(rt, value);
    armAsm->Orr(rt, rt, value);
}

void recLDL()
{
	if (!_Rt_)
		return;

#ifdef REC_LOADS
	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);
	if (_Rs_)
		_addNeededX86reg(X86TYPE_GPR, _Rs_);

    const a64::WRegister temp1(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));
	_freeX86reg(EAX);
	_freeX86reg(ECX);
	_freeX86reg(EDX);
//	_freeX86reg(arg1regd);

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 srcadr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;

		// If _Rs_ is equal to _Rt_ we need to put the shift in to eax since it won't take the CONST path.
		if (_Rs_ == _Rt_) {
//            xMOV(temp1, srcadr);
            armAsm->Mov(temp1, srcadr);
        }

		srcadr &= ~0x07;

		vtlb_DynGenReadNonQuad_Const(64, false, false, srcadr, RETURN_READ_IN_RAX);
	}
	else
	{
		// Load ECX with the source memory address that we're reading from.
//		_freeX86reg(arg1regd);
        _freeX86reg(ECX);
//		_eeMoveGPRtoR(arg1regd, _Rs_);
        _eeMoveGPRtoR(ECX, _Rs_);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }

//		xMOV(temp1, arg1regd);
        armAsm->Mov(temp1, ECX);
//		xAND(arg1regd, ~0x07);
        armAsm->And(ECX, ECX, ~0x07);

		vtlb_DynGenReadNonQuad(64, false, false, ECX.GetCode(), RETURN_READ_IN_RAX);
	}

    const a64::XRegister treg(_allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE));

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 shift = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		shift = ((shift & 0x7) + 1) * 8;
		if (shift != 64)
		{
//			ldlrhelper_const(shift, xSHR, 64 - shift, xSHL, rax, treg);
            ldlrhelper_const(shift, SHIFTV::xSHR, 64 - shift, SHIFTV::xSHL, a64::XRegister(RAX), treg);
		}
		else
		{
//			xMOV(treg, rax);
            armAsm->Mov(treg, RAX);
		}
	}
	else
	{
//		xAND(temp1, 0x7);
        armAsm->And(temp1, temp1, 0x7);
//		xCMP(temp1, 7);
        armAsm->Cmp(temp1, 7);
//		xCMOVE(treg, rax); // swap register with memory when not shifting
        armAsm->Csel(treg, RAX, treg, a64::Condition::eq);
//		xForwardJE8 skip;
        a64::Label skip;
        armAsm->B(&skip, a64::Condition::eq);
		// Calculate the shift from top bit to lowest.
//		xADD(temp1, 1);
        armAsm->Add(temp1, temp1, 1);
//		xMOV(edx, 64);
        armAsm->Mov(EDX, 64);
//		xSHL(temp1, 3);
        armAsm->Lsl(temp1, temp1, 3);
//		xSUB(edx, temp1);
        armAsm->Sub(EDX, EDX, temp1);

//		ldlrhelper(temp1, xSHR, edx, xSHL, rax, treg);
        ldlrhelper(temp1, SHIFTV::xSHR, EDX, SHIFTV::xSHL, RAX, treg);
//		skip.SetTarget();
        armBind(&skip);
	}

	_freeX86reg(temp1);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(LDL);
#endif

	EE::Profiler.EmitOp(eeOpcode::LDL);
}

////////////////////////////////////////////////////
void recLDR()
{
	if (!_Rt_)
		return;

#ifdef REC_LOADS
	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);
	if (_Rs_)
		_addNeededX86reg(X86TYPE_GPR, _Rs_);

    const a64::WRegister temp1(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));
	_freeX86reg(EAX);
	_freeX86reg(ECX);
	_freeX86reg(EDX);
//	_freeX86reg(arg1regd);

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 srcadr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;

		// If _Rs_ is equal to _Rt_ we need to put the shift in to eax since it won't take the CONST path.
		if (_Rs_ == _Rt_) {
//            xMOV(temp1, srcadr);
            armAsm->Mov(temp1, srcadr);
        }

		srcadr &= ~0x07;

		vtlb_DynGenReadNonQuad_Const(64, false, false, srcadr, RETURN_READ_IN_RAX);
	}
	else
	{
		// Load ECX with the source memory address that we're reading from.
//		_freeX86reg(arg1regd);
        _freeX86reg(ECX);
//		_eeMoveGPRtoR(arg1regd, _Rs_);
        _eeMoveGPRtoR(ECX, _Rs_);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }

//		xMOV(temp1, arg1regd);
        armAsm->Mov(temp1, ECX);
//		xAND(arg1regd, ~0x07);
        armAsm->And(ECX, ECX, ~0x07);

		vtlb_DynGenReadNonQuad(64, false, false, ECX.GetCode(), RETURN_READ_IN_RAX);
	}

    const a64::XRegister treg(_allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ | MODE_WRITE));

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 shift = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		shift = (shift & 0x7) * 8;
		if (shift != 0)
		{
//			ldlrhelper_const(64 - shift, xSHL, shift, xSHR, rax, treg);
            ldlrhelper_const(64 - shift, SHIFTV::xSHL, shift, SHIFTV::xSHR, a64::XRegister(RAX), treg);
		}
		else
		{
//			xMOV(treg, rax);
            armAsm->Mov(treg, RAX);
		}
	}
	else
	{
//		xAND(temp1, 0x7);
        armAsm->Ands(temp1, temp1, 0x7);
//		xCMOVE(treg, rax); // swap register with memory when not shifting
        armAsm->Csel(treg, RAX, treg, a64::Condition::eq);
//		xForwardJE8 skip;
        a64::Label skip;
        armAsm->B(&skip, a64::Condition::eq);
		// Calculate the shift from top bit to lowest.
//		xMOV(edx, 64);
        armAsm->Mov(EDX, 64);
//		xSHL(temp1, 3);
        armAsm->Lsl(temp1, temp1, 3);
//		xSUB(edx, temp1);
        armAsm->Sub(EDX, EDX, temp1);

//		ldlrhelper(edx, xSHL, temp1, xSHR, rax, treg);
        ldlrhelper(EDX, SHIFTV::xSHL, temp1, SHIFTV::xSHR, RAX, treg);
//		skip.SetTarget();
        armBind(&skip);
	}

	_freeX86reg(temp1);
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(LDR);
#endif

	EE::Profiler.EmitOp(eeOpcode::LDR);
}

////////////////////////////////////////////////////

/// Masks value with (0xffffffffffffffff maskshift maskamt), merges with (rt shift amt), saves to dummyValue
//static void sdlrhelper_const(int maskamt, const xImpl_Group2& maskshift, int amt, const xImpl_Group2& shift, const xRegister64& value, const xRegister64& rt)
static void sdlrhelper_const(int maskamt, const SHIFTV maskshift, int amt, const SHIFTV shift, const a64::XRegister& value, const a64::XRegister& rt)
{
	pxAssert(rt.GetCode() != ECX.GetCode() && value.GetCode() != ECX.GetCode());
//	xMOV(rcx, -1);
    armAsm->Mov(RCX, -1);

//	maskshift(rcx, maskamt);
    switch (maskshift)
    {
        case SHIFTV::xSHR:
            armAsm->Lsr(RCX, RCX, maskamt);
            break;
        case SHIFTV::xSAR:
            armAsm->Asr(RCX, RCX, maskamt);
            break;
        case SHIFTV::xSHL:
            armAsm->Lsl(RCX, RCX, maskamt);
            break;
    }

//	xAND(rcx, value);
    armAsm->And(RCX, RCX, value);

//	shift(rt, amt);
    switch (shift)
    {
        case SHIFTV::xSHR:
            armAsm->Lsr(rt, rt, amt);
            break;
        case SHIFTV::xSAR:
            armAsm->Asr(rt, rt, amt);
            break;
        case SHIFTV::xSHL:
            armAsm->Lsl(rt, rt, amt);
            break;
    }

//	xOR(rt, rcx);
    armAsm->Orr(rt, rt, RCX);
}

/// Masks value with (0xffffffffffffffff maskshift maskamt), merges with (rt shift amt), saves to dummyValue
//static void sdlrhelper(const xRegister32& maskamt, const xImpl_Group2& maskshift, const xRegister32& amt, const xImpl_Group2& shift, const xRegister64& value, const xRegister64& rt)
static void sdlrhelper(const a64::Register& maskamt, const SHIFTV maskshift, const a64::Register& amt, const SHIFTV shift, const a64::Register& value, const a64::Register& rt)
{
	pxAssert(rt.GetCode() != ECX.GetCode() && amt.GetCode() != ECX.GetCode() && value.GetCode() != ECX.GetCode());

	// Generate mask 128-(shiftx8)
    const a64::XRegister maskamt64(maskamt.GetCode());
//	xMOV(ecx, maskamt);
    armAsm->Mov(ECX, maskamt);
//	xMOV(maskamt64, -1);
    armAsm->Mov(maskamt64, -1);

//	maskshift(maskamt64, cl);
    switch (maskshift)
    {
        case SHIFTV::xSHR:
            armAsm->Lsr(maskamt64, maskamt64, RCX);
            break;
        case SHIFTV::xSAR:
            armAsm->Asr(maskamt64, maskamt64, RCX);
            break;
        case SHIFTV::xSHL:
            armAsm->Lsl(maskamt64, maskamt64, RCX);
            break;
    }

//	xAND(maskamt64, value);
    armAsm->And(maskamt64, maskamt64, value);

	// Shift over reg value
//	xMOV(ecx, amt);
    armAsm->Mov(ECX, amt);

//	shift(rt, cl);
    switch (shift)
    {
        case SHIFTV::xSHR:
            armAsm->Lsr(rt, rt, RCX);
            break;
        case SHIFTV::xSAR:
            armAsm->Asr(rt, rt, RCX);
            break;
        case SHIFTV::xSHL:
            armAsm->Lsl(rt, rt, RCX);
            break;
    }

//	xOR(rt, maskamt64);
    armAsm->Orr(rt, rt, maskamt64);
}

void recSDL()
{
#ifdef REC_STORES
	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);

	_freeX86reg(ECX);
    _freeX86reg(EDX);
//	_freeX86reg(arg2regd);

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 adr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		u32 aligned = adr & ~0x07;
		u32 shift = ((adr & 0x7) + 1) * 8;
		if (shift == 64)
		{
//			_eeMoveGPRtoR(arg2reg, _Rt_);
            _eeMoveGPRtoR(RDX, _Rt_);
		}
		else
		{
			vtlb_DynGenReadNonQuad_Const(64, false, false, aligned, RETURN_READ_IN_RAX);
//			_eeMoveGPRtoR(arg2reg, _Rt_);
            _eeMoveGPRtoR(RDX, _Rt_);
//			sdlrhelper_const(shift, xSHL, 64 - shift, xSHR, rax, arg2reg);
            sdlrhelper_const(shift, SHIFTV::xSHL, 64 - shift, SHIFTV::xSHR, a64::XRegister(RAX), a64::XRegister(RDX));
		}
		vtlb_DynGenWrite_Const(64, false, aligned, EDX.GetCode());
	}
	else
	{
		if (_Rs_)
			_addNeededX86reg(X86TYPE_GPR, _Rs_);

		// Load ECX with the source memory address that we're reading from.
//		_freeX86reg(arg1regd);
        _freeX86reg(ECX);
//		_eeMoveGPRtoR(arg1regd, _Rs_);
        _eeMoveGPRtoR(ECX, _Rs_);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }

		_freeX86reg(ECX);
		_freeX86reg(EDX);
//		_freeX86reg(arg2regd);

        const a64::WRegister temp1(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));
        const a64::XRegister temp2(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));
//		_eeMoveGPRtoR(arg2reg, _Rt_);
        _eeMoveGPRtoR(RDX, _Rt_);

//		xMOV(temp1, arg1regd);
        armAsm->Mov(temp1, ECX);
//		xMOV(temp2, arg2reg);
        armAsm->Mov(temp2, RDX);
//		xAND(arg1regd, ~0x07);
        armAsm->And(ECX, ECX, ~0x07);
//		xAND(temp1, 0x7);
        armAsm->And(temp1, temp1, 0x7);
//		xCMP(temp1, 7);
        armAsm->Cmp(temp1, 7);

		// If we're not using fastmem, we need to flush early. Because the first read
		// (which would flush) happens inside a branch.
		if (!CHECK_FASTMEM || vtlb_IsFaultingPC(pc))
			iFlushCall(FLUSH_FULLVTLB);

//		xForwardJE8 skip;
        a64::Label skip;
        armAsm->B(&skip, a64::Condition::eq);
//		xADD(temp1, 1);
        armAsm->Add(temp1, temp1, 1);
		vtlb_DynGenReadNonQuad(64, false, false, ECX.GetCode(), RETURN_READ_IN_RAX);

		//Calculate the shift from top bit to lowest
//		xMOV(edx, 64);
        armAsm->Mov(EDX, 64);
//		xSHL(temp1, 3);
        armAsm->Lsl(temp1, temp1, 3);
//		xSUB(edx, temp1);
        armAsm->Sub(EDX, EDX, temp1);

//		sdlrhelper(temp1, xSHL, edx, xSHR, rax, temp2);
        sdlrhelper(temp1, SHIFTV::xSHL, EDX, SHIFTV::xSHR, RAX, temp2);

//		_eeMoveGPRtoR(arg1regd, _Rs_, false);
        _eeMoveGPRtoR(ECX, _Rs_, false);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }
//		xAND(arg1regd, ~0x7);
        armAsm->And(ECX, ECX, ~0x7);
//		skip.SetTarget();
        armBind(&skip);

//		vtlb_DynGenWrite(64, false, arg1regd.GetId(), temp2.GetId());
        vtlb_DynGenWrite(64, false, ECX.GetCode(), temp2.GetCode());
		_freeX86reg(temp2.GetCode());
		_freeX86reg(temp1.GetCode());
	}
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(SDL);
#endif
	EE::Profiler.EmitOp(eeOpcode::SDL);
}

////////////////////////////////////////////////////
void recSDR()
{
#ifdef REC_STORES
	// avoid flushing and immediately reading back
	if (_Rt_)
		_addNeededX86reg(X86TYPE_GPR, _Rt_);

	_freeX86reg(ECX);
    _freeX86reg(EDX);
//	_freeX86reg(arg2regd);

	if (GPR_IS_CONST1(_Rs_))
	{
		u32 adr = g_cpuConstRegs[_Rs_].UL[0] + _Imm_;
		u32 aligned = adr & ~0x07;
		u32 shift = (adr & 0x7) * 8;
		if (shift == 0)
		{
//			_eeMoveGPRtoR(arg2reg, _Rt_);
            _eeMoveGPRtoR(RDX, _Rt_);
		}
		else
		{
			vtlb_DynGenReadNonQuad_Const(64, false, false, aligned, RETURN_READ_IN_RAX);
//			_eeMoveGPRtoR(arg2reg, _Rt_);
            _eeMoveGPRtoR(RDX, _Rt_);
//			sdlrhelper_const(64 - shift, xSHR, shift, xSHL, rax, arg2reg);
            sdlrhelper_const(64 - shift, SHIFTV::xSHR, shift, SHIFTV::xSHL, a64::XRegister(RAX), a64::XRegister(RDX));
		}

		vtlb_DynGenWrite_Const(64, false, aligned, RDX.GetCode());
	}
	else
	{
		if (_Rs_)
			_addNeededX86reg(X86TYPE_GPR, _Rs_);

		// Load ECX with the source memory address that we're reading from.
//		_eeMoveGPRtoR(arg1regd, _Rs_);
        _eeMoveGPRtoR(ECX, _Rs_);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }

		_freeX86reg(ECX);
		_freeX86reg(EDX);
//		_freeX86reg(arg2regd);

        const a64::WRegister temp1(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));
        const a64::XRegister temp2(_allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED));
//		_eeMoveGPRtoR(arg2reg, _Rt_);
        _eeMoveGPRtoR(RDX, _Rt_);

//		xMOV(temp1, arg1regd);
        armAsm->Mov(temp1, ECX);
//		xMOV(temp2, arg2reg);
        armAsm->Mov(temp2, RDX);
//		xAND(arg1regd, ~0x07);
        armAsm->Ands(ECX, ECX, ~0x07);
//		xAND(temp1, 0x7);
        armAsm->Ands(temp1, temp1, 0x7);

		// If we're not using fastmem, we need to flush early. Because the first read
		// (which would flush) happens inside a branch.
		if (!CHECK_FASTMEM || vtlb_IsFaultingPC(pc))
			iFlushCall(FLUSH_FULLVTLB);

//		xForwardJE8 skip;
        a64::Label skip;
        armAsm->B(&skip, a64::Condition::eq);
		vtlb_DynGenReadNonQuad(64, false, false, ECX.GetCode(), RETURN_READ_IN_RAX);

//		xMOV(edx, 64);
        armAsm->Mov(EDX, 64);
//		xSHL(temp1, 3);
        armAsm->Lsl(temp1, temp1, 3);
//		xSUB(edx, temp1);
        armAsm->Sub(EDX, EDX, temp1);

//		sdlrhelper(edx, xSHR, temp1, xSHL, rax, temp2);
        sdlrhelper(EDX, SHIFTV::xSHR, temp1, SHIFTV::xSHL, RAX, temp2);

//		_eeMoveGPRtoR(arg1regd, _Rs_, false);
        _eeMoveGPRtoR(ECX, _Rs_, false);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }
//		xAND(arg1regd, ~0x7);
        armAsm->And(ECX, ECX, ~0x7);
//		xMOV(arg2reg, temp2);
        armAsm->Mov(RDX, temp2);
//		skip.SetTarget();
        armBind(&skip);

//		vtlb_DynGenWrite(64, false, arg1regd.GetId(), temp2.GetId());
        vtlb_DynGenWrite(64, false, ECX.GetCode(), temp2.GetCode());
		_freeX86reg(temp2.GetCode());
		_freeX86reg(temp1.GetCode());
	}
#else
	iFlushCall(FLUSH_INTERPRETER);
	_deleteEEreg(_Rs_, 1);
	_deleteEEreg(_Rt_, 1);
	recCall(SDR);
#endif
	EE::Profiler.EmitOp(eeOpcode::SDR);
}

//////////////////////////////////////////////////////////////////////////////////////////
/*********************************************************
* Load and store for COP1                                *
* Format:  OP rt, offset(base)                           *
*********************************************************/

////////////////////////////////////////////////////

void recLWC1()
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
//		_freeX86reg(arg1regd);
        _freeX86reg(ECX);
//		_eeMoveGPRtoR(arg1regd, _Rs_);
        _eeMoveGPRtoR(ECX, _Rs_);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }

		vtlb_DynGenReadNonQuad(32, false, true, ECX.GetCode(), alloc_cb);
	}

	EE::Profiler.EmitOp(eeOpcode::LWC1);
#endif
}

//////////////////////////////////////////////////////

void recSWC1()
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
//		_freeX86reg(arg1regd);
        _freeX86reg(ECX);
//		_eeMoveGPRtoR(arg1regd, _Rs_);
        _eeMoveGPRtoR(ECX, _Rs_);
		if (_Imm_ != 0) {
//            xADD(arg1regd, _Imm_);
            armAsm->Add(ECX, ECX, _Imm_);
        }

		vtlb_DynGenWrite(32, true, ECX.GetCode(), regt);
	}

	EE::Profiler.EmitOp(eeOpcode::SWC1);
#endif
}

#endif

} // namespace R5900::Dynarec::OpcodeImpl
