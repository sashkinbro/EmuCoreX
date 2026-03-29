// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "iR5900.h"
#include "iFPU.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

//alignas(16) const u32 g_minvals[4] = {0xff7fffff, 0xff7fffff, 0xff7fffff, 0xff7fffff};
//alignas(16) const u32 g_maxvals[4] = {0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff};

//------------------------------------------------------------------
namespace R5900 {
namespace Dynarec {
namespace OpcodeImpl {
namespace COP1 {

namespace DOUBLE
{

	void recABS_S_xmm(int info);
	void recADD_S_xmm(int info);
	void recADDA_S_xmm(int info);
	void recC_EQ_xmm(int info);
	void recC_LE_xmm(int info);
	void recC_LT_xmm(int info);
	void recDIV_S_xmm(int info);
	void recMADD_S_xmm(int info);
	void recMADDA_S_xmm(int info);
	void recMAX_S_xmm(int info);
	void recMIN_S_xmm(int info);
	void recMOV_S_xmm(int info);
	void recMSUB_S_xmm(int info);
	void recMSUBA_S_xmm(int info);
	void recMUL_S_xmm(int info);
	void recMULA_S_xmm(int info);
	void recNEG_S_xmm(int info);
	void recSUB_S_xmm(int info);
	void recSUBA_S_xmm(int info);
	void recSQRT_S_xmm(int info);
	void recRSQRT_S_xmm(int info);

}; // namespace DOUBLE

//------------------------------------------------------------------
// Helper Macros
//------------------------------------------------------------------
#define _Ft_ _Rt_
#define _Fs_ _Rd_
#define _Fd_ _Sa_

// FCR31 Flags
#define FPUflagC  0x00800000
#define FPUflagI  0x00020000
#define FPUflagD  0x00010000
#define FPUflagO  0x00008000
#define FPUflagU  0x00004000
#define FPUflagSI 0x00000040
#define FPUflagSD 0x00000020
#define FPUflagSO 0x00000010
#define FPUflagSU 0x00000008

// Add/Sub opcodes produce the same results as the ps2
#define FPU_CORRECT_ADD_SUB 1

//alignas(16) static const u32 s_neg[4] = {0x80000000, 0xffffffff, 0xffffffff, 0xffffffff};
//alignas(16) static const u32 s_pos[4] = {0x7fffffff, 0xffffffff, 0xffffffff, 0xffffffff};

#define REC_FPUBRANCH(f) \
	void f(); \
	void rec##f() \
	{ \
		iFlushCall(FLUSH_INTERPRETER); \
		armEmitCall(reinterpret_cast<void*>((uptr)R5900::Interpreter::OpcodeImpl::COP1::f)); \
		g_branch = 2; \
	}

#define REC_FPUFUNC(f) \
	void f(); \
	void rec##f() \
	{ \
		iFlushCall(FLUSH_INTERPRETER); \
		armEmitCall(reinterpret_cast<void*>((uptr)R5900::Interpreter::OpcodeImpl::COP1::f)); \
	}
//------------------------------------------------------------------

//------------------------------------------------------------------
// *FPU Opcodes!*
//------------------------------------------------------------------

// Those opcode are marked as special ! But I don't understand why we can't run them in the interpreter
#ifndef FPU_RECOMPILE

REC_FPUFUNC(CFC1);
REC_FPUFUNC(CTC1);
REC_FPUFUNC(MFC1);
REC_FPUFUNC(MTC1);

#else

//------------------------------------------------------------------
// CFC1 / CTC1
//------------------------------------------------------------------
void recCFC1(void)
{
	if (!_Rt_)
		return;
	EE::Profiler.EmitOp(eeOpcode::CFC1);

	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
	if (_Fs_ >= 16)
	{
        auto reg32 = a64::WRegister(regt);

//		xMOV(xRegister32(regt), ptr32[&fpuRegs.fprc[31]]);
        armLoad(reg32, PTR_CPU(fpuRegs.fprc[31]));
//		xAND(xRegister32(regt), 0x0083c078); //remove always-zero bits
        armAsm->And(reg32, reg32, 0x0083c078);
//		xOR(xRegister32(regt), 0x01000001); //set always-one bits
        armAsm->Orr(reg32, reg32, 0x01000001);
//		xMOVSX(xRegister64(regt), xRegister32(regt));
        armAsm->Sxtw(a64::XRegister(regt), reg32);
	}
	else
	{
//		xMOVSX(xRegister64(regt), ptr32[&fpuRegs.fprc[0]]);
        armLoadsw(a64::XRegister(regt), PTR_CPU(fpuRegs.fprc[0]));
	}
}

void recCTC1()
{
	if (_Fs_ != 31)
		return;
	EE::Profiler.EmitOp(eeOpcode::CTC1);

	if (GPR_IS_CONST1(_Rt_))
	{
//		xMOV(ptr32[&fpuRegs.fprc[_Fs_]], g_cpuConstRegs[_Rt_].UL[0]);
        armStore(PTR_CPU(fpuRegs.fprc[_Fs_]), g_cpuConstRegs[_Rt_].UL[0]);
	}
	else
	{
		int mmreg = _checkXMMreg(XMMTYPE_GPRREG, _Rt_, MODE_READ);

		if (mmreg >= 0)
		{
//			xMOVSS(ptr[&fpuRegs.fprc[_Fs_]], xRegisterSSE(mmreg));
            armStore(PTR_CPU(fpuRegs.fprc[_Fs_]), a64::QRegister(mmreg).S());
		}
		else if ((mmreg = _checkX86reg(X86TYPE_GPR, _Rt_, MODE_READ)) >= 0)
		{
//			xMOV(ptr32[&fpuRegs.fprc[_Fs_]], xRegister32(mmreg));
            armStore(PTR_CPU(fpuRegs.fprc[_Fs_]), a64::WRegister(mmreg));
		}
		else
		{
			_deleteGPRtoXMMreg(_Rt_, 1);

//			xMOV(eax, ptr[&cpuRegs.GPR.r[_Rt_].UL[0]]);
            armLoad(EAX, PTR_CPU(cpuRegs.GPR.r[_Rt_].UL[0]));
//			xMOV(ptr[&fpuRegs.fprc[_Fs_]], eax);
            armStore(PTR_CPU(fpuRegs.fprc[_Fs_]), EAX);
		}
	}
}
//------------------------------------------------------------------


//------------------------------------------------------------------
// MFC1
//------------------------------------------------------------------

void recMFC1()
{
	if (!_Rt_)
		return;

	EE::Profiler.EmitOp(eeOpcode::MFC1);

	const int xmmregt = _allocIfUsedGPRtoXMM(_Rt_, MODE_READ | MODE_WRITE);
	const int regs = _allocIfUsedFPUtoXMM(_Fs_, MODE_READ);
	if (regs >= 0 && xmmregt >= 0)
	{
		// if we're in xmm, we shouldn't be const
		pxAssert(!GPR_IS_CONST1(_Rt_));

		// both in xmm, sign extend and insert lower bits
		const int temp = _allocTempXMMreg(XMMT_FPS);
        auto regQ = a64::QRegister(temp);

//		xMOVAPS(xRegisterSSE(temp), xRegisterSSE(regs));
        armAsm->Mov(regQ, a64::QRegister(regs));
//		xPSRA.D(xRegisterSSE(temp), 31);
        armAsm->Sshr(regQ.V4S(), regQ.V4S(), 31);
//		xMOVSS(xRegisterSSE(xmmregt), xRegisterSSE(regs));
        armAsm->Mov(a64::QRegister(xmmregt).S(), 0, a64::QRegister(regs).S(), 0);
//		xINSERTPS(xRegisterSSE(xmmregt), xRegisterSSE(temp), _MM_MK_INSERTPS_NDX(0, 1, 0));
        armAsm->Ins(a64::QRegister(xmmregt).V4S(), 1, regQ.V4S(), 0);
		_freeXMMreg(temp);
		return;
	}

	// storing to a gpr..
	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);

	// shouldn't be const after we're writing.
	pxAssert(!GPR_IS_CONST1(_Rt_));

