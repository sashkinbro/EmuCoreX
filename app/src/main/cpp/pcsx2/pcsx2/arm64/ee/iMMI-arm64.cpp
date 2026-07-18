// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/ee/iR5900-arm64.h"
#include "iMMI-arm64.h"
#include "common/BitUtils.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

namespace Interp = R5900::Interpreter::OpcodeImpl::MMI;

namespace R5900 {
namespace Dynarec {
namespace OpcodeImpl {
namespace MMI {

#ifndef MMI_RECOMPILE

REC_FUNC_DEL(PLZCW, _Rd_);

REC_FUNC_DEL(PMFHL, _Rd_);
REC_FUNC_DEL(PMTHL, _Rd_);

REC_FUNC_DEL(PSRLW, _Rd_);
REC_FUNC_DEL(PSRLH, _Rd_);

REC_FUNC_DEL(PSRAH, _Rd_);
REC_FUNC_DEL(PSRAW, _Rd_);

REC_FUNC_DEL(PSLLH, _Rd_);
REC_FUNC_DEL(PSLLW, _Rd_);

#else

void recPLZCW()
{
	if (!_Rd_)
		return;

	// TODO(Stenzek): Don't flush to memory at the end here. Careful of Rs == Rd.

	EE::Profiler.EmitOp(eeOpcode::PLZCW);

	if (!_Rs_ || GPR_IS_CONST1(_Rs_))
	{
		_eeOnWriteReg(_Rd_, 0);
		_deleteEEreg(_Rd_, 0);
		GPR_SET_CONST(_Rd_);

		// Return the leading sign bits, excluding the original bit
		g_cpuConstRegs[_Rd_].UL[0] = !_Rs_ ? 31 : Common::CountLeadingSignBits(g_cpuConstRegs[_Rs_].SL[0]) - 1;
		g_cpuConstRegs[_Rd_].UL[1] = !_Rs_ ? 31 : Common::CountLeadingSignBits(g_cpuConstRegs[_Rs_].SL[1]) - 1;

		return;
	}

	_eeOnWriteReg(_Rd_, 0);

	const int xmmregs = _checkXMMreg(XMMTYPE_GPRREG, _Rs_, MODE_READ);
	const int x86regs = _checkX86reg(X86TYPE_GPR, _Rs_, MODE_READ);

	_deleteEEreg(_Rd_, DELETE_REG_FREE_NO_WRITEBACK);

	// Count the number of leading bits (MSB) that match the sign bit, excluding the sign
	// bit itself.
	recBeginOaknutEmit();
	for (int lane = 0; lane < 2; lane++)
	{
		if (xmmregs >= 0)
		{
			if (lane == 0)
				oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(xmmregs));
			else
				oakAsm->MOV(OAK_WSCRATCH, oakQRegister(xmmregs).Selem()[1]);
		}
		else if (x86regs >= 0)
		{
			if (lane == 0)
				oakAsm->MOV(OAK_WSCRATCH, oakWRegister(x86regs));
			else
				oakAsm->LSR(OAK_XSCRATCH, oakXRegister(x86regs), 32);
		}
		else
		{
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UL[lane]))});
		}

		oakAsm->ASR(OAK_WSCRATCH2, OAK_WSCRATCH, 31);
		oakAsm->EOR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
		oakAsm->CLZ(OAK_WSCRATCH, OAK_WSCRATCH);
		oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, 1);
		oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rd_].UL[lane]))});
	}
	recEndOaknutEmit();

	GPR_DEL_CONST(_Rd_);
}

static void mmiLoadWordFromXmm_emit_oaknut(oak::WReg dst, int xmmreg, int lane)
{
	if (lane == 0)
	{
		oakAsm->FMOV(dst, oakSRegister(xmmreg));
	}
	else
	{
		oakAsm->MOV(dst, oakQRegister(xmmreg).Selem()[lane]);
	}
}

static void mmiLoadByteShuffleMask_emit_oaknut(u64 low, u64 high)
{
	oakAsm->MOV(OAK_XSCRATCH, low);
	oakAsm->INS(OAK_QSCRATCH3.Delem()[0], OAK_XSCRATCH);
	oakAsm->MOV(OAK_XSCRATCH, high);
	oakAsm->INS(OAK_QSCRATCH3.Delem()[1], OAK_XSCRATCH);
}

enum class MMILogicOp
{
	And,
	Nor,
	Or,
	Xor,
};

static void mmiLogicalQ_emit_oaknut(MMILogicOp op, int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	const auto clear_dst = [dstreg]() {
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	};

	const auto move_if_needed = [dstreg](int srcreg) {
		if (dstreg != srcreg)
			oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(srcreg).B16());
	};

	recBeginOaknutEmit();
	switch (op)
	{
		case MMILogicOp::And:
			if (rs_zero || rt_zero)
				clear_dst();
			else
				oakAsm->AND(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
			break;

		case MMILogicOp::Nor:
			if (rs_zero && rt_zero)
			{
				clear_dst();
				oakAsm->NOT(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16());
			}
			else if (rs_zero)
			{
				oakAsm->NOT(oakQRegister(dstreg).B16(), oakQRegister(treg).B16());
			}
			else if (rt_zero)
			{
				oakAsm->NOT(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
			}
			else
			{
				oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
				oakAsm->NOT(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16());
			}
			break;

		case MMILogicOp::Or:
			if (rs_zero && rt_zero)
				clear_dst();
			else if (rs_zero)
				move_if_needed(treg);
			else if (rt_zero)
				move_if_needed(sreg);
			else
				oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
			break;

		case MMILogicOp::Xor:
			if (rs_zero && rt_zero)
				clear_dst();
			else if (rs_zero)
				move_if_needed(treg);
			else if (rt_zero)
				move_if_needed(sreg);
			else
				oakAsm->EOR(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
			break;
	}
	recEndOaknutEmit();
}

static void recPMFHL_LW_emit_oaknut(int dstreg, int loreg, int hireg)
{
	recBeginOaknutEmit();
	oakAsm->TRN1(oakQRegister(dstreg).S4(), oakQRegister(loreg).S4(), oakQRegister(hireg).S4());
	recEndOaknutEmit();
}

static void recPMFHL_UW_emit_oaknut(int dstreg, int loreg, int hireg)
{
	recBeginOaknutEmit();
	oakAsm->TRN2(oakQRegister(dstreg).S4(), oakQRegister(loreg).S4(), oakQRegister(hireg).S4());
	recEndOaknutEmit();
}

static void recPMFHL_SLWLane_emit_oaknut(int dstreg, int loreg, int hireg, int dstlane, int wordlane)
{
	oak::Label clamp_high;
	oak::Label clamp_low;
	oak::Label done;

	recBeginOaknutEmit();
	mmiLoadWordFromXmm_emit_oaknut(OAK_WSCRATCH, loreg, wordlane);
	mmiLoadWordFromXmm_emit_oaknut(OAK_WSCRATCH2, hireg, wordlane);
	oakAsm->LSL(OAK_XSCRATCH2, OAK_XSCRATCH2, 32);
	oakAsm->ORR(OAK_XSCRATCH, OAK_XSCRATCH, OAK_XSCRATCH2);

	oakAsm->MOV(OAK_XSCRATCH2, 0x7fffffff);
	oakAsm->CMP(OAK_XSCRATCH, OAK_XSCRATCH2);
	oakAsm->B(oak::Cond::GE, clamp_high);
	oakAsm->MOV(OAK_XSCRATCH2, UINT64_C(0xffffffff80000000));
	oakAsm->CMP(OAK_XSCRATCH, OAK_XSCRATCH2);
	oakAsm->B(oak::Cond::LE, clamp_low);

	oakAsm->SXTW(OAK_XSCRATCH, OAK_WSCRATCH);
	oakAsm->B(done);

	oakAsm->l(clamp_high);
	oakAsm->MOV(OAK_XSCRATCH, 0x7fffffff);
	oakAsm->B(done);

	oakAsm->l(clamp_low);
	oakAsm->MOV(OAK_XSCRATCH, UINT64_C(0xffffffff80000000));

	oakAsm->l(done);
	oakAsm->INS(oakQRegister(dstreg).Delem()[dstlane], OAK_XSCRATCH);
	recEndOaknutEmit();
}

static void recPMFHL_SLW_emit_oaknut(int dstreg, int loreg, int hireg)
{
	recPMFHL_SLWLane_emit_oaknut(dstreg, loreg, hireg, 0, 0);
	recPMFHL_SLWLane_emit_oaknut(dstreg, loreg, hireg, 1, 2);
}

static void recPMFHL_LH_emit_oaknut(int dstreg, int loreg, int hireg)
{
	recBeginOaknutEmit();
	oakAsm->UZP1(OAK_QSCRATCH.H8(), oakQRegister(loreg).H8(), oakQRegister(hireg).H8());
	oakAsm->REV64(OAK_QSCRATCH2.S4(), OAK_QSCRATCH.S4());
	oakAsm->UZP1(oakQRegister(dstreg).S4(), OAK_QSCRATCH.S4(), OAK_QSCRATCH2.S4());
	recEndOaknutEmit();
}

static void recPMFHL_SH_emit_oaknut(int dstreg, int loreg, int hireg)
{
	recBeginOaknutEmit();
	oakAsm->SQXTN(OAK_DSCRATCH.H4(), oakQRegister(loreg).S4());
	oakAsm->SQXTN2(OAK_QSCRATCH.H8(), oakQRegister(hireg).S4());
	oakAsm->REV64(OAK_QSCRATCH2.S4(), OAK_QSCRATCH.S4());
	oakAsm->UZP1(oakQRegister(dstreg).S4(), OAK_QSCRATCH.S4(), OAK_QSCRATCH2.S4());
	recEndOaknutEmit();
}

void recPMFHL()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PMFHL);

	int info = eeRecompileCodeXMM(XMMINFO_WRITED | XMMINFO_READLO | XMMINFO_READHI);

	switch (_Sa_)
	{
		case 0x00: // LW
			recPMFHL_LW_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI);
			break;

		case 0x01: // UW
			recPMFHL_UW_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI);
			break;

		case 0x02: // SLW
			recPMFHL_SLW_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI);
			break;

		case 0x03: // LH
			recPMFHL_LH_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI);
			break;

		case 0x04: // SH
			recPMFHL_SH_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI);
			break;
		default:
			Console.Error("PMFHL??  *pcsx2 head esplode!*");
			pxFail("PMFHL??  *pcsx2 head esplode!*");
	}

	_clearNeededXMMregs();
}

