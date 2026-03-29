// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"
#include "cpuinfo.h"

//------------------------------------------------------------------
// Dispatcher Functions
//------------------------------------------------------------------
static bool mvuNeedsFPCRUpdate(mV)
{
	// always update on the vu1 thread
	if (isVU1 && THREAD_VU1)
		return true;

	// otherwise only emit when it's different to the EE
	return EmuConfig.Cpu.FPUFPCR.bitmask != (isVU0 ? EmuConfig.Cpu.VU0FPCR.bitmask : EmuConfig.Cpu.VU1FPCR.bitmask);
}

// Generates the code for entering/exit recompiled blocks
void mVUdispatcherAB(mV)
{
    mVU.startFunct = armGetCurrentCodePointer();
	{
//		xScopedStackFrame frame(false, true);
        armBeginStackFrame();

        // From memory to registry
        armMoveAddressToReg(RSTATE_MVU, &g_vuRegistersPack);
        armMoveAddressToReg(RSTATE_CPU, &g_cpuRegistersPack);

		// = The caller has already put the needed parameters in ecx/edx:
        if (!isVU1) {
//            xFastCall((void*)mVUexecuteVU0, arg1reg, arg2reg);
            armEmitCall(reinterpret_cast<void*>(mVUexecuteVU0));
        }
        else        {
//            xFastCall((void*)mVUexecuteVU1, arg1reg, arg2reg);
            armEmitCall(reinterpret_cast<void*>(mVUexecuteVU1));
        }

		// Load VU's MXCSR state
		if (mvuNeedsFPCRUpdate(mVU)) {
//            xLDMXCSR(ptr32[isVU0 ? &EmuConfig.Cpu.VU0FPCR.bitmask : &EmuConfig.Cpu.VU1FPCR.bitmask]);
            armAsm->Msr(a64::FPCR, armLoad64(isVU0 ? PTR_CPU(Cpu.VU0FPCR.bitmask) : PTR_CPU(Cpu.VU1FPCR.bitmask)));
        }

        // Load Regs
//		xMOVAPS (xmmT1, ptr128[&mVU.regs().VI[REG_P].UL]);
        armAsm->Ldr(xmmT1, PTR_CPU(vuRegs[mVU.index].VI[REG_P].UL));
//		xMOVAPS (xmmPQ, ptr128[&mVU.regs().VI[REG_Q].UL]);
        armAsm->Ldr(xmmPQ, PTR_CPU(vuRegs[mVU.index].VI[REG_Q].UL));
//		xMOVDZX (xmmT2, ptr32[&mVU.regs().pending_q]);
        armAsm->Ldr(xmmT2, PTR_CPU(vuRegs[mVU.index].pending_q));
//		xSHUF.PS(xmmPQ, xmmT1, 0); // wzyx = PPQQ
        armSHUFPS(xmmPQ, xmmT1, 0);
        //Load in other Q instance
//		xPSHUF.D(xmmPQ, xmmPQ, 0xe1);
        armPSHUFD(xmmPQ, xmmPQ, 0xe1);
//		xMOVSS(xmmPQ, xmmT2);
        armAsm->Mov(xmmPQ.S(), 0, xmmT2.S(), 0);
//		xPSHUF.D(xmmPQ, xmmPQ, 0xe1);
        armPSHUFD(xmmPQ, xmmPQ, 0xe1);

        if (isVU1)
        {
            //Load in other P instance
//			xMOVDZX(xmmT2, ptr32[&mVU.regs().pending_p]);
            armAsm->Ldr(xmmT2, PTR_CPU(vuRegs[mVU.index].pending_p));
//			xPSHUF.D(xmmPQ, xmmPQ, 0x1B);
            armPSHUFD(xmmPQ, xmmPQ, 0x1B);
//			xMOVSS(xmmPQ, xmmT2);
            armAsm->Mov(xmmPQ.S(), 0, xmmT2.S(), 0);
//			xPSHUF.D(xmmPQ, xmmPQ, 0x1B);
            armPSHUFD(xmmPQ, xmmPQ, 0x1B);
        }

//		xMOVAPS(xmmT1, ptr128[&mVU.regs().micro_macflags]);
        armAsm->Ldr(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_macflags));
//		xMOVAPS(ptr128[mVU.macFlag], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_MVU(microVU[mVU.index].macFlag));

//		xMOVAPS(xmmT1, ptr128[&mVU.regs().micro_clipflags]);
        armAsm->Ldr(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_clipflags));
