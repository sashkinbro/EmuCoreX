// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "arm64/iop/iR3000A-arm64.h"
#include "IopMem.h"
#include "IopDma.h"
#include "IopGte.h"
#include "arm64/OaknutHelpers-arm64.h"

#include "common/Console.h"


#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

extern int g_psxWriteOk;
extern u32 g_psxMaxRecMem;
extern u8* recBeginOaknutEmit();
extern void recEndOaknutEmit();

static constexpr int IOP_HOST_W0 = 0;
static constexpr int IOP_HOST_W1 = 1;

// TODO(Stenzek): Operate directly on mem when destination register is not live.
// Do we want aligned targets? Seems wasteful...
#ifdef PCSX2_DEBUG
#define x86SetJ32A x86SetJ32
#endif

static int rpsxAllocRegIfUsed(int reg, int mode)
{
	if (EEINST_USEDTEST(reg))
		return _allocX86reg(X86TYPE_PSX, reg, mode);
	else
		return _checkX86reg(X86TYPE_PSX, reg, mode);
}

static void rpsxMoveStoT_emit_oaknut(int info)
{
	if (EEREC_T == EEREC_S)
		return;

	const oak::WReg regt = oakWRegister(EEREC_T);
	if (info & PROCESS_EE_S)
		oakAsm->MOV(regt, oakWRegister(EEREC_S));
	else
		oakLoad32(regt, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
}

static void rpsxMoveTtoD_emit_oaknut(int info)
{
	if (EEREC_D == EEREC_T)
		return;

	const oak::WReg regd = oakWRegister(EEREC_D);
	if (info & PROCESS_EE_T)
		oakAsm->MOV(regd, oakWRegister(EEREC_T));
	else
		oakLoad32(regd, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static void rpsxMoveGuestToOakW_emit_oaknut(oak::WReg dst, u32 guest, int cached_host)
{
	if (PSX_IS_CONST1(guest))
	{
		oakAsm->MOV(dst, g_psxConstRegs[guest]);
	}
	else if (cached_host >= 0)
	{
		const oak::WReg src = oakWRegister(cached_host);
		if (dst.index() != src.index())
			oakAsm->MOV(dst, src);
	}
	else
	{
		oakLoad32(dst, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[guest]))});
	}
}

static void rpsxStoreGuestToCpu32_emit_oaknut(OakMemOperand dst, u32 guest)
{
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, guest, _checkX86reg(X86TYPE_PSX, guest, MODE_READ));
	oakStore32(OAK_WSCRATCH, dst);
	recEndOaknutEmit();
}

static void rpsxCopyReg_emit_oaknut(int dest, int src)
{
	const int roldsrc = _checkX86reg(X86TYPE_PSX, src, MODE_READ);
	if (roldsrc >= 0 && psxTryRenameReg(dest, src, roldsrc, 0, 0) >= 0)
		return;

	const int rdest = rpsxAllocRegIfUsed(dest, MODE_WRITE);
	if (PSX_IS_CONST1(src))
	{
		if (dest < 32)
		{
			g_psxConstRegs[dest] = g_psxConstRegs[src];
			PSX_SET_CONST(dest);
		}
		else
		{
			recBeginOaknutEmit();
			if (rdest >= 0)
				oakAsm->MOV(oakWRegister(rdest), g_psxConstRegs[src]);
			else
			{
				oakAsm->MOV(OAK_WSCRATCH, g_psxConstRegs[src]);
				oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[dest]))});
			}
			recEndOaknutEmit();
		}

		return;
	}

	if (dest < 32)
		PSX_DEL_CONST(dest);

	const int rsrc = rpsxAllocRegIfUsed(src, MODE_READ);
	recBeginOaknutEmit();
	if (rsrc >= 0 && rdest >= 0)
		oakAsm->MOV(oakWRegister(rdest), oakWRegister(rsrc));
	else if (rdest >= 0)
		oakLoad32(oakWRegister(rdest), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[src]))});
	else if (rsrc >= 0)
		oakStore32(oakWRegister(rsrc), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[dest]))});
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[src]))});
		oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[dest]))});
	}
	recEndOaknutEmit();
}

////
static void rpsxADDIU_const()
{
	g_psxConstRegs[_Rt_] = g_psxConstRegs[_Rs_] + _Imm_;
}

static void rpsxADDIU_emit_direct(int info)
{
	// Rt = Rs + Im
	recBeginOaknutEmit();
	rpsxMoveStoT_emit_oaknut(info);
	if (_Imm_ > 0)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(_Imm_));
		oakAsm->ADD(oakWRegister(EEREC_T), oakWRegister(EEREC_T), OAK_WSCRATCH);
	}
	else if (_Imm_ < 0)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(-_Imm_));
		oakAsm->SUB(oakWRegister(EEREC_T), oakWRegister(EEREC_T), OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void rpsxADDIU_(int info)
{
	rpsxADDIU_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE1(ADDIU, XMMINFO_WRITET | XMMINFO_READS);

void rpsxADDI()
{
	rpsxADDIU();
}


//// SLTI
static void rpsxSLTI_const()
{
	g_psxConstRegs[_Rt_] = *(int*)&g_psxConstRegs[_Rs_] < _Imm_;
}

static void rpsxSLTI_emit_direct(int info)
{
	const int dreg_id = (_Rt_ == _Rs_) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_T;
	recBeginOaknutEmit();
	const oak::WReg dreg = oakWRegister(dreg_id);
	oakAsm->MOV(OAK_WSCRATCH2, static_cast<u32>(_Imm_));

	if (info & PROCESS_EE_S)
		oakAsm->CMP(oakWRegister(EEREC_S), OAK_WSCRATCH2);
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
		oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
	}

	oakAsm->CSET(dreg, oak::Cond::LT);
	recEndOaknutEmit();

	if (dreg_id != EEREC_T)
	{
		std::swap(x86regs[dreg_id], x86regs[EEREC_T]);
		_freeX86reg(EEREC_T);
	}
}

static void rpsxSLTI_(int info)
{
	rpsxSLTI_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE1(SLTI, XMMINFO_WRITET | XMMINFO_READS | XMMINFO_NORENAME);

//// SLTIU
static void rpsxSLTIU_const()
{
	g_psxConstRegs[_Rt_] = g_psxConstRegs[_Rs_] < (u32)_Imm_;
}

static void rpsxSLTIU_emit_direct(int info)
{
	const int dreg_id = (_Rt_ == _Rs_) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_T;
	recBeginOaknutEmit();
	const oak::WReg dreg = oakWRegister(dreg_id);
	oakAsm->MOV(OAK_WSCRATCH2, static_cast<u32>(_Imm_));

	if (info & PROCESS_EE_S)
		oakAsm->CMP(oakWRegister(EEREC_S), OAK_WSCRATCH2);
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
		oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
	}

	oakAsm->CSET(dreg, oak::Cond::CC);
	recEndOaknutEmit();

	if (dreg_id != EEREC_T)
	{
		std::swap(x86regs[dreg_id], x86regs[EEREC_T]);
		_freeX86reg(EEREC_T);
	}
}

static void rpsxSLTIU_(int info)
{
	rpsxSLTIU_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE1(SLTIU, XMMINFO_WRITET | XMMINFO_READS | XMMINFO_NORENAME);

static void rpsxANDI_const()
{
	g_psxConstRegs[_Rt_] = g_psxConstRegs[_Rs_] & _ImmU_;
}

static void rpsxANDI_emit_direct(int info)
{
	recBeginOaknutEmit();
	if (_ImmU_ != 0)
	{
		rpsxMoveStoT_emit_oaknut(info);
		oakAsm->MOV(OAK_WSCRATCH, _ImmU_);
		oakAsm->AND(oakWRegister(EEREC_T), oakWRegister(EEREC_T), OAK_WSCRATCH);
	}
	else
	{
		const oak::WReg regt = oakWRegister(EEREC_T);
		oakAsm->EOR(regt, regt, regt);
	}
	recEndOaknutEmit();
}

static void rpsxANDI_(int info)
{
	rpsxANDI_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE1(ANDI, XMMINFO_WRITET | XMMINFO_READS);

//// ORI
static void rpsxORI_const()
{
	g_psxConstRegs[_Rt_] = g_psxConstRegs[_Rs_] | _ImmU_;
}

static void rpsxORI_emit_direct(int info)
{
	recBeginOaknutEmit();
	if (_ImmU_ != 0)
	{
		rpsxMoveStoT_emit_oaknut(info);
		oakAsm->MOV(OAK_WSCRATCH, _ImmU_);
		oakAsm->ORR(oakWRegister(EEREC_T), oakWRegister(EEREC_T), OAK_WSCRATCH);
	}
	else if (EEREC_T != EEREC_S)
	{
		rpsxMoveStoT_emit_oaknut(info);
	}
	recEndOaknutEmit();
}

static void rpsxORI_(int info)
{
	rpsxORI_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE1(ORI, XMMINFO_WRITET | XMMINFO_READS);

static void rpsxXORI_const()
{
	g_psxConstRegs[_Rt_] = g_psxConstRegs[_Rs_] ^ _ImmU_;
}

static void rpsxXORI_emit_direct(int info)
{
	recBeginOaknutEmit();
	if (_ImmU_ != 0)
	{
		rpsxMoveStoT_emit_oaknut(info);
		oakAsm->MOV(OAK_WSCRATCH, _ImmU_);
		oakAsm->EOR(oakWRegister(EEREC_T), oakWRegister(EEREC_T), OAK_WSCRATCH);
	}
	else if (EEREC_T != EEREC_S)
	{
		rpsxMoveStoT_emit_oaknut(info);
	}
	recEndOaknutEmit();
}

static void rpsxXORI_(int info)
{
	rpsxXORI_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE1(XORI, XMMINFO_WRITET | XMMINFO_READS);

void rpsxLUI()
{
	if (!_Rt_)
		return;
	_psxOnWriteReg(_Rt_);
	_psxDeleteReg(_Rt_, 0);
	PSX_SET_CONST(_Rt_);
	g_psxConstRegs[_Rt_] = psxRegs.code << 16;
}

static void rpsxADDU_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rs_] + g_psxConstRegs[_Rt_];
}

static void rpsxADDU_consts(int info)
{
	const s32 cval = static_cast<s32>(g_psxConstRegs[_Rs_]);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(oakWRegister(EEREC_D), _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
	if (cval != 0)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(cval));
		oakAsm->ADD(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void rpsxADDU_constt(int info)
{
	const s32 cval = static_cast<s32>(g_psxConstRegs[_Rt_]);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(oakWRegister(EEREC_D), _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	if (cval != 0)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(cval));
		oakAsm->ADD(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void rpsxADDU_emit_direct(int info)
{
	const oak::WReg dreg = oakWRegister(EEREC_D);
	recBeginOaknutEmit();

	if ((info & PROCESS_EE_S) && (info & PROCESS_EE_T))
	{
		if (EEREC_D == EEREC_S)
			oakAsm->ADD(dreg, dreg, oakWRegister(EEREC_T));
		else if (EEREC_D == EEREC_T)
			oakAsm->ADD(dreg, dreg, oakWRegister(EEREC_S));
		else
		{
			oakAsm->MOV(dreg, oakWRegister(EEREC_S));
			oakAsm->ADD(dreg, dreg, oakWRegister(EEREC_T));
		}
	}
	else if (info & PROCESS_EE_S)
	{
		rpsxMoveGuestToOakW_emit_oaknut(dreg, _Rs_, EEREC_S);
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
		oakAsm->ADD(dreg, dreg, OAK_WSCRATCH);
	}
	else if (info & PROCESS_EE_T)
	{
		if (EEREC_D == EEREC_T)
		{
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
			oakAsm->ADD(dreg, OAK_WSCRATCH, dreg);
		}
		else
		{
			oakLoad32(dreg, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
			oakAsm->ADD(dreg, dreg, oakWRegister(EEREC_T));
		}
	}
	else
	{
		oakLoad32(dreg, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
		oakAsm->ADD(dreg, dreg, OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

void rpsxADDU_(int info)
{
	rpsxADDU_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(ADDU, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

void rpsxADD() { rpsxADDU(); }

static void rpsxSUBU_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rs_] - g_psxConstRegs[_Rt_];
}

static void rpsxSUBU_consts(int info)
{
	// more complex because Rt can be Rd, and we're reversing the op
	const s32 sval = g_psxConstRegs[_Rs_];
	const int dreg_id = (_Rt_ == _Rd_) ? IOP_HOST_W0 : EEREC_D;
	const oak::WReg dreg = oakWRegister(dreg_id);
	recBeginOaknutEmit();
	oakAsm->MOV(dreg, static_cast<u32>(sval));

	if (info & PROCESS_EE_T)
		oakAsm->SUB(dreg, dreg, oakWRegister(EEREC_T));
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
		oakAsm->SUB(dreg, dreg, OAK_WSCRATCH);
	}

	if (dreg_id != EEREC_D)
		oakAsm->MOV(oakWRegister(EEREC_D), dreg);
	recEndOaknutEmit();
}

static void rpsxSUBU_constt(int info)
{
	const s32 tval = g_psxConstRegs[_Rt_];
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(oakWRegister(EEREC_D), _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	if (tval != 0)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(tval));
		oakAsm->SUB(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void rpsxSUBU_emit_direct(int info)
{
	const oak::WReg dreg = oakWRegister(EEREC_D);
	recBeginOaknutEmit();

	// Rd = Rs - Rt
	if (_Rs_ == _Rt_)
	{
		oakAsm->EOR(dreg, dreg, dreg);
		recEndOaknutEmit();
		return;
	}

	// a bit messier here because it's not commutative..
	if ((info & PROCESS_EE_S) && (info & PROCESS_EE_T))
	{
		if (EEREC_D == EEREC_S)
			oakAsm->SUB(dreg, dreg, oakWRegister(EEREC_T));
		else if (EEREC_D == EEREC_T)
		{
			oakAsm->MOV(OAK_WSCRATCH, oakWRegister(EEREC_S));
			oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, dreg);
			oakAsm->MOV(dreg, OAK_WSCRATCH);
		}
		else
		{
			oakAsm->MOV(dreg, oakWRegister(EEREC_S));
			oakAsm->SUB(dreg, dreg, oakWRegister(EEREC_T));
		}
	}
	else if (info & PROCESS_EE_S)
	{
		rpsxMoveGuestToOakW_emit_oaknut(dreg, _Rs_, EEREC_S);
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
		oakAsm->SUB(dreg, dreg, OAK_WSCRATCH);
	}
	else if (info & PROCESS_EE_T)
	{
		if (EEREC_D == EEREC_T)
		{
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
			oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, dreg);
			oakAsm->MOV(dreg, OAK_WSCRATCH);
		}
		else
		{
			oakLoad32(dreg, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
			oakAsm->SUB(dreg, dreg, oakWRegister(EEREC_T));
		}
	}
	else
	{
		oakLoad32(dreg, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
		oakAsm->SUB(dreg, dreg, OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void rpsxSUBU_(int info)
{
	rpsxSUBU_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(SUBU, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

void rpsxSUB() { rpsxSUBU(); }

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

static void rpsxLogicalOp_constv(LogicalOp op, int info, int creg, u32 vreg, int regv)
{
	const LogicalOp xOP = op == LogicalOp::NOR ? LogicalOp::OR : op;
	s32 fixedInput, fixedOutput, identityInput;
	bool hasFixed = true;
	switch (op)
	{
		case LogicalOp::AND:
			fixedInput = 0;
			fixedOutput = 0;
			identityInput = -1;
			break;
		case LogicalOp::OR:
			fixedInput = -1;
			fixedOutput = -1;
			identityInput = 0;
			break;
		case LogicalOp::XOR:
			hasFixed = false;
			identityInput = 0;
			break;
		case LogicalOp::NOR:
			fixedInput = -1;
			fixedOutput = 0;
			identityInput = 0;
			break;
		default:
			pxAssert(0);
	}

	const s32 cval = static_cast<s32>(g_psxConstRegs[creg]);

	const oak::WReg reg32 = oakWRegister(EEREC_D);
	recBeginOaknutEmit();
	if (hasFixed && cval == fixedInput)
	{
		oakAsm->MOV(reg32, static_cast<u32>(fixedOutput));
	}
	else
	{
		if (regv >= 0)
			oakAsm->MOV(reg32, oakWRegister(regv));
		else
			oakLoad32(reg32, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[vreg]))});

		if (cval != identityInput)
		{
			oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(cval));
			switch (xOP)
			{
				case LogicalOp::AND:
					oakAsm->AND(reg32, reg32, OAK_WSCRATCH);
					break;
				case LogicalOp::OR:
					oakAsm->ORR(reg32, reg32, OAK_WSCRATCH);
					break;
				case LogicalOp::XOR:
					oakAsm->EOR(reg32, reg32, OAK_WSCRATCH);
					break;
				default:
					pxAssert(0);
			}
		}
		if (op == LogicalOp::NOR)
			oakAsm->MVN(reg32, reg32);
	}
	recEndOaknutEmit();
}

static void rpsxAND_emit_direct(int info)
{
	u32 rs = _Rs_, rt = _Rt_;
	int regs = (info & PROCESS_EE_S) ? EEREC_S : -1;
	int regt = (info & PROCESS_EE_T) ? EEREC_T : -1;
	if (_Rd_ == _Rt_)
	{
		std::swap(rs, rt);
		std::swap(regs, regt);
	}

	const oak::WReg dreg = oakWRegister(EEREC_D);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(dreg, rs, regs);
	if (regt >= 0)
		oakAsm->AND(dreg, dreg, oakWRegister(regt));
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[rt]))});
		oakAsm->AND(dreg, dreg, OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void rpsxOR_emit_direct(int info)
{
	u32 rs = _Rs_, rt = _Rt_;
	int regs = (info & PROCESS_EE_S) ? EEREC_S : -1;
	int regt = (info & PROCESS_EE_T) ? EEREC_T : -1;
	if (_Rd_ == _Rt_)
	{
		std::swap(rs, rt);
		std::swap(regs, regt);
	}

	const oak::WReg dreg = oakWRegister(EEREC_D);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(dreg, rs, regs);
	if (regt >= 0)
		oakAsm->ORR(dreg, dreg, oakWRegister(regt));
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[rt]))});
		oakAsm->ORR(dreg, dreg, OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void rpsxXOR_emit_direct(int info)
{
	u32 rs = _Rs_, rt = _Rt_;
	int regs = (info & PROCESS_EE_S) ? EEREC_S : -1;
	int regt = (info & PROCESS_EE_T) ? EEREC_T : -1;
	if (_Rd_ == _Rt_)
	{
		std::swap(rs, rt);
		std::swap(regs, regt);
	}

	const oak::WReg dreg = oakWRegister(EEREC_D);
	recBeginOaknutEmit();
	if (rs == rt)
	{
		oakAsm->EOR(dreg, dreg, dreg);
		recEndOaknutEmit();
		return;
	}

	rpsxMoveGuestToOakW_emit_oaknut(dreg, rs, regs);
	if (regt >= 0)
		oakAsm->EOR(dreg, dreg, oakWRegister(regt));
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[rt]))});
		oakAsm->EOR(dreg, dreg, OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void rpsxNOR_emit_direct(int info)
{
	u32 rs = _Rs_, rt = _Rt_;
	int regs = (info & PROCESS_EE_S) ? EEREC_S : -1;
	int regt = (info & PROCESS_EE_T) ? EEREC_T : -1;
	if (_Rd_ == _Rt_)
	{
		std::swap(rs, rt);
		std::swap(regs, regt);
	}

	const oak::WReg dreg = oakWRegister(EEREC_D);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(dreg, rs, regs);
	if (regt >= 0)
		oakAsm->ORR(dreg, dreg, oakWRegister(regt));
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[rt]))});
		oakAsm->ORR(dreg, dreg, OAK_WSCRATCH);
	}
	oakAsm->MVN(dreg, dreg);
	recEndOaknutEmit();
}

static void rpsxAND_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rs_] & g_psxConstRegs[_Rt_];
}

static void rpsxAND_consts(int info)
{
	rpsxLogicalOp_constv(LogicalOp::AND, info, _Rs_, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
}

static void rpsxAND_constt(int info)
{
	rpsxLogicalOp_constv(LogicalOp::AND, info, _Rt_, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
}

static void rpsxAND_(int info)
{
	rpsxAND_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(AND, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

static void rpsxOR_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rs_] | g_psxConstRegs[_Rt_];
}

static void rpsxOR_consts(int info)
{
	rpsxLogicalOp_constv(LogicalOp::OR, info, _Rs_, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
}

static void rpsxOR_constt(int info)
{
	rpsxLogicalOp_constv(LogicalOp::OR, info, _Rt_, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
}

static void rpsxOR_(int info)
{
	rpsxOR_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(OR, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

//// XOR
static void rpsxXOR_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rs_] ^ g_psxConstRegs[_Rt_];
}

static void rpsxXOR_consts(int info)
{
	rpsxLogicalOp_constv(LogicalOp::XOR, info, _Rs_, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
}

static void rpsxXOR_constt(int info)
{
	rpsxLogicalOp_constv(LogicalOp::XOR, info, _Rt_, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
}

static void rpsxXOR_(int info)
{
	rpsxXOR_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(XOR, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

//// NOR
static void rpsxNOR_const()
{
	g_psxConstRegs[_Rd_] = ~(g_psxConstRegs[_Rs_] | g_psxConstRegs[_Rt_]);
}

static void rpsxNOR_consts(int info)
{
	rpsxLogicalOp_constv(LogicalOp::NOR, info, _Rs_, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);
}

static void rpsxNOR_constt(int info)
{
	rpsxLogicalOp_constv(LogicalOp::NOR, info, _Rt_, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
}

static void rpsxNOR_(int info)
{
	rpsxNOR_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(NOR, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT);

//// SLT
static void rpsxSLT_const()
{
	g_psxConstRegs[_Rd_] = *(int*)&g_psxConstRegs[_Rs_] < *(int*)&g_psxConstRegs[_Rt_];
}

static void rpsxSLTs_const(int info, int sign, int st)
{
	const s32 cval = g_psxConstRegs[st ? _Rt_ : _Rs_];
	const oak::Cond SET = st ? (sign ? oak::Cond::LT : oak::Cond::CC) : (sign ? oak::Cond::GT : oak::Cond::HI);

	const int dreg_id = (_Rd_ == (st ? _Rs_ : _Rt_)) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_D;
	const oak::WReg dreg = oakWRegister(dreg_id);
	const int regs = st ? ((info & PROCESS_EE_S) ? EEREC_S : -1) : ((info & PROCESS_EE_T) ? EEREC_T : -1);

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH2, static_cast<u32>(cval));
	if (regs >= 0)
		oakAsm->CMP(oakWRegister(regs), OAK_WSCRATCH2);
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[st ? _Rs_ : _Rt_]))});
		oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
	}
	oakAsm->CSET(dreg, SET);
	recEndOaknutEmit();

	if (dreg_id != EEREC_D)
	{
		std::swap(x86regs[dreg_id], x86regs[EEREC_D]);
		_freeX86reg(EEREC_D);
	}
}

static void rpsxSLT_consts(int info)
{
	rpsxSLTs_const(info, 1, 0);
}

static void rpsxSLT_constt(int info)
{
	rpsxSLTs_const(info, 1, 1);
}

static void rpsxSLT_emit_direct(int info)
{
	const int dreg_id = (_Rd_ == _Rt_ || _Rd_ == _Rs_) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_D;
	const int regs = (info & PROCESS_EE_S) ? EEREC_S : _allocX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	const oak::WReg dreg = oakWRegister(dreg_id);

	recBeginOaknutEmit();
	if (info & PROCESS_EE_T)
		oakAsm->CMP(oakWRegister(regs), oakWRegister(EEREC_T));
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
		oakAsm->CMP(oakWRegister(regs), OAK_WSCRATCH);
	}
	oakAsm->CSET(dreg, oak::Cond::LT);
	recEndOaknutEmit();

	if (dreg_id != EEREC_D)
	{
		std::swap(x86regs[dreg_id], x86regs[EEREC_D]);
		_freeX86reg(EEREC_D);
	}
}

static void rpsxSLT_(int info)
{
	rpsxSLT_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(SLT, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT | XMMINFO_NORENAME);

//// SLTU
static void rpsxSLTU_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rs_] < g_psxConstRegs[_Rt_];
}

static void rpsxSLTU_consts(int info)
{
	rpsxSLTs_const(info, 0, 0);
}

static void rpsxSLTU_constt(int info)
{
	rpsxSLTs_const(info, 0, 1);
}

static void rpsxSLTU_emit_direct(int info)
{
	const int dreg_id = (_Rd_ == _Rt_ || _Rd_ == _Rs_) ? _allocX86reg(X86TYPE_TEMP, 0, 0) : EEREC_D;
	const int regs = (info & PROCESS_EE_S) ? EEREC_S : _allocX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	const oak::WReg dreg = oakWRegister(dreg_id);

	recBeginOaknutEmit();
	if (info & PROCESS_EE_T)
		oakAsm->CMP(oakWRegister(regs), oakWRegister(EEREC_T));
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
		oakAsm->CMP(oakWRegister(regs), OAK_WSCRATCH);
	}
	oakAsm->CSET(dreg, oak::Cond::CC);
	recEndOaknutEmit();

	if (dreg_id != EEREC_D)
	{
		std::swap(x86regs[dreg_id], x86regs[EEREC_D]);
		_freeX86reg(EEREC_D);
	}
}

static void rpsxSLTU_(int info)
{
	rpsxSLTU_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(SLTU, XMMINFO_WRITED | XMMINFO_READS | XMMINFO_READT | XMMINFO_NORENAME);

//// MULT
static void rpsxMULT_const()
{
	_deletePSXtoX86reg(PSX_HI, DELETE_REG_FREE_NO_WRITEBACK);
	_deletePSXtoX86reg(PSX_LO, DELETE_REG_FREE_NO_WRITEBACK);

	u64 res = (s64)((s64) * (int*)&g_psxConstRegs[_Rs_] * (s64) * (int*)&g_psxConstRegs[_Rt_]);

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>((res >> 32) & 0xffffffff));
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.hi))});
	oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(res & 0xffffffff));
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.lo))});
	recEndOaknutEmit();
}

