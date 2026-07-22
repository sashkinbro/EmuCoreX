// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "arm64/iop/iR3000A-arm64.h"
#include "R3000A.h"
#include "arm64/ee/BaseblockEx-arm64.h"
#include "R5900OpcodeTables.h"
#include "IopBios.h"
#include "IopHw.h"
#include "Common.h"
#include "VMManager.h"

#include <time.h>

#ifndef _WIN32
#include <sys/types.h>
#endif

#include "arm64/ee/iCore-arm64.h"

#include "Config.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "JitProfiler.h"
#include "HangTrace.h"

#include "common/AlignedMalloc.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/Perf.h"
#include "DebugTools/Breakpoints.h"


#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

extern void psxBREAK();

u32 g_psxMaxRecMem = 0;

alignas(16) uptr psxRecLUT[0x10000];
u32 psxhwLUT[0x10000];

static __fi u32 HWADDR(u32 mem) { return psxhwLUT[mem >> 16] + mem; }

static BASEBLOCK* recRAM = nullptr; // and the ptr to the blocks here
static BASEBLOCK* recROM = nullptr; // and here
static BASEBLOCK* recROM1 = nullptr; // also here
static BASEBLOCK* recROM2 = nullptr; // also here
static BaseBlocks recBlocks;
static u8* recPtr = nullptr;
static u8* recPtrEnd = nullptr;
u32 psxpc; // recompiler psxpc
int psxbranch; // set for branch
u32 g_iopCyclePenalty;

static EEINST* s_pInstCache = nullptr;
static u32 s_nInstCacheSize = 0;

static BASEBLOCK* s_pCurBlock = nullptr;
static BASEBLOCKEX* s_pCurBlockEx = nullptr;

static u32 s_nEndBlock = 0; // what psxpc the current block ends
static u32 s_branchTo;
static bool s_nBlockFF;

static u32 s_saveConstRegs[32];
static u32 s_saveHasConstReg = 0, s_saveFlushedConstReg = 0;
static EEINST* s_psaveInstInfo = nullptr;

u32 s_psxBlockCycles = 0; // cycles of current block recompiling
static u32 s_savenBlockCycles = 0;
static bool s_recompilingDelaySlot = false;

static void iPsxBranchTest(u32 newpc, u32 cpuBranch);
//void psxRecompileNextInstruction(int delayslot);

extern void (*rpsxBSC[64])();
void rpsxpropBSC(EEINST* prev, EEINST* pinst);

static void iopClearRecLUT(BASEBLOCK* base, int count);

#define PSX_GETBLOCK(x) PC_GETBLOCK_(x, psxRecLUT)

#define PSXREC_CLEARM(mem) \
	(((mem) < g_psxMaxRecMem && (psxRecLUT[(mem) >> 16] + (mem))) ? \
			psxRecClearMem(mem) : \
            4)


// =====================================================================================================
//  Dynamically Compiled Dispatchers - R3000A style
// =====================================================================================================

static void iopRecRecompile(u32 startpc);

static const void* iopDispatcherEvent = nullptr;
static const void* iopDispatcherReg = nullptr;
static const void* iopJITCompile = nullptr;
static const void* iopEnterRecompiledCode = nullptr;
static const void* iopExitRecompiledCode = nullptr;

static void recEventTest()
{
	_cpuEventTest_Shared();
}

static void iopOakBeginStackFrame()
{
	using namespace oak::util;

	oakAsm->SUB(SP, SP, 144);
	oakAsm->STP(X19, X20, SP, oak::SOffset<10, 3>(32));
	oakAsm->STP(X21, X22, SP, oak::SOffset<10, 3>(48));
	oakAsm->STP(X23, X24, SP, oak::SOffset<10, 3>(64));
	oakAsm->STP(X25, X26, SP, oak::SOffset<10, 3>(80));
	oakAsm->STP(X27, X28, SP, oak::SOffset<10, 3>(96));
	oakAsm->STP(X29, X30, SP, oak::SOffset<10, 3>(112));
}

static void iopOakEndStackFrame()
{
	using namespace oak::util;

	oakAsm->LDP(X29, X30, SP, oak::SOffset<10, 3>(112));
	oakAsm->LDP(X27, X28, SP, oak::SOffset<10, 3>(96));
	oakAsm->LDP(X25, X26, SP, oak::SOffset<10, 3>(80));
	oakAsm->LDP(X23, X24, SP, oak::SOffset<10, 3>(64));
	oakAsm->LDP(X21, X22, SP, oak::SOffset<10, 3>(48));
	oakAsm->LDP(X19, X20, SP, oak::SOffset<10, 3>(32));
	oakAsm->ADD(SP, SP, 144);
}

static void iopOakLoadCurrentPc()
{
	oakLoad32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))});
}

static void iopOakEmitDispatcherRegBody()
{
	using namespace oak::util;

	iopOakLoadCurrentPc();
	oakAsm->LSR(W1, W0, 16);
	oakAsm->LDR(X1, X29, X1, oak::IndexExt::LSL, 3);
	oakAsm->LSR(W0, W0, 2);
	oakAsm->LDR(X0, X1, X0, oak::IndexExt::LSL, 3);
	oakAsm->BR(X0);
}

// The address for all cleared blocks.  It recompiles the current pc and then
// dispatches to the recompiled block address.
static const void* _DynGen_JITCompile()
{
	pxAssertMsg(iopDispatcherReg != NULL, "Please compile the DispatcherReg subroutine *before* JITComple.  Thanks.");

//	u8* retval = xGetPtr();
	u8* retval = oakGetCurrentCodePointer();

	iopOakLoadCurrentPc();
	oakEmitCall(reinterpret_cast<void*>(iopRecRecompile));


	iopOakEmitDispatcherRegBody();

	return retval;
}

// called when jumping to variable pc address
static const void* _DynGen_DispatcherReg()
{
//	u8* retval = xGetPtr();
	u8* retval = oakGetCurrentCodePointer();


	iopOakEmitDispatcherRegBody();

	return retval;
}

// --------------------------------------------------------------------------------------
//  EnterRecompiledCode  - dynamic compilation stub!
// --------------------------------------------------------------------------------------
static const void* _DynGen_EnterRecompiledCode()
{
	using namespace oak::util;

	// Optimization: The IOP never uses stack-based parameter invocation, so we can avoid
	// allocating any room on the stack for it (which is important since the IOP's entry
	// code gets invoked quite a lot).

	u8* retval = oakGetCurrentCodePointer();

	{ // Properly scope the frame prologue/epilogue
#ifdef ENABLE_VTUNE
		xScopedStackFrame frame(true, true);
#else
		iopOakBeginStackFrame();
#endif
		oakMoveAddressToReg(X26, iopMem->Main);
		oakMoveAddressToReg(X27, &g_cpuRegistersPack);
		oakMoveAddressToReg(X29, &psxRecLUT);

		oakEmitJmp(iopDispatcherReg);

		// Save an exit point
		iopExitRecompiledCode = oakGetCurrentCodePointer();

		iopOakEndStackFrame();
	}

	oakAsm->RET();

	return retval;
}

static void _DynGen_Dispatchers()
{
//	const u8* start = xGetAlignedCallTarget();
	const u8* start = oakGetCurrentCodePointer();

	// Place the EventTest and DispatcherReg stuff at the top, because they get called the
	// most and stand to benefit from strong alignment and direct referencing.
	iopDispatcherEvent = oakGetCurrentCodePointer();
	oakEmitCall(reinterpret_cast<void *>(recEventTest));
	iopDispatcherReg = _DynGen_DispatcherReg();

	iopJITCompile = _DynGen_JITCompile();
	iopEnterRecompiledCode = _DynGen_EnterRecompiledCode();

	recBlocks.SetJITCompile(iopJITCompile);

	Perf::any.Register(start, oakGetCurrentCodePointer() - start, "IOP Dispatcher Oaknut");
}

