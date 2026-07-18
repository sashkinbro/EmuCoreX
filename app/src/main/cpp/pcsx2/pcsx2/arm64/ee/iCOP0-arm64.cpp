// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Important Note to Future Developers:
//   None of the COP0 instructions are really critical performance items,
//   so don't waste time converting any more them into recompiled code
//   unless it can make them nicely compact.  Calling the C versions will
//   suffice.

#include "Common.h"
#include "R5900OpcodeTables.h"
#include "arm64/ee/iR5900-arm64.h"
#include "iCOP0-arm64.h"
#include "arm64/OaknutHelpers-arm64.h"

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

static void recBC0BranchTest_emit_oaknut()
{
	// Do not remove this as a simple performance cleanup. On ARM64 this currently
	// acts as a required EE state barrier; removing it caused startup black screens.
	_eeFlushAllDirty();

	// COP0 branch conditionals are based on the following equation:
	//  (((psHu16(DMAC_STAT) | ~psHu16(DMAC_PCR)) & 0x3ff) == 0x3ff)
	// By De Morgan, this is equivalent to:
	//  ((psHu16(DMAC_PCR) & ~psHu16(DMAC_STAT) & 0x3ff) == 0)
	// BC0F checks if the statement is false, BC0T checks if the statement is true.
	// 32-bit loads are ok because the test masks to the lower 10 bits.

	recBeginOaknutEmit();
	oakMoveAddressToReg(oak::util::X4, &psHu32(DMAC_STAT));
	oakAsm->LDR(OAK_WSCRATCH, oak::util::X4);
	oakAsm->LDR(OAK_WSCRATCH2, oak::util::X4, oak::POffset<14, 2>(16));
	oakAsm->BIC(OAK_WSCRATCH2, OAK_WSCRATCH2, OAK_WSCRATCH);
	oakAsm->TST(OAK_WSCRATCH2, 0x3ff);
	recEndOaknutEmit();
}

void recBC0F()
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	const bool swap = TrySwapDelaySlot(0, 0, 0, false);
	recBC0BranchTest_emit_oaknut();
	recDoBranchImmOaknut(branchTo, oak::Cond::EQ, false, swap);
}

void recBC0T()
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	const bool swap = TrySwapDelaySlot(0, 0, 0, false);
	recBC0BranchTest_emit_oaknut();
	recDoBranchImmOaknut(branchTo, oak::Cond::NE, false, swap);
}

void recBC0FL()
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	recBC0BranchTest_emit_oaknut();
	recDoBranchImmOaknut(branchTo, oak::Cond::EQ, true, false);
}

void recBC0TL()
{
	const u32 branchTo = ((s32)_Imm_ * 4) + pc;
	recBC0BranchTest_emit_oaknut();
	recDoBranchImmOaknut(branchTo, oak::Cond::NE, true, false);
}

