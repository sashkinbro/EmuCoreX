// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Important Note to Future Developers:
//   None of the COP0 instructions are really critical performance items,
//   so don't waste time converting any more them into recompiled code
//   unless it can make them nicely compact.  Calling the C versions will
//   suffice.

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "iR5900.h"
#include "iCOP0.h"

namespace Interp = R5900::Interpreter::OpcodeImpl::COP0;
#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

namespace R5900 {
namespace Dynarec {
namespace OpcodeImpl {
namespace COP0 {

/*********************************************************
*   COP0 opcodes                                         *
*                                                        *
*********************************************************/

// emits "setup" code for a COP0 branch test.  The instruction immediately following
// this should be a conditional Jump -- JZ or JNZ normally.
static void _setupBranchTest()
{
	_eeFlushAllDirty();

	// COP0 branch conditionals are based on the following equation:
	//  (((psHu16(DMAC_STAT) | ~psHu16(DMAC_PCR)) & 0x3ff) == 0x3ff)
	// BC0F checks if the statement is false, BC0T checks if the statement is true.

	// note: We only want to compare the 16 bit values of DMAC_STAT and PCR.
	// But using 32-bit loads here is ok (and faster), because we mask off
	// everything except the lower 10 bits away.

//	xMOV(eax, ptr[(&psHu32(DMAC_PCR))]);
    armAsm->Mov(EAX, psHu32(DMAC_PCR));
//	xMOV(ecx, 0x3ff); // ECX is our 10-bit mask var
    armAsm->Mov(ECX, 0x3ff);
//	xNOT(eax);
    armAsm->Mvn(EAX, EAX);
//	xOR(eax, ptr[(&psHu32(DMAC_STAT))]);
    armAsm->Orr(EAX, EAX, armLoadPtr(&psHu32(DMAC_STAT)));
//	xAND(eax, ecx);
    armAsm->And(EAX, EAX, ECX);
//	xCMP(eax, ecx);
    armAsm->Cmp(EAX, ECX);
}

void recBC0F()
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	const bool swap = TrySwapDelaySlot(0, 0, 0, false);
	_setupBranchTest();
//	recDoBranchImm(branchTo, JE32(0), false, swap);

    a64::Label label;
    armAsm->B(&label, a64::eq);
    recDoBranchImm(branchTo, &label, false, swap);
}

void recBC0T()
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	const bool swap = TrySwapDelaySlot(0, 0, 0, false);
	_setupBranchTest();
//	recDoBranchImm(branchTo, JNE32(0), false, swap);

    a64::Label label;
    armAsm->B(&label, a64::ne);
    recDoBranchImm(branchTo, &label, false, swap);
}

void recBC0FL()
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	_setupBranchTest();
//	recDoBranchImm(branchTo, JE32(0), true, false);

    a64::Label label;
    armAsm->B(&label, a64::eq);
    recDoBranchImm(branchTo, &label, true, false);
}

void recBC0TL()
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	_setupBranchTest();
//	recDoBranchImm(branchTo, JNE32(0), true, false);

    a64::Label label;
    armAsm->B(&label, a64::ne);
    recDoBranchImm(branchTo, &label, true, false);
}

void recTLBR() { recCall(Interp::TLBR); }
void recTLBP() { recCall(Interp::TLBP); }
void recTLBWI() { recCall(Interp::TLBWI); }
void recTLBWR() { recCall(Interp::TLBWR); }

void recERET()
{
	recBranchCall(Interp::ERET);
}

void recEI()
{
	// must branch after enabling interrupts, so that anything
	// pending gets triggered properly.
	recBranchCall(Interp::EI);
}

void recDI()
{
	//// No need to branch after disabling interrupts...

	//iFlushCall(0);

	//xMOV(eax, ptr[&cpuRegs.cycle ]);
	//xMOV(ptr[&g_nextBranchCycle], eax);

	//xFastCall((void*)(uptr)Interp::DI );

	// Fixes booting issues in the following games:
	// Jak X, Namco 50th anniversary, Spongebob the Movie, Spongebob Battle for Bikini Bottom,
	// The Incredibles, The Incredibles rize of the underminer, Soukou kihei armodyne, Garfield Saving Arlene, Tales of Fandom Vol. 2.
	if (!g_recompilingDelaySlot)
		recompileNextInstruction(false, false); // DI execution is delayed by one instruction

//	xMOV(eax, ptr[&cpuRegs.CP0.n.Status]);
    armLoad(EAX, PTR_CPU(cpuRegs.CP0.n.Status));
//	xTEST(eax, 0x20006); // EXL | ERL | EDI
    armAsm->Tst(EAX, 0x20006);
//	xForwardJNZ8 iHaveNoIdea;
    a64::Label iHaveNoIdea;
    armAsm->B(&iHaveNoIdea, a64::Condition::ne);
//	xTEST(eax, 0x18); // KSU
    armAsm->Tst(EAX, 0x18);
//	xForwardJNZ8 inUserMode;
    a64::Label inUserMode;
    armAsm->B(&inUserMode, a64::Condition::ne);
//	iHaveNoIdea.SetTarget();
    armBind(&iHaveNoIdea);
//	xAND(eax, ~(u32)0x10000); // EIE
    armAsm->And(EAX, EAX, ~(u32)0x10000);
//	xMOV(ptr[&cpuRegs.CP0.n.Status], eax);
    armStore(PTR_CPU(cpuRegs.CP0.n.Status), EAX);
//	inUserMode.SetTarget();
    armBind(&inUserMode);
}


