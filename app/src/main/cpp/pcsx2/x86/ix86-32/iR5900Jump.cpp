// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "x86/iR5900.h"

using namespace x86Emitter;

namespace R5900::Dynarec::OpcodeImpl
{

/*********************************************************
* Jump to target                                         *
* Format:  OP target                                     *
*********************************************************/
#ifndef JUMP_RECOMPILE

namespace Interp = R5900::Interpreter::OpcodeImpl;

REC_SYS(J);
REC_SYS_DEL(JAL, 31);
REC_SYS(JR);
REC_SYS_DEL(JALR, _Rd_);

#else

namespace Interp = R5900::Interpreter::OpcodeImpl;

#define REC_JUMP_DELAY_FALLBACK(op) \
	do \
	{ \
		if (HasBranchInDelaySlot()) \
		{ \
			recBranchCall(Interp::op); \
			return; \
		} \
	} while (0)

#define REC_JUMP_DELAY_FALLBACK_DEL(op, delreg) \
	do \
	{ \
		if (HasBranchInDelaySlot()) \
		{ \
			if ((delreg) > 0) \
				_deleteEEreg((delreg), 1); \
			recBranchCall(Interp::op); \
			return; \
		} \
	} while (0)

////////////////////////////////////////////////////
void recJ()
{
	REC_JUMP_DELAY_FALLBACK(J);

	EE::Profiler.EmitOp(eeOpcode::J);

	// SET_FPUSTATE;
	u32 newpc = (_InstrucTarget_ << 2) + (pc & 0xf0000000);
	recompileNextInstruction(true, false);
	if (EmuConfig.Gamefixes.GoemonTlbHack)
		SetBranchImm(vtlb_V2P(newpc));
	else
		SetBranchImm(newpc);
}

////////////////////////////////////////////////////
void recJAL()
{
	REC_JUMP_DELAY_FALLBACK_DEL(JAL, 31);

	EE::Profiler.EmitOp(eeOpcode::JAL);

	u32 newpc = (_InstrucTarget_ << 2) + (pc & 0xf0000000);
	_deleteEEreg(31, 0);
	if (EE_CONST_PROP)
	{
		GPR_SET_CONST(31);
		g_cpuConstRegs[31].UL[0] = pc + 4;
		g_cpuConstRegs[31].UL[1] = 0;
	}
	else
	{
//		xMOV(ptr32[&cpuRegs.GPR.r[31].UL[0]], pc + 4);
        armStore(PTR_CPU(cpuRegs.GPR.r[31].UL[0]), pc + 4);
//		xMOV(ptr32[&cpuRegs.GPR.r[31].UL[1]], 0);
        armStore(PTR_CPU(cpuRegs.GPR.r[31].UL[1]), 0);
	}

	recompileNextInstruction(true, false);
	if (EmuConfig.Gamefixes.GoemonTlbHack)
		SetBranchImm(vtlb_V2P(newpc));
	else
		SetBranchImm(newpc);
}

/*********************************************************
* Register jump                                          *
* Format:  OP rs, rd                                     *
*********************************************************/

////////////////////////////////////////////////////
void recJR()
{
	REC_JUMP_DELAY_FALLBACK(JR);

	EE::Profiler.EmitOp(eeOpcode::JR);

	const bool swap = EmuConfig.Gamefixes.GoemonTlbHack ? false : TrySwapDelaySlot(_Rs_, 0, 0, true);
	if (!swap)
	{
		_eeMoveGPRtoM(PTR_CPU(cpuRegs.pcWriteback), _Rs_);

		if (EmuConfig.Gamefixes.GoemonTlbHack)
		{
			armLoad(ECX, PTR_CPU(cpuRegs.pcWriteback));
			vtlb_DynV2P();
			armStore(PTR_CPU(cpuRegs.pcWriteback), EAX);
		}

		recompileNextInstruction(true, false);
		armLoad(EAX, PTR_CPU(cpuRegs.pcWriteback));
	}
	else
	{
		if (GPR_IS_DIRTY_CONST(_Rs_) || _hasX86reg(X86TYPE_GPR, _Rs_, 0))
		{
			const int x86reg = _allocX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
			armAsm->Mov(EAX, a64::WRegister(x86reg));
		}
		else
		{
			_eeMoveGPRtoR(EAX, _Rs_);
		}
	}

	SetBranchReg();
}

////////////////////////////////////////////////////
void recJALR()
{
	REC_JUMP_DELAY_FALLBACK_DEL(JALR, _Rd_);

	EE::Profiler.EmitOp(eeOpcode::JALR);

	const u32 newpc = pc + 4;
	const bool swap = (EmuConfig.Gamefixes.GoemonTlbHack || _Rd_ == _Rs_) ? false : TrySwapDelaySlot(_Rs_, 0, _Rd_, true);

	// uncomment when there are NO instructions that need to call interpreter
	//	int mmreg;
	//	if (GPR_IS_CONST1(_Rs_))
	//		xMOV(ptr32[&cpuRegs.pc], g_cpuConstRegs[_Rs_].UL[0]);
	//	else
	//	{
	//		int mmreg;
	//
	//		if ((mmreg = _checkXMMreg(XMMTYPE_GPRREG, _Rs_, MODE_READ)) >= 0)
	//		{
	//			xMOVSS(ptr[&cpuRegs.pc], xRegisterSSE(mmreg));
	//		}
	//		else {
	//			xMOV(eax, ptr[(void*)((int)&cpuRegs.GPR.r[_Rs_].UL[0])]);
	//			xMOV(ptr[&cpuRegs.pc], eax);
	//		}
	//	}

	if (!swap)
	{
		_eeMoveGPRtoM(PTR_CPU(cpuRegs.pcWriteback), _Rs_);

		if (EmuConfig.Gamefixes.GoemonTlbHack)
		{
			armLoad(ECX, PTR_CPU(cpuRegs.pcWriteback));
			vtlb_DynV2P();
			armStore(PTR_CPU(cpuRegs.pcWriteback), EAX);
		}
	}

	if (_Rd_)
	{
		_deleteEEreg(_Rd_, 0);
		if (EE_CONST_PROP)
		{
			GPR_SET_CONST(_Rd_);
			g_cpuConstRegs[_Rd_].UD[0] = newpc;
		}
		else
		{
//			xWriteImm64ToMem(&cpuRegs.GPR.r[_Rd_].UD[0], rax, newpc);
            armStore64(PTR_CPU(cpuRegs.GPR.r[_Rd_].UD[0]), newpc);
		}
	}

	if (!swap)
	{
		recompileNextInstruction(true, false);
		armLoad(EAX, PTR_CPU(cpuRegs.pcWriteback));
	}
	else
	{
		if (GPR_IS_DIRTY_CONST(_Rs_) || _hasX86reg(X86TYPE_GPR, _Rs_, 0))
		{
			const int x86reg = _allocX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
			armAsm->Mov(EAX, a64::WRegister(x86reg));
		}
		else
		{
			_eeMoveGPRtoR(EAX, _Rs_);
		}
	}

	SetBranchReg();
}

#endif

} // namespace R5900::Dynarec::OpcodeImpl