static void rpsxWritebackHILO_emit_oaknut(int info, oak::WReg lo, oak::WReg hi)
{
	if (EEINST_LIVETEST(PSX_LO))
	{
		if (info & PROCESS_EE_LO)
			oakAsm->MOV(oakWRegister(EEREC_LO), lo);
		else
			oakStore32(lo, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.lo))});
	}

	if (EEINST_LIVETEST(PSX_HI))
	{
		if (info & PROCESS_EE_HI)
			oakAsm->MOV(oakWRegister(EEREC_HI), hi);
		else
			oakStore32(hi, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.hi))});
	}
}

static void rpsxMULTsuperconst_emit_oaknut(int info, int sreg, int imm, bool sign)
{
	// Lo/Hi = Rs * Rt (signed)
	const int regs = rpsxAllocRegIfUsed(sreg, MODE_READ);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(imm));
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH2, sreg, regs);

	if (sign)
		oakAsm->SMULL(OAK_XSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	else
		oakAsm->UMULL(OAK_XSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);

	oakAsm->LSR(OAK_XSCRATCH2, OAK_XSCRATCH, 32);
	rpsxWritebackHILO_emit_oaknut(info, OAK_WSCRATCH, OAK_WSCRATCH2);
	recEndOaknutEmit();
}

static void rpsxMULTsuper_emit_oaknut(int info, bool sign)
{
	// Lo/Hi = Rs * Rt (signed)
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH2, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);

	if (sign)
		oakAsm->SMULL(OAK_XSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	else
		oakAsm->UMULL(OAK_XSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);

	oakAsm->LSR(OAK_XSCRATCH2, OAK_XSCRATCH, 32);
	rpsxWritebackHILO_emit_oaknut(info, OAK_WSCRATCH, OAK_WSCRATCH2);
	recEndOaknutEmit();
}

static void rpsxMULT_consts(int info)
{
	rpsxMULTsuperconst_emit_oaknut(info, _Rt_, g_psxConstRegs[_Rs_], true);
}

static void rpsxMULT_constt(int info)
{
	rpsxMULTsuperconst_emit_oaknut(info, _Rs_, g_psxConstRegs[_Rt_], true);
}

static void rpsxMULT_(int info)
{
	rpsxMULTsuper_emit_oaknut(info, true);
}

PSXRECOMPILE_CONSTCODE3_PENALTY(MULT, 1, psxInstCycles_Mult);

//// MULTU
static void rpsxMULTU_const()
{
	_deletePSXtoX86reg(PSX_HI, DELETE_REG_FREE_NO_WRITEBACK);
	_deletePSXtoX86reg(PSX_LO, DELETE_REG_FREE_NO_WRITEBACK);

	u64 res = (u64)((u64)g_psxConstRegs[_Rs_] * (u64)g_psxConstRegs[_Rt_]);

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>((res >> 32) & 0xffffffff));
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.hi))});
	oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(res & 0xffffffff));
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.lo))});
	recEndOaknutEmit();
}

static void rpsxMULTU_consts(int info)
{
	rpsxMULTsuperconst_emit_oaknut(info, _Rt_, g_psxConstRegs[_Rs_], false);
}

static void rpsxMULTU_constt(int info)
{
	rpsxMULTsuperconst_emit_oaknut(info, _Rs_, g_psxConstRegs[_Rt_], false);
}

static void rpsxMULTU_(int info)
{
	rpsxMULTsuper_emit_oaknut(info, false);
}

PSXRECOMPILE_CONSTCODE3_PENALTY(MULTU, 1, psxInstCycles_Mult);

//// DIV
static void rpsxDIV_const()
{
	_deletePSXtoX86reg(PSX_HI, DELETE_REG_FREE_NO_WRITEBACK);
	_deletePSXtoX86reg(PSX_LO, DELETE_REG_FREE_NO_WRITEBACK);

	u32 lo, hi;

	/*
	 * Normally, when 0x80000000(-2147483648), the signed minimum value, is divided by 0xFFFFFFFF(-1), the
	 * 	operation will result in overflow. However, in this instruction an overflow exception does not occur and the
	 * 	result will be as follows:
	 * 	Quotient: 0x80000000 (-2147483648), and remainder: 0x00000000 (0)
	 */
	// Of course host CPU does overflow !
	if (g_psxConstRegs[_Rs_] == 0x80000000u && g_psxConstRegs[_Rt_] == 0xFFFFFFFFu)
	{
		recBeginOaknutEmit();
		oakAsm->MOV(OAK_WSCRATCH, 0);
		oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.hi))});
		oakAsm->MOV(OAK_WSCRATCH, 0x80000000);
		oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.lo))});
		recEndOaknutEmit();
		return;
	}

	if (g_psxConstRegs[_Rt_] != 0)
	{
		lo = *(int*)&g_psxConstRegs[_Rs_] / *(int*)&g_psxConstRegs[_Rt_];
		hi = *(int*)&g_psxConstRegs[_Rs_] % *(int*)&g_psxConstRegs[_Rt_];
	}
	else
	{
		hi = g_psxConstRegs[_Rs_];
		if (g_psxConstRegs[_Rs_] & 0x80000000u)
			lo = 0x1;
		else
			lo = 0xFFFFFFFFu;
	}

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, hi);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.hi))});
	oakAsm->MOV(OAK_WSCRATCH, lo);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.lo))});
	recEndOaknutEmit();
}

static void rpsxDIVsuper_emit_oaknut(int info, bool sign, int process = 0)
{
	recBeginOaknutEmit();

	if (process & PROCESS_CONSTS)
		oakAsm->MOV(OAK_WSCRATCH, g_psxConstRegs[_Rs_]);
	else
		rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);

	if (process & PROCESS_CONSTT)
		oakAsm->MOV(OAK_WSCRATCH2, g_psxConstRegs[_Rt_]);
	else
		rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH2, _Rt_, (info & PROCESS_EE_T) ? EEREC_T : -1);

	oak::Label normal_division;
	oak::Label divide_by_zero;
	oak::Label done;
	const oak::WReg quotient = oak::util::W4;

	if (sign)
	{
		oak::Label not_overflow;
		oakAsm->MOV(quotient, 0x80000000);
		oakAsm->CMP(OAK_WSCRATCH, quotient);
		oakAsm->B(oak::Cond::NE, not_overflow);
		oakAsm->MOV(quotient, 0xffffffff);
		oakAsm->CMP(OAK_WSCRATCH2, quotient);
		oakAsm->B(oak::Cond::NE, not_overflow);
		oakAsm->MOV(OAK_WSCRATCH2, 0);
		oakAsm->MOV(quotient, 0x80000000);
		oakAsm->B(done);
		oakAsm->l(not_overflow);
	}

	oakAsm->CBNZ(OAK_WSCRATCH2, normal_division);

	oakAsm->l(divide_by_zero);
	oakAsm->MOV(OAK_WSCRATCH2, OAK_WSCRATCH);
	if (sign)
	{
		oakAsm->ASR(quotient, OAK_WSCRATCH, 31);
		oakAsm->LSL(quotient, quotient, 1);
		oakAsm->MVN(quotient, quotient);
	}
	else
	{
		oakAsm->MOV(quotient, 0xffffffff);
	}
	oakAsm->B(done);

	oakAsm->l(normal_division);
	if (sign)
		oakAsm->SDIV(quotient, OAK_WSCRATCH, OAK_WSCRATCH2);
	else
		oakAsm->UDIV(quotient, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->MSUB(OAK_WSCRATCH2, quotient, OAK_WSCRATCH2, OAK_WSCRATCH);

	oakAsm->l(done);
	rpsxWritebackHILO_emit_oaknut(info, quotient, OAK_WSCRATCH2);
	recEndOaknutEmit();
}

static void rpsxDIV_consts(int info)
{
	rpsxDIVsuper_emit_oaknut(info, true, PROCESS_CONSTS);
}

static void rpsxDIV_constt(int info)
{
	rpsxDIVsuper_emit_oaknut(info, true, PROCESS_CONSTT);
}

static void rpsxDIV_(int info)
{
	rpsxDIVsuper_emit_oaknut(info, true);
}

PSXRECOMPILE_CONSTCODE3_PENALTY(DIV, 1, psxInstCycles_Div);

//// DIVU
void rpsxDIVU_const()
{
	u32 lo, hi;

	_deletePSXtoX86reg(PSX_HI, DELETE_REG_FREE_NO_WRITEBACK);
	_deletePSXtoX86reg(PSX_LO, DELETE_REG_FREE_NO_WRITEBACK);

	if (g_psxConstRegs[_Rt_] != 0)
	{
		lo = g_psxConstRegs[_Rs_] / g_psxConstRegs[_Rt_];
		hi = g_psxConstRegs[_Rs_] % g_psxConstRegs[_Rt_];
	}
	else
	{
		hi = g_psxConstRegs[_Rs_];
		lo = 0xFFFFFFFFu;
	}

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, hi);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.hi))});
	oakAsm->MOV(OAK_WSCRATCH, lo);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.n.lo))});
	recEndOaknutEmit();
}

void rpsxDIVU_consts(int info)
{
	rpsxDIVsuper_emit_oaknut(info, false, PROCESS_CONSTS);
}

void rpsxDIVU_constt(int info)
{
	rpsxDIVsuper_emit_oaknut(info, false, PROCESS_CONSTT);
}

void rpsxDIVU_(int info)
{
	rpsxDIVsuper_emit_oaknut(info, false);
}

PSXRECOMPILE_CONSTCODE3_PENALTY(DIVU, 1, psxInstCycles_Div);

// TLB loadstore functions

static u8* rpsxGetConstantAddressOperand(bool store)
{
#if 0
	if (!PSX_IS_CONST1(_Rs_))
		return nullptr;

	const u32 addr = g_psxConstRegs[_Rs_];
	return store ? iopVirtMemW<u8>(addr) : const_cast<u8*>(iopVirtMemR<u8>(addr));
#else
	return nullptr;
#endif
}

static int rpsxPrepareAddressOperand_emit_oaknut()
{
	int rs;
	if (PSX_IS_CONST1(_Rs_))
		rs = _allocX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	else
		rs = _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ);

	_freeX86reg(IOP_HOST_W0);
	return rs;
}

static void rpsxCalcAddressOperand_emit_oaknut(int rs)
{
	if (rs >= 0)
		oakAsm->MOV(OAK_WARG1, oakWRegister(rs));
	else
		oakLoad32(OAK_WARG1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});

	if (_Imm_)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(_Imm_));
		oakAsm->ADD(OAK_WARG1, OAK_WARG1, OAK_WSCRATCH);
	}
}

static int rpsxPrepareStoreOperand_emit_oaknut()
{
	int rt;
	if (PSX_IS_CONST1(_Rt_))
		rt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
	else
		rt = _checkX86reg(X86TYPE_PSX, _Rt_, MODE_READ);

	_freeX86reg(IOP_HOST_W1);
	return rt;
}