static void recTLBR_emit_oaknut()
{
	recBeginOaknutEmit();

	oak::Label done;

	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Index))});
	oakAsm->AND(oak::util::W5, oak::util::W5, 0x3f);
	oakAsm->CMP(oak::util::W5, 48);
	oakAsm->B(oak::Cond::GE, done);

	oakAsm->LSL(oak::util::X5, oak::util::X5, 4);
	oakMoveAddressToReg(oak::util::X4, tlb);
	oakAsm->ADD(oak::util::X4, oak::util::X4, oak::util::X5);

	oakLoad32(oak::util::W5, {oak::util::X4, static_cast<s64>(offsetof(tlbs, PageMask))});
	oakAsm->MOV(oak::util::W8, 0x01ffe000);
	oakAsm->AND(oak::util::W5, oak::util::W5, oak::util::W8);
	oakStore32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.PageMask))});

	oakLoad32(oak::util::W6, {oak::util::X4, static_cast<s64>(offsetof(tlbs, EntryHi))});
	oakAsm->MOV(oak::util::W7, 0x1f00);
	oakAsm->ORR(oak::util::W7, oak::util::W7, oak::util::W5);
	oakAsm->MVN(oak::util::W7, oak::util::W7);
	oakAsm->AND(oak::util::W6, oak::util::W6, oak::util::W7);
	oakStore32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.EntryHi))});

	oakLoad32(oak::util::W6, {oak::util::X4, static_cast<s64>(offsetof(tlbs, EntryLo0))});
	oakLoad32(oak::util::W7, {oak::util::X4, static_cast<s64>(offsetof(tlbs, EntryLo1))});
	oakAsm->AND(oak::util::W5, oak::util::W6, oak::util::W7);
	oakAsm->AND(oak::util::W5, oak::util::W5, 1);

	oakAsm->MOV(oak::util::W8, ~static_cast<u32>(0xfc000001));
	oakAsm->AND(oak::util::W6, oak::util::W6, oak::util::W8);
	oakAsm->ORR(oak::util::W6, oak::util::W6, oak::util::W5);
	oakStore32(oak::util::W6, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.EntryLo0))});

	oakAsm->MOV(oak::util::W8, ~static_cast<u32>(0x7c000001));
	oakAsm->AND(oak::util::W7, oak::util::W7, oak::util::W8);
	oakAsm->ORR(oak::util::W7, oak::util::W7, oak::util::W5);
	oakStore32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.EntryLo1))});

	oakAsm->l(done);
	recEndOaknutEmit();
}

void recTLBR()
{
	iFlushCall(FLUSH_INTERPRETER);
	recTLBR_emit_oaknut();
}

static void recTLBP_emit_oaknut()
{
	recBeginOaknutEmit();

	oak::Label done;

	oakLoad32(oak::util::W9, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.EntryHi))});
	oakAsm->AND(oak::util::W10, oak::util::W9, 0x7ffff);
	oakAsm->LSR(oak::util::W9, oak::util::W9, 24);
	oakMoveAddressToReg(oak::util::X4, tlb);

	for (u32 i = 0; i < 48; i++)
	{
		oak::Label next;
		oak::Label hit;
		const s64 tlb_offset = static_cast<s64>(sizeof(tlbs) * i);

		oakLoad32(oak::util::W5, {oak::util::X4, tlb_offset + static_cast<s64>(offsetof(tlbs, PageMask))});
		oakAsm->LSR(oak::util::W5, oak::util::W5, 13);
		oakAsm->AND(oak::util::W5, oak::util::W5, 0xfff);

		oakLoad32(oak::util::W6, {oak::util::X4, tlb_offset + static_cast<s64>(offsetof(tlbs, EntryHi))});
		oakAsm->LSR(oak::util::W7, oak::util::W6, 13);
		oakAsm->MOV(oak::util::W8, 0x7ffff);
		oakAsm->AND(oak::util::W7, oak::util::W7, oak::util::W8);
		oakAsm->MVN(oak::util::W8, oak::util::W5);
		oakAsm->AND(oak::util::W7, oak::util::W7, oak::util::W8);
		oakAsm->LSL(oak::util::W7, oak::util::W7, 13);
		oakAsm->AND(oak::util::W8, oak::util::W10, oak::util::W8);
		oakAsm->CMP(oak::util::W7, oak::util::W8);
		oakAsm->B(oak::Cond::NE, next);

		oakLoad32(oak::util::W7, {oak::util::X4, tlb_offset + static_cast<s64>(offsetof(tlbs, EntryLo0))});
		oakLoad32(oak::util::W8, {oak::util::X4, tlb_offset + static_cast<s64>(offsetof(tlbs, EntryLo1))});
		oakAsm->AND(oak::util::W7, oak::util::W7, oak::util::W8);
		oakAsm->TST(oak::util::W7, 1);
		oakAsm->B(oak::Cond::NE, hit);

		oakAsm->AND(oak::util::W7, oak::util::W6, 0xff);
		oakAsm->CMP(oak::util::W7, oak::util::W9);
		oakAsm->B(oak::Cond::NE, next);

		oakAsm->l(hit);
		oakAsm->MOV(oak::util::W7, i);
		oakStore32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Index))});
		oakAsm->B(done);

		oakAsm->l(next);
	}

	oakAsm->MOV(oak::util::W7, 0x80000000);
	oakStore32(oak::util::W7, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Index))});

	oakAsm->l(done);
	recEndOaknutEmit();
}