	if (regs >= 0)
	{
		// xmm -> gpr
//		xMOVD(xRegister32(regt), xRegisterSSE(regs));
        armAsm->Fmov(a64::WRegister(regt), a64::QRegister(regs).S());
//		xMOVSX(xRegister64(regt), xRegister32(regt));
        armAsm->Sxtw(a64::XRegister(regt), a64::WRegister(regt));
	}
	else
	{
		// mem -> gpr
//		xMOVSX(xRegister64(regt), ptr32[&fpuRegs.fpr[_Fs_].UL]);
        armLoadsw(a64::XRegister(regt), PTR_CPU(fpuRegs.fpr[_Fs_].UL));
	}
}

//------------------------------------------------------------------


//------------------------------------------------------------------
// MTC1
//------------------------------------------------------------------
void recMTC1()
{
	EE::Profiler.EmitOp(eeOpcode::MTC1);
	if (GPR_IS_CONST1(_Rt_))
	{
		const int xmmreg = _allocIfUsedFPUtoXMM(_Fs_, MODE_WRITE);
		if (xmmreg >= 0)
		{
            auto regQ = a64::QRegister(xmmreg);

			// common case: mtc1 zero, fnn
			if (g_cpuConstRegs[_Rt_].UL[0] == 0)
			{
//				xPXOR(xRegisterSSE(xmmreg), xRegisterSSE(xmmreg));
                armAsm->Eor(regQ.V16B(), regQ.V16B(), regQ.V16B());
			}
			else
			{
				// may as well flush the constant register, since we're needing it in a gpr anyway
				const int x86reg = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
//				xMOVDZX(xRegisterSSE(xmmreg), xRegister32(x86reg));
                armAsm->Fmov(regQ.S(), a64::Register(x86reg, a64::kWRegSize));
			}
		}
		else
		{
			pxAssert(!_hasXMMreg(XMMTYPE_FPREG, _Fs_));
//			xMOV(ptr32[&fpuRegs.fpr[_Fs_].UL], g_cpuConstRegs[_Rt_].UL[0]);
            armStore(PTR_CPU(fpuRegs.fpr[_Fs_].UL), g_cpuConstRegs[_Rt_].UL[0]);
		}
	}
	else
	{
		const int xmmgpr = _checkXMMreg(XMMTYPE_GPRREG, _Rt_, MODE_READ);
		if (xmmgpr >= 0)
		{
			if (g_pCurInstInfo->regs[_Rt_] & EEINST_LASTUSE)
			{
				// transfer the reg directly
				_deleteFPtoXMMreg(_Fs_, DELETE_REG_FREE_NO_WRITEBACK);
				_reallocateXMMreg(xmmgpr, XMMTYPE_FPREG, _Fs_, MODE_WRITE);
			}
			else
			{
                auto regQ = a64::QRegister(xmmgpr);

				const int xmmreg2 = _allocIfUsedFPUtoXMM(_Fs_, MODE_WRITE);
				if (xmmreg2 >= 0) {
//                    xMOVSS(xRegisterSSE(xmmreg2), xRegisterSSE(xmmgpr));
                    armAsm->Mov(a64::QRegister(xmmreg2).S(), 0, regQ.S(), 0);
                }
				else {
//                    xMOVSS(ptr[&fpuRegs.fpr[_Fs_].UL], xRegisterSSE(xmmgpr));
                    armStore(PTR_CPU(fpuRegs.fpr[_Fs_].UL), regQ.S());
                }
			}
		}
		else
		{
			// may as well cache it..
			const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_READ);
			const int mmreg2 = _allocIfUsedFPUtoXMM(_Fs_, MODE_WRITE);

            auto reg32 = a64::Register(regt, a64::kWRegSize);

			if (mmreg2 >= 0)
			{
//				xMOVDZX(xRegisterSSE(mmreg2), xRegister32(regt));
                armAsm->Fmov(a64::QRegister(mmreg2).S(), reg32);
			}
			else
			{
//				xMOV(ptr32[&fpuRegs.fpr[_Fs_].UL], xRegister32(regt));
                armStore(PTR_CPU(fpuRegs.fpr[_Fs_].UL), reg32);
			}
		}
	}
}
#endif
//------------------------------------------------------------------


#ifndef FPU_RECOMPILE // If FPU_RECOMPILE is not defined, then use the interpreter opcodes. (CFC1, CTC1, MFC1, and MTC1 are special because they work specifically with the EE rec so they're defined above)

REC_FPUFUNC(ABS_S);
REC_FPUFUNC(ADD_S);
REC_FPUFUNC(ADDA_S);
REC_FPUBRANCH(BC1F);
REC_FPUBRANCH(BC1T);
REC_FPUBRANCH(BC1FL);
REC_FPUBRANCH(BC1TL);
REC_FPUFUNC(C_EQ);
REC_FPUFUNC(C_F);
REC_FPUFUNC(C_LE);
REC_FPUFUNC(C_LT);
REC_FPUFUNC(CVT_S);
REC_FPUFUNC(CVT_W);
REC_FPUFUNC(DIV_S);
REC_FPUFUNC(MAX_S);
REC_FPUFUNC(MIN_S);
REC_FPUFUNC(MADD_S);
REC_FPUFUNC(MADDA_S);
REC_FPUFUNC(MOV_S);
REC_FPUFUNC(MSUB_S);
REC_FPUFUNC(MSUBA_S);
REC_FPUFUNC(MUL_S);
REC_FPUFUNC(MULA_S);
REC_FPUFUNC(NEG_S);
REC_FPUFUNC(SUB_S);
REC_FPUFUNC(SUBA_S);
REC_FPUFUNC(SQRT_S);
REC_FPUFUNC(RSQRT_S);

#else // FPU_RECOMPILE

//------------------------------------------------------------------
// Clamp Functions (Converts NaN's and Infinities to Normal Numbers)
//------------------------------------------------------------------

static int fpuCopyToTempForClamp(int fpureg, int xmmreg)
{
	if (FPUINST_USEDTEST(fpureg))
	{
		const int tempreg = _allocTempXMMreg(XMMT_FPS);
//		xMOVSS(xRegisterSSE(tempreg), xRegisterSSE(xmmreg));
        armAsm->Mov(a64::QRegister(tempreg).S(), 0, a64::QRegister(xmmreg).S(), 0);
		return tempreg;
	}

	// flush back the original value, before we mess with it below
	if (FPUINST_LIVETEST(fpureg))
		_flushXMMreg(xmmreg);

	// turn it into a temp, so in case the liveness was incorrect, we don't reuse it after clamp
	_reallocateXMMreg(xmmreg, XMMTYPE_TEMP, 0, 0, true);
	return xmmreg;
}

static void fpuFreeIfTemp(int xmmreg)
{
	if (xmmregs[xmmreg].inuse && xmmregs[xmmreg].type == XMMTYPE_TEMP)
		_freeXMMreg(xmmreg);
}

__fi void fpuFloat3(int regd) // +NaN -> +fMax, -NaN -> -fMax, +Inf -> +fMax, -Inf -> -fMax
{
    auto regQ = a64::QRegister(regd);

//	xPMIN.SD(xRegisterSSE(regd), ptr128[&g_maxvals[0]]);
    armAsm->Smin(regQ.V4S(), regQ.V4S(), armLoadPtrV(PTR_CPU(mVUss4.g_maxvals[0])).V4S());
//	xPMIN.UD(xRegisterSSE(regd), ptr128[&g_minvals[0]]);
    armAsm->Umin(regQ.V4S(), regQ.V4S(), armLoadPtrV(PTR_CPU(mVUss4.g_minvals[0])).V4S());
}

__fi void fpuFloat(int regd) // +/-NaN -> +fMax, +Inf -> +fMax, -Inf -> -fMax
{
	if (CHECK_FPU_OVERFLOW)
	{
        auto regQ = a64::QRegister(regd);

//		xMIN.SS(xRegisterSSE(regd), ptr[&g_maxvals[0]]); // MIN() must be before MAX()! So that NaN's become +Maximum
        armAsm->Fminnm(regQ.S(), regQ.S(), armLoadPtrV(PTR_CPU(mVUss4.g_maxvals[0])).S());
//		xMAX.SS(xRegisterSSE(regd), ptr[&g_minvals[0]]);
        armAsm->Fmaxnm(regQ.S(), regQ.S(), armLoadPtrV(PTR_CPU(mVUss4.g_minvals[0])).S());
	}
}

__fi void fpuFloat2(int regd) // +NaN -> +fMax, -NaN -> -fMax, +Inf -> +fMax, -Inf -> -fMax
{
	if (CHECK_FPU_OVERFLOW)
	{
		fpuFloat3(regd);
	}
}

void ClampValues(int regd)
{
	fpuFloat(regd);
}
//------------------------------------------------------------------


//------------------------------------------------------------------
// ABS XMM
//------------------------------------------------------------------
void recABS_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::ABS_F);

    auto regED = a64::QRegister(EEREC_D);

	if (info & PROCESS_EE_S) {
//        xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
        armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_S).S(), 0);
    }
	else {
//        xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Fs_]]);
        armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
    }

//	xAND.PS(xRegisterSSE(EEREC_D), ptr[&s_pos[0]]);
    armAsm->And(regED.V16B(), regED.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_pos[0])).V16B());
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags

	if (CHECK_FPU_OVERFLOW) { // Only need to do positive clamp, since EEREC_D is positive
//        xMIN.SS(xRegisterSSE(EEREC_D), ptr[&g_maxvals[0]]);
        armAsm->Fminnm(regED.S(), regED.S(), armLoadPtrV(PTR_CPU(mVUss4.g_maxvals[0])).S());
    }
}

FPURECOMPILE_CONSTCODE(ABS_S, XMMINFO_WRITED | XMMINFO_READS);
//------------------------------------------------------------------


//------------------------------------------------------------------
// FPU_ADD_SUB (Used to mimic PS2's FPU add/sub behavior)
//------------------------------------------------------------------
// Compliant IEEE FPU uses, in computations, uses additional "guard" bits to the right of the mantissa
// but EE-FPU doesn't. Substraction (and addition of positive and negative) may shift the mantissa left,
// causing those bits to appear in the result; this function masks out the bits of the mantissa that will
// get shifted right to the guard bits to ensure that the guard bits are empty.
// The difference of the exponents = the amount that the smaller operand will be shifted right by.
// Modification - the PS2 uses a single guard bit? (Coded by Nneeve)
//------------------------------------------------------------------
void FPU_ADD_SUB(int regd, int regt, int issub)
{
    a64::Label j8Ptr0, j8Ptr1, j8Ptr2, j8Ptr3, j8Ptr4, j8Ptr5, j8Ptr6, j8Ptr7;

    auto regD = a64::QRegister(regd);
    auto regT = a64::QRegister(regt);

	const int xmmtemp = _allocTempXMMreg(XMMT_FPS); //temporary for anding with regd/regt
    auto regQTemp = a64::QRegister(xmmtemp);

//	xMOVD(ecx, xRegisterSSE(regd)); // ecx receives regd
    armAsm->Fmov(ECX, regD.S());
//	xMOVD(eax, xRegisterSSE(regt)); // eax receives regt
    armAsm->Fmov(EAX, regT.S());

	//mask the exponents
//	xSHR(ecx, 23);
    armAsm->Lsr(ECX, ECX, 23);
//	xSHR(eax, 23);
    armAsm->Lsr(EAX, EAX, 23);
//	xAND(ecx, 0xff);
    armAsm->And(ECX, ECX, 0xff);
//	xAND(eax, 0xff);
    armAsm->And(EAX, EAX, 0xff);

//	xSUB(ecx, eax); //tempecx = exponent difference
    armAsm->Sub(ECX, ECX, EAX);
//	xCMP(ecx, 25);
    armAsm->Cmp(ECX, 25);
//	j8Ptr[0] = JGE8(0);
    armAsm->B(&j8Ptr0, a64::Condition::ge);
//	xCMP(ecx, 0);
    armAsm->Cmp(ECX, 0);
//	j8Ptr[1] = JG8(0);
    armAsm->B(&j8Ptr1, a64::Condition::gt);
//	j8Ptr[2] = JE8(0);
    armAsm->B(&j8Ptr2, a64::Condition::eq);
//	xCMP(ecx, -25);
    armAsm->Cmp(ECX, -25);
//	j8Ptr[3] = JLE8(0);
    armAsm->B(&j8Ptr3, a64::Condition::le);

	//diff = -24 .. -1 , expd < expt
//	xNEG(ecx);
    armAsm->Neg(ECX, ECX);
//	xDEC(ecx);
    armAsm->Sub(ECX, ECX, 1);
//	xMOV(eax, 0xffffffff);
    armAsm->Mov(EAX, 0xffffffff);
//	xSHL(eax, cl); //temp2 = 0xffffffff << tempecx
    armAsm->Lsl(EAX, EAX, ECX);
//	xMOVDZX(xRegisterSSE(xmmtemp), eax);
    armAsm->Fmov(regQTemp.S(), EAX);
//	xAND.PS(xRegisterSSE(regd), xRegisterSSE(xmmtemp));
    armAsm->And(regD.V16B(), regD.V16B(), regQTemp.V16B());
	if (issub) {
//        xSUB.SS(xRegisterSSE(regd), xRegisterSSE(regt));
        armAsm->Fsub(regD.S(), regD.S(), regT.S());
    }
	else {
//        xADD.SS(xRegisterSSE(regd), xRegisterSSE(regt));
        armAsm->Fadd(regD.S(), regD.S(), regT.S());
    }
//	j8Ptr[4] = JMP8(0);
    armAsm->B(&j8Ptr4);

//	x86SetJ8(j8Ptr[0]);
    armBind(&j8Ptr0);
	//diff = 25 .. 255 , expt < expd
//	xMOVAPS(xRegisterSSE(xmmtemp), xRegisterSSE(regt));
    armAsm->Mov(regQTemp, regT);
//	xAND.PS(xRegisterSSE(xmmtemp), ptr[s_neg]);
    armAsm->And(regQTemp.V16B(), regQTemp.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_neg)).V16B());
	if (issub) {
//        xSUB.SS(xRegisterSSE(regd), xRegisterSSE(xmmtemp));
        armAsm->Fsub(regD.S(), regD.S(), regQTemp.S());
    }
	else {
//        xADD.SS(xRegisterSSE(regd), xRegisterSSE(xmmtemp));
        armAsm->Fadd(regD.S(), regD.S(), regQTemp.S());
    }