////////////////////////////////////////////////////
using namespace R3000A;

#define IOP_CPU(field) OakMemOperand{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, field))}

static constexpr oak::WReg OAK_EAX = oak::util::W0;
static constexpr oak::WReg OAK_ECX = oak::util::W1;
static constexpr oak::WReg OAK_EDX = oak::util::W2;
static constexpr oak::WReg OAK_EBX = oak::util::W3;
static constexpr oak::WReg OAK_EEX = oak::util::W4;

static bool iopOakIsCallerSaved(int id)
{
#if defined(__ANDROID__)
	return id <= 15;
#elif defined(_WIN32)
	return id <= 2 || (id >= 8 && id <= 11);
#else
	return id <= 2 || id == 6 || id == 7 || (id >= 8 && id <= 11);
#endif
}

void _psxFlushConstReg(int reg)
{
	if (PSX_IS_CONST1(reg) && !(g_psxFlushedConstReg & (1 << reg)))
	{
		oakAsm->MOV(OAK_WSCRATCH, g_psxConstRegs[reg]);
		oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.GPR.r[reg]));
		g_psxFlushedConstReg |= (1 << reg);
	}
}

void _psxFlushConstRegs()
{
	// TODO: Combine flushes

	int i;

	// flush constants

	// ignore r0
	for (i = 1; i < 32; ++i)
	{
		if (g_psxHasConstReg & (1 << i))
		{

			if (!(g_psxFlushedConstReg & (1 << i)))
			{
				oakAsm->MOV(OAK_WSCRATCH, g_psxConstRegs[i]);
				oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.GPR.r[i]));
				g_psxFlushedConstReg |= 1 << i;
			}

			if (g_psxHasConstReg == g_psxFlushedConstReg)
				break;
		}
	}
}

void _psxDeleteReg(int reg, int flush)
{
	if (!reg)
		return;
	if (flush && PSX_IS_CONST1(reg))
		_psxFlushConstReg(reg);

	PSX_DEL_CONST(reg);
	_deletePSXtoX86reg(reg, flush ? DELETE_REG_FREE : DELETE_REG_FREE_NO_WRITEBACK);
}

void _psxMoveGPRtoR(oak::WReg to, int fromgpr)
{
	if (PSX_IS_CONST1(fromgpr))
	{
		oakAsm->MOV(to, g_psxConstRegs[fromgpr]);
	}
	else
	{
		const int reg = EEINST_USEDTEST(fromgpr) ? _allocX86reg(X86TYPE_PSX, fromgpr, MODE_READ) : _checkX86reg(X86TYPE_PSX, fromgpr, MODE_READ);
		if (reg >= 0)
			oakAsm->MOV(to, oakWRegister(reg));
		else
			oakLoad32(to, IOP_CPU(psxRegs.GPR.r[fromgpr]));
	}
}

void _psxMoveGPRtoM(OakMemOperand to, int fromgpr)
{
	if (PSX_IS_CONST1(fromgpr))
	{
		oakAsm->MOV(OAK_WSCRATCH, g_psxConstRegs[fromgpr]);
		oakStore32(OAK_WSCRATCH, to);
	}
	else
	{
		const int reg = EEINST_USEDTEST(fromgpr) ? _allocX86reg(X86TYPE_PSX, fromgpr, MODE_READ) : _checkX86reg(X86TYPE_PSX, fromgpr, MODE_READ);
		if (reg >= 0)
			oakStore32(oakWRegister(reg), to);
		else
		{
			oakLoad32(OAK_EAX, IOP_CPU(psxRegs.GPR.r[fromgpr]));
			oakStore32(OAK_EAX, to);
		}
	}
}

void _psxFlushCall(int flushtype)
{
	// Free ARM64 host registers that are not saved across function calls:
	for (u32 i = 0; i < iREGCNT_GPR; i++)
	{
		if (!x86regs[i].inuse)
			continue;

		if (iopOakIsCallerSaved(i) ||
			((flushtype & FLUSH_FREE_NONTEMP_X86) && x86regs[i].type != X86TYPE_TEMP) ||
			((flushtype & FLUSH_FREE_TEMP_X86) && x86regs[i].type == X86TYPE_TEMP))
		{
			_freeX86reg(i);
		}
	}

	if (flushtype & FLUSH_ALL_X86)
		_flushX86regs();

	if (flushtype & FLUSH_CONSTANT_REGS)
		_psxFlushConstRegs();

	if ((flushtype & FLUSH_PC) /*&& !g_cpuFlushedPC*/)
	{
		oakAsm->MOV(OAK_WSCRATCH, psxpc);
		oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.pc));
		//g_cpuFlushedPC = true;
	}
}

void _psxFlushAllDirty()
{
	// TODO: Combine flushes
	for (u32 i = 0; i < 32; ++i)
	{
		if (PSX_IS_CONST1(i))
			_psxFlushConstReg(i);
	}

	_flushX86regs();
}

void psxSaveBranchState()
{
	s_savenBlockCycles = s_psxBlockCycles;
	memcpy(s_saveConstRegs, g_psxConstRegs, sizeof(g_psxConstRegs));
	s_saveHasConstReg = g_psxHasConstReg;
	s_saveFlushedConstReg = g_psxFlushedConstReg;
	s_psaveInstInfo = g_pCurInstInfo;

	// save all regs
	memcpy(s_saveX86regs, x86regs, sizeof(x86regs));
}

void psxLoadBranchState()
{
	s_psxBlockCycles = s_savenBlockCycles;

	memcpy(g_psxConstRegs, s_saveConstRegs, sizeof(g_psxConstRegs));
	g_psxHasConstReg = s_saveHasConstReg;
	g_psxFlushedConstReg = s_saveFlushedConstReg;
	g_pCurInstInfo = s_psaveInstInfo;

	// restore all regs
	memcpy(x86regs, s_saveX86regs, sizeof(x86regs));
}

////////////////////
// Code Templates //
////////////////////

void _psxOnWriteReg(int reg)
{
	PSX_DEL_CONST(reg);
}

bool psxTrySwapDelaySlot(u32 rs, u32 rt, u32 rd)
{
#if 1
	if (s_recompilingDelaySlot)
		return false;

	const u32 opcode_encoded = iopMemRead32(psxpc);
	if (opcode_encoded == 0)
	{
		psxRecompileNextInstruction(true, true);
		return true;
	}

	const u32 opcode_rs = ((opcode_encoded >> 21) & 0x1F);
	const u32 opcode_rt = ((opcode_encoded >> 16) & 0x1F);
	const u32 opcode_rd = ((opcode_encoded >> 11) & 0x1F);

	switch (opcode_encoded >> 26)
	{
		case 8: // ADDI
		case 9: // ADDIU
		case 10: // SLTI
		case 11: // SLTIU
		case 12: // ANDIU
		case 13: // ORI
		case 14: // XORI
		case 15: // LUI
		case 32: // LB
		case 33: // LH
		case 34: // LWL
		case 35: // LW
		case 36: // LBU
		case 37: // LHU
		case 38: // LWR
		case 39: // LWU
		case 40: // SB
		case 41: // SH
		case 42: // SWL
		case 43: // SW
		case 46: // SWR
		{
			if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && (rd == opcode_rs || rd == opcode_rt)))
				goto is_unsafe;
		}
		break;

		case 50: // LWC2
		case 58: // SWC2
			break;

		case 0: // SPECIAL
		{
			switch (opcode_encoded & 0x3F)
			{
				case 0: // SLL
				case 2: // SRL
				case 3: // SRA
				case 4: // SLLV
				case 6: // SRLV
				case 7: // SRAV
				case 32: // ADD
				case 33: // ADDU
				case 34: // SUB
				case 35: // SUBU
				case 36: // AND
				case 37: // OR
				case 38: // XOR
				case 39: // NOR
				case 42: // SLT
				case 43: // SLTU
				{
					if ((rs != 0 && rs == opcode_rd) || (rt != 0 && rt == opcode_rd) || (rd != 0 && (rd == opcode_rs || rd == opcode_rt)))
						goto is_unsafe;
				}
				break;

				case 15: // SYNC
				case 24: // MULT
				case 25: // MULTU
				case 26: // DIV
				case 27: // DIVU
					break;

				default:
					goto is_unsafe;
			}
		}
		break;

		case 16: // COP0
		case 17: // COP1
		case 18: // COP2
		case 19: // COP3
		{
			switch ((opcode_encoded >> 21) & 0x1F)
			{
				case 0: // MFC0
				case 2: // CFC0
				{
					if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && rd == opcode_rt))
						goto is_unsafe;
				}
				break;

				case 4: // MTC0
				case 6: // CTC0
					break;

				default:
				{
					// swap when it's GTE
					if ((opcode_encoded >> 26) != 18)
						goto is_unsafe;
				}
				break;
			}
			break;
		}
		break;

		default:
			goto is_unsafe;
	}

	psxRecompileNextInstruction(true, true);
	return true;