//		xMOVAPS(ptr128[mVU.clipFlag], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_MVU(microVU[mVU.index].clipFlag));

//		xMOV(gprF0, ptr32[&mVU.regs().micro_statusflags[0]]);
        armAsm->Ldr(gprF0, PTR_CPU(vuRegs[mVU.index].micro_statusflags[0]));
//		xMOV(gprF1, ptr32[&mVU.regs().micro_statusflags[1]]);
        armAsm->Ldr(gprF1, PTR_CPU(vuRegs[mVU.index].micro_statusflags[1]));
//		xMOV(gprF2, ptr32[&mVU.regs().micro_statusflags[2]]);
        armAsm->Ldr(gprF2, PTR_CPU(vuRegs[mVU.index].micro_statusflags[2]));
//		xMOV(gprF3, ptr32[&mVU.regs().micro_statusflags[3]]);
        armAsm->Ldr(gprF3, PTR_CPU(vuRegs[mVU.index].micro_statusflags[3]));

		// Jump to Recompiled Code Block
//		xJMP(rax);
        armAsm->Br(RAX);

		mVU.exitFunct = armGetCurrentCodePointer();

		// Load EE's MXCSR state
		if (mvuNeedsFPCRUpdate(mVU)) {
//            xLDMXCSR(ptr32[&EmuConfig.Cpu.FPUFPCR.bitmask]);
            armAsm->Msr(a64::FPCR, armLoad64(PTR_CPU(Cpu.FPUFPCR.bitmask)));
        }

		// = The first two DWORD or smaller arguments are passed in ECX and EDX registers;
		//              all other arguments are passed right to left.
        if (!isVU1) {
//            xFastCall((void*)mVUcleanUpVU0);
            armEmitCall(reinterpret_cast<void*>(mVUcleanUpVU0));
        }
        else        {
//            xFastCall((void*)mVUcleanUpVU1);
            armEmitCall(reinterpret_cast<void*>(mVUcleanUpVU1));
        }

        armEndStackFrame();
	}

//	xRET();
    armAsm->Ret();

	Perf::any.Register(mVU.startFunct, static_cast<u32>(mVU.prog.x86start - mVU.startFunct),
		mVU.index ? "VU1StartFunc" : "VU0StartFunc");
}

// Generates the code for resuming/exit xgkick
void mVUdispatcherCD(mV)
{
    mVU.startFunctXG = armGetCurrentCodePointer();
	{
//		xScopedStackFrame frame(false, true);
        armBeginStackFrame();

        // Load VU's MXCSR state
        if (mvuNeedsFPCRUpdate(mVU)) {
//            xLDMXCSR(ptr32[isVU0 ? &EmuConfig.Cpu.VU0FPCR.bitmask : &EmuConfig.Cpu.VU1FPCR.bitmask]);
            armAsm->Msr(a64::FPCR, armLoad64(isVU0 ? PTR_CPU(Cpu.VU0FPCR.bitmask) : PTR_CPU(Cpu.VU1FPCR.bitmask)));
        }

        mVUrestoreRegs(mVU);
//		xMOV(gprF0, ptr32[&mVU.regs().micro_statusflags[0]]);
        armAsm->Ldr(gprF0, PTR_CPU(vuRegs[mVU.index].micro_statusflags[0]));
//		xMOV(gprF1, ptr32[&mVU.regs().micro_statusflags[1]]);
        armAsm->Ldr(gprF1, PTR_CPU(vuRegs[mVU.index].micro_statusflags[1]));
//		xMOV(gprF2, ptr32[&mVU.regs().micro_statusflags[2]]);
        armAsm->Ldr(gprF2, PTR_CPU(vuRegs[mVU.index].micro_statusflags[2]));
//		xMOV(gprF3, ptr32[&mVU.regs().micro_statusflags[3]]);
        armAsm->Ldr(gprF3, PTR_CPU(vuRegs[mVU.index].micro_statusflags[3]));

        // Jump to Recompiled Code Block
//		xJMP(ptrNative[&mVU.resumePtrXG]);
        armEmitJmp(&mVU.resumePtrXG);

        mVU.exitFunctXG = armGetCurrentCodePointer();

        // Backup Status Flag (other regs were backed up on xgkick)
//		xMOV(ptr32[&mVU.regs().micro_statusflags[0]], gprF0);
        armAsm->Str(gprF0, PTR_CPU(vuRegs[mVU.index].micro_statusflags[0]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[1]], gprF1);
        armAsm->Str(gprF1, PTR_CPU(vuRegs[mVU.index].micro_statusflags[1]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[2]], gprF2);
        armAsm->Str(gprF2, PTR_CPU(vuRegs[mVU.index].micro_statusflags[2]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[3]], gprF3);
        armAsm->Str(gprF3, PTR_CPU(vuRegs[mVU.index].micro_statusflags[3]));

        // Load EE's MXCSR state
        if (mvuNeedsFPCRUpdate(mVU)) {
//            xLDMXCSR(ptr32[&EmuConfig.Cpu.FPUFPCR.bitmask]);
            armAsm->Msr(a64::FPCR, armLoad64(PTR_CPU(Cpu.FPUFPCR.bitmask)));
        }

        armEndStackFrame();
	}

//	xRET();
    armAsm->Ret();

	Perf::any.Register(mVU.startFunctXG, static_cast<u32>(mVU.prog.x86start - mVU.startFunctXG),
		mVU.index ? "VU1StartFuncXG" : "VU0StartFuncXG");
}