static void rpsxCalcStoreOperand_emit_oaknut(int rt)
{
	if (rt >= 0)
		oakAsm->MOV(OAK_WARG2, oakWRegister(rt));
	else
		oakLoad32(OAK_WARG2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static int rpsxCaptureAddressOperand_emit_oaknut()
{
	const int temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int rs = rpsxPrepareAddressOperand_emit_oaknut();

	recBeginOaknutEmit();
	rpsxCalcAddressOperand_emit_oaknut(rs);
	oakAsm->MOV(oakWRegister(temp), OAK_WARG1);
	recEndOaknutEmit();

	return temp;
}

static int rpsxCaptureStoreOperand_emit_oaknut()
{
	const int temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int rt = rpsxPrepareStoreOperand_emit_oaknut();

	recBeginOaknutEmit();
	rpsxCalcStoreOperand_emit_oaknut(rt);
	oakAsm->MOV(oakWRegister(temp), OAK_WARG2);
	recEndOaknutEmit();

	return temp;
}

static int rpsxPrepareLoadTarget_emit_oaknut()
{
	int rt = -1;
	if (_Rt_ != 0)
	{
		PSX_DEL_CONST(_Rt_);
		_deletePSXtoX86reg(_Rt_, DELETE_REG_FREE_NO_WRITEBACK);
	}

	_psxFlushCall(FLUSH_FULLVTLB);

	if (_Rt_ != 0)
		rt = rpsxAllocRegIfUsed(_Rt_, MODE_WRITE);

	return rt;
}

static void rpsxFinishLoad8Signed_emit_oaknut(int rt)
{
	const oak::WReg dreg = (rt < 0) ? OAK_WARG1 : oakWRegister(rt);
	oakAsm->SXTB(dreg, OAK_WARG1);
	if (rt < 0)
		oakStore32(OAK_WARG1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static void rpsxFinishLoad8Unsigned_emit_oaknut(int rt)
{
	const oak::WReg dreg = (rt < 0) ? OAK_WARG1 : oakWRegister(rt);
	oakAsm->UXTB(dreg, OAK_WARG1);
	if (rt < 0)
		oakStore32(OAK_WARG1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static void rpsxFinishLoad16Signed_emit_oaknut(int rt)
{
	const oak::WReg dreg = (rt < 0) ? OAK_WARG1 : oakWRegister(rt);
	oakAsm->SXTH(dreg, OAK_WARG1);
	if (rt < 0)
		oakStore32(OAK_WARG1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static void rpsxFinishLoad16Unsigned_emit_oaknut(int rt)
{
	const oak::WReg dreg = (rt < 0) ? OAK_WARG1 : oakWRegister(rt);
	oakAsm->UXTH(dreg, OAK_WARG1);
	if (rt < 0)
		oakStore32(OAK_WARG1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static void rpsxFinishLoad32_emit_oaknut(int rt)
{
	const oak::WReg dreg = (rt < 0) ? OAK_WARG1 : oakWRegister(rt);
	if (dreg.index() != OAK_WARG1.index())
		oakAsm->MOV(dreg, OAK_WARG1);
	if (rt < 0)
		oakStore32(OAK_WARG1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static void rpsxLB_emit_oaknut()
{
	const int addr_temp = rpsxCaptureAddressOperand_emit_oaknut();
	const int rt = rpsxPrepareLoadTarget_emit_oaknut();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));

	oak::Label is_ram_read;
	oak::Label done;
	oakAsm->TBZ(OAK_WARG1, 28, is_ram_read);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead8));
	if (_Rt_ == 0)
	{
		oakAsm->l(is_ram_read);
		recEndOaknutEmit();
		_freeX86reg(addr_temp);
		return;
	}
	oakAsm->B(done);
	oakAsm->l(is_ram_read);
	oakAsm->AND(OAK_WARG1, OAK_WARG1, 0x1fffff);
	oakAsm->LDRB(OAK_WARG1, oak::util::X26, oak::util::X0);
	oakAsm->l(done);
	rpsxFinishLoad8Signed_emit_oaknut(rt);
	recEndOaknutEmit();
	_freeX86reg(addr_temp);
}

static void rpsxLBU_emit_oaknut()
{
	const int addr_temp = rpsxCaptureAddressOperand_emit_oaknut();
	const int rt = rpsxPrepareLoadTarget_emit_oaknut();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));

	oak::Label is_ram_read;
	oak::Label done;
	oakAsm->TBZ(OAK_WARG1, 28, is_ram_read);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead8));
	if (_Rt_ == 0)
	{
		oakAsm->l(is_ram_read);
		recEndOaknutEmit();
		_freeX86reg(addr_temp);
		return;
	}
	oakAsm->B(done);
	oakAsm->l(is_ram_read);
	oakAsm->AND(OAK_WARG1, OAK_WARG1, 0x1fffff);
	oakAsm->LDRB(OAK_WARG1, oak::util::X26, oak::util::X0);
	oakAsm->l(done);
	rpsxFinishLoad8Unsigned_emit_oaknut(rt);
	recEndOaknutEmit();
	_freeX86reg(addr_temp);
}

static void rpsxLH_emit_oaknut()
{
	const int addr_temp = rpsxCaptureAddressOperand_emit_oaknut();
	const int rt = rpsxPrepareLoadTarget_emit_oaknut();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));

	oak::Label is_ram_read;
	oak::Label done;
	oakAsm->TBZ(OAK_WARG1, 28, is_ram_read);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead16));
	if (_Rt_ == 0)
	{
		oakAsm->l(is_ram_read);
		recEndOaknutEmit();
		_freeX86reg(addr_temp);
		return;
	}
	oakAsm->B(done);
	oakAsm->l(is_ram_read);
	oakAsm->AND(OAK_WARG1, OAK_WARG1, 0x1fffff);
	oakAsm->LDRH(OAK_WARG1, oak::util::X26, oak::util::X0);
	oakAsm->l(done);
	rpsxFinishLoad16Signed_emit_oaknut(rt);
	recEndOaknutEmit();
	_freeX86reg(addr_temp);
}

static void rpsxLHU_emit_oaknut()
{
	const int addr_temp = rpsxCaptureAddressOperand_emit_oaknut();
	const int rt = rpsxPrepareLoadTarget_emit_oaknut();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));

	oak::Label is_ram_read;
	oak::Label done;
	oakAsm->TBZ(OAK_WARG1, 28, is_ram_read);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead16));
	if (_Rt_ == 0)
	{
		oakAsm->l(is_ram_read);
		recEndOaknutEmit();
		_freeX86reg(addr_temp);
		return;
	}
	oakAsm->B(done);
	oakAsm->l(is_ram_read);
	oakAsm->AND(OAK_WARG1, OAK_WARG1, 0x1fffff);
	oakAsm->LDRH(OAK_WARG1, oak::util::X26, oak::util::X0);
	oakAsm->l(done);
	rpsxFinishLoad16Unsigned_emit_oaknut(rt);
	recEndOaknutEmit();
	_freeX86reg(addr_temp);
}

static void rpsxLW_emit_oaknut()
{
	const int addr_temp = rpsxCaptureAddressOperand_emit_oaknut();
	const int rt = rpsxPrepareLoadTarget_emit_oaknut();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));

	oak::Label is_ram_read;
	oak::Label done;
	oakAsm->TBZ(OAK_WARG1, 28, is_ram_read);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead32));
	if (_Rt_ == 0)
	{
		oakAsm->l(is_ram_read);
		recEndOaknutEmit();
		_freeX86reg(addr_temp);
		return;
	}
	oakAsm->B(done);
	oakAsm->l(is_ram_read);
	oakAsm->AND(OAK_WARG1, OAK_WARG1, 0x1fffff);
	oakAsm->LDR(OAK_WARG1, oak::util::X26, oak::util::X0);
	oakAsm->l(done);
	rpsxFinishLoad32_emit_oaknut(rt);

	recEndOaknutEmit();
	_freeX86reg(addr_temp);
}

static void rpsxPrepareStoreCall_emit_oaknut(int& addr_temp, int& value_temp)
{
	addr_temp = rpsxCaptureAddressOperand_emit_oaknut();
	value_temp = rpsxCaptureStoreOperand_emit_oaknut();
	_psxFlushCall(FLUSH_FULLVTLB);
}

static void rpsxSB_emit_oaknut()
{
	int addr_temp;
	int value_temp;
	rpsxPrepareStoreCall_emit_oaknut(addr_temp, value_temp);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));
	oakAsm->MOV(OAK_WARG2, oakWRegister(value_temp));
	oakEmitCall(reinterpret_cast<void*>(iopMemWrite8));
	recEndOaknutEmit();
	_freeX86reg(value_temp);
	_freeX86reg(addr_temp);
}

static void rpsxSH_emit_oaknut()
{
	int addr_temp;
	int value_temp;
	rpsxPrepareStoreCall_emit_oaknut(addr_temp, value_temp);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));
	oakAsm->MOV(OAK_WARG2, oakWRegister(value_temp));
	oakEmitCall(reinterpret_cast<void*>(iopMemWrite16));
	recEndOaknutEmit();
	_freeX86reg(value_temp);
	_freeX86reg(addr_temp);
}

static void rpsxSW_emit_oaknut()
{
	int addr_temp;
	int value_temp;
	rpsxPrepareStoreCall_emit_oaknut(addr_temp, value_temp);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));
	oakAsm->MOV(OAK_WARG2, oakWRegister(value_temp));
	oakEmitCall(reinterpret_cast<void*>(iopMemWrite32));
	recEndOaknutEmit();
	_freeX86reg(value_temp);
	_freeX86reg(addr_temp);
}

static void rpsxSWConstantAddress_emit_oaknut(u8* ptr, int rt)
{
	const oak::XReg addr = (rt == OAK_XSCRATCH.index()) ? OAK_XSCRATCH2 : OAK_XSCRATCH;
	recBeginOaknutEmit();
	oakMoveAddressToReg(addr, ptr);
	oakAsm->STR(oakWRegister(rt), addr);
	recEndOaknutEmit();
}

static int rpsxPrepareUnalignedLoadSource_emit_oaknut()
{
	return _Rt_ ? _allocX86reg(X86TYPE_PSX, _Rt_, MODE_READ) : -1;
}

static void rpsxMoveOldRtToTemp_emit_oaknut(oak::WReg oldrt, int rt_src)
{
	if (!_Rt_)
		return;

	if (rt_src >= 0)
		oakAsm->MOV(oldrt, oakWRegister(rt_src));
	else
		oakLoad32(oldrt, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static int rpsxPrepareUnalignedLoadTarget_emit_oaknut()
{
	if (!_Rt_)
		return -1;

	PSX_DEL_CONST(_Rt_);
	_deletePSXtoX86reg(_Rt_, DELETE_REG_FREE_NO_WRITEBACK);
	return rpsxAllocRegIfUsed(_Rt_, MODE_WRITE);
}

static void rpsxWriteUnalignedLoadResult_emit_oaknut(int rt, oak::WReg result)
{
	if (!_Rt_)
		return;

	if (rt >= 0)
		oakAsm->MOV(oakWRegister(rt), result);
	else
		oakStore32(result, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
}

static void rpsxLWL_emit_oaknut()
{
	const int addr_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int shift_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int oldrt_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int rs = rpsxPrepareAddressOperand_emit_oaknut();
	const int rt_src = rpsxPrepareUnalignedLoadSource_emit_oaknut();

	recBeginOaknutEmit();
	const oak::WReg addr = oakWRegister(addr_temp);
	const oak::WReg shift = oakWRegister(shift_temp);
	const oak::WReg oldrt = oakWRegister(oldrt_temp);
	rpsxCalcAddressOperand_emit_oaknut(rs);
	oakAsm->MOV(shift, OAK_WARG1);
	oakAsm->AND(shift, shift, oak::BitImm32(3));
	oakAsm->LSL(shift, shift, 3);
	rpsxMoveOldRtToTemp_emit_oaknut(oldrt, rt_src);
	oakAsm->AND(addr, OAK_WARG1, oak::BitImm32(~3u));
	recEndOaknutEmit();

	_psxFlushCall(FLUSH_FULLVTLB);
	const int rt = rpsxPrepareUnalignedLoadTarget_emit_oaknut();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, addr);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead32));

	if (_Rt_)
	{
		oakAsm->MOV(OAK_WSCRATCH, 0x00ffffffu);
		oakAsm->LSR(OAK_WSCRATCH, OAK_WSCRATCH, shift);
		oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, oldrt);
		oakAsm->MOV(OAK_WSCRATCH2, 24);
		oakAsm->SUB(OAK_WSCRATCH2, OAK_WSCRATCH2, shift);
		oakAsm->LSL(OAK_WARG1, OAK_WARG1, OAK_WSCRATCH2);
		oakAsm->ORR(OAK_WARG1, OAK_WARG1, OAK_WSCRATCH);
		rpsxWriteUnalignedLoadResult_emit_oaknut(rt, OAK_WARG1);
	}

	recEndOaknutEmit();
	_freeX86reg(oldrt_temp);
	_freeX86reg(shift_temp);
	_freeX86reg(addr_temp);
}

static void rpsxLWR_emit_oaknut()
{
	const int addr_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int shift_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int oldrt_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	const int rs = rpsxPrepareAddressOperand_emit_oaknut();
	const int rt_src = rpsxPrepareUnalignedLoadSource_emit_oaknut();

	recBeginOaknutEmit();
	const oak::WReg addr = oakWRegister(addr_temp);
	const oak::WReg shift = oakWRegister(shift_temp);
	const oak::WReg oldrt = oakWRegister(oldrt_temp);
	rpsxCalcAddressOperand_emit_oaknut(rs);
	oakAsm->MOV(shift, OAK_WARG1);
	oakAsm->AND(shift, shift, oak::BitImm32(3));
	oakAsm->LSL(shift, shift, 3);
	rpsxMoveOldRtToTemp_emit_oaknut(oldrt, rt_src);
	oakAsm->AND(addr, OAK_WARG1, oak::BitImm32(~3u));
	recEndOaknutEmit();

	_psxFlushCall(FLUSH_FULLVTLB);
	const int rt = rpsxPrepareUnalignedLoadTarget_emit_oaknut();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, addr);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead32));

	if (_Rt_)
	{
		oakAsm->MOV(OAK_WSCRATCH2, 24);
		oakAsm->SUB(OAK_WSCRATCH2, OAK_WSCRATCH2, shift);
		oakAsm->MOV(OAK_WSCRATCH, 0xffffff00u);
		oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
		oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, oldrt);
		oakAsm->LSR(OAK_WARG1, OAK_WARG1, shift);
		oakAsm->ORR(OAK_WARG1, OAK_WARG1, OAK_WSCRATCH);
		rpsxWriteUnalignedLoadResult_emit_oaknut(rt, OAK_WARG1);
	}

	recEndOaknutEmit();
	_freeX86reg(oldrt_temp);
	_freeX86reg(shift_temp);
	_freeX86reg(addr_temp);
}

static void rpsxPrepareUnalignedStoreOperands_emit_oaknut(int& shift_temp, int& addr_temp, int& oldrt_temp)
{
	shift_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	addr_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);
	oldrt_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);

	const int rs = rpsxPrepareAddressOperand_emit_oaknut();
	const int rt_src = rpsxPrepareStoreOperand_emit_oaknut();

	recBeginOaknutEmit();
	const oak::WReg shift = oakWRegister(shift_temp);
	const oak::WReg addr = oakWRegister(addr_temp);
	const oak::WReg oldrt = oakWRegister(oldrt_temp);
	rpsxCalcAddressOperand_emit_oaknut(rs);
	oakAsm->MOV(shift, OAK_WARG1);
	oakAsm->AND(shift, shift, oak::BitImm32(3));
	oakAsm->LSL(shift, shift, 3);
	rpsxCalcStoreOperand_emit_oaknut(rt_src);
	oakAsm->MOV(oldrt, OAK_WARG2);
	oakAsm->AND(addr, OAK_WARG1, oak::BitImm32(~3u));
	recEndOaknutEmit();
}

static void rpsxSWL_emit_oaknut()
{
	int shift_temp;
	int addr_temp;
	int oldrt_temp;
	rpsxPrepareUnalignedStoreOperands_emit_oaknut(shift_temp, addr_temp, oldrt_temp);
	_psxFlushCall(FLUSH_FULLVTLB);

	recBeginOaknutEmit();
	const oak::WReg shift = oakWRegister(shift_temp);
	const oak::WReg addr = oakWRegister(addr_temp);
	const oak::WReg oldrt = oakWRegister(oldrt_temp);
	oakAsm->MOV(OAK_WARG1, addr);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead32));

	oakAsm->MOV(OAK_WSCRATCH, 0xffffff00u);
	oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, shift);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WARG1);
	oakAsm->MOV(OAK_WSCRATCH2, 24);
	oakAsm->SUB(OAK_WSCRATCH2, OAK_WSCRATCH2, shift);
	oakAsm->LSR(OAK_WARG2, oldrt, OAK_WSCRATCH2);
	oakAsm->ORR(OAK_WARG2, OAK_WARG2, OAK_WSCRATCH);
	oakAsm->MOV(OAK_WARG1, addr);
	oakEmitCall(reinterpret_cast<void*>(iopMemWrite32));

	recEndOaknutEmit();
	_freeX86reg(oldrt_temp);
	_freeX86reg(addr_temp);
	_freeX86reg(shift_temp);
}

static void rpsxSWR_emit_oaknut()
{
	int shift_temp;
	int addr_temp;
	int oldrt_temp;
	rpsxPrepareUnalignedStoreOperands_emit_oaknut(shift_temp, addr_temp, oldrt_temp);
	_psxFlushCall(FLUSH_FULLVTLB);

	recBeginOaknutEmit();
	const oak::WReg shift = oakWRegister(shift_temp);
	const oak::WReg addr = oakWRegister(addr_temp);
	const oak::WReg oldrt = oakWRegister(oldrt_temp);
	oakAsm->MOV(OAK_WARG1, addr);
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead32));

	oakAsm->MOV(OAK_WSCRATCH2, 24);
	oakAsm->SUB(OAK_WSCRATCH2, OAK_WSCRATCH2, shift);
	oakAsm->MOV(OAK_WSCRATCH, 0x00ffffffu);
	oakAsm->LSR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WARG1);
	oakAsm->LSL(OAK_WARG2, oldrt, shift);
	oakAsm->ORR(OAK_WARG2, OAK_WARG2, OAK_WSCRATCH);
	oakAsm->MOV(OAK_WARG1, addr);
	oakEmitCall(reinterpret_cast<void*>(iopMemWrite32));

	recEndOaknutEmit();
	_freeX86reg(oldrt_temp);
	_freeX86reg(addr_temp);
	_freeX86reg(shift_temp);
}

