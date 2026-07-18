// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/ee/iR5900-arm64.h"

#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

namespace R5900::Dynarec::OpcodeImpl
{
/*********************************************************
* Register arithmetic                                    *
* Format:  OP rd, rs, rt                                 *
*********************************************************/

#ifndef ARITHMETIC_RECOMPILE

namespace Interp = R5900::Interpreter::OpcodeImpl;

REC_FUNC_DEL(ADD, _Rd_);
REC_FUNC_DEL(ADDU, _Rd_);
REC_FUNC_DEL(DADD, _Rd_);
REC_FUNC_DEL(DADDU, _Rd_);
REC_FUNC_DEL(SUB, _Rd_);
REC_FUNC_DEL(SUBU, _Rd_);
REC_FUNC_DEL(DSUB, _Rd_);
REC_FUNC_DEL(DSUBU, _Rd_);
REC_FUNC_DEL(AND, _Rd_);
REC_FUNC_DEL(OR, _Rd_);
REC_FUNC_DEL(XOR, _Rd_);
REC_FUNC_DEL(NOR, _Rd_);
REC_FUNC_DEL(SLT, _Rd_);
REC_FUNC_DEL(SLTU, _Rd_);

#else

static void recArithmeticMoveGPRtoOakW(oak::WReg to, int fromgpr)
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
	if (const int reg = _checkX86reg(X86TYPE_GPR, fromgpr, MODE_READ); reg >= 0)
	{
		oakAsm->MOV(to, oakWRegister(reg));
		return;
	}
	if (const int reg = _checkXMMreg(XMMTYPE_GPRREG, fromgpr, MODE_READ); reg >= 0)
	{
		oakAsm->FMOV(to, oakSRegister(reg));
		return;
	}
	oakLoad32(to,
		{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[fromgpr].UL[0]))});
}

static void recArithmeticMoveGPRtoOakX(oak::XReg to, int fromgpr)
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
	if (const int reg = _checkX86reg(X86TYPE_GPR, fromgpr, MODE_READ); reg >= 0)
	{
		oakAsm->MOV(to, oakXRegister(reg));
		return;
	}
	if (const int reg = _checkXMMreg(XMMTYPE_GPRREG, fromgpr, MODE_READ); reg >= 0)
	{
		oakAsm->FMOV(to, oakDRegister(reg));
		return;
	}
	oakLoad64(to,
		{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[fromgpr].UD[0]))});
}

static bool recAddOverflows32(u32 lhs, u32 rhs, u32 result)
{
	return ((~(lhs ^ rhs) & (lhs ^ result)) >> 31) != 0;
}

static bool recAddOverflows64(u64 lhs, u64 rhs, u64 result)
{
	return ((~(lhs ^ rhs) & (lhs ^ result)) >> 63) != 0;
}

static bool recSubOverflows32(u32 lhs, u32 rhs, u32 result)
{
	return (((lhs ^ rhs) & (lhs ^ result)) >> 31) != 0;
}

static bool recSubOverflows64(u64 lhs, u64 rhs, u64 result)
{
	return (((lhs ^ rhs) & (lhs ^ result)) >> 63) != 0;
}

