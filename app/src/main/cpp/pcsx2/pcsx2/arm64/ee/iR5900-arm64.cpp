// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "CDVD/CDVD.h"
#include "DebugTools/Breakpoints.h"
#include "Elfheader.h"
#include "GS.h"
#include "Memory.h"
#include "Patch.h"
#include "R3000A.h"
#include "R5900OpcodeTables.h"
#include "VMManager.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "vtlb.h"
#include "arm64/ee/BaseblockEx-arm64.h"
#include "arm64/ee/iR5900-arm64.h"
#include "arm64/ee/iR5900Analysis-arm64.h"
#include "JitProfiler.h"
#include "HangTrace.h"

#include "common/AlignedMalloc.h"
#include "common/FastJmp.h"
#include "common/HeapArray.h"
#include "common/Perf.h"
#include "arm64/vu/microVU_Misc-arm64.h"

// Only for MOVQ workaround.
#if !defined(__ANDROID__)
#include "common/emitter/internal.h"
#endif


#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif
using namespace R5900;

static bool eeRecNeedsReset = false;
static bool eeCpuExecuting = false;
static bool eeRecExitRequested = false;

#define PC_GETBLOCK(x) PC_GETBLOCK_(x, recLUT)

u32 maxrecmem = 0;
alignas(16) static uptr recLUT[_64kb];
alignas(16) static u32 hwLUT[_64kb];

static __fi u32 HWADDR(u32 mem) { return hwLUT[mem >> 16] + mem; }

u32 s_nBlockCycles = 0; // cycles of current block recompiling
bool s_nBlockInterlocked = false; // Block is VU0 interlocked
u32 pc; // recompiler pc
int g_branch; // set for branch
alignas(16) GPR_reg64 g_cpuConstRegs[32] = {};
u32 g_cpuHasConstReg = 0, g_cpuFlushedConstReg = 0;
bool g_cpuFlushedPC, g_cpuFlushedCode, g_recompilingDelaySlot, g_maySignalException;

eeProfiler EE::Profiler;

////////////////////////////////////////////////////////////////
// Static Private Variables - R5900 Dynarec

#define ARM64_RECOMPILER

static DynamicHeapArray<u8, 4096> recRAMCopy;
static DynamicHeapArray<u8, 4096> recLutReserve_RAM;
static size_t recLutSize;
static bool extraRam;

static BASEBLOCK* recRAM = nullptr; // and the ptr to the blocks here
static BASEBLOCK* recROM = nullptr; // and here
static BASEBLOCK* recROM1 = nullptr; // also here
static BASEBLOCK* recROM2 = nullptr; // also here

static BaseBlocks recBlocks;
static u8* recPtr = nullptr;
static u8* recPtrEnd = nullptr;
EEINST* s_pInstCache = nullptr;
static u32 s_nInstCacheSize = 0;

static BASEBLOCK* s_pCurBlock = nullptr;
static BASEBLOCKEX* s_pCurBlockEx = nullptr;
u32 s_nEndBlock = 0; // what pc the current block ends
u32 s_branchTo;
static bool s_nBlockFF;

// save states for branches
GPR_reg64 s_saveConstRegs[32];
static u32 s_saveHasConstReg = 0, s_saveFlushedConstReg = 0;
static EEINST* s_psaveInstInfo = nullptr;

static u32 s_savenBlockCycles = 0;

static void iBranchTest(u32 newpc = 0xffffffff);
static void ClearRecLUT(BASEBLOCK* base, int count);
static u32 scaleblockcycles();
void recExitExecutionImmediate();
static void recExitExecution();



void _eeFlushAllDirty()
{
	_flushXMMregs();
	_flushX86regs();

	// flush constants, do them all at once for slightly better codegen
	_flushConstRegs(false);
}

static void eeMoveGPRZeroToR_emit_oaknut(int to)
{
	recBeginOaknutEmit();
	const oak::WReg to_w = oakWRegister(to);
	oakAsm->EOR(to_w, to_w, to_w);
	recEndOaknutEmit();
}

static void eeMoveGPRConstToR_emit_oaknut(int to, int fromgpr)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakWRegister(to), g_cpuConstRegs[fromgpr].UL[0]);
	recEndOaknutEmit();
}

static void eeMoveGPRCachedGprToR_emit_oaknut(int to, int x86reg)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakWRegister(to), oakWRegister(x86reg));
	recEndOaknutEmit();
}

static void eeMoveGPRCachedXmmToR_emit_oaknut(int to, int xmmreg)
{
	recBeginOaknutEmit();
	oakAsm->FMOV(oakWRegister(to), oakSRegister(xmmreg));
	recEndOaknutEmit();
}

static void eeMoveGPRMemoryToR_emit_oaknut(int to, int fromgpr)
{
	const s64 offset = static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UL[0]) + fromgpr * sizeof(GPR_reg));
	recBeginOaknutEmit();
	oakLoad32(oakWRegister(to), {oak::util::X27, offset});
	recEndOaknutEmit();
}

void _eeMoveGPRtoR(int to, int fromgpr, bool allow_preload)
{
	if (fromgpr == 0)
	{
		eeMoveGPRZeroToR_emit_oaknut(to);
	}
	else if (GPR_IS_CONST1(fromgpr))
	{
		eeMoveGPRConstToR_emit_oaknut(to, fromgpr);
	}
	else
	{
		int x86reg = _checkX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
		int xmmreg = _checkXMMreg(XMMTYPE_GPRREG, fromgpr, MODE_READ);

		if (allow_preload && x86reg < 0 && xmmreg < 0)
		{
			if (EEINST_XMM_USEDTEST(fromgpr))
				xmmreg = _allocGPRtoXMMreg(fromgpr, MODE_READ);
			else if (EEINST_USEDTEST(fromgpr))
				x86reg = _allocX86reg(X86TYPE_GPR, fromgpr, MODE_READ);
		}

		if (x86reg >= 0)
		{
			eeMoveGPRCachedGprToR_emit_oaknut(to, x86reg);
		}
		else if (xmmreg >= 0)
		{
			eeMoveGPRCachedXmmToR_emit_oaknut(to, xmmreg);
		}
		else
		{
			eeMoveGPRMemoryToR_emit_oaknut(to, fromgpr);
		}
	}
}

// RECCYCLE sync helpers: keep cpuRegs.cycle in memory consistent with W24 delta.
// W24 = cpuRegs.cycle - cpuRegs.nextEventCycle (pinned across blocks).
void recFlushReccycle()
{
	// cpuRegs.cycle = nextEventCycle + RECCYCLE
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	oakAsm->ADD(OAK_WSCRATCH2, OAK_WSCRATCH, oak::util::W24);
	oakStore32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
}