void recTLBP()
{
	iFlushCall(FLUSH_INTERPRETER);
	recTLBP_emit_oaknut();
}

static void recTLBWI_emit_oaknut()
{
	recBeginOaknutEmit();

	oak::Label done;

	recFlushReccycle();
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakStore32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	recReloadReccycle();

	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Index))});
	oakAsm->AND(oak::util::W5, oak::util::W5, 0x3f);
	oakAsm->CMP(oak::util::W5, 48);
	oakAsm->B(oak::Cond::GE, done);

	oakAsm->MOV(oak::util::W1, oak::util::W5);
	oakAsm->LSL(oak::util::X0, oak::util::X5, 4);
	oakMoveAddressToReg(oak::util::X4, tlb);
	oakAsm->ADD(oak::util::X0, oak::util::X4, oak::util::X0);
	oakEmitCall(reinterpret_cast<void*>(UnmapTLB));

	oakLoad32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Index))});
	oakAsm->AND(oak::util::W0, oak::util::W0, 0x3f);
	oakEmitCall(reinterpret_cast<void*>(WriteTLB));

	oakAsm->l(done);
	recEndOaknutEmit();
}

void recTLBWI()
{
	iFlushCall(FLUSH_INTERPRETER);
	recTLBWI_emit_oaknut();
	g_branch = 2;
}

static void recTLBWR_emit_oaknut()
{
	recBeginOaknutEmit();

	oak::Label done;

	recFlushReccycle();
	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakStore32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	recReloadReccycle();

	oakLoad32(oak::util::W5, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Random))});
	oakAsm->AND(oak::util::W5, oak::util::W5, 0x3f);
	oakAsm->CMP(oak::util::W5, 48);
	oakAsm->B(oak::Cond::GE, done);

	oakAsm->MOV(oak::util::W1, oak::util::W5);
	oakAsm->LSL(oak::util::X0, oak::util::X5, 4);
	oakMoveAddressToReg(oak::util::X4, tlb);
	oakAsm->ADD(oak::util::X0, oak::util::X4, oak::util::X0);
	oakEmitCall(reinterpret_cast<void*>(UnmapTLB));

	oakLoad32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Random))});
	oakAsm->AND(oak::util::W0, oak::util::W0, 0x3f);
	oakEmitCall(reinterpret_cast<void*>(WriteTLB));

	oakAsm->l(done);
	recEndOaknutEmit();
}

void recTLBWR()
{
	iFlushCall(FLUSH_INTERPRETER);
	recTLBWR_emit_oaknut();
	g_branch = 2;
}

static void recSetNextEventDelta4_inline_oaknut()
{
	oak::Label done;

	recFlushReccycle();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	oakAsm->SUB(OAK_WSCRATCH2, OAK_WSCRATCH2, OAK_WSCRATCH);
	oakAsm->CMP(OAK_WSCRATCH2, 4);
	oakAsm->B(oak::Cond::LE, done);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, 4);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	recReloadReccycle();

	oakAsm->l(done);
}

static void recSetNextEventDelta4_emit_oaknut()
{
	recBeginOaknutEmit();
	recSetNextEventDelta4_inline_oaknut();
	recEndOaknutEmit();
}