template <bool Is64Bit, bool IsSubtract>
static void recSignedArithmetic_emit_oaknut()
{
	oak::Label no_overflow;
	recBeginOaknutEmit();
	if constexpr (Is64Bit)
	{
		recArithmeticMoveGPRtoOakX(OAK_XSCRATCH, _Rs_);
		recArithmeticMoveGPRtoOakX(OAK_XSCRATCH2, _Rt_);
		if constexpr (IsSubtract)
			oakAsm->SUBS(OAK_XSCRATCH, OAK_XSCRATCH, OAK_XSCRATCH2);
		else
			oakAsm->ADDS(OAK_XSCRATCH, OAK_XSCRATCH, OAK_XSCRATCH2);
	}
	else
	{
		recArithmeticMoveGPRtoOakW(OAK_WSCRATCH, _Rs_);
		recArithmeticMoveGPRtoOakW(OAK_WSCRATCH2, _Rt_);
		if constexpr (IsSubtract)
			oakAsm->SUBS(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
		else
			oakAsm->ADDS(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	}
	oakAsm->B(oak::Cond::VC, no_overflow);
	recEndOaknutEmit();

	recEmitArithmeticOverflowException();

	recBeginOaknutEmit();
	oakAsm->l(no_overflow);
	recEndOaknutEmit();
	if (_Rd_ != 0)
	{
		_deleteEEreg(_Rd_, 0);
		const int regd = _allocX86reg(X86TYPE_GPR, _Rd_, MODE_WRITE);
		recBeginOaknutEmit();
		if constexpr (Is64Bit)
			oakAsm->MOV(oakXRegister(regd), OAK_XSCRATCH);
		else
				oakAsm->SXTW(oakXRegister(regd), OAK_WSCRATCH);
		GPR_DEL_CONST(_Rd_);
		recEndOaknutEmit();
	}
}

template <bool Is64Bit, bool IsSubtract>
static void recSignedArithmetic()
{
	EE::Profiler.EmitOp(Is64Bit ? (IsSubtract ? eeOpcode::DSUB : eeOpcode::DADD) :
		(IsSubtract ? eeOpcode::SUB : eeOpcode::ADD));

	if (GPR_IS_CONST2(_Rs_, _Rt_))
	{
		const u64 lhs = g_cpuConstRegs[_Rs_].UD[0];
		const u64 rhs = g_cpuConstRegs[_Rt_].UD[0];
		const u64 result = IsSubtract ? lhs - rhs : lhs + rhs;
		const bool overflow = Is64Bit ?
			(IsSubtract ? recSubOverflows64(lhs, rhs, result) : recAddOverflows64(lhs, rhs, result)) :
			(IsSubtract ? recSubOverflows32(static_cast<u32>(lhs), static_cast<u32>(rhs), static_cast<u32>(result)) :
				recAddOverflows32(static_cast<u32>(lhs), static_cast<u32>(rhs), static_cast<u32>(result)));
		if (overflow)
		{
			recEmitArithmeticOverflowException();
			return;
		}
		if (_Rd_ != 0)
		{
			_deleteGPRtoX86reg(_Rd_, DELETE_REG_FREE_NO_WRITEBACK);
			_deleteGPRtoXMMreg(_Rd_, DELETE_REG_FLUSH_AND_FREE);
			GPR_SET_CONST(_Rd_);
			g_cpuConstRegs[_Rd_].UD[0] = Is64Bit ? result :
				static_cast<u64>(static_cast<s64>(static_cast<s32>(result)));
		}
		return;
	}

	recSignedArithmetic_emit_oaknut<Is64Bit, IsSubtract>();
}

void recADD() { recSignedArithmetic<false, false>(); }
void recDADD() { recSignedArithmetic<true, false>(); }
void recSUB() { recSignedArithmetic<false, true>(); }
void recDSUB() { recSignedArithmetic<true, true>(); }

//// ADD
static void recADD_const()
{
	g_cpuConstRegs[_Rd_].SD[0] = s64(s32(g_cpuConstRegs[_Rs_].UL[0] + g_cpuConstRegs[_Rt_].UL[0]));
}

// s is constant
static void recADD_consts_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const s32 cval = g_cpuConstRegs[_Rs_].SL[0];
	recBeginOaknutEmit();

	const oak::WReg regd_w = oakWRegister(EEREC_D);
	if (info & PROCESS_EE_T)
		oakAsm->MOV(regd_w, oakWRegister(EEREC_T));
	else
		oakLoad32(regd_w, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UL[0]))});

	if (cval != 0)
		oakAddSignedImm(regd_w, regd_w, cval, OAK_WSCRATCH);
	oakAsm->SXTW(oakXRegister(EEREC_D), regd_w);

	recEndOaknutEmit();
}

static void recADD_consts(int info)
{
	recADD_consts_emit_oaknut(info);
}

// t is constant
static void recADD_constt_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const s32 cval = g_cpuConstRegs[_Rt_].SL[0];
	recBeginOaknutEmit();

	const oak::WReg regd_w = oakWRegister(EEREC_D);
	if (info & PROCESS_EE_S)
		oakAsm->MOV(regd_w, oakWRegister(EEREC_S));
	else
		oakLoad32(regd_w, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UL[0]))});

	if (cval != 0)
		oakAddSignedImm(regd_w, regd_w, cval, OAK_WSCRATCH);
	oakAsm->SXTW(oakXRegister(EEREC_D), regd_w);

	recEndOaknutEmit();
}

static void recADD_constt(int info)
{
	recADD_constt_emit_oaknut(info);
}