void recReloadReccycle()
{
	// RECCYCLE = cpuRegs.cycle - cpuRegs.nextEventCycle
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakLoad32(OAK_WSCRATCH2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	oakAsm->SUB(oak::util::W24, OAK_WSCRATCH, OAK_WSCRATCH2);
}

static void recBranchCallScheduleImmediateTest_emit_oaknut()
{
	recBeginOaknutEmit();
	recFlushReccycle();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	recReloadReccycle();
	recEndOaknutEmit();
}

// Use this to call into interpreter functions that require an immediate branchtest
// to be done afterward (anything that throws an exception or enables interrupts, etc).
void recBranchCall(void (*func)())
{
	// In order to make sure a branch test is performed, the nextBranchCycle is set
	// to the current cpu cycle.
	recBranchCallScheduleImmediateTest_emit_oaknut();
	recCall(func);
	g_branch = 2;
}

void recCall(void (*func)())
{
	iFlushCall(FLUSH_INTERPRETER);
	recBeginOaknutEmit();
	recFlushReccycle();
	oakEmitCall(reinterpret_cast<void*>(func));
	recReloadReccycle();
	recEndOaknutEmit();
}

// =====================================================================================================
//  R5900 Dispatchers
// =====================================================================================================

static void recRecompile(const u32 startpc);
static void dyna_block_discard(u32 start, u32 sz);
static void dyna_page_reset(u32 start, u32 sz);

static const void* DispatcherEvent = nullptr;
static const void* DispatcherReg = nullptr;
static const void* JITCompile = nullptr;
static const void* EnterRecompiledCode = nullptr;
static const void* DispatchBlockDiscard = nullptr;
static const void* DispatchPageReset = nullptr;

static void recEventTest()
{
	_cpuEventTest_Shared();

	if (eeRecExitRequested)
	{
		eeRecExitRequested = false;
		recExitExecution();
	}
}

static void oakLoadCurrentPc()
{
	oakLoad32(oak::util::W0, {oak::util::X27, offsetof(cpuRegistersPack, cpuRegs.pc)});
}

static void oakEmitDispatcherRegBody()
{
	using namespace oak::util;

	oakLoadCurrentPc();
	oakAsm->LSR(W1, W0, 16);
	oakAsm->LDR(X1, X29, X1, oak::IndexExt::LSL, 3);
	oakAsm->LSR(W0, W0, 2);
	oakAsm->LDR(X0, X1, X0, oak::IndexExt::LSL, 3);
	oakAsm->BR(X0);
}

static const void* _DynGen_JITCompileOaknut()
{
	pxAssertMsg(DispatcherReg != NULL, "Please compile the DispatcherReg subroutine *before* JITCompile.");

	oakAlignAsmPtr();
	u8* retval = oakGetCurrentCodePointer();

	oakLoadCurrentPc();
	oakEmitCall(reinterpret_cast<const void*>(recRecompile));
	oakEmitDispatcherRegBody();

	return retval;
}

static const void* _DynGen_DispatcherRegOaknut()
{
	u8* retval = oakGetCurrentCodePointer();
	oakEmitDispatcherRegBody();
	return retval;
}

static const void* _DynGen_DispatcherEventOaknut()
{
	using namespace oak::util;

	u8* retval = oakGetCurrentCodePointer();

	// Sync cpuRegs.cycle from RECCYCLE before calling event test:
	// cpuRegs.cycle = nextEventCycle + RECCYCLE (W24)
	oakLoad32(OAK_WSCRATCH, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	oakAsm->ADD(OAK_WSCRATCH2, OAK_WSCRATCH, W24);
	oakStore32(OAK_WSCRATCH2, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});

	oakEmitCall(reinterpret_cast<const void*>(recEventTest));

	// Reload RECCYCLE from memory after event processing:
	// RECCYCLE = cpuRegs.cycle - cpuRegs.nextEventCycle
	oakLoad32(OAK_WSCRATCH, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakLoad32(OAK_WSCRATCH2, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	oakAsm->SUB(W24, OAK_WSCRATCH, OAK_WSCRATCH2);

	return retval;
}

static const void* _DynGen_EnterRecompiledCodeOaknut()
{
	using namespace oak::util;

	pxAssertMsg(DispatcherReg, "Dynamically generated dispatchers are required prior to generating EnterRecompiledCode!");

	oakAlignAsmPtr();
	u8* retval = oakGetCurrentCodePointer();

#ifdef _WIN32
	static constexpr u32 stack_size = 32 + 8;
#else
	static constexpr u32 stack_size = 16;
#endif

	oakAsm->SUB(SP, SP, stack_size);

	oakMoveAddressToReg(X29, &recLUT);
	oakMoveAddressToReg(X28, &psxRegs);
	oakMoveAddressToReg(X27, &g_cpuRegistersPack);
	oakMoveAddressToReg(OAK_XVUCLAMP, &g_cpuRegistersPack.mVUglob);

	if (CHECK_FASTMEM)
		oakLoad64(oak::util::X19, {X27, offsetof(cpuRegistersPack, vtlbdata.fastmem_base)});

	// Pinned FPU clamp constants in callee-saved NEON registers (v8/v9 = s8/s9)
	// These survive all C function calls per AAPCS64 and provide zero-cost
	// clamp bounds for FMINNM/FMAXNM without per-instruction materialization.
	oakAsm->MOV(OAK_WSCRATCH, 0x7f7fffff);
	oakAsm->FMOV(oak::SReg(8), OAK_WSCRATCH);  // s8 = +FLT_MAX
	oakAsm->MOV(OAK_WSCRATCH, 0xff7fffff);
	oakAsm->FMOV(oak::SReg(9), OAK_WSCRATCH);  // s9 = -FLT_MAX

	// Pinned RECCYCLE register (X24): holds delta = cpuRegs.cycle - cpuRegs.nextEventCycle
	// Negative (MI) = no events pending, positive (PL) = events need processing.
	// Eliminates 2 memory loads + 1 store + 1 ALU per block (saves 4 insn/block).
	oakLoad32(OAK_WSCRATCH, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakLoad32(OAK_WSCRATCH2, {X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	oakAsm->SUB(W24, OAK_WSCRATCH, OAK_WSCRATCH2);

	oakEmitJmp(DispatcherReg);

	return retval;
}

static const void* _DynGen_DispatchBlockDiscardOaknut()
{
	u8* retval = oakGetCurrentCodePointer();
	oakEmitCall(reinterpret_cast<const void*>(dyna_block_discard));
	oakEmitJmp(DispatcherReg);
	return retval;
}

static const void* _DynGen_DispatchPageResetOaknut()
{
	u8* retval = oakGetCurrentCodePointer();
	oakEmitCall(reinterpret_cast<const void*>(dyna_page_reset));
	oakEmitJmp(DispatcherReg);
	return retval;
}

static void _DynGen_DispatchersOaknut()
{
	const u8* start = oakGetCurrentCodePointer();

	DispatcherEvent = _DynGen_DispatcherEventOaknut();
	DispatcherReg = _DynGen_DispatcherRegOaknut();

	JITCompile = _DynGen_JITCompileOaknut();
	EnterRecompiledCode = _DynGen_EnterRecompiledCodeOaknut();
	DispatchBlockDiscard = _DynGen_DispatchBlockDiscardOaknut();
	DispatchPageReset = _DynGen_DispatchPageResetOaknut();

	recBlocks.SetJITCompile(JITCompile);

	Perf::any.Register(start, static_cast<u32>(oakGetCurrentCodePointer() - start), "EE Dispatcher Oaknut");
}


//////////////////////////////////////////////////////////////////////////////////////////
//

static __ri void ClearRecLUT(BASEBLOCK* base, int memsize)
{
	for (int i = 0; i < memsize / (int)sizeof(uptr); i++)
		base[i].SetFnptr((uptr)JITCompile);
}

static void recReserveRAM()
{
	recLutSize = (Ps2MemSize::ExposedRam + Ps2MemSize::Rom + Ps2MemSize::Rom1 + Ps2MemSize::Rom2) * wordsize / 4;

	if (recRAMCopy.size() != Ps2MemSize::ExposedRam)
		recRAMCopy.resize(Ps2MemSize::ExposedRam);

	if (recLutReserve_RAM.size() != recLutSize)
		recLutReserve_RAM.resize(recLutSize);

	BASEBLOCK* basepos = reinterpret_cast<BASEBLOCK*>(recLutReserve_RAM.data());
	recRAM = basepos;
	basepos += (Ps2MemSize::ExposedRam / 4);
	recROM = basepos;
	basepos += (Ps2MemSize::Rom / 4);
	recROM1 = basepos;
	basepos += (Ps2MemSize::Rom1 / 4);
	recROM2 = basepos;
	basepos += (Ps2MemSize::Rom2 / 4);

	for (int i = 0; i < 0x10000; i++)
		recLUT_SetPage(recLUT, 0, 0, 0, i, 0);

	for (int i = 0x0000; i < (int)(Ps2MemSize::ExposedRam / 0x10000); i++)
	{
		recLUT_SetPage(recLUT, hwLUT, recRAM, 0x0000, i, i);
		recLUT_SetPage(recLUT, hwLUT, recRAM, 0x2000, i, i);
		recLUT_SetPage(recLUT, hwLUT, recRAM, 0x3000, i, i);
		recLUT_SetPage(recLUT, hwLUT, recRAM, 0x8000, i, i);
		recLUT_SetPage(recLUT, hwLUT, recRAM, 0xa000, i, i);
		recLUT_SetPage(recLUT, hwLUT, recRAM, 0xb000, i, i);
		recLUT_SetPage(recLUT, hwLUT, recRAM, 0xc000, i, i);
		recLUT_SetPage(recLUT, hwLUT, recRAM, 0xd000, i, i);
	}

	for (int i = 0x1fc0; i < 0x2000; i++)
	{
		recLUT_SetPage(recLUT, hwLUT, recROM, 0x0000, i, i - 0x1fc0);
		recLUT_SetPage(recLUT, hwLUT, recROM, 0x8000, i, i - 0x1fc0);
		recLUT_SetPage(recLUT, hwLUT, recROM, 0xa000, i, i - 0x1fc0);
	}

	for (int i = 0x1e00; i < 0x1e40; i++)
	{
		recLUT_SetPage(recLUT, hwLUT, recROM1, 0x0000, i, i - 0x1e00);
		recLUT_SetPage(recLUT, hwLUT, recROM1, 0x8000, i, i - 0x1e00);
		recLUT_SetPage(recLUT, hwLUT, recROM1, 0xa000, i, i - 0x1e00);
	}

	for (int i = 0x1e40; i < 0x1e80; i++)
	{
		recLUT_SetPage(recLUT, hwLUT, recROM2, 0x0000, i, i - 0x1e40);
		recLUT_SetPage(recLUT, hwLUT, recROM2, 0x8000, i, i - 0x1e40);
		recLUT_SetPage(recLUT, hwLUT, recROM2, 0xa000, i, i - 0x1e40);
	}
}

static void recReserve()
{
	recPtr = SysMemory::GetEERec();
	recPtrEnd = SysMemory::GetEERecEnd() - _64kb;
	recReserveRAM();

	pxAssertRel(!s_pInstCache, "InstCache not allocated");
	s_nInstCacheSize = 128;
	s_pInstCache = (EEINST*)malloc(sizeof(EEINST) * s_nInstCacheSize);
	if (!s_pInstCache)
		pxFailRel("Failed to allocate R5900 InstCache array");
}

// Manual protection follows the host page size. This matters on Android devices
// with 16 KiB pages, where protecting one host page covers four PS2 4 KiB pages.
alignas(16) static u16 manual_page[Ps2MemSize::TotalRam >> __pageshift];
alignas(16) static u32 manual_slow_page[Ps2MemSize::TotalRam >> __pageshift];
alignas(16) static u8 manual_counter[Ps2MemSize::TotalRam >> __pageshift];

////////////////////////////////////////////////////
static void recResetRaw()
{
	u8* const old_high_water = recPtr;
	if (CHECK_EXTRAMEM != extraRam)
	{
		recReserveRAM();
		extraRam = !extraRam;
	}

	EE::Profiler.Reset();

    oakSetAsmPtr(SysMemory::GetEERec(), _4kb);
    oakStartBlock();

	_DynGen_DispatchersOaknut();

	recPtr = oakEndBlock();

    // recVTLB => iR5900LoadStore
    recPtr = vtlb_DynGenDispatchers(recPtr);
	SysMemory::DiscardCodeCachePages(recPtr, old_high_water);

	ClearRecLUT(reinterpret_cast<BASEBLOCK*>(recLutReserve_RAM.data()), recLutSize);
	recRAMCopy.fill(0);

	maxrecmem = 0;

	if (s_pInstCache)
		memset(s_pInstCache, 0, sizeof(EEINST) * s_nInstCacheSize);

	recBlocks.Reset();
	vtlb_ClearLoadStoreInfo();

	g_branch = 0;

	memset(manual_page, 0, sizeof(manual_page));
	memset(manual_slow_page, 0, sizeof(manual_slow_page));
	memset(manual_counter, 0, sizeof(manual_counter));
}

void recShutdown()
{
	recRAMCopy.deallocate();
	recLutReserve_RAM.deallocate();

	recBlocks.Reset();

	recRAM = recROM = recROM1 = recROM2 = nullptr;

	safe_free(s_pInstCache);
	s_nInstCacheSize = 0;

	recPtr = nullptr;
	recPtrEnd = nullptr;
}

void recStep()
{
}

static fastjmp_buf m_SetJmp_StateCheck;

static void recExitExecution()
{
	recExitExecutionImmediate();
}

void recExitExecutionImmediate()
{
	fastjmp_jmp(&m_SetJmp_StateCheck, 1);
}

static void recSafeExitExecution()
{
	// If we're currently processing events, we can't safely jump out of the recompiler here, because we'll
	// leave things in an inconsistent state. So instead, we flag it for exiting once cpuEventTest() returns.
	// Exiting in the middle of a rec block with the registers unsaved would be a bad idea too..
	eeRecExitRequested = true;

	// Force an event test at the end of this block.
	if (!eeEventTestIsActive)
	{
		// EE is running.
		cpuRegs.nextEventCycle = 0;
	}
	else
	{
		// IOP might be running, so break out if so.
		if (psxRegs.iopCycleEE > 0)
		{
			psxRegs.iopBreak += psxRegs.iopCycleEE; // record the number of cycles the IOP didn't run.
			psxRegs.iopCycleEE = 0;
		}
	}
}

static void recResetEE()
{
	if (eeCpuExecuting)
	{
		// get outta here as soon as we can
		eeRecNeedsReset = true;
		recSafeExitExecution();
		return;
	}

	recResetRaw();
}

static void recCancelInstruction()
{
	pxFailRel("recCancelInstruction() called, this should never happen!");
}

static void recExecute()
{
	// Reset before we try to execute any code, if there's one pending.
	// We need to do this here, because if we reset while we're executing, it sets the "needs reset"
	// flag, which triggers a JIT exit (the fastjmp_set below), and eventually loops back here.
	if (eeRecNeedsReset)
	{
		eeRecNeedsReset = false;
		recResetRaw();
	}

	// setjmp will save the register context and will return 0
	// A call to longjmp will restore the context (included the eip/rip)
	// but will return the longjmp 2nd parameter (here 1)
	if (!fastjmp_set(&m_SetJmp_StateCheck))
	{
		eeCpuExecuting = true;
		((void (*)())EnterRecompiledCode)();

		// Generally unreachable code here ...
	}

	eeCpuExecuting = false;

	EE::Profiler.Print();
}

////////////////////////////////////////////////////
static void recSYSCALL_emit_oaknut();

void R5900::Dynarec::OpcodeImpl::recSYSCALL()
{
	EE::Profiler.EmitOp(eeOpcode::SYSCALL);
	if (GPR_IS_CONST1(3))
	{
		// If it's FlushCache or iFlushCache, we can skip it since we don't support cache in the JIT.
		if (g_cpuConstRegs[3].UC[0] == 0x64 || g_cpuConstRegs[3].UC[0] == 0x68)
		{
			// Emulate the amount of cycles it takes for the exception handlers to run
			// This number was found by using github.com/F0bes/flushcache-cycles
			s_nBlockCycles += 5650;
			return;
		}
	}

	iFlushCall(FLUSH_INTERPRETER);
	recSYSCALL_emit_oaknut();
	g_branch = 2; // Indirect branch with event check.
}

static void recSYSCALL_emit_oaknut()
{
	recBeginOaknutEmit();
	recFlushReccycle();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	recReloadReccycle();
	oakEmitCall(reinterpret_cast<void*>(eeExecuteSyscallInstruction));
	recEndOaknutEmit();
}

static void recBREAK_emit_oaknut()
{
	recBeginOaknutEmit();
	recFlushReccycle();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	recReloadReccycle();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, 4);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	oakAsm->MOV(OAK_WARG1, 0x24);
	oakLoad32(OAK_WARG2, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.branch))});
	oakEmitCall(reinterpret_cast<void*>(cpuException));
	recEndOaknutEmit();
}

////////////////////////////////////////////////////
void R5900::Dynarec::OpcodeImpl::recBREAK()
{
	EE::Profiler.EmitOp(eeOpcode::BREAK);

	iFlushCall(FLUSH_INTERPRETER);
	recBREAK_emit_oaknut();
	g_branch = 2; // Indirect branch with event check.
}

// Size is in dwords (4 bytes)
void recClear(u32 addr, u32 size)
{
	if ((addr) >= maxrecmem || !(recLUT[(addr) >> 16] + (addr & ~0xFFFFUL)))
		return;

	addr = HWADDR(addr);

    u32 addr_size = addr + (size << 2); // // size * 4
	int blockidx = recBlocks.LastIndex(addr_size - 4);

	if (blockidx == -1)
		return;

	u32 lowerextent = 0xFFFFFFFF, upperextent = 0, ceiling = 0xFFFFFFFF; // 0xFFFFFFFF == -1

	BASEBLOCKEX* pexblock = recBlocks[blockidx + 1];
	if (pexblock)
		ceiling = pexblock->startpc;

	int toRemoveLast = blockidx;

    u32 blockstart, blockend;
	while ((pexblock = recBlocks[blockidx]))
	{
		blockstart = pexblock->startpc;
		blockend = pexblock->startpc + (pexblock->size << 2); // pexblock->size * 4
        BASEBLOCK* pblock = PC_GETBLOCK(blockstart);

		if (pblock == s_pCurBlock)
		{
			if (toRemoveLast != blockidx)
			{
				recBlocks.Remove((blockidx + 1), toRemoveLast);
			}
			toRemoveLast = --blockidx;
			continue;
		}

		if (blockend <= addr)
		{
			lowerextent = std::max(lowerextent, blockend);
			break;
		}

		lowerextent = std::min(lowerextent, blockstart);
		upperextent = std::max(upperextent, blockend);
		pblock->SetFnptr((uptr)JITCompile);

		blockidx--;
	}

	if (toRemoveLast != blockidx)
	{
		recBlocks.Remove((blockidx + 1), toRemoveLast);
	}

	upperextent = std::min(upperextent, ceiling);

	if (upperextent > lowerextent)
		ClearRecLUT(PC_GETBLOCK(lowerextent), upperextent - lowerextent);
}


static int* s_pCode;

void SetBranchReg()
{
	g_branch = 1;
	recBeginOaknutEmit();
	oakStore32(oakWRegister(EE_HOST_RAX), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	recEndOaknutEmit();

	iFlushCall(FLUSH_EVERYTHING);

	iBranchTest();
}

void SetBranchImm(u32 imm)
{
	g_branch = 1;

	pxAssert(imm);

	// end the current block
	iFlushCall(FLUSH_EVERYTHING);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, imm);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	recEndOaknutEmit();
	iBranchTest(imm);
}

u8* recBeginThunk()
{
	// if recPtr reached the mem limit reset whole mem
	if (recPtr >= recPtrEnd)
		eeRecNeedsReset = true;

	oakSetAsmPtr(recPtr, _4kb);
	recPtr = oakStartBlock();
	return recPtr;
}

u8* recEndThunk()
{
	u8* block_end = oakEndBlock();

	pxAssert(block_end < SysMemory::GetEERecEnd());
	recPtr = block_end;
	return block_end;
}

bool TrySwapDelaySlot(u32 rs, u32 rt, u32 rd, bool allow_loadstore)
{
#if 1
	if (g_recompilingDelaySlot)
		return false;

	const u32 opcode_encoded = *(u32*)PSM(pc);
	if (opcode_encoded == 0)
	{
		recompileNextInstruction(true, true);
		return true;
	}

	//std::string disasm;
	//disR5900Fasm(disasm, opcode_encoded, pc, false);

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
		case 24: // DADDI
		case 25: // DADDIU
		{
			if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && (rd == opcode_rs || rd == opcode_rt)))
				goto is_unsafe;
		}
		break;

		case 26: // LDL
		case 27: // LDR
		case 30: // LQ
		case 31: // SQ
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
		case 44: // SDL
		case 45: // SDR
		case 46: // SWR
		case 55: // LD
		case 63: // SD
		{
			// We can't allow loadstore swaps for BC0x/BC2x, since they could affect the condition.
			if (!allow_loadstore || (rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && (rd == opcode_rs || rd == opcode_rt)))
				goto is_unsafe;
		}
		break;

		case 15: // LUI
		{
			if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && rd == opcode_rt))
				goto is_unsafe;
		}
		break;

		case 49: // LWC1
		case 57: // SWC1
		case 54: // LQC2
		case 62: // SQC2
			if (!allow_loadstore)
				goto is_unsafe;
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
				case 10: // MOVZ
				case 11: // MOVN
				case 20: // DSLLV
				case 22: // DSRLV
				case 23: // DSRAV
				case 24: // MULT
				case 25: // MULTU
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
				case 44: // DADD
				case 45: // DADDU
				case 46: // DSUB
				case 47: // DSUBU
				case 56: // DSLL
				case 58: // DSRL
				case 59: // DSRA
				case 60: // DSLL32
				case 62: // DSRL32
				case 63: // DSRA32
				{
					if ((rs != 0 && rs == opcode_rd) || (rt != 0 && rt == opcode_rd) || (rd != 0 && (rd == opcode_rs || rd == opcode_rt)))
						goto is_unsafe;
				}
				break;

				case 15: // SYNC
				case 26: // DIV
				case 27: // DIVU
					break;

				default:
					goto is_unsafe;
			}
		}
		break;

		case 16: // COP0
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

				case 16: // TLB (technically would be safe, but we don't use it anyway)
				default:
					goto is_unsafe;
			}
			break;
		}
		break;

		case 17: // COP1
		{
			switch ((opcode_encoded >> 21) & 0x1F)
			{
				case 0: // MFC1
				case 2: // CFC1
				{
					if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && rd == opcode_rt))
						goto is_unsafe;
				}
				break;

				case 4: // MTC1
				case 6: // CTC1
				case 16: // S
				{
					const u32 funct = (opcode_encoded & 0x3F);
					if (funct == 50 || funct == 52 || funct == 54) // C.EQ, C.LT, C.LE
					{
						// affects flags that we're comparing
						goto is_unsafe;
					}
				}
					[[fallthrough]];

				case 20: // W
				{
				}
				break;

				default:
					goto is_unsafe;
			}
		}
		break;

		case 18: // COP2
		{
			switch ((opcode_encoded >> 21) & 0x1F)
			{
				case 8: // BC2XX
					goto is_unsafe;

				case 1: // QMFC2
				case 2: // CFC2
				{
					if ((rs != 0 && rs == opcode_rt) || (rt != 0 && rt == opcode_rt) || (rd != 0 && rd == opcode_rt))
						goto is_unsafe;
				}
				break;

				default:
					break;
			}

		}
		break;

		case 28: // MMI
		{
			switch (opcode_encoded & 0x3F)
			{
				case 8: // MMI0
				case 9: // MMI1
				case 10: // MMI2
				case 40: // MMI3
				case 41: // MMI3
				case 52: // PSLLH
				case 54: // PSRLH
				case 55: // LSRAH
				case 60: // PSLLW
				case 62: // PSRLW
				case 63: // PSRAW
				{
					if ((rs != 0 && rs == opcode_rd) || (rt != 0 && rt == opcode_rd) || (rd != 0 && rd == opcode_rd))
						goto is_unsafe;
				}
				break;

				default:
					goto is_unsafe;
			}
		}
		break;

		default:
			goto is_unsafe;
	}

	recompileNextInstruction(true, true);
	return true;

