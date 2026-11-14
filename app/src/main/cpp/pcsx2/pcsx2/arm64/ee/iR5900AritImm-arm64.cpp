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

void recADDI(void)
{
	EE::Profiler.EmitOp(eeOpcode::ADDI);
	eeRecompileCodeRC1(recADDI_const, recADDI_, (XMMINFO_WRITET | XMMINFO_READS));
}

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

void recDADDI(void)
{
	EE::Profiler.EmitOp(eeOpcode::DADDI);
	eeRecompileCodeRC1(recDADDI_const, recDADDI_, (XMMINFO_WRITET | XMMINFO_READS | XMMINFO_64BITOP));
}

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