//	j8Ptr[5] = JMP8(0);
    armAsm->B(&j8Ptr5);

//	x86SetJ8(j8Ptr[1]);
    armBind(&j8Ptr1);
	//diff = 1 .. 24, expt < expd
//	xDEC(ecx);
    armAsm->Sub(ECX, ECX, 1);
//	xMOV(eax, 0xffffffff);
    armAsm->Mov(EAX, 0xffffffff);
//	xSHL(eax, cl); //temp2 = 0xffffffff << tempecx
    armAsm->Lsl(EAX, EAX, ECX);
//	xMOVDZX(xRegisterSSE(xmmtemp), eax);
    armAsm->Fmov(regQTemp.S(), EAX);
//	xAND.PS(xRegisterSSE(xmmtemp), xRegisterSSE(regt));
    armAsm->And(regQTemp.V16B(), regQTemp.V16B(), regT.V16B());
	if (issub) {
//        xSUB.SS(xRegisterSSE(regd), xRegisterSSE(xmmtemp));
        armAsm->Fsub(regD.S(), regD.S(), regQTemp.S());
    }
	else {
//        xADD.SS(xRegisterSSE(regd), xRegisterSSE(xmmtemp));
        armAsm->Fadd(regD.S(), regD.S(), regQTemp.S());
    }
//	j8Ptr[6] = JMP8(0);
    armAsm->B(&j8Ptr6);

//	x86SetJ8(j8Ptr[3]);
    armBind(&j8Ptr3);
	//diff = -255 .. -25, expd < expt
//	xAND.PS(xRegisterSSE(regd), ptr[s_neg]);
    armAsm->And(regD.V16B(), regD.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_neg)).V16B());
	if (issub) {
//        xSUB.SS(xRegisterSSE(regd), xRegisterSSE(regt));
        armAsm->Fsub(regD.S(), regD.S(), regT.S());
    }
	else {
//        xADD.SS(xRegisterSSE(regd), xRegisterSSE(regt));
        armAsm->Fadd(regD.S(), regD.S(), regT.S());
    }
//	j8Ptr[7] = JMP8(0);
    armAsm->B(&j8Ptr7);

//	x86SetJ8(j8Ptr[2]);
    armBind(&j8Ptr2);
	//diff == 0
	if (issub) {
//        xSUB.SS(xRegisterSSE(regd), xRegisterSSE(regt));
        armAsm->Fsub(regD.S(), regD.S(), regT.S());
    }
	else {
//        xADD.SS(xRegisterSSE(regd), xRegisterSSE(regt));
        armAsm->Fadd(regD.S(), regD.S(), regT.S());
    }

//	x86SetJ8(j8Ptr[4]);
    armBind(&j8Ptr4);
//	x86SetJ8(j8Ptr[5]);
    armBind(&j8Ptr5);
//	x86SetJ8(j8Ptr[6]);
    armBind(&j8Ptr6);
//	x86SetJ8(j8Ptr[7]);
    armBind(&j8Ptr7);

	_freeXMMreg(xmmtemp);
}

void FPU_ADD(int regd, int regt)
{
	if (FPU_CORRECT_ADD_SUB) {
        FPU_ADD_SUB(regd, regt, 0);
    }
	else {
//        xADD.SS(xRegisterSSE(regd), xRegisterSSE(regt));
        auto regD = a64::QRegister(regd);
        armAsm->Fadd(regD.S(), regD.S(), a64::QRegister(regt).S());
    }
}

void FPU_SUB(int regd, int regt)
{
	if (FPU_CORRECT_ADD_SUB) {
        FPU_ADD_SUB(regd, regt, 1);
    }
	else {
//        xSUB.SS(xRegisterSSE(regd), xRegisterSSE(regt));
        auto regD = a64::QRegister(regd);
        armAsm->Fsub(regD.S(), regD.S(), a64::QRegister(regt).S());
    }
}

//------------------------------------------------------------------
// Note: PS2's multiplication uses some variant of booth multiplication with wallace trees:
// It cuts off some bits, resulting in inaccurate and non-commutative results.
// The PS2's result mantissa is either equal to x86's rounding to zero result mantissa
// or SMALLER (by 0x1). (this means that x86's other rounding modes are only less similar to PS2's mul)
//------------------------------------------------------------------

void FPU_MUL(int regd, int regt, bool reverseOperands)
{
//	u8 *endMul = nullptr;
    a64::Label endMul;

    auto regD = a64::QRegister(regd);
    auto regT = a64::QRegister(regt);

	if (CHECK_FPUMULHACK)
	{
		// 	if ((s == 0x3e800000) && (t == 0x40490fdb))
		// 		return 0x3f490fda; // needed for Tales of Destiny Remake (only in a very specific room late-game)
		// 	else
		// 		return 0;

//		alignas(16) static constexpr const u32 result[4] = { 0x3f490fda };

//		xMOVD(ecx, xRegisterSSE(reverseOperands ? regt : regd));
        armAsm->Fmov(ECX, reverseOperands ? regT.S() : regD.S());
//		xMOVD(edx, xRegisterSSE(reverseOperands ? regd : regt));
        armAsm->Fmov(EDX, reverseOperands ? regD.S() : regT.S());

		// if (((s ^ 0x3e800000) | (t ^ 0x40490fdb)) != 0) { hack; }
//		xXOR(ecx, 0x3e800000);
        armAsm->Eor(ECX, ECX, 0x3e800000);
//		xXOR(edx, 0x40490fdb);
        armAsm->Eor(EDX, EDX, 0x40490fdb);
//		xOR(edx, ecx);
        armAsm->Orr(EDX, EDX, ECX);

//		u8* noHack = JNZ8(0);
        a64::Label noHack;
        armCbnz(EDX, &noHack);
//			xMOVAPS(xRegisterSSE(regd), ptr128[result]);
            armAsm->Ldr(regD.Q(), PTR_CPU(mVUss4.result));
//			endMul = JMP8(0);
            armAsm->B(&endMul);
//		x86SetJ8(noHack);
        armBind(&noHack);
	}

//	xMUL.SS(xRegisterSSE(regd), xRegisterSSE(regt));
    armAsm->Fmul(regD.S(), regD.S(), regT.S());

	if (CHECK_FPUMULHACK) {
//        x86SetJ8(endMul);
        armBind(&endMul);
    }
}

void FPU_MUL(int regd, int regt) { FPU_MUL(regd, regt, false); }
void FPU_MUL_REV(int regd, int regt) { FPU_MUL(regd, regt, true); } //reversed operands

void ARM_MAXSS_XMM_to_XMM(int regd, int regt) {
    auto regD = a64::QRegister(regd);
    armAsm->Fmaxnm(regD.V4S(), regD.V4S(), a64::QRegister(regt).V4S());
}
void ARM_MINSS_XMM_to_XMM(int regd, int regt) {
    auto regD = a64::QRegister(regd);
    armAsm->Fminnm(regD.V4S(), regD.V4S(), a64::QRegister(regt).V4S());
}

//------------------------------------------------------------------
// CommutativeOp XMM (used for ADD, MUL, MAX, and MIN opcodes)
//------------------------------------------------------------------
static void (*recComOpXMM_to_XMM[])(int, int) = {
    FPU_ADD, FPU_MUL,     ARM_MAXSS_XMM_to_XMM, ARM_MINSS_XMM_to_XMM};

static void (*recComOpXMM_to_XMM_REV[])(int, int) = { //reversed operands
    FPU_ADD, FPU_MUL_REV, ARM_MAXSS_XMM_to_XMM, ARM_MINSS_XMM_to_XMM};

//static void (*recComOpM32_to_XMM[] )(x86SSERegType, uptr) = {
//	SSE_ADDSS_M32_to_XMM, SSE_MULSS_M32_to_XMM, SSE_MAXSS_M32_to_XMM, SSE_MINSS_M32_to_XMM };

int recCommutativeOp(int info, int regd, int op)
{
	int t0reg = _allocTempXMMreg(XMMT_FPS);
    auto regT0 = a64::QRegister(t0reg);

    auto regD = a64::QRegister(regd);

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
			if (regd == EEREC_S)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW /*&& !CHECK_FPUCLAMPHACK */ || (op >= 2))
				{
					fpuFloat2(regd);
					fpuFloat2(t0reg);
				}
				recComOpXMM_to_XMM[op](regd, t0reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW || (op >= 2))
				{
					fpuFloat2(regd);
					fpuFloat2(EEREC_S);
				}
				recComOpXMM_to_XMM_REV[op](regd, EEREC_S);
			}
			break;
		case PROCESS_EE_T:
			if (regd == EEREC_T)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_OVERFLOW || (op >= 2))
				{
					fpuFloat2(regd);
					fpuFloat2(t0reg);
				}
				recComOpXMM_to_XMM_REV[op](regd, t0reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_OVERFLOW || (op >= 2))
				{
					fpuFloat2(regd);
					fpuFloat2(EEREC_T);
				}
				recComOpXMM_to_XMM[op](regd, EEREC_T);
			}
			break;
		case (PROCESS_EE_S | PROCESS_EE_T):
			if (regd == EEREC_T)
			{
				if (CHECK_FPU_EXTRA_OVERFLOW || (op >= 2))
				{
					fpuFloat2(regd);
					fpuFloat2(EEREC_S);
				}
				recComOpXMM_to_XMM_REV[op](regd, EEREC_S);
			}
			else
			{
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Mov(regD.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				if (CHECK_FPU_EXTRA_OVERFLOW || (op >= 2))
				{
					fpuFloat2(regd);
					fpuFloat2(EEREC_T);
				}
				recComOpXMM_to_XMM[op](regd, EEREC_T);
			}
			break;
		default:
			Console.WriteLn(Color_Magenta, "FPU: recCommutativeOp case 4");
//			xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
			if (CHECK_FPU_EXTRA_OVERFLOW || (op >= 2))
			{
				fpuFloat2(regd);
				fpuFloat2(t0reg);
			}
			recComOpXMM_to_XMM[op](regd, t0reg);
			break;
	}

	_freeXMMreg(t0reg);
	return regd;
}
//------------------------------------------------------------------


//------------------------------------------------------------------
// ADD XMM
//------------------------------------------------------------------
void recADD_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::ADD_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	ClampValues(recCommutativeOp(info, EEREC_D, 0));
	//REC_FPUOP(ADD_S);
}

FPURECOMPILE_CONSTCODE(ADD_S, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

void recADDA_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::ADDA_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	ClampValues(recCommutativeOp(info, EEREC_ACC, 0));
}