is_unsafe:
	return false;
#else
	return false;
#endif
}

int psxTryRenameReg(int to, int from, int fromx86, int other, int xmminfo)
{
	// can't rename when in form Rd = Rs op Rt and Rd == Rs or Rd == Rt
	if ((xmminfo & XMMINFO_NORENAME) || fromx86 < 0 || to == from || to == other || !EEINST_RENAMETEST(from))
		return -1;


	// flush back when it's been modified
	if (x86regs[fromx86].mode & MODE_WRITE && EEINST_LIVETEST(from))
		_writebackX86Reg(fromx86);

	// remove all references to renamed-to register
	_deletePSXtoX86reg(to, DELETE_REG_FREE_NO_WRITEBACK);
	PSX_DEL_CONST(to);

	// and do the actual rename, new register has been modified.
	x86regs[fromx86].reg = to;
	x86regs[fromx86].mode |= MODE_READ | MODE_WRITE;
	return fromx86;
}

// rd = rs op rt
void psxRecompileCodeConst0(R3000AFNPTR constcode, R3000AFNPTR_INFO constscode, R3000AFNPTR_INFO consttcode, R3000AFNPTR_INFO noconstcode, int xmminfo)
{
	if (!_Rd_)
		return;

	if (PSX_IS_CONST2(_Rs_, _Rt_))
	{
		_deletePSXtoX86reg(_Rd_, DELETE_REG_FREE_NO_WRITEBACK);
		PSX_SET_CONST(_Rd_);
		constcode();
		return;
	}

	// we have to put these up here, because the register allocator below will wipe out const flags
	// for the destination register when/if it switches it to write mode.
	const bool s_is_const = PSX_IS_CONST1(_Rs_);
	const bool t_is_const = PSX_IS_CONST1(_Rt_);
	const bool d_is_const = PSX_IS_CONST1(_Rd_);
	const bool s_is_used = EEINST_USEDTEST(_Rs_);
	const bool t_is_used = EEINST_USEDTEST(_Rt_);

	if (!s_is_const)
		_addNeededGPRtoX86reg(_Rs_);
	if (!t_is_const)
		_addNeededGPRtoX86reg(_Rt_);
	if (!d_is_const)
		_addNeededGPRtoX86reg(_Rd_);

	u32 info = 0;
	int regs = _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	if (regs < 0 && ((!s_is_const && s_is_used) || _Rs_ == _Rd_))
		regs = _allocX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	if (regs >= 0)
		info |= PROCESS_EE_SET_S(regs);

	int regt = _checkX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
	if (regt < 0 && ((!t_is_const && t_is_used) || _Rt_ == _Rd_))
		regt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
	if (regt >= 0)
		info |= PROCESS_EE_SET_T(regt);

	// If S is no longer live, swap D for S. Saves the move.
	int regd = psxTryRenameReg(_Rd_, _Rs_, regs, _Rt_, xmminfo);
	if (regd < 0)
	{
		// TODO: If not live, write direct to memory.
		regd = _allocX86reg(X86TYPE_PSX, _Rd_, MODE_WRITE);
	}
	if (regd >= 0)
		info |= PROCESS_EE_SET_D(regd);

	_validateRegs();

	if (s_is_const && regs < 0)
	{
		// This *must* go inside the if, because of when _Rs_ =  _Rd_
		PSX_DEL_CONST(_Rd_);
		constscode(info /*| PROCESS_CONSTS*/);
		return;
	}

	if (t_is_const && regt < 0)
	{
		PSX_DEL_CONST(_Rd_);
		consttcode(info /*| PROCESS_CONSTT*/);
		return;
	}

	PSX_DEL_CONST(_Rd_);
	noconstcode(info);
}

static void psxRecompileIrxImport()
{
	u32 import_table = irxImportTableAddr(psxpc - 4);
	u16 index = psxRegs.code & 0xffff;
	if (!import_table)
		return;

	const std::string libname = iopMemReadString(import_table + 12, 8);

	irxHLE hle = irxImportHLE(libname, index);
#ifdef PCSX2_DEVBUILD
	const irxDEBUG debug = irxImportDebug(libname, index);
#else
	const irxDEBUG debug = 0;
#endif

	if (!hle && !debug)
		return;

	oakAsm->MOV(OAK_WSCRATCH, psxRegs.code);
	oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.code));
	oakAsm->MOV(OAK_WSCRATCH, psxpc);
	oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.pc));
	_psxFlushCall(FLUSH_NODESTROY);

	if (debug)
		oakEmitCall(reinterpret_cast<const void*>(debug));

	if (hle)
	{
		oakEmitCall(reinterpret_cast<const void*>(hle));
		oakEmitCbnz(OAK_EAX, iopDispatcherReg);
	}
}

// rt = rs op imm16
void psxRecompileCodeConst1(R3000AFNPTR constcode, R3000AFNPTR_INFO noconstcode, int xmminfo)
{
	if (!_Rt_)
	{
		// check for iop module import table magic
		if (psxRegs.code >> 16 == 0x2400)
			psxRecompileIrxImport();
		return;
	}

	if (PSX_IS_CONST1(_Rs_))
	{
		_deletePSXtoX86reg(_Rt_, DELETE_REG_FREE_NO_WRITEBACK);
		PSX_SET_CONST(_Rt_);
		constcode();
		return;
	}

	_addNeededPSXtoX86reg(_Rs_);
	_addNeededPSXtoX86reg(_Rt_);

	u32 info = 0;

	const bool s_is_used = EEINST_USEDTEST(_Rs_);
	const int regs = s_is_used ? _allocX86reg(X86TYPE_PSX, _Rs_, MODE_READ) : _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	if (regs >= 0)
		info |= PROCESS_EE_SET_S(regs);

	int regt = psxTryRenameReg(_Rt_, _Rs_, regs, 0, xmminfo);
	if (regt < 0)
	{
		regt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_WRITE);
	}
	if (regt >= 0)
		info |= PROCESS_EE_SET_T(regt);

	_validateRegs();

	PSX_DEL_CONST(_Rt_);
	noconstcode(info);
}