// nothing is constant
static void recADD_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	u32 rs = _Rs_, rt = _Rt_;
	int regs = (info & PROCESS_EE_S) ? EEREC_S : -1;
	int regt = (info & PROCESS_EE_T) ? EEREC_T : -1;
	if (_Rd_ == _Rt_)
	{
		std::swap(rs, rt);
		std::swap(regs, regt);
	}

	recBeginOaknutEmit();

	const oak::WReg regd_w = oakWRegister(EEREC_D);
	if (regs >= 0)
		oakAsm->MOV(regd_w, oakWRegister(regs));
	else
		oakLoad32(regd_w, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UL[0]) + rs * sizeof(GPR_reg))});

	if (regt >= 0)
		oakAsm->ADD(regd_w, regd_w, oakWRegister(regt));
	else
	{
		oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UL[0]) + rt * sizeof(GPR_reg))});
		oakAsm->ADD(regd_w, regd_w, OAK_WSCRATCH2);
	}
	oakAsm->SXTW(oakXRegister(EEREC_D), regd_w);

	recEndOaknutEmit();
}

static void recADD_(int info)
{
	recADD_emit_oaknut(info);
}

//// ADDU
void recADDU(void)
{
	EE::Profiler.EmitOp(eeOpcode::ADDU);
	eeRecompileCodeRC0(recADD_const, recADD_consts, recADD_constt, recADD_, (XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT));
}

//// DADD
void recDADD_const(void)
{
	g_cpuConstRegs[_Rd_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] + g_cpuConstRegs[_Rt_].UD[0];
}

// s is constant
static void recDADD_consts_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const s64 cval = g_cpuConstRegs[_Rs_].SD[0];
	recBeginOaknutEmit();

	const oak::XReg regd_x = oakXRegister(EEREC_D);
	if (info & PROCESS_EE_T)
		oakAsm->MOV(regd_x, oakXRegister(EEREC_T));
	else
		oakLoad64(regd_x, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UD[0]))});

	if (cval != 0)
		oakAddSignedImm(regd_x, regd_x, cval, OAK_XSCRATCH);

	recEndOaknutEmit();
}

static void recDADD_consts(int info)
{
	recDADD_consts_emit_oaknut(info);
}

// t is constant
static void recDADD_constt_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const s64 cval = g_cpuConstRegs[_Rt_].SD[0];
	recBeginOaknutEmit();

	const oak::XReg regd_x = oakXRegister(EEREC_D);
	if (info & PROCESS_EE_S)
		oakAsm->MOV(regd_x, oakXRegister(EEREC_S));
	else
		oakLoad64(regd_x, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});

	if (cval != 0)
		oakAddSignedImm(regd_x, regd_x, cval, OAK_XSCRATCH);

	recEndOaknutEmit();
}

static void recDADD_constt(int info)
{
	recDADD_constt_emit_oaknut(info);
}

// nothing is constant
static void recDADD_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	u32 rs = _Rs_, rt = _Rt_;
	int regs = (info & PROCESS_EE_S) ? EEREC_S : -1;
	int regt = (info & PROCESS_EE_T) ? EEREC_T : -1;
	if (_Rd_ == _Rt_)
	{
		std::swap(rs, rt);
		std::swap(regs, regt);
	}

	recBeginOaknutEmit();

	const oak::XReg regd_x = oakXRegister(EEREC_D);
	if (regs >= 0)
		oakAsm->MOV(regd_x, oakXRegister(regs));
	else
		oakLoad64(regd_x, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UD[0]) + rs * sizeof(GPR_reg))});

	if (regt >= 0)
		oakAsm->ADD(regd_x, regd_x, oakXRegister(regt));
	else
	{
		oakLoad64(OAK_XSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UD[0]) + rt * sizeof(GPR_reg))});
		oakAsm->ADD(regd_x, regd_x, OAK_XSCRATCH2);
	}

	recEndOaknutEmit();
}

static void recDADD_(int info)
{
	recDADD_emit_oaknut(info);
}

//// DADDU
void recDADDU(void)
{
	EE::Profiler.EmitOp(eeOpcode::DADDU);
	eeRecompileCodeRC0(recDADD_const, recDADD_consts, recDADD_constt, recDADD_, (XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT | XMMINFO_64BITOP));
}

//// SUB

