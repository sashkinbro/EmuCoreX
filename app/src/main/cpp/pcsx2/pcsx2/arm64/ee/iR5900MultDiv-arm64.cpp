// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/ee/iR5900-arm64.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

namespace Interp = R5900::Interpreter::OpcodeImpl;

namespace R5900::Dynarec::OpcodeImpl
{

/*********************************************************
* Register mult/div & Register trap logic                *
* Format:  OP rs, rt                                     *
*********************************************************/
#ifndef MULTDIV_RECOMPILE

REC_FUNC_DEL(MULT, _Rd_);
REC_FUNC_DEL(MULTU, _Rd_);
REC_FUNC_DEL(MULT1, _Rd_);
REC_FUNC_DEL(MULTU1, _Rd_);

REC_FUNC(DIV);
REC_FUNC(DIVU);
REC_FUNC(DIV1);
REC_FUNC(DIVU1);

REC_FUNC_DEL(MADD, _Rd_);
REC_FUNC_DEL(MADDU, _Rd_);
REC_FUNC_DEL(MADD1, _Rd_);
REC_FUNC_DEL(MADDU1, _Rd_);

#else

template <bool WriteD, bool Upper>
static void recWritebackHILOExact(int info)
{
	// writeback low 32 bits, sign extended to 64 bits
	bool eax_sign_extended = false;

	// case 1: LO is already in an XMM - use the xmm
	// case 2: LO is used as an XMM later in the block - use or allocate the XMM
	// case 3: LO is used as a GPR later in the block - use XMM if upper, otherwise use GPR, so it can be renamed
	// case 4: LO is already in a GPR - write to the GPR, or write to memory if upper
	// case 4: LO is not used - writeback to memory

	if constexpr (Upper)
	{
		// Upper-lane operations preserve the lower HI/LO halves. Flush and
		// invalidate any full-width cached values before updating memory.
		_deleteEEreg(XMMGPR_LO, 1);
		_deleteEEreg(XMMGPR_HI, 1);
		recBeginOaknutEmit();
		oakAsm->SXTW(OAK_XSCRATCH, oak::util::W0);
		oakStore64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.LO.UD[1]))});
		oakAsm->SXTW(OAK_XSCRATCH, oak::util::W2);
		oakStore64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.HI.UD[1]))});
		recEndOaknutEmit();
	}

	if constexpr (!Upper)
	{
	if (EEINST_LIVETEST(XMMGPR_LO))
	{
		const bool loused = EEINST_USEDTEST(XMMGPR_LO);
		const bool lousedxmm = loused && (Upper || EEINST_XMM_USEDTEST(XMMGPR_LO));
		const int xmmlo = lousedxmm ? _allocGPRtoXMMreg(XMMGPR_LO, MODE_READ | MODE_WRITE) : _checkXMMreg(XMMTYPE_GPRREG, XMMGPR_LO, MODE_WRITE);
		if (xmmlo >= 0)
		{
			recBeginOaknutEmit();
			oakAsm->SXTW(oak::util::X0, oak::util::W0);
			oakAsm->INS(oakQRegister(xmmlo).Delem()[static_cast<u8>(Upper)], oak::util::X0);
			recEndOaknutEmit();
		}
		else
		{
			const int gprlo = Upper ? -1 : (loused ? _allocX86reg(X86TYPE_GPR, XMMGPR_LO, MODE_WRITE) : _checkX86reg(X86TYPE_GPR, XMMGPR_LO, MODE_WRITE));
			if (gprlo >= 0)
			{
				recBeginOaknutEmit();
				oakAsm->SXTW(oakXRegister(gprlo), oak::util::W0);
				recEndOaknutEmit();
			}
			else
			{
				recBeginOaknutEmit();
				oakAsm->SXTW(oak::util::X0, oak::util::W0);
				eax_sign_extended = true;
				oakStore64(oak::util::X0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.LO.UD[Upper]))});
				recEndOaknutEmit();
			}
		}
	}

	if (EEINST_LIVETEST(XMMGPR_HI))
	{
		const bool hiused = EEINST_USEDTEST(XMMGPR_HI);
		const bool hiusedxmm = hiused && (Upper || EEINST_XMM_USEDTEST(XMMGPR_HI));
		const int xmmhi = hiusedxmm ? _allocGPRtoXMMreg(XMMGPR_HI, MODE_READ | MODE_WRITE) : _checkXMMreg(XMMTYPE_GPRREG, XMMGPR_HI, MODE_WRITE);
		if (xmmhi >= 0)
		{
			recBeginOaknutEmit();
			oakAsm->SXTW(oak::util::X2, oak::util::W2);
			oakAsm->INS(oakQRegister(xmmhi).Delem()[static_cast<u8>(Upper)], oak::util::X2);
			recEndOaknutEmit();
		}
		else
		{
			const int gprhi = Upper ? -1 : (hiused ? _allocX86reg(X86TYPE_GPR, XMMGPR_HI, MODE_WRITE) : _checkX86reg(X86TYPE_GPR, XMMGPR_HI, MODE_WRITE));
			if (gprhi >= 0)
			{
				recBeginOaknutEmit();
				oakAsm->SXTW(oakXRegister(gprhi), oak::util::W2);
				recEndOaknutEmit();
			}
			else
			{
				recBeginOaknutEmit();
				oakAsm->SXTW(oak::util::X2, oak::util::W2);
				oakStore64(oak::util::X2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.HI.UD[Upper]))});
				recEndOaknutEmit();
			}
		}
	}
	}

	// writeback lo to Rd if present
	if constexpr (WriteD)
	{
		if (_Rd_ && EEINST_LIVETEST(_Rd_))
		{
			// TODO: This can be made optimal by keeping it in an xmm.
			// But currently the templates aren't hooked up for that - we'd need a "allow xmm" flag.
			if (info & PROCESS_EE_D)
			{
				recBeginOaknutEmit();
				if (eax_sign_extended)
					oakAsm->MOV(oakXRegister(EEREC_D), oak::util::X0);
				else
					oakAsm->SXTW(oakXRegister(EEREC_D), oak::util::W0);
				recEndOaknutEmit();
			}
			else
			{
				recBeginOaknutEmit();
				if (!eax_sign_extended)
					oakAsm->SXTW(oak::util::X0, oak::util::W0);
				oakStore64(oak::util::X0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rd_].UD[0]))});
				recEndOaknutEmit();
			}
		}
	}
}