FPURECOMPILE_CONSTCODE(ADDA_S, XMMINFO_WRITEACC | XMMINFO_READS | XMMINFO_READT);
//------------------------------------------------------------------

//------------------------------------------------------------------
// BC1x XMM
//------------------------------------------------------------------

static void _setupBranchTest()
{
	_eeFlushAllDirty();

	// COP1 branch conditionals are based on the following equation:
	// (fpuRegs.fprc[31] & 0x00800000)
	// BC2F checks if the statement is false, BC2T checks if the statement is true.

//	xMOV(eax, ptr[&fpuRegs.fprc[31]]);
    armLoad(EAX, PTR_CPU(fpuRegs.fprc[31]));
//	xTEST(eax, FPUflagC);
    armAsm->Tst(EAX, FPUflagC);
}

void recBC1F()
{
	EE::Profiler.EmitOp(eeOpcode::BC1F);
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	const bool swap = TrySwapDelaySlot(0, 0, 0, true);
	_setupBranchTest();
//	recDoBranchImm(branchTo, JNZ32(0), false, swap);

    a64::Label jmpSkip_;
    armAsm->B(&jmpSkip_, a64::Condition::ne);
    recDoBranchImm(branchTo, &jmpSkip_, false, swap);
}

void recBC1T()
{
	EE::Profiler.EmitOp(eeOpcode::BC1T);
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	const bool swap = TrySwapDelaySlot(0, 0, 0, true);
	_setupBranchTest();
//	recDoBranchImm(branchTo, JZ32(0), false, swap);

    a64::Label jmpSkip_;
    armAsm->B(&jmpSkip_, a64::Condition::eq);
    recDoBranchImm(branchTo, &jmpSkip_, false, swap);
}

void recBC1FL()
{
	EE::Profiler.EmitOp(eeOpcode::BC1FL);
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	_setupBranchTest();
//	recDoBranchImm(branchTo, JNZ32(0), true, false);

    a64::Label jmpSkip_;
    armAsm->B(&jmpSkip_, a64::Condition::ne);
    recDoBranchImm(branchTo, &jmpSkip_, true, false);
}

void recBC1TL()
{
	EE::Profiler.EmitOp(eeOpcode::BC1TL);
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	_setupBranchTest();
//	recDoBranchImm(branchTo, JZ32(0), true, false);

    a64::Label jmpSkip_;
    armAsm->B(&jmpSkip_, a64::Condition::eq);
    recDoBranchImm(branchTo, &jmpSkip_, true, false);
}
//------------------------------------------------------------------


//------------------------------------------------------------------
// C.x.S XMM
//------------------------------------------------------------------
void recC_EQ_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::CEQ_F);

	//Console.WriteLn("recC_EQ_xmm()");

    a64::Label j8Ptr0, j8Ptr1;

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
			{
				const int regs = fpuCopyToTempForClamp(_Fs_, EEREC_S);
				fpuFloat3(regs);

				const int t0reg = _allocTempXMMreg(XMMT_FPS);
                auto regT0 = a64::QRegister(t0reg);

//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				fpuFloat3(t0reg);

//				xUCOMI.SS(xRegisterSSE(regs), xRegisterSSE(t0reg));
                armAsm->Fcmp(a64::QRegister(regs).S(), regT0.S());

				_freeXMMreg(t0reg);
				fpuFreeIfTemp(regs);
			}
			break;

		case PROCESS_EE_T:
			{
				const int regt = fpuCopyToTempForClamp(_Ft_, EEREC_T);
				fpuFloat3(regt);

				const int t0reg = _allocTempXMMreg(XMMT_FPS);
                auto regT0 = a64::QRegister(t0reg);

//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				fpuFloat3(t0reg);

//				xUCOMI.SS(xRegisterSSE(t0reg), xRegisterSSE(regt));
                armAsm->Fcmp(regT0.S(), a64::QRegister(regt).S());

				_freeXMMreg(t0reg);
				fpuFreeIfTemp(regt);
			}
			break;

		case (PROCESS_EE_S | PROCESS_EE_T):
			{
				const int regs = fpuCopyToTempForClamp(_Fs_, EEREC_S);
				fpuFloat3(regs);

				const int regt = fpuCopyToTempForClamp(_Ft_, EEREC_T);
				fpuFloat3(regt);

//				xUCOMI.SS(xRegisterSSE(regs), xRegisterSSE(regt));
                armAsm->Fcmp(a64::QRegister(regs).S(), a64::QRegister(regt).S());

				fpuFreeIfTemp(regs);
				fpuFreeIfTemp(regt);
			}
			break;

		default:
			Console.WriteLn(Color_Magenta, "recC_EQ_xmm: Default");
//			xMOV(eax, ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(EAX, PTR_CPU(fpuRegs.fpr[_Fs_]));
//			xCMP(eax, ptr[&fpuRegs.fpr[_Ft_]]);
            armAsm->Cmp(EAX, armLoad(PTR_CPU(fpuRegs.fpr[_Ft_])));

//			j8Ptr[0] = JZ8(0);
            armAsm->B(&j8Ptr0, a64::Condition::eq);
//				xAND(ptr32[&fpuRegs.fprc[31]], ~FPUflagC);
                armAnd(PTR_CPU(fpuRegs.fprc[31]), ~FPUflagC);
//				j8Ptr[1] = JMP8(0);
                armAsm->B(&j8Ptr1);
//			x86SetJ8(j8Ptr[0]);
            armBind(&j8Ptr0);
//				xOR(ptr32[&fpuRegs.fprc[31]], FPUflagC);
                armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagC);
//			x86SetJ8(j8Ptr[1]);
            armBind(&j8Ptr1);
			return;
	}

//	j8Ptr[0] = JZ8(0);
    armAsm->B(&j8Ptr0, a64::Condition::eq);
//		xAND(ptr32[&fpuRegs.fprc[31]], ~FPUflagC);
        armAnd(PTR_CPU(fpuRegs.fprc[31]), ~FPUflagC);
//		j8Ptr[1] = JMP8(0);
        armAsm->B(&j8Ptr1);
//	x86SetJ8(j8Ptr[0]);
    armBind(&j8Ptr0);
//		xOR(ptr32[&fpuRegs.fprc[31]], FPUflagC);
        armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagC);
//	x86SetJ8(j8Ptr[1]);
    armBind(&j8Ptr1);
}

FPURECOMPILE_CONSTCODE(C_EQ, XMMINFO_READS | XMMINFO_READT);
//REC_FPUFUNC(C_EQ);

void recC_F()
{
	EE::Profiler.EmitOp(eeOpcode::CF_F);
//	xAND(ptr32[&fpuRegs.fprc[31]], ~FPUflagC);
    armAnd(PTR_CPU(fpuRegs.fprc[31]), ~FPUflagC);
}
//REC_FPUFUNC(C_F);

void recC_LE_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::CLE_F);

	//Console.WriteLn("recC_LE_xmm()");

    a64::Label j8Ptr0, j8Ptr1;

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
		{
			const int regs = fpuCopyToTempForClamp(_Fs_, EEREC_S);
			fpuFloat3(regs);

			const int t0reg = _allocTempXMMreg(XMMT_FPS);
            auto regT0 = a64::QRegister(t0reg);

//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
			fpuFloat3(t0reg);

//			xUCOMI.SS(xRegisterSSE(regs), xRegisterSSE(t0reg));
            armAsm->Fcmp(a64::QRegister(regs).S(), regT0.S());

			_freeXMMreg(t0reg);
			fpuFreeIfTemp(regs);
		}
		break;

		case PROCESS_EE_T:
		{
			const int regt = fpuCopyToTempForClamp(_Ft_, EEREC_T);
			fpuFloat3(regt);

			const int t0reg = _allocTempXMMreg(XMMT_FPS);
            auto regT0 = a64::QRegister(t0reg);

//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
			fpuFloat3(t0reg);

//			xUCOMI.SS(xRegisterSSE(t0reg), xRegisterSSE(regt));
            armAsm->Fcmp(regT0.S(), a64::QRegister(regt).S());

			_freeXMMreg(t0reg);
			fpuFreeIfTemp(regt);
		}
		break;

		case (PROCESS_EE_S | PROCESS_EE_T):
		{
			const int regs = fpuCopyToTempForClamp(_Fs_, EEREC_S);
			fpuFloat3(regs);

			const int regt = fpuCopyToTempForClamp(_Ft_, EEREC_T);
			fpuFloat3(regt);

//			xUCOMI.SS(xRegisterSSE(regs), xRegisterSSE(regt));
            armAsm->Fcmp(a64::QRegister(regs).S(), a64::QRegister(regt).S());

			fpuFreeIfTemp(regs);
			fpuFreeIfTemp(regt);
		}
		break;

		default: // Untested and incorrect, but this case is never reached AFAIK (cottonvibes)
			Console.WriteLn(Color_Magenta, "recC_LE_xmm: Default");
//			xMOV(eax, ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(EAX, PTR_CPU(fpuRegs.fpr[_Fs_]));
//			xCMP(eax, ptr[&fpuRegs.fpr[_Ft_]]);
            armAsm->Cmp(EAX, armLoad(PTR_CPU(fpuRegs.fpr[_Ft_])));

//			j8Ptr[0] = JLE8(0);
            armAsm->B(&j8Ptr0, a64::Condition::le);
//				xAND(ptr32[&fpuRegs.fprc[31]], ~FPUflagC);
                armAnd(PTR_CPU(fpuRegs.fprc[31]), ~FPUflagC);
//				j8Ptr[1] = JMP8(0);
                armAsm->B(&j8Ptr1);
//			x86SetJ8(j8Ptr[0]);
            armBind(&j8Ptr0);
//				xOR(ptr32[&fpuRegs.fprc[31]], FPUflagC);
                armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagC);
//			x86SetJ8(j8Ptr[1]);
            armBind(&j8Ptr1);
			return;
	}

//	j8Ptr[0] = JBE8(0);
    armAsm->B(&j8Ptr0, a64::Condition::ls);
//		xAND(ptr32[&fpuRegs.fprc[31]], ~FPUflagC);
        armAnd(PTR_CPU(fpuRegs.fprc[31]), ~FPUflagC);
//		j8Ptr[1] = JMP8(0);
        armAsm->B(&j8Ptr1);
//	x86SetJ8(j8Ptr[0]);
    armBind(&j8Ptr0);
//		xOR(ptr32[&fpuRegs.fprc[31]], FPUflagC);
        armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagC);
//	x86SetJ8(j8Ptr[1]);
    armBind(&j8Ptr1);
}

FPURECOMPILE_CONSTCODE(C_LE, XMMINFO_READS | XMMINFO_READT);
//REC_FPUFUNC(C_LE);