static void mVUGenerateWaitMTVU(mV)
{
    mVU.waitMTVU = armGetCurrentCodePointer();

    int i;
    for (i = 0; i < static_cast<int>(iREGCNT_GPR); ++i)
    {
        if (!armIsCallerSaved(i) || i == 4)
            continue;

        // T1 often contains the address we're loading when waiting for VU1.
        // T2 isn't used until afterwards, so don't bother saving it.
        if (i == gprT2.GetCode())
            continue;

        armAsm->Push(a64::xzr, a64::XRegister(i));
    }
    //
    for (i = 0; i < static_cast<int>(iREGCNT_XMM); ++i)
    {
        if (!armIsCallerSavedXmm(i))
            continue;

        armAsm->Push(a64::d31, a64::DRegister(i));
    }

    ////
//	xFastCall((void*)mVUwaitMTVU);
    armAsm->Push(a64::xzr, a64::lr);
    armEmitCall((void*)mVUwaitMTVU);
    armAsm->Pop(a64::lr, a64::xzr);
    ////

    for (i = static_cast<int>(iREGCNT_XMM - 1); i >= 0; --i)
    {
        if (!armIsCallerSavedXmm(i))
            continue;

        armAsm->Pop(a64::DRegister(i), a64::d31);
    }
    //
    for (i = static_cast<int>(iREGCNT_GPR - 1); i >= 0; --i)
    {
        if (!armIsCallerSaved(i) || i == 4)
            continue;

        if (i == gprT2.GetCode())
            continue;

        armAsm->Pop(a64::XRegister(i), a64::xzr);
    }

//	xRET();
    armAsm->Ret();

	Perf::any.Register(mVU.waitMTVU, static_cast<u32>(mVU.prog.x86start - mVU.waitMTVU),
		mVU.index ? "VU1WaitMTVU" : "VU0WaitMTVU");
}