is_unsafe:
	return false;
#else
	return false;
#endif
}

void SaveBranchState()
{
	s_savenBlockCycles = s_nBlockCycles;
	memcpy(s_saveConstRegs, g_cpuConstRegs, sizeof(g_cpuConstRegs));
	s_saveHasConstReg = g_cpuHasConstReg;
	s_saveFlushedConstReg = g_cpuFlushedConstReg;
	s_psaveInstInfo = g_pCurInstInfo;

	memcpy(s_saveX86regs, x86regs, sizeof(x86regs));
	memcpy(s_saveXMMregs, xmmregs, sizeof(xmmregs));
}

void LoadBranchState()
{
	s_nBlockCycles = s_savenBlockCycles;

	memcpy(g_cpuConstRegs, s_saveConstRegs, sizeof(g_cpuConstRegs));
	g_cpuHasConstReg = s_saveHasConstReg;
	g_cpuFlushedConstReg = s_saveFlushedConstReg;
	g_pCurInstInfo = s_psaveInstInfo;

	memcpy(x86regs, s_saveX86regs, sizeof(x86regs));
	memcpy(xmmregs, s_saveXMMregs, sizeof(xmmregs));
}

static void iFlushCallPcWriteback_emit_oaknut(u32 value)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, value);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	recEndOaknutEmit();
}

static void iFlushCallCodeWriteback_emit_oaknut(u32 value)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, value);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.code))});
	recEndOaknutEmit();
}

void iFlushCall(int flushtype)
{
	// Free ARM64 host registers that are not saved across function calls:
	for (u32 i = 0; i < iREGCNT_GPR; i++)
	{
		if (!x86regs[i].inuse)
			continue;

		if (oakIsCallerSaved(i) ||
			((flushtype & FLUSH_FREE_VU0) && x86regs[i].type == X86TYPE_VIREG) ||
			((flushtype & FLUSH_FREE_NONTEMP_X86) && x86regs[i].type != X86TYPE_TEMP) ||
			((flushtype & FLUSH_FREE_TEMP_X86) && x86regs[i].type == X86TYPE_TEMP))
		{
			_freeX86reg(i);
		}
	}

	for (u32 i = 0; i < iREGCNT_XMM; i++)
	{
		if (!xmmregs[i].inuse)
			continue;

		if (oakIsCallerSavedXmm(i) ||
			(flushtype & FLUSH_FREE_XMM) ||
			((flushtype & FLUSH_FREE_VU0) && xmmregs[i].type == XMMTYPE_VFREG))
		{
			_freeXMMreg(i);
		}
	}

	if (flushtype & FLUSH_ALL_X86)
		_flushX86regs();

	if (flushtype & FLUSH_FLUSH_XMM)
		_flushXMMregs();

	if (flushtype & FLUSH_CONSTANT_REGS)
		_flushConstRegs(true);

	if ((flushtype & FLUSH_PC) && !g_cpuFlushedPC)
	{
		iFlushCallPcWriteback_emit_oaknut(pc);
		g_cpuFlushedPC = true;
	}

	if ((flushtype & FLUSH_CODE) && !g_cpuFlushedCode)
	{
		iFlushCallCodeWriteback_emit_oaknut(cpuRegs.code);
		g_cpuFlushedCode = true;
	}

#if 0
	if ((flushtype == FLUSH_CAUSE) && !g_maySignalException)
	{
		if (g_recompilingDelaySlot)
			xOR(ptr32[&cpuRegs.CP0.n.Cause], 1 << 31); // BD
		g_maySignalException = true;
	}
#endif
}

// Note: scaleblockcycles() scales s_nBlockCycles respective to the EECycleRate value for manipulating the cycles of current block recompiling.
// s_nBlockCycles is 3 bit fixed point.  Divide by 8 when done!
// Scaling blocks under 40 cycles seems to produce countless problem, so let's try to avoid them.