static void recPMTHL_emit_oaknut(int sreg, int loreg, int hireg)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(loreg).Selem()[0], oakQRegister(sreg).Selem()[0]);
	oakAsm->MOV(oakQRegister(loreg).Selem()[2], oakQRegister(sreg).Selem()[2]);
	oakAsm->MOV(oakQRegister(hireg).Selem()[0], oakQRegister(sreg).Selem()[1]);
	oakAsm->MOV(oakQRegister(hireg).Selem()[2], oakQRegister(sreg).Selem()[3]);
	recEndOaknutEmit();
}

void recPMTHL()
{
	if (_Sa_ != 0)
		return;

	EE::Profiler.EmitOp(eeOpcode::PMTHL);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READLO | XMMINFO_READHI | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPMTHL_emit_oaknut(EEREC_S, EEREC_LO, EEREC_HI);

	_clearNeededXMMregs();
}

static void recPSRLH_emit_oaknut(int dstreg, int treg)
{
	recBeginOaknutEmit();
	if ((_Sa_ & 0xf) == 0)
		oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), oakQRegister(treg).B16());
	else
		oakAsm->USHR(oakQRegister(dstreg).H8(), oakQRegister(treg).H8(), _Sa_ & 0xf);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSRLH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSRLH);

	int info = eeRecompileCodeXMM(XMMINFO_READT | XMMINFO_WRITED);
	recPSRLH_emit_oaknut(EEREC_D, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSRLW_emit_oaknut(int dstreg, int treg)
{
	recBeginOaknutEmit();
	if (_Sa_ == 0)
		oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), oakQRegister(treg).B16());
	else
		oakAsm->USHR(oakQRegister(dstreg).S4(), oakQRegister(treg).S4(), _Sa_);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSRLW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSRLW);

	int info = eeRecompileCodeXMM(XMMINFO_READT | XMMINFO_WRITED);
	recPSRLW_emit_oaknut(EEREC_D, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSRAH_emit_oaknut(int dstreg, int treg)
{
	recBeginOaknutEmit();
	if ((_Sa_ & 0xf) == 0)
		oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), oakQRegister(treg).B16());
	else
		oakAsm->SSHR(oakQRegister(dstreg).H8(), oakQRegister(treg).H8(), _Sa_ & 0xf);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSRAH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSRAH);

	int info = eeRecompileCodeXMM(XMMINFO_READT | XMMINFO_WRITED);
	recPSRAH_emit_oaknut(EEREC_D, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSRAW_emit_oaknut(int dstreg, int treg)
{
	recBeginOaknutEmit();
	if (_Sa_ == 0)
		oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), oakQRegister(treg).B16());
	else
		oakAsm->SSHR(oakQRegister(dstreg).S4(), oakQRegister(treg).S4(), _Sa_);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSRAW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSRAW);

	int info = eeRecompileCodeXMM(XMMINFO_READT | XMMINFO_WRITED);
	recPSRAW_emit_oaknut(EEREC_D, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSLLH_emit_oaknut(int dstreg, int treg)
{
	recBeginOaknutEmit();
	if ((_Sa_ & 0xf) == 0)
		oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), oakQRegister(treg).B16());
	else
		oakAsm->SHL(oakQRegister(dstreg).H8(), oakQRegister(treg).H8(), _Sa_ & 0xf);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSLLH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSLLH);

	int info = eeRecompileCodeXMM(XMMINFO_READT | XMMINFO_WRITED);
	recPSLLH_emit_oaknut(EEREC_D, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSLLW_emit_oaknut(int dstreg, int treg)
{
	recBeginOaknutEmit();
	if (_Sa_ == 0)
		oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), oakQRegister(treg).B16());
	else
		oakAsm->SHL(oakQRegister(dstreg).S4(), oakQRegister(treg).S4(), _Sa_);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSLLW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSLLW);

	int info = eeRecompileCodeXMM(XMMINFO_READT | XMMINFO_WRITED);
	recPSLLW_emit_oaknut(EEREC_D, EEREC_T);
	_clearNeededXMMregs();
}

/*
void recMADD()
{
}

void recMADDU()
{
}

void recPLZCW()
{
}
*/

#endif

/*********************************************************
*   MMI0 opcodes                                         *
*                                                        *
*********************************************************/
#ifndef MMI0_RECOMPILE

REC_FUNC_DEL(PADDB,  _Rd_);
REC_FUNC_DEL(PADDH,  _Rd_);
REC_FUNC_DEL(PADDW,  _Rd_);
REC_FUNC_DEL(PADDSB, _Rd_);
REC_FUNC_DEL(PADDSH, _Rd_);
REC_FUNC_DEL(PADDSW, _Rd_);
REC_FUNC_DEL(PSUBB,  _Rd_);
REC_FUNC_DEL(PSUBH,  _Rd_);
REC_FUNC_DEL(PSUBW,  _Rd_);
REC_FUNC_DEL(PSUBSB, _Rd_);
REC_FUNC_DEL(PSUBSH, _Rd_);
REC_FUNC_DEL(PSUBSW, _Rd_);

REC_FUNC_DEL(PMAXW,  _Rd_);
REC_FUNC_DEL(PMAXH,  _Rd_);

REC_FUNC_DEL(PCGTW,  _Rd_);
REC_FUNC_DEL(PCGTH,  _Rd_);
REC_FUNC_DEL(PCGTB,  _Rd_);

REC_FUNC_DEL(PEXTLW, _Rd_);

REC_FUNC_DEL(PPACW,  _Rd_);
REC_FUNC_DEL(PEXTLH, _Rd_);
REC_FUNC_DEL(PPACH,  _Rd_);
REC_FUNC_DEL(PEXTLB, _Rd_);
REC_FUNC_DEL(PPACB,  _Rd_);
REC_FUNC_DEL(PEXT5,  _Rd_);
REC_FUNC_DEL(PPAC5,  _Rd_);

#else

static void mmi0LoadQSource_emit_oaknut(oak::QReg dst, int hostreg, bool is_zero)
{
	if (is_zero)
		oakAsm->MOVI(dst.B16(), 0);
	else
		oakAsm->MOV(dst.B16(), oakQRegister(hostreg).B16());
}

static void mmi0StoreQ_emit_oaknut(int dstreg, oak::QReg src)
{
	oakAsm->MOV(oakQRegister(dstreg).B16(), src.B16());
}