template <bool WriteD, bool Upper>
static void recWritebackConstHILOExact(u64 res)
{
	// It's not often that MULT/DIV are entirely constant. So while the MOV64s here are not optimal
	// by any means, it's not something that's going to be hit often enough to worry about a cache.
	// Except for apparently when it's getting set to all-zeros, but that'll be fine with immediates.
	const s64 loval = static_cast<s64>(static_cast<s32>(static_cast<u32>(res)));
	const s64 hival = static_cast<s64>(static_cast<s32>(static_cast<u32>(res >> 32)));

	if constexpr (Upper)
	{
		_deleteEEreg(XMMGPR_LO, 1);
		_deleteEEreg(XMMGPR_HI, 1);
		recBeginOaknutEmit();
		oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(loval));
		oakStore64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.LO.UD[1]))});
		oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(hival));
		oakStore64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.HI.UD[1]))});
		recEndOaknutEmit();
	}

	if constexpr (!Upper)
	{
	if (EEINST_LIVETEST(XMMGPR_LO))
	{
		const bool lolive = EEINST_USEDTEST(XMMGPR_LO);
		const bool lolivexmm = lolive && (Upper || EEINST_XMM_USEDTEST(XMMGPR_LO));
		const int xmmlo = lolivexmm ? _allocGPRtoXMMreg(XMMGPR_LO, MODE_READ | MODE_WRITE) : _checkXMMreg(XMMTYPE_GPRREG, XMMGPR_LO, MODE_WRITE);
		if (xmmlo >= 0)
		{
			recBeginOaknutEmit();
			oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(loval));
			oakAsm->INS(oakQRegister(xmmlo).Delem()[static_cast<u8>(Upper)], OAK_XSCRATCH);
			recEndOaknutEmit();
		}
		else
		{
			const int gprlo = Upper ? -1 : (lolive ? _allocX86reg(X86TYPE_GPR, XMMGPR_LO, MODE_WRITE) : _checkX86reg(X86TYPE_GPR, XMMGPR_LO, MODE_WRITE));
			if (gprlo >= 0) {
				recBeginOaknutEmit();
				oakAsm->MOV(oakXRegister(gprlo), static_cast<u64>(loval));
				recEndOaknutEmit();
            }
			else {
				recBeginOaknutEmit();
				oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(loval));
				oakStore64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.LO.UD[Upper]))});
				recEndOaknutEmit();
            }
		}
	}

	if (EEINST_LIVETEST(XMMGPR_HI))
	{
		const bool hilive = EEINST_USEDTEST(XMMGPR_HI);
		const bool hilivexmm = hilive && (Upper || EEINST_XMM_USEDTEST(XMMGPR_HI));
		const int xmmhi = hilivexmm ? _allocGPRtoXMMreg(XMMGPR_HI, MODE_READ | MODE_WRITE) : _checkXMMreg(XMMTYPE_GPRREG, XMMGPR_HI, MODE_WRITE);
		if (xmmhi >= 0)
		{
			recBeginOaknutEmit();
			oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(hival));
			oakAsm->INS(oakQRegister(xmmhi).Delem()[static_cast<u8>(Upper)], OAK_XSCRATCH);
			recEndOaknutEmit();
		}
		else
		{
			const int gprhi = Upper ? -1 : (hilive ? _allocX86reg(X86TYPE_GPR, XMMGPR_HI, MODE_WRITE) : _checkX86reg(X86TYPE_GPR, XMMGPR_HI, MODE_WRITE));
			if (gprhi >= 0) {
				recBeginOaknutEmit();
				oakAsm->MOV(oakXRegister(gprhi), static_cast<u64>(hival));
				recEndOaknutEmit();
            }
			else {
				recBeginOaknutEmit();
				oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(hival));
				oakStore64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.HI.UD[Upper]))});
				recEndOaknutEmit();
            }
		}
	}
	}

	// writeback lo to Rd if present
	if constexpr (WriteD)
	{
		if (_Rd_)
			g_cpuConstRegs[_Rd_].UD[0] = static_cast<u64>(loval);

		if (_Rd_ && EEINST_LIVETEST(_Rd_))
		{
			_eeOnWriteReg(_Rd_, 0);

			const int regd = _checkX86reg(X86TYPE_GPR, _Rd_, MODE_WRITE);
			if (regd >= 0) {
				recBeginOaknutEmit();
				oakAsm->MOV(oakXRegister(regd), static_cast<u64>(loval));
				recEndOaknutEmit();
			}
			else {
				recBeginOaknutEmit();
				oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(loval));
				oakStore64(OAK_XSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rd_].UD[0]))});
				recEndOaknutEmit();
			}
		}
	}
}