#ifndef CP0_RECOMPILE

REC_SYS(MFC0);
REC_SYS(MTC0);

#else

void recMFC0()
{
	if (_Rd_ == 9)
	{
		// This case needs to be handled even if the write-back is ignored (_Rt_ == 0 )
//		xMOV(ecx, ptr32[&cpuRegs.cycle]);
//		xADD(ecx, scaleblockcycles_clear());
//		xMOV(ptr32[&cpuRegs.cycle], ecx); // update cycles
        armAdd(ECX, PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//		xMOV(eax, ecx);
        armAsm->Mov(EAX, ECX);
//		xSUB(eax, ptr[&cpuRegs.lastCOP0Cycle]);
        armAsm->Sub(EAX, EAX, armLoad(PTR_CPU(cpuRegs.lastCOP0Cycle)));
//		xADD(ptr[&cpuRegs.CP0.n.Count], eax);
        armAdd(PTR_CPU(cpuRegs.CP0.n.Count), EAX);
//		xMOV(ptr[&cpuRegs.lastCOP0Cycle], ecx);
        armStore(PTR_CPU(cpuRegs.lastCOP0Cycle), ECX);

		if (!_Rt_)
			return;

		const int regt = _Rt_ ? _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE) : -1;
//		xMOVSX(xRegister64(regt), ptr32[&cpuRegs.CP0.r[_Rd_]]);
        armLoadsw(a64::XRegister(regt), PTR_CPU(cpuRegs.CP0.r[_Rd_]));
		return;
	}

	if (!_Rt_)
		return;

	if (_Rd_ == 25)
	{
		if (0 == (_Imm_ & 1)) // MFPS, register value ignored
		{
			const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
//			xMOVSX(xRegister64(regt), ptr32[&cpuRegs.PERF.n.pccr]);
            armLoadsw(a64::XRegister(regt), PTR_CPU(cpuRegs.PERF.n.pccr));
		}
		else if (0 == (_Imm_ & 2)) // MFPC 0, only LSB of register matters
		{
			iFlushCall(FLUSH_INTERPRETER);
//			xMOV(eax, ptr32[&cpuRegs.cycle]);
//			xADD(eax, scaleblockcycles_clear());
//			xMOV(ptr32[&cpuRegs.cycle], eax); // update cycles
            armAdd(PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//			xFastCall((void*)COP0_UpdatePCCR);
            armEmitCall(reinterpret_cast<void*>(COP0_UpdatePCCR));

			const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
//			xMOVSX(xRegister64(regt), ptr32[&cpuRegs.PERF.n.pcr0]);
            armLoadsw(a64::XRegister(regt), PTR_CPU(cpuRegs.PERF.n.pcr0));
		}
		else // MFPC 1
		{
			iFlushCall(FLUSH_INTERPRETER);
//			xMOV(eax, ptr32[&cpuRegs.cycle]);
//			xADD(eax, scaleblockcycles_clear());
//			xMOV(ptr32[&cpuRegs.cycle], eax); // update cycles
            armAdd(PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//			xFastCall((void*)COP0_UpdatePCCR);
            armEmitCall(reinterpret_cast<void*>(COP0_UpdatePCCR));

			const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
//			xMOVSX(xRegister64(regt), ptr32[&cpuRegs.PERF.n.pcr1]);
            armLoadsw(a64::XRegister(regt), PTR_CPU(cpuRegs.PERF.n.pcr1));
		}

		return;
	}
	else if (_Rd_ == 24)
	{
		COP0_LOG("MFC0 Breakpoint debug Registers code = %x\n", cpuRegs.code & 0x3FF);
		return;
	}

	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
//	xMOVSX(xRegister64(regt), ptr32[&cpuRegs.CP0.r[_Rd_]]);
    armLoadsw(a64::XRegister(regt), PTR_CPU(cpuRegs.CP0.r[_Rd_]));
}

void recMTC0()
{
	if (GPR_IS_CONST1(_Rt_))
	{
		switch (_Rd_)
		{
			case 12:
				iFlushCall(FLUSH_INTERPRETER);
//				xMOV(eax, ptr32[&cpuRegs.cycle]);
//				xADD(eax, scaleblockcycles_clear());
//				xMOV(ptr32[&cpuRegs.cycle], eax); // update cycles
                armAdd(PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//				xFastCall((void*)WriteCP0Status, g_cpuConstRegs[_Rt_].UL[0]);
                armAsm->Mov(EAX, g_cpuConstRegs[_Rt_].UL[0]);
                armEmitCall(reinterpret_cast<void*>(WriteCP0Status));
				break;

			case 16:
				iFlushCall(FLUSH_INTERPRETER);
//				xFastCall((void*)WriteCP0Config, g_cpuConstRegs[_Rt_].UL[0]);
                armAsm->Mov(EAX, g_cpuConstRegs[_Rt_].UL[0]);
                armEmitCall(reinterpret_cast<void*>(WriteCP0Config));
				break;

			case 9:
//				xMOV(ecx, ptr32[&cpuRegs.cycle]);
//				xADD(ecx, scaleblockcycles_clear());
//				xMOV(ptr32[&cpuRegs.cycle], ecx); // update cycles
                armAdd(ECX, PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//				xMOV(ptr[&cpuRegs.lastCOP0Cycle], ecx);
                armStore(PTR_CPU(cpuRegs.lastCOP0Cycle), ECX);
//				xMOV(ptr32[&cpuRegs.CP0.r[9]], g_cpuConstRegs[_Rt_].UL[0]);
                armStore(PTR_CPU(cpuRegs.CP0.r[9]), g_cpuConstRegs[_Rt_].UL[0]);
				break;

			case 25:
				if (0 == (_Imm_ & 1)) // MTPS
				{
					if (0 != (_Imm_ & 0x3E)) // only effective when the register is 0
						break;
					// Updates PCRs and sets the PCCR.
					iFlushCall(FLUSH_INTERPRETER);
//					xMOV(eax, ptr32[&cpuRegs.cycle]);
//					xADD(eax, scaleblockcycles_clear());
//					xMOV(ptr32[&cpuRegs.cycle], eax); // update cycles
                    armAdd(PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//					xFastCall((void*)COP0_UpdatePCCR);
                    armEmitCall(reinterpret_cast<void*>(COP0_UpdatePCCR));
//					xMOV(ptr32[&cpuRegs.PERF.n.pccr], g_cpuConstRegs[_Rt_].UL[0]);
                    armStore(PTR_CPU(cpuRegs.PERF.n.pccr), g_cpuConstRegs[_Rt_].UL[0]);
//					xFastCall((void*)COP0_DiagnosticPCCR);
                    armEmitCall(reinterpret_cast<void*>(COP0_DiagnosticPCCR));
				}
				else if (0 == (_Imm_ & 2)) // MTPC 0, only LSB of register matters
				{
//					xMOV(eax, ptr32[&cpuRegs.cycle]);
//					xADD(eax, scaleblockcycles_clear());
//					xMOV(ptr32[&cpuRegs.cycle], eax); // update cycles
                    armAdd(EAX, PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//					xMOV(ptr32[&cpuRegs.PERF.n.pcr0], g_cpuConstRegs[_Rt_].UL[0]);
                    armStore(PTR_CPU(cpuRegs.PERF.n.pcr0), g_cpuConstRegs[_Rt_].UL[0]);
//					xMOV(ptr[&cpuRegs.lastPERFCycle[0]], eax);
                    armStore(PTR_CPU(cpuRegs.lastPERFCycle[0]), EAX);
				}
				else // MTPC 1
				{
//					xMOV(eax, ptr32[&cpuRegs.cycle]);
//					xADD(eax, scaleblockcycles_clear());
//					xMOV(ptr32[&cpuRegs.cycle], eax); // update cycles
                    armAdd(EAX, PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//					xMOV(ptr32[&cpuRegs.PERF.n.pcr1], g_cpuConstRegs[_Rt_].UL[0]);
                    armStore(PTR_CPU(cpuRegs.PERF.n.pcr1), g_cpuConstRegs[_Rt_].UL[0]);
//					xMOV(ptr[&cpuRegs.lastPERFCycle[1]], eax);
                    armStore(PTR_CPU(cpuRegs.lastPERFCycle[1]), EAX);
				}
				break;

			case 24:
				COP0_LOG("MTC0 Breakpoint debug Registers code = %x\n", cpuRegs.code & 0x3FF);
				break;

			default:
//				xMOV(ptr32[&cpuRegs.CP0.r[_Rd_]], g_cpuConstRegs[_Rt_].UL[0]);
                armStore(PTR_CPU(cpuRegs.CP0.r[_Rd_]), g_cpuConstRegs[_Rt_].UL[0]);
				break;
		}
	}
	else
	{
		switch (_Rd_)
		{
			case 12:
				_eeMoveGPRtoR(RAX, _Rt_);
				iFlushCall(FLUSH_INTERPRETER);
//				xMOV(eax, ptr32[&cpuRegs.cycle]);
//				xADD(eax, scaleblockcycles_clear());
//				xMOV(ptr32[&cpuRegs.cycle], eax); // update cycles
                armAdd(PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//				xFastCall((void*)WriteCP0Status);
                armEmitCall(reinterpret_cast<void*>(WriteCP0Status));
				break;

			case 16:
				_eeMoveGPRtoR(RAX, _Rt_);
				iFlushCall(FLUSH_INTERPRETER);
//				xFastCall((void*)WriteCP0Config);
                armEmitCall(reinterpret_cast<const void*>(WriteCP0Config));
				break;

			case 9:
//				xMOV(ecx, ptr32[&cpuRegs.cycle]);
//				xADD(ecx, scaleblockcycles_clear());
//				xMOV(ptr32[&cpuRegs.cycle], ecx); // update cycles
                armAdd(ECX, PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
				_eeMoveGPRtoM(PTR_CPU(cpuRegs.CP0.r[9]), _Rt_);
//				xMOV(ptr[&cpuRegs.lastCOP0Cycle], ecx);
                armStore(PTR_CPU(cpuRegs.lastCOP0Cycle), ECX);
				break;

			case 25:
				if (0 == (_Imm_ & 1)) // MTPS
				{
					if (0 != (_Imm_ & 0x3E)) // only effective when the register is 0
						break;
					iFlushCall(FLUSH_INTERPRETER);
//					xMOV(eax, ptr32[&cpuRegs.cycle]);
//					xADD(eax, scaleblockcycles_clear());
//					xMOV(ptr32[&cpuRegs.cycle], eax); // update cycles
                    armAdd(PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
//					xFastCall((void*)COP0_UpdatePCCR);
                    armEmitCall(reinterpret_cast<void*>(COP0_UpdatePCCR));
					_eeMoveGPRtoM(PTR_CPU(cpuRegs.PERF.n.pccr), _Rt_);
//					xFastCall((void*)COP0_DiagnosticPCCR);
                    armEmitCall(reinterpret_cast<void*>(COP0_DiagnosticPCCR));
				}
				else if (0 == (_Imm_ & 2)) // MTPC 0, only LSB of register matters
				{
//					xMOV(ecx, ptr32[&cpuRegs.cycle]);
//					xADD(ecx, scaleblockcycles_clear());
//					xMOV(ptr32[&cpuRegs.cycle], ecx); // update cycles
                    armAdd(ECX, PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
					_eeMoveGPRtoM(PTR_CPU(cpuRegs.PERF.n.pcr0), _Rt_);
//					xMOV(ptr[&cpuRegs.lastPERFCycle[0]], ecx);
                    armStore(PTR_CPU(cpuRegs.lastPERFCycle[0]), ECX);
				}
				else // MTPC 1
				{
//					xMOV(ecx, ptr32[&cpuRegs.cycle]);
//					xADD(ecx, scaleblockcycles_clear());
//					xMOV(ptr32[&cpuRegs.cycle], ecx); // update cycles
                    armAdd(ECX, PTR_CPU(cpuRegs.cycle), scaleblockcycles_clear());
					_eeMoveGPRtoM(PTR_CPU(cpuRegs.PERF.n.pcr1), _Rt_);
//					xMOV(ptr[&cpuRegs.lastPERFCycle[1]], ecx);
                    armStore(PTR_CPU(cpuRegs.lastPERFCycle[1]), ECX);
				}
				break;

			case 24:
				COP0_LOG("MTC0 Breakpoint debug Registers code = %x\n", cpuRegs.code & 0x3FF);
				break;

			default:
				_eeMoveGPRtoM(PTR_CPU(cpuRegs.CP0.r[_Rd_]), _Rt_);
				break;
		}
	}
}
#endif


/*void rec(COP0) {
}

void rec(BC0F) {
}

void rec(BC0T) {
}

void rec(BC0FL) {
}

void rec(BC0TL) {
}

void rec(TLBR) {
}

void rec(TLBWI) {
}

void rec(TLBWR) {
}

void rec(TLBP) {
}*/

} // namespace COP0
} // namespace OpcodeImpl
} // namespace Dynarec
} // namespace R5900