// rd = rt op sa
void psxRecompileCodeConst2(R3000AFNPTR constcode, R3000AFNPTR_INFO noconstcode, int xmminfo)
{
	if (!_Rd_)
		return;

	if (PSX_IS_CONST1(_Rt_))
	{
		_deletePSXtoX86reg(_Rd_, DELETE_REG_FREE_NO_WRITEBACK);
		PSX_SET_CONST(_Rd_);
		constcode();
		return;
	}

	_addNeededPSXtoX86reg(_Rt_);
	_addNeededPSXtoX86reg(_Rd_);

	u32 info = 0;
	const bool s_is_used = EEINST_USEDTEST(_Rt_);
	const int regt = s_is_used ? _allocX86reg(X86TYPE_PSX, _Rt_, MODE_READ) : _checkX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
	if (regt >= 0)
		info |= PROCESS_EE_SET_T(regt);

	int regd = psxTryRenameReg(_Rd_, _Rt_, regt, 0, xmminfo);
	if (regd < 0)
	{
		regd = _allocX86reg(X86TYPE_PSX, _Rd_, MODE_WRITE);
	}
	if (regd >= 0)
		info |= PROCESS_EE_SET_D(regd);

	_validateRegs();

	PSX_DEL_CONST(_Rd_);
	noconstcode(info);
}

// rd = rt MULT rs  (SPECIAL)
void psxRecompileCodeConst3(R3000AFNPTR constcode, R3000AFNPTR_INFO constscode, R3000AFNPTR_INFO consttcode, R3000AFNPTR_INFO noconstcode, int LOHI)
{
	if (PSX_IS_CONST2(_Rs_, _Rt_))
	{
		if (LOHI)
		{
			_deletePSXtoX86reg(PSX_LO, DELETE_REG_FREE_NO_WRITEBACK);
			_deletePSXtoX86reg(PSX_HI, DELETE_REG_FREE_NO_WRITEBACK);
		}

		constcode();
		return;
	}

	// we have to put these up here, because the register allocator below will wipe out const flags
	// for the destination register when/if it switches it to write mode.
	const bool s_is_const = PSX_IS_CONST1(_Rs_);
	const bool t_is_const = PSX_IS_CONST1(_Rt_);
	const bool s_is_used = EEINST_USEDTEST(_Rs_);
	const bool t_is_used = EEINST_USEDTEST(_Rt_);

	if (!s_is_const)
		_addNeededGPRtoX86reg(_Rs_);
	if (!t_is_const)
		_addNeededGPRtoX86reg(_Rt_);
	if (LOHI)
	{
		if (EEINST_LIVETEST(PSX_LO))
			_addNeededPSXtoX86reg(PSX_LO);
		if (EEINST_LIVETEST(PSX_HI))
			_addNeededPSXtoX86reg(PSX_HI);
	}

	u32 info = 0;
	int regs = _checkX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	if (regs < 0 && !s_is_const && s_is_used)
		regs = _allocX86reg(X86TYPE_PSX, _Rs_, MODE_READ);
	if (regs >= 0)
		info |= PROCESS_EE_SET_S(regs);

	// need at least one in a register
	int regt = _checkX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
	if (regs < 0 || (regt < 0 && !t_is_const && t_is_used))
		regt = _allocX86reg(X86TYPE_PSX, _Rt_, MODE_READ);
	if (regt >= 0)
		info |= PROCESS_EE_SET_T(regt);

	if (LOHI)
	{
		// going to destroy lo/hi, so invalidate if we're writing it back to state
		const bool lo_is_used = EEINST_USEDTEST(PSX_LO);
		const int reglo = lo_is_used ? _allocX86reg(X86TYPE_PSX, PSX_LO, MODE_WRITE) : -1;
		if (reglo >= 0)
			info |= PROCESS_EE_SET_LO(reglo) | PROCESS_EE_LO;
		else
			_deletePSXtoX86reg(PSX_LO, DELETE_REG_FREE_NO_WRITEBACK);

		const bool hi_is_live = EEINST_USEDTEST(PSX_HI);
		const int reghi = hi_is_live ? _allocX86reg(X86TYPE_PSX, PSX_HI, MODE_WRITE) : -1;
		if (reghi >= 0)
			info |= PROCESS_EE_SET_HI(reghi) | PROCESS_EE_HI;
		else
			_deletePSXtoX86reg(PSX_HI, DELETE_REG_FREE_NO_WRITEBACK);
	}

	_validateRegs();

	if (s_is_const && regs < 0)
	{
		// This *must* go inside the if, because of when _Rs_ =  _Rd_
		constscode(info /*| PROCESS_CONSTS*/);
		return;
	}

	if (t_is_const && regt < 0)
	{
		consttcode(info /*| PROCESS_CONSTT*/);
		return;
	}

	noconstcode(info);
}

static u8* m_recBlockAlloc = NULL;

static const uint m_recBlockAllocSize =
	(((Ps2MemSize::IopRam + Ps2MemSize::Rom + Ps2MemSize::Rom1 + Ps2MemSize::Rom2) / 4) * sizeof(BASEBLOCK));

static void recReserve()
{
	recPtr = SysMemory::GetIOPRec();
	recPtrEnd = SysMemory::GetIOPRecEnd() - _64kb;

	// Goal: Allocate BASEBLOCKs for every possible branch target in IOP memory.
	// Any 4-byte aligned address makes a valid branch target as per MIPS design (all instructions are
	// always 4 bytes long).

	if (!m_recBlockAlloc)
	{
		// We're on 64-bit, if these memory allocations fail, we're in real trouble.
		m_recBlockAlloc = (u8*)_aligned_malloc(m_recBlockAllocSize, 4096);
		if (!m_recBlockAlloc)
			pxFailRel("Failed to allocate R3000A BASEBLOCK lookup tables");
	}

	u8* curpos = m_recBlockAlloc;
	recRAM = (BASEBLOCK*)curpos;
	curpos += (Ps2MemSize::IopRam / 4) * sizeof(BASEBLOCK);
	recROM = (BASEBLOCK*)curpos;
	curpos += (Ps2MemSize::Rom / 4) * sizeof(BASEBLOCK);
	recROM1 = (BASEBLOCK*)curpos;
	curpos += (Ps2MemSize::Rom1 / 4) * sizeof(BASEBLOCK);
	recROM2 = (BASEBLOCK*)curpos;
	curpos += (Ps2MemSize::Rom2 / 4) * sizeof(BASEBLOCK);

	pxAssertRel(!s_pInstCache, "InstCache not allocated");
	s_nInstCacheSize = 128;
	s_pInstCache = (EEINST*)malloc(sizeof(EEINST) * s_nInstCacheSize);
	if (!s_pInstCache)
		pxFailRel("Failed to allocate R3000 InstCache array.");
}