void recC_LT_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::CLT_F);

	//Console.WriteLn("recC_LT_xmm()");

    a64::Label j8Ptr0, j8Ptr1;

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
		{
			const int regs = fpuCopyToTempForClamp(_Fs_, EEREC_S);
			fpuFloat3(regs);

			const int t0reg = _allocTempXMMreg(XMMT_FPS);
            auto regT0 = a64::QRegister(t0reg);

//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
			fpuFloat3(t0reg);

//			xUCOMI.SS(xRegisterSSE(regs), xRegisterSSE(t0reg));
            armAsm->Fcmp(a64::QRegister(regs).S(), regT0.S());

			_freeXMMreg(t0reg);
			fpuFreeIfTemp(regs);
		}
		break;

		case PROCESS_EE_T:
		{
			const int regt = fpuCopyToTempForClamp(_Ft_, EEREC_T);
			fpuFloat3(regt);

			const int t0reg = _allocTempXMMreg(XMMT_FPS);
            auto regT0 = a64::QRegister(t0reg);

//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
			fpuFloat3(t0reg);

//			xUCOMI.SS(xRegisterSSE(t0reg), xRegisterSSE(regt));
            armAsm->Fcmp(regT0.S(), a64::QRegister(regt).S());

			_freeXMMreg(t0reg);
			fpuFreeIfTemp(regt);
		}
		break;

		case (PROCESS_EE_S | PROCESS_EE_T):
		{
			const int regs = fpuCopyToTempForClamp(_Fs_, EEREC_S);
			fpuFloat3(regs);

			const int regt = fpuCopyToTempForClamp(_Ft_, EEREC_T);
			fpuFloat3(regt);

//			xUCOMI.SS(xRegisterSSE(regs), xRegisterSSE(regt));
            armAsm->Fcmp(a64::QRegister(regs).S(), a64::QRegister(regt).S());

			fpuFreeIfTemp(regs);
			fpuFreeIfTemp(regt);
		}
		break;

		default:
			Console.WriteLn(Color_Magenta, "recC_LT_xmm: Default");
//			xMOV(eax, ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(EAX, PTR_CPU(fpuRegs.fpr[_Fs_]));
//			xCMP(eax, ptr[&fpuRegs.fpr[_Ft_]]);
            armAsm->Cmp(EAX, armLoad(PTR_CPU(fpuRegs.fpr[_Ft_])));

//			j8Ptr[0] = JL8(0);
            armAsm->B(&j8Ptr0, a64::Condition::lt);
//				xAND(ptr32[&fpuRegs.fprc[31]], ~FPUflagC);
                armAnd(PTR_CPU(fpuRegs.fprc[31]), ~FPUflagC);
//				j8Ptr[1] = JMP8(0);
                armAsm->B(&j8Ptr1);
//			x86SetJ8(j8Ptr[0]);
            armBind(&j8Ptr0);
//				xOR(ptr32[&fpuRegs.fprc[31]], FPUflagC);
                armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagC);
//			x86SetJ8(j8Ptr[1]);
            armBind(&j8Ptr1);
			return;
	}

//	j8Ptr[0] = JB8(0);
    armAsm->B(&j8Ptr0, a64::Condition::cc);
//		xAND(ptr32[&fpuRegs.fprc[31]], ~FPUflagC);
        armAnd(PTR_CPU(fpuRegs.fprc[31]), ~FPUflagC);
//		j8Ptr[1] = JMP8(0);
        armAsm->B(&j8Ptr1);
//	x86SetJ8(j8Ptr[0]);
    armBind(&j8Ptr0);
//		xOR(ptr32[&fpuRegs.fprc[31]], FPUflagC);
        armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagC);
//	x86SetJ8(j8Ptr[1]);
    armBind(&j8Ptr1);
}

FPURECOMPILE_CONSTCODE(C_LT, XMMINFO_READS | XMMINFO_READT);
//REC_FPUFUNC(C_LT);
//------------------------------------------------------------------


//------------------------------------------------------------------
// CVT.x XMM
//------------------------------------------------------------------
void recCVT_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::CVTS_F);
	if (info & PROCESS_EE_D)
	{
        auto regED = a64::QRegister(EEREC_D);

		if (info & PROCESS_EE_S) {
//            xCVTDQ2PS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
            armAsm->Scvtf(regED.V4S(), a64::QRegister(EEREC_S).V4S());
        }
		else {
//            xCVTSI2SS(xRegisterSSE(EEREC_D), ptr32[&fpuRegs.fpr[_Fs_]]);
            armAsm->Scvtf(regED.S(), armLoad(PTR_CPU(fpuRegs.fpr[_Fs_])));
        }
	}
	else
	{
		const int temp = _allocTempXMMreg(XMMT_FPS);
        auto regTemp = a64::QRegister(temp);

//		xCVTSI2SS(xRegisterSSE(temp), ptr32[&fpuRegs.fpr[_Fs_]]);
        armAsm->Scvtf(regTemp.S(), armLoad(PTR_CPU(fpuRegs.fpr[_Fs_])));
//		xMOVSS(ptr32[&fpuRegs.fpr[_Fd_]], xRegisterSSE(temp));
        armStore(PTR_CPU(fpuRegs.fpr[_Fd_]), regTemp.S());
		_freeXMMreg(temp);
	}
}

void recCVT_S()
{
	// Float version is fully accurate, no double version
	eeFPURecompileCode(recCVT_S_xmm, R5900::Interpreter::OpcodeImpl::COP1::CVT_S, XMMINFO_WRITED | XMMINFO_READS);
}

void recCVT_W()
{
	// Float version is fully accurate, no double version

	// If we have the following EmitOP() on the top then it'll get calculated twice when CHECK_FPU_FULL is true
	// as we also have an EmitOP() at recCVT_W() on iFPUd.cpp.  hence we have it below the possible return.
	EE::Profiler.EmitOp(eeOpcode::CVTW);

	int regs = _checkXMMreg(XMMTYPE_FPREG, _Fs_, MODE_READ);

	if (regs >= 0)
	{
//		xCVTTSS2SI(eax, xRegisterSSE(regs));
        armAsm->Fcvtzs(EAX, a64::QRegister(regs).S());
//		xMOVD(edx, xRegisterSSE(regs));
        armAsm->Fmov(EDX, a64::QRegister(regs).S());
	}
	else
	{
//		xCVTTSS2SI(eax, ptr32[&fpuRegs.fpr[_Fs_]]);
        armAsm->Fcvtzs(EAX, armLoadPtrV(PTR_CPU(fpuRegs.fpr[_Fs_])).S());
//		xMOV(edx, ptr[&fpuRegs.fpr[_Fs_]]);
        armLoad(EDX, PTR_CPU(fpuRegs.fpr[_Fs_]));
	}

	//kill register allocation for dst because we write directly to fpuRegs.fpr[_Fd_]
	_deleteFPtoXMMreg(_Fd_, DELETE_REG_FREE_NO_WRITEBACK);

	// cvttss2si converts unrepresentable values to 0x80000000, so negative values are already handled.
	// So we just need to handle positive values.
//	xCMP(edx, 0x4f000000); // If the input is greater than INT_MAX
    armAsm->Cmp(EDX, 0x4f000000);
//	xMOV(edx, 0x7fffffff);
    armAsm->Mov(EDX, 0x7fffffff);
//	xCMOVGE(eax, edx);     // Saturate it
    armAsm->Csel(EAX, EDX, EAX, a64::Condition::ge);

	//Write the result
//	xMOV(ptr[&fpuRegs.fpr[_Fd_]], eax);
    armStore(PTR_CPU(fpuRegs.fpr[_Fd_]), EAX);
}
//------------------------------------------------------------------


//------------------------------------------------------------------
// DIV XMM
//------------------------------------------------------------------
void recDIVhelper1(int regd, int regt) // Sets flags
{
//	u8 *pjmp1, *pjmp2;
//	u32 *ajmp32, *bjmp32;
    a64::Label pjmp1, pjmp2;
    a64::Label ajmp32, bjmp32;

    auto regD = a64::QRegister(regd);
    auto regT = a64::QRegister(regt);

	const int t1reg = _allocTempXMMreg(XMMT_FPS);
    auto regT1 = a64::QRegister(t1reg);

//	xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagI | FPUflagD)); // Clear I and D flags
    armAnd(PTR_CPU(fpuRegs.fprc[31]), ~(FPUflagI | FPUflagD));

	/*--- Check for divide by zero ---*/
//	xXOR.PS(xRegisterSSE(t1reg), xRegisterSSE(t1reg));
    armAsm->Eor(regT1.V16B(), regT1.V16B(), regT1.V16B());
//	xCMPEQ.SS(xRegisterSSE(t1reg), xRegisterSSE(regt));
    armAsm->Fcmeq(regT1.S(), regT1.S(), regT.S());
//	xMOVMSKPS(eax, xRegisterSSE(t1reg));
    armMOVMSKPS(EAX, regT1);
//	xAND(eax, 1); //Check sign (if regt == zero, sign will be set)
    armAsm->And(EAX, EAX, 1);
//	ajmp32 = JZ32(0); //Skip if not set
    armAsm->Cbz(EAX, &ajmp32);

		/*--- Check for 0/0 ---*/
//		xXOR.PS(xRegisterSSE(t1reg), xRegisterSSE(t1reg));
        armAsm->Eor(regT1.V16B(), regT1.V16B(), regT1.V16B());
//		xCMPEQ.SS(xRegisterSSE(t1reg), xRegisterSSE(regd));
        armAsm->Fcmeq(regT1.S(), regT1.S(), regD.S());
//		xMOVMSKPS(eax, xRegisterSSE(t1reg));
        armMOVMSKPS(EAX, regT1);
//		xAND(eax, 1); //Check sign (if regd == zero, sign will be set)
        armAsm->And(EAX, EAX, 1);
//		pjmp1 = JZ8(0); //Skip if not set
        armAsm->Cbz(EAX, &pjmp1);
//			xOR(ptr32[&fpuRegs.fprc[31]], FPUflagI | FPUflagSI); // Set I and SI flags ( 0/0 )
            armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagI | FPUflagSI);
//			pjmp2 = JMP8(0);
            armAsm->B(&pjmp2);
//		x86SetJ8(pjmp1); //x/0 but not 0/0
        armBind(&pjmp1);
//			xOR(ptr32[&fpuRegs.fprc[31]], FPUflagD | FPUflagSD); // Set D and SD flags ( x/0 )
            armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagD | FPUflagSD);
//		x86SetJ8(pjmp2);
        armBind(&pjmp2);

		/*--- Make regd +/- Maximum ---*/
//		xXOR.PS(xRegisterSSE(regd), xRegisterSSE(regt)); // Make regd Positive or Negative
        armAsm->Eor(regD.V16B(), regD.V16B(), regT.V16B());
//		xAND.PS(xRegisterSSE(regd), ptr[&s_neg[0]]); // Get the sign bit
        armAsm->And(regD.V16B(), regD.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_neg[0])).V16B());
//		xOR.PS(xRegisterSSE(regd), ptr[&g_maxvals[0]]); // regd = +/- Maximum
        armAsm->Orr(regD.V16B(), regD.V16B(), armLoadPtrV(PTR_CPU(mVUss4.g_maxvals[0])).V16B());
        //xMOVSSZX(xRegisterSSE(regd), ptr[&g_maxvals[0]]);