static void recPMAXW_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SMAX(oakQRegister(dstreg).S4(), oakQRegister(sreg).S4(), oakQRegister(treg).S4());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPMAXW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PMAXW);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPMAXW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPPACW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi0LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->UZP1(oakQRegister(dstreg).S4(), oakQRegister(treg).S4(), ssrc.S4());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPPACW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PPACW);

	int info = eeRecompileCodeXMM(((_Rs_ != 0) ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPPACW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPPACH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi0LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->UZP1(oakQRegister(dstreg).H8(), oakQRegister(treg).H8(), ssrc.H8());
	recEndOaknutEmit();
}

void recPPACH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PPACH);

	int info = eeRecompileCodeXMM((_Rs_ != 0 ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPPACH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPPACB_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi0LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->UZP1(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), ssrc.B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPPACB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PPACB);

	int info = eeRecompileCodeXMM((_Rs_ != 0 ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPPACB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPEXT5_emit_oaknut(int dstreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_QSCRATCH3.B16(), oakQRegister(treg).B16());

	oakAsm->SHL(oakQRegister(dstreg).S4(), OAK_QSCRATCH3.S4(), 3);
	oakAsm->MOVI(OAK_QSCRATCH.S4(), 0xf8);
	oakAsm->AND(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), OAK_QSCRATCH.B16());

	oakAsm->SHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4(), 6);
	oakAsm->MOVI(OAK_QSCRATCH.S4(), 0xf8, oak::LslSymbol::LSL, 8);
	oakAsm->AND(OAK_QSCRATCH2.B16(), OAK_QSCRATCH2.B16(), OAK_QSCRATCH.B16());
	oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), OAK_QSCRATCH2.B16());

	oakAsm->SHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4(), 9);
	oakAsm->MOVI(OAK_QSCRATCH.S4(), 0xf8, oak::LslSymbol::LSL, 16);
	oakAsm->AND(OAK_QSCRATCH2.B16(), OAK_QSCRATCH2.B16(), OAK_QSCRATCH.B16());
	oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), OAK_QSCRATCH2.B16());

	oakAsm->SHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4(), 16);
	oakAsm->MOVI(OAK_QSCRATCH.S4(), 0x80, oak::LslSymbol::LSL, 24);
	oakAsm->AND(OAK_QSCRATCH2.B16(), OAK_QSCRATCH2.B16(), OAK_QSCRATCH.B16());
	oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), OAK_QSCRATCH2.B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPEXT5()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXT5);

	int info = eeRecompileCodeXMM(XMMINFO_READT | XMMINFO_WRITED);
	recPEXT5_emit_oaknut(EEREC_D, EEREC_T);
	_clearNeededXMMregs();
}

static void recPPAC5_emit_oaknut(int dstreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_QSCRATCH3.B16(), oakQRegister(treg).B16());

	oakAsm->USHR(oakQRegister(dstreg).S4(), OAK_QSCRATCH3.S4(), 3);
	oakAsm->MOVI(OAK_QSCRATCH.S4(), 0x1f);
	oakAsm->AND(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), OAK_QSCRATCH.B16());

	oakAsm->USHR(OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4(), 11);
	oakAsm->AND(OAK_QSCRATCH2.B16(), OAK_QSCRATCH2.B16(), OAK_QSCRATCH.B16());
	oakAsm->SHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), 5);
	oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), OAK_QSCRATCH2.B16());

	oakAsm->USHR(OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4(), 19);
	oakAsm->AND(OAK_QSCRATCH2.B16(), OAK_QSCRATCH2.B16(), OAK_QSCRATCH.B16());
	oakAsm->SHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), 10);
	oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), OAK_QSCRATCH2.B16());

	oakAsm->USHR(OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4(), 31);
	oakAsm->SHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), 15);
	oakAsm->ORR(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), OAK_QSCRATCH2.B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPPAC5()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PPAC5);

	int info = eeRecompileCodeXMM(XMMINFO_READT | XMMINFO_WRITED);
	recPPAC5_emit_oaknut(EEREC_D, EEREC_T);
	_clearNeededXMMregs();
}

static void recPMAXH_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SMAX(oakQRegister(dstreg).H8(), oakQRegister(sreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPMAXH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PMAXH);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPMAXH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPCGTB_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->CMGT(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPCGTB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCGTB);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPCGTB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPCGTH_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->CMGT(oakQRegister(dstreg).H8(), oakQRegister(sreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPCGTH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCGTH);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPCGTH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPCGTW_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->CMGT(oakQRegister(dstreg).S4(), oakQRegister(sreg).S4(), oakQRegister(treg).S4());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPCGTW()
{
	//TODO:optimize RS | RT== 0
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCGTW);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPCGTW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPADDSB_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SQADD(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPADDSB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDSB);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPADDSB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPADDSH_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SQADD(oakQRegister(dstreg).H8(), oakQRegister(sreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPADDSH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDSH);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPADDSH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPADDSW_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SQADD(oakQRegister(dstreg).S4(), oakQRegister(sreg).S4(), oakQRegister(treg).S4());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
//NOTE: check kh2 movies if changing this
void recPADDSW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDSW);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPADDSW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSUBSB_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SQSUB(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSUBSB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBSB);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPSUBSB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSUBSH_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SQSUB(oakQRegister(dstreg).H8(), oakQRegister(sreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSUBSH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBSH);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPSUBSH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSUBSW_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SQSUB(oakQRegister(dstreg).S4(), oakQRegister(sreg).S4(), oakQRegister(treg).S4());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
//NOTE: check kh2 movies if changing this
void recPSUBSW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBSW);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPSUBSW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPADDB_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->ADD(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPADDB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDB);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPADDB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPADDH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero && rt_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else if (rs_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(treg).B16());
	else if (rt_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
	else
		oakAsm->ADD(oakQRegister(dstreg).H8(), oakQRegister(sreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPADDH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDH);

	int info = eeRecompileCodeXMM((_Rs_ != 0 ? XMMINFO_READS : 0) | (_Rt_ != 0 ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPADDH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPADDW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero && rt_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else if (rs_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(treg).B16());
	else if (rt_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
	else
		oakAsm->ADD(oakQRegister(dstreg).S4(), oakQRegister(sreg).S4(), oakQRegister(treg).S4());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPADDW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDW);

	int info = eeRecompileCodeXMM((_Rs_ != 0 ? XMMINFO_READS : 0) | (_Rt_ != 0 ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPADDW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPSUBB_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SUB(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSUBB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBB);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPSUBB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSUBH_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SUB(oakQRegister(dstreg).H8(), oakQRegister(sreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSUBH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBH);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPSUBH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPSUBW_emit_oaknut(int dstreg, int sreg, int treg)
{
	recBeginOaknutEmit();
	oakAsm->SUB(oakQRegister(dstreg).S4(), oakQRegister(sreg).S4(), oakQRegister(treg).S4());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSUBW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBW);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recPSUBW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T);
	_clearNeededXMMregs();
}

static void recPEXTLW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi0LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->ZIP1(oakQRegister(dstreg).S4(), oakQRegister(treg).S4(), ssrc.S4());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPEXTLW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXTLW);

	int info = eeRecompileCodeXMM((_Rs_ != 0 ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPEXTLW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPEXTLB_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi0LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->ZIP1(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), ssrc.B16());
	recEndOaknutEmit();
}

void recPEXTLB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXTLB);

	int info = eeRecompileCodeXMM((_Rs_ != 0 ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPEXTLB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPEXTLH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi0LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->ZIP1(oakQRegister(dstreg).H8(), oakQRegister(treg).H8(), ssrc.H8());
	recEndOaknutEmit();
}

void recPEXTLH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXTLH);

	int info = eeRecompileCodeXMM((_Rs_ != 0 ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPEXTLH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

#endif

/*********************************************************
*   MMI1 opcodes                                         *
*                                                        *
*********************************************************/
#ifndef MMI1_RECOMPILE

REC_FUNC_DEL(PABSW,  _Rd_);
REC_FUNC_DEL(PABSH,  _Rd_);

REC_FUNC_DEL(PMINW,  _Rd_);
REC_FUNC_DEL(PADSBH, _Rd_);
REC_FUNC_DEL(PMINH,  _Rd_);
REC_FUNC_DEL(PCEQB,  _Rd_);
REC_FUNC_DEL(PCEQH,  _Rd_);
REC_FUNC_DEL(PCEQW,  _Rd_);

REC_FUNC_DEL(PADDUB, _Rd_);
REC_FUNC_DEL(PADDUH, _Rd_);
REC_FUNC_DEL(PADDUW, _Rd_);

REC_FUNC_DEL(PSUBUB, _Rd_);
REC_FUNC_DEL(PSUBUH, _Rd_);
REC_FUNC_DEL(PSUBUW, _Rd_);

REC_FUNC_DEL(PEXTUW, _Rd_);
REC_FUNC_DEL(PEXTUH, _Rd_);
REC_FUNC_DEL(PEXTUB, _Rd_);
REC_FUNC_DEL(QFSRV,  _Rd_);

#else

static void mmi1LoadQSource_emit_oaknut(oak::QReg dst, int hostreg, bool is_zero)
{
	if (is_zero)
		oakAsm->MOVI(dst.B16(), 0);
	else
		oakAsm->MOV(dst.B16(), oakQRegister(hostreg).B16());
}

static void mmi1StoreQ_emit_oaknut(int dstreg, oak::QReg src)
{
	oakAsm->MOV(oakQRegister(dstreg).B16(), src.B16());
}

static void recPABSW_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, treg, true);
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH : oakQRegister(treg);
	oakAsm->SQABS(oakQRegister(dstreg).S4(), tsrc.S4());
	recEndOaknutEmit();
}

void recPABSW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PABSW);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPABSW_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPABSH_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, treg, true);
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH : oakQRegister(treg);
	oakAsm->SQABS(oakQRegister(dstreg).H8(), tsrc.H8());
	recEndOaknutEmit();
}

void recPABSH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PABSH);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPABSH_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMINW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, true);
	if (rt_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH2, treg, true);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH2 : oakQRegister(treg);
	oakAsm->SMIN(oakQRegister(dstreg).S4(), ssrc.S4(), tsrc.S4());
	recEndOaknutEmit();
}