static void rpsxLWL()
{
	rpsxLWL_emit_oaknut();
}

static void rpsxLWR()
{
	rpsxLWR_emit_oaknut();
}

static void rpsxSWL()
{
	rpsxSWL_emit_oaknut();
}

static void rpsxSWR()
{
	rpsxSWR_emit_oaknut();
}

static void rpsxLB()
{
	rpsxLB_emit_oaknut();
}

static void rpsxLBU()
{
	rpsxLBU_emit_oaknut();
}

static void rpsxLH()
{
	rpsxLH_emit_oaknut();
}

static void rpsxLHU()
{
	rpsxLHU_emit_oaknut();
}

static void rpsxLW()
{
	rpsxLW_emit_oaknut();
}

static void rpsxSB()
{
	rpsxSB_emit_oaknut();
}

static void rpsxSH()
{
	rpsxSH_emit_oaknut();
}

static void rpsxSW()
{
	u8* ptr = rpsxGetConstantAddressOperand(true);
	if (ptr)
	{
		const int rt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
		rpsxSWConstantAddress_emit_oaknut(ptr, rt);
		return;
	}

	rpsxSW_emit_oaknut();
}

//// SLL
static void rpsxSLL_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rt_] << _Sa_;
}

static void rpsxSLLs_(int info, int sa)
{
	recBeginOaknutEmit();
	rpsxMoveTtoD_emit_oaknut(info);
	if (sa != 0)
		oakAsm->LSL(oakWRegister(EEREC_D), oakWRegister(EEREC_D), sa);
	recEndOaknutEmit();
}

static void rpsxSLL_emit_direct(int info)
{
	rpsxSLLs_(info, _Sa_);
}

static void rpsxSLL_(int info)
{
	rpsxSLL_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE2(SLL, XMMINFO_WRITED | XMMINFO_READS);

//// SRL
static void rpsxSRL_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rt_] >> _Sa_;
}

static void rpsxSRLs_(int info, int sa)
{
	recBeginOaknutEmit();
	rpsxMoveTtoD_emit_oaknut(info);
	if (sa != 0)
		oakAsm->LSR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), sa);
	recEndOaknutEmit();
}

static void rpsxSRL_emit_direct(int info)
{
	rpsxSRLs_(info, _Sa_);
}

static void rpsxSRL_(int info)
{
	rpsxSRL_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE2(SRL, XMMINFO_WRITED | XMMINFO_READS);

//// SRA
static void rpsxSRA_const()
{
	g_psxConstRegs[_Rd_] = *(int*)&g_psxConstRegs[_Rt_] >> _Sa_;
}

static void rpsxSRAs_(int info, int sa)
{
	recBeginOaknutEmit();
	rpsxMoveTtoD_emit_oaknut(info);
	if (sa != 0)
		oakAsm->ASR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), sa);
	recEndOaknutEmit();
}

static void rpsxSRA_emit_direct(int info)
{
	rpsxSRAs_(info, _Sa_);
}

static void rpsxSRA_(int info)
{
	rpsxSRA_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE2(SRA, XMMINFO_WRITED | XMMINFO_READS);

//// SLLV
static void rpsxSLLV_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rt_] << (g_psxConstRegs[_Rs_] & 0x1f);
}

static void rpsxSLLV_consts(int info)
{
	rpsxSLLs_(info, g_psxConstRegs[_Rs_] & 0x1f);
}

static void rpsxSLLV_constt(int info)
{
	pxAssert(_Rs_ != 0);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	oakAsm->MOV(oakWRegister(EEREC_D), g_psxConstRegs[_Rt_]);
	oakAsm->LSL(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void rpsxSLLV_emit_direct(int info)
{
	pxAssert(_Rs_ != 0);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	rpsxMoveTtoD_emit_oaknut(info);
	oakAsm->LSL(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void rpsxSLLV_(int info)
{
	rpsxSLLV_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(SLLV, XMMINFO_WRITED | XMMINFO_READS);

//// SRLV
static void rpsxSRLV_const()
{
	g_psxConstRegs[_Rd_] = g_psxConstRegs[_Rt_] >> (g_psxConstRegs[_Rs_] & 0x1f);
}

static void rpsxSRLV_consts(int info)
{
	rpsxSRLs_(info, g_psxConstRegs[_Rs_] & 0x1f);
}

static void rpsxSRLV_constt(int info)
{
	pxAssert(_Rs_ != 0);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	oakAsm->MOV(oakWRegister(EEREC_D), g_psxConstRegs[_Rt_]);
	oakAsm->LSR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void rpsxSRLV_emit_direct(int info)
{
	pxAssert(_Rs_ != 0);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	rpsxMoveTtoD_emit_oaknut(info);
	oakAsm->LSR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void rpsxSRLV_(int info)
{
	rpsxSRLV_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(SRLV, XMMINFO_WRITED | XMMINFO_READS);

//// SRAV
static void rpsxSRAV_const()
{
	g_psxConstRegs[_Rd_] = *(int*)&g_psxConstRegs[_Rt_] >> (g_psxConstRegs[_Rs_] & 0x1f);
}

static void rpsxSRAV_consts(int info)
{
	rpsxSRAs_(info, g_psxConstRegs[_Rs_] & 0x1f);
}

static void rpsxSRAV_constt(int info)
{
	pxAssert(_Rs_ != 0);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	oakAsm->MOV(oakWRegister(EEREC_D), g_psxConstRegs[_Rt_]);
	oakAsm->ASR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void rpsxSRAV_emit_direct(int info)
{
	pxAssert(_Rs_ != 0);
	recBeginOaknutEmit();
	rpsxMoveGuestToOakW_emit_oaknut(OAK_WSCRATCH, _Rs_, (info & PROCESS_EE_S) ? EEREC_S : -1);
	rpsxMoveTtoD_emit_oaknut(info);
	oakAsm->ASR(oakWRegister(EEREC_D), oakWRegister(EEREC_D), OAK_WSCRATCH);
	recEndOaknutEmit();
}

static void rpsxSRAV_(int info)
{
	rpsxSRAV_emit_direct(info);
}

PSXRECOMPILE_CONSTCODE0(SRAV, XMMINFO_WRITED | XMMINFO_READS);

extern void rpsxSYSCALL();
extern void rpsxBREAK();

static void rpsxMFHI()
{
	if (!_Rd_)
		return;

	rpsxCopyReg_emit_oaknut(_Rd_, PSX_HI);
}

static void rpsxMTHI()
{
	rpsxCopyReg_emit_oaknut(PSX_HI, _Rs_);
}

static void rpsxMFLO()
{
	if (!_Rd_)
		return;

	rpsxCopyReg_emit_oaknut(_Rd_, PSX_LO);
}

static void rpsxMTLO()
{
	rpsxCopyReg_emit_oaknut(PSX_LO, _Rs_);
}

static void rpsxJ()
{
	// j target
	u32 newpc = _InstrucTarget_ * 4 + (psxpc & 0xf0000000);
	psxRecompileNextInstruction(true, false);
	psxSetBranchImm(newpc);
}

static void rpsxJAL()
{
	u32 newpc = (_InstrucTarget_ << 2) + (psxpc & 0xf0000000);
	_psxDeleteReg(31, DELETE_REG_FREE_NO_WRITEBACK);
	PSX_SET_CONST(31);
	g_psxConstRegs[31] = psxpc + 4;

	psxRecompileNextInstruction(true, false);
	psxSetBranchImm(newpc);
}

static void rpsxJR()
{
	psxSetBranchReg(_Rs_);
}

static void rpsxJALR()
{
	const u32 newpc = psxpc + 4;
	if (_Rd_ == _Rs_ && _Rd_ != 0)
	{
		_psxDeleteReg(_Rd_, DELETE_REG_FREE_NO_WRITEBACK);
		PSX_SET_CONST(_Rd_);
		g_psxConstRegs[_Rd_] = newpc;

		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(newpc);
		return;
	}

	const bool swap = (_Rd_ == _Rs_) ? false : psxTrySwapDelaySlot(_Rs_, 0, _Rd_);

	// jalr Rs
	int wbreg = -1;
	if (!swap)
	{
		wbreg = _allocX86reg(X86TYPE_PCWRITEBACK, 0, MODE_WRITE | MODE_CALLEESAVED);
		recBeginOaknutEmit();
		rpsxMoveGuestToOakW_emit_oaknut(oakWRegister(wbreg), _Rs_, _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ));
		recEndOaknutEmit();
	}

	if (_Rd_)
	{
		_psxDeleteReg(_Rd_, DELETE_REG_FREE_NO_WRITEBACK);
		PSX_SET_CONST(_Rd_);
		g_psxConstRegs[_Rd_] = newpc;
	}

	if (!swap)
	{
		psxRecompileNextInstruction(true, false);

		if (x86regs[wbreg].inuse && x86regs[wbreg].type == X86TYPE_PCWRITEBACK)
		{
			recBeginOaknutEmit();
			oakStore32(oakWRegister(wbreg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))});
			recEndOaknutEmit();
			x86regs[wbreg].inuse = 0;
		}
		else
		{
			recBeginOaknutEmit();
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pcWriteback))});
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))});
			recEndOaknutEmit();
		}
	}
	else
	{
		if (PSX_IS_DIRTY_CONST(_Rs_) || _hasX86reg(X86TYPE_PSX, _Rs_, 0))
		{
			const int x86reg = _allocX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
			recBeginOaknutEmit();
			oakStore32(oakWRegister(x86reg), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))});
			recEndOaknutEmit();
		}
		else
		{
			rpsxStoreGuestToCpu32_emit_oaknut({oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))}, _Rs_);
		}
	}

	psxSetBranchReg(0xffffffff);
}

//// BEQ
static u8* rpsxSetBranchEQ_emit_oaknut(int process)
{
	int regs = -1;
	int regt = -1;
	if (process & PROCESS_CONSTS)
		regt = _checkX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
	else if (process & PROCESS_CONSTT)
		regs = _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	else
	{
		// Force S into a register, since we need to load it, may as well cache.
		regs = _allocX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
		regt = _checkX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
	}

	recBeginOaknutEmit();
	if (process & PROCESS_CONSTS)
	{
		if (regt >= 0)
			oakAsm->MOV(OAK_WSCRATCH, oakWRegister(regt));
		else
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});

		oakAsm->MOV(OAK_WSCRATCH2, g_psxConstRegs[_Rs_]);
		oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
	}
	else if (process & PROCESS_CONSTT)
	{
		if (regs >= 0)
			oakAsm->MOV(OAK_WSCRATCH, oakWRegister(regs));
		else
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});

		oakAsm->MOV(OAK_WSCRATCH2, g_psxConstRegs[_Rt_]);
		oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
	}
	else
	{
		if (regt >= 0)
			oakAsm->CMP(oakWRegister(regs), oakWRegister(regt));
		else
		{
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rt_]))});
			oakAsm->CMP(oakWRegister(regs), OAK_WSCRATCH);
		}
	}

	u8* branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return branch;
}

static u8* rpsxSetBranchRsZero_emit_oaknut()
{
	const int regs = _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	recBeginOaknutEmit();
	if (regs >= 0)
		oakAsm->CMP(oakWRegister(regs), 0);
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.GPR.r[_Rs_]))});
		oakAsm->CMP(OAK_WSCRATCH, 0);
	}

	u8* branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return branch;
}

static void rpsxBEQ_const()
{
	u32 branchTo;

	if (g_psxConstRegs[_Rs_] == g_psxConstRegs[_Rt_])
		branchTo = ((s32)_Imm_ * 4) + psxpc;
	else
		branchTo = psxpc + 4;

	psxRecompileNextInstruction(true, false);
	psxSetBranchImm(branchTo);
}

static void rpsxBEQ_process(int process)
{
	u32 branchTo = ((s32)_Imm_ * 4) + psxpc;

	if (_Rs_ == _Rt_)
	{
		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(branchTo);
	}
	else
	{
		const bool swap = psxTrySwapDelaySlot(_Rs_, _Rt_, 0);
		_psxFlushAllDirty();

		u8* s_pbranchjmp = rpsxSetBranchEQ_emit_oaknut(process);

		if (!swap)
		{
			psxSaveBranchState();
			psxRecompileNextInstruction(true, false);
		}

		psxSetBranchImm(branchTo);

		oakPatchCondBranch(s_pbranchjmp, oakGetCurrentCodePointer(), oak::Cond::NE, false);

		if (!swap)
		{
			// recopy the next inst
			psxpc -= 4;
			psxLoadBranchState();
			psxRecompileNextInstruction(true, false);
		}

		psxSetBranchImm(psxpc);
	}
}

static void rpsxBEQ()
{
	// prefer using the host register over an immediate, it'll be smaller code.
	if (PSX_IS_CONST2(_Rs_, _Rt_))
		rpsxBEQ_const();
	else if (PSX_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ) < 0)
		rpsxBEQ_process(PROCESS_CONSTS);
	else if (PSX_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_PSX, _Rt_, MODE_READ) < 0)
		rpsxBEQ_process(PROCESS_CONSTT);
	else
		rpsxBEQ_process(0);
}

//// BNE
static void rpsxBNE_const()
{
	u32 branchTo;

	if (g_psxConstRegs[_Rs_] != g_psxConstRegs[_Rt_])
		branchTo = ((s32)_Imm_ * 4) + psxpc;
	else
		branchTo = psxpc + 4;

	psxRecompileNextInstruction(true, false);
	psxSetBranchImm(branchTo);
}

static void rpsxBNE_process(int process)
{
	const u32 branchTo = ((s32)_Imm_ * 4) + psxpc;

	if (_Rs_ == _Rt_)
	{
		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(psxpc);
		return;
	}

	const bool swap = psxTrySwapDelaySlot(_Rs_, _Rt_, 0);
	_psxFlushAllDirty();

	u8* s_pbranchjmp = rpsxSetBranchEQ_emit_oaknut(process);

	if (!swap)
	{
		psxSaveBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(psxpc);

	oakPatchCondBranch(s_pbranchjmp, oakGetCurrentCodePointer(), oak::Cond::NE, false);

	if (!swap)
	{
		// recopy the next inst
		psxpc -= 4;
		psxLoadBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(branchTo);
}

static void rpsxBNE()
{
	if (PSX_IS_CONST2(_Rs_, _Rt_))
		rpsxBNE_const();
	else if (PSX_IS_CONST1(_Rs_) && _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ) < 0)
		rpsxBNE_process(PROCESS_CONSTS);
	else if (PSX_IS_CONST1(_Rt_) && _checkX86reg(X86TYPE_PSX, _Rt_, MODE_READ) < 0)
		rpsxBNE_process(PROCESS_CONSTT);
	else
		rpsxBNE_process(0);
}

//// BLTZ
static void rpsxBLTZ()
{
	// Branch if Rs < 0
	u32 branchTo = (s32)_Imm_ * 4 + psxpc;

	if (PSX_IS_CONST1(_Rs_))
	{
		if ((int)g_psxConstRegs[_Rs_] >= 0)
			branchTo = psxpc + 4;

		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(branchTo);
		return;
	}

	const bool swap = psxTrySwapDelaySlot(_Rs_, 0, 0);
	_psxFlushAllDirty();

	u8* pjmp = rpsxSetBranchRsZero_emit_oaknut();

	if (!swap)
	{
		psxSaveBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(psxpc);

	oakPatchCondBranch(pjmp, oakGetCurrentCodePointer(), oak::Cond::LT, false);

	if (!swap)
	{
		// recopy the next inst
		psxpc -= 4;
		psxLoadBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(branchTo);
}

//// BGEZ
static void rpsxBGEZ()
{
	u32 branchTo = ((s32)_Imm_ * 4) + psxpc;

	if (PSX_IS_CONST1(_Rs_))
	{
		if ((int)g_psxConstRegs[_Rs_] < 0)
			branchTo = psxpc + 4;

		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(branchTo);
		return;
	}

	const bool swap = psxTrySwapDelaySlot(_Rs_, 0, 0);
	_psxFlushAllDirty();

	u8* pjmp = rpsxSetBranchRsZero_emit_oaknut();

	if (!swap)
	{
		psxSaveBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(psxpc);

	oakPatchCondBranch(pjmp, oakGetCurrentCodePointer(), oak::Cond::GE, false);

	if (!swap)
	{
		// recopy the next inst
		psxpc -= 4;
		psxLoadBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(branchTo);
}

//// BLTZAL
static void rpsxBLTZAL()
{
	// Branch if Rs < 0
	u32 branchTo = (s32)_Imm_ * 4 + psxpc;

	_psxDeleteReg(31, DELETE_REG_FREE_NO_WRITEBACK);

	PSX_SET_CONST(31);
	g_psxConstRegs[31] = psxpc + 4;

	if (PSX_IS_CONST1(_Rs_))
	{
		if ((int)g_psxConstRegs[_Rs_] >= 0)
			branchTo = psxpc + 4;

		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(branchTo);
		return;
	}

	const bool swap = psxTrySwapDelaySlot(_Rs_, 0, 0);
	_psxFlushAllDirty();

	u8* pjmp = rpsxSetBranchRsZero_emit_oaknut();

	if (!swap)
	{
		psxSaveBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(psxpc);

	oakPatchCondBranch(pjmp, oakGetCurrentCodePointer(), oak::Cond::LT, false);

	if (!swap)
	{
		// recopy the next inst
		psxpc -= 4;
		psxLoadBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(branchTo);
}

//// BGEZAL
static void rpsxBGEZAL()
{
	u32 branchTo = ((s32)_Imm_ * 4) + psxpc;

	_psxDeleteReg(31, DELETE_REG_FREE_NO_WRITEBACK);

	PSX_SET_CONST(31);
	g_psxConstRegs[31] = psxpc + 4;

	if (PSX_IS_CONST1(_Rs_))
	{
		if ((int)g_psxConstRegs[_Rs_] < 0)
			branchTo = psxpc + 4;

		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(branchTo);
		return;
	}

	const bool swap = psxTrySwapDelaySlot(_Rs_, 0, 0);
	_psxFlushAllDirty();

	u8* pjmp = rpsxSetBranchRsZero_emit_oaknut();

	if (!swap)
	{
		psxSaveBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(psxpc);

	oakPatchCondBranch(pjmp, oakGetCurrentCodePointer(), oak::Cond::GE, false);

	if (!swap)
	{
		// recopy the next inst
		psxpc -= 4;
		psxLoadBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(branchTo);
}

//// BLEZ
static void rpsxBLEZ()
{
	// Branch if Rs <= 0
	u32 branchTo = (s32)_Imm_ * 4 + psxpc;

	if (PSX_IS_CONST1(_Rs_))
	{
		if ((int)g_psxConstRegs[_Rs_] > 0)
			branchTo = psxpc + 4;

		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(branchTo);
		return;
	}

	const bool swap = psxTrySwapDelaySlot(_Rs_, 0, 0);
	_psxFlushAllDirty();

	u8* pjmp = rpsxSetBranchRsZero_emit_oaknut();

	if (!swap)
	{
		psxSaveBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(psxpc);

	oakPatchCondBranch(pjmp, oakGetCurrentCodePointer(), oak::Cond::LE, false);

	if (!swap)
	{
		psxpc -= 4;
		psxLoadBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(branchTo);
}

//// BGTZ
static void rpsxBGTZ()
{
	// Branch if Rs > 0
	u32 branchTo = (s32)_Imm_ * 4 + psxpc;

	_psxFlushAllDirty();

	if (PSX_IS_CONST1(_Rs_))
	{
		if ((int)g_psxConstRegs[_Rs_] <= 0)
			branchTo = psxpc + 4;

		psxRecompileNextInstruction(true, false);
		psxSetBranchImm(branchTo);
		return;
	}

	const bool swap = psxTrySwapDelaySlot(_Rs_, 0, 0);
	_psxFlushAllDirty();

	u8* pjmp = rpsxSetBranchRsZero_emit_oaknut();

	if (!swap)
	{
		psxSaveBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(psxpc);

	oakPatchCondBranch(pjmp, oakGetCurrentCodePointer(), oak::Cond::GT, false);

	if (!swap)
	{
		psxpc -= 4;
		psxLoadBranchState();
		psxRecompileNextInstruction(true, false);
	}

	psxSetBranchImm(branchTo);
}

static void rpsxMFC0_emit_oaknut();
static void rpsxCFC0_emit_oaknut();
static void rpsxMTC0_emit_oaknut();
static void rpsxCTC0_emit_oaknut();
static void rpsxRFE_emit_oaknut();

static void rpsxMFC0()
{
	rpsxMFC0_emit_oaknut();
}

static void rpsxMFC0_emit_oaknut()
{
	// Rt = Cop0->Rd
	if (!_Rt_)
		return;

	const int rt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_WRITE);
	recBeginOaknutEmit();
	oakLoad32(oakWRegister(rt), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0.r[_Rd_]))});
	recEndOaknutEmit();
}

static void rpsxCFC0()
{
	rpsxCFC0_emit_oaknut();
}

static void rpsxCFC0_emit_oaknut()
{
	// Rt = Cop0->Rd
	if (!_Rt_)
		return;

	const int rt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_WRITE);
	recBeginOaknutEmit();
	oakLoad32(oakWRegister(rt), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0.r[_Rd_]))});
	recEndOaknutEmit();
}

static void rpsxMTC0()
{
	rpsxMTC0_emit_oaknut();
}

static void rpsxMTC0_emit_oaknut()
{
	// Cop0->Rd = Rt
	if (PSX_IS_CONST1(_Rt_))
	{
		recBeginOaknutEmit();
		oakAsm->MOV(OAK_WSCRATCH, g_psxConstRegs[_Rt_]);
		oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0.r[_Rd_]))});
		recEndOaknutEmit();
	}
	else
	{
		const int rt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
		recBeginOaknutEmit();
		oakStore32(oakWRegister(rt), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0.r[_Rd_]))});
		recEndOaknutEmit();
	}
}