//		bjmp32 = JMP32(0);
        armAsm->B(&bjmp32);

//	x86SetJ32(ajmp32);
    armBind(&ajmp32);

	/*--- Normal Divide ---*/
	if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(regt); }
//	xDIV.SS(xRegisterSSE(regd), xRegisterSSE(regt));
    armAsm->Fdiv(regD.S(), regD.S(), regT.S());

	ClampValues(regd);
//	x86SetJ32(bjmp32);
    armBind(&bjmp32);

	_freeXMMreg(t1reg);
}

void recDIVhelper2(int regd, int regt) // Doesn't sets flags
{
	if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(regt); }
//	xDIV.SS(xRegisterSSE(regd), xRegisterSSE(regt));
    auto regD = a64::QRegister(regd);
    armAsm->Fdiv(regD.S(), regD.S(), a64::QRegister(regt).S());
	ClampValues(regd);
}

alignas(16) static FPControlRegister roundmode_nearest;

void recDIV_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::DIV_F);
	int t0reg = _allocTempXMMreg(XMMT_FPS);
    auto regT0 = a64::QRegister(t0reg);
	//Console.WriteLn("DIV");

    auto regED = a64::QRegister(EEREC_D);

	if (EmuConfig.Cpu.FPUFPCR.bitmask != EmuConfig.Cpu.FPUDivFPCR.bitmask) {
//        xLDMXCSR(ptr32[&EmuConfig.Cpu.FPUDivFPCR.bitmask]);
        armAsm->Msr(a64::FPCR, armLoad64(PTR_CPU(Cpu.FPUDivFPCR.bitmask)));
    }

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
			//Console.WriteLn("FPU: DIV case 1");
//			xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
            armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_S).S(), 0);
//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
			if (CHECK_FPU_EXTRA_FLAGS)
				recDIVhelper1(EEREC_D, t0reg);
			else
				recDIVhelper2(EEREC_D, t0reg);
			break;
		case PROCESS_EE_T:
			//Console.WriteLn("FPU: DIV case 2");
			if (EEREC_D == EEREC_T)
			{
//				xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
                armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_T).S(), 0);
//				xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_FLAGS)
					recDIVhelper1(EEREC_D, t0reg);
				else
					recDIVhelper2(EEREC_D, t0reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_FLAGS)
					recDIVhelper1(EEREC_D, EEREC_T);
				else
					recDIVhelper2(EEREC_D, EEREC_T);
			}
			break;
		case (PROCESS_EE_S | PROCESS_EE_T):
			//Console.WriteLn("FPU: DIV case 3");
			if (EEREC_D == EEREC_T)
			{
//				xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
                armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_T).S(), 0);
//				xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
                armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				if (CHECK_FPU_EXTRA_FLAGS)
					recDIVhelper1(EEREC_D, t0reg);
				else
					recDIVhelper2(EEREC_D, t0reg);
			}
			else
			{
//				xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
                armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				if (CHECK_FPU_EXTRA_FLAGS)
					recDIVhelper1(EEREC_D, EEREC_T);
				else
					recDIVhelper2(EEREC_D, EEREC_T);
			}
			break;
		default:
			//Console.WriteLn("FPU: DIV case 4");
//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
//			xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
			if (CHECK_FPU_EXTRA_FLAGS)
				recDIVhelper1(EEREC_D, t0reg);
			else
				recDIVhelper2(EEREC_D, t0reg);
			break;
	}

	if (EmuConfig.Cpu.FPUFPCR.bitmask != EmuConfig.Cpu.FPUDivFPCR.bitmask) {
//        xLDMXCSR(ptr32[&EmuConfig.Cpu.FPUFPCR.bitmask]);
        armAsm->Msr(a64::FPCR, armLoad64(PTR_CPU(Cpu.FPUFPCR.bitmask)));
    }

	_freeXMMreg(t0reg);
}

FPURECOMPILE_CONSTCODE(DIV_S, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);
//------------------------------------------------------------------



//------------------------------------------------------------------
// MADD XMM
//------------------------------------------------------------------
void recMADDtemp(int info, int regd)
{
	const int t0reg = _allocTempXMMreg(XMMT_FPS);
    auto regT0 = a64::QRegister(t0reg);

    auto regD = a64::QRegister(regd);

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
			if (regd == EEREC_S)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Fmul(regD.S(), regD.S(), regT0.S());
				if (info & PROCESS_EE_ACC)
				{
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(EEREC_ACC); fpuFloat(regd); }
					FPU_ADD(regd, EEREC_ACC);
				}
				else
				{
//					xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(EEREC_ACC); fpuFloat(t0reg); }
					FPU_ADD(regd, t0reg);
				}
			}
			else if ((info & PROCESS_EE_ACC) && regd == EEREC_ACC)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(EEREC_S); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_S));
                armAsm->Fmul(regT0.S(), regT0.S(), a64::QRegister(EEREC_S).S());
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_ADD(regd, t0reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_S); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_S).S());
				if (info & PROCESS_EE_ACC)
				{
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(EEREC_ACC); fpuFloat(regd); }
					FPU_ADD(regd, EEREC_ACC);
				}
				else
				{
//					xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(EEREC_ACC); fpuFloat(t0reg); }
					FPU_ADD(regd, t0reg);
				}
			}
			break;
		case PROCESS_EE_T:
			if (regd == EEREC_T)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Fmul(regD.S(), regD.S(), regT0.S());
				if (info & PROCESS_EE_ACC)
				{
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(EEREC_ACC); fpuFloat(regd); }
					FPU_ADD(regd, EEREC_ACC);
				}
				else
				{
//					xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(EEREC_ACC); fpuFloat(t0reg); }
					FPU_ADD(regd, t0reg);
				}
			}
			else if ((info & PROCESS_EE_ACC) && regd == EEREC_ACC)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(EEREC_T); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regT0.S(), regT0.S(), a64::QRegister(EEREC_T).S());
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_ADD(regd, t0reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_T); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_T).S());
				if (info & PROCESS_EE_ACC)
				{
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(EEREC_ACC); fpuFloat(regd); }
					FPU_ADD(regd, EEREC_ACC);
				}
				else
				{
//					xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(EEREC_ACC); fpuFloat(t0reg); }
					FPU_ADD(regd, t0reg);
				}
			}
			break;
		case (PROCESS_EE_S | PROCESS_EE_T):
			if (regd == EEREC_S)
			{
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_T); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_T).S());
				if (info & PROCESS_EE_ACC)
				{
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(EEREC_ACC); }
					FPU_ADD(regd, EEREC_ACC);
				}
				else
				{
//					xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
					FPU_ADD(regd, t0reg);
				}
			}
			else if (regd == EEREC_T)
			{
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_S); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_S).S());
				if (info & PROCESS_EE_ACC)
				{
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(EEREC_ACC); }
					FPU_ADD(regd, EEREC_ACC);
				}
				else
				{
//					xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
					FPU_ADD(regd, t0reg);
				}
			}
			else if ((info & PROCESS_EE_ACC) && regd == EEREC_ACC)
			{
//				xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_S));
                armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(t0reg); fpuFloat2(EEREC_T); }
//				xMUL.SS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regT0.S(), regT0.S(), a64::QRegister(EEREC_T).S());
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_ADD(regd, t0reg);
			}
			else
			{
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Mov(regD.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_T); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_T).S());
				if (info & PROCESS_EE_ACC)
				{
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(EEREC_ACC); }
					FPU_ADD(regd, EEREC_ACC);
				}
				else
				{
//					xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
					FPU_ADD(regd, t0reg);
				}
			}
			break;
		default:
			if ((info & PROCESS_EE_ACC) && regd == EEREC_ACC)
			{
				const int t1reg = _allocTempXMMreg(XMMT_FPS);
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
//				xMOVSSZX(xRegisterSSE(t1reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(a64::QRegister(t1reg).S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(t0reg); fpuFloat2(t1reg); }
//				xMUL.SS(xRegisterSSE(t0reg), xRegisterSSE(t1reg));
                armAsm->Fmul(regT0.S(), regT0.S(), a64::QRegister(t1reg).S());
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_ADD(regd, t0reg);
				_freeXMMreg(t1reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Fmul(regD.S(), regD.S(), regT0.S());
				if (info & PROCESS_EE_ACC)
				{
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(EEREC_ACC); }
					FPU_ADD(regd, EEREC_ACC);
				}
				else
				{
//					xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
					if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
					FPU_ADD(regd, t0reg);
				}
			}
			break;
	}

	ClampValues(regd);
	_freeXMMreg(t0reg);
}

void recMADD_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MADD_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	recMADDtemp(info, EEREC_D);
}

FPURECOMPILE_CONSTCODE(MADD_S, XMMINFO_WRITED | XMMINFO_READACC | XMMINFO_READS | XMMINFO_READT);

void recMADDA_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MADDA_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	recMADDtemp(info, EEREC_ACC);
}

FPURECOMPILE_CONSTCODE(MADDA_S, XMMINFO_WRITEACC | XMMINFO_READACC | XMMINFO_READS | XMMINFO_READT);
//------------------------------------------------------------------


//------------------------------------------------------------------
// MAX / MIN XMM
//------------------------------------------------------------------
void recMAX_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MAX_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	recCommutativeOp(info, EEREC_D, 2);
}

FPURECOMPILE_CONSTCODE(MAX_S, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

void recMIN_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MIN_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	recCommutativeOp(info, EEREC_D, 3);
}

FPURECOMPILE_CONSTCODE(MIN_S, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);
//------------------------------------------------------------------


//------------------------------------------------------------------
// MOV XMM
//------------------------------------------------------------------
void recMOV_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MOV_F);

    auto regED = a64::QRegister(EEREC_D);

	if (info & PROCESS_EE_S) {
//        xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
        armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_S).S(), 0);
    }
	else {
//        xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Fs_]]);
        armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
    }
}

FPURECOMPILE_CONSTCODE(MOV_S, XMMINFO_WRITED | XMMINFO_READS);
//------------------------------------------------------------------