static void recERET_emit_oaknut()
{
	recBeginOaknutEmit();

	oak::Label error_level;
	oak::Label done;

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});
	oakAsm->MOV(OAK_WSCRATCH2, 0x4);
	oakAsm->TST(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->B(oak::Cond::NE, error_level);

	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.EPC))});
	oakStore32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	oakAsm->MOV(OAK_WSCRATCH2, ~static_cast<u32>(0x2));
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});
	oakAsm->B(done);

	oakAsm->l(error_level);
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.ErrorEPC))});
	oakStore32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	oakAsm->MOV(OAK_WSCRATCH2, ~static_cast<u32>(0x4));
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});

	oakAsm->l(done);
	recSetNextEventDelta4_inline_oaknut();
	recEndOaknutEmit();
}

void recERET()
{
	iFlushCall(FLUSH_INTERPRETER);
	recERET_emit_oaknut();
	g_branch = 2;
}

static void recEI_emit_oaknut()
{
	recBeginOaknutEmit();

	oak::Label enable_eie;
	oak::Label done;

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});
	oakAsm->MOV(OAK_WSCRATCH2, 0x20006);
	oakAsm->TST(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->B(oak::Cond::NE, enable_eie);

	oakAsm->MOV(OAK_WSCRATCH2, 0x18);
	oakAsm->TST(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->B(oak::Cond::NE, done);

	oakAsm->l(enable_eie);
	oakAsm->MOV(OAK_WSCRATCH2, 0x10000);
	oakAsm->ORR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});
	recSetNextEventDelta4_inline_oaknut();

	oakAsm->l(done);
	recEndOaknutEmit();
}

void recEI()
{
	iFlushCall(FLUSH_INTERPRETER);
	recEI_emit_oaknut();
	g_branch = 2;
}

static void recDI_emit_oaknut()
{
	recBeginOaknutEmit();

	oak::Label i_have_no_idea;
	oak::Label in_user_mode;

	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});
	oakAsm->MOV(OAK_WSCRATCH2, 0x20006);
	oakAsm->TST(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->B(oak::Cond::NE, i_have_no_idea);

	oakAsm->MOV(OAK_WSCRATCH2, 0x18);
	oakAsm->TST(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->B(oak::Cond::NE, in_user_mode);

	oakAsm->l(i_have_no_idea);
	oakAsm->MOV(OAK_WSCRATCH2, ~static_cast<u32>(0x10000));
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});

	oakAsm->l(in_user_mode);
	recEndOaknutEmit();
}

void recDI()
{
	//// No need to branch after disabling interrupts...

	//iFlushCall(0);



	// Fixes booting issues in the following games:
	// Jak X, Namco 50th anniversary, Spongebob the Movie, Spongebob Battle for Bikini Bottom,
	// The Incredibles, The Incredibles rize of the underminer, Soukou kihei armodyne, Garfield Saving Arlene, Tales of Fandom Vol. 2.
	if (!g_recompilingDelaySlot)
		recompileNextInstruction(false, false); // DI execution is delayed by one instruction

	recDI_emit_oaknut();
}


#ifndef CP0_RECOMPILE

REC_SYS(MFC0);
REC_SYS(MTC0);

#else

static u32 recCOP0TakePriorBlockCycles()
{
	// recompileNextInstruction() accounts for the current opcode before calling
	// its rec. COP0 timing registers observe the cycle at instruction entry, so
	// flush only preceding instructions and leave this opcode for the block tail.
	const u32 current_cycles = GetCurrentInstruction().cycles *
		(2 - ((cpuRegs.CP0.n.Config >> 18) & 0x1));
	pxAssert(s_nBlockCycles >= current_cycles);
	s_nBlockCycles -= current_cycles;
	// The generic scaler deliberately returns at least one cycle for a block.
	// There is no preceding block at all when this is zero, so do not invent one.
	const u32 prior_cycles = (s_nBlockCycles != 0) ? scaleblockcycles_clear() : 0;
	s_nBlockCycles += current_cycles;
	return prior_cycles;
}