static void rpsxCTC0()
{
	rpsxCTC0_emit_oaknut();
}

static void rpsxCTC0_emit_oaknut()
{
	// Cop0->Rd = Rt
	if (PSX_IS_CONST1(_Rt_))
	{
		recBeginOaknutEmit();
		oakAsm->MOV(OAK_WSCRATCH, g_psxConstRegs[_Rt_]);
		oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0.r[_Rd_]))});
		recEndOaknutEmit();
	}
	else
	{
		const int rt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
		recBeginOaknutEmit();
		oakStore32(oakWRegister(rt), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0.r[_Rd_]))});
		recEndOaknutEmit();
	}
}

static void rpsxRFE()
{
	rpsxRFE_emit_oaknut();
}

static void rpsxRFE_emit_oaknut()
{
	// Test the IOP's INTC status, so that any pending ints get raised.
	_psxFlushCall(0);

	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0.n.Status))});
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH, 0x3c);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0xfffffff0);
	oakAsm->LSR(OAK_WSCRATCH2, OAK_WSCRATCH2, 2);
	oakAsm->ORR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP0.n.Status))});
	oakEmitCall(reinterpret_cast<void*>((uptr)&iopTestIntc));
	recEndOaknutEmit();
}

//// COP2

static void rgteNCLIP_emit_oaknut()
{
	recBeginOaknutEmit();

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[12]))});
	oakAsm->SXTH(oak::util::W5, oak::util::W4); // SX0
	oakAsm->ASR(oak::util::W6, oak::util::W4, 16); // SY0

	oakLoad32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[13]))});
	oakAsm->SXTH(oak::util::W8, oak::util::W7); // SX1
	oakAsm->ASR(oak::util::W9, oak::util::W7, 16); // SY1

	oakLoad32(oak::util::W10, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[14]))});
	oakAsm->SXTH(oak::util::W11, oak::util::W10); // SX2
	oakAsm->ASR(oak::util::W12, oak::util::W10, 16); // SY2

	oakAsm->SUB(OAK_WSCRATCH, oak::util::W9, oak::util::W12);
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W5, OAK_WSCRATCH);
	oakAsm->SUB(OAK_WSCRATCH2, oak::util::W12, oak::util::W6);
	oakAsm->MADD(OAK_WSCRATCH, oak::util::W8, OAK_WSCRATCH2, OAK_WSCRATCH);
	oakAsm->SUB(OAK_WSCRATCH2, oak::util::W6, oak::util::W9);
	oakAsm->MADD(OAK_WSCRATCH, oak::util::W11, OAK_WSCRATCH2, OAK_WSCRATCH);

	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[24]))});
	oakAsm->MOV(OAK_WSCRATCH, 0);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[31]))});

	recEndOaknutEmit();
}

static void rgteNCLIP()
{
	rgteNCLIP_emit_oaknut();
}

static void rgteMacOverflowFlags_emit_oaknut(oak::XReg value, u32 high_flag, u32 low_flag);
static void rgteClampMacToIrSigned_emit_oaknut(oak::WReg mac, int ir_reg, u32 flag_bit);
static void rgteSQRClampMacToIr1_emit_oaknut(oak::WReg mac, int ir_reg, u32 flag_bit);
static void rgteClampMacToRgb_emit_oaknut(oak::WReg dst, int mac_reg, u32 flag_bit);
static void rgteStoreGpRgbFifo_emit_oaknut();
static void rgteStoreFlagWithSummary_emit_oaknut();

static void rgteOPComponent_emit_oaknut(int diag_reg, int ir_a_reg, int diag_sub_reg, int ir_b_reg, int mac_reg, u32 mac_high_flag, u32 mac_low_flag, bool shift)
{
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[diag_reg]))});
	oakAsm->SXTH(oak::util::W5, oak::util::W5);
	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_a_reg]))});
	oakAsm->SMULL(OAK_XSCRATCH, oak::util::W5, oak::util::W6);

	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[diag_sub_reg]))});
	oakAsm->SXTH(oak::util::W5, oak::util::W5);
	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_b_reg]))});
	oakAsm->SMULL(oak::util::X5, oak::util::W5, oak::util::W6);

	oakAsm->SUB(OAK_XSCRATCH, OAK_XSCRATCH, oak::util::X5);
	if (shift)
		oakAsm->ASR(OAK_XSCRATCH, OAK_XSCRATCH, 12);

	rgteMacOverflowFlags_emit_oaknut(OAK_XSCRATCH, mac_high_flag, mac_low_flag);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteOP_emit_oaknut()
{
	recBeginOaknutEmit();

	const bool shift = (psxRegs.code & 0x80000) != 0;

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteOPComponent_emit_oaknut(2, 11, 4, 10, 25, 1u << 26, 1u << 29, shift);
	rgteOPComponent_emit_oaknut(4, 9, 0, 11, 26, 1u << 25, 1u << 28, shift);
	rgteOPComponent_emit_oaknut(0, 10, 2, 9, 27, 1u << 24, 1u << 27, shift);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteOP()
{
	rgteOP_emit_oaknut();
}

static void rgteClampSigned16NoFlag_emit_oaknut(oak::WReg value)
{
	oak::Label not_low;
	oak::Label done;

	oakAsm->MOV(oak::util::W4, 0xffff8000u);
	oakAsm->CMP(value, oak::util::W4);
	oakAsm->B(oak::Cond::GE, not_low);
	oakAsm->MOV(value, oak::util::W4);
	oakAsm->B(done);

	oakAsm->l(not_low);
	oakAsm->MOV(oak::util::W4, 32767);
	oakAsm->CMP(value, oak::util::W4);
	oakAsm->CSEL(value, oak::util::W4, value, oak::Cond::GT);

	oakAsm->l(done);
}

static void rgteDPCSComponent_emit_oaknut(u8 rgb_shift, int far_color_reg, int mac_reg)
{
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[6]))});
	if (rgb_shift != 0)
		oakAsm->UBFX(oak::util::W5, oak::util::W5, rgb_shift, 8);
	else
		oakAsm->AND(oak::util::W5, oak::util::W5, 0xff);
	oakAsm->LSL(oak::util::W5, oak::util::W5, 4);

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[far_color_reg]))});
	oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
	rgteClampSigned16NoFlag_emit_oaknut(OAK_WSCRATCH);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W6, OAK_WSCRATCH);
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteDPCS_emit_oaknut()
{
	recBeginOaknutEmit();

	rgteDPCSComponent_emit_oaknut(0, 21, 25);
	rgteDPCSComponent_emit_oaknut(8, 22, 26);
	rgteDPCSComponent_emit_oaknut(16, 23, 27);

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreGpRgbFifo_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteDPCS()
{
	rgteDPCS_emit_oaknut();
}

static void rgteINTPLComponent_emit_oaknut(int ir_reg, int far_color_reg, int mac_reg)
{
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_reg]))});
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[far_color_reg]))});
	oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
	rgteClampSigned16NoFlag_emit_oaknut(OAK_WSCRATCH);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W6, OAK_WSCRATCH);
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteINTPL_emit_oaknut()
{
	recBeginOaknutEmit();

	rgteINTPLComponent_emit_oaknut(9, 21, 25);
	rgteINTPLComponent_emit_oaknut(10, 22, 26);
	rgteINTPLComponent_emit_oaknut(11, 23, 27);

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreGpRgbFifo_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteINTPL()
{
	rgteINTPL_emit_oaknut();
}

static void rgteLoadControlHalfword_emit_oaknut(oak::WReg dst, int control_reg, u8 shift)
{
	oakLoad32(dst, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[control_reg]))});
	if (shift == 0)
		oakAsm->SXTH(dst, dst);
	else
		oakAsm->ASR(dst, dst, shift);
}

static void rgteCCComponent_emit_oaknut(int back_color_reg, int c1_reg, u8 c1_shift, int c2_reg, u8 c2_shift, int c3_reg, u8 c3_shift, u8 rgb_shift, int mac_reg)
{
	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c1_reg, c1_shift);
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[9]))});
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W4, oak::util::W5);

	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c2_reg, c2_shift);
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[10]))});
	oakAsm->MUL(oak::util::W5, oak::util::W4, oak::util::W5);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);

	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c3_reg, c3_shift);
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[11]))});
	oakAsm->MUL(oak::util::W5, oak::util::W4, oak::util::W5);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);

	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[back_color_reg]))});
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W4);

	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[6]))});
	if (rgb_shift != 0)
		oakAsm->UBFX(oak::util::W5, oak::util::W5, rgb_shift, 8);
	else
		oakAsm->AND(oak::util::W5, oak::util::W5, 0xff);
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W5, OAK_WSCRATCH);
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 8);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteCC_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteCCComponent_emit_oaknut(13, 16, 0, 16, 16, 17, 0, 0, 25);
	rgteCCComponent_emit_oaknut(14, 17, 16, 18, 0, 18, 16, 8, 26);
	rgteCCComponent_emit_oaknut(15, 19, 0, 19, 16, 20, 0, 16, 27);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreGpRgbFifo_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteCC()
{
	rgteCC_emit_oaknut();
}

static void rgteLoadDataHalfword_emit_oaknut(oak::WReg dst, int data_reg, u8 shift)
{
	oakLoad32(dst, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[data_reg]))});
	if (shift == 0)
		oakAsm->SXTH(dst, dst);
	else
		oakAsm->ASR(dst, dst, shift);
}

static void rgteClampF12Unsigned_emit_oaknut(oak::WReg dst, u32 flag_bit)
{
	oak::Label not_negative;
	oak::Label in_range;
	oak::Label done;

	oakAsm->CMP(OAK_XSCRATCH, 0);
	oakAsm->B(oak::Cond::GE, not_negative);
	oakAsm->MOV(dst, 0);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(not_negative);
	oakAsm->MOV(oak::util::X4, 32767ll << 12);
	oakAsm->CMP(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->MOV(dst, 32767 << 12);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(in_range);
	if (dst.index() != OAK_WSCRATCH.index())
		oakAsm->MOV(dst, OAK_WSCRATCH);

	oakAsm->l(done);
}

static void rgteNCSLightDot_emit_oaknut(oak::WReg dst, int c1_reg, u8 c1_shift, int c2_reg, u8 c2_shift, int c3_reg, u8 c3_shift, int vx_reg, u8 vx_shift, int vy_reg, u8 vy_shift, int vz_reg, u8 vz_shift, u32 flag_bit)
{
	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c1_reg, c1_shift);
	rgteLoadDataHalfword_emit_oaknut(oak::util::W5, vx_reg, vx_shift);
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W4, oak::util::W5);

	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c2_reg, c2_shift);
	rgteLoadDataHalfword_emit_oaknut(oak::util::W5, vy_reg, vy_shift);
	oakAsm->MUL(oak::util::W5, oak::util::W4, oak::util::W5);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);

	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c3_reg, c3_shift);
	rgteLoadDataHalfword_emit_oaknut(oak::util::W5, vz_reg, vz_shift);
	oakAsm->MUL(oak::util::W5, oak::util::W4, oak::util::W5);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakAsm->SXTW(OAK_XSCRATCH, OAK_WSCRATCH);
	rgteClampF12Unsigned_emit_oaknut(dst, flag_bit);
}

static void rgteNCSColorDot_emit_oaknut(oak::WReg ll1, oak::WReg ll2, oak::WReg ll3, int back_color_reg, int c1_reg, u8 c1_shift, int c2_reg, u8 c2_shift, int c3_reg, u8 c3_shift, int mac_reg, u32 flag_bit)
{
	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c1_reg, c1_shift);
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W4, ll1);

	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c2_reg, c2_shift);
	oakAsm->MUL(oak::util::W5, oak::util::W4, ll2);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);

	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c3_reg, c3_shift);
	oakAsm->MUL(oak::util::W5, oak::util::W4, ll3);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[back_color_reg]))});
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W4);
	oakAsm->SXTW(OAK_XSCRATCH, OAK_WSCRATCH);
	rgteClampF12Unsigned_emit_oaknut(OAK_WSCRATCH, flag_bit);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteNCSForVector_emit_oaknut(int vx_reg, u8 vx_shift, int vy_reg, u8 vy_shift, int vz_reg, u8 vz_shift)
{
	rgteNCSLightDot_emit_oaknut(oak::util::W10, 8, 0, 8, 16, 9, 0, vx_reg, vx_shift, vy_reg, vy_shift, vz_reg, vz_shift, 1u << 24);
	rgteNCSLightDot_emit_oaknut(oak::util::W11, 9, 16, 10, 0, 10, 16, vx_reg, vx_shift, vy_reg, vy_shift, vz_reg, vz_shift, 1u << 23);
	rgteNCSLightDot_emit_oaknut(oak::util::W12, 11, 0, 11, 16, 12, 0, vx_reg, vx_shift, vy_reg, vy_shift, vz_reg, vz_shift, 1u << 22);

	rgteNCSColorDot_emit_oaknut(oak::util::W10, oak::util::W11, oak::util::W12, 13, 16, 0, 16, 16, 17, 0, 25, 1u << 24);
	rgteNCSColorDot_emit_oaknut(oak::util::W10, oak::util::W11, oak::util::W12, 14, 17, 16, 18, 0, 18, 16, 26, 1u << 23);
	rgteNCSColorDot_emit_oaknut(oak::util::W10, oak::util::W11, oak::util::W12, 15, 19, 0, 19, 16, 20, 0, 27, 1u << 22);
}

static void rgteNCS_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteNCSForVector_emit_oaknut(0, 0, 0, 16, 1, 0);
	rgteStoreGpRgbFifo_emit_oaknut();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteNCS()
{
	rgteNCS_emit_oaknut();
}

static void rgteNCTStoreRgbSlot_emit_oaknut(int rgb_reg)
{
	rgteClampMacToRgb_emit_oaknut(oak::util::W4, 25, 1u << 21);
	rgteClampMacToRgb_emit_oaknut(oak::util::W5, 26, 1u << 20);
	rgteClampMacToRgb_emit_oaknut(oak::util::W6, 27, 1u << 19);
	oakLoad32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[6]))});
	oakAsm->AND(oak::util::W7, oak::util::W7, 0xff000000u);
	oakAsm->ORR(oak::util::W7, oak::util::W7, oak::util::W4);
	oakAsm->ORR(oak::util::W7, oak::util::W7, oak::util::W5, oak::LogShift::LSL, 8);
	oakAsm->ORR(oak::util::W7, oak::util::W7, oak::util::W6, oak::LogShift::LSL, 16);
	oakStore32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[rgb_reg]))});
}

static void rgteNCT_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteNCSForVector_emit_oaknut(0, 0, 0, 16, 1, 0);
	rgteNCTStoreRgbSlot_emit_oaknut(20);
	rgteNCSForVector_emit_oaknut(2, 0, 2, 16, 3, 0);
	rgteNCTStoreRgbSlot_emit_oaknut(21);
	rgteNCSForVector_emit_oaknut(4, 0, 4, 16, 5, 0);
	rgteNCTStoreRgbSlot_emit_oaknut(22);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteNCT()
{
	rgteNCT_emit_oaknut();
}