//------------------------------------------------------------------
// MSUB XMM
//------------------------------------------------------------------
void recMSUBtemp(int info, int regd)
{
	int t0reg = _allocTempXMMreg(XMMT_FPS);
    auto regT0 = a64::QRegister(t0reg);

    auto regD = a64::QRegister(regd);

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
			if (regd == EEREC_S)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Fmul(regD.S(), regD.S(), regT0.S());
				if (info & PROCESS_EE_ACC) {
//                    xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_ACC));
                    armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_ACC).S(), 0);
                }
				else {
//                    xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
                }
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(t0reg, regd);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Mov(regD.S(), 0, regT0.S(), 0);
			}
			else if ((info & PROCESS_EE_ACC) && regd == EEREC_ACC)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(EEREC_S); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_S));
                armAsm->Fmul(regT0.S(), regT0.S(), a64::QRegister(EEREC_S).S());
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(regd, t0reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_S); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_S).S());
				if (info & PROCESS_EE_ACC) {
//                    xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_ACC));
                    armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_ACC).S(), 0);
                }
				else {
//                    xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
                }
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(t0reg, regd);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Mov(regD.S(), 0, regT0.S(), 0);
			}
			break;
		case PROCESS_EE_T:
			if (regd == EEREC_T)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Fmul(regD.S(), regD.S(), regT0.S());
				if (info & PROCESS_EE_ACC) {
//                    xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_ACC));
                    armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_ACC).S(), 0);
                }
				else {
//                    xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
                }
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(t0reg, regd);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Mov(regD.S(), 0, regT0.S(), 0);
			}
			else if ((info & PROCESS_EE_ACC) && regd == EEREC_ACC)
			{
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(EEREC_T); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regT0.S(), regT0.S(), a64::QRegister(EEREC_T).S());
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(regd, t0reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_T); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_T).S());
				if (info & PROCESS_EE_ACC) {
//                    xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_ACC));
                    armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_ACC).S(), 0);
                }
				else {
//                    xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
                }
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(t0reg, regd);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Mov(regD.S(), 0, regT0.S(), 0);
			}
			break;
		case (PROCESS_EE_S | PROCESS_EE_T):
			if (regd == EEREC_S)
			{
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_T); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_T).S());
				if (info & PROCESS_EE_ACC) {
//                    xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_ACC));
                    armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_ACC).S(), 0);
                }
				else {
//                    xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
                }
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(t0reg, regd);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Mov(regD.S(), 0, regT0.S(), 0);
			}
			else if (regd == EEREC_T)
			{
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_S); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_S).S());
				if (info & PROCESS_EE_ACC) {
//                    xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_ACC));
                    armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_ACC).S(), 0);
                }
				else {
//                    xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
                }
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(t0reg, regd);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Mov(regD.S(), 0, regT0.S(), 0);
			}
			else if ((info & PROCESS_EE_ACC) && regd == EEREC_ACC)
			{
//				xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_S));
                armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(t0reg); fpuFloat2(EEREC_T); }
//				xMUL.SS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regT0.S(), regT0.S(), a64::QRegister(EEREC_T).S());
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(regd, t0reg);
			}
			else
			{
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Mov(regD.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(EEREC_T); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(EEREC_T));
                armAsm->Fmul(regD.S(), regD.S(), a64::QRegister(EEREC_T).S());
				if (info & PROCESS_EE_ACC) {
//                    xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_ACC));
                    armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_ACC).S(), 0);
                }
				else {
//                    xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
                }
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(t0reg, regd);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Mov(regD.S(), 0, regT0.S(), 0);
			}
			break;
		default:
			if ((info & PROCESS_EE_ACC) && regd == EEREC_ACC)
			{
				const int t1reg = _allocTempXMMreg(XMMT_FPS);
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
//				xMOVSSZX(xRegisterSSE(t1reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(a64::QRegister(t1reg).S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(t0reg); fpuFloat2(t1reg); }
//				xMUL.SS(xRegisterSSE(t0reg), xRegisterSSE(t1reg));
                armAsm->Fmul(regT0.S(), regT0.S(), a64::QRegister(t1reg).S());
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(regd, t0reg);
				_freeXMMreg(t1reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
//				xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
                armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat2(regd); fpuFloat2(t0reg); }
//				xMUL.SS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Fmul(regD.S(), regD.S(), regT0.S());
				if (info & PROCESS_EE_ACC) {
//                    xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_ACC));
                    armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_ACC).S(), 0);
                }
				else {
//                    xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.ACC]);
                    armLoad(regT0.S(), PTR_CPU(fpuRegs.ACC));
                }
				if (CHECK_FPU_EXTRA_OVERFLOW) { fpuFloat(regd); fpuFloat(t0reg); }
				FPU_SUB(t0reg, regd);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(t0reg));
                armAsm->Mov(regD.S(), 0, regT0.S(), 0);
			}
			break;
	}

	ClampValues(regd);
	_freeXMMreg(t0reg);
}

void recMSUB_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MSUB_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	recMSUBtemp(info, EEREC_D);
}

FPURECOMPILE_CONSTCODE(MSUB_S, XMMINFO_WRITED | XMMINFO_READACC | XMMINFO_READS | XMMINFO_READT);

void recMSUBA_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MSUBA_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	recMSUBtemp(info, EEREC_ACC);
}

FPURECOMPILE_CONSTCODE(MSUBA_S, XMMINFO_WRITEACC | XMMINFO_READACC | XMMINFO_READS | XMMINFO_READT);
//------------------------------------------------------------------


//------------------------------------------------------------------
// MUL XMM
//------------------------------------------------------------------
void recMUL_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MUL_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	ClampValues(recCommutativeOp(info, EEREC_D, 1));
}

FPURECOMPILE_CONSTCODE(MUL_S, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

void recMULA_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::MULA_F);
	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
	ClampValues(recCommutativeOp(info, EEREC_ACC, 1));
}

FPURECOMPILE_CONSTCODE(MULA_S, XMMINFO_WRITEACC | XMMINFO_READS | XMMINFO_READT);
//------------------------------------------------------------------


//------------------------------------------------------------------
// NEG XMM
//------------------------------------------------------------------
void recNEG_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::NEG_F);

    auto regED = a64::QRegister(EEREC_D);

	if (info & PROCESS_EE_S) {
//        xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
        armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_S).S(), 0);
    }
	else {
//        xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Fs_]]);
        armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
    }

	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags
//	xXOR.PS(xRegisterSSE(EEREC_D), ptr[&s_neg[0]]);
    armAsm->Eor(regED.V16B(), regED.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_neg[0])).V16B());

	// Always preserve sign. Using float clamping here would result in
	// +inf to become +fMax instead of -fMax, which is definitely wrong.
	fpuFloat3(EEREC_D);
}

FPURECOMPILE_CONSTCODE(NEG_S, XMMINFO_WRITED | XMMINFO_READS);
//------------------------------------------------------------------


//------------------------------------------------------------------
// SUB XMM
//------------------------------------------------------------------
void recSUBhelper(int regd, int regt)
{
	if (CHECK_FPU_EXTRA_OVERFLOW /*&& !CHECK_FPUCLAMPHACK*/) { fpuFloat2(regd); fpuFloat2(regt); }
	FPU_SUB(regd, regt);
}

void recSUBop(int info, int regd)
{
	int t0reg = _allocTempXMMreg(XMMT_FPS);
    auto regT0 = a64::QRegister(t0reg);

    auto regD = a64::QRegister(regd);

	//xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagO|FPUflagU)); // Clear O and U flags

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
			//Console.WriteLn("FPU: SUB case 1");
			if (regd != EEREC_S) {
//                xMOVSS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Mov(regD.S(), 0, a64::QRegister(EEREC_S).S(), 0);
            }
//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
			recSUBhelper(regd, t0reg);
			break;
		case PROCESS_EE_T:
			//Console.WriteLn("FPU: SUB case 2");
			if (regd == EEREC_T)
			{
//				xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
                armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_T).S(), 0);
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				recSUBhelper(regd, t0reg);
			}
			else
			{
//				xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
                armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
				recSUBhelper(regd, EEREC_T);
			}
			break;
		case (PROCESS_EE_S | PROCESS_EE_T):
			//Console.WriteLn("FPU: SUB case 3");
			if (regd == EEREC_T)
			{
//				xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
                armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_T).S(), 0);
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Mov(regD.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				recSUBhelper(regd, t0reg);
			}
			else
			{
//				xMOVSS(xRegisterSSE(regd), xRegisterSSE(EEREC_S));
                armAsm->Mov(regD.S(), 0, a64::QRegister(EEREC_S).S(), 0);
				recSUBhelper(regd, EEREC_T);
			}
			break;
		default:
			Console.Warning("FPU: SUB case 4");
//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
//			xMOVSSZX(xRegisterSSE(regd), ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(regD.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
			recSUBhelper(regd, t0reg);
			break;
	}

	ClampValues(regd);
	_freeXMMreg(t0reg);
}

void recSUB_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::SUB_F);
	recSUBop(info, EEREC_D);
}

FPURECOMPILE_CONSTCODE(SUB_S, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);


void recSUBA_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::SUBA_F);
	recSUBop(info, EEREC_ACC);
}

FPURECOMPILE_CONSTCODE(SUBA_S, XMMINFO_WRITEACC | XMMINFO_READS | XMMINFO_READT);
//------------------------------------------------------------------


//------------------------------------------------------------------
// SQRT XMM
//------------------------------------------------------------------
void recSQRT_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::SQRT_F);
	bool roundmodeFlag = false;
	//Console.WriteLn("FPU: SQRT");

    auto regED = a64::QRegister(EEREC_D);

	if (EmuConfig.Cpu.FPUFPCR.GetRoundMode() != FPRoundMode::Nearest)
	{
		// Set roundmode to nearest if it isn't already
		//Console.WriteLn("sqrt to nearest");
		roundmode_nearest = EmuConfig.Cpu.FPUFPCR;
		roundmode_nearest.SetRoundMode(FPRoundMode::Nearest);
//		xLDMXCSR(ptr32[&roundmode_nearest.bitmask]);
        armAsm->Msr(a64::FPCR, armLoad64(PTR_CPU(Cpu.FPUFPCR.bitmask)));
		roundmodeFlag = true;
	}

	if (info & PROCESS_EE_T) {
//        xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_T));
        armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_T).S(), 0);
    }
	else {
//        xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Ft_]]);
        armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
    }

	if (CHECK_FPU_EXTRA_FLAGS)
	{
//		xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagI | FPUflagD)); // Clear I and D flags
        armAnd(PTR_CPU(fpuRegs.fprc[31]), ~(FPUflagI | FPUflagD));

		/*--- Check for negative SQRT ---*/
//		xMOVMSKPS(eax, xRegisterSSE(EEREC_D));
        armMOVMSKPS(EAX, regED);
//		xAND(eax, 1); //Check sign
        armAsm->And(EAX, EAX, 1);
//		u8* pjmp = JZ8(0); //Skip if none are
        a64::Label pjmp;
        armAsm->Cbz(EAX, &pjmp);
//			xOR(ptr32[&fpuRegs.fprc[31]], FPUflagI | FPUflagSI); // Set I and SI flags
            armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagI | FPUflagSI);
//			xAND.PS(xRegisterSSE(EEREC_D), ptr[&s_pos[0]]); // Make EEREC_D Positive
            armAsm->And(regED.V16B(), regED.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_pos[0])).V16B());
//		x86SetJ8(pjmp);
        armBind(&pjmp);
	}
	else {
//        xAND.PS(xRegisterSSE(EEREC_D), ptr[&s_pos[0]]); // Make EEREC_D Positive
        armAsm->And(regED.V16B(), regED.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_pos[0])).V16B());
    }

	if (CHECK_FPU_OVERFLOW) { // Only need to do positive clamp, since EEREC_D is positive
//        xMIN.SS(xRegisterSSE(EEREC_D), ptr[&g_maxvals[0]]);
        armAsm->Fminnm(regED.S(), regED.S(), armLoadPtrV(PTR_CPU(mVUss4.g_maxvals[0])).S());
    }