#define DEFAULT_SCALED_BLOCKS() (s_nBlockCycles >> 3)

static u32 scaleblockcycles_calculation()
{
	const bool lowcycles = (s_nBlockCycles <= 40);
	const s8 cyclerate = EmuConfig.Speedhacks.EECycleRate;
	u32 scale_cycles = 0;

	if (cyclerate == 0 || lowcycles || cyclerate < -99 || cyclerate > 3)
		scale_cycles = DEFAULT_SCALED_BLOCKS();

	else if (cyclerate > 1)
		scale_cycles = s_nBlockCycles >> (2 + cyclerate);

	else if (cyclerate == 1)
		scale_cycles = DEFAULT_SCALED_BLOCKS() / 1.3f; // Adds a mild 30% increase in clockspeed for value 1.

	else if (cyclerate == -1) // the mildest value.
		// These values were manually tuned to yield mild speedup with high compatibility
		scale_cycles = (s_nBlockCycles <= 80 || s_nBlockCycles > 168 ? 5 : 7) * s_nBlockCycles / 32;

	else
		scale_cycles = ((5 + (-2 * (cyclerate + 1))) * s_nBlockCycles) >> 5;

	// Ensure block cycle count is never less than 1.
	return (scale_cycles < 1) ? 1 : scale_cycles;
}

static u32 scaleblockcycles()
{
	const u32 scaled = scaleblockcycles_calculation();

	return scaled;
}
u32 scaleblockcycles_clear()
{
	u32 scaled = scaleblockcycles_calculation();

	const s8 cyclerate = EmuConfig.Speedhacks.EECycleRate;
	const bool lowcycles = (s_nBlockCycles <= 40);

	if (!lowcycles && cyclerate > 1)
	{
		s_nBlockCycles &= (0x1 << (cyclerate + 2)) - 1;
	}
	else
	{
		s_nBlockCycles &= 0x7;
	}

	return scaled;
}

// Generates dynarec code for Event tests followed by a block dispatch (branch).
// Parameters:
//   newpc - address to jump to at the end of the block.  If newpc == 0xffffffff then
//   the jump is assumed to be to a register (dynamic).  For any other value the
//   jump is assumed to be static, in which case the block will be "hardlinked" after
//   the first time it's dispatched.
//
//   noDispatch - When set true, then jump to Dispatcher.  Used by the recs
//   for blocks which perform exception checks without branching (it's enabled by
//   setting "g_branch = 2";
static u8* recEventBranchPatchpoint_emit_oaknut()
{
	u8* branch = nullptr;
	recBeginOaknutEmit();
	branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return branch;
}

static u8* recBlockLinkPatchpoint_emit_oaknut()
{
	u8* link_patch = nullptr;
	recBeginOaknutEmit();
	link_patch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return link_patch;
}

static void recJumpDispatcherEvent_emit_oaknut()
{
	recBeginOaknutEmit();
	oakEmitJmp(DispatcherEvent);
	recEndOaknutEmit();
}

static void recJumpDispatcherReg_emit_oaknut()
{
	recBeginOaknutEmit();
	oakEmitJmp(DispatcherReg);
	recEndOaknutEmit();
}

static void iBranchTestWaitLoopCycles_emit_oaknut(u32 scaled_cycles)
{
	recBeginOaknutEmit();
	// Update RECCYCLE delta with block cycles
	if (scaled_cycles <= 4095)
		oakAsm->ADDS(oak::util::W24, oak::util::W24, (int)scaled_cycles);
	else
	{
		oakAsm->MOV(OAK_WSCRATCH, scaled_cycles);
		oakAsm->ADDS(oak::util::W24, oak::util::W24, OAK_WSCRATCH);
	}
	// If MI (delta < 0, cycle < nextEvent): clamp delta to 0 (fast-forward to nextEvent)
	oakAsm->CSEL(oak::util::W24, oak::util::WZR, oak::util::W24, oak::Cond::MI);
	// DispatcherEvent will sync cycle from W24 and call recEventTest
	recEndOaknutEmit();
}

static void iBranchTestUpdateCycleAndCompareEvent_emit_oaknut(u32 scaled_cycles)
{
	// Delta-cycle optimization: RECCYCLE (W24) = cycle - nextEventCycle.
	// Instead of 5 insns (load cycle, add, store, load nextEvent, sub),
	// just ADDS W24, W24, #scaled_cycles (1 insn).
	// Result: MI = no events (negative), PL = events pending (positive/zero).
	recBeginOaknutEmit();
		if (scaled_cycles <= 4095)
			oakAsm->ADDS(oak::util::W24, oak::util::W24, (int)scaled_cycles);
		else
		{
			oakAsm->MOV(OAK_WSCRATCH, scaled_cycles);
			oakAsm->ADDS(oak::util::W24, oak::util::W24, OAK_WSCRATCH);
		}
	recEndOaknutEmit();
}

static void iBranchTest(u32 newpc)
{
	// Check the Event scheduler if our "cycle target" has been reached.
	// Equiv code to:
	//    cpuRegs.cycle += blockcycles;
	//    if ( cpuRegs.cycle > g_nextEventCycle ) { DoEvents(); }

	if (EmuConfig.Speedhacks.WaitLoop && s_nBlockFF && newpc == s_branchTo)
	{
		iBranchTestWaitLoopCycles_emit_oaknut(scaleblockcycles());

		recJumpDispatcherEvent_emit_oaknut();
	}
	else
	{
		iBranchTestUpdateCycleAndCompareEvent_emit_oaknut(scaleblockcycles());

		u8* event_branch = recEventBranchPatchpoint_emit_oaknut();

		if (newpc == 0xffffffff) {
			recJumpDispatcherReg_emit_oaknut();
        }
		else {
//            recBlocks.Link(HWADDR(newpc), xJcc32(Jcc_Signed));
			u8* link_patch = recBlockLinkPatchpoint_emit_oaknut();
			recBlocks.Link(HWADDR(newpc), reinterpret_cast<s32*>(link_patch));
        }

		oakPatchCondBranch(event_branch, oakGetCurrentCodePointer(), oak::Cond::PL, false);

		recJumpDispatcherEvent_emit_oaknut();
	}
}

// opcode 'code' modifies:
// 1: status
// 2: MAC
// 4: clip
int cop2flags(u32 code)
{
	if (code >> 26 != 022)
		return 0; // not COP2
	if ((code >> 25 & 1) == 0)
		return 0; // a branch or transfer instruction

	switch (code >> 2 & 15)
	{
		case 15:
			switch (code >> 6 & 0x1f)
			{
				case 4: // ITOF*
				case 5: // FTOI*
				case 12: // MOVE MR32
				case 13: // LQI SQI LQD SQD
				case 15: // MTIR MFIR ILWR ISWR
				case 16: // RNEXT RGET RINIT RXOR
					return 0;
				case 7: // MULAq, ABS, MULAi, CLIP
					if ((code & 3) == 1) // ABS
						return 0;
					if ((code & 3) == 3) // CLIP
						return 4;
					return 3;
				case 11: // SUBA, MSUBA, OPMULA, NOP
					if ((code & 3) == 3) // NOP
						return 0;
					return 3;
				case 14: // DIV, SQRT, RSQRT, WAITQ
					if ((code & 3) == 3) // WAITQ
						return 0;
					return 1; // but different timing, ugh
				default:
					break;
			}
			break;
		case 4: // MAXbc
		case 5: // MINbc
		case 12: // IADD, ISUB, IADDI
		case 13: // IAND, IOR
		case 14: // VCALLMS, VCALLMSR
			return 0;
		case 7:
			if ((code & 1) == 1) // MAXi, MINIi
				return 0;
			return 3;
		case 10:
			if ((code & 3) == 3) // MAX
				return 0;
			return 3;
		case 11:
			if ((code & 3) == 3) // MINI
				return 0;
			return 3;
		default:
			break;
	}
	return 3;
}

int COP2DivUnitTimings(u32 code)
{
	// Note: Cycles are off by 1 since the check ignores the actual op, so they are off by 1
	switch (code & 0x3FF)
	{
		case 0x3BC: // DIV
		case 0x3BD: // SQRT
			return 6;
		case 0x3BE: // RSQRT
			return 12;
		default:
			return 0; // Used mainly for WAITQ
	}
}

bool COP2IsQOP(u32 code)
{
	if (_Opcode_ != 022) // Not COP2 operation
		return false;

	if ((code & 0x3f) == 0x20) // VADDq
		return true;
	if ((code & 0x3f) == 0x21) // VMADDq
		return true;
	if ((code & 0x3f) == 0x24) // VSUBq
		return true;
	if ((code & 0x3f) == 0x25) // VMSUBq
		return true;
	if ((code & 0x3f) == 0x1C) // VMULq
		return true;
	if ((code & 0x7FF) == 0x1FC) // VMULAq
		return true;
	if ((code & 0x7FF) == 0x23C) // VADDAq
		return true;
	if ((code & 0x7FF) == 0x23D) // VMADDAq
		return true;
	if ((code & 0x7FF) == 0x27C) // VSUBAq
		return true;
	if ((code & 0x7FF) == 0x27D) // VMSUBAq
		return true;

	return false;
}


void dynarecCheckBreakpoint()
{
	u32 pc = cpuRegs.pc;
	if (CBreakPoints::CheckSkipFirst(BREAKPOINT_EE, pc) != 0)
		return;

	const int bpFlags = isBreakpointNeeded(pc);
	bool hit = false;
	//check breakpoint at current pc
	if (bpFlags & 1)
	{
		auto cond = CBreakPoints::GetBreakPointCondition(BREAKPOINT_EE, pc);
		if (cond == NULL || cond->Evaluate())
		{
			hit = true;
		}
	}
	//check breakpoint in delay slot
	if (bpFlags & 2)
	{
		auto cond = CBreakPoints::GetBreakPointCondition(BREAKPOINT_EE, pc + 4);
		if (cond == NULL || cond->Evaluate())
			hit = true;
	}

	if (!hit)
		return;

	CBreakPoints::SetBreakpointTriggered(true, BREAKPOINT_EE);
	VMManager::SetPaused(true);
	recExitExecution();
}

void dynarecMemcheck(size_t i)
{
	const u32 op = memRead32(cpuRegs.pc);
	const OPCODE& opcode = GetInstruction(op);
	if (CBreakPoints::CheckSkipFirst(BREAKPOINT_EE, pc) != 0)
		return;

	auto mc = CBreakPoints::GetMemChecks(BREAKPOINT_EE)[i];

	if (mc.hasCond)
	{
		if (!mc.cond.Evaluate())
			return;
	}

	CBreakPoints::SetBreakpointTriggered(true, BREAKPOINT_EE);
	VMManager::SetPaused(true);
	recExitExecution();
}

static void recMemcheckStandardizeAddress_emit_oaknut()
{
	recBeginOaknutEmit();
	oakEmitCall(reinterpret_cast<const void*>(standardizeBreakpointAddress));
	recEndOaknutEmit();
}

static void recMemcheckHit_emit_oaknut(size_t index)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oak::util::X0, static_cast<u64>(index));
	oakEmitCall(reinterpret_cast<void*>(dynarecMemcheck));
	recEndOaknutEmit();
}

static void recBreakpointCheck_emit_oaknut()
{
	recBeginOaknutEmit();
	oakEmitCall(reinterpret_cast<void*>(dynarecCheckBreakpoint));
	recEndOaknutEmit();
}