static void rgteNCCSApplyRgbComponent_emit_oaknut(u8 rgb_shift, int mac_reg)
{
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[6]))});
	if (rgb_shift != 0)
		oakAsm->UBFX(oak::util::W4, oak::util::W4, rgb_shift, 8);
	else
		oakAsm->AND(oak::util::W4, oak::util::W4, 0xff);

	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
	oakAsm->SMULL(OAK_XSCRATCH, oak::util::W4, oak::util::W5);
	oakAsm->ASR(OAK_XSCRATCH, OAK_XSCRATCH, 8);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteNCCS_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteNCSForVector_emit_oaknut(0, 0, 0, 16, 1, 0);
	rgteNCCSApplyRgbComponent_emit_oaknut(0, 25);
	rgteNCCSApplyRgbComponent_emit_oaknut(8, 26);
	rgteNCCSApplyRgbComponent_emit_oaknut(16, 27);
	rgteStoreGpRgbFifo_emit_oaknut();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteNCCS()
{
	rgteNCCS_emit_oaknut();
}

static void rgteClampF12Signed_emit_oaknut(u32 flag_bit)
{
	oak::Label not_low;
	oak::Label in_range;
	oak::Label done;

	oakAsm->MOV(oak::util::X4, UINT64_C(0xfffffffff8000000));
	oakAsm->CMP(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(oak::Cond::GE, not_low);
	oakAsm->MOV(OAK_XSCRATCH, oak::util::X4);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(not_low);
	oakAsm->MOV(oak::util::X4, 32767ll << 12);
	oakAsm->CMP(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->MOV(OAK_XSCRATCH, oak::util::X4);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(in_range);
	oakAsm->l(done);
}

static void rgteNCDSDepthCueComponent_emit_oaknut(u8 rgb_shift, int far_color_reg, int mac_reg, u32 flag_bit)
{
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[6]))});
	if (rgb_shift != 0)
		oakAsm->UBFX(oak::util::W4, oak::util::W4, rgb_shift, 8);
	else
		oakAsm->AND(oak::util::W4, oak::util::W4, 0xff);

	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
	oakAsm->SMULL(oak::util::X5, oak::util::W4, oak::util::W5);
	// The interpreter stores this intermediate in an s32 before depth cueing.
	oakAsm->SXTW(oak::util::X5, oak::util::W5);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[far_color_reg]))});
	// IopGte performs the shift as a 32-bit operation before widening it.
	oakAsm->LSL(oak::util::W6, oak::util::W6, 8);
	oakAsm->SXTW(oak::util::X6, oak::util::W6);
	oakAsm->SUB(OAK_XSCRATCH, oak::util::X6, oak::util::X5);
	rgteClampF12Signed_emit_oaknut(flag_bit);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
	oakAsm->SXTW(oak::util::X6, oak::util::W6);
	oakAsm->MUL(oak::util::X6, oak::util::X6, OAK_XSCRATCH);
	oakAsm->ASR(oak::util::X6, oak::util::X6, 12);
	oakAsm->ADD(OAK_XSCRATCH, oak::util::X5, oak::util::X6);
	oakAsm->ASR(OAK_XSCRATCH, OAK_XSCRATCH, 8);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteNCDS_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteNCSForVector_emit_oaknut(0, 0, 0, 16, 1, 0);
	rgteNCDSDepthCueComponent_emit_oaknut(0, 21, 25, 1u << 24);
	rgteNCDSDepthCueComponent_emit_oaknut(8, 22, 26, 1u << 23);
	rgteNCDSDepthCueComponent_emit_oaknut(16, 23, 27, 1u << 22);
	rgteStoreGpRgbFifo_emit_oaknut();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteNCDS()
{
	rgteNCDS_emit_oaknut();
}

static void rgteNCDT_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteNCSForVector_emit_oaknut(0, 0, 0, 16, 1, 0);
	rgteNCDSDepthCueComponent_emit_oaknut(0, 21, 25, 1u << 24);
	rgteNCDSDepthCueComponent_emit_oaknut(8, 22, 26, 1u << 23);
	rgteNCDSDepthCueComponent_emit_oaknut(16, 23, 27, 1u << 22);
	rgteNCTStoreRgbSlot_emit_oaknut(20);
	rgteNCSForVector_emit_oaknut(2, 0, 2, 16, 3, 0);
	rgteNCDSDepthCueComponent_emit_oaknut(0, 21, 25, 1u << 24);
	rgteNCDSDepthCueComponent_emit_oaknut(8, 22, 26, 1u << 23);
	rgteNCDSDepthCueComponent_emit_oaknut(16, 23, 27, 1u << 22);
	rgteNCTStoreRgbSlot_emit_oaknut(21);
	rgteNCSForVector_emit_oaknut(4, 0, 4, 16, 5, 0);
	rgteNCDSDepthCueComponent_emit_oaknut(0, 21, 25, 1u << 24);
	rgteNCDSDepthCueComponent_emit_oaknut(8, 22, 26, 1u << 23);
	rgteNCDSDepthCueComponent_emit_oaknut(16, 23, 27, 1u << 22);
	rgteNCTStoreRgbSlot_emit_oaknut(22);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteNCDT()
{
	rgteNCDT_emit_oaknut();
}

static void rgteClampSigned16ToX_emit_oaknut(u32 flag_bit)
{
	oak::Label not_low;
	oak::Label in_range;
	oak::Label done;

	oakAsm->MOV(oak::util::X4, UINT64_C(0xffffffffffff8000));
	oakAsm->CMP(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(oak::Cond::GE, not_low);
	oakAsm->MOV(OAK_XSCRATCH, oak::util::X4);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(not_low);
	oakAsm->MOV(oak::util::X4, 32767);
	oakAsm->CMP(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->MOV(OAK_XSCRATCH, oak::util::X4);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(in_range);
	oakAsm->l(done);
}

static void rgteCDPComponent_emit_oaknut(u8 rgb_shift, int back_color_reg, int c1_reg, u8 c1_shift, int c2_reg, u8 c2_shift, int c3_reg, u8 c3_shift, int far_color_reg, int mac_reg, u32 clamp_flag)
{
	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c1_reg, c1_shift);
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[9]))});
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W4, oak::util::W5);

	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c2_reg, c2_shift);
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[10]))});
	oakAsm->MUL(oak::util::W5, oak::util::W4, oak::util::W5);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);

	rgteLoadControlHalfword_emit_oaknut(oak::util::W4, c3_reg, c3_shift);
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[11]))});
	oakAsm->MUL(oak::util::W5, oak::util::W4, oak::util::W5);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[back_color_reg]))});
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W4);
	oakAsm->SXTW(OAK_XSCRATCH, OAK_WSCRATCH);

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[6]))});
	if (rgb_shift != 0)
		oakAsm->UBFX(oak::util::W4, oak::util::W4, rgb_shift, 8);
	else
		oakAsm->AND(oak::util::W4, oak::util::W4, 0xff);
	oakAsm->MUL(oak::util::X5, oak::util::X4, OAK_XSCRATCH);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[far_color_reg]))});
	oakAsm->SXTW(oak::util::X6, oak::util::W6);
	oakAsm->SUB(OAK_XSCRATCH, oak::util::X6, oak::util::X5);
	rgteClampSigned16ToX_emit_oaknut(clamp_flag);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
	oakAsm->SXTW(oak::util::X6, oak::util::W6);
	oakAsm->MUL(oak::util::X6, oak::util::X6, OAK_XSCRATCH);
	oakAsm->ADD(OAK_XSCRATCH, oak::util::X5, oak::util::X6);
	oak::Label not_low;
	oak::Label in_range;
	oak::Label store;
	oakAsm->MOV(oak::util::X4, UINT64_C(0xffffffff80000000));
	oakAsm->CMP(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(oak::Cond::GE, not_low);
	oakAsm->MOV(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(store);
	oakAsm->l(not_low);
	oakAsm->MOV(oak::util::X4, 0x7fffffff);
	oakAsm->CMP(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->MOV(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(store);
	oakAsm->l(in_range);
	oakAsm->l(store);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteCDP_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteCDPComponent_emit_oaknut(0, 13, 16, 0, 16, 16, 17, 0, 21, 25, 1u << 24);
	rgteCDPComponent_emit_oaknut(8, 14, 17, 16, 18, 0, 18, 16, 22, 26, 1u << 23);
	rgteCDPComponent_emit_oaknut(16, 15, 19, 0, 19, 16, 20, 0, 23, 27, 1u << 22);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreGpRgbFifo_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteCDP()
{
	rgteCDP_emit_oaknut();
}

static void rgteMVMVALoadVectorComponent_emit_oaknut(oak::WReg dst, u32 vector_mode, int component)
{
	switch (vector_mode)
	{
		case 0x00000:
			rgteLoadDataHalfword_emit_oaknut(dst, component == 0 ? 0 : component == 1 ? 0 : 1, component == 1 ? 16 : 0);
			break;
		case 0x08000:
			rgteLoadDataHalfword_emit_oaknut(dst, component == 0 ? 2 : component == 1 ? 2 : 3, component == 1 ? 16 : 0);
			break;
		case 0x10000:
			rgteLoadDataHalfword_emit_oaknut(dst, component == 0 ? 4 : component == 1 ? 4 : 5, component == 1 ? 16 : 0);
			break;
		case 0x18000:
			oakLoad32(dst, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[9 + component]))});
			oakAsm->SXTH(dst, dst);
			break;
	}
}

static void rgteMVMVALoadMatrixComponent_emit_oaknut(oak::WReg dst, u32 matrix_mode, int row, int component)
{
	const int base = matrix_mode == 0x00000 ? 0 : matrix_mode == 0x20000 ? 8 : 16;
	static constexpr int reg_offsets[3][3] = {
		{0, 0, 1},
		{1, 2, 2},
		{3, 3, 4},
	};
	static constexpr u8 shifts[3][3] = {
		{0, 16, 0},
		{16, 0, 16},
		{0, 16, 0},
	};
	rgteLoadControlHalfword_emit_oaknut(dst, base + reg_offsets[row][component], shifts[row][component]);
}

static void rgteMVMVADot_emit_oaknut(u32 matrix_mode, int row, int add_reg, int mac_reg, u32 mac_high_flag, u32 mac_low_flag, bool shift)
{
	if (matrix_mode == 0x60000)
	{
		oakAsm->MOV(OAK_XSCRATCH, 0);
	}
	else
	{
		rgteMVMVALoadMatrixComponent_emit_oaknut(oak::util::W4, matrix_mode, row, 0);
		oakAsm->MUL(OAK_WSCRATCH, oak::util::W4, oak::util::W10);
		rgteMVMVALoadMatrixComponent_emit_oaknut(oak::util::W4, matrix_mode, row, 1);
		oakAsm->MUL(oak::util::W5, oak::util::W4, oak::util::W11);
		oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
		rgteMVMVALoadMatrixComponent_emit_oaknut(oak::util::W4, matrix_mode, row, 2);
		oakAsm->MUL(oak::util::W5, oak::util::W4, oak::util::W12);
		oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
		oakAsm->SXTW(OAK_XSCRATCH, OAK_WSCRATCH);
		if (shift)
			oakAsm->ASR(OAK_XSCRATCH, OAK_XSCRATCH, 12);
	}

	if (add_reg >= 0)
	{
		oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[add_reg]))});
		oakAsm->SXTW(oak::util::X4, oak::util::W4);
		oakAsm->ADD(OAK_XSCRATCH, OAK_XSCRATCH, oak::util::X4);
	}

	rgteMacOverflowFlags_emit_oaknut(OAK_XSCRATCH, mac_high_flag, mac_low_flag);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteMVMVA_emit_oaknut()
{
	recBeginOaknutEmit();

	const u32 matrix_mode = psxRegs.code & 0x60000;
	const u32 vector_mode = psxRegs.code & 0x18000;
	const u32 translation_mode = psxRegs.code & 0x6000;
	const bool shift = (psxRegs.code & 0x80000) != 0;
	const bool unsigned_ir = (psxRegs.code & 0x400) != 0;
	const int add_base = translation_mode == 0x0000 ? 5 : translation_mode == 0x2000 ? 13 : translation_mode == 0x4000 ? 21 : -1;

	if (vector_mode == 0x18000 || vector_mode == 0x10000 || vector_mode == 0x08000 || vector_mode == 0x00000)
	{
		rgteMVMVALoadVectorComponent_emit_oaknut(oak::util::W10, vector_mode, 0);
		rgteMVMVALoadVectorComponent_emit_oaknut(oak::util::W11, vector_mode, 1);
		rgteMVMVALoadVectorComponent_emit_oaknut(oak::util::W12, vector_mode, 2);
	}

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteMVMVADot_emit_oaknut(matrix_mode, 0, add_base < 0 ? -1 : add_base + 0, 25, 1u << 26, 1u << 29, shift);
	rgteMVMVADot_emit_oaknut(matrix_mode, 1, add_base < 0 ? -1 : add_base + 1, 26, 1u << 25, 1u << 28, shift);
	rgteMVMVADot_emit_oaknut(matrix_mode, 2, add_base < 0 ? -1 : add_base + 2, 27, 1u << 24, 1u << 27, shift);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	if (unsigned_ir)
		rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	else
		rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	if (unsigned_ir)
		rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	else
		rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	if (unsigned_ir)
		rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	else
		rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteMVMVA()
{
	rgteMVMVA_emit_oaknut();
}

static void rgteClampMacToSz_emit_oaknut(int sz_reg)
{
	oak::Label not_negative;
	oak::Label in_range;
	oak::Label store;

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	oakAsm->CMP(OAK_WSCRATCH, 0);
	oakAsm->B(oak::Cond::GE, not_negative);
	oakAsm->MOV(OAK_WSCRATCH, 0);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, 1u << 18);
	oakAsm->B(store);

	oakAsm->l(not_negative);
	oakAsm->MOV(oak::util::W4, 65535);
	oakAsm->CMP(OAK_WSCRATCH, oak::util::W4);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->MOV(OAK_WSCRATCH, oak::util::W4);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, 1u << 18);
	oakAsm->B(store);

	oakAsm->l(in_range);
	oakAsm->l(store);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[sz_reg]))});
}

static void rgteClampScreenComponent_emit_oaknut(oak::WReg value, u32 flag_bit)
{
	oak::Label not_low;
	oak::Label in_range;
	oak::Label done;

	oakAsm->MOV(oak::util::W8, 0xfffffc00u);
	oakAsm->CMP(value, oak::util::W8);
	oakAsm->B(oak::Cond::GE, not_low);
	oakAsm->MOV(value, oak::util::W8);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(not_low);
	oakAsm->MOV(oak::util::W8, 1023);
	oakAsm->CMP(value, oak::util::W8);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->MOV(value, oak::util::W8);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(in_range);
	oakAsm->l(done);
}

static void rgteComputeProjectionFactor_emit_oaknut(int sz_reg)
{
	oak::Label use_clamped;
	oak::Label done;

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[sz_reg]))});
	oakAsm->CBZ(oak::util::W4, use_clamped);

	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[26]))});
	oakAsm->AND(oak::util::W5, oak::util::W5, 0xffff);
	oakAsm->LSL(oak::util::X5, oak::util::X5, 16);
	oakAsm->UDIV(oak::util::X13, oak::util::X5, oak::util::X4);
	oakAsm->MOV(oak::util::X5, 2 << 16);
	oakAsm->CMP(oak::util::X13, oak::util::X5);
	oakAsm->B(oak::Cond::LS, done);

	oakAsm->l(use_clamped);
	oakAsm->MOV(oak::util::X13, 2 << 16);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, 1u << 17);

	oakAsm->l(done);
}

static void rgteProjectScreenComponent_emit_oaknut(int ir_reg, int offset_reg, oak::WReg dst, u32 flag_bit)
{
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_reg]))});
	oakAsm->SXTW(oak::util::X4, oak::util::W4);
	oakAsm->LSL(oak::util::X4, oak::util::X4, 16);
	oakAsm->MUL(oak::util::X4, oak::util::X4, oak::util::X13);
	oakAsm->ASR(oak::util::X4, oak::util::X4, 16);
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[offset_reg]))});
	oakAsm->SXTW(oak::util::X5, oak::util::W5);
	oakAsm->ADD(oak::util::X4, oak::util::X5, oak::util::X4);
	oakAsm->ASR(oak::util::X4, oak::util::X4, 16);
	oakAsm->MOV(dst, oak::util::W4);
	rgteClampScreenComponent_emit_oaknut(dst, flag_bit);
}

static void rgteStoreProjectedSxy_emit_oaknut(int sxy_reg)
{
	rgteProjectScreenComponent_emit_oaknut(9, 24, oak::util::W9, 1u << 14);
	rgteProjectScreenComponent_emit_oaknut(10, 25, oak::util::W10, 1u << 13);
	oakAsm->AND(oak::util::W9, oak::util::W9, 0xffff);
	oakAsm->BFI(oak::util::W9, oak::util::W10, 16, 16);
	oakStore32(oak::util::W9, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[sxy_reg]))});
}

static void rgteStoreDepthCue_emit_oaknut()
{
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[27]))});
	oakAsm->SXTH(oak::util::W4, oak::util::W4);
	oakAsm->SXTW(oak::util::X4, oak::util::W4);
	oakAsm->LSL(oak::util::X4, oak::util::X4, 8);
	oakAsm->MUL(oak::util::X4, oak::util::X4, oak::util::X13);
	oakAsm->ASR(oak::util::X4, oak::util::X4, 8);
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[28]))});
	oakAsm->SXTW(oak::util::X5, oak::util::W5);
	oakAsm->ADD(oak::util::X4, oak::util::X5, oak::util::X4);
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[24]))});

	oakAsm->ASR(oak::util::X4, oak::util::X4, 12);
	oak::Label not_negative;
	oak::Label in_range;
	oak::Label store;
	oakAsm->CMP(oak::util::X4, 0);
	oakAsm->B(oak::Cond::GE, not_negative);
	oakAsm->MOV(oak::util::W4, 0);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, 1u << 12);
	oakAsm->B(store);
	oakAsm->l(not_negative);
	oakAsm->MOV(oak::util::X5, 65535);
	oakAsm->CMP(oak::util::X4, oak::util::X5);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->MOV(oak::util::W4, 65535);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, 1u << 12);
	oakAsm->B(store);
	oakAsm->l(in_range);
	oakAsm->MOV(oak::util::W4, oak::util::W4);
	oakAsm->l(store);
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
}