static void recMFC0UpdateCount_emit_oaknut()
{
	const u32 block_cycles = recCOP0TakePriorBlockCycles();

	recBeginOaknutEmit();
	// Flush RECCYCLE to get accurate cycle in memory, then add block cycles
	recFlushReccycle();
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakAsm->ADD(OAK_WSCRATCH2, OAK_WSCRATCH2, block_cycles);
	oakStore32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});

	oakAsm->MOV(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastCOP0Cycle))});
	oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, oak::util::W4);
	oak::Label nonzero_increment;
	oakAsm->CBNZ(OAK_WSCRATCH, nonzero_increment);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, 1);
	oakAsm->l(nonzero_increment);

	oakLoad32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Count))});
	oakAsm->ADD(oak::util::W4, oak::util::W4, OAK_WSCRATCH);
	oakStore32(oak::util::W4, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Count))});
	oakStore32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastCOP0Cycle))});
	recReloadReccycle();
	recEndOaknutEmit();
}

static void recMFC0LoadSigned32_emit_oaknut(int host_reg, s64 offset)
{
	const oak::WReg dst_w = oakWRegister(host_reg);
	const oak::XReg dst_x = oakXRegister(host_reg);

	recBeginOaknutEmit();
	oakLoad32(dst_w, {oak::util::X27, offset});
	oakAsm->SXTW(dst_x, dst_w);
	recEndOaknutEmit();
}

static void recMTC0StoreConst32_emit_oaknut(s64 offset, u32 value)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, value);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, offset});
	recEndOaknutEmit();
}

static void recMTC0UpdateCycle_emit_oaknut(s64 last_cycle_offset = -1)
{
	const u32 block_cycles = recCOP0TakePriorBlockCycles();

	recBeginOaknutEmit();
	recFlushReccycle();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, block_cycles);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	if (last_cycle_offset >= 0)
		oakStore32(OAK_WSCRATCH, {oak::util::X27, last_cycle_offset});
	recReloadReccycle();
	recEndOaknutEmit();
}

static void recCOP0MoveConstToArg1_emit_oaknut(u32 value)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WARG1, value);
	recEndOaknutEmit();
}

static void recCOP0MoveGPRToArg1_emit_oaknut(int fromgpr)
{
	if (fromgpr == 0 || GPR_IS_CONST1(fromgpr))
	{
		recCOP0MoveConstToArg1_emit_oaknut(fromgpr == 0 ? 0 : g_cpuConstRegs[fromgpr].UL[0]);
		return;
	}

	int gprreg = _checkX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
	int xmmreg = _checkXMMreg(XMMTYPE_GPRREG, fromgpr, MODE_READ);

	if (gprreg < 0 && xmmreg < 0)
	{
		if (EEINST_XMM_USEDTEST(fromgpr))
			xmmreg = _allocGPRtoXMMreg(fromgpr, MODE_READ);
		else if (EEINST_USEDTEST(fromgpr))
			gprreg = _allocX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
	}

	recBeginOaknutEmit();
	if (gprreg >= 0)
		oakAsm->MOV(OAK_WARG1, oakWRegister(gprreg));
	else if (xmmreg >= 0)
		oakAsm->FMOV(OAK_WARG1, oakSRegister(xmmreg));
	else
		oakLoad32(OAK_WARG1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[fromgpr].UL[0]))});
	recEndOaknutEmit();
}

static void recCOP0Call_emit_oaknut(const void* fn)
{
	recBeginOaknutEmit();
	recFlushReccycle();
	oakEmitCall(fn);
	recReloadReccycle();
	recEndOaknutEmit();
}