static void recMemcheckAdjustAddress_emit_oaknut(s16 offset, u32 bits)
{
	recBeginOaknutEmit();
	if (offset > 0)
		oakAsm->ADD(oak::util::W0, oak::util::W0, static_cast<u32>(offset));
	else if (offset < 0)
		oakAsm->SUB(oak::util::W0, oak::util::W0, static_cast<u32>(-offset));

	if (bits == 128)
	{
		oakAsm->LSR(oak::util::W0, oak::util::W0, 4);
		oakAsm->LSL(oak::util::W0, oak::util::W0, 4);
	}
	recEndOaknutEmit();
}

static void recMemcheckPrepareRange_emit_oaknut(u32 bits)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oak::util::W1, oak::util::W0);
	oakAsm->ADD(oak::util::W2, oak::util::W0, bits >> 3);
	recEndOaknutEmit();
}

static u8* recMemcheckBranchPatchpoint_emit_oaknut()
{
	u8* branch = nullptr;
	recBeginOaknutEmit();
	branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return branch;
}

static u8* recMemcheckCompareEnd_emit_oaknut(u32 end)
{
	u8* branch = nullptr;
	recBeginOaknutEmit();
	oakAsm->MOV(oak::util::W0, end);
	oakAsm->CMP(oak::util::W1, oak::util::W0);
	branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return branch;
}

static u8* recMemcheckCompareStart_emit_oaknut(u32 start)
{
	u8* branch = nullptr;
	recBeginOaknutEmit();
	oakAsm->MOV(oak::util::W0, start);
	oakAsm->CMP(oak::util::W0, oak::util::W2);
	branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return branch;
}

static void recManualPageInitArgsAndPmap_emit_oaknut(u32 inpage_ptr, u32 inpage_words)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oak::util::W0, inpage_ptr);
	oakAsm->MOV(oak::util::W1, inpage_words);
	oakLoad64(oak::util::X17, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vtlbdata.pmap))});
	recEndOaknutEmit();
}

static void recManualPageCompareWord_emit_oaknut(u32 lpc_addr)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oak::util::X16, lpc_addr);
	oakAsm->ADD(oak::util::X16, oak::util::X17, oak::util::X16);
	oakAsm->LDR(oak::util::W2, oak::util::X16);
	oakAsm->MOV(oak::util::W3, *static_cast<u32*>(vtlb_GetPhyPtr(lpc_addr)));
	oakAsm->CMP(oak::util::W2, oak::util::W3);
	oakEmitCondBranch(oak::Cond::NE, DispatchBlockDiscard);
	recEndOaknutEmit();
}

static void recManualPageAddAndResetOnCarry_emit_oaknut(u16* counter, u32 size)
{
	recBeginOaknutEmit();
	oakMoveAddressToReg(oak::util::X16, counter);
	oakAsm->LDRH(oak::util::W4, oak::util::X16);
	oakAsm->MOV(OAK_WSCRATCH2, size);
	oakAsm->ADD(oak::util::W4, oak::util::W4, OAK_WSCRATCH2);
	oakAsm->STRH(oak::util::W4, oak::util::X16);
	// LDRH widens the value, so a 32-bit ADDS carry does not describe a
	// 16-bit overflow. Compare the widened sum with 0x10000 explicitly.
	oakAsm->CMP(oak::util::W4, 0x10000);
	oakEmitCondBranch(oak::Cond::CS, DispatchPageReset);
	recEndOaknutEmit();
}

static void recManualPageAddAndResetOnCarrySlow_emit_oaknut(u32* counter, u32 size)
{
	recBeginOaknutEmit();
	oakMoveAddressToReg(oak::util::X16, counter);
	oakAsm->LDR(oak::util::W4, oak::util::X16);
	oakAsm->MOV(OAK_WSCRATCH2, size);
	oakAsm->ADDS(oak::util::W4, oak::util::W4, OAK_WSCRATCH2);
	oakAsm->STR(oak::util::W4, oak::util::X16);
	oakEmitCondBranch(oak::Cond::CS, DispatchPageReset);
	recEndOaknutEmit();
}

void recMemcheck(u32 op, u32 bits, bool store)
{
	iFlushCall(FLUSH_EVERYTHING | FLUSH_PC);

	// compute accessed address
	_eeMoveGPRtoR(EE_HOST_RAX, (op >> 21) & 0x1F);
	recMemcheckAdjustAddress_emit_oaknut(static_cast<s16>(op), bits);

	recMemcheckStandardizeAddress_emit_oaknut();
	recMemcheckPrepareRange_emit_oaknut(bits);

	// ecx = access address
	// edx = access address+size

	auto checks = CBreakPoints::GetMemChecks(BREAKPOINT_EE);
	for (size_t i = 0; i < checks.size(); i++)
	{
		if (checks[i].result == 0)
			continue;
		if ((checks[i].memCond & MEMCHECK_WRITE) == 0 && store)
			continue;
		if ((checks[i].memCond & MEMCHECK_READ) == 0 && !store)
			continue;

		// logic: memAddress < bpEnd && bpStart < memAddress+memSize

		u8* next1 = recMemcheckCompareEnd_emit_oaknut(standardizeBreakpointAddress(checks[i].end));

		u8* next2 = recMemcheckCompareStart_emit_oaknut(standardizeBreakpointAddress(checks[i].start));

		// hit the breakpoint
		if (checks[i].result & MEMCHECK_BREAK)
		{
			recMemcheckHit_emit_oaknut(i);
		}

		oakPatchCondBranch(next1, oakGetCurrentCodePointer(), oak::Cond::GE, false);
		oakPatchCondBranch(next2, oakGetCurrentCodePointer(), oak::Cond::GE, false);
	}
}

void encodeBreakpoint()
{
	if (isBreakpointNeeded(pc) != 0)
	{
		iFlushCall(FLUSH_EVERYTHING | FLUSH_PC);
		recBreakpointCheck_emit_oaknut();
	}
}

void encodeMemcheck()
{
	const int needed = isMemcheckNeeded(pc);
	if (needed == 0)
		return;

	const u32 op = memRead32(needed == 2 ? pc + 4 : pc);
	const OPCODE& opcode = GetInstruction(op);

	const bool store = (opcode.flags & IS_STORE) != 0;
	switch (opcode.flags & MEMTYPE_MASK)
	{
		case MEMTYPE_BYTE:
			recMemcheck(op, 8, store);
			break;
		case MEMTYPE_HALF:
			recMemcheck(op, 16, store);
			break;
		case MEMTYPE_WORD:
			recMemcheck(op, 32, store);
			break;
		case MEMTYPE_DWORD:
			recMemcheck(op, 64, store);
			break;
		case MEMTYPE_QWORD:
			recMemcheck(op, 128, store);
			break;
	}
}

static void recompileDelaySlotBegin_emit_oaknut()
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, 1);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.IsDelaySlot))});
	recEndOaknutEmit();
}

static void recompileDelaySlotEnd_emit_oaknut()
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, 0);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.IsDelaySlot))});
	recEndOaknutEmit();
}

static void recompileDelaySlotBranchBegin_emit_oaknut()
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, 1);
	oakStore32(OAK_WSCRATCH,
		{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.branch))});
	recEndOaknutEmit();
}

static void recompileDelaySlotBranchEnd_emit_oaknut()
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, 0);
	oakStore32(OAK_WSCRATCH,
		{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.branch))});
	recEndOaknutEmit();
}

static bool delaySlotOpcodeMayRaiseException(u32 code)
{
	const u32 primary = code >> 26;
	if (primary == 8 || primary == 24) // ADDI, DADDI
		return true;
	if (primary == 1)
	{
		const u32 rt = (code >> 16) & 0x1f;
		return rt == 8 || rt == 9 || rt == 10 || rt == 11 || rt == 12 || rt == 14;
	}
	if (primary != 0)
		return false;

	switch (code & 0x3f)
	{
		case 0x0c: // SYSCALL
		case 0x0d: // BREAK
		case 0x20: // ADD
		case 0x22: // SUB
		case 0x2c: // DADD
		case 0x2e: // DSUB
		case 0x30: // TGE
		case 0x31: // TGEU
		case 0x32: // TLT
		case 0x33: // TLTU
		case 0x34: // TEQ
		case 0x36: // TNE
			return true;
		default:
			return false;
	}
}

void recEmitArithmeticOverflowException()
{
	// This is a cold path. Preserve the allocator's compile-time state so the
	// non-overflow path can keep all of its cached registers, while emitting a
	// complete architectural writeback for the exception path.
	_x86regs saved_x86regs[iREGCNT_GPR];
	_xmmregs saved_xmmregs[iREGCNT_XMM];
	memcpy(saved_x86regs, x86regs, sizeof(saved_x86regs));
	memcpy(saved_xmmregs, xmmregs, sizeof(saved_xmmregs));
	const u32 saved_has_const = g_cpuHasConstReg;
	const u32 saved_flushed_const = g_cpuFlushedConstReg;
	const bool saved_flushed_pc = g_cpuFlushedPC;
	const bool saved_flushed_code = g_cpuFlushedCode;

	iFlushCall(FLUSH_INTERPRETER);
	recFlushReccycle();
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH,
		{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakStore32(OAK_WSCRATCH,
		{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	recEndOaknutEmit();
	recReloadReccycle();

	// Arithmetic overflow helpers use the interpreter's already-advanced PC.
	// Trap opcodes are different: their opcode implementation subtracts four.
	recBeginOaknutEmit();
	if (g_recompilingDelaySlot)
	{
		// During delay-slot recompilation the compile cursor still points at the
		// delay instruction, while the interpreter has already advanced past it.
		oakLoad32(OAK_WSCRATCH,
			{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
		oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, 4);
		oakStore32(OAK_WSCRATCH,
			{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	}
	oakAsm->MOV(OAK_WARG1, 0x30);
	oakLoad32(OAK_WARG2,
		{oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.branch))});
	oakEmitCall(reinterpret_cast<void*>(cpuException));
	oakEmitJmp(DispatcherEvent);
	recEndOaknutEmit();

	memcpy(x86regs, saved_x86regs, sizeof(saved_x86regs));
	memcpy(xmmregs, saved_xmmregs, sizeof(saved_xmmregs));
	g_cpuHasConstReg = saved_has_const;
	g_cpuFlushedConstReg = saved_flushed_const;
	g_cpuFlushedPC = saved_flushed_pc;
	g_cpuFlushedCode = saved_flushed_code;
}

void recEmitExceptionExit()
{
	recBeginOaknutEmit();
	oakEmitJmp(DispatcherEvent);
	recEndOaknutEmit();
}

static void recompileDelaySlotClearCauseBd_emit_oaknut()
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Cause))});
	oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, 1);
	oakAsm->LSR(OAK_WSCRATCH, OAK_WSCRATCH, 1);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.CP0.n.Cause))});
	recEndOaknutEmit();
}