static void rgteRTPS1ForVector_emit_oaknut(u32 vector_mode)
{
	rgteMVMVALoadVectorComponent_emit_oaknut(oak::util::W10, vector_mode, 0);
	rgteMVMVALoadVectorComponent_emit_oaknut(oak::util::W11, vector_mode, 1);
	rgteMVMVALoadVectorComponent_emit_oaknut(oak::util::W12, vector_mode, 2);
	rgteMVMVADot_emit_oaknut(0x00000, 0, 5, 25, 1u << 26, 1u << 29, true);
	rgteMVMVADot_emit_oaknut(0x00000, 1, 6, 26, 1u << 25, 1u << 28, true);
	rgteMVMVADot_emit_oaknut(0x00000, 2, 7, 27, 1u << 24, 1u << 27, true);
}

static void rgteRTPS_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteRTPS1ForVector_emit_oaknut(0x00000);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[17]))});
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[16]))});
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[18]))});
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[17]))});
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[19]))});
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[18]))});
	rgteClampMacToSz_emit_oaknut(19);

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[13]))});
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[12]))});
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[14]))});
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[13]))});

	rgteComputeProjectionFactor_emit_oaknut(19);
	rgteStoreProjectedSxy_emit_oaknut(14);
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[14]))});
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[15]))});
	rgteStoreDepthCue_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteRTPS()
{
	rgteRTPS_emit_oaknut();
}

static void rgteRTPTProjectIntermediate_emit_oaknut(u32 vector_mode, int sz_reg, int sxy_reg)
{
	rgteRTPS1ForVector_emit_oaknut(vector_mode);
	rgteClampMacToSz_emit_oaknut(sz_reg);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	rgteComputeProjectionFactor_emit_oaknut(sz_reg);
	rgteStoreProjectedSxy_emit_oaknut(sxy_reg);
}

static void rgteRTPT_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[19]))});
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[16]))});

	rgteRTPTProjectIntermediate_emit_oaknut(0x00000, 17, 12);
	rgteRTPTProjectIntermediate_emit_oaknut(0x08000, 18, 13);

	rgteRTPS1ForVector_emit_oaknut(0x10000);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteClampMacToSz_emit_oaknut(19);
	rgteComputeProjectionFactor_emit_oaknut(19);
	rgteStoreProjectedSxy_emit_oaknut(14);
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[14]))});
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[15]))});
	rgteStoreDepthCue_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteRTPT()
{
	rgteRTPT_emit_oaknut();
}

static void rgteSQRClampMacToIr1_emit_oaknut(oak::WReg mac, int ir_reg, u32 flag_bit)
{
	oak::Label not_negative;
	oak::Label in_range;
	oak::Label store;

	oakAsm->CMP(mac, 0);
	oakAsm->B(oak::Cond::GE, not_negative);
	oakAsm->MOV(oak::util::W4, 0);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(store);

	oakAsm->l(not_negative);
	oakAsm->MOV(oak::util::W4, 32767);
	oakAsm->CMP(mac, oak::util::W4);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(store);

	oakAsm->l(in_range);
	oakAsm->MOV(oak::util::W4, mac);

	oakAsm->l(store);
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_reg]))});
}

static void rgteSQRComponent_emit_oaknut(int ir_reg, int mac_reg, u32 mac_high_flag, u32 ir_flag_bit, bool shift)
{
	oak::Label no_mac_overflow;

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_reg]))});
	oakAsm->SMULL(OAK_XSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH);
	if (shift)
		oakAsm->ASR(OAK_XSCRATCH, OAK_XSCRATCH, 12);

	oakAsm->MOV(oak::util::X4, 0x7fffffff);
	oakAsm->CMP(OAK_XSCRATCH, oak::util::X4);
	oakAsm->B(oak::Cond::LE, no_mac_overflow);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, mac_high_flag);
	oakAsm->l(no_mac_overflow);

	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, ir_reg, ir_flag_bit);
}

static void rgteSQR_emit_oaknut()
{
	recBeginOaknutEmit();

	const bool shift = (psxRegs.code & 0x80000) != 0;

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteSQRComponent_emit_oaknut(9, 25, 1u << 26, 1u << 24, shift);
	rgteSQRComponent_emit_oaknut(10, 26, 1u << 25, 1u << 23, shift);
	rgteSQRComponent_emit_oaknut(11, 27, 1u << 24, 1u << 22, shift);

	oakAsm->MOV(oak::util::W4, 0x7f87e000);
	oakAsm->TST(OAK_WSCRATCH2, oak::util::W4);
	oakAsm->ORR(oak::util::W4, OAK_WSCRATCH2, 0x80000000u);
	oakAsm->CSEL(OAK_WSCRATCH2, oak::util::W4, OAK_WSCRATCH2, oak::Cond::NE);
	oakStore32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[31]))});

	recEndOaknutEmit();
}

static void rgteSQR()
{
	rgteSQR_emit_oaknut();
}

static void rgteDCPLComponent_emit_oaknut(u8 rgb_shift, int ir_reg, int far_color_reg, int mac_reg)
{
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[6]))});
	if (rgb_shift != 0)
		oakAsm->UBFX(oak::util::W5, oak::util::W5, rgb_shift, 8);
	else
		oakAsm->AND(oak::util::W5, oak::util::W5, 0xff);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_reg]))});
	oakAsm->MUL(oak::util::W7, oak::util::W5, oak::util::W6);

	oakAsm->ASR(oak::util::W4, oak::util::W7, 12);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[far_color_reg]))});
	oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W4);
	rgteClampSigned16NoFlag_emit_oaknut(OAK_WSCRATCH);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W6, OAK_WSCRATCH);
	oakAsm->SXTW(OAK_XSCRATCH, OAK_WSCRATCH);
	oakAsm->SXTW(oak::util::X7, oak::util::W7);
	oakAsm->ADD(OAK_XSCRATCH, OAK_XSCRATCH, oak::util::X7);
	oakAsm->ASR(OAK_XSCRATCH, OAK_XSCRATCH, 8);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteDCPL_emit_oaknut()
{
	recBeginOaknutEmit();

	rgteDCPLComponent_emit_oaknut(0, 9, 21, 25);
	rgteDCPLComponent_emit_oaknut(8, 10, 22, 26);
	rgteDCPLComponent_emit_oaknut(16, 11, 23, 27);

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreGpRgbFifo_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteDCPL()
{
	rgteDCPL_emit_oaknut();
}

static void rgteDPCTComponent_emit_oaknut(u8 rgb_shift, int far_color_reg, int mac_reg)
{
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[20]))});
	if (rgb_shift != 0)
		oakAsm->UBFX(oak::util::W5, oak::util::W5, rgb_shift, 8);
	else
		oakAsm->AND(oak::util::W5, oak::util::W5, 0xff);
	oakAsm->LSL(oak::util::W5, oak::util::W5, 4);

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[far_color_reg]))});
	oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
	rgteClampSigned16NoFlag_emit_oaknut(OAK_WSCRATCH);

	oakLoad32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W6, OAK_WSCRATCH);
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W5);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
}

static void rgteDPCTStage_emit_oaknut(bool final_stage)
{
	rgteDPCTComponent_emit_oaknut(0, 21, 25);
	rgteDPCTComponent_emit_oaknut(8, 22, 26);
	rgteDPCTComponent_emit_oaknut(16, 23, 27);

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	if (final_stage)
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
		rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
		rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
		rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	}
	rgteStoreGpRgbFifo_emit_oaknut();
}

static void rgteDPCT_emit_oaknut()
{
	recBeginOaknutEmit();

	rgteDPCTStage_emit_oaknut(false);
	rgteDPCTStage_emit_oaknut(false);
	rgteDPCTStage_emit_oaknut(true);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteDPCT()
{
	rgteDPCT_emit_oaknut();
}

static void rgteClampOtzFromMac0_emit_oaknut()
{
	oak::Label not_negative;
	oak::Label check_high;
	oak::Label done;

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	oakAsm->CMP(OAK_WSCRATCH, 0);
	oakAsm->B(oak::Cond::GE, not_negative);
	oakAsm->MOV(OAK_WSCRATCH, 0);
	oakAsm->MOV(OAK_WSCRATCH2, 0x80040000u);
	oakAsm->B(done);

	oakAsm->l(not_negative);
	oakAsm->MOV(oak::util::W4, 65535);
	oakAsm->CMP(OAK_WSCRATCH, oak::util::W4);
	oakAsm->B(oak::Cond::LE, check_high);
	oakAsm->MOV(OAK_WSCRATCH, oak::util::W4);
	oakAsm->MOV(OAK_WSCRATCH2, 0x80040000u);

	oakAsm->l(check_high);
	oakAsm->l(done);
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[7]))});
	oakAsm->BFI(oak::util::W4, OAK_WSCRATCH, 0, 16);
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[7]))});
	oakStore32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[31]))});
}

static void rgteAVSZ3_emit_oaknut()
{
	recBeginOaknutEmit();

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[17]))});
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0xffff);
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[18]))});
	oakAsm->UBFX(OAK_WSCRATCH2, OAK_WSCRATCH2, 0, 16);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[19]))});
	oakAsm->UBFX(OAK_WSCRATCH2, OAK_WSCRATCH2, 0, 16);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);

	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[29]))});
	oakAsm->SXTH(OAK_WSCRATCH2, OAK_WSCRATCH2);
	oakAsm->MUL(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[24]))});
	rgteClampOtzFromMac0_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteAVSZ3()
{
	rgteAVSZ3_emit_oaknut();
}

static void rgteAVSZ4_emit_oaknut()
{
	recBeginOaknutEmit();

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[16]))});
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0xffff);
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[17]))});
	oakAsm->UBFX(OAK_WSCRATCH2, OAK_WSCRATCH2, 0, 16);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[18]))});
	oakAsm->UBFX(OAK_WSCRATCH2, OAK_WSCRATCH2, 0, 16);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[19]))});
	oakAsm->UBFX(OAK_WSCRATCH2, OAK_WSCRATCH2, 0, 16);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);

	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[30]))});
	oakAsm->SXTH(OAK_WSCRATCH2, OAK_WSCRATCH2);
	oakAsm->MUL(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[24]))});
	rgteClampOtzFromMac0_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteAVSZ4()
{
	rgteAVSZ4_emit_oaknut();
}

static void rgteMacOverflowFlags_emit_oaknut(oak::XReg value, u32 high_flag, u32 low_flag)
{
	oak::Label no_high;
	oak::Label done;

	oakAsm->MOV(oak::util::X4, 0x7fffffff);
	oakAsm->CMP(value, oak::util::X4);
	oakAsm->B(oak::Cond::LE, no_high);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, high_flag);
	oakAsm->B(done);

	oakAsm->l(no_high);
	oakAsm->MOV(oak::util::X4, UINT64_C(0xffffffff80000000));
	oakAsm->CMP(value, oak::util::X4);
	oakAsm->B(oak::Cond::GE, done);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, low_flag);

	oakAsm->l(done);
}

static void rgteClampMacToIrSigned_emit_oaknut(oak::WReg mac, int ir_reg, u32 flag_bit)
{
	oak::Label not_low;
	oak::Label in_range;
	oak::Label store;

	oakAsm->MOV(oak::util::W4, 0xffff8000u);
	oakAsm->CMP(mac, oak::util::W4);
	oakAsm->B(oak::Cond::GE, not_low);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(store);

	oakAsm->l(not_low);
	oakAsm->MOV(oak::util::W4, 32767);
	oakAsm->CMP(mac, oak::util::W4);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(store);

	oakAsm->l(in_range);
	oakAsm->MOV(oak::util::W4, mac);

	oakAsm->l(store);
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_reg]))});
}

static void rgteClampMacToRgb_emit_oaknut(oak::WReg dst, int mac_reg, u32 flag_bit)
{
	oak::Label not_negative;
	oak::Label in_range;
	oak::Label done;

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
	oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 4);
	oakAsm->CMP(OAK_WSCRATCH, 0);
	oakAsm->B(oak::Cond::GE, not_negative);
	oakAsm->MOV(dst, 0);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(not_negative);
	oakAsm->MOV(oak::util::W8, 255);
	oakAsm->CMP(OAK_WSCRATCH, oak::util::W8);
	oakAsm->B(oak::Cond::LE, in_range);
	oakAsm->MOV(dst, oak::util::W8);
	oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, flag_bit);
	oakAsm->B(done);

	oakAsm->l(in_range);
	oakAsm->MOV(dst, OAK_WSCRATCH);

	oakAsm->l(done);
}

static void rgteStoreFlagWithSummary_emit_oaknut()
{
	oakAsm->MOV(oak::util::W4, 0x7f87e000);
	oakAsm->TST(OAK_WSCRATCH2, oak::util::W4);
	oakAsm->ORR(oak::util::W4, OAK_WSCRATCH2, 0x80000000u);
	oakAsm->CSEL(OAK_WSCRATCH2, oak::util::W4, OAK_WSCRATCH2, oak::Cond::NE);
	oakStore32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[31]))});
}

static void rgteGPFComponent_emit_oaknut(int ir_reg, int mac_reg, u32 ir_flag_bit, bool shift)
{
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_reg]))});
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W4, OAK_WSCRATCH);
	if (shift)
		oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, ir_reg, ir_flag_bit);
}

static void rgteGPLComponent_emit_oaknut(int ir_reg, int mac_reg, u32 ir_flag_bit, bool shift)
{
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[8]))});
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[ir_reg]))});
	oakAsm->MUL(OAK_WSCRATCH, oak::util::W4, OAK_WSCRATCH);
	if (shift)
		oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 12);
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W4);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[mac_reg]))});
	rgteClampMacToIrSigned_emit_oaknut(OAK_WSCRATCH, ir_reg, ir_flag_bit);
}

static void rgteStoreGpRgbFifo_emit_oaknut()
{
	oakLoad32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[21]))});
	oakStore32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[20]))});
	oakLoad32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[22]))});
	oakStore32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[21]))});

	oakLoad32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[6]))});
	oakAsm->AND(oak::util::W7, oak::util::W7, 0xff000000u);
	rgteClampMacToRgb_emit_oaknut(oak::util::W4, 25, 1u << 21);
	rgteClampMacToRgb_emit_oaknut(oak::util::W5, 26, 1u << 20);
	rgteClampMacToRgb_emit_oaknut(oak::util::W6, 27, 1u << 19);
	oakAsm->ORR(oak::util::W7, oak::util::W7, oak::util::W4);
	oakAsm->ORR(oak::util::W7, oak::util::W7, oak::util::W5, oak::LogShift::LSL, 8);
	oakAsm->ORR(oak::util::W7, oak::util::W7, oak::util::W6, oak::LogShift::LSL, 16);
	oakStore32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[22]))});
}

static void rgteGPF_emit_oaknut()
{
	recBeginOaknutEmit();

	const bool shift = (psxRegs.code & 0x80000) != 0;

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteGPFComponent_emit_oaknut(9, 25, 1u << 24, shift);
	rgteGPFComponent_emit_oaknut(10, 26, 1u << 23, shift);
	rgteGPFComponent_emit_oaknut(11, 27, 1u << 22, shift);
	rgteStoreGpRgbFifo_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteGPF()
{
	rgteGPF_emit_oaknut();
}

static void rgteGPL_emit_oaknut()
{
	recBeginOaknutEmit();

	const bool shift = (psxRegs.code & 0x80000) != 0;

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteGPLComponent_emit_oaknut(9, 25, 1u << 24, shift);
	rgteGPLComponent_emit_oaknut(10, 26, 1u << 23, shift);
	rgteGPLComponent_emit_oaknut(11, 27, 1u << 22, shift);
	rgteStoreGpRgbFifo_emit_oaknut();
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteGPL()
{
	rgteGPL_emit_oaknut();
}

static void rgteNCCT_emit_oaknut()
{
	recBeginOaknutEmit();

	oakAsm->MOV(OAK_WSCRATCH2, 0);
	rgteNCSForVector_emit_oaknut(0, 0, 0, 16, 1, 0);
	rgteNCCSApplyRgbComponent_emit_oaknut(0, 25);
	rgteNCCSApplyRgbComponent_emit_oaknut(8, 26);
	rgteNCCSApplyRgbComponent_emit_oaknut(16, 27);
	rgteNCTStoreRgbSlot_emit_oaknut(20);
	rgteNCSForVector_emit_oaknut(2, 0, 2, 16, 3, 0);
	rgteNCCSApplyRgbComponent_emit_oaknut(0, 25);
	rgteNCCSApplyRgbComponent_emit_oaknut(8, 26);
	rgteNCCSApplyRgbComponent_emit_oaknut(16, 27);
	rgteNCTStoreRgbSlot_emit_oaknut(21);
	rgteNCSForVector_emit_oaknut(4, 0, 4, 16, 5, 0);
	rgteNCCSApplyRgbComponent_emit_oaknut(0, 25);
	rgteNCCSApplyRgbComponent_emit_oaknut(8, 26);
	rgteNCCSApplyRgbComponent_emit_oaknut(16, 27);
	rgteNCTStoreRgbSlot_emit_oaknut(22);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[25]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 9, 1u << 24);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[26]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 10, 1u << 23);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[27]))});
	rgteSQRClampMacToIr1_emit_oaknut(OAK_WSCRATCH, 11, 1u << 22);
	rgteStoreFlagWithSummary_emit_oaknut();

	recEndOaknutEmit();
}

static void rgteNCCT()
{
	rgteNCCT_emit_oaknut();
}

static void rgteMFC2Value_emit_oaknut(oak::WReg dst, int reg)
{
	if (reg == 29)
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[9]))});
		oakAsm->LSR(dst, OAK_WSCRATCH, 7);
		oakAsm->AND(dst, dst, 0x1f);

		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[10]))});
		oakAsm->LSR(OAK_WSCRATCH2, OAK_WSCRATCH, 2);
		oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x3e0);
		oakAsm->ORR(dst, dst, OAK_WSCRATCH2);

		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[11]))});
		oakAsm->LSL(OAK_WSCRATCH2, OAK_WSCRATCH, 3);
		oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x7c00);
		oakAsm->ORR(dst, dst, OAK_WSCRATCH2);
		oakStore32(dst, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[29]))});
		return;
	}

	oakLoad32(dst, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[reg]))});
}