static void recMTC0StoreGPR32_emit_oaknut(s64 offset, int fromgpr)
{
	if (fromgpr == 0)
	{
		recMTC0StoreConst32_emit_oaknut(offset, 0);
		return;
	}

	if (GPR_IS_CONST1(fromgpr))
	{
		recMTC0StoreConst32_emit_oaknut(offset, g_cpuConstRegs[fromgpr].UL[0]);
		return;
	}

	int gprreg = _checkX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
	int xmmreg = _checkXMMreg(XMMTYPE_GPRREG, fromgpr, MODE_READ);

	if (gprreg < 0 && xmmreg < 0)
	{
		if (EEINST_XMM_USEDTEST(fromgpr))
			xmmreg = _allocGPRtoXMMreg(fromgpr, MODE_READ);
		else if (EEINST_USEDTEST(fromgpr))
			gprreg = _allocX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
	}

	recBeginOaknutEmit();
	if (gprreg >= 0)
	{
		oakStore32(oakWRegister(gprreg), {oak::util::X27, offset});
	}
	else if (xmmreg >= 0)
	{
		oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(xmmreg));
		oakStore32(OAK_WSCRATCH, {oak::util::X27, offset});
	}
	else
	{
		oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[fromgpr].UL[0]))});
		oakStore32(OAK_WSCRATCH, {oak::util::X27, offset});
	}
	recEndOaknutEmit();
}

static void recMTC0Status_emit_oaknut(int rt)
{
	oak::Label check_pccr;
	oak::Label fast_path;
	oak::Label slow_path;
	oak::Label done;

	iFlushCall(FLUSH_INTERPRETER);
	recMTC0UpdateCycle_emit_oaknut();
	recCOP0MoveGPRToArg1_emit_oaknut(rt);

	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});
	oakAsm->TBZ(OAK_WSCRATCH, 2, check_pccr);

	oakAsm->l(fast_path);
	oakStore32(OAK_WARG1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Status))});
	recFlushReccycle();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastPERFCycle[0]))});
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastPERFCycle[1]))});
	recEndOaknutEmit();
	recSetNextEventDelta4_emit_oaknut();
	recBeginOaknutEmit();
	oakAsm->B(done);

	oakAsm->l(check_pccr);
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pccr))});
	oakAsm->TBNZ(OAK_WSCRATCH, 31, slow_path);
	oakAsm->B(fast_path);

	oakAsm->l(slow_path);
	recEndOaknutEmit();
	recCOP0Call_emit_oaknut(reinterpret_cast<void*>(WriteCP0Status));
	recBeginOaknutEmit();
	oakAsm->l(done);
	recEndOaknutEmit();
}

void recMFC0()
{
	if (_Rd_ == 9)
	{
		// This case needs to be handled even if the write-back is ignored (_Rt_ == 0 )
		recMFC0UpdateCount_emit_oaknut();

		if (!_Rt_)
			return;

		const int regt = _Rt_ ? _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE) : -1;
		recMFC0LoadSigned32_emit_oaknut(regt, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.r[_Rd_])));
		return;
	}

	if (!_Rt_)
		return;

	if (_Rd_ == 25)
	{
		if (0 == (_Imm_ & 1)) // MFPS, register value ignored
		{
			const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
			recMFC0LoadSigned32_emit_oaknut(regt, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pccr)));
		}
		else if (0 == (_Imm_ & 2)) // MFPC 0, only LSB of register matters
		{
			iFlushCall(FLUSH_INTERPRETER);
			recMTC0UpdateCycle_emit_oaknut();
			recCOP0Call_emit_oaknut(reinterpret_cast<void*>(COP0_UpdatePCCR));

			const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
			recMFC0LoadSigned32_emit_oaknut(regt, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pcr0)));
		}
		else // MFPC 1
		{
			iFlushCall(FLUSH_INTERPRETER);
			recMTC0UpdateCycle_emit_oaknut();
			recCOP0Call_emit_oaknut(reinterpret_cast<void*>(COP0_UpdatePCCR));

			const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
			recMFC0LoadSigned32_emit_oaknut(regt, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pcr1)));
		}

		return;
	}
	else if (_Rd_ == 24)
	{
		return;
	}

	const int regt = _allocX86reg(X86TYPE_GPR, _Rt_, MODE_WRITE);
	recMFC0LoadSigned32_emit_oaknut(regt, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.r[_Rd_])));
}