void recPMINW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PMINW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPMINW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPADSBH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, true);
	if (rt_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH2, treg, true);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH2 : oakQRegister(treg);
	oakAsm->SUB(OAK_QSCRATCH3.H8(), ssrc.H8(), tsrc.H8());
	oakAsm->ADD(OAK_QSCRATCH2.H8(), ssrc.H8(), tsrc.H8());
	oakAsm->MOV(OAK_QSCRATCH3.Delem()[1], OAK_QSCRATCH2.Delem()[1]);
	mmi1StoreQ_emit_oaknut(dstreg, OAK_QSCRATCH3);
	recEndOaknutEmit();
}

void recPADSBH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADSBH);

	const int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPADSBH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPADDUW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero && rt_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else if (rs_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(treg).B16());
	else if (rt_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
	else
		oakAsm->UQADD(oakQRegister(dstreg).S4(), oakQRegister(sreg).S4(), oakQRegister(treg).S4());
	recEndOaknutEmit();
}

void recPADDUW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDUW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPADDUW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPSUBUB_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else if (rt_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
	else
		oakAsm->UQSUB(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
	recEndOaknutEmit();
}

void recPSUBUB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBUB);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPSUBUB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPSUBUH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else if (rt_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
	else
		oakAsm->UQSUB(oakQRegister(dstreg).H8(), oakQRegister(sreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

void recPSUBUH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBUH);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPSUBUH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPSUBUW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else if (rt_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
	else
		oakAsm->UQSUB(oakQRegister(dstreg).S4(), oakQRegister(sreg).S4(), oakQRegister(treg).S4());
	recEndOaknutEmit();
}

void recPSUBUW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSUBUW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPSUBUW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPEXTUH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->ZIP2(oakQRegister(dstreg).H8(), oakQRegister(treg).H8(), ssrc.H8());
	recEndOaknutEmit();
}

void recPEXTUH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXTUH);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPEXTUH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

alignas(16) static u32 tempqw[8];

static void recQFSRV_emit_oaknut(int dstreg, int sreg, int treg, int rs, int rt)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.sa))});
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0xf);

	// QFSRV reads a 128-bit window from the conceptual {Rt, Rs} pair. When the
	// guest registers are adjacent, that pair already has the required layout
	// in cpuRegs.GPR. Store the allocator-resident source values there and use
	// the pinned register-pack base instead of materializing tempqw's absolute
	// address. This is alias-safe because both sources are stored before dst is
	// overwritten, including Rd==Rs/Rt.
	if (rt != 0 && rs == rt + 1)
	{
		const s64 rt_offset = static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0]) + rt * sizeof(GPR_reg));
		const s64 rs_offset = rt_offset + sizeof(GPR_reg);
		pxAssert(rt_offset >= 0 && rt_offset <= 4095);
		oakStore128(oakQRegister(treg), {oak::util::X27, rt_offset});
		oakStore128(oakQRegister(sreg), {oak::util::X27, rs_offset});
		oakAsm->ADD(OAK_XSCRATCH2, oak::util::X27, static_cast<u32>(rt_offset));
	}
	else
	{
		oakMoveAddressToReg(OAK_XSCRATCH2, tempqw);
		oakAsm->STR(oakQRegister(treg), OAK_XSCRATCH2);
		oakAsm->STR(oakQRegister(sreg), OAK_XSCRATCH2, 16);
	}
	oakAsm->LDR(oakQRegister(dstreg), OAK_XSCRATCH2, OAK_XSCRATCH);
	recEndOaknutEmit();
}

void recQFSRV()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::QFSRV);

	int info = eeRecompileCodeXMM(XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED);
	recQFSRV_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_, _Rt_);
	_clearNeededXMMregs();
}

static void recPEXTUB_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->ZIP2(oakQRegister(dstreg).B16(), oakQRegister(treg).B16(), ssrc.B16());
	recEndOaknutEmit();
}

void recPEXTUB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXTUB);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPEXTUB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPEXTUW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, rs_zero);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	oakAsm->ZIP2(oakQRegister(dstreg).S4(), oakQRegister(treg).S4(), ssrc.S4());
	recEndOaknutEmit();
}

void recPEXTUW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXTUW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | XMMINFO_READT | XMMINFO_WRITED);
	recPEXTUW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPMINH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, true);
	if (rt_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH2, treg, true);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH2 : oakQRegister(treg);
	oakAsm->SMIN(oakQRegister(dstreg).H8(), ssrc.H8(), tsrc.H8());
	recEndOaknutEmit();
}

void recPMINH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PMINH);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPMINH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPCEQB_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, true);
	if (rt_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH2, treg, true);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH2 : oakQRegister(treg);
	oakAsm->CMEQ(oakQRegister(dstreg).B16(), ssrc.B16(), tsrc.B16());
	recEndOaknutEmit();
}

void recPCEQB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCEQB);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPCEQB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPCEQH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, true);
	if (rt_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH2, treg, true);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH2 : oakQRegister(treg);
	oakAsm->CMEQ(oakQRegister(dstreg).H8(), ssrc.H8(), tsrc.H8());
	recEndOaknutEmit();
}

void recPCEQH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCEQH);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPCEQH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPCEQW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH, sreg, true);
	if (rt_zero)
		mmi1LoadQSource_emit_oaknut(OAK_QSCRATCH2, treg, true);
	const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH2 : oakQRegister(treg);
	oakAsm->CMEQ(oakQRegister(dstreg).S4(), ssrc.S4(), tsrc.S4());
	recEndOaknutEmit();
}

void recPCEQW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCEQW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPCEQW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPADDUB_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero && rt_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else if (rs_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(treg).B16());
	else if (rt_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
	else
		oakAsm->UQADD(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16(), oakQRegister(treg).B16());
	recEndOaknutEmit();
}

void recPADDUB()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDUB);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPADDUB_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPADDUH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero && rt_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else if (rs_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(treg).B16());
	else if (rt_zero)
		oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(sreg).B16());
	else
		oakAsm->UQADD(oakQRegister(dstreg).H8(), oakQRegister(sreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

void recPADDUH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PADDUH);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPADDUH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

#endif
/*********************************************************
*   MMI2 opcodes                                         *
*                                                        *
*********************************************************/
#ifndef MMI2_RECOMPILE

REC_FUNC_DEL(PMFHI,  _Rd_);
REC_FUNC_DEL(PMFLO,  _Rd_);
REC_FUNC_DEL(PCPYLD, _Rd_);
REC_FUNC_DEL(PAND,  _Rd_);
REC_FUNC_DEL(PXOR,  _Rd_);

REC_FUNC_DEL(PMADDW, _Rd_);
REC_FUNC_DEL(PSLLVW, _Rd_);
REC_FUNC_DEL(PSRLVW, _Rd_);
REC_FUNC_DEL(PMSUBW, _Rd_);
REC_FUNC_DEL(PINTH,  _Rd_);
REC_FUNC_DEL(PMULTW, _Rd_);
REC_FUNC_DEL(PDIVW,  _Rd_);
REC_FUNC_DEL(PMADDH, _Rd_);
REC_FUNC_DEL(PHMADH, _Rd_);
REC_FUNC_DEL(PMSUBH, _Rd_);
REC_FUNC_DEL(PHMSBH, _Rd_);
REC_FUNC_DEL(PEXEH,  _Rd_);
REC_FUNC_DEL(PREVH,  _Rd_);
REC_FUNC_DEL(PMULTH, _Rd_);
REC_FUNC_DEL(PDIVBW, _Rd_);
REC_FUNC_DEL(PEXEW,  _Rd_);
REC_FUNC_DEL(PROT3W, _Rd_);

#else

static void mmi2LoadQSource_emit_oaknut(oak::QReg dst, int hostreg, bool is_zero)
{
	if (is_zero)
		oakAsm->MOVI(dst.B16(), 0);
	else
		oakAsm->MOV(dst.B16(), oakQRegister(hostreg).B16());
}

enum class MMIVariableWordShift
{
	LeftLogical,
	RightLogical,
	RightArithmetic,
};

static void mmi2VariableWordShiftEven_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero, MMIVariableWordShift shift)
{
	if (rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
		return;
	}

	mmi2LoadQSource_emit_oaknut(OAK_QSCRATCH, treg, false);
	oakAsm->UZP1(OAK_QSCRATCH.S4(), OAK_QSCRATCH.S4(), OAK_QSCRATCH.S4());

	if (rs_zero)
	{
		oakAsm->MOV(OAK_QSCRATCH2.B16(), OAK_QSCRATCH.B16());
	}
	else
	{
		mmi2LoadQSource_emit_oaknut(OAK_QSCRATCH2, sreg, false);
		oakAsm->UZP1(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4());
		oakAsm->MOVI(OAK_QSCRATCH3.S4(), 0x1f);
		oakAsm->AND(OAK_QSCRATCH2.B16(), OAK_QSCRATCH2.B16(), OAK_QSCRATCH3.B16());

		if (shift == MMIVariableWordShift::LeftLogical)
		{
			oakAsm->USHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH.S4(), OAK_QSCRATCH2.S4());
		}
		else
		{
			oakAsm->NEG(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4());
			if (shift == MMIVariableWordShift::RightLogical)
				oakAsm->USHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH.S4(), OAK_QSCRATCH2.S4());
			else
				oakAsm->SSHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH.S4(), OAK_QSCRATCH2.S4());
		}
	}

	oakAsm->SSHLL(oakQRegister(dstreg).D2(), OAK_QSCRATCH2.toD().S2(), 0);
}