static void rgteMTC2Value_emit_oaknut(oak::WReg value, int reg)
{
	switch (reg)
	{
		case 8:
		case 9:
		case 10:
		case 11:
			oakAsm->SXTH(OAK_WSCRATCH, value);
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[reg]))});
			break;

		case 15:
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[13]))});
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[12]))});
			oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[14]))});
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[13]))});
			oakStore32(value, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[14]))});
			oakStore32(value, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[15]))});
			break;

		case 16:
		case 17:
		case 18:
		case 19:
			oakAsm->AND(OAK_WSCRATCH, value, 0xffff);
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[reg]))});
			break;

		case 28:
			oakStore32(value, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[28]))});
			oakAsm->AND(OAK_WSCRATCH, value, 0x1f);
			oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, 7);
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[9]))});
			oakAsm->UBFX(OAK_WSCRATCH, value, 5, 5);
			oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, 7);
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[10]))});
			oakAsm->UBFX(OAK_WSCRATCH, value, 10, 5);
			oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, 7);
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[11]))});
			break;

		case 30:
			oakStore32(value, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[30]))});
			oakAsm->ASR(OAK_WSCRATCH2, value, 31);
			oakAsm->EOR(OAK_WSCRATCH, value, OAK_WSCRATCH2);
			oakAsm->CLZ(OAK_WSCRATCH, OAK_WSCRATCH);
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[31]))});
			break;

		default:
			oakStore32(value, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2D.r[reg]))});
			break;
	}
}

static void rgteMFC2_emit_oaknut()
{
	if (!_Rt_)
		return;

	const int rt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_WRITE);
	recBeginOaknutEmit();
	rgteMFC2Value_emit_oaknut(oakWRegister(rt), _Rd_);
	recEndOaknutEmit();
}

static void rgteMFC2()
{
	rgteMFC2_emit_oaknut();
}

static void rgteCFC2_emit_oaknut()
{
	if (!_Rt_)
		return;

	const int rt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_WRITE);
	recBeginOaknutEmit();
	oakLoad32(oakWRegister(rt), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[_Rd_]))});
	recEndOaknutEmit();
}

static void rgteCFC2()
{
	rgteCFC2_emit_oaknut();
}

static void rgteMTC2_emit_oaknut()
{
	const int rt = rpsxPrepareStoreOperand_emit_oaknut();
	recBeginOaknutEmit();
	rpsxCalcStoreOperand_emit_oaknut(rt);
	rgteMTC2Value_emit_oaknut(OAK_WARG2, _Rd_);
	recEndOaknutEmit();
}

static void rgteMTC2()
{
	rgteMTC2_emit_oaknut();
}

static void rgteCTC2_emit_oaknut()
{
	const int rt = rpsxPrepareStoreOperand_emit_oaknut();
	recBeginOaknutEmit();
	rpsxCalcStoreOperand_emit_oaknut(rt);
	oakStore32(OAK_WARG2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.CP2C.r[_Rd_]))});
	recEndOaknutEmit();
}

static void rgteCTC2()
{
	rgteCTC2_emit_oaknut();
}

static void rgteLWC2_emit_oaknut()
{
	const int addr_temp = rpsxCaptureAddressOperand_emit_oaknut();
	_psxFlushCall(FLUSH_FULLVTLB);

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));
	oakEmitCall(reinterpret_cast<const void*>(iopMemRead32));
	rgteMTC2Value_emit_oaknut(OAK_WARG1, _Rt_);
	recEndOaknutEmit();
}

static void rgteLWC2()
{
	rgteLWC2_emit_oaknut();
}

static void rgteSWC2_emit_oaknut()
{
	const int addr_temp = rpsxCaptureAddressOperand_emit_oaknut();
	const int value_temp = _allocX86reg(X86TYPE_TEMP, 0, MODE_CALLEESAVED);

	recBeginOaknutEmit();
	rgteMFC2Value_emit_oaknut(oakWRegister(value_temp), _Rt_);
	recEndOaknutEmit();

	_psxFlushCall(FLUSH_FULLVTLB);

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, oakWRegister(addr_temp));
	oakAsm->MOV(OAK_WARG2, oakWRegister(value_temp));
	oakEmitCall(reinterpret_cast<void*>(iopMemWrite32));
	recEndOaknutEmit();
}

static void rgteSWC2()
{
	rgteSWC2_emit_oaknut();
}


// R3000A tables
extern void (*rpsxBSC[64])();
extern void (*rpsxSPC[64])();
extern void (*rpsxREG[32])();
extern void (*rpsxCP0[32])();
extern void (*rpsxCP2[64])();
extern void (*rpsxCP2BSC[32])();

static void rpsxSPECIAL() { rpsxSPC[_Funct_](); }
static void rpsxREGIMM() { rpsxREG[_Rt_](); }
static void rpsxCOP0() { rpsxCP0[_Rs_](); }
static void rpsxCOP2()
{
	if (_Rs_ == 16)
		_psxFlushCall(0);
	rpsxCP2[_Funct_]();
}
static void rpsxBASIC() { rpsxCP2BSC[_Rs_](); }

static void rpsxNULL()
{
	Console.WriteLn("psxUNK: %8.8x", psxRegs.code);
}

// clang-format off
void (*rpsxBSC[64])() = {
	rpsxSPECIAL, rpsxREGIMM, rpsxJ   , rpsxJAL  , rpsxBEQ , rpsxBNE , rpsxBLEZ, rpsxBGTZ,
	rpsxADDI   , rpsxADDIU , rpsxSLTI, rpsxSLTIU, rpsxANDI, rpsxORI , rpsxXORI, rpsxLUI ,
	rpsxCOP0   , rpsxNULL  , rpsxCOP2, rpsxNULL , rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxNULL   , rpsxNULL  , rpsxNULL, rpsxNULL , rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxLB     , rpsxLH    , rpsxLWL , rpsxLW   , rpsxLBU , rpsxLHU , rpsxLWR , rpsxNULL,
	rpsxSB     , rpsxSH    , rpsxSWL , rpsxSW   , rpsxNULL, rpsxNULL, rpsxSWR , rpsxNULL,
	rpsxNULL   , rpsxNULL  , rgteLWC2, rpsxNULL , rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxNULL   , rpsxNULL  , rgteSWC2, rpsxNULL , rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
};

void (*rpsxSPC[64])() = {
	rpsxSLL , rpsxNULL, rpsxSRL , rpsxSRA , rpsxSLLV   , rpsxNULL , rpsxSRLV, rpsxSRAV,
	rpsxJR  , rpsxJALR, rpsxNULL, rpsxNULL, rpsxSYSCALL, rpsxBREAK, rpsxNULL, rpsxNULL,
	rpsxMFHI, rpsxMTHI, rpsxMFLO, rpsxMTLO, rpsxNULL   , rpsxNULL , rpsxNULL, rpsxNULL,
	rpsxMULT, rpsxMULTU, rpsxDIV, rpsxDIVU, rpsxNULL   , rpsxNULL , rpsxNULL, rpsxNULL,
	rpsxADD , rpsxADDU, rpsxSUB , rpsxSUBU, rpsxAND    , rpsxOR   , rpsxXOR , rpsxNOR ,
	rpsxNULL, rpsxNULL, rpsxSLT , rpsxSLTU, rpsxNULL   , rpsxNULL , rpsxNULL, rpsxNULL,
	rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL   , rpsxNULL , rpsxNULL, rpsxNULL,
	rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL   , rpsxNULL , rpsxNULL, rpsxNULL,
};

void (*rpsxREG[32])() = {
	rpsxBLTZ  , rpsxBGEZ  , rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxNULL  , rpsxNULL  , rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxBLTZAL, rpsxBGEZAL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxNULL  , rpsxNULL  , rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
};

void (*rpsxCP0[32])() = {
	rpsxMFC0, rpsxNULL, rpsxCFC0, rpsxNULL, rpsxMTC0, rpsxNULL, rpsxCTC0, rpsxNULL,
	rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxRFE , rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
};

void (*rpsxCP2[64])() = {
	rpsxBASIC, rgteRTPS , rpsxNULL , rpsxNULL, rpsxNULL, rpsxNULL , rgteNCLIP, rpsxNULL, // 00
	rpsxNULL , rpsxNULL , rpsxNULL , rpsxNULL, rgteOP  , rpsxNULL , rpsxNULL , rpsxNULL, // 08
	rgteDPCS , rgteINTPL, rgteMVMVA, rgteNCDS, rgteCDP , rpsxNULL , rgteNCDT , rpsxNULL, // 10
	rpsxNULL , rpsxNULL , rpsxNULL , rgteNCCS, rgteCC  , rpsxNULL , rgteNCS  , rpsxNULL, // 18
	rgteNCT  , rpsxNULL , rpsxNULL , rpsxNULL, rpsxNULL, rpsxNULL , rpsxNULL , rpsxNULL, // 20
	rgteSQR  , rgteDCPL , rgteDPCT , rpsxNULL, rpsxNULL, rgteAVSZ3, rgteAVSZ4, rpsxNULL, // 28
	rgteRTPT , rpsxNULL , rpsxNULL , rpsxNULL, rpsxNULL, rpsxNULL , rpsxNULL , rpsxNULL, // 30
	rpsxNULL , rpsxNULL , rpsxNULL , rpsxNULL, rpsxNULL, rgteGPF  , rgteGPL  , rgteNCCT, // 38
};

void (*rpsxCP2BSC[32])() = {
	rgteMFC2, rpsxNULL, rgteCFC2, rpsxNULL, rgteMTC2, rpsxNULL, rgteCTC2, rpsxNULL,
	rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
	rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL, rpsxNULL,
};
// clang-format on

////////////////////////////////////////////////
// Back-Prob Function Tables - Gathering Info //
////////////////////////////////////////////////
#define rpsxpropSetRead(reg) \
	{ \
		if (!(pinst->regs[reg] & EEINST_USED)) \
			pinst->regs[reg] |= EEINST_LASTUSE; \
		prev->regs[reg] |= EEINST_LIVE | EEINST_USED; \
		pinst->regs[reg] |= EEINST_USED; \
		_recFillRegister(*pinst, XMMTYPE_GPRREG, reg, 0); \
	}

#define rpsxpropSetWrite(reg) \
	{ \
		prev->regs[reg] &= ~(EEINST_LIVE | EEINST_USED); \
		if (!(pinst->regs[reg] & EEINST_USED)) \
			pinst->regs[reg] |= EEINST_LASTUSE; \
		pinst->regs[reg] |= EEINST_USED; \
		_recFillRegister(*pinst, XMMTYPE_GPRREG, reg, 1); \
	}

void rpsxpropBSC(EEINST* prev, EEINST* pinst);
void rpsxpropSPECIAL(EEINST* prev, EEINST* pinst);
void rpsxpropREGIMM(EEINST* prev, EEINST* pinst);
void rpsxpropCP0(EEINST* prev, EEINST* pinst);
void rpsxpropCP2(EEINST* prev, EEINST* pinst);

//SPECIAL, REGIMM, J   , JAL  , BEQ , BNE , BLEZ, BGTZ,
//ADDI   , ADDIU , SLTI, SLTIU, ANDI, ORI , XORI, LUI ,
//COP0   , NULL  , COP2, NULL , NULL, NULL, NULL, NULL,
//NULL   , NULL  , NULL, NULL , NULL, NULL, NULL, NULL,
//LB     , LH    , LWL , LW   , LBU , LHU , LWR , NULL,
//SB     , SH    , SWL , SW   , NULL, NULL, SWR , NULL,
//NULL   , NULL  , NULL, NULL , NULL, NULL, NULL, NULL,
//NULL   , NULL  , NULL, NULL , NULL, NULL, NULL, NULL
void rpsxpropBSC(EEINST* prev, EEINST* pinst)
{
	switch (psxRegs.code >> 26)
	{
		case 0:
			rpsxpropSPECIAL(prev, pinst);
			break;
		case 1:
			rpsxpropREGIMM(prev, pinst);
			break;
		case 2: // j
			break;
		case 3: // jal
			rpsxpropSetWrite(31);
			break;
		case 4: // beq
		case 5: // bne
			rpsxpropSetRead(_Rs_);
			rpsxpropSetRead(_Rt_);
			break;

		case 6: // blez
		case 7: // bgtz
			rpsxpropSetRead(_Rs_);
			break;

		case 15: // lui
			rpsxpropSetWrite(_Rt_);
			break;

		case 16:
			rpsxpropCP0(prev, pinst);
			break;
		case 18:
			rpsxpropCP2(prev, pinst);
			break;

		// stores
		case 40:
		case 41:
		case 42:
		case 43:
		case 46:
			rpsxpropSetRead(_Rt_);
			rpsxpropSetRead(_Rs_);
			break;

		case 50: // LWC2
		case 58: // SWC2
			// Operation on COP2 registers/memory. GPRs are left untouched
			break;

		default:
			rpsxpropSetWrite(_Rt_);
			rpsxpropSetRead(_Rs_);
			break;
	}
}

//SLL , NULL, SRL , SRA , SLLV   , NULL , SRLV, SRAV,
//JR  , JALR, NULL, NULL, SYSCALL, BREAK, NULL, NULL,
//MFHI, MTHI, MFLO, MTLO, NULL   , NULL , NULL, NULL,
//MULT, MULTU, DIV, DIVU, NULL   , NULL , NULL, NULL,
//ADD , ADDU, SUB , SUBU, AND    , OR   , XOR , NOR ,
//NULL, NULL, SLT , SLTU, NULL   , NULL , NULL, NULL,
//NULL, NULL, NULL, NULL, NULL   , NULL , NULL, NULL,
//NULL, NULL, NULL, NULL, NULL   , NULL , NULL, NULL,
void rpsxpropSPECIAL(EEINST* prev, EEINST* pinst)
{
	switch (_Funct_)
	{
		case 0: // SLL
		case 2: // SRL
		case 3: // SRA
			rpsxpropSetWrite(_Rd_);
			rpsxpropSetRead(_Rt_);
			break;

		case 8: // JR
			rpsxpropSetRead(_Rs_);
			break;
		case 9: // JALR
			rpsxpropSetWrite(_Rd_);
			rpsxpropSetRead(_Rs_);
			break;

		case 12: // syscall
		case 13: // break
			_recClearInst(prev);
			prev->info = 0;
			break;
		case 15: // sync
			break;

		case 16: // mfhi
			rpsxpropSetWrite(_Rd_);
			rpsxpropSetRead(PSX_HI);
			break;
		case 17: // mthi
			rpsxpropSetWrite(PSX_HI);
			rpsxpropSetRead(_Rs_);
			break;
		case 18: // mflo
			rpsxpropSetWrite(_Rd_);
			rpsxpropSetRead(PSX_LO);
			break;
		case 19: // mtlo
			rpsxpropSetWrite(PSX_LO);
			rpsxpropSetRead(_Rs_);
			break;

		case 24: // mult
		case 25: // multu
		case 26: // div
		case 27: // divu
			rpsxpropSetWrite(PSX_LO);
			rpsxpropSetWrite(PSX_HI);
			rpsxpropSetRead(_Rs_);
			rpsxpropSetRead(_Rt_);
			break;

		case 32: // add
		case 33: // addu
		case 34: // sub
		case 35: // subu
			rpsxpropSetWrite(_Rd_);
			if (_Rs_)
				rpsxpropSetRead(_Rs_);
			if (_Rt_)
				rpsxpropSetRead(_Rt_);
			break;

		default:
			rpsxpropSetWrite(_Rd_);
			rpsxpropSetRead(_Rs_);
			rpsxpropSetRead(_Rt_);
			break;
	}
}

//BLTZ  , BGEZ  , NULL, NULL, NULL, NULL, NULL, NULL,
//NULL  , NULL  , NULL, NULL, NULL, NULL, NULL, NULL,
//BLTZAL, BGEZAL, NULL, NULL, NULL, NULL, NULL, NULL,
//NULL  , NULL  , NULL, NULL, NULL, NULL, NULL, NULL
void rpsxpropREGIMM(EEINST* prev, EEINST* pinst)
{
	switch (_Rt_)
	{
		case 0: // bltz
		case 1: // bgez
			rpsxpropSetRead(_Rs_);
			break;

		case 16: // bltzal
		case 17: // bgezal
			// do not write 31
			rpsxpropSetRead(_Rs_);
			break;

			jNO_DEFAULT
	}
}

//MFC0, NULL, CFC0, NULL, MTC0, NULL, CTC0, NULL,
//NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
//RFE , NULL, NULL, NULL, NULL, NULL, NULL, NULL,
//NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
void rpsxpropCP0(EEINST* prev, EEINST* pinst)
{
	switch (_Rs_)
	{
		case 0: // mfc0
		case 2: // cfc0
			rpsxpropSetWrite(_Rt_);
			break;

		case 4: // mtc0
		case 6: // ctc0
			rpsxpropSetRead(_Rt_);
			break;
		case 16: // rfe
			break;

			jNO_DEFAULT
	}
}


// Basic table:
// gteMFC2, psxNULL, gteCFC2, psxNULL, gteMTC2, psxNULL, gteCTC2, psxNULL,
// psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
// psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
// psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL, psxNULL,
void rpsxpropCP2_basic(EEINST* prev, EEINST* pinst)
{
	switch (_Rs_)
	{
		case 0: // mfc2
		case 2: // cfc2
			rpsxpropSetWrite(_Rt_);
			break;

		case 4: // mtc2
		case 6: // ctc2
			rpsxpropSetRead(_Rt_);
			break;

		default:
			pxFail("iop invalid opcode in const propagation (rpsxpropCP2/BASIC)");
			break;
	}
}


// Main table:
// psxBASIC, gteRTPS , psxNULL , psxNULL, psxNULL, psxNULL , gteNCLIP, psxNULL, // 00
// psxNULL , psxNULL , psxNULL , psxNULL, gteOP  , psxNULL , psxNULL , psxNULL, // 08
// gteDPCS , gteINTPL, gteMVMVA, gteNCDS, gteCDP , psxNULL , gteNCDT , psxNULL, // 10
// psxNULL , psxNULL , psxNULL , gteNCCS, gteCC  , psxNULL , gteNCS  , psxNULL, // 18
// gteNCT  , psxNULL , psxNULL , psxNULL, psxNULL, psxNULL , psxNULL , psxNULL, // 20
// gteSQR  , gteDCPL , gteDPCT , psxNULL, psxNULL, gteAVSZ3, gteAVSZ4, psxNULL, // 28
// gteRTPT , psxNULL , psxNULL , psxNULL, psxNULL, psxNULL , psxNULL , psxNULL, // 30
// psxNULL , psxNULL , psxNULL , psxNULL, psxNULL, gteGPF  , gteGPL  , gteNCCT, // 38
void rpsxpropCP2(EEINST* prev, EEINST* pinst)
{
	switch (_Funct_)
	{
		case 0: // Basic opcode
			rpsxpropCP2_basic(prev, pinst);
			break;

		default:
			// COP2 operation are likely done with internal COP2 registers
			// No impact on GPR
			break;
	}
}