static void recSUB_const()
{
	g_cpuConstRegs[_Rd_].SD[0] = s64(s32(g_cpuConstRegs[_Rs_].UL[0] - g_cpuConstRegs[_Rt_].UL[0]));
}

static void recSUB_consts_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const s32 sval = g_cpuConstRegs[_Rs_].SL[0];
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(sval));
	if (info & PROCESS_EE_T)
		oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, oakWRegister(EEREC_T));
	else
	{
		oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].SL[0]))});
		oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	}
	oakAsm->SXTW(oakXRegister(EEREC_D), OAK_WSCRATCH);

	recEndOaknutEmit();
}

static void recSUB_consts(int info)
{
	recSUB_consts_emit_oaknut(info);
}

static void recSUB_constt_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const s32 tval = g_cpuConstRegs[_Rt_].SL[0];
	recBeginOaknutEmit();

	const oak::WReg regd_w = oakWRegister(EEREC_D);
	if (info & PROCESS_EE_S)
		oakAsm->MOV(regd_w, oakWRegister(EEREC_S));
	else
		oakLoad32(regd_w, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UL[0]))});

	if (tval != 0)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(tval));
		oakAsm->SUB(regd_w, regd_w, OAK_WSCRATCH);
	}
	oakAsm->SXTW(oakXRegister(EEREC_D), regd_w);

	recEndOaknutEmit();
}

static void recSUB_constt(int info)
{
	recSUB_constt_emit_oaknut(info);
}

static void recSUB_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	recBeginOaknutEmit();

	if (_Rs_ == _Rt_)
	{
		oakAsm->EOR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), oakWRegister(EEREC_D));
		recEndOaknutEmit();
		return;
	}

	const bool t_in_host = (info & PROCESS_EE_T) != 0;
	const bool d_aliases_t = t_in_host && EEREC_D == EEREC_T;
	const oak::WReg result_w = d_aliases_t ? OAK_WSCRATCH : oakWRegister(EEREC_D);

	if (info & PROCESS_EE_S)
		oakAsm->MOV(result_w, oakWRegister(EEREC_S));
	else
		oakLoad32(result_w, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UL[0]))});

	if (t_in_host)
		oakAsm->SUB(result_w, result_w, oakWRegister(EEREC_T));
	else
	{
		oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UL[0]))});
		oakAsm->SUB(result_w, result_w, OAK_WSCRATCH2);
	}
	oakAsm->SXTW(oakXRegister(EEREC_D), result_w);

	recEndOaknutEmit();
}

static void recSUB_(int info)
{
	recSUB_emit_oaknut(info);
}

//// SUBU
void recSUBU(void)
{
	EE::Profiler.EmitOp(eeOpcode::SUBU);
	eeRecompileCodeRC0(recSUB_const, recSUB_consts, recSUB_constt, recSUB_, (XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED));
}

//// DSUB
static void recDSUB_const()
{
	g_cpuConstRegs[_Rd_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] - g_cpuConstRegs[_Rt_].UD[0];
}

static void recDSUB_consts_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const s64 sval = g_cpuConstRegs[_Rs_].SD[0];
	recBeginOaknutEmit();

	const bool t_in_host = (info & PROCESS_EE_T) != 0;
	const bool d_aliases_t = t_in_host && EEREC_D == EEREC_T;
	const oak::XReg result_x = d_aliases_t ? OAK_XSCRATCH : oakXRegister(EEREC_D);

	oakAsm->MOV(result_x, static_cast<u64>(sval));
	if (t_in_host)
		oakAsm->SUB(result_x, result_x, oakXRegister(EEREC_T));
	else
	{
		oakLoad64(OAK_XSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].SD[0]))});
		oakAsm->SUB(result_x, result_x, OAK_XSCRATCH2);
	}

	if (d_aliases_t)
		oakAsm->MOV(oakXRegister(EEREC_D), result_x);

	recEndOaknutEmit();
}

static void recDSUB_consts(int info)
{
	recDSUB_consts_emit_oaknut(info);
}

static void recDSUB_constt_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const s64 tval = g_cpuConstRegs[_Rt_].SD[0];
	recBeginOaknutEmit();

	const oak::XReg regd_x = oakXRegister(EEREC_D);
	if (info & PROCESS_EE_S)
		oakAsm->MOV(regd_x, oakXRegister(EEREC_S));
	else
		oakLoad64(regd_x, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});

	if (tval != 0)
	{
		oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(tval));
		oakAsm->SUB(regd_x, regd_x, OAK_XSCRATCH);
	}

	recEndOaknutEmit();
}