static void mmi2StoreQ_emit_oaknut(int dstreg, oak::QReg src)
{
	oakAsm->MOV(oakQRegister(dstreg).B16(), src.B16());
}

static void mmi2LoadWordSource_emit_oaknut(oak::WReg dst, int hostreg, int lane, bool is_zero)
{
	if (is_zero)
		oakAsm->MOV(dst, 0);
	else
		mmiLoadWordFromXmm_emit_oaknut(dst, hostreg, lane);
}

static void mmi2LoadSignedHalfSource_emit_oaknut(oak::WReg dst, int hostreg, int lane, bool is_zero)
{
	if (is_zero)
		oakAsm->MOV(dst, 0);
	else
		oakAsm->SMOV(dst, oakQRegister(hostreg).Helem()[lane]);
}

static void mmi2WriteSignedDoubleword_emit_oaknut(int hostreg, int lane, oak::WReg src)
{
	oakAsm->SXTW(OAK_XSCRATCH, src);
	oakAsm->INS(oakQRegister(hostreg).Delem()[static_cast<u8>(lane)], OAK_XSCRATCH);
}

static void mmi2WriteRdFromLoHiEvenWords_emit_oaknut(int dstreg, int loreg, int hireg)
{
	oakAsm->TRN1(oakQRegister(dstreg).S4(), oakQRegister(loreg).S4(), oakQRegister(hireg).S4());
}

static void mmi2SignedHalfProduct_emit_oaknut(int sreg, int treg, int half_lane, bool rs_zero, bool rt_zero)
{
	mmi2LoadSignedHalfSource_emit_oaknut(oak::util::W0, sreg, half_lane, rs_zero);
	mmi2LoadSignedHalfSource_emit_oaknut(oak::util::W1, treg, half_lane, rt_zero);
	oakAsm->MUL(oak::util::W0, oak::util::W0, oak::util::W1);
}

static void mmi2SignedWordDiv_emit_oaknut()
{
	oak::Label not_overflow;
	oak::Label div_nonzero;
	oak::Label done;

	oakAsm->MOV(oak::util::W4, 0x80000000);
	oakAsm->CMP(oak::util::W0, oak::util::W4);
	oakAsm->B(oak::Cond::NE, not_overflow);
	oakAsm->MOV(oak::util::W4, 0xffffffff);
	oakAsm->CMP(oak::util::W1, oak::util::W4);
	oakAsm->B(oak::Cond::NE, not_overflow);
	oakAsm->MOV(oak::util::W2, 0);
	oakAsm->B(done);

	oakAsm->l(not_overflow);
	oakAsm->CBNZ(oak::util::W1, div_nonzero);
	oakAsm->MOV(oak::util::W2, oak::util::W0);
	oakAsm->ASR(oak::util::W0, oak::util::W0, 31);
	oakAsm->LSL(oak::util::W0, oak::util::W0, 1);
	oakAsm->MVN(oak::util::W0, oak::util::W0);
	oakAsm->B(done);

	oakAsm->l(div_nonzero);
	oakAsm->MOV(oak::util::W4, oak::util::W0);
	oakAsm->SDIV(oak::util::W0, oak::util::W4, oak::util::W1);
	oakAsm->MSUB(oak::util::W2, oak::util::W0, oak::util::W1, oak::util::W4);

	oakAsm->l(done);
}

static void mmi2MulAccWordLane_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, int dd, int ss, bool has_rd, bool rs_zero, bool rt_zero, bool add)
{
	mmi2LoadWordSource_emit_oaknut(oak::util::W0, sreg, ss, rs_zero);
	mmi2LoadWordSource_emit_oaknut(oak::util::W1, treg, ss, rt_zero);

	oakAsm->MOV(oak::util::W4, 0);
	if (add && ss == 0)
	{
		oakAsm->MOV(OAK_WSCRATCH, 0x7fffffff);
		oakAsm->AND(OAK_WSCRATCH2, oak::util::W1, OAK_WSCRATCH);
		oakAsm->CMP(OAK_WSCRATCH2, 0);
		oakAsm->CSET(OAK_WSCRATCH, oak::Cond::EQ);
		oakAsm->MOV(oak::util::W2, 0x7fffffff);
		oakAsm->CMP(OAK_WSCRATCH2, oak::util::W2);
		oakAsm->CSET(OAK_WSCRATCH2, oak::Cond::EQ);
		oakAsm->ORR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
		oakAsm->CMP(oak::util::W0, oak::util::W1);
		oakAsm->CSET(OAK_WSCRATCH2, oak::Cond::NE);
		oakAsm->AND(oak::util::W4, OAK_WSCRATCH, OAK_WSCRATCH2);
	}

	oakAsm->SMULL(oak::util::X0, oak::util::W0, oak::util::W1);

	mmi2LoadWordSource_emit_oaknut(oak::util::W1, loreg, ss, false);
	if (add)
		oakAsm->ADD(oak::util::W1, oak::util::W1, oak::util::W0);
	else
		oakAsm->SUB(oak::util::W1, oak::util::W1, oak::util::W0);
	mmi2WriteSignedDoubleword_emit_oaknut(loreg, dd, oak::util::W1);
	if (has_rd)
		oakAsm->MOV(oakQRegister(dstreg).Selem()[dd * 2], oak::util::W1);

	mmi2LoadWordSource_emit_oaknut(oak::util::W1, hireg, ss, false);
	oakAsm->SXTW(oak::util::X1, oak::util::W1);
	oakAsm->LSL(oak::util::X1, oak::util::X1, 32);
	if (add)
	{
		oakAsm->ADD(oak::util::X0, oak::util::X0, oak::util::X1);
		oakAsm->MOV(oak::util::X2, 0x70000000);
		oakAsm->CMP(oak::util::W4, 0);
		oakAsm->CSEL(oak::util::X2, oak::util::X2, oak::util::XZR, oak::Cond::NE);
		oakAsm->ADD(oak::util::X0, oak::util::X0, oak::util::X2);
	}
	else
	{
		oakAsm->SUB(oak::util::X0, oak::util::X1, oak::util::X0);
	}
	oakAsm->MOV(oak::util::X1, 0xffffffff);
	oakAsm->SDIV(oak::util::X0, oak::util::X0, oak::util::X1);
	mmi2WriteSignedDoubleword_emit_oaknut(hireg, dd, oak::util::W0);
	if (has_rd)
		oakAsm->MOV(oakQRegister(dstreg).Selem()[dd * 2 + 1], oak::util::W0);
}