void recResetIOP()
{
	u8* const old_high_water = recPtr;
	oakSetAsmPtr(SysMemory::GetIOPRec(), _4kb);
	oakStartBlock();

	_DynGen_Dispatchers();

	recPtr = oakEndBlock();
	SysMemory::DiscardCodeCachePages(recPtr, old_high_water);

	iopClearRecLUT((BASEBLOCK*)m_recBlockAlloc,
		(((Ps2MemSize::IopRam + Ps2MemSize::Rom + Ps2MemSize::Rom1 + Ps2MemSize::Rom2) / 4)));

	for (int i = 0; i < 0x10000; i++)
		recLUT_SetPage(psxRecLUT, 0, 0, 0, i, 0);

	// IOP knows 64k pages, hence for the 0x10000's

	// The bottom 2 bits of PC are always zero, so we <<14 to "compress"
	// the pc indexer into it's lower common denominator.

	// We're only mapping 20 pages here in 4 places.
	// 0x80 comes from : (Ps2MemSize::IopRam / 0x10000) * 4

	for (int i = 0; i < 0x80; i++)
	{
		recLUT_SetPage(psxRecLUT, psxhwLUT, recRAM, 0x0000, i, i & 0x1f);
		recLUT_SetPage(psxRecLUT, psxhwLUT, recRAM, 0x8000, i, i & 0x1f);
		recLUT_SetPage(psxRecLUT, psxhwLUT, recRAM, 0xa000, i, i & 0x1f);
	}

	for (int i = 0x1fc0; i < 0x2000; i++)
	{
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM, 0x0000, i, i - 0x1fc0);
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM, 0x8000, i, i - 0x1fc0);
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM, 0xa000, i, i - 0x1fc0);
	}

	for (int i = 0x1e00; i < 0x1e40; i++)
	{
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM1, 0x0000, i, i - 0x1e00);
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM1, 0x8000, i, i - 0x1e00);
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM1, 0xa000, i, i - 0x1e00);
	}

	for (int i = 0x1e40; i < 0x1e48; i++)
	{
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM2, 0x0000, i, i - 0x1e40);
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM2, 0x8000, i, i - 0x1e40);
		recLUT_SetPage(psxRecLUT, psxhwLUT, recROM2, 0xa000, i, i - 0x1e40);
	}

	if (s_pInstCache)
		memset(s_pInstCache, 0, sizeof(EEINST) * s_nInstCacheSize);

	recBlocks.Reset();
	g_psxMaxRecMem = 0;

	psxbranch = 0;
}

static void recShutdown()
{
	safe_aligned_free(m_recBlockAlloc);

	safe_free(s_pInstCache);
	s_nInstCacheSize = 0;

	recPtr = nullptr;
	recPtrEnd = nullptr;
}

static void iopClearRecLUT(BASEBLOCK* base, int count)
{
	for (int i = 0; i < count; i++)
		base[i].SetFnptr((uptr)iopJITCompile);
}

static __noinline s32 recExecuteBlock(s32 eeCycles)
{
	psxRegs.iopBreak = 0;
	psxRegs.iopCycleEE = eeCycles;

#ifdef PCSX2_DEVBUILD
	//if (SysTrace.SIF.IsActive())
	//	SysTrace.IOP.R3000A.Write("Switching to IOP CPU for %d cycles", eeCycles);
#endif

	// [TODO] recExecuteBlock could be replaced by a direct call to the iopEnterRecompiledCode()
	//   (by assigning its address to the psxRec structure).  But for that to happen, we need
	//   to move iopBreak/iopCycleEE update code to emitted assembly code. >_<  --air

	// Likely Disasm, as borrowed from MSVC:

	// Entry:
	// 	mov         eax,dword ptr [esp+4]
	// 	mov         dword ptr [iopBreak (0E88DCCh)],0
	// 	mov         dword ptr [iopCycleEE (832A84h)],eax

	// Exit:
	// 	mov         ecx,dword ptr [iopBreak (0E88DCCh)]
	// 	mov         edx,dword ptr [iopCycleEE (832A84h)]
	// 	lea         eax,[edx+ecx]

	((void(*)())iopEnterRecompiledCode)();

	return psxRegs.iopBreak + psxRegs.iopCycleEE;
}

// Returns the offset to the next instruction after any cleared memory
static __fi u32 psxRecClearMem(u32 pc)
{
	BASEBLOCK* pblock;

	pblock = PSX_GETBLOCK(pc);
	// if ((u8*)iopJITCompile == pblock->GetFnptr())
	if (pblock->GetFnptr() == (uptr)iopJITCompile)
		return 4;

	pc = HWADDR(pc);

	u32 lowerextent = pc, upperextent = pc + 4;
	int blockidx = recBlocks.Index(pc);
	pxAssert(blockidx != -1);

	while (BASEBLOCKEX* pexblock = recBlocks[blockidx - 1])
	{
		if (pexblock->startpc + pexblock->size * 4 <= lowerextent)
			break;

		lowerextent = std::min(lowerextent, pexblock->startpc);
		blockidx--;
	}

	int toRemoveFirst = blockidx;

	while (BASEBLOCKEX* pexblock = recBlocks[blockidx])
	{
		if (pexblock->startpc >= upperextent)
			break;

		lowerextent = std::min(lowerextent, pexblock->startpc);
		upperextent = std::max(upperextent, pexblock->startpc + pexblock->size * 4);

		blockidx++;
	}

	if (toRemoveFirst != blockidx)
	{
		recBlocks.Remove(toRemoveFirst, (blockidx - 1));
	}

	iopClearRecLUT(PSX_GETBLOCK(lowerextent), (upperextent - lowerextent) / 4);

	return upperextent - pc;
}

static __fi void recClearIOP(u32 Addr, u32 Size)
{
	u32 pc = Addr;
	while (pc < Addr + (Size << 2)) // Size * 4
		pc += PSXREC_CLEARM(pc);
}

void psxSetBranchReg(u32 reg)
{
	psxbranch = 1;

	if (reg != 0xffffffff)
	{
		const bool swap = psxTrySwapDelaySlot(reg, 0, 0);

		if (!swap)
		{
			const int wbreg = _allocX86reg(X86TYPE_PCWRITEBACK, 0, MODE_WRITE | MODE_CALLEESAVED);
			const oak::WReg reg32 = oakWRegister(wbreg);

			_psxMoveGPRtoR(reg32, reg);

			psxRecompileNextInstruction(true, false);

			if (x86regs[wbreg].inuse && x86regs[wbreg].type == X86TYPE_PCWRITEBACK)
			{
				oakStore32(reg32, IOP_CPU(psxRegs.pc));
				x86regs[wbreg].inuse = 0;
			}
			else
			{
				oakLoad32(OAK_EAX, IOP_CPU(psxRegs.pcWriteback));
				oakStore32(OAK_EAX, IOP_CPU(psxRegs.pc));
			}
		}
		else
		{
			if (PSX_IS_DIRTY_CONST(reg) || _hasX86reg(X86TYPE_PSX, reg, 0))
			{
				const int x86reg = _allocX86reg(X86TYPE_PSX, reg, MODE_READ);
				oakStore32(oakWRegister(x86reg), IOP_CPU(psxRegs.pc));
			}
			else
			{
				_psxMoveGPRtoM(IOP_CPU(psxRegs.pc), reg);
			}
		}
	}

	_psxFlushCall(FLUSH_EVERYTHING);
	iPsxBranchTest(0xffffffff, 1);

	oakEmitJmp(iopDispatcherReg);
}

void psxSetBranchImm(u32 imm)
{
	psxbranch = 1;
	pxAssert(imm);

	// end the current block
	oakAsm->MOV(OAK_WSCRATCH, imm);
	oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.pc));

	_psxFlushCall(FLUSH_EVERYTHING);
	iPsxBranchTest(imm, imm <= psxpc);

	u8* link_patch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recBlocks.Link(HWADDR(imm), reinterpret_cast<s32*>(link_patch));
}

static __fi u32 psxScaleBlockCycles()
{
	return s_psxBlockCycles;
}

// AArch64 ADD/SUB immediates can only encode 12 bits, optionally shifted by 12.
// Long IOP blocks can exceed that range, so fall back to a register operand.
// OAK_WSCRATCH2 is safe here because all callers update OAK_EBX or OAK_WSCRATCH.
static void iPsxAddBlockCycles(oak::WReg reg, u32 cycles)
{
	if (oak::AddSubImm::is_valid(cycles))
		oakAsm->ADD(reg, reg, cycles);
	else
	{
		oakAsm->MOV(OAK_WSCRATCH2, cycles);
		oakAsm->ADD(reg, reg, OAK_WSCRATCH2);
	}
}