static void recDSUB_constt(int info)
{
	recDSUB_constt_emit_oaknut(info);
}

static void recDSUB_emit_oaknut(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	recBeginOaknutEmit();

	if (_Rs_ == _Rt_)
	{
		oakAsm->EOR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), oakWRegister(EEREC_D));
		recEndOaknutEmit();
		return;
	}

	const bool t_in_host = (info & PROCESS_EE_T) != 0;
	const bool d_aliases_t = t_in_host && EEREC_D == EEREC_T;
	const oak::XReg result_x = d_aliases_t ? OAK_XSCRATCH : oakXRegister(EEREC_D);

	if (info & PROCESS_EE_S)
		oakAsm->MOV(result_x, oakXRegister(EEREC_S));
	else
		oakLoad64(result_x, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});

	if (t_in_host)
		oakAsm->SUB(result_x, result_x, oakXRegister(EEREC_T));
	else
	{
		oakLoad64(OAK_XSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UD[0]))});
		oakAsm->SUB(result_x, result_x, OAK_XSCRATCH2);
	}

	if (d_aliases_t)
		oakAsm->MOV(oakXRegister(EEREC_D), result_x);

	recEndOaknutEmit();
}

static void recDSUB_(int info)
{
	recDSUB_emit_oaknut(info);
}

//// DSUBU
void recDSUBU(void)
{
	EE::Profiler.EmitOp(eeOpcode::DSUBU);
	eeRecompileCodeRC0(recDSUB_const, recDSUB_consts, recDSUB_constt, recDSUB_, (XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED | XMMINFO_64BITOP));
}

//// AND
static void recAND_const()
{
	g_cpuConstRegs[_Rd_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] & g_cpuConstRegs[_Rt_].UD[0];
}

namespace
{
enum class LogicalOp
{
	AND,
	OR,
	XOR,
	NOR
};
} // namespace

static void emitLogicalRegImm_oaknut(LogicalOp op, oak::XReg dst, u64 imm)
{
	const LogicalOp emit_op = (op == LogicalOp::NOR) ? LogicalOp::OR : op;
	if (oak::detail::encode_bit_imm(imm))
	{
		switch (emit_op)
		{
			case LogicalOp::AND:
				oakAsm->AND(dst, dst, oak::BitImm64(imm));
				break;
			case LogicalOp::OR:
				oakAsm->ORR(dst, dst, oak::BitImm64(imm));
				break;
			case LogicalOp::XOR:
				oakAsm->EOR(dst, dst, oak::BitImm64(imm));
				break;
			case LogicalOp::NOR:
				break;
		}
	}
	else
	{
		oakAsm->MOV(OAK_XSCRATCH, imm);
		switch (emit_op)
		{
			case LogicalOp::AND:
				oakAsm->AND(dst, dst, OAK_XSCRATCH);
				break;
			case LogicalOp::OR:
				oakAsm->ORR(dst, dst, OAK_XSCRATCH);
				break;
			case LogicalOp::XOR:
				oakAsm->EOR(dst, dst, OAK_XSCRATCH);
				break;
			case LogicalOp::NOR:
				break;
		}
	}
}

static void emitLogicalRegReg_oaknut(LogicalOp op, oak::XReg dst, oak::XReg rhs)
{
	switch (op)
	{
		case LogicalOp::AND:
			oakAsm->AND(dst, dst, rhs);
			break;
		case LogicalOp::OR:
		case LogicalOp::NOR:
			oakAsm->ORR(dst, dst, rhs);
			break;
		case LogicalOp::XOR:
			oakAsm->EOR(dst, dst, rhs);
			break;
	}
}

static void recLogicalOp_constv_emit_oaknut(LogicalOp op, int info, int creg, u32 vreg, int regv)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	s64 fixed_input = 0;
	s64 fixed_output = 0;
	s64 identity_input = 0;
	bool has_fixed = true;
	switch (op)
	{
		case LogicalOp::AND:
			fixed_input = 0;
			fixed_output = 0;
			identity_input = -1;
			break;
		case LogicalOp::OR:
			fixed_input = -1;
			fixed_output = -1;
			identity_input = 0;
			break;
		case LogicalOp::XOR:
			has_fixed = false;
			identity_input = 0;
			break;
		case LogicalOp::NOR:
			fixed_input = -1;
			fixed_output = 0;
			identity_input = 0;
			break;
	}

	const GPR_reg64 cval = g_cpuConstRegs[creg];
	recBeginOaknutEmit();

	if (has_fixed && cval.SD[0] == fixed_input)
	{
		oakAsm->MOV(oakXRegister(EEREC_D), static_cast<u64>(fixed_output));
		recEndOaknutEmit();
		return;
	}

	const oak::XReg regd_x = oakXRegister(EEREC_D);
	if (regv >= 0)
		oakAsm->MOV(regd_x, oakXRegister(regv));
	else
		oakLoad64(regd_x, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UD[0]) + vreg * sizeof(GPR_reg))});

	if (cval.SD[0] != identity_input)
		emitLogicalRegImm_oaknut(op, regd_x, cval.UD[0]);
	if (op == LogicalOp::NOR)
		oakAsm->MVN(regd_x, regd_x);

	recEndOaknutEmit();
}

static void recLogicalOp_emit_oaknut(LogicalOp op, int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	u32 rs = _Rs_, rt = _Rt_;
	int regs = (info & PROCESS_EE_S) ? EEREC_S : -1;
	int regt = (info & PROCESS_EE_T) ? EEREC_T : -1;
	if (_Rd_ == _Rt_)
	{
		std::swap(rs, rt);
		std::swap(regs, regt);
	}

	if (op == LogicalOp::XOR && rs == rt)
	{
		recBeginOaknutEmit();
		oakAsm->EOR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), oakWRegister(EEREC_D));
		recEndOaknutEmit();
		return;
	}

	recBeginOaknutEmit();

	const oak::XReg regd_x = oakXRegister(EEREC_D);
	if (regs >= 0)
		oakAsm->MOV(regd_x, oakXRegister(regs));
	else
		oakLoad64(regd_x, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UD[0]) + rs * sizeof(GPR_reg))});

	if (regt >= 0)
		emitLogicalRegReg_oaknut(op, regd_x, oakXRegister(regt));
	else
	{
		oakLoad64(OAK_XSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UD[0]) + rt * sizeof(GPR_reg))});
		emitLogicalRegReg_oaknut(op, regd_x, OAK_XSCRATCH2);
	}
	if (op == LogicalOp::NOR)
		oakAsm->MVN(regd_x, regd_x);

	recEndOaknutEmit();
}

static void recAND_consts(int info)
{
	recLogicalOp_constv_emit_oaknut(LogicalOp::AND, info, _Rs_, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
}

static void recAND_constt(int info)
{
	recLogicalOp_constv_emit_oaknut(LogicalOp::AND, info, _Rt_, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
}

static void recAND_(int info)
{
	recLogicalOp_emit_oaknut(LogicalOp::AND, info);
}

EERECOMPILE_CODERC0(AND, XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED | XMMINFO_64BITOP);

//// OR
static void recOR_const()
{
	g_cpuConstRegs[_Rd_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] | g_cpuConstRegs[_Rt_].UD[0];
}

static void recOR_consts(int info)
{
	recLogicalOp_constv_emit_oaknut(LogicalOp::OR, info, _Rs_, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
}

static void recOR_constt(int info)
{
	recLogicalOp_constv_emit_oaknut(LogicalOp::OR, info, _Rt_, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
}

static void recOR_(int info)
{
	recLogicalOp_emit_oaknut(LogicalOp::OR, info);
}

EERECOMPILE_CODERC0(OR, XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED | XMMINFO_64BITOP);

//// XOR
static void recXOR_const()
{
	g_cpuConstRegs[_Rd_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] ^ g_cpuConstRegs[_Rt_].UD[0];
}

static void recXOR_consts(int info)
{
	recLogicalOp_constv_emit_oaknut(LogicalOp::XOR, info, _Rs_, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
}

static void recXOR_constt(int info)
{
	recLogicalOp_constv_emit_oaknut(LogicalOp::XOR, info, _Rt_, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
}

static void recXOR_(int info)
{
	recLogicalOp_emit_oaknut(LogicalOp::XOR, info);
}

EERECOMPILE_CODERC0(XOR, XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED | XMMINFO_64BITOP);

//// NOR
static void recNOR_const()
{
	g_cpuConstRegs[_Rd_].UD[0] = ~(g_cpuConstRegs[_Rs_].UD[0] | g_cpuConstRegs[_Rt_].UD[0]);
}

static void recNOR_consts(int info)
{
	recLogicalOp_constv_emit_oaknut(LogicalOp::NOR, info, _Rs_, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
}

static void recNOR_constt(int info)
{
	recLogicalOp_constv_emit_oaknut(LogicalOp::NOR, info, _Rt_, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
}

static void recNOR_(int info)
{
	recLogicalOp_emit_oaknut(LogicalOp::NOR, info);
}

EERECOMPILE_CODERC0(NOR, XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED | XMMINFO_64BITOP);

//// SLT - test with silent hill, lemans
static void recSLT_const()
{
	g_cpuConstRegs[_Rd_].UD[0] = g_cpuConstRegs[_Rs_].SD[0] < g_cpuConstRegs[_Rt_].SD[0];
}

static void recSLT_constv_emit_oaknut(int info, bool is_signed, bool const_is_t)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const u32 other_gpr = const_is_t ? _Rs_ : _Rt_;
	const int other_host = const_is_t ? ((info & PROCESS_EE_S) ? EEREC_S : -1) : ((info & PROCESS_EE_T) ? EEREC_T : -1);
	const s64 cval = g_cpuConstRegs[const_is_t ? _Rt_ : _Rs_].SD[0];
	const int dreg = (_Rd_ == other_gpr) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_D;
	const oak::Cond cond = const_is_t ? (is_signed ? oak::Cond::LT : oak::Cond::CC) : (is_signed ? oak::Cond::GT : oak::Cond::HI);

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(cval));
	if (other_host >= 0)
		oakAsm->CMP(oakXRegister(other_host), OAK_XSCRATCH);
	else
	{
		oakLoad64(OAK_XSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UD[0]) + other_gpr * sizeof(GPR_reg))});
		oakAsm->CMP(OAK_XSCRATCH2, OAK_XSCRATCH);
	}
	oakAsm->CSET(oakWRegister(dreg), cond);
	recEndOaknutEmit();

	if (dreg != EEREC_D)
	{
		std::swap(x86regs[dreg], x86regs[EEREC_D]);
		_freeX86reg(EEREC_D);
	}
}

static void recSLT_emit_oaknut(int info, bool is_signed)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	const int dreg = (_Rd_ == _Rt_ || _Rd_ == _Rs_) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_D;
	const int regs = (info & PROCESS_EE_S) ? EEREC_S : _allocX86reg(X86TYPE_GPR, _Rs_, MODE_READ);
	const oak::Cond cond = is_signed ? oak::Cond::LT : oak::Cond::CC;

	recBeginOaknutEmit();
	if (info & PROCESS_EE_T)
		oakAsm->CMP(oakXRegister(regs), oakXRegister(EEREC_T));
	else
	{
		oakLoad64(OAK_XSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rt_].UD[0]))});
		oakAsm->CMP(oakXRegister(regs), OAK_XSCRATCH2);
	}
	oakAsm->CSET(oakWRegister(dreg), cond);
	recEndOaknutEmit();

	if (dreg != EEREC_D)
	{
		std::swap(x86regs[dreg], x86regs[EEREC_D]);
		_freeX86reg(EEREC_D);
	}
}

static void recSLT_consts(int info)
{
	recSLT_constv_emit_oaknut(info, true, false);
}

static void recSLT_constt(int info)
{
	recSLT_constv_emit_oaknut(info, true, true);
}

static void recSLT_(int info)
{
	recSLT_emit_oaknut(info, true);
}

EERECOMPILE_CODERC0(SLT, XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED | XMMINFO_NORENAME);

// SLTU - test with silent hill, lemans
static void recSLTU_const()
{
	g_cpuConstRegs[_Rd_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] < g_cpuConstRegs[_Rt_].UD[0];
}

static void recSLTU_consts(int info)
{
	recSLT_constv_emit_oaknut(info, false, false);
}

static void recSLTU_constt(int info)
{
	recSLT_constv_emit_oaknut(info, false, true);
}

static void recSLTU_(int info)
{
	recSLT_emit_oaknut(info, false);
}

EERECOMPILE_CODERC0(SLTU, XMMINFO_READS | XMMINFO_READT | XMMINFO_WRITED | XMMINFO_NORENAME);

#endif

} // namespace R5900::Dynarec::OpcodeImpl