//// MULT
static void recMULT_const()
{
	s64 res = (s64)g_cpuConstRegs[_Rs_].SL[0] * (s64)g_cpuConstRegs[_Rt_].SL[0];

	recWritebackConstHILOExact<true, false>(res);
}

template <bool Signed, bool Upper, int Process>
static void recMULTExact(int info)
{
	// TODO(Stenzek): Use MULX where available.
	recBeginOaknutEmit();

	if constexpr ((Process & PROCESS_CONSTS) != 0)
	{
		oakAsm->MOV(oak::util::W0, g_cpuConstRegs[_Rs_].UL[0]);
		if (info & PROCESS_EE_T)
		{
			if constexpr (Signed)
				oakAsm->SMULL(oak::util::X0, oak::util::W0, oakWRegister(EEREC_T));
			else
				oakAsm->UMULL(oak::util::X0, oak::util::W0, oakWRegister(EEREC_T));
		}
		else
		{
			oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UL[0]))});
			if constexpr (Signed)
				oakAsm->SMULL(oak::util::X0, oak::util::W0, oak::util::W4);
			else
				oakAsm->UMULL(oak::util::X0, oak::util::W0, oak::util::W4);
		}
	}
	else if constexpr ((Process & PROCESS_CONSTT) != 0)
	{
		oakAsm->MOV(oak::util::W0, g_cpuConstRegs[_Rt_].UL[0]);
		if (info & PROCESS_EE_S)
		{
			if constexpr (Signed)
				oakAsm->SMULL(oak::util::X0, oak::util::W0, oakWRegister(EEREC_S));
			else
				oakAsm->UMULL(oak::util::X0, oak::util::W0, oakWRegister(EEREC_S));
		}
		else
		{
			oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UL[0]))});
			if constexpr (Signed)
				oakAsm->SMULL(oak::util::X0, oak::util::W0, oak::util::W4);
			else
				oakAsm->UMULL(oak::util::X0, oak::util::W0, oak::util::W4);
		}
	}
	else
	{
		// S is more likely to be in a register than T (so put T in eax).
		if (info & PROCESS_EE_T)
			oakAsm->MOV(oak::util::W0, oakWRegister(EEREC_T));
		else
			oakLoad32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UL[0]))});

		if (info & PROCESS_EE_S)
		{
			if constexpr (Signed)
				oakAsm->SMULL(oak::util::X0, oak::util::W0, oakWRegister(EEREC_S));
			else
				oakAsm->UMULL(oak::util::X0, oak::util::W0, oakWRegister(EEREC_S));
		}
		else
		{
			oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UL[0]))});
			if constexpr (Signed)
				oakAsm->SMULL(oak::util::X0, oak::util::W0, oak::util::W4);
			else
				oakAsm->UMULL(oak::util::X0, oak::util::W0, oak::util::W4);
		}
	}
	oakAsm->LSR(oak::util::X2, oak::util::X0, 32);

	recEndOaknutEmit();

	recWritebackHILOExact<true, Upper>(info);
}