static void iPsxSubBlockCyclesAndSetFlags(oak::WReg reg, u32 cycles)
{
	if (oak::AddSubImm::is_valid(cycles))
		oakAsm->SUBS(reg, reg, cycles);
	else
	{
		oakAsm->MOV(OAK_WSCRATCH2, cycles);
		oakAsm->SUBS(reg, reg, OAK_WSCRATCH2);
	}
}

static void iPsxAddEECycles_emit_oaknut(u32 blockCycles);

static void iPsxAddEECycles(u32 blockCycles)
{
	iPsxAddEECycles_emit_oaknut(blockCycles);
}

static void iPsxAddEECycles_emit_oaknut(u32 blockCycles)
{
	if (!(psxHu32(HW_ICFG) & (1 << 3))) [[likely]]
	{
		oakLoad32(OAK_WSCRATCH, IOP_CPU(psxRegs.iopCycleEE));
		if (blockCycles != 0xFFFFFFFF)
			iPsxSubBlockCyclesAndSetFlags(OAK_WSCRATCH, blockCycles * 8);
		else
			oakAsm->SUBS(OAK_WSCRATCH, OAK_WSCRATCH, OAK_EAX);
		oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.iopCycleEE));
		return;
	}

	// F = gcd(PS2CLK, PSXCLK) = 230400
	const u32 cnum = 1280; // PS2CLK / F
	const u32 cdenom = 147; // PSXCLK / F

	if (blockCycles != 0xFFFFFFFF)
		oakAsm->MOV(OAK_EAX, blockCycles * cnum);
	oakLoad32(OAK_WSCRATCH2, IOP_CPU(psxRegs.iopCycleEECarry));
	oakAsm->ADD(OAK_EAX, OAK_EAX, OAK_WSCRATCH2);
	oakAsm->MOV(OAK_EEX, cdenom);
	oakAsm->UDIV(OAK_ECX, OAK_EAX, OAK_EEX);
	oakAsm->MSUB(OAK_EEX, OAK_ECX, OAK_EEX, OAK_EAX);
	oakStore32(OAK_EEX, IOP_CPU(psxRegs.iopCycleEECarry));
	oakLoad32(OAK_EEX, IOP_CPU(psxRegs.iopCycleEE));
	oakAsm->SUBS(OAK_EEX, OAK_EEX, OAK_ECX);
	oakStore32(OAK_EEX, IOP_CPU(psxRegs.iopCycleEE));
}

static void iPsxBranchTest(u32 newpc, u32 cpuBranch)
{
	u32 blockCycles = psxScaleBlockCycles();

	if (EmuConfig.Speedhacks.WaitLoop && s_nBlockFF && newpc == s_branchTo)
	{
		oakLoad32(OAK_EAX, IOP_CPU(psxRegs.cycle));
		oakAsm->MOV(OAK_ECX, OAK_EAX);
		oakLoad32(OAK_EDX, IOP_CPU(psxRegs.iopCycleEE));
		oakAsm->ADD(OAK_EDX, OAK_EDX, 7);
		oakAsm->ASR(OAK_EDX, OAK_EDX, 3);
		oakAsm->ADD(OAK_EAX, OAK_EAX, OAK_EDX);
		oakLoad32(OAK_EEX, IOP_CPU(psxRegs.iopNextEventCycle));
		oakAsm->CMP(OAK_EAX, OAK_EEX);
		oakAsm->CSEL(OAK_EAX, OAK_EEX, OAK_EAX, oak::Cond::HI);
		oakStore32(OAK_EAX, IOP_CPU(psxRegs.cycle));
		oakAsm->SUB(OAK_EAX, OAK_EAX, OAK_ECX);
		oakAsm->LSL(OAK_EAX, OAK_EAX, 3);
		iPsxAddEECycles(0xFFFFFFFF);
		oakEmitCondBranch(oak::Cond::LE, iopExitRecompiledCode);

		oakEmitCall(reinterpret_cast<const void*>(iopEventTest));

		if (newpc != 0xffffffff)
		{
			oakLoad32(OAK_WSCRATCH, IOP_CPU(psxRegs.pc));
			oakAsm->MOV(OAK_WSCRATCH2, newpc);
			oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
			oakEmitCondBranch(oak::Cond::NE, iopDispatcherReg);
		}
	}
	else
	{
		oakLoad32(OAK_EBX, IOP_CPU(psxRegs.cycle));
		iPsxAddBlockCycles(OAK_EBX, blockCycles);
		oakStore32(OAK_EBX, IOP_CPU(psxRegs.cycle));

		// jump if iopCycleEE <= 0  (iop's timeslice timed out, so time to return control to the EE)
		iPsxAddEECycles(blockCycles);
		oakEmitCondBranch(oak::Cond::LE, iopExitRecompiledCode);

		// check if an event is pending
		oakLoad32(OAK_EEX, IOP_CPU(psxRegs.iopNextEventCycle));
		oakAsm->SUBS(OAK_EBX, OAK_EBX, OAK_EEX);
		oak::Label nointerruptpending;
		oakAsm->B(oak::Cond::MI, nointerruptpending);

		oakEmitCall(reinterpret_cast<void*>(iopEventTest));

		if (newpc != 0xffffffff)
		{
			oakLoad32(OAK_WSCRATCH, IOP_CPU(psxRegs.pc));
			oakAsm->MOV(OAK_WSCRATCH2, newpc);
			oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
			oakEmitCondBranch(oak::Cond::NE, iopDispatcherReg);
		}

		oakAsm->l(nointerruptpending);
	}
}

static void rpsxSYSCALL_emit_oaknut();
static void rpsxBREAK_emit_oaknut();

void rpsxSYSCALL()
{
	rpsxSYSCALL_emit_oaknut();
}

static void rpsxSYSCALL_emit_oaknut()
{
	const u32 exception_pc = psxpc - 4;
	const u32 block_cycles = psxScaleBlockCycles();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, psxRegs.code);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.code))});
	oakAsm->MOV(OAK_WSCRATCH, exception_pc);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))});
	recEndOaknutEmit();

	_psxFlushCall(FLUSH_NODESTROY);

	recBeginOaknutEmit();
	oak::Label skip_cycle_update;
	oakAsm->MOV(OAK_WARG1, 0x20);
	oakAsm->MOV(OAK_WARG2, psxbranch == 1);
	oakEmitCall(reinterpret_cast<const void*>(psxException));
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))});
	oakAsm->MOV(OAK_WSCRATCH2, exception_pc);
	oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->B(oak::Cond::EQ, skip_cycle_update);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.cycle))});
	iPsxAddBlockCycles(OAK_WSCRATCH, block_cycles);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.cycle))});
	iPsxAddEECycles_emit_oaknut(block_cycles);
	oakEmitJmp(iopDispatcherReg);
	oakAsm->l(skip_cycle_update);
	recEndOaknutEmit();
}

void rpsxBREAK()
{
	rpsxBREAK_emit_oaknut();
}

static void rpsxBREAK_emit_oaknut()
{
	const u32 exception_pc = psxpc - 4;
	const u32 block_cycles = psxScaleBlockCycles();

	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, psxRegs.code);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.code))});
	oakAsm->MOV(OAK_WSCRATCH, exception_pc);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))});
	recEndOaknutEmit();

	_psxFlushCall(FLUSH_NODESTROY);

	recBeginOaknutEmit();
	oak::Label skip_cycle_update;
	oakAsm->MOV(OAK_WARG1, 0x24);
	oakAsm->MOV(OAK_WARG2, psxbranch == 1);
	oakEmitCall(reinterpret_cast<const void*>(psxException));
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.pc))});
	oakAsm->MOV(OAK_WSCRATCH2, exception_pc);
	oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->B(oak::Cond::EQ, skip_cycle_update);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.cycle))});
	iPsxAddBlockCycles(OAK_WSCRATCH, block_cycles);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, psxRegs.cycle))});
	iPsxAddEECycles_emit_oaknut(block_cycles);
	oakEmitJmp(iopDispatcherReg);
	oakAsm->l(skip_cycle_update);
	recEndOaknutEmit();
}