//	xSQRT.SS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_D));
    armAsm->Fsqrt(regED.S(), regED.S());
	if (CHECK_FPU_EXTRA_OVERFLOW) // Shouldn't need to clamp again since SQRT of a number will always be smaller than the original number, doing it just incase :/
		ClampValues(EEREC_D);

	if (roundmodeFlag) {
//        xLDMXCSR(ptr32[&EmuConfig.Cpu.FPUFPCR.bitmask]);
        armAsm->Msr(a64::FPCR, armLoad64(PTR_CPU(Cpu.FPUFPCR.bitmask)));
    }
}

FPURECOMPILE_CONSTCODE(SQRT_S, XMMINFO_WRITED | XMMINFO_READT);
//------------------------------------------------------------------


//------------------------------------------------------------------
// RSQRT XMM
//------------------------------------------------------------------
void recRSQRThelper1(int regd, int t0reg) // Preforms the RSQRT function when regd <- Fs and t0reg <- Ft (Sets correct flags)
{
//	u8 *pjmp1, *pjmp2;
//	u32 *pjmp32;
//	u8 *qjmp1, *qjmp2;
    a64::Label pjmp1, pjmp2;
    a64::Label pjmp32;
    a64::Label qjmp1, qjmp2;

    auto regD = a64::QRegister(regd);
    auto regT0 = a64::QRegister(t0reg);

	int t1reg = _allocTempXMMreg(XMMT_FPS);

//	xAND(ptr32[&fpuRegs.fprc[31]], ~(FPUflagI | FPUflagD)); // Clear I and D flags
    armAnd(PTR_CPU(fpuRegs.fprc[31]), ~(FPUflagI | FPUflagD));

	/*--- (first) Check for negative SQRT ---*/
//	xMOVMSKPS(eax, xRegisterSSE(t0reg));
    armMOVMSKPS(EAX, regT0);
//	xAND(eax, 1); //Check sign
    armAsm->And(EAX, EAX, 1);
//	pjmp2 = JZ8(0); //Skip if not set
    armAsm->Cbz(EAX, &pjmp2);
//		xOR(ptr32[&fpuRegs.fprc[31]], FPUflagI | FPUflagSI); // Set I and SI flags
        armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagI | FPUflagSI);
//		xAND.PS(xRegisterSSE(t0reg), ptr[&s_pos[0]]); // Make t0reg Positive
        armAsm->And(regT0.V16B(), regT0.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_pos[0])).V16B()); // Make t0reg Positive
//	x86SetJ8(pjmp2);
    armBind(&pjmp2);

	/*--- Check for zero ---*/
//	xXOR.PS(xRegisterSSE(t1reg), xRegisterSSE(t1reg));
    armAsm->Eor(a64::QRegister(t1reg).V16B(), a64::QRegister(t1reg).V16B(), a64::QRegister(t1reg).V16B());
//	xCMPEQ.SS(xRegisterSSE(t1reg), xRegisterSSE(t0reg));
    armAsm->Fcmeq(a64::QRegister(t1reg).S(), a64::QRegister(t1reg).S(), regT0.S());
//	xMOVMSKPS(eax, xRegisterSSE(t1reg));
    armMOVMSKPS(EAX, a64::QRegister(t1reg));
//	xAND(eax, 1); //Check sign (if t0reg == zero, sign will be set)
    armAsm->And(EAX, EAX, 1);
//	pjmp1 = JZ8(0); //Skip if not set
    armAsm->Cbz(EAX, &pjmp1);
		/*--- Check for 0/0 ---*/
//		xXOR.PS(xRegisterSSE(t1reg), xRegisterSSE(t1reg));
        armAsm->Eor(a64::QRegister(t1reg).V16B(), a64::QRegister(t1reg).V16B(), a64::QRegister(t1reg).V16B());
//		xCMPEQ.SS(xRegisterSSE(t1reg), xRegisterSSE(regd));
        armAsm->Fcmeq(a64::QRegister(t1reg).S(), a64::QRegister(t1reg).S(), regD.S());
//		xMOVMSKPS(eax, xRegisterSSE(t1reg));
        armMOVMSKPS(EAX, a64::QRegister(t1reg));
//		xAND(eax, 1); //Check sign (if regd == zero, sign will be set)
        armAsm->And(EAX, EAX, 1);
//		qjmp1 = JZ8(0); //Skip if not set
        armAsm->Cbz(EAX, &qjmp1);
//			xOR(ptr32[&fpuRegs.fprc[31]], FPUflagI | FPUflagSI); // Set I and SI flags ( 0/0 )
            armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagI | FPUflagSI);
//			qjmp2 = JMP8(0);
            armAsm->B(&qjmp2);
//		x86SetJ8(qjmp1); //x/0 but not 0/0
        armBind(&qjmp1);
//			xOR(ptr32[&fpuRegs.fprc[31]], FPUflagD | FPUflagSD); // Set D and SD flags ( x/0 )
            armOrr(PTR_CPU(fpuRegs.fprc[31]), FPUflagD | FPUflagSD);
//		x86SetJ8(qjmp2);
        armBind(&qjmp2);

		/*--- Make regd +/- Maximum ---*/
//		xAND.PS(xRegisterSSE(regd), ptr[&s_neg[0]]); // Get the sign bit
        armAsm->And(regD.V16B(), regD.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_neg[0])).V16B());
//		xOR.PS(xRegisterSSE(regd), ptr[&g_maxvals[0]]); // regd = +/- Maximum
        armAsm->Orr(regD.V16B(), regD.V16B(), armLoadPtrV(PTR_CPU(mVUss4.g_maxvals[0])).V16B());
//		pjmp32 = JMP32(0);
        armAsm->B(&pjmp32);
//	x86SetJ8(pjmp1);
    armBind(&pjmp1);

	if (CHECK_FPU_EXTRA_OVERFLOW)
	{
//		xMIN.SS(xRegisterSSE(t0reg), ptr[&g_maxvals[0]]); // Only need to do positive clamp, since t0reg is positive
        armAsm->Fminnm(regT0.S(), regT0.S(), armLoadPtrV(PTR_CPU(mVUss4.g_maxvals[0])).S());
		fpuFloat2(regd);
	}

//	xSQRT.SS(xRegisterSSE(t0reg), xRegisterSSE(t0reg));
    armAsm->Fsqrt(regT0.S(), regT0.S());
//	xDIV.SS(xRegisterSSE(regd), xRegisterSSE(t0reg));
    armAsm->Fdiv(regD.S(), regD.S(), regT0.S());

	ClampValues(regd);
//	x86SetJ32(pjmp32);
    armBind(&pjmp32);

	_freeXMMreg(t1reg);
}

void recRSQRThelper2(int regd, int t0reg) // Preforms the RSQRT function when regd <- Fs and t0reg <- Ft (Doesn't set flags)
{
    auto regD = a64::QRegister(regd);
    auto regT0 = a64::QRegister(t0reg);

//	xAND.PS(xRegisterSSE(t0reg), ptr[&s_pos[0]]); // Make t0reg Positive
    armAsm->And(regT0.V16B(), regT0.V16B(), armLoadPtrV(PTR_CPU(mVUss4.s_pos[0])).V16B());
	if (CHECK_FPU_EXTRA_OVERFLOW)
	{
//		xMIN.SS(xRegisterSSE(t0reg), ptr[&g_maxvals[0]]); // Only need to do positive clamp, since t0reg is positive
        armAsm->Fminnm(regT0.S(), regT0.S(), armLoadPtrV(PTR_CPU(mVUss4.g_maxvals[0])).S());
		fpuFloat2(regd);
	}
//	xSQRT.SS(xRegisterSSE(t0reg), xRegisterSSE(t0reg));
    armAsm->Fsqrt(regT0.S(), regT0.S());
//	xDIV.SS(xRegisterSSE(regd), xRegisterSSE(t0reg));
    armAsm->Fdiv(regD.S(), regD.S(), regT0.S());
	ClampValues(regd);
}

void recRSQRT_S_xmm(int info)
{
	EE::Profiler.EmitOp(eeOpcode::RSQRT_F);

	// RSQRT doesn't change the round mode, because RSQRTSS ignores the rounding mode in MXCSR.
	const int t0reg = _allocTempXMMreg(XMMT_FPS);
    auto regT0 = a64::QRegister(t0reg);

    auto regED = a64::QRegister(EEREC_D);
	//Console.WriteLn("FPU: RSQRT");

	switch (info & (PROCESS_EE_S | PROCESS_EE_T))
	{
		case PROCESS_EE_S:
			//Console.WriteLn("FPU: RSQRT case 1");
//			xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
            armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_S).S(), 0);
//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
			if (CHECK_FPU_EXTRA_FLAGS)
				recRSQRThelper1(EEREC_D, t0reg);
			else
				recRSQRThelper2(EEREC_D, t0reg);
			break;
		case PROCESS_EE_T:
			//Console.WriteLn("FPU: RSQRT case 2");
//			xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
            armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_T).S(), 0);
//			xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
			if (CHECK_FPU_EXTRA_FLAGS)
				recRSQRThelper1(EEREC_D, t0reg);
			else
				recRSQRThelper2(EEREC_D, t0reg);
			break;
		case (PROCESS_EE_S | PROCESS_EE_T):
			//Console.WriteLn("FPU: RSQRT case 3");
//			xMOVSS(xRegisterSSE(t0reg), xRegisterSSE(EEREC_T));
            armAsm->Mov(regT0.S(), 0, a64::QRegister(EEREC_T).S(), 0);
//			xMOVSS(xRegisterSSE(EEREC_D), xRegisterSSE(EEREC_S));
            armAsm->Mov(regED.S(), 0, a64::QRegister(EEREC_S).S(), 0);
			if (CHECK_FPU_EXTRA_FLAGS)
				recRSQRThelper1(EEREC_D, t0reg);
			else
				recRSQRThelper2(EEREC_D, t0reg);
			break;
		default:
			//Console.WriteLn("FPU: RSQRT case 4");
//			xMOVSSZX(xRegisterSSE(t0reg), ptr[&fpuRegs.fpr[_Ft_]]);
            armLoad(regT0.S(), PTR_CPU(fpuRegs.fpr[_Ft_]));
//			xMOVSSZX(xRegisterSSE(EEREC_D), ptr[&fpuRegs.fpr[_Fs_]]);
            armLoad(regED.S(), PTR_CPU(fpuRegs.fpr[_Fs_]));
			if (CHECK_FPU_EXTRA_FLAGS)
				recRSQRThelper1(EEREC_D, t0reg);
			else
				recRSQRThelper2(EEREC_D, t0reg);
			break;
	}
	_freeXMMreg(t0reg);
}

FPURECOMPILE_CONSTCODE(RSQRT_S, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

#endif // FPU_RECOMPILE

} // namespace COP1
} // namespace OpcodeImpl
} // namespace Dynarec
} // namespace R5900