static void recMULT_emit_oaknut(int info)
{
	recMULTExact<true, false, 0>(info);
}

static void recMULT_(int info)
{
	recMULT_emit_oaknut(info);
}

static void recMULT_consts_emit_oaknut(int info)
{
	recMULTExact<true, false, PROCESS_CONSTS>(info);
}

static void recMULT_consts(int info)
{
	recMULT_consts_emit_oaknut(info);
}

static void recMULT_constt_emit_oaknut(int info)
{
	recMULTExact<true, false, PROCESS_CONSTT>(info);
}

static void recMULT_constt(int info)
{
	recMULT_constt_emit_oaknut(info);
}

// lo/hi allocation are taken care of in recWritebackHILO().
EERECOMPILE_CODERC0(MULT, XMMINFO_READS | XMMINFO_READT | (_Rd_ ? XMMINFO_WRITED : 0));

//// MULTU
static void recMULTU_const()
{
	const u64 res = (u64)g_cpuConstRegs[_Rs_].UL[0] * (u64)g_cpuConstRegs[_Rt_].UL[0];

	recWritebackConstHILOExact<true, false>(res);
}

static void recMULTU_emit_oaknut(int info)
{
	recMULTExact<false, false, 0>(info);
}

static void recMULTU_(int info)
{
	recMULTU_emit_oaknut(info);
}

static void recMULTU_consts_emit_oaknut(int info)
{
	recMULTExact<false, false, PROCESS_CONSTS>(info);
}

static void recMULTU_consts(int info)
{
	recMULTU_consts_emit_oaknut(info);
}

static void recMULTU_constt_emit_oaknut(int info)
{
	recMULTExact<false, false, PROCESS_CONSTT>(info);
}

static void recMULTU_constt(int info)
{
	recMULTU_constt_emit_oaknut(info);
}

// don't specify XMMINFO_WRITELO or XMMINFO_WRITEHI, that is taken care of
EERECOMPILE_CODERC0(MULTU, XMMINFO_READS | XMMINFO_READT | (_Rd_ ? XMMINFO_WRITED : 0));