static bool psxDynarecCheckBreakpoint()
{
	u32 pc = psxRegs.pc;
	if (CBreakPoints::CheckSkipFirst(BREAKPOINT_IOP, pc) == pc)
		return false;

	int bpFlags = psxIsBreakpointNeeded(pc);
	bool hit = false;
	//check breakpoint at current pc
	if (bpFlags & 1)
	{
		auto cond = CBreakPoints::GetBreakPointCondition(BREAKPOINT_IOP, pc);
		if (cond == NULL || cond->Evaluate())
		{
			hit = true;
		}
	}
	//check breakpoint in delay slot
	if (bpFlags & 2)
	{
		auto cond = CBreakPoints::GetBreakPointCondition(BREAKPOINT_IOP, pc + 4);
		if (cond == NULL || cond->Evaluate())
			hit = true;
	}

	if (!hit)
		return false;

	CBreakPoints::SetBreakpointTriggered(true, BREAKPOINT_IOP);
	VMManager::SetPaused(true);

	// Exit the EE too.
	Cpu->ExitExecution();
	return true;
}

static bool psxDynarecMemcheck(size_t i)
{
	const u32 pc = psxRegs.pc;
	const u32 op = iopMemRead32(pc);
	const R5900::OPCODE& opcode = R5900::GetInstruction(op);
	auto mc = CBreakPoints::GetMemChecks(BREAKPOINT_IOP)[i];

	if (CBreakPoints::CheckSkipFirst(BREAKPOINT_IOP, pc) == pc)
		return false;

	if (mc.hasCond)
	{
		if (!mc.cond.Evaluate())
			return false;
	}

	CBreakPoints::SetBreakpointTriggered(true, BREAKPOINT_IOP);
	VMManager::SetPaused(true);

	// Exit the EE too.
	Cpu->ExitExecution();
	return true;
}

static void psxRecMemcheck(u32 op, u32 bits, bool store)
{
	_psxFlushCall(FLUSH_EVERYTHING | FLUSH_PC);

	// compute accessed address
	_psxMoveGPRtoR(OAK_ECX, (op >> 21) & 0x1F);
	const s32 offset = static_cast<s16>(op);
	if (offset > 0)
		oakAsm->ADD(OAK_ECX, OAK_ECX, static_cast<u32>(offset));
	else if (offset < 0)
		oakAsm->SUB(OAK_ECX, OAK_ECX, static_cast<u32>(-offset));

	oakAsm->MOV(OAK_EDX, OAK_ECX);
	oakAsm->ADD(OAK_EDX, OAK_EDX, bits >> 3);

	// ecx = access address
	// edx = access address+size

	auto checks = CBreakPoints::GetMemChecks(BREAKPOINT_IOP);
	for (size_t i = 0; i < checks.size(); i++)
	{
		if (checks[i].result == 0)
			continue;
		if ((checks[i].memCond & MEMCHECK_WRITE) == 0 && store)
			continue;
		if ((checks[i].memCond & MEMCHECK_READ) == 0 && !store)
			continue;

		// logic: memAddress < bpEnd && bpStart < memAddress+memSize

		oakAsm->MOV(OAK_EAX, checks[i].end);
		oakAsm->CMP(OAK_ECX, OAK_EAX);
		oak::Label next1;
		oakAsm->B(oak::Cond::GE, next1);

		oakAsm->MOV(OAK_EAX, checks[i].start);
		oakAsm->CMP(OAK_EAX, OAK_EDX);
		oak::Label next2;
		oakAsm->B(oak::Cond::GE, next2);

		// hit the breakpoint

		if (checks[i].result & MEMCHECK_BREAK)
		{
			oakAsm->MOV(OAK_WARG1, static_cast<u32>(i));
			oakEmitCall(reinterpret_cast<const void*>(psxDynarecMemcheck));
			oakEmitCbnz(OAK_EAX, iopExitRecompiledCode);
		}

		oakAsm->l(next1);
		oakAsm->l(next2);
	}
}

static void psxEncodeBreakpoint()
{
	if (psxIsBreakpointNeeded(psxpc) != 0)
	{
		_psxFlushCall(FLUSH_EVERYTHING | FLUSH_PC);
		oakEmitCall(reinterpret_cast<void*>(psxDynarecCheckBreakpoint));
		oakEmitCbnz(OAK_EAX, iopExitRecompiledCode);
	}
}

static void psxEncodeMemcheck()
{
	int needed = psxIsMemcheckNeeded(psxpc);
	if (needed == 0)
		return;

	u32 op = iopMemRead32(needed == 2 ? psxpc + 4 : psxpc);
	const R5900::OPCODE& opcode = R5900::GetInstruction(op);

	bool store = (opcode.flags & IS_STORE) != 0;
	switch (opcode.flags & MEMTYPE_MASK)
	{
		case MEMTYPE_BYTE:
			psxRecMemcheck(op, 8, store);
			break;
		case MEMTYPE_HALF:
			psxRecMemcheck(op, 16, store);
			break;
		case MEMTYPE_WORD:
			psxRecMemcheck(op, 32, store);
			break;
		case MEMTYPE_DWORD:
			psxRecMemcheck(op, 64, store);
			break;
	}
}

void psxRecompileNextInstruction(bool delayslot, bool swapped_delayslot)
{
	const u32 profiler_pc = psxpc;
	const int old_code = psxRegs.code;
	EEINST* old_inst_info = g_pCurInstInfo;
	s_recompilingDelaySlot = delayslot;

	// add breakpoint
	if (!delayslot)
	{
		// Broken on x64
		psxEncodeBreakpoint();
		psxEncodeMemcheck();
	}
	else
	{
		_clearNeededX86regs();
	}

	psxRegs.code = iopMemRead32(psxpc);
	JitProfiler::OpcodeRangeScope profiler_scope;
	if (JitProfiler::IsActive())
		profiler_scope.Begin(1, HWADDR(profiler_pc), static_cast<u32>(psxRegs.code));
	s_psxBlockCycles++;
	psxpc += 4;

	g_pCurInstInfo++;

	g_iopCyclePenalty = 0;
	rpsxBSC[psxRegs.code >> 26]();
	s_psxBlockCycles += g_iopCyclePenalty;

	if (!swapped_delayslot)
		_clearNeededX86regs();

	if (swapped_delayslot)
	{
		psxRegs.code = old_code;
		g_pCurInstInfo = old_inst_info;
	}

}