static void recPMADDW_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	mmi2MulAccWordLane_emit_oaknut(dstreg, loreg, hireg, sreg, treg, 0, 0, has_rd, rs_zero, rt_zero, true);
	mmi2MulAccWordLane_emit_oaknut(dstreg, loreg, hireg, sreg, treg, 1, 2, has_rd, rs_zero, rt_zero, true);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPMADDW()
{
	EE::Profiler.EmitOp(eeOpcode::PMADDW);

	// Even when one multiplier is zero, PMADDW's lower lane still inspects both
	// source values for the EE multiplication-error adjustment.
	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) |
		(_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI | XMMINFO_READLO | XMMINFO_READHI);
	recPMADDW_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPSLLVW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	mmi2VariableWordShiftEven_emit_oaknut(dstreg, sreg, treg, rs_zero, rt_zero, MMIVariableWordShift::LeftLogical);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSLLVW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSLLVW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPSLLVW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPSRLVW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	mmi2VariableWordShiftEven_emit_oaknut(dstreg, sreg, treg, rs_zero, rt_zero, MMIVariableWordShift::RightLogical);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPSRLVW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSRLVW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPSRLVW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMSUBW_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	mmi2MulAccWordLane_emit_oaknut(dstreg, loreg, hireg, sreg, treg, 0, 0, has_rd, rs_zero, rt_zero, false);
	mmi2MulAccWordLane_emit_oaknut(dstreg, loreg, hireg, sreg, treg, 1, 2, has_rd, rs_zero, rt_zero, false);
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPMSUBW()
{
	EE::Profiler.EmitOp(eeOpcode::PMSUBW);

	int info = eeRecompileCodeXMM((((_Rs_) && (_Rt_)) ? XMMINFO_READS : 0) | (((_Rs_) && (_Rt_)) ? XMMINFO_READT : 0) | (_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI | XMMINFO_READLO | XMMINFO_READHI);
	recPMSUBW_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMULTW_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	for (int lane = 0; lane < 2; lane++)
	{
		const int word = lane * 2;
		mmi2LoadWordSource_emit_oaknut(oak::util::W0, sreg, word, rs_zero);
		mmi2LoadWordSource_emit_oaknut(oak::util::W1, treg, word, rt_zero);
		oakAsm->SMULL(oak::util::X0, oak::util::W0, oak::util::W1);
		if (has_rd)
			oakAsm->INS(oakQRegister(dstreg).Delem()[static_cast<u8>(lane)], oak::util::X0);
		mmi2WriteSignedDoubleword_emit_oaknut(loreg, lane, oak::util::W0);
		oakAsm->LSR(oak::util::X1, oak::util::X0, 32);
		mmi2WriteSignedDoubleword_emit_oaknut(hireg, lane, oak::util::W1);
	}
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPMULTW()
{
	EE::Profiler.EmitOp(eeOpcode::PMULTW);

	int info = eeRecompileCodeXMM((((_Rs_) && (_Rt_)) ? XMMINFO_READS : 0) | (((_Rs_) && (_Rt_)) ? XMMINFO_READT : 0) | (_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPMULTW_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}
////////////////////////////////////////////////////

static void recPDIVW_emit_oaknut(int loreg, int hireg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	for (int lane = 0; lane < 2; lane++)
	{
		const int word = lane * 2;
		mmi2LoadWordSource_emit_oaknut(oak::util::W0, sreg, word, rs_zero);
		mmi2LoadWordSource_emit_oaknut(oak::util::W1, treg, word, rt_zero);
		mmi2SignedWordDiv_emit_oaknut();
		mmi2WriteSignedDoubleword_emit_oaknut(loreg, lane, oak::util::W0);
		mmi2WriteSignedDoubleword_emit_oaknut(hireg, lane, oak::util::W2);
	}
	recEndOaknutEmit();
}

void recPDIVW()
{
	EE::Profiler.EmitOp(eeOpcode::PDIVW);

	const int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPDIVW_emit_oaknut(EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

////////////////////////////////////////////////////
static void recPDIVBW_emit_oaknut(int loreg, int hireg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	mmi2LoadSignedHalfSource_emit_oaknut(oak::util::W1, treg, 0, rt_zero);
	oakAsm->MOV(OAK_WSCRATCH, oak::util::W1);

	for (int lane = 0; lane < 4; lane++)
	{
		mmi2LoadWordSource_emit_oaknut(oak::util::W0, sreg, lane, rs_zero);
		oakAsm->MOV(oak::util::W1, OAK_WSCRATCH);
		mmi2SignedWordDiv_emit_oaknut();
		oakAsm->MOV(oakQRegister(loreg).Selem()[lane], oak::util::W0);
		oakAsm->MOV(oakQRegister(hireg).Selem()[lane], oak::util::W2);
	}
	recEndOaknutEmit();
}

void recPDIVBW()
{
	EE::Profiler.EmitOp(eeOpcode::PDIVBW);

	const int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPDIVBW_emit_oaknut(EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

////////////////////////////////////////////////////

static void mmi2AccHalfProduct_emit_oaknut(int loreg, int hireg, int sreg, int treg, int half_lane, bool rs_zero, bool rt_zero, bool add)
{
	const int accreg = ((half_lane & 2) != 0) ? hireg : loreg;
	const int acclane = (half_lane & 1) + (((half_lane & 4) != 0) ? 2 : 0);
	mmi2SignedHalfProduct_emit_oaknut(sreg, treg, half_lane, rs_zero, rt_zero);
	mmi2LoadWordSource_emit_oaknut(oak::util::W1, accreg, acclane, false);
	if (add)
		oakAsm->ADD(oak::util::W1, oak::util::W1, oak::util::W0);
	else
		oakAsm->SUB(oak::util::W1, oak::util::W1, oak::util::W0);
	oakAsm->MOV(oakQRegister(accreg).Selem()[acclane], oak::util::W1);
}

static void mmi2StoreHorizontalHalfPair_emit_oaknut(int targetreg, int firstlane, int sreg, int treg, int even_half, bool rs_zero, bool rt_zero, bool subtract_even)
{
	mmi2SignedHalfProduct_emit_oaknut(sreg, treg, even_half + 1, rs_zero, rt_zero);
	oakAsm->MOV(oak::util::W2, oak::util::W0);
	mmi2SignedHalfProduct_emit_oaknut(sreg, treg, even_half, rs_zero, rt_zero);
	if (subtract_even)
	{
		oakAsm->SUB(oak::util::W0, oak::util::W2, oak::util::W0);
		oakAsm->MOV(oakQRegister(targetreg).Selem()[firstlane], oak::util::W0);
		oakAsm->MVN(oak::util::W2, oak::util::W2);
		oakAsm->MOV(oakQRegister(targetreg).Selem()[firstlane + 1], oak::util::W2);
	}
	else
	{
		oakAsm->ADD(oak::util::W0, oak::util::W0, oak::util::W2);
		oakAsm->MOV(oakQRegister(targetreg).Selem()[firstlane], oak::util::W0);
		oakAsm->MOV(oakQRegister(targetreg).Selem()[firstlane + 1], oak::util::W2);
	}
}

static void recPHMADH_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	mmi2StoreHorizontalHalfPair_emit_oaknut(loreg, 0, sreg, treg, 0, rs_zero, rt_zero, false);
	mmi2StoreHorizontalHalfPair_emit_oaknut(hireg, 0, sreg, treg, 2, rs_zero, rt_zero, false);
	mmi2StoreHorizontalHalfPair_emit_oaknut(loreg, 2, sreg, treg, 4, rs_zero, rt_zero, false);
	mmi2StoreHorizontalHalfPair_emit_oaknut(hireg, 2, sreg, treg, 6, rs_zero, rt_zero, false);
	if (has_rd)
		mmi2WriteRdFromLoHiEvenWords_emit_oaknut(dstreg, loreg, hireg);
	recEndOaknutEmit();
}

void recPHMADH()
{
	EE::Profiler.EmitOp(eeOpcode::PHMADH);

	int info = eeRecompileCodeXMM((_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPHMADH_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMSUBH_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	for (int lane = 0; lane < 8; lane++)
		mmi2AccHalfProduct_emit_oaknut(loreg, hireg, sreg, treg, lane, rs_zero, rt_zero, false);
	if (has_rd)
		mmi2WriteRdFromLoHiEvenWords_emit_oaknut(dstreg, loreg, hireg);
	recEndOaknutEmit();
}

void recPMSUBH()
{
	EE::Profiler.EmitOp(eeOpcode::PMSUBH);

	int info = eeRecompileCodeXMM((_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_READS | XMMINFO_READT | XMMINFO_READLO | XMMINFO_READHI | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPMSUBH_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

////////////////////////////////////////////////////
static void recPHMSBH_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	mmi2StoreHorizontalHalfPair_emit_oaknut(loreg, 0, sreg, treg, 0, rs_zero, rt_zero, true);
	mmi2StoreHorizontalHalfPair_emit_oaknut(hireg, 0, sreg, treg, 2, rs_zero, rt_zero, true);
	mmi2StoreHorizontalHalfPair_emit_oaknut(loreg, 2, sreg, treg, 4, rs_zero, rt_zero, true);
	mmi2StoreHorizontalHalfPair_emit_oaknut(hireg, 2, sreg, treg, 6, rs_zero, rt_zero, true);
	if (has_rd)
		mmi2WriteRdFromLoHiEvenWords_emit_oaknut(dstreg, loreg, hireg);
	recEndOaknutEmit();
}

void recPHMSBH()
{
	EE::Profiler.EmitOp(eeOpcode::PHMSBH);

	int info = eeRecompileCodeXMM((_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPHMSBH_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPEXEH_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	}
	else
	{
		mmiLoadByteShuffleMask_emit_oaknut(0x0706010003020504ull, 0x0f0e09080b0a0d0cull);
		oakAsm->MOV(OAK_QSCRATCH2.B16(), oakQRegister(treg).B16());
		oakAsm->TBL(oakQRegister(dstreg).B16(), oak::List(OAK_QSCRATCH2.B16()), OAK_QSCRATCH3.B16());
	}
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPEXEH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXEH);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPEXEH_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPREVH_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	else
		oakAsm->REV64(oakQRegister(dstreg).H8(), oakQRegister(treg).H8());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPREVH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PREVH);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPREVH_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPINTH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero)
	{
		oakAsm->MOVI(OAK_QSCRATCH2.B16(), 0);
	}
	else
	{
		oakAsm->EXT(OAK_QSCRATCH2.B16(), oakQRegister(sreg).B16(), oakQRegister(sreg).B16(), 8);
	}
	const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH : oakQRegister(treg);
	if (rt_zero)
		oakAsm->MOVI(OAK_QSCRATCH.B16(), 0);
	oakAsm->ZIP1(oakQRegister(dstreg).H8(), tsrc.H8(), OAK_QSCRATCH2.H8());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPINTH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PINTH);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPINTH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPEXEW_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	}
	else
	{
		oakAsm->REV64(oakQRegister(dstreg).S4(), oakQRegister(treg).S4());
		oakAsm->EXT(oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), oakQRegister(dstreg).B16(), 12);
	}
	recEndOaknutEmit();
}

void recPEXEW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXEW);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPEXEW_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPROT3W_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	}
	else
	{
		oakAsm->REV64(OAK_QSCRATCH.S4(), oakQRegister(treg).S4());
		oakAsm->EXT(OAK_QSCRATCH2.B16(), oakQRegister(treg).B16(), oakQRegister(treg).B16(), 8);
		oakAsm->ZIP1(oakQRegister(dstreg).S4(), OAK_QSCRATCH.S4(), OAK_QSCRATCH2.S4());
	}
	recEndOaknutEmit();
}

void recPROT3W()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PROT3W);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPROT3W_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMULTH_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	for (int lane = 0; lane < 8; lane++)
	{
		const int accreg = ((lane & 2) != 0) ? hireg : loreg;
		const int acclane = (lane & 1) + (((lane & 4) != 0) ? 2 : 0);
		mmi2SignedHalfProduct_emit_oaknut(sreg, treg, lane, rs_zero, rt_zero);
		oakAsm->MOV(oakQRegister(accreg).Selem()[acclane], oak::util::W0);
	}
	if (has_rd)
		mmi2WriteRdFromLoHiEvenWords_emit_oaknut(dstreg, loreg, hireg);
	recEndOaknutEmit();
}

void recPMULTH()
{
	EE::Profiler.EmitOp(eeOpcode::PMULTH);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | (_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPMULTH_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMFHI_emit_oaknut(int dstreg, int hireg)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(hireg).B16());
	recEndOaknutEmit();
}

void recPMFHI()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PMFHI);

	int info = eeRecompileCodeXMM(XMMINFO_WRITED | XMMINFO_READHI);
	recPMFHI_emit_oaknut(EEREC_D, EEREC_HI);
	_clearNeededXMMregs();
}