////////////////////////////////////////////////////
static void recMULT1_const()
{
	s64 res = (s64)g_cpuConstRegs[_Rs_].SL[0] * (s64)g_cpuConstRegs[_Rt_].SL[0];

	recWritebackConstHILOExact<true, true>((u64)res);
}

static void recMULT1_emit_oaknut(int info)
{
	recMULTExact<true, true, 0>(info);
}

static void recMULT1_(int info)
{
	recMULT1_emit_oaknut(info);
}

static void recMULT1_consts_emit_oaknut(int info)
{
	recMULTExact<true, true, PROCESS_CONSTS>(info);
}

static void recMULT1_consts(int info)
{
	recMULT1_consts_emit_oaknut(info);
}

static void recMULT1_constt_emit_oaknut(int info)
{
	recMULTExact<true, true, PROCESS_CONSTT>(info);
}

static void recMULT1_constt(int info)
{
	recMULT1_constt_emit_oaknut(info);
}

EERECOMPILE_CODERC0(MULT1, XMMINFO_READS | XMMINFO_READT | (_Rd_ ? XMMINFO_WRITED : 0));

////////////////////////////////////////////////////
static void recMULTU1_const()
{
	u64 res = (u64)g_cpuConstRegs[_Rs_].UL[0] * (u64)g_cpuConstRegs[_Rt_].UL[0];

	recWritebackConstHILOExact<true, true>(res);
}

static void recMULTU1_emit_oaknut(int info)
{
	recMULTExact<false, true, 0>(info);
}

static void recMULTU1_(int info)
{
	recMULTU1_emit_oaknut(info);
}

static void recMULTU1_consts_emit_oaknut(int info)
{
	recMULTExact<false, true, PROCESS_CONSTS>(info);
}

static void recMULTU1_consts(int info)
{
	recMULTU1_consts_emit_oaknut(info);
}

static void recMULTU1_constt_emit_oaknut(int info)
{
	recMULTExact<false, true, PROCESS_CONSTT>(info);
}

static void recMULTU1_constt(int info)
{
	recMULTU1_constt_emit_oaknut(info);
}

EERECOMPILE_CODERC0(MULTU1, XMMINFO_READS | XMMINFO_READT | (_Rd_ ? XMMINFO_WRITED : 0));

//// DIV

template <bool Upper>
static void recDIVConstExact()
{
	s32 quot, rem;
	if (g_cpuConstRegs[_Rs_].UL[0] == 0x80000000 && g_cpuConstRegs[_Rt_].SL[0] == -1)
	{
		quot = (s32)0x80000000;
		rem = 0;
	}
	else if (g_cpuConstRegs[_Rt_].SL[0] != 0)
	{
		quot = g_cpuConstRegs[_Rs_].SL[0] / g_cpuConstRegs[_Rt_].SL[0];
		rem = g_cpuConstRegs[_Rs_].SL[0] % g_cpuConstRegs[_Rt_].SL[0];
	}
	else
	{
		quot = (g_cpuConstRegs[_Rs_].SL[0] < 0) ? 1 : -1;
		rem = g_cpuConstRegs[_Rs_].SL[0];
	}
	recWritebackConstHILOExact<false, Upper>((u64)quot | ((u64)rem << 32));
}

static void recDIV_const()
{
	recDIVConstExact<false>();
}

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