void recompileNextInstruction(bool delayslot, bool swapped_delay_slot)
{
	const u32 profiler_pc = pc;
	if (EmuConfig.EnablePatches)
		Patch::ApplyDynamicPatches(pc);

	// add breakpoint
	if (!delayslot)
	{
		encodeBreakpoint();
		encodeMemcheck();
	}
	else
	{

		_clearNeededX86regs();
		_clearNeededXMMregs();
	}

	s_pCode = (int*)PSM(pc);
	pxAssert(s_pCode);

#if 0
	// acts as a tag for delimiting recompiled instructions when viewing ARM64 disasm.
	if (IsDevBuild)
		xNOP();
	if (IsDebugBuild)
		xMOV(eax, pc);
#endif

	const int old_code = cpuRegs.code;
	EEINST* old_inst_info = g_pCurInstInfo;

	cpuRegs.code = *(int*)s_pCode;
	JitProfiler::OpcodeRangeScope profiler_scope;
	if (JitProfiler::IsActive())
		profiler_scope.Begin(0, HWADDR(profiler_pc), static_cast<u32>(cpuRegs.code));

	if (!delayslot)
	{
		pc += 4;
		g_cpuFlushedPC = false;
		g_cpuFlushedCode = false;
	}
	else
	{
		// increment after recompiling so that pc points to the branch during recompilation
		g_recompilingDelaySlot = true;
	}

	g_pCurInstInfo++;

    // pc might be past s_nEndBlock if the last instruction in the block is a DI.
    u32 s_nEndBlock_pc = (s_nEndBlock - pc) / 4 + 1;
    if (pc <= s_nEndBlock && (g_pCurInstInfo + s_nEndBlock_pc) <= s_pInstCache + s_nInstCacheSize)
    {
        int i, count;
        for (i = 0; i < iREGCNT_GPR; ++i)
        {
            if (x86regs[i].inuse)
            {
                count = _recIsRegReadOrWritten(g_pCurInstInfo, s_nEndBlock_pc, x86regs[i].type, x86regs[i].reg);
                if (count > 0)
                    x86regs[i].counter = 1000 - count;
                else
                    x86regs[i].counter = 0;
            }
        }

        for (i = 0; i < iREGCNT_XMM; ++i)
        {
            if (xmmregs[i].inuse)
            {
                count = _recIsRegReadOrWritten(g_pCurInstInfo, s_nEndBlock_pc, xmmregs[i].type, xmmregs[i].reg);
                if (count > 0)
                    xmmregs[i].counter = 1000 - count;
                else
                    xmmregs[i].counter = 0;
            }
        }
    }

	if (g_pCurInstInfo->info & EEINST_COP2_FLUSH_VU0_REGISTERS)
	{
		_flushCOP2regs();
	}

	const OPCODE& opcode = GetCurrentInstruction();

	//pxAssert( !(g_pCurInstInfo->info & EEINSTINFO_NOREC) );
	// if this instruction is a jump or a branch, exit right away
	if (delayslot)
	{
		bool check_branch_delay = false;
		switch (_Opcode_)
		{
			case 0:
				switch (_Funct_)
				{
					case 8: // jr
					case 9: // jalr
						check_branch_delay = true;
						break;
				}
				break;

			case 1:
				switch (_Rt_)
				{
					case 0:
					case 1:
					case 2:
					case 3:
					case 0x10:
					case 0x11:
					case 0x12:
					case 0x13:
						check_branch_delay = true;
						break;
				}
				break;

			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
			case 0x14:
			case 0x15:
			case 0x16:
			case 0x17:
				check_branch_delay = true;
				break;

			case 0x11:
				if (_Rs_ == 8) // COP1 bc1f/bc1t/bc1fl/bc1tl
					check_branch_delay = true;
				break;
		}
		// Check for branch in delay slot, new code by FlatOut.
		// Gregory tested this in 2017 using the ps2autotests suite and remarked "So far we return 1 (even with this PR), and the HW 2.
		// Original PR and discussion at https://github.com/PCSX2/pcsx2/pull/1783 so we don't forget this information.
		if (check_branch_delay)
		{
			_clearNeededX86regs();
			_clearNeededXMMregs();
			pc += 4;
			g_cpuFlushedPC = false;
			g_cpuFlushedCode = false;
			if (g_maySignalException) {
				recompileDelaySlotClearCauseBd_emit_oaknut();
            }

			g_recompilingDelaySlot = false;
			return;
		}
	}
	// Only memory operations need the vtlb IsDelaySlot marker. Keeping it off
	// ordinary ALU/move/NOP delay slots removes four emitted instructions from
	// the common path without weakening the current immediate vtlb exception
	// exit. Other exception behavior remains unchanged from this core.
	const bool delay_slot_memory = delayslot && cpuRegs.code != 0 && (opcode.flags & IS_MEMORY);
	const bool delay_slot_cpu_exception = delayslot && cpuRegs.code != 0 &&
		delaySlotOpcodeMayRaiseException(cpuRegs.code);
	if (delay_slot_memory)
		recompileDelaySlotBegin_emit_oaknut();
	if (delay_slot_cpu_exception)
		recompileDelaySlotBranchBegin_emit_oaknut();

	if (cpuRegs.code == 0x00000000)
	{
		// Note: Tests on a ps2 suggested more like 5 cycles for a NOP. But there's many factors in this..
		s_nBlockCycles += 9 * (2 - ((cpuRegs.CP0.n.Config >> 18) & 0x1));
	}
	else
	{
		//If the COP0 DIE bit is disabled, cycles should be doubled.
		s_nBlockCycles += opcode.cycles * (2 - ((cpuRegs.CP0.n.Config >> 18) & 0x1));
		opcode.recompile();
	}

	if (!swapped_delay_slot)
	{
		_clearNeededX86regs();
		_clearNeededXMMregs();
	}
	_validateRegs();

	if (delayslot)
	{
		if (delay_slot_cpu_exception)
			recompileDelaySlotBranchEnd_emit_oaknut();
		if (delay_slot_memory)
			recompileDelaySlotEnd_emit_oaknut();
		pc += 4;
		g_cpuFlushedPC = false;
		g_cpuFlushedCode = false;
		if (g_maySignalException) {
			recompileDelaySlotClearCauseBd_emit_oaknut();
        }
		g_recompilingDelaySlot = false;
	}

	g_maySignalException = false;

	cpuRegs.code = *s_pCode;

	if (swapped_delay_slot)
	{
		cpuRegs.code = old_code;
		g_pCurInstInfo = old_inst_info;
	}
}


// Called when a block under manual protection fails it's pre-execution integrity check.
// (meaning the actual code area has been modified -- ie dynamic modules being loaded or,
//  less likely, self-modifying code)
void dyna_block_discard(u32 start, u32 sz)
{
#ifdef PCSX2_DEVBUILD
	eeRecPerfLog.Write(Color_StrongGray, "Clearing Manual Block @ 0x%08X  [size=%d]", start, sz * 4);
#endif
	recClear(start, sz);
}

// called when a page under manual protection has been run enough times to be a candidate
// for being reset under the faster vtlb write protection.  All blocks in the page are cleared
// and the block is re-assigned for write protection.
void dyna_page_reset(u32 start, u32 sz)
{
	const u32 page_start = start & ~static_cast<u32>(__pagemask);
	const u32 page_index = page_start >> __pageshift;
	recClear(page_start, __pagesize / sizeof(u32));
	manual_page[page_index] = 0;
	manual_slow_page[page_index] = 0;

	// A busy mixed code/data page gets a long probation interval instead of
	// becoming permanently slow. After that interval, retry normal protection.
	if (manual_counter[page_index] > 3)
		manual_counter[page_index] = 0;
	manual_counter[page_index]++;
	mmap_MarkCountedRamPage(start);
}

static void memory_protect_recompiled_code(u32 startpc, u32 size)
{
	u32 inpage_ptr = HWADDR(startpc);
	const u32 inpage_sz = size << 2; // size * 4
	const u32 host_page_index = inpage_ptr >> __pageshift;

	// The kernel context register is stored @ 0x800010C0-0x80001300
	// The EENULL thread context register is stored @ 0x81000-....
	// Compare at host-page granularity: on 16 KiB Android systems, neighbouring
	// PS2 4 KiB pages share the same mprotect unit and must remain manual too.
	const u32 startpc_host_page = startpc & ~static_cast<u32>(__pagemask);
	const bool contains_thread_stack =
		startpc_host_page == (0x00081000u & ~static_cast<u32>(__pagemask)) ||
		startpc_host_page == (0x800010c0u & ~static_cast<u32>(__pagemask));

	// note: blocks are guaranteed to reside within the confines of a single page.
	const vtlb_ProtectionMode PageType = contains_thread_stack ? ProtMode_Manual : mmap_GetRamPageInfo(inpage_ptr);

	switch (PageType)
	{
		case ProtMode_NotRequired:
			break;

		case ProtMode_None:
		case ProtMode_Write:
			mmap_MarkCountedRamPage(inpage_ptr);
			manual_page[host_page_index] = 0;
			manual_slow_page[host_page_index] = 0;
			break;

		case ProtMode_Manual:
			recManualPageInitArgsAndPmap_emit_oaknut(inpage_ptr, inpage_sz >> 2);

            u32 lpc_addr;
			u32 lpc = inpage_ptr;
			u32 stg = inpage_sz;

			while (stg > 0)
			{

                lpc_addr = lpc & 0x1fffffff;

				recManualPageCompareWord_emit_oaknut(lpc_addr);

				stg -= 4;
				lpc += 4;
			}

			// Tweakpoint!  3 is a 'magic' number representing the number of times a counted block
			// is re-protected before the recompiler gives up and sets it up as an uncounted (permanent)
			// manual block.  Higher thresholds result in more recompilations for blocks that share code
			// and data on the same page.  Side effects of a lower threshold: over extended gameplay
			// with several map changes, a game's overall performance could degrade.

			// (ideally, perhaps, manual_counter should be reset to 0 every few minutes?)

			if (!contains_thread_stack && manual_counter[host_page_index] <= 3)
			{
				// Counted blocks add a weighted (by block size) value into manual_page each time they're
				// run.  If the block gets run a lot, it resets and re-protects itself in the hope
				// that whatever forced it to be manually-checked before was a 1-time deal.

				// Counted blocks have a secondary threshold check in manual_counter, which forces a block
				// to 'uncounted' mode if it's recompiled several times.  This protects against excessive
				// recompilation of blocks that reside on the same codepage as data.

				// fixme? Currently this algo is kinda dumb and results in the forced recompilation of a
				// lot of blocks before it decides to mark a 'busy' page as uncounted.  There might be
				// be a more clever approach that could streamline this process, by doing a first-pass
				// test using the vtlb memory protection (without recompilation!) to reprotect a counted
				// block.  But unless a new algo is relatively simple in implementation, it's probably
				// not worth the effort (tests show that we have lots of recompiler memory to spare, and
				// that the current amount of recompilation is fairly cheap).

				recManualPageAddAndResetOnCarry_emit_oaknut(&manual_page[host_page_index], size);

#ifdef PCSX2_DEVBUILD
				// note: clearcnt is measured per-page, not per-block!
				eeRecPerfLog.Write("Manual block @ %08X : size =%3d  page/offs = 0x%05X/0x%03X  inpgsz = %d  clearcnt = %d",
					startpc, size, host_page_index, inpage_ptr & __pagemask, inpage_sz, manual_counter[host_page_index]);
#endif
			}
			else
			{
				if (!contains_thread_stack)
				{
					// Do not leave pages in per-execution memcmp mode forever. A 32-bit
					// weighted counter makes the retry rare for genuinely busy pages.
					recManualPageAddAndResetOnCarrySlow_emit_oaknut(&manual_slow_page[host_page_index], size);
				}
#ifdef PCSX2_DEVBUILD
				eeRecPerfLog.Write("Uncounted Manual block @ 0x%08X : size =%3d page/offs = 0x%05X/0x%03X  inpgsz = %d",
					startpc, size, host_page_index, inpage_ptr & __pagemask, inpage_sz);
#endif
			}
			break;
	}
}

// Skip MPEG Game-Fix
static void skipMPEGReturnTrue_emit_oaknut()
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, 1);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[2].UL[0]))});
	oakAsm->MOV(OAK_WSCRATCH, 0);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[2].UL[1]))});
	oakLoad32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[31].UL[0]))});
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	recEndOaknutEmit();
}

static bool skipMPEG_By_Pattern(u32 sPC)
{

	if (!CHECK_SKIPMPEGHACK)
		return 0;

	// sceMpegIsEnd: lw reg, 0x40(a0); jr ra; lw v0, 0(reg)
	if ((s_nEndBlock == sPC + 12) && (memRead32(sPC + 4) == 0x03e00008))
	{
		const u32 code = memRead32(sPC);
		const u32 p1 = 0x8c800040;
		const u32 p2 = 0x8c020000 | (code & 0x1f0000) << 5;
		if ((code & 0xffe0ffff) != p1)
			return 0;
		if (memRead32(sPC + 8) != p2)
			return 0;
		skipMPEGReturnTrue_emit_oaknut();
		iBranchTest();
		g_branch = 1;
		pc = s_nEndBlock;
		return 1;
	}
	return 0;
}