static void mVUGenerateCopyPipelineState(mV)
{
    mVU.copyPLState = armGetCurrentCodePointer();
    {
        auto mop_rax = a64::MemOperand(RAX);
        auto mop_lpState = PTR_MVU(microVU[mVU.index].prog.lpState);

//		xMOVAPS(xmm0, ptr[rax]);
        armAsm->Ldr(xmm0, mop_rax);
//		xMOVAPS(xmm1, ptr[rax + 16u]);
        armAsm->Ldr(xmm1, armOffsetMemOperand(mop_rax, 16u));
//		xMOVAPS(xmm2, ptr[rax + 32u]);
        armAsm->Ldr(xmm2, armOffsetMemOperand(mop_rax, 32u));
//		xMOVAPS(xmm3, ptr[rax + 48u]);
        armAsm->Ldr(xmm3, armOffsetMemOperand(mop_rax, 48u));
//		xMOVAPS(xmm4, ptr[rax + 64u]);
        armAsm->Ldr(xmm4, armOffsetMemOperand(mop_rax, 64u));
//		xMOVAPS(xmm5, ptr[rax + 80u]);
        armAsm->Ldr(xmm5, armOffsetMemOperand(mop_rax, 80u));

//		xMOVUPS(ptr[reinterpret_cast<u8*>(&mVU.prog.lpState)], xmm0);
        armAsm->Str(xmm0, mop_lpState);
//		xMOVUPS(ptr[reinterpret_cast<u8*>(&mVU.prog.lpState) + 16u], xmm1);
        armAsm->Str(xmm1, armOffsetMemOperand(mop_lpState, 16u));
//		xMOVUPS(ptr[reinterpret_cast<u8*>(&mVU.prog.lpState) + 32u], xmm2);
        armAsm->Str(xmm2, armOffsetMemOperand(mop_lpState, 32u));
//		xMOVUPS(ptr[reinterpret_cast<u8*>(&mVU.prog.lpState) + 48u], xmm3);
        armAsm->Str(xmm3, armOffsetMemOperand(mop_lpState, 48u));
//		xMOVUPS(ptr[reinterpret_cast<u8*>(&mVU.prog.lpState) + 64u], xmm4);
        armAsm->Str(xmm4, armOffsetMemOperand(mop_lpState, 64u));
//		xMOVUPS(ptr[reinterpret_cast<u8*>(&mVU.prog.lpState) + 80u], xmm5);
        armAsm->Str(xmm5, armOffsetMemOperand(mop_lpState, 80u));
    }

//	xRET();
    armAsm->Ret();

	Perf::any.Register(mVU.copyPLState, static_cast<u32>(mVU.prog.x86start - mVU.copyPLState),
		mVU.index ? "VU1CopyPLState" : "VU0CopyPLState");
}

//------------------------------------------------------------------
// Micro VU - Custom Quick Search
//------------------------------------------------------------------

// Generates a custom optimized block-search function
// Note: Structs must be 16-byte aligned! (GCC doesn't guarantee this)
static void mVUGenerateCompareState(mV)
{
    mVU.compareStateF = armGetCurrentCodePointer();
    {
        auto mop_arg1reg = a64::MemOperand(RCX);
        auto mop_arg2reg = a64::MemOperand(RDX);

//		xMOVAPS  (xmm0, ptr32[arg1reg]);
        armAsm->Ldr(xmm0, mop_arg1reg);
//		xPCMP.EQD(xmm0, ptr32[arg2reg]);
        armAsm->Cmeq(xmm0.V4S(), xmm0.V4S(), armLoadPtrM(mop_arg2reg).V4S());
//		xMOVAPS  (xmm1, ptr32[arg1reg + 0x10]);
        armAsm->Ldr(xmm1, armOffsetMemOperand(mop_arg1reg, 0x10));
//		xPCMP.EQD(xmm1, ptr32[arg2reg + 0x10]);
        armAsm->Cmeq(xmm1.V4S(), xmm1.V4S(), armLoadPtrM(armOffsetMemOperand(mop_arg2reg, 0x10)).V4S());
//		xPAND    (xmm0, xmm1);
        armAsm->And(xmm0.V16B(), xmm0.V16B(), xmm1.V16B());

//		xMOVMSKPS(eax, xmm0);
        armMOVMSKPS(EAX, xmm0);
//		xXOR     (eax, 0xf);
        armAsm->Eor(EAX, EAX, 0xf);

//		xForwardJNZ8 exitPoint;
        a64::Label exitPoint;
        armCbnz(EAX, &exitPoint);

//		xMOVAPS  (xmm0, ptr32[arg1reg + 0x20]);
        armAsm->Ldr(xmm0, armOffsetMemOperand(mop_arg1reg, 0x20));
//		xPCMP.EQD(xmm0, ptr32[arg2reg + 0x20]);
        armAsm->Cmeq(xmm0.V4S(), xmm0.V4S(), armLoadPtrM(armOffsetMemOperand(mop_arg2reg, 0x20)).V4S());
//		xMOVAPS  (xmm1, ptr32[arg1reg + 0x30]);
        armAsm->Ldr(xmm1, armOffsetMemOperand(mop_arg1reg, 0x30));
//		xPCMP.EQD(xmm1, ptr32[arg2reg + 0x30]);
        armAsm->Cmeq(xmm1.V4S(), xmm1.V4S(), armLoadPtrM(armOffsetMemOperand(mop_arg2reg, 0x30)).V4S());
//		xPAND    (xmm0, xmm1);
        armAsm->And(xmm0.V16B(), xmm0.V16B(), xmm1.V16B());

//		xMOVAPS  (xmm1, ptr32[arg1reg + 0x40]);
        armAsm->Ldr(xmm1, armOffsetMemOperand(mop_arg1reg, 0x40));
//		xPCMP.EQD(xmm1, ptr32[arg2reg + 0x40]);
        armAsm->Cmeq(xmm1.V4S(), xmm1.V4S(), armLoadPtrM(armOffsetMemOperand(mop_arg2reg, 0x40)).V4S());
//		xMOVAPS  (xmm2, ptr32[arg1reg + 0x50]);
        armAsm->Ldr(xmm2, armOffsetMemOperand(mop_arg1reg, 0x50));
//		xPCMP.EQD(xmm2, ptr32[arg2reg + 0x50]);
        armAsm->Cmeq(xmm2.V4S(), xmm2.V4S(), armLoadPtrM(armOffsetMemOperand(mop_arg2reg, 0x50)).V4S());
//		xPAND    (xmm1, xmm2);
        armAsm->And(xmm1.V16B(), xmm1.V16B(), xmm2.V16B());
//		xPAND    (xmm0, xmm1);
        armAsm->And(xmm0.V16B(), xmm0.V16B(), xmm1.V16B());

//		xMOVMSKPS(eax, xmm0);
        armMOVMSKPS(EAX, xmm0);
//		xXOR(eax, 0xf);
        armAsm->Eor(EAX, EAX, 0xf);

//		exitPoint.SetTarget();
        armBind(&exitPoint);
    }

//	xRET();
    armAsm->Ret();
}