template <bool Signed, bool Upper, int Process>
static void recDIVExact(int info)
{
	recBeginOaknutEmit();

	if constexpr ((Process & PROCESS_CONSTT) != 0)
	{
		oakAsm->MOV(oak::util::W1, g_cpuConstRegs[_Rt_].UL[0]);
	}
	else if (info & PROCESS_EE_T)
	{
		oakAsm->MOV(oak::util::W1, oakWRegister(EEREC_T));
	}
	else
	{
		oakLoad32(oak::util::W1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UL[0]))});
	}

	if constexpr ((Process & PROCESS_CONSTS) != 0)
		oakAsm->MOV(oak::util::W0, g_cpuConstRegs[_Rs_].UL[0]);
	else
		recMoveGPRtoOakW(oak::util::W0, _Rs_);

	oak::Label end1;
	if constexpr (Signed) //test for overflow (host CPU behavior differs here)
	{
		oak::Label not_overflow;
		oakAsm->MOV(oak::util::W4, 0x80000000u);
		oakAsm->CMP(oak::util::W0, oak::util::W4);
		oakAsm->B(oak::Cond::NE, not_overflow);
		oakAsm->MOV(oak::util::W4, 0xffffffffu);
		oakAsm->CMP(oak::util::W1, oak::util::W4);
		oakAsm->B(oak::Cond::NE, not_overflow);
		//overflow case:
		oakAsm->EOR(oak::util::W2, oak::util::W2, oak::util::W2); // W0 remains 0x80000000.
		oakAsm->B(end1);

		oakAsm->l(not_overflow);
	}

	oak::Label cont3;
	oakAsm->CBNZ(oak::util::W1, cont3);
	//divide by zero
	oakAsm->MOV(oak::util::W2, oak::util::W0);
	if constexpr (Signed) //set EAX to (EAX < 0)?1:-1
	{
		oakAsm->ASR(oak::util::W0, oak::util::W0, 31); //(EAX < 0)?-1:0
		oakAsm->LSL(oak::util::W0, oak::util::W0, 1); //(EAX < 0)?-2:0
		oakAsm->MVN(oak::util::W0, oak::util::W0); //(EAX < 0)?1:-1
	}
	else
	{
		oakAsm->MOV(oak::util::W0, 0xffffffffu);
	}
	oak::Label end2;
	oakAsm->B(end2);

	oakAsm->l(cont3);

	oakAsm->MOV(oak::util::W4, oak::util::W0);
	if constexpr (Signed)
	{
		oakAsm->SDIV(oak::util::W0, oak::util::W4, oak::util::W1);
	}
	else
	{
		oakAsm->UDIV(oak::util::W0, oak::util::W4, oak::util::W1);
	}
	oakAsm->MSUB(oak::util::W2, oak::util::W0, oak::util::W1, oak::util::W4);

	if constexpr (Signed)
		oakAsm->l(end1);
	oakAsm->l(end2);

	recEndOaknutEmit();

	// need to execute regardless of bad divide
	recWritebackHILOExact<false, Upper>(info);
}

static void recDIV_emit_oaknut(int info)
{
	recDIVExact<true, false, 0>(info);
}

static void recDIV_(int info)
{
	recDIV_emit_oaknut(info);
}

static void recDIV_consts_emit_oaknut(int info)
{
	recDIVExact<true, false, PROCESS_CONSTS>(info);
}

static void recDIV_consts(int info)
{
	recDIV_consts_emit_oaknut(info);
}

static void recDIV_constt_emit_oaknut(int info)
{
	recDIVExact<true, false, PROCESS_CONSTT>(info);
}

static void recDIV_constt(int info)
{
	recDIV_constt_emit_oaknut(info);
}

// We handle S reading in the routine itself, since it needs to go into eax.
EERECOMPILE_CODERC0(DIV, XMMINFO_READS | XMMINFO_READT);

//// DIVU
template <bool Upper>
static void recDIVUConstExact()
{
	u32 quot, rem;
	if (g_cpuConstRegs[_Rt_].UL[0] != 0)
	{
		quot = g_cpuConstRegs[_Rs_].UL[0] / g_cpuConstRegs[_Rt_].UL[0];
		rem = g_cpuConstRegs[_Rs_].UL[0] % g_cpuConstRegs[_Rt_].UL[0];
	}
	else
	{
		quot = 0xffffffff;
		rem = g_cpuConstRegs[_Rs_].UL[0];
	}

	recWritebackConstHILOExact<false, Upper>((u64)quot | ((u64)rem << 32));
}

static void recDIVU_const()
{
	recDIVUConstExact<false>();
}

static void recDIVU_emit_oaknut(int info)
{
	recDIVExact<false, false, 0>(info);
}

static void recDIVU_(int info)
{
	recDIVU_emit_oaknut(info);
}

static void recDIVU_consts_emit_oaknut(int info)
{
	recDIVExact<false, false, PROCESS_CONSTS>(info);
}