static void iopRecRecompile(const u32 startpc)
{
	u32 i;
	u32 willbranch3 = 0;

	// When upgrading the IOP, there are two resets, the second of which is a 'fake' reset
	// This second 'reset' involves UDNL calling SYSMEM and LOADCORE directly, resetting LOADCORE's modules
	// This detects when SYSMEM is called and clears the modules then
	if(startpc == 0x890)
	{
		R3000SymbolGuardian.ClearIrxModules();
	}

	// Inject IRX hack
	if (startpc == 0x1630 && EmuConfig.CurrentIRX.length() > 3)
	{
		if (iopMemRead32(0x20018) == 0x1F)
		{
			// FIXME do I need to increase the module count (0x1F -> 0x20)
			iopMemWrite32(0x20094, 0xbffc0000);
		}
	}

	pxAssert(startpc);

	// if recPtr reached the mem limit reset whole mem
	if (recPtr >= recPtrEnd)
	{
		recResetIOP();
	}

	oakSetAsmPtr(recPtr, _256kb);
	recPtr = oakStartBlock();

	s_pCurBlock = PSX_GETBLOCK(startpc);

	pxAssert(s_pCurBlock->GetFnptr() == (uptr)iopJITCompile);

	s_pCurBlockEx = recBlocks.Get(HWADDR(startpc));

	if (!s_pCurBlockEx || s_pCurBlockEx->startpc != HWADDR(startpc))
		s_pCurBlockEx = recBlocks.New(HWADDR(startpc), (uptr)recPtr);

	if (JitProfiler::IsActive())
	{
		JitProfiler::EmitBlockIncrement(&s_pCurBlockEx->execution_count);
	}
	if (HangTrace::IsActive())
	{
		const u32 first_code = iopMemRead32(startpc);
		HangTrace::EmitBlockTrace(HangTrace::CPU_IOP, HWADDR(startpc), first_code);
	}

	psxbranch = 0;

	s_pCurBlock->SetFnptr((uptr)oakGetCurrentCodePointer());
	s_psxBlockCycles = 0;

	// reset recomp state variables
	psxpc = startpc;
	g_psxHasConstReg = g_psxFlushedConstReg = 1;

	_initX86regs();

	if ((psxHu32(HW_ICFG) & 8) && (HWADDR(startpc) == 0xa0 || HWADDR(startpc) == 0xb0 || HWADDR(startpc) == 0xc0))
	{
		oakEmitCall(reinterpret_cast<void*>(psxBiosCall));
		oakEmitCbnz(OAK_EAX, iopDispatcherReg);
	}


	// go until the next branch
	i = startpc;
	s_nEndBlock = 0xffffffff;
	s_branchTo = -1;

	while (1)
	{
		BASEBLOCK* pblock = PSX_GETBLOCK(i);
		if (i != startpc && pblock->GetFnptr() != (uptr)iopJITCompile)
		{
			// branch = 3
			willbranch3 = 1;
			s_nEndBlock = i;
			break;
		}

		psxRegs.code = iopMemRead32(i);

		switch (psxRegs.code >> 26)
		{
			case 0: // special
				if (_Funct_ == 8 || _Funct_ == 9)
				{ // JR, JALR
					s_nEndBlock = i + 8;
					goto StartRecomp;
				}
				break;

			case 1: // regimm
				if (_Rt_ == 0 || _Rt_ == 1 || _Rt_ == 16 || _Rt_ == 17)
				{
					s_branchTo = _Imm_ * 4 + i + 4;
					if (s_branchTo > startpc && s_branchTo < i)
						s_nEndBlock = s_branchTo;
					else
						s_nEndBlock = i + 8;
					goto StartRecomp;
				}
				break;

			case 2: // J
			case 3: // JAL
				s_branchTo = (_InstrucTarget_ << 2) | ((i + 4) & 0xf0000000);
				s_nEndBlock = i + 8;
				goto StartRecomp;

			// branches
			case 4:
			case 5:
			case 6:
			case 7:
				s_branchTo = _Imm_ * 4 + i + 4;
				if (s_branchTo > startpc && s_branchTo < i)
					s_nEndBlock = s_branchTo;
				else
					s_nEndBlock = i + 8;
				goto StartRecomp;
		}

		i += 4;
	}

StartRecomp:

	s_nBlockFF = false;
	if (s_branchTo == startpc)
	{
		s_nBlockFF = true;
		for (i = startpc; i < s_nEndBlock; i += 4)
		{
			if (i != s_nEndBlock - 8)
			{
				switch (iopMemRead32(i))
				{
					case 0: // nop
						break;
					default:
						s_nBlockFF = false;
				}
			}
		}
	}

	// rec info //
	{
		EEINST* pcur;

		const u32 block_offset = (s_nEndBlock - startpc) >> 2;
		if (s_nInstCacheSize < block_offset + 1)
		{
			const u32 required_size = block_offset + 1;
			const u32 new_size = std::max(required_size, s_nInstCacheSize << 1);
			EEINST* new_cache = (EEINST*)malloc(sizeof(EEINST) * new_size);
			if (!new_cache)
				pxFailRel("Failed to allocate R3000A InstCache array");

			if (s_pInstCache && s_nInstCacheSize > 0)
				memcpy(new_cache, s_pInstCache, sizeof(EEINST) * s_nInstCacheSize);

			free(s_pInstCache);
			s_pInstCache = new_cache;
			s_nInstCacheSize = new_size;
		}

		pcur = s_pInstCache + block_offset;
		_recClearInst(pcur);
		pcur->info = 0;

		for (i = s_nEndBlock; i > startpc; i -= 4)
		{
			psxRegs.code = iopMemRead32(i - 4);
			pcur[-1] = pcur[0];
			rpsxpropBSC(pcur - 1, pcur);
			pcur--;
		}
	}

	g_pCurInstInfo = s_pInstCache;
	while (!psxbranch && psxpc < s_nEndBlock)
	{
		psxRecompileNextInstruction(false, false);
	}

	pxAssert((psxpc - startpc) >> 2 <= 0xffff);
	s_pCurBlockEx->size = (psxpc - startpc) >> 2;

	if (!(psxpc & 0x10000000))
		g_psxMaxRecMem = std::max((psxpc & ~0xa0000000), g_psxMaxRecMem);

	if (psxbranch == 2)
	{
		_psxFlushCall(FLUSH_EVERYTHING);

		iPsxBranchTest(0xffffffff, 1);

		oakEmitJmp(iopDispatcherReg);
	}
	else
	{
		if (psxbranch)
			pxAssert(!willbranch3);
		else
		{
			oakLoad32(OAK_WSCRATCH, IOP_CPU(psxRegs.cycle));
			iPsxAddBlockCycles(OAK_WSCRATCH, psxScaleBlockCycles());
			oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.cycle));
			iPsxAddEECycles(psxScaleBlockCycles());
		}

		if (willbranch3 || !psxbranch)
		{
			pxAssert(psxpc == s_nEndBlock);
			_psxFlushCall(FLUSH_EVERYTHING);
			oakAsm->MOV(OAK_WSCRATCH, psxpc);
			oakStore32(OAK_WSCRATCH, IOP_CPU(psxRegs.pc));
			u8* link_patch = oakGetCurrentCodePointer();
			oakAsm->NOP();
			recBlocks.Link(HWADDR(s_nEndBlock), reinterpret_cast<s32*>(link_patch));
			psxbranch = 3;
		}
	}

	pxAssert(oakGetCurrentCodePointer() < SysMemory::GetIOPRecEnd());

	pxAssert(oakGetCurrentCodePointer() - recPtr < _64kb);
	s_pCurBlockEx->x86size = oakGetCurrentCodePointer() - recPtr;

	Perf::iop.RegisterPC((void*)s_pCurBlockEx->fnptr, s_pCurBlockEx->x86size, s_pCurBlockEx->startpc);
	JitProfiler::RecordBlockCompile(1, s_pCurBlockEx->startpc, s_pCurBlockEx->size, s_pCurBlockEx->x86size);

	recPtr = oakEndBlock();

	pxAssert((g_psxHasConstReg & g_psxFlushedConstReg) == g_psxHasConstReg);

	s_pCurBlock = NULL;
	s_pCurBlockEx = NULL;
}

void IOP_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks)
{
	for (u32 i = 0; i < recBlocks.GetBlockCount(); i++)
	{
		BASEBLOCKEX* b = recBlocks[i];
		if (!b) continue;
		JitBlockProfile p;
		p.startpc = b->startpc;
		p.size = b->size;
		p.host_size = b->x86size;
		p.execution_count = b->execution_count;
		p.type = 1; // IOP
		outBlocks.push_back(p);
	}
}

R3000Acpu psxRec = {
	recReserve,
	recResetIOP,
	recExecuteBlock,
	recClearIOP,
	recShutdown,
};