static u8* recSkipTimeoutLoop_emit_oaknut(s32 reg)
{
	const s64 v0_offset = static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[0].UL[0]) + reg * sizeof(GPR_reg));
	u8* link_patch = nullptr;

	recBeginOaknutEmit();
	// Flush RECCYCLE to memory so cycle is up-to-date
	recFlushReccycle();
	oakLoad32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakLoad32(oak::util::W1, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.nextEventCycle))});
	oakAsm->CMP(oak::util::W0, oak::util::W1);

	oak::Label not_dispatcher;
	oakAsm->B(oak::Cond::CC, not_dispatcher);
	oakAsm->ADD(oak::util::W0, oak::util::W0, 8);
	oakStore32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	recReloadReccycle();
	oakEmitJmp(DispatcherEvent);

	oakAsm->l(not_dispatcher);
	oakLoad32(oak::util::W2, {oak::util::X27, v0_offset});
	oakAsm->ADD(oak::util::W3, oak::util::W0, oak::util::W2, oak::AddSubShift::LSL, 3);
	oakAsm->CMP(oak::util::W1, oak::util::W3);
	oakAsm->CSEL(oak::util::W3, oak::util::W1, oak::util::W3, oak::Cond::CC);
	oakStore32(oak::util::W3, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.cycle))});
	oakAsm->SUB(oak::util::W3, oak::util::W3, oak::util::W0);
	oakAsm->LSR(oak::util::W3, oak::util::W3, 3);
	oakAsm->SUBS(oak::util::W2, oak::util::W2, oak::util::W3);
	oakStore32(oak::util::W2, {oak::util::X27, v0_offset});
	recReloadReccycle();
	oakEmitCondBranch(oak::Cond::NE, DispatcherEvent);
	oakAsm->MOV(OAK_WSCRATCH, s_nEndBlock);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	link_patch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();

	return link_patch;
}

static bool recSkipTimeoutLoop(s32 reg, bool is_timeout_loop)
{
	if (!EmuConfig.Speedhacks.WaitLoop || !is_timeout_loop)
		return false;

	// basically, if the time it takes the loop to run is shorter than the
	// time to the next event, then we want to skip ahead to the event, but
	// update v0 to reflect how long the loop would have run for.

	// if (cycle >= nextEventCycle) { jump to dispatcher, we're running late }
	// new_cycles = min(v0 * 8, nextEventCycle)
	// new_v0 = (new_cycles - cycles) / 8
	// if new_v0 > 0 { jump to dispatcher because loop exited early }
	// else new_v0 is 0, so exit loop


	// TODO: In the case where nextEventCycle < cycle because it's overflowed, tack 8
	// cycles onto the event count, so hopefully it'll wrap around. This is pretty
	// gross, but until we switch to 64-bit counters, not many better options.
//	not_dispatcher.SetTarget();

//	recBlocks.Link(HWADDR(s_nEndBlock), xJcc32());
	u8* link_patch = recSkipTimeoutLoop_emit_oaknut(reg);
	recBlocks.Link(HWADDR(s_nEndBlock), reinterpret_cast<s32*>(link_patch));

	g_branch = 1;
	pc = s_nEndBlock;

	return true;
}

static void recEELoadMainHook_emit_oaknut()
{
	recBeginOaknutEmit();
	oakEmitCall(reinterpret_cast<void*>(eeloadHook));
	recEndOaknutEmit();
}

static void recEELoadExecHook_emit_oaknut()
{
	recBeginOaknutEmit();
	oakEmitCall(reinterpret_cast<void*>(eeloadHook2));
	recEndOaknutEmit();
}

static void recGoemonPreloadTlb_emit_oaknut()
{
	recBeginOaknutEmit();
	oakEmitCall(reinterpret_cast<void*>(GoemonPreloadTlb));
	recEndOaknutEmit();
}

static void recGoemonUnloadTlb_emit_oaknut()
{
	recBeginOaknutEmit();
	oakLoad32(oak::util::W0, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.GPR.r[4].UL[0]))});
	oakEmitCall(reinterpret_cast<void*>(GoemonUnloadTlb));
	recEndOaknutEmit();
}

static u8* recShortBlockLink_emit_oaknut(u32 next_pc, u32 scaled_cycles)
{
	u8* link_patch = nullptr;
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, next_pc);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
	// Update RECCYCLE and sync cycle to memory for the linked block
	if (scaled_cycles <= 4095)
		oakAsm->ADD(oak::util::W24, oak::util::W24, (int)scaled_cycles);
	else
	{
		oakAsm->MOV(OAK_WSCRATCH, scaled_cycles);
		oakAsm->ADD(oak::util::W24, oak::util::W24, OAK_WSCRATCH);
	}
	recFlushReccycle();
	link_patch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return link_patch;
}