static void recDIVU_consts(int info)
{
	recDIVU_consts_emit_oaknut(info);
}

static void recDIVU_constt_emit_oaknut(int info)
{
	recDIVExact<false, false, PROCESS_CONSTT>(info);
}

static void recDIVU_constt(int info)
{
	recDIVU_constt_emit_oaknut(info);
}

EERECOMPILE_CODERC0(DIVU, XMMINFO_READS | XMMINFO_READT);

// Game compatibility guard: PSi OPS black-screens if DIV1 uses the generic
// recDIVExact<true, true>() path. Keep the conservative LO1/HI1 cache handling
// unless that game is specifically retested.
static void recDIV1_safe_emit_oaknut()
{
	_deleteEEreg(XMMGPR_LO, 1);
	_deleteEEreg(XMMGPR_HI, 1);

	recBeginOaknutEmit();

	recMoveGPRtoOakW(oak::util::W1, _Rt_);
	recMoveGPRtoOakW(oak::util::W0, _Rs_);

	oak::Label end1;
	oak::Label not_overflow;
	oakAsm->MOV(oak::util::W4, 0x80000000u);
	oakAsm->CMP(oak::util::W0, oak::util::W4);
	oakAsm->B(oak::Cond::NE, not_overflow);
	oakAsm->MOV(oak::util::W4, 0xffffffffu);
	oakAsm->CMP(oak::util::W1, oak::util::W4);
	oakAsm->B(oak::Cond::NE, not_overflow);
	oakAsm->MOV(oak::util::W2, 0);
	oakAsm->B(end1);

	oakAsm->l(not_overflow);

	oak::Label cont3;
	oakAsm->CBNZ(oak::util::W1, cont3);
	oakAsm->MOV(oak::util::W2, oak::util::W0);
	oakAsm->ASR(oak::util::W0, oak::util::W0, 31);
	oakAsm->LSL(oak::util::W0, oak::util::W0, 1);
	oakAsm->MVN(oak::util::W0, oak::util::W0);
	oak::Label end2;
	oakAsm->B(end2);

	oakAsm->l(cont3);

	oakAsm->MOV(oak::util::W4, oak::util::W0);
	oakAsm->SDIV(oak::util::W0, oak::util::W4, oak::util::W1);
	oakAsm->MSUB(oak::util::W2, oak::util::W0, oak::util::W1, oak::util::W4);

	oakAsm->l(end1);
	oakAsm->l(end2);

	oakAsm->SXTW(oak::util::X0, oak::util::W0);
	oakStore64(oak::util::X0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.LO.UD[1]))});
	oakAsm->SXTW(oak::util::X2, oak::util::W2);
	oakStore64(oak::util::X2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.HI.UD[1]))});

	recEndOaknutEmit();
}

void recDIV1(void)
{
	EE::Profiler.EmitOp(eeOpcode::DIV1);
	recDIV1_safe_emit_oaknut();
}

static void recDIVU1_const()
{
	recDIVUConstExact<true>();
}

static void recDIVU1_emit_oaknut(int info)
{
	recDIVExact<false, true, 0>(info);
}

static void recDIVU1_(int info)
{
	recDIVU1_emit_oaknut(info);
}

static void recDIVU1_consts_emit_oaknut(int info)
{
	recDIVExact<false, true, PROCESS_CONSTS>(info);
}

static void recDIVU1_consts(int info)
{
	recDIVU1_consts_emit_oaknut(info);
}

static void recDIVU1_constt_emit_oaknut(int info)
{
	recDIVExact<false, true, PROCESS_CONSTT>(info);
}

static void recDIVU1_constt(int info)
{
	recDIVU1_constt_emit_oaknut(info);
}

EERECOMPILE_CODERC0(DIVU1, XMMINFO_READS | XMMINFO_READT);

// TODO(Stenzek): All of these :(