static void recPMFLO_emit_oaknut(int dstreg, int loreg)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(dstreg).B16(), oakQRegister(loreg).B16());
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPMFLO()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PMFLO);

	int info = eeRecompileCodeXMM(XMMINFO_WRITED | XMMINFO_READLO);
	recPMFLO_emit_oaknut(EEREC_D, EEREC_LO);
	_clearNeededXMMregs();
}

static void recPAND_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	mmiLogicalQ_emit_oaknut(MMILogicOp::And, dstreg, sreg, treg, rs_zero, rt_zero);
}

////////////////////////////////////////////////////
void recPAND()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PAND);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPAND_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPXOR_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	mmiLogicalQ_emit_oaknut(MMILogicOp::Xor, dstreg, sreg, treg, rs_zero, rt_zero);
}

////////////////////////////////////////////////////
void recPXOR()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PXOR);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPXOR_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPCPYLD_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero && rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	}
	else
	{
		if (rt_zero)
			oakAsm->MOVI(OAK_QSCRATCH.B16(), 0);
		if (rs_zero)
			oakAsm->MOVI(OAK_QSCRATCH2.B16(), 0);

		const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH : oakQRegister(treg);
		const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH2 : oakQRegister(sreg);
		oakAsm->ZIP1(oakQRegister(dstreg).D2(), tsrc.D2(), ssrc.D2());
	}
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void recPCPYLD()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCPYLD);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPCPYLD_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMADDH_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	for (int lane = 0; lane < 8; lane++)
		mmi2AccHalfProduct_emit_oaknut(loreg, hireg, sreg, treg, lane, rs_zero, rt_zero, true);
	if (has_rd)
		mmi2WriteRdFromLoHiEvenWords_emit_oaknut(dstreg, loreg, hireg);
	recEndOaknutEmit();
}