static void recRecompile(const u32 startpc)
{
	u32 i = 0;
	u32 willbranch3 = 0;

	pxAssert(startpc);

	// if recPtr reached the mem limit reset whole mem
	if (recPtr >= recPtrEnd)
		eeRecNeedsReset = true;

	if (HWADDR(startpc) == VMManager::Internal::GetCurrentELFEntryPoint())
		VMManager::Internal::EntryPointCompilingOnCPUThread();

	if (eeRecNeedsReset)
	{
		eeRecNeedsReset = false;
		recResetRaw();
	}

	oakSetAsmPtr(recPtr, _256kb);
	recPtr = oakStartBlock();

	s_pCurBlock = PC_GETBLOCK(startpc);

	pxAssert(s_pCurBlock->GetFnptr() == (uptr)JITCompile);

	s_pCurBlockEx = recBlocks.Get(HWADDR(startpc));
	pxAssert(!s_pCurBlockEx || s_pCurBlockEx->startpc != HWADDR(startpc));

	s_pCurBlockEx = recBlocks.New(HWADDR(startpc), (uptr)recPtr);

	pxAssert(s_pCurBlockEx);

	if (JitProfiler::IsActive())
	{
		JitProfiler::EmitBlockIncrement(&s_pCurBlockEx->execution_count);
	}
	if (HangTrace::IsActive())
	{
		const u32 first_code = memRead32(startpc);
		HangTrace::EmitBlockTrace(HangTrace::CPU_EE, HWADDR(startpc), first_code);
	}

	if (HWADDR(startpc) == EELOAD_START)
	{
		// The EELOAD _start function is the same across all BIOS versions
		const u32 mainjump = memRead32(EELOAD_START + 0x9c);
		if (mainjump >> 26 == 3) // JAL
			g_eeloadMain = ((EELOAD_START + 0xa0) & 0xf0000000U) | (mainjump << 2 & 0x0fffffffU);
	}

	if (g_eeloadMain && HWADDR(startpc) == HWADDR(g_eeloadMain))
	{
		recEELoadMainHook_emit_oaknut();
		if (VMManager::Internal::IsFastBootInProgress())
		{
			// There are four known versions of EELOAD, identifiable by the location of the 'jal' to the EELOAD function which
			// calls ExecPS2(). The function itself is at the same address in all BIOSs after v1.00-v1.10.
			const u32 typeAexecjump = memRead32(EELOAD_START + 0x470); // v1.00, v1.01?, v1.10?
			const u32 typeBexecjump = memRead32(EELOAD_START + 0x5B0); // v1.20, v1.50, v1.60 (3000x models)
			const u32 typeCexecjump = memRead32(EELOAD_START + 0x618); // v1.60 (3900x models)
			const u32 typeDexecjump = memRead32(EELOAD_START + 0x600); // v1.70, v1.90, v2.00, v2.20, v2.30
			if ((typeBexecjump >> 26 == 3) || (typeCexecjump >> 26 == 3) || (typeDexecjump >> 26 == 3)) // JAL to 0x822B8
				g_eeloadExec = EELOAD_START + 0x2B8;
			else if (typeAexecjump >> 26 == 3) // JAL to 0x82170
				g_eeloadExec = EELOAD_START + 0x170;
			else // There might be other types of EELOAD, because these models' BIOSs have not been examined: 18000, 3500x, 3700x, 5500x, and 7900x. However, all BIOS versions have been examined except for v1.01 and v1.10.
				Console.WriteLn("recRecompile: Could not enable launch arguments for fast boot mode; unidentified BIOS version! Please report this to the PCSX2 developers.");
		}
	}

	if (g_eeloadExec && HWADDR(startpc) == HWADDR(g_eeloadExec)) {
		recEELoadExecHook_emit_oaknut();
    }

	g_branch = 0;

	// reset recomp state variables
	s_nBlockCycles = 0;
	s_nBlockInterlocked = false;
	pc = startpc;
	g_cpuHasConstReg = g_cpuFlushedConstReg = 1;
	pxAssert(g_cpuConstRegs[0].UD[0] == 0);

	_initX86regs();
	_initXMMregs();


	if (EmuConfig.Gamefixes.GoemonTlbHack)
	{
		if (pc == 0x33ad48 || pc == 0x35060c)
		{
			// 0x33ad48 and 0x35060c are the return address of the function (0x356250) that populate the TLB cache
			recGoemonPreloadTlb_emit_oaknut();
		}
		else if (pc == 0x3563b8)
		{
			// Game will unmap some virtual addresses. If a constant address were hardcoded in the block, we would be in a bad situation.
			eeRecNeedsReset = true;
			// 0x3563b8 is the start address of the function that invalidate entry in TLB cache
			recGoemonUnloadTlb_emit_oaknut();
		}
	}

	// go until the next branch
	i = startpc;
	s_nEndBlock = 0xffffffff;
	s_branchTo = -1;

	// Timeout loop speedhack.
	// God of War 2 and other games (e.g. NFS series) have these timeout loops which just spin for a few thousand
	// iterations, usually after kicking something which results in an IRQ, but instead of cancelling the loop,
	// they just let it finish anyway. Such loops look like:
	//
	//   00186D6C addiu  v0,v0, -0x1
	//   00186D70 nop
	//   00186D74 nop
	//   00186D78 nop
	//   00186D7C nop
	//   00186D80 bne    v0, zero, ->$0x00186D6C
	//   00186D84 nop
	//
	// Skipping them entirely seems to have no negative effects, but we skip cycles based on the incoming value
	// if the register being decremented, which appears to vary. So far I haven't seen any which increment instead
	// of decrementing, so we'll limit the test to that to be safe.
	//
	s32 timeout_reg = -1;
	bool is_timeout_loop = true;

	// compile breakpoints as individual blocks
	const int n1 = isBreakpointNeeded(i);
	const int n2 = isMemcheckNeeded(i);
	const int n = std::max<int>(n1, n2);
	if (n != 0)
	{
		s_nEndBlock = i + (n << 2); // n * 4
		goto StartRecomp;
	}

	while (1)
	{
		BASEBLOCK* pblock = PC_GETBLOCK(i);

		// stop before breakpoints
		if (isBreakpointNeeded(i) != 0 || isMemcheckNeeded(i) != 0)
		{
			s_nEndBlock = i;
			break;
		}

		if (i != startpc) // Block size truncation checks.
		{
			if ((i & 0xffc) == 0x0) // breaks blocks at 4k page boundaries
			{
				willbranch3 = 1;
				s_nEndBlock = i;

#ifdef PCSX2_DEVBUILD
				eeRecPerfLog.Write("Pagesplit @ %08X : size=%d insts", startpc, (i - startpc) / 4);
#endif
				break;
			}

			if (pblock->GetFnptr() != (uptr)JITCompile)
			{
				willbranch3 = 1;
				s_nEndBlock = i;
				break;
			}
		}

		//HUH ? PSM ? whut ? THIS IS VIRTUAL ACCESS GOD DAMMIT
		cpuRegs.code = *(int*)PSM(i);

		if (is_timeout_loop)
		{
			if ((cpuRegs.code >> 26) == 8 || (cpuRegs.code >> 26) == 9)
			{
				// addi/addiu
				if (timeout_reg >= 0 || _Rs_ != _Rt_ || _Imm_ >= 0)
					is_timeout_loop = false;
				else
					timeout_reg = _Rs_;
			}
			else if ((cpuRegs.code >> 26) == 5)
			{
				// bne
				if (timeout_reg != static_cast<s32>(_Rs_) || _Rt_ != 0 || memRead32(i + 4) != 0)
					is_timeout_loop = false;
			}
			else if (cpuRegs.code != 0)
			{
				is_timeout_loop = false;
			}
		}

		switch (cpuRegs.code >> 26)
		{
			case 0: // special
				if (_Funct_ == 8 || _Funct_ == 9) // JR, JALR
				{
					s_nEndBlock = i + 8;
					goto StartRecomp;
				}
				else if (_Funct_ == 12 || _Funct_ == 13) // SYSCALL, BREAK
				{
					s_nEndBlock = i + 4; // No delay slot.
					goto StartRecomp;
				}
				break;

			case 1: // regimm

				if (_Rt_ < 4 || (_Rt_ >= 16 && _Rt_ < 20))
				{
					// branches
					s_branchTo = (_Imm_ << 2) + i + 4; // _Imm_ * 4
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
			case 20:
			case 21:
			case 22:
			case 23:
				s_branchTo = (_Imm_ << 2) + i + 4; // _Imm_ * 4
				if (s_branchTo > startpc && s_branchTo < i)
					s_nEndBlock = s_branchTo;
				else
					s_nEndBlock = i + 8;

				goto StartRecomp;

			case 16: // cp0
				if (_Rs_ == 16)
				{
					if (_Funct_ == 24) // eret
					{
						s_nEndBlock = i + 4;
						goto StartRecomp;
					}
				}
				// Fall through!
				// COP0's branch opcodes line up with COP1 and COP2's

			case 17: // cp1
			case 18: // cp2
				if (_Rs_ == 8)
				{
					// BC1F, BC1T, BC1FL, BC1TL
					// BC2F, BC2T, BC2FL, BC2TL
					s_branchTo = (_Imm_ << 2) + i + 4; // _Imm_ * 4
					if (s_branchTo > startpc && s_branchTo < i)
						s_nEndBlock = s_branchTo;
					else
						s_nEndBlock = i + 8;

					goto StartRecomp;
				}
				break;
		}

		i += 4;
	}

StartRecomp:

	// The idea here is that as long as a loop doesn't write to a register it's already read
	// (excepting registers initialised with constants or memory loads) or use any instructions
	// which alter the machine state apart from registers, it will do the same thing on every
	// iteration.
	s_nBlockFF = false;
	if (s_branchTo == startpc)
	{
		s_nBlockFF = true;

		u32 reads = 0, loads = 1;

		for (i = startpc; i < s_nEndBlock; i += 4)
		{
			if (i == s_nEndBlock - 8)
				continue;
			cpuRegs.code = *(u32*)PSM(i);
			// nop
			if (cpuRegs.code == 0)
				continue;
			// cache, sync
			else if (_Opcode_ == 057 || (_Opcode_ == 0 && _Funct_ == 017))
				continue;
			// imm arithmetic
			else if ((_Opcode_ & 070) == 010 || (_Opcode_ & 076) == 030)
			{
				if (loads & 1 << _Rs_)
				{
					loads |= 1 << _Rt_;
					continue;
				}
				else
					reads |= 1 << _Rs_;
				if (reads & 1 << _Rt_)
				{
					s_nBlockFF = false;
					break;
				}
			}
			// common register arithmetic instructions
			else if (_Opcode_ == 0 && (_Funct_ & 060) == 040 && (_Funct_ & 076) != 050)
			{
				if (loads & 1 << _Rs_ && loads & 1 << _Rt_)
				{
					loads |= 1 << _Rd_;
					continue;
				}
				else
					reads |= 1 << _Rs_ | 1 << _Rt_;
				if (reads & 1 << _Rd_)
				{
					s_nBlockFF = false;
					break;
				}
			}
			// loads
			else if ((_Opcode_ & 070) == 040 || (_Opcode_ & 076) == 032 || _Opcode_ == 067)
			{
				if (loads & 1 << _Rs_)
				{
					loads |= 1 << _Rt_;
					continue;
				}
				else
					reads |= 1 << _Rs_;
				if (reads & 1 << _Rt_)
				{
					s_nBlockFF = false;
					break;
				}
			}
			// mfc*, cfc*
			else if ((_Opcode_ & 074) == 020 && _Rs_ < 4)
			{
				loads |= 1 << _Rt_;
			}
			else
			{
				s_nBlockFF = false;
				break;
			}
		}
	}
	else
	{
		is_timeout_loop = false;
	}

	// rec info //
	bool has_cop2_instructions = false;
	{
        u32 block_offset = (s_nEndBlock - startpc) >> 2; // (s_nEndBlock - startpc) / 4
		if (s_nInstCacheSize < block_offset + 1)
		{
			const u32 required_size = block_offset + 1;
			const u32 new_size = std::max(required_size, s_nInstCacheSize << 1); // s_nInstCacheSize * 2
			
			EEINST* new_cache = (EEINST*)malloc(sizeof(EEINST) * new_size);
			if (!new_cache)
				pxFailRel("Failed to allocate R5900 InstCache array");
			
			if (s_pInstCache && s_nInstCacheSize > 0)
			{
				memcpy(new_cache, s_pInstCache, sizeof(EEINST) * s_nInstCacheSize);
			}
			
			free(s_pInstCache);
			s_pInstCache = new_cache;
			s_nInstCacheSize = new_size;
		}

		EEINST* pcur = s_pInstCache + block_offset;
		_recClearInst(pcur);
		pcur->info = 0;

		for (i = s_nEndBlock; i > startpc; i -= 4)
		{
			cpuRegs.code = *(int*)PSM(i - 4);
			pcur[-1] = pcur[0];
			recBackpropBSC(cpuRegs.code, pcur - 1, pcur);
			pcur--;

			has_cop2_instructions |= (_Opcode_ == 022 || _Opcode_ == 066 || _Opcode_ == 076);
		}
	}

	// eventually we'll want to have a vector of passes or something.
	if (has_cop2_instructions)
	{
		COP2MicroFinishPass().Run(startpc, s_nEndBlock, s_pInstCache + 1);

		if (EmuConfig.Speedhacks.vuFlagHack)
			COP2FlagHackPass().Run(startpc, s_nEndBlock, s_pInstCache + 1);
	}

	// Detect and handle self-modified code
	memory_protect_recompiled_code(startpc, (s_nEndBlock - startpc) >> 2);

	// Skip Recompilation if sceMpegIsEnd Pattern detected
	const bool doRecompilation = !skipMPEG_By_Pattern(startpc) && !recSkipTimeoutLoop(timeout_reg, is_timeout_loop);

	if (doRecompilation)
	{
		// Finally: Generate ARM64 recompiled code!
		g_pCurInstInfo = s_pInstCache;
		while (!g_branch && pc < s_nEndBlock)
		{
			recompileNextInstruction(false, false); // For the love of recursion, batman!
		}
	}

	pxAssert((pc - startpc) >> 2 <= 0xffff);
	s_pCurBlockEx->size = (pc - startpc) >> 2;

	if (HWADDR(pc) <= Ps2MemSize::ExposedRam)
	{
		BASEBLOCKEX* oldBlock;
		int ii = recBlocks.LastIndex(HWADDR(pc) - 4);
		while ((oldBlock = recBlocks[ii--]))
		{
			if (oldBlock == s_pCurBlockEx)
				continue;
			if (oldBlock->startpc >= HWADDR(pc))
				continue;
			if ((oldBlock->startpc + (oldBlock->size << 2)) <= HWADDR(startpc)) // oldBlock->size * 4
				break;

			if (memcmp(&recRAMCopy[oldBlock->startpc >> 2], PSM(oldBlock->startpc), oldBlock->size << 2)) // oldBlock->startpc / 4, oldBlock->size * 4
			{
				recClear(startpc, (pc - startpc) >> 2); // (pc - startpc) / 4
				s_pCurBlockEx = recBlocks.Get(HWADDR(startpc));
				pxAssert(s_pCurBlockEx->startpc == HWADDR(startpc));
				break;
			}
		}

		memcpy(&recRAMCopy[HWADDR(startpc) >> 2], PSM(startpc), pc - startpc); // HWADDR(startpc) / 4
	}

	s_pCurBlock->SetFnptr((uptr)recPtr);

	if (!(pc & 0x10000000))
		maxrecmem = std::max((pc & ~0xa0000000), maxrecmem);

	if (g_branch == 2)
	{
		// Branch type 2 - This is how I "think" this works (air):
		// Performs a branch/event test but does not actually "break" the block.
		// This allows exceptions to be raised, and is thus sufficient for
		// certain types of things like SYSCALL, EI, etc.  but it is not sufficient
		// for actual branching instructions.

		iFlushCall(FLUSH_EVERYTHING);
		iBranchTest();
	}
	else
	{
		if (g_branch)
			pxAssert(!willbranch3);

		if (willbranch3 || !g_branch)
		{

			iFlushCall(FLUSH_EVERYTHING);

			// Split Block concatenation mode.
			// This code is run when blocks are split either to keep block sizes manageable
			// or because we're crossing a 4k page protection boundary in ps2 mem.  The latter
			// case can result in very short blocks which should not issue branch tests for
			// performance reasons.

			const int numinsts = (pc - startpc) >> 2; // (pc - startpc) / 4
			if (numinsts > 6)
				SetBranchImm(pc);
			else
			{
//				recBlocks.Link(HWADDR(pc), xJcc32());
				u8* link_patch = recShortBlockLink_emit_oaknut(pc, scaleblockcycles());
				recBlocks.Link(HWADDR(pc), reinterpret_cast<s32*>(link_patch));
			}
		}
	}

	pxAssert(oakGetCurrentCodePointer() < SysMemory::GetEERecEnd());

	s_pCurBlockEx->x86size = static_cast<u32>(oakGetCurrentCodePointer() - recPtr);

#if 0
	// Example: Dump both arm64/EE code
	if (startpc == 0x456630) {
		iDumpBlock(s_pCurBlockEx->startpc, s_pCurBlockEx->size*4, s_pCurBlockEx->fnptr, s_pCurBlockEx->x86size);
	}
#endif
	Perf::ee.RegisterPC((void*)s_pCurBlockEx->fnptr, s_pCurBlockEx->x86size, s_pCurBlockEx->startpc);
	JitProfiler::RecordBlockCompile(0, s_pCurBlockEx->startpc, s_pCurBlockEx->size, s_pCurBlockEx->x86size);

	recPtr = oakEndBlock();

	pxAssert((g_cpuHasConstReg & g_cpuFlushedConstReg) == g_cpuHasConstReg);

	s_pCurBlock = nullptr;
	s_pCurBlockEx = nullptr;
}

void EE_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks)
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
		p.type = 0; // EE
		outBlocks.push_back(p);
	}
}

R5900cpu recCpu = {
	recReserve,
	recShutdown,

	recResetEE,
	recStep,
	recExecute,

	recSafeExitExecution,
	recCancelInstruction,
	recClear};