template <int HiloID>
static void writeBackMAddToHiLoRd()
{
	// eax -> LO, edx -> HI
	if (_Rd_)
	{
		_eeOnWriteReg(_Rd_, 1);
		_deleteEEreg(_Rd_, 0);
	}

	recBeginOaknutEmit();

	oakAsm->SXTW(oak::util::X0, oak::util::W0);
	if (_Rd_)
		oakStore64(oak::util::X0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rd_].UD[0]))});
	oakStore64(oak::util::X0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.LO.UD[HiloID]))});

	oakAsm->SXTW(oak::util::X0, oak::util::W2);
	oakStore64(oak::util::X0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.HI.UD[HiloID]))});

	recEndOaknutEmit();
}

template <int HiloID>
static void addConstantAndWriteBackToHiLoRd(u64 constant)
{
	_deleteEEreg(XMMGPR_LO, 1);
	_deleteEEreg(XMMGPR_HI, 1);

	recBeginOaknutEmit();

	oakLoad32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.LO.UL[HiloID * 2]))});
	oakLoad32(oak::util::W2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.HI.UL[HiloID * 2]))});
	oakAsm->MOV(oak::util::W4, static_cast<u32>(constant));
	oakAsm->ADDS(oak::util::W0, oak::util::W0, oak::util::W4);
	oakAsm->MOV(oak::util::W4, static_cast<u32>(constant >> 32));
	oakAsm->ADC(oak::util::W2, oak::util::W2, oak::util::W4);

	recEndOaknutEmit();

	writeBackMAddToHiLoRd<HiloID>();
}

template <int HiloID>
static void addEaxEdxAndWriteBackToHiLoRd()
{
	recBeginOaknutEmit();

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.LO.UL[HiloID * 2]))});
	oakAsm->ADDS(oak::util::W0, oak::util::W0, oak::util::W4);
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.HI.UL[HiloID * 2]))});
	oakAsm->ADC(oak::util::W2, oak::util::W2, oak::util::W4);

	recEndOaknutEmit();

	writeBackMAddToHiLoRd<HiloID>();
}

template <bool Signed, int HiloID>
static void recMADDExact_emit_oaknut()
{
	if (GPR_IS_CONST2(_Rs_, _Rt_))
	{
		const u64 result = Signed ?
			static_cast<u64>(static_cast<s64>(g_cpuConstRegs[_Rs_].SL[0]) * static_cast<s64>(g_cpuConstRegs[_Rt_].SL[0])) :
			static_cast<u64>(g_cpuConstRegs[_Rs_].UL[0]) * static_cast<u64>(g_cpuConstRegs[_Rt_].UL[0]);
		addConstantAndWriteBackToHiLoRd<HiloID>(result);
		return;
	}

	_deleteEEreg(XMMGPR_LO, 1);
	_deleteEEreg(XMMGPR_HI, 1);

	recBeginOaknutEmit();

	recMoveGPRtoOakW(oak::util::W0, _Rs_);
	recMoveGPRtoOakW(oak::util::W4, _Rt_);

	if constexpr (Signed)
		oakAsm->SMULL(oak::util::X0, oak::util::W0, oak::util::W4);
	else
		oakAsm->UMULL(oak::util::X0, oak::util::W0, oak::util::W4);
	oakAsm->LSR(oak::util::X2, oak::util::X0, 32);

	recEndOaknutEmit();

	addEaxEdxAndWriteBackToHiLoRd<HiloID>();
}

static void recMADD_emit_oaknut()
{
	recMADDExact_emit_oaknut<true, 0>();
}

void recMADD()
{
	recMADD_emit_oaknut();
}

static void recMADDU_emit_oaknut()
{
	recMADDExact_emit_oaknut<false, 0>();
}

void recMADDU()
{
	recMADDU_emit_oaknut();
}

static void recMADD1_emit_oaknut()
{
	recMADDExact_emit_oaknut<true, 1>();
}

void recMADD1()
{
	recMADD1_emit_oaknut();
}

static void recMADDU1_emit_oaknut()
{
	recMADDExact_emit_oaknut<false, 1>();
}

void recMADDU1()
{
	recMADDU1_emit_oaknut();
}

#endif

} // namespace R5900::Dynarec::OpcodeImpl