void recPMADDH()
{
	EE::Profiler.EmitOp(eeOpcode::PMADDH);

	int info = eeRecompileCodeXMM((_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_READS | XMMINFO_READT | XMMINFO_READLO | XMMINFO_READHI | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPMADDH_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

#endif
/*********************************************************
*   MMI3 opcodes                                         *
*                                                        *
*********************************************************/
#ifndef MMI3_RECOMPILE

REC_FUNC_DEL(PMADDUW, _Rd_);
REC_FUNC_DEL(PSRAVW,  _Rd_);
REC_FUNC_DEL(PMTHI,   _Rd_);
REC_FUNC_DEL(PMTLO,   _Rd_);
REC_FUNC_DEL(PINTEH,  _Rd_);
REC_FUNC_DEL(PMULTUW, _Rd_);
REC_FUNC_DEL(PDIVUW,  _Rd_);
REC_FUNC_DEL(PCPYUD,  _Rd_);
REC_FUNC_DEL(POR,     _Rd_);
REC_FUNC_DEL(PNOR,    _Rd_);
REC_FUNC_DEL(PCPYH,   _Rd_);
REC_FUNC_DEL(PEXCW,   _Rd_);
REC_FUNC_DEL(PEXCH,   _Rd_);

#else

static void mmi3LoadQSource_emit_oaknut(oak::QReg dst, int hostreg, bool is_zero)
{
	if (is_zero)
		oakAsm->MOVI(dst.B16(), 0);
	else
		oakAsm->MOV(dst.B16(), oakQRegister(hostreg).B16());
}

static void mmi3LoadWordSource_emit_oaknut(oak::WReg dst, int hostreg, int lane, bool is_zero)
{
	if (is_zero)
		oakAsm->MOV(dst, 0);
	else
		mmiLoadWordFromXmm_emit_oaknut(dst, hostreg, lane);
}

static void mmi3WriteSignedDoubleword_emit_oaknut(int hostreg, int lane, oak::WReg src)
{
	oakAsm->SXTW(OAK_XSCRATCH, src);
	oakAsm->INS(oakQRegister(hostreg).Delem()[static_cast<u8>(lane)], OAK_XSCRATCH);
}

static void mmi3LoadUnsignedAcc64_emit_oaknut(oak::XReg dst, int loreg, int hireg, int word)
{
	mmi3LoadWordSource_emit_oaknut(oak::util::W2, loreg, word, false);
	mmi3LoadWordSource_emit_oaknut(oak::util::W4, hireg, word, false);
	oakAsm->ORR(dst, oak::util::X2, oak::util::X4, oak::LogShift::LSL, 32);
}

static void mmi3WriteUnsignedProductResult_emit_oaknut(int dstreg, int loreg, int hireg, int dd, oak::XReg product, bool has_rd)
{
	if (has_rd)
		oakAsm->INS(oakQRegister(dstreg).Delem()[static_cast<u8>(dd)], product);

	mmi3WriteSignedDoubleword_emit_oaknut(loreg, dd, oak::util::W0);
	oakAsm->LSR(oak::util::X1, product, 32);
	mmi3WriteSignedDoubleword_emit_oaknut(hireg, dd, oak::util::W1);
}

static void mmi3UnsignedWordDiv_emit_oaknut()
{
	oak::Label div_nonzero;
	oak::Label done;

	oakAsm->CBNZ(oak::util::W1, div_nonzero);
	oakAsm->MOV(oak::util::W2, oak::util::W0);
	oakAsm->MOV(oak::util::W0, 0xffffffffu);
	oakAsm->B(done);

	oakAsm->l(div_nonzero);
	oakAsm->MOV(oak::util::W4, oak::util::W0);
	oakAsm->UDIV(oak::util::W0, oak::util::W4, oak::util::W1);
	oakAsm->MSUB(oak::util::W2, oak::util::W0, oak::util::W1, oak::util::W4);

	oakAsm->l(done);
}

static void recPMADDUW_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	for (int lane = 0; lane < 2; lane++)
	{
		const int word = lane * 2;
		mmi3LoadWordSource_emit_oaknut(oak::util::W0, sreg, word, rs_zero);
		mmi3LoadWordSource_emit_oaknut(oak::util::W1, treg, word, rt_zero);
		oakAsm->UMULL(oak::util::X0, oak::util::W0, oak::util::W1);
		mmi3LoadUnsignedAcc64_emit_oaknut(oak::util::X1, loreg, hireg, word);
		oakAsm->ADD(oak::util::X0, oak::util::X0, oak::util::X1);
		mmi3WriteUnsignedProductResult_emit_oaknut(dstreg, loreg, hireg, lane, oak::util::X0, has_rd);
	}
	recEndOaknutEmit();
}

void recPMADDUW()
{
	EE::Profiler.EmitOp(eeOpcode::PMADDUW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | (_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI | XMMINFO_READLO | XMMINFO_READHI);
	recPMADDUW_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPSRAVW_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	mmi2VariableWordShiftEven_emit_oaknut(dstreg, sreg, treg, rs_zero, rt_zero, MMIVariableWordShift::RightArithmetic);
	recEndOaknutEmit();
}

void recPSRAVW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PSRAVW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPSRAVW_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMTHI_emit_oaknut(int hireg, int sreg, bool rs_zero)
{
	recBeginOaknutEmit();
	mmi3LoadQSource_emit_oaknut(oakQRegister(hireg), sreg, rs_zero);
	recEndOaknutEmit();
}

void recPMTHI()
{
	EE::Profiler.EmitOp(eeOpcode::PMTHI);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | XMMINFO_WRITEHI);
	recPMTHI_emit_oaknut(EEREC_HI, EEREC_S, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPMTLO_emit_oaknut(int loreg, int sreg, bool rs_zero)
{
	recBeginOaknutEmit();
	mmi3LoadQSource_emit_oaknut(oakQRegister(loreg), sreg, rs_zero);
	recEndOaknutEmit();
}

void recPMTLO()
{
	EE::Profiler.EmitOp(eeOpcode::PMTLO);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | XMMINFO_WRITELO);
	recPMTLO_emit_oaknut(EEREC_LO, EEREC_S, _Rs_ == 0);
	_clearNeededXMMregs();
}

static void recPINTEH_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
	{
		oakAsm->MOVI(OAK_QSCRATCH.B16(), 0);
	}
	else
	{
		oakAsm->UZP1(OAK_QSCRATCH.H8(), oakQRegister(treg).H8(), oakQRegister(treg).H8());
	}
	if (rs_zero)
	{
		oakAsm->MOVI(OAK_QSCRATCH2.B16(), 0);
	}
	else
	{
		oakAsm->UZP1(OAK_QSCRATCH2.H8(), oakQRegister(sreg).H8(), oakQRegister(sreg).H8());
	}
	oakAsm->ZIP1(oakQRegister(dstreg).H8(), OAK_QSCRATCH.H8(), OAK_QSCRATCH2.H8());
	recEndOaknutEmit();
}

void recPINTEH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PINTEH);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPINTEH_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPMULTUW_emit_oaknut(int dstreg, int loreg, int hireg, int sreg, int treg, bool has_rd, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	for (int lane = 0; lane < 2; lane++)
	{
		const int word = lane * 2;
		mmi3LoadWordSource_emit_oaknut(oak::util::W0, sreg, word, rs_zero);
		mmi3LoadWordSource_emit_oaknut(oak::util::W1, treg, word, rt_zero);
		oakAsm->UMULL(oak::util::X0, oak::util::W0, oak::util::W1);
		mmi3WriteUnsignedProductResult_emit_oaknut(dstreg, loreg, hireg, lane, oak::util::X0, has_rd);
	}
	recEndOaknutEmit();
}

void recPMULTUW()
{
	EE::Profiler.EmitOp(eeOpcode::PMULTUW);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | (_Rd_ ? XMMINFO_WRITED : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPMULTUW_emit_oaknut(EEREC_D, EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rd_ != 0, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPDIVUW_emit_oaknut(int loreg, int hireg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	for (int lane = 0; lane < 2; lane++)
	{
		const int word = lane * 2;
		mmi3LoadWordSource_emit_oaknut(oak::util::W0, sreg, word, rs_zero);
		mmi3LoadWordSource_emit_oaknut(oak::util::W1, treg, word, rt_zero);
		mmi3UnsignedWordDiv_emit_oaknut();
		mmi3WriteSignedDoubleword_emit_oaknut(loreg, lane, oak::util::W0);
		mmi3WriteSignedDoubleword_emit_oaknut(hireg, lane, oak::util::W2);
	}
	recEndOaknutEmit();
}

void recPDIVUW()
{
	EE::Profiler.EmitOp(eeOpcode::PDIVUW);

	const int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITELO | XMMINFO_WRITEHI);
	recPDIVUW_emit_oaknut(EEREC_LO, EEREC_HI, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPCPYUD_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rs_zero && rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	}
	else
	{
		if (rs_zero)
			oakAsm->MOVI(OAK_QSCRATCH.B16(), 0);
		if (rt_zero)
			oakAsm->MOVI(OAK_QSCRATCH2.B16(), 0);

		const oak::QReg ssrc = rs_zero ? OAK_QSCRATCH : oakQRegister(sreg);
		const oak::QReg tsrc = rt_zero ? OAK_QSCRATCH2 : oakQRegister(treg);
		oakAsm->ZIP2(oakQRegister(dstreg).D2(), ssrc.D2(), tsrc.D2());
	}
	recEndOaknutEmit();
}

void recPCPYUD()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCPYUD);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPCPYUD_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPOR_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	mmiLogicalQ_emit_oaknut(MMILogicOp::Or, dstreg, sreg, treg, rs_zero, rt_zero);
}

void recPOR()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::POR);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPOR_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPNOR_emit_oaknut(int dstreg, int sreg, int treg, bool rs_zero, bool rt_zero)
{
	mmiLogicalQ_emit_oaknut(MMILogicOp::Nor, dstreg, sreg, treg, rs_zero, rt_zero);
}

void recPNOR()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PNOR);

	int info = eeRecompileCodeXMM((_Rs_ ? XMMINFO_READS : 0) | (_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPNOR_emit_oaknut(EEREC_D, EEREC_S, EEREC_T, _Rs_ == 0, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPCPYH_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	}
	else
	{
		const bool alias = (dstreg == treg);
		if (alias)
			oakAsm->MOV(OAK_QSCRATCH.B16(), oakQRegister(treg).B16());
		const oak::QReg src = alias ? OAK_QSCRATCH : oakQRegister(treg);
		oakAsm->DUP(oakQRegister(dstreg).H8(), src.Helem()[0]);
		oakAsm->DUP(OAK_QSCRATCH2.H8(), src.Helem()[4]);
		oakAsm->MOV(oakQRegister(dstreg).Delem()[1], OAK_QSCRATCH2.Delem()[0]);
	}
	recEndOaknutEmit();
}

void recPCPYH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PCPYH);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPCPYH_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPEXCW_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	}
	else
	{
		oakAsm->REV64(OAK_QSCRATCH.S4(), oakQRegister(treg).S4());
		oakAsm->UZP1(oakQRegister(dstreg).S4(), oakQRegister(treg).S4(), OAK_QSCRATCH.S4());
	}
	recEndOaknutEmit();
}

void recPEXCW()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXCW);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPEXCW_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

static void recPEXCH_emit_oaknut(int dstreg, int treg, bool rt_zero)
{
	recBeginOaknutEmit();
	if (rt_zero)
	{
		oakAsm->MOVI(oakQRegister(dstreg).B16(), 0);
	}
	else
	{
		mmiLoadByteShuffleMask_emit_oaknut(0x0706030205040100ull, 0x0f0e0b0a0d0c0908ull);
		oakAsm->MOV(OAK_QSCRATCH2.B16(), oakQRegister(treg).B16());
		oakAsm->TBL(oakQRegister(dstreg).B16(), oak::List(OAK_QSCRATCH2.B16()), OAK_QSCRATCH3.B16());
	}
	recEndOaknutEmit();
}

void recPEXCH()
{
	if (!_Rd_)
		return;

	EE::Profiler.EmitOp(eeOpcode::PEXCH);

	int info = eeRecompileCodeXMM((_Rt_ ? XMMINFO_READT : 0) | XMMINFO_WRITED);
	recPEXCH_emit_oaknut(EEREC_D, EEREC_T, _Rt_ == 0);
	_clearNeededXMMregs();
}

#endif // else MMI3_RECOMPILE

} // namespace MMI
} // namespace OpcodeImpl
} // namespace Dynarec
} // namespace R5900