//------------------------------------------------------------------
// Execution Functions
//------------------------------------------------------------------

// Executes for number of cycles
_mVUt void* mVUexecute(u32 startPC, u32 cycles)
{
	microVU& mVU = mVUx;
	u32 vuLimit = vuIndex ? 0x3ff8 : 0xff8;
	if (startPC > vuLimit + 7)
	{
		DevCon.Warning("microVU%x Warning: startPC = 0x%x, cycles = 0x%x", vuIndex, startPC, cycles);
	}

	mVU.cycles = cycles;
	mVU.totalCycles = cycles;

//	xSetPtr(mVU.prog.x86ptr); // Set x86ptr to where last program left off

	return mVUsearchProg<vuIndex>(startPC & vuLimit, (uptr)&mVU.prog.lpState); // Find and set correct program
}

//------------------------------------------------------------------
// Cleanup Functions
//------------------------------------------------------------------

_mVUt void mVUcleanUp()
{
	microVU& mVU = mVUx;

//	mVU.prog.x86ptr = x86Ptr;

	if ((mVU.prog.x86ptr < mVU.prog.x86start) || (mVU.prog.x86ptr >= mVU.prog.x86end))
	{
		Console.WriteLn(vuIndex ? Color_Orange : Color_Magenta, "microVU%d: Program cache limit reached.", mVU.index);
		mVUreset(mVU, false);
	}

	mVU.cycles = mVU.totalCycles - std::max(0, mVU.cycles);
	mVU.regs().cycle += mVU.cycles;

	if (!vuIndex || !THREAD_VU1)
	{
		u32 cycles_passed = std::min(mVU.cycles, 3000) * EmuConfig.Speedhacks.EECycleSkip;
		if (cycles_passed > 0)
		{
			s32 vu0_offset = VU0.cycle - cpuRegs.cycle;
			cpuRegs.cycle += cycles_passed;

			// VU0 needs to stay in sync with the CPU otherwise things get messy
			// So we need to adjust when VU1 skips cycles also
			if (!vuIndex)
				VU0.cycle = cpuRegs.cycle + vu0_offset;
			else
				VU0.cycle += cycles_passed;
		}
	}
	mVU.profiler.Print();
}

//------------------------------------------------------------------
// Caller Functions
//------------------------------------------------------------------

void* mVUexecuteVU0(u32 startPC, u32 cycles) { return mVUexecute<0>(startPC, cycles); }
void* mVUexecuteVU1(u32 startPC, u32 cycles) { return mVUexecute<1>(startPC, cycles); }
void mVUcleanUpVU0() { mVUcleanUp<0>(); }
void mVUcleanUpVU1() { mVUcleanUp<1>(); }