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
* Arithmetic with immediate operand                      *
* Format:  OP rt, rs, immediate                          *
*********************************************************/

#ifndef ARITHMETICIMM_RECOMPILE

namespace Interp = R5900::Interpreter::OpcodeImpl;

REC_FUNC_DEL(ADDI, _Rt_);
REC_FUNC_DEL(ADDIU, _Rt_);
REC_FUNC_DEL(DADDI, _Rt_);
REC_FUNC_DEL(DADDIU, _Rt_);
REC_FUNC_DEL(ANDI, _Rt_);
REC_FUNC_DEL(ORI, _Rt_);
REC_FUNC_DEL(XORI, _Rt_);

REC_FUNC_DEL(SLTI, _Rt_);
REC_FUNC_DEL(SLTIU, _Rt_);

#else

static void recImmediateMoveGPRtoOakW(oak::WReg to, int fromgpr)
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

static void recImmediateMoveGPRtoOakX(oak::XReg to, int fromgpr)
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

template <bool Is64Bit>
static void recSignedImmediate()
{
	EE::Profiler.EmitOp(Is64Bit ? eeOpcode::DADDI : eeOpcode::ADDI);
	const u64 rhs = static_cast<u64>(static_cast<s64>(_Imm_));

	if (GPR_IS_CONST1(_Rs_))
	{
		const u64 lhs = g_cpuConstRegs[_Rs_].UD[0];
		const u64 result = lhs + rhs;
		const bool overflow = Is64Bit ?
			(((~(lhs ^ rhs) & (lhs ^ result)) >> 63) != 0) :
			(((~(static_cast<u32>(lhs) ^ static_cast<u32>(rhs)) &
				(static_cast<u32>(lhs) ^ static_cast<u32>(result))) >> 31) != 0);
		if (overflow)
		{
			recEmitArithmeticOverflowException();
			return;
		}
		if (_Rt_ != 0)
		{
			_deleteGPRtoX86reg(_Rt_, DELETE_REG_FREE_NO_WRITEBACK);
			_deleteGPRtoXMMreg(_Rt_, DELETE_REG_FLUSH_AND_FREE);
			GPR_SET_CONST(_Rt_);
			g_cpuConstRegs[_Rt_].UD[0] = Is64Bit ? result :
				static_cast<u64>(static_cast<s64>(static_cast<s32>(result)));
		}
		return;
	}

	oak::Label no_overflow;
	recBeginOaknutEmit();
	if constexpr (Is64Bit)
	{
		recImmediateMoveGPRtoOakX(OAK_XSCRATCH, _Rs_);
		oakAsm->MOV(OAK_XSCRATCH2, rhs);
		oakAsm->ADDS(OAK_XSCRATCH, OAK_XSCRATCH, OAK_XSCRATCH2);
	}
	else
	{
		recImmediateMoveGPRtoOakW(OAK_WSCRATCH, _Rs_);
		oakAsm->MOV(OAK_WSCRATCH2, static_cast<u32>(rhs));
		oakAsm->ADDS(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	}
	oakAsm->B(oak::Cond::VC, no_overflow);
	recEndOaknutEmit();

	recEmitArithmeticOverflowException();

	recBeginOaknutEmit();
	oakAsm->l(no_overflow);
	recEndOaknutEmit();
	if (_Rt_ != 0)
	{
		_deleteEEreg(_Rt_, 0);
		const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
		recBeginOaknutEmit();
		if constexpr (Is64Bit)
			oakAsm->MOV(oakXRegister(regt), OAK_XSCRATCH);
		else
			oakAsm->SXTW(oakXRegister(regt), OAK_WSCRATCH);
		GPR_DEL_CONST(_Rt_);
		recEndOaknutEmit();
	}
}

//// ADDI
static void recADDI_const(void)
{
	g_cpuConstRegs[_Rt_].SD[0] = s64(s32(g_cpuConstRegs[_Rs_].UL[0] + u32(s32(_Imm_))));
}

static void recADDI_emit_oaknut(int info)
{
	using namespace oak::util;

	recBeginOaknutEmit();

	const oak::WReg regt_w = oakWRegister(EEREC_T);
	const oak::XReg regt_x = oakXRegister(EEREC_T);
	if (info & PROCESS_EE_S)
		oakAsm->MOV(regt_w, oakWRegister(EEREC_S));
	else
		oakLoad32(regt_w, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UL[0]))});

	oakAddSignedImm(regt_w, regt_w, static_cast<s32>(_Imm_), W16);
	oakAsm->SXTW(regt_x, regt_w);

	recEndOaknutEmit();
}

static void recADDI_(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));
	recADDI_emit_oaknut(info);
}

void recADDI() { recSignedImmediate<false>(); }

////////////////////////////////////////////////////
void recADDIU(void)
{
	EE::Profiler.EmitOp(eeOpcode::ADDIU);
	eeRecompileCodeRC1(recADDI_const, recADDI_, (XMMINFO_WRITET | XMMINFO_READS));
}

static void recDADDI_const()
{
	g_cpuConstRegs[_Rt_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] + u64(s64(_Imm_));
}

static void recDADDI_emit_oaknut(int info)
{
	using namespace oak::util;

	recBeginOaknutEmit();

	const oak::XReg regt_x = oakXRegister(EEREC_T);
	if (info & PROCESS_EE_S)
		oakAsm->MOV(regt_x, oakXRegister(EEREC_S));
	else
		oakLoad64(regt_x, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});

	oakAddSignedImm(regt_x, regt_x, static_cast<s64>(_Imm_), X16);

	recEndOaknutEmit();
}

static void recDADDI_(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));
	recDADDI_emit_oaknut(info);
}

void recDADDI() { recSignedImmediate<true>(); }

//// DADDIU
void recDADDIU(void)
{
	EE::Profiler.EmitOp(eeOpcode::DADDIU);
	eeRecompileCodeRC1(recDADDI_const, recDADDI_, (XMMINFO_WRITET | XMMINFO_READS | XMMINFO_64BITOP));
}

//// SLTIU
static void recSLTIU_const()
{
	g_cpuConstRegs[_Rt_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] < (u64)(_Imm_);
}

static void recSLTIU_emit_oaknut(int info, int regt)
{
	using namespace oak::util;

	recBeginOaknutEmit();

	const oak::XReg src = OAK_XSCRATCH2;
	if (info & PROCESS_EE_S)
		oakAsm->MOV(src, oakXRegister(EEREC_S));
	else
		oakLoad64(src, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});

	oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(static_cast<s64>(_Imm_)));
	oakAsm->CMP(src, OAK_XSCRATCH);
	oakAsm->CSET(oakWRegister(regt), CC);

	recEndOaknutEmit();
}

static void recSLTIU_(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));

	// TODO(Stenzek): this can be made to suck less by turning Rs into a temp and reallocating Rt.
	const int dreg = (_Rt_ == _Rs_) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_T;
	recSLTIU_emit_oaknut(info, dreg);

	if (dreg != EEREC_T)
	{
		std::swap(x86regs[dreg], x86regs[EEREC_T]);
		_freeX86reg(EEREC_T);
	}
}

EERECOMPILE_CODEX(eeRecompileCodeRC1, SLTIU, XMMINFO_WRITET | XMMINFO_READS | XMMINFO_64BITOP | XMMINFO_NORENAME);

//// SLTI
static void recSLTI_const()
{
	g_cpuConstRegs[_Rt_].UD[0] = g_cpuConstRegs[_Rs_].SD[0] < (s64)(_Imm_);
}

static void recSLTI_emit_oaknut(int info, int regt)
{
	using namespace oak::util;

	recBeginOaknutEmit();

	const oak::XReg src = OAK_XSCRATCH2;
	if (info & PROCESS_EE_S)
		oakAsm->MOV(src, oakXRegister(EEREC_S));
	else
		oakLoad64(src, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});

	oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(static_cast<s64>(_Imm_)));
	oakAsm->CMP(src, OAK_XSCRATCH);
	oakAsm->CSET(oakWRegister(regt), LT);

	recEndOaknutEmit();
}

static void recSLTI_(int info)
{
	const int dreg = (_Rt_ == _Rs_) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_T;
	recSLTI_emit_oaknut(info, dreg);

	if (dreg != EEREC_T)
	{
		std::swap(x86regs[dreg], x86regs[EEREC_T]);
		_freeX86reg(EEREC_T);
	}
}

EERECOMPILE_CODEX(eeRecompileCodeRC1, SLTI, XMMINFO_WRITET | XMMINFO_READS | XMMINFO_64BITOP | XMMINFO_NORENAME);

//// ANDI
static void recANDI_const()
{
	g_cpuConstRegs[_Rt_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] & (u64)_ImmU_; // Zero-extended Immediate
}

namespace
{
enum class LogicalImmOp
{
	AND,
	OR,
	XOR
};
} // namespace

static void emitLogicalImm_oaknut(LogicalImmOp op, oak::XReg dst, u64 imm)
{
	if (oak::detail::encode_bit_imm(imm))
	{
		switch (op)
		{
			case LogicalImmOp::AND:
				oakAsm->AND(dst, dst, oak::BitImm64(imm));
				break;
			case LogicalImmOp::OR:
				oakAsm->ORR(dst, dst, oak::BitImm64(imm));
				break;
			case LogicalImmOp::XOR:
				oakAsm->EOR(dst, dst, oak::BitImm64(imm));
				break;
		}
	}
	else
	{
		oakAsm->MOV(OAK_XSCRATCH, imm);
		switch (op)
		{
			case LogicalImmOp::AND:
				oakAsm->AND(dst, dst, OAK_XSCRATCH);
				break;
			case LogicalImmOp::OR:
				oakAsm->ORR(dst, dst, OAK_XSCRATCH);
				break;
			case LogicalImmOp::XOR:
				oakAsm->EOR(dst, dst, OAK_XSCRATCH);
				break;
		}
	}
}

static void recLogicalOpI_emit_oaknut(int info, LogicalImmOp op)
{
	using namespace oak::util;

	recBeginOaknutEmit();

	const oak::XReg regt_x = oakXRegister(EEREC_T);
	const u64 imm = static_cast<u64>(_ImmU_);
	if (op == LogicalImmOp::AND && imm == 0)
	{
		oakAsm->EOR(oakWRegister(EEREC_T), oakWRegister(EEREC_T), oakWRegister(EEREC_T));
		recEndOaknutEmit();
		return;
	}

	if (info & PROCESS_EE_S)
		oakAsm->MOV(regt_x, oakXRegister(EEREC_S));
	else
		oakLoad64(regt_x, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[_Rs_].UD[0]))});

	if (imm != 0)
		emitLogicalImm_oaknut(op, regt_x, imm);

	recEndOaknutEmit();
}

static void recANDI_(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));
	recLogicalOpI_emit_oaknut(info, LogicalImmOp::AND);
}

EERECOMPILE_CODEX(eeRecompileCodeRC1, ANDI, XMMINFO_WRITET | XMMINFO_READS | XMMINFO_64BITOP);

////////////////////////////////////////////////////
static void recORI_const()
{
	g_cpuConstRegs[_Rt_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] | (u64)_ImmU_; // Zero-extended Immediate
}

static void recORI_(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));
	recLogicalOpI_emit_oaknut(info, LogicalImmOp::OR);
}

EERECOMPILE_CODEX(eeRecompileCodeRC1, ORI, XMMINFO_WRITET | XMMINFO_READS | XMMINFO_64BITOP);

////////////////////////////////////////////////////
static void recXORI_const()
{
	g_cpuConstRegs[_Rt_].UD[0] = g_cpuConstRegs[_Rs_].UD[0] ^ (u64)_ImmU_; // Zero-extended Immediate
}

static void recXORI_(int info)
{
	pxAssert(!(info & PROCESS_EE_XMM));
	recLogicalOpI_emit_oaknut(info, LogicalImmOp::XOR);
}

EERECOMPILE_CODEX(eeRecompileCodeRC1, XORI, XMMINFO_WRITET | XMMINFO_READS | XMMINFO_64BITOP);

#endif

} // namespace R5900::Dynarec::OpcodeImpl