void recMTC0()
{
	if (GPR_IS_CONST1(_Rt_))
	{
		switch (_Rd_)
		{
			case 12:
				recMTC0Status_emit_oaknut(_Rt_);
				break;

			case 16:
				iFlushCall(FLUSH_INTERPRETER);
				recCOP0MoveConstToArg1_emit_oaknut(g_cpuConstRegs[_Rt_].UL[0]);
				recCOP0Call_emit_oaknut(reinterpret_cast<void*>(WriteCP0Config));
				break;

			case 9:
				recMTC0UpdateCycle_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastCOP0Cycle)));
				recMTC0StoreConst32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.r[9])), g_cpuConstRegs[_Rt_].UL[0]);
				break;

			case 25:
				if (0 == (_Imm_ & 1)) // MTPS
				{
					if (0 != (_Imm_ & 0x3E)) // only effective when the register is 0
						break;
					// Updates PCRs and sets the PCCR.
					iFlushCall(FLUSH_INTERPRETER);
					recMTC0UpdateCycle_emit_oaknut();
					recCOP0Call_emit_oaknut(reinterpret_cast<void*>(COP0_UpdatePCCR));
					recMTC0StoreConst32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pccr)), g_cpuConstRegs[_Rt_].UL[0]);
					recCOP0Call_emit_oaknut(reinterpret_cast<void*>(COP0_DiagnosticPCCR));
				}
				else if (0 == (_Imm_ & 2)) // MTPC 0, only LSB of register matters
				{
					recMTC0UpdateCycle_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastPERFCycle[0])));
					recMTC0StoreConst32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pcr0)), g_cpuConstRegs[_Rt_].UL[0]);
				}
				else // MTPC 1
				{
					recMTC0UpdateCycle_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastPERFCycle[1])));
					recMTC0StoreConst32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pcr1)), g_cpuConstRegs[_Rt_].UL[0]);
				}
				break;

			case 24:
				break;

			default:
				recMTC0StoreConst32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.r[_Rd_])), g_cpuConstRegs[_Rt_].UL[0]);
				break;
		}
	}
	else
	{
		switch (_Rd_)
		{
			case 12:
				recMTC0Status_emit_oaknut(_Rt_);
				break;

			case 16:
				iFlushCall(FLUSH_INTERPRETER);
				recCOP0MoveGPRToArg1_emit_oaknut(_Rt_);
				recCOP0Call_emit_oaknut(reinterpret_cast<const void*>(WriteCP0Config));
				break;

			case 9:
				recMTC0UpdateCycle_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastCOP0Cycle)));
				recMTC0StoreGPR32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.r[9])), _Rt_);
				break;

			case 25:
				if (0 == (_Imm_ & 1)) // MTPS
				{
					if (0 != (_Imm_ & 0x3E)) // only effective when the register is 0
						break;
					iFlushCall(FLUSH_INTERPRETER);
					recMTC0UpdateCycle_emit_oaknut();
					recCOP0Call_emit_oaknut(reinterpret_cast<void*>(COP0_UpdatePCCR));
					recMTC0StoreGPR32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pccr)), _Rt_);
					recCOP0Call_emit_oaknut(reinterpret_cast<void*>(COP0_DiagnosticPCCR));
				}
				else if (0 == (_Imm_ & 2)) // MTPC 0, only LSB of register matters
				{
					recMTC0UpdateCycle_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastPERFCycle[0])));
					recMTC0StoreGPR32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pcr0)), _Rt_);
				}
				else // MTPC 1
				{
					recMTC0UpdateCycle_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.lastPERFCycle[1])));
					recMTC0StoreGPR32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.PERF.n.pcr1)), _Rt_);
				}
				break;

			case 24:
				break;

			default:
				recMTC0StoreGPR32_emit_oaknut(static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.r[_Rd_])), _Rt_);
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
