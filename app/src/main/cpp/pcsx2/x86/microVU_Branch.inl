// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

extern void mVUincCycles(microVU& mVU, int x);
extern void* mVUcompile(microVU& mVU, u32 startPC, uptr pState);
__fi int getLastFlagInst(microRegInfo& pState, int* xFlag, int flagType, int isEbit)
{
	if (isEbit)
		return findFlagInst(xFlag, 0x7fffffff);
	if (pState.needExactMatch & (1 << flagType))
		return 3;
	return (((pState.flagInfo >> (2 * flagType + 2)) & 3) - 1) & 3;
}

void mVU0clearlpStateJIT() { if (!microVU0.prog.cleared) std::memset(&microVU0.prog.lpState, 0, sizeof(microVU1.prog.lpState)); }
void mVU1clearlpStateJIT() { if (!microVU1.prog.cleared) std::memset(&microVU1.prog.lpState, 0, sizeof(microVU1.prog.lpState)); }

void mVUDTendProgram(mV, microFlagCycles* mFC, int isEbit)
{

	int fStatus = getLastFlagInst(mVUpBlock->pState, mFC->xStatus, 0, isEbit);
	int fMac    = getLastFlagInst(mVUpBlock->pState, mFC->xMac, 1, isEbit);
	int fClip   = getLastFlagInst(mVUpBlock->pState, mFC->xClip, 2, isEbit);
	int qInst   = 0;
	int pInst   = 0;
	microBlock stateBackup;
	memcpy(&stateBackup, &mVUregs, sizeof(mVUregs)); //backup the state, it's about to get screwed with.

	mVU.regAlloc->TDwritebackAll(); //Writing back ok, invalidating early kills the rec, so don't do it :P

	if (isEbit)
	{
		mVUincCycles(mVU, 100); // Ensures Valid P/Q instances (And sets all cycle data to 0)
		mVUcycles -= 100;
		qInst = mVU.q;
		pInst = mVU.p;
		mVUregs.xgkickcycles = 0;
		if (mVUinfo.doDivFlag)
		{
			sFLAG.doFlag = true;
			sFLAG.write = fStatus;
			mVUdivSet(mVU);
		}
		//Run any pending XGKick, providing we've got to it.
		if (mVUinfo.doXGKICK && xPC >= mVUinfo.XGKICKPC)
		{
			mVU_XGKICK_DELAY(mVU);
		}
		if (isVU1 && CHECK_XGKICKHACK)
		{
			mVUlow.kickcycles = 99;
			mVU_XGKICK_SYNC(mVU, true);
		}
        if (!isVU1) {
//            xFastCall((void *) mVU0clearlpStateJIT);
            armEmitCall(reinterpret_cast<void*>(mVU0clearlpStateJIT));
        }
        else {
//            xFastCall((void *) mVU1clearlpStateJIT);
            armEmitCall(reinterpret_cast<void*>(mVU1clearlpStateJIT));
        }
	}

    // Save P/Q Regs
    if (qInst) {
//        xPSHUF.D(xmmPQ, xmmPQ, 0xe1);
        armPSHUFD(xmmPQ, xmmPQ, 0xe1);
    }
//	xMOVSS(ptr32[&mVU.regs().VI[REG_Q].UL], xmmPQ);
    armAsm->Str(xmmPQ.S(), PTR_CPU(vuRegs[mVU.index].VI[REG_Q].UL));
//	xPSHUF.D(xmmPQ, xmmPQ, 0xe1);
    armPSHUFD(xmmPQ, xmmPQ, 0xe1);
//	xMOVSS(ptr32[&mVU.regs().pending_q], xmmPQ);
    armAsm->Str(xmmPQ.S(), PTR_CPU(vuRegs[mVU.index].pending_q));
//	xPSHUF.D(xmmPQ, xmmPQ, 0xe1);
    armPSHUFD(xmmPQ, xmmPQ, 0xe1);

    if (isVU1)
    {
        if (pInst) {
//            xPSHUF.D(xmmPQ, xmmPQ, 0xb4); // Swap Pending/Active P
            armPSHUFD(xmmPQ, xmmPQ, 0xb4);
        }
//		xPSHUF.D(xmmPQ, xmmPQ, 0xC6); // 3 0 1 2
        armPSHUFD(xmmPQ, xmmPQ, 0xC6);
//		xMOVSS(ptr32[&mVU.regs().VI[REG_P].UL], xmmPQ);
        armAsm->Str(xmmPQ.S(), PTR_CPU(vuRegs[mVU.index].VI[REG_P].UL));
//		xPSHUF.D(xmmPQ, xmmPQ, 0x87); // 0 2 1 3
        armPSHUFD(xmmPQ, xmmPQ, 0x87);
//		xMOVSS(ptr32[&mVU.regs().pending_p], xmmPQ);
        armAsm->Str(xmmPQ.S(), PTR_CPU(vuRegs[mVU.index].pending_p));
//		xPSHUF.D(xmmPQ, xmmPQ, 0x27); // 3 2 1 0
        armPSHUFD(xmmPQ, xmmPQ, 0x27);
    }

    // Save MAC, Status and CLIP Flag Instances
    mVUallocSFLAGc(gprT1, gprT2, fStatus);
//	xMOV(ptr32[&mVU.regs().VI[REG_STATUS_FLAG].UL], gprT1);
    armAsm->Str(gprT1, PTR_CPU(vuRegs[mVU.index].VI[REG_STATUS_FLAG].UL));
    mVUallocMFLAGa(mVU, gprT1, fMac);
    mVUallocCFLAGa(mVU, gprT2, fClip);
//	xMOV(ptr32[&mVU.regs().VI[REG_MAC_FLAG].UL], gprT1);
    armAsm->Str(gprT1, PTR_CPU(vuRegs[mVU.index].VI[REG_MAC_FLAG].UL));
//	xMOV(ptr32[&mVU.regs().VI[REG_CLIP_FLAG].UL], gprT2);
    armAsm->Str(gprT2, PTR_CPU(vuRegs[mVU.index].VI[REG_CLIP_FLAG].UL));

    if (!isEbit) // Backup flag instances
    {
//		xMOVAPS(xmmT1, ptr128[mVU.macFlag]);
        armAsm->Ldr(xmmT1.Q(), PTR_MVU(microVU[mVU.index].macFlag));
//		xMOVAPS(ptr128[&mVU.regs().micro_macflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_macflags));
//		xMOVAPS(xmmT1, ptr128[mVU.clipFlag]);
        armAsm->Ldr(xmmT1.Q(), PTR_MVU(microVU[mVU.index].clipFlag));
//		xMOVAPS(ptr128[&mVU.regs().micro_clipflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_clipflags));

//		xMOV(ptr32[&mVU.regs().micro_statusflags[0]], gprF0);
        armAsm->Str(gprF0, PTR_CPU(vuRegs[mVU.index].micro_statusflags[0]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[1]], gprF1);
        armAsm->Str(gprF1, PTR_CPU(vuRegs[mVU.index].micro_statusflags[1]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[2]], gprF2);
        armAsm->Str(gprF2, PTR_CPU(vuRegs[mVU.index].micro_statusflags[2]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[3]], gprF3);
        armAsm->Str(gprF3, PTR_CPU(vuRegs[mVU.index].micro_statusflags[3]));
    }
    else // Flush flag instances
    {
//		xMOVDZX(xmmT1, ptr32[&mVU.regs().VI[REG_CLIP_FLAG].UL]);
        armAsm->Ldr(xmmT1, PTR_CPU(vuRegs[mVU.index].VI[REG_CLIP_FLAG].UL));
//		xSHUF.PS(xmmT1, xmmT1, 0);
        armSHUFPS(xmmT1, xmmT1, 0);
//		xMOVAPS(ptr128[&mVU.regs().micro_clipflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_clipflags));

//		xMOVDZX(xmmT1, ptr32[&mVU.regs().VI[REG_MAC_FLAG].UL]);
        armAsm->Ldr(xmmT1, PTR_CPU(vuRegs[mVU.index].VI[REG_MAC_FLAG].UL));
//		xSHUF.PS(xmmT1, xmmT1, 0);
        armSHUFPS(xmmT1, xmmT1, 0);
//		xMOVAPS(ptr128[&mVU.regs().micro_macflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_macflags));

//		xMOVDZX(xmmT1, getFlagReg(fStatus));
        armAsm->Fmov(xmmT1.S(), getFlagReg(fStatus));
//		xSHUF.PS(xmmT1, xmmT1, 0);
        armSHUFPS(xmmT1, xmmT1, 0);
//		xMOVAPS(ptr128[&mVU.regs().micro_statusflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_statusflags));
    }

	if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack) {
//        xMOV(ptr32[&mVU.regs().nextBlockCycles], 0);
        armAsm->Str(a64::wzr, PTR_CPU(vuRegs[mVU.index].nextBlockCycles));
    }

//	xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
    armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));

	if (isEbit) // Clear 'is busy' Flags
	{
		if (!mVU.index || !THREAD_VU1)
		{
//			xAND(ptr32[&VU0.VI[REG_VPU_STAT].UL], (isVU1 ? ~0x100 : ~0x001)); // VBS0/VBS1 flag
            armAnd(PTR_CPU(vuRegs[0].VI[REG_VPU_STAT].UL), (isVU1 ? ~0x100 : ~0x001));
		}
	}

	if (isEbit != 2) // Save PC, and Jump to Exit Point
	{
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUTBit);
            armEmitCall(reinterpret_cast<void*>(mVUTBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
	}

	memcpy(&mVUregs, &stateBackup, sizeof(mVUregs)); //Restore the state for the rest of the recompile
}

void mVUendProgram(mV, microFlagCycles* mFC, int isEbit)
{

	int fStatus = getLastFlagInst(mVUpBlock->pState, mFC->xStatus, 0, isEbit && isEbit != 3);
	int fMac    = getLastFlagInst(mVUpBlock->pState, mFC->xMac, 1, isEbit && isEbit != 3);
	int fClip   = getLastFlagInst(mVUpBlock->pState, mFC->xClip, 2, isEbit && isEbit != 3);
	int qInst   = 0;
	int pInst   = 0;
	microBlock stateBackup;
	memcpy(&stateBackup, &mVUregs, sizeof(mVUregs)); //backup the state, it's about to get screwed with.
	if (!isEbit || isEbit == 3)
		mVU.regAlloc->TDwritebackAll(); //Writing back ok, invalidating early kills the rec, so don't do it :P
	else
		mVU.regAlloc->flushAll();

	if (isEbit && isEbit != 3)
	{
		std::memset(&mVUinfo, 0, sizeof(mVUinfo));
		std::memset(&mVUregsTemp, 0, sizeof(mVUregsTemp));
		mVUincCycles(mVU, 100); // Ensures Valid P/Q instances (And sets all cycle data to 0)
		mVUcycles -= 100;
		qInst = mVU.q;
		pInst = mVU.p;
		mVUregs.xgkickcycles = 0;
		if (mVUinfo.doDivFlag)
		{
			sFLAG.doFlag = true;
			sFLAG.write = fStatus;
			mVUdivSet(mVU);
		}
		if (mVUinfo.doXGKICK)
		{
			mVU_XGKICK_DELAY(mVU);
		}
		if (isVU1 && CHECK_XGKICKHACK)
		{
			mVUlow.kickcycles = 99;
			mVU_XGKICK_SYNC(mVU, true);
		}
        if (!isVU1) {
//            xFastCall((void *) mVU0clearlpStateJIT);
            armEmitCall(reinterpret_cast<void*>(mVU0clearlpStateJIT));
        }
        else {
//            xFastCall((void *) mVU1clearlpStateJIT);
            armEmitCall(reinterpret_cast<void*>(mVU1clearlpStateJIT));
        }
	}

    // Save P/Q Regs
    if (qInst) {
//        xPSHUF.D(xmmPQ, xmmPQ, 0xe1);
        armPSHUFD(xmmPQ, xmmPQ, 0xe1);
    }
//	xMOVSS(ptr32[&mVU.regs().VI[REG_Q].UL], xmmPQ);
    armAsm->Str(xmmPQ.S(), PTR_CPU(vuRegs[mVU.index].VI[REG_Q].UL));
//	xPSHUF.D(xmmPQ, xmmPQ, 0xe1);
    armPSHUFD(xmmPQ, xmmPQ, 0xe1);
//	xMOVSS(ptr32[&mVU.regs().pending_q], xmmPQ);
    armAsm->Str(xmmPQ.S(), PTR_CPU(vuRegs[mVU.index].pending_q));
//	xPSHUF.D(xmmPQ, xmmPQ, 0xe1);
    armPSHUFD(xmmPQ, xmmPQ, 0xe1);

    if (isVU1)
    {
        if (pInst) {
//            xPSHUF.D(xmmPQ, xmmPQ, 0xb4); // Swap Pending/Active P
            armPSHUFD(xmmPQ, xmmPQ, 0xb4);
        }
//		xPSHUF.D(xmmPQ, xmmPQ, 0xC6); // 3 0 1 2
        armPSHUFD(xmmPQ, xmmPQ, 0xC6);
//		xMOVSS(ptr32[&mVU.regs().VI[REG_P].UL], xmmPQ);
        armAsm->Str(xmmPQ.S(), PTR_CPU(vuRegs[mVU.index].VI[REG_P].UL));
//		xPSHUF.D(xmmPQ, xmmPQ, 0x87); // 0 2 1 3
        armPSHUFD(xmmPQ, xmmPQ, 0x87);
//		xMOVSS(ptr32[&mVU.regs().pending_p], xmmPQ);
        armAsm->Str(xmmPQ.S(), PTR_CPU(vuRegs[mVU.index].pending_p));
//		xPSHUF.D(xmmPQ, xmmPQ, 0x27); // 3 2 1 0
        armPSHUFD(xmmPQ, xmmPQ, 0x27);
    }

    // Save MAC, Status and CLIP Flag Instances
    mVUallocSFLAGc(gprT1, gprT2, fStatus);
//	xMOV(ptr32[&mVU.regs().VI[REG_STATUS_FLAG].UL], gprT1);
    armAsm->Str(gprT1, PTR_CPU(vuRegs[mVU.index].VI[REG_STATUS_FLAG].UL));
    mVUallocMFLAGa(mVU, gprT1, fMac);
    mVUallocCFLAGa(mVU, gprT2, fClip);
//	xMOV(ptr32[&mVU.regs().VI[REG_MAC_FLAG].UL], gprT1);
    armAsm->Str(gprT1, PTR_CPU(vuRegs[mVU.index].VI[REG_MAC_FLAG].UL));
//	xMOV(ptr32[&mVU.regs().VI[REG_CLIP_FLAG].UL], gprT2);
    armAsm->Str(gprT2, PTR_CPU(vuRegs[mVU.index].VI[REG_CLIP_FLAG].UL));

    if (!isEbit || isEbit == 3) // Backup flag instances
    {
//		xMOVAPS(xmmT1, ptr128[mVU.macFlag]);
        armAsm->Ldr(xmmT1.Q(), PTR_MVU(microVU[mVU.index].macFlag));
//		xMOVAPS(ptr128[&mVU.regs().micro_macflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_macflags));
//		xMOVAPS(xmmT1, ptr128[mVU.clipFlag]);
        armAsm->Ldr(xmmT1.Q(), PTR_MVU(microVU[mVU.index].clipFlag));
//		xMOVAPS(ptr128[&mVU.regs().micro_clipflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_clipflags));

//		xMOV(ptr32[&mVU.regs().micro_statusflags[0]], gprF0);
        armAsm->Str(gprF0, PTR_CPU(vuRegs[mVU.index].micro_statusflags[0]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[1]], gprF1);
        armAsm->Str(gprF1, PTR_CPU(vuRegs[mVU.index].micro_statusflags[1]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[2]], gprF2);
        armAsm->Str(gprF2, PTR_CPU(vuRegs[mVU.index].micro_statusflags[2]));
//		xMOV(ptr32[&mVU.regs().micro_statusflags[3]], gprF3);
        armAsm->Str(gprF3, PTR_CPU(vuRegs[mVU.index].micro_statusflags[3]));
    }
    else // Flush flag instances
    {
//		xMOVDZX(xmmT1, ptr32[&mVU.regs().VI[REG_CLIP_FLAG].UL]);
        armAsm->Ldr(xmmT1, PTR_CPU(vuRegs[mVU.index].VI[REG_CLIP_FLAG].UL));
//		xSHUF.PS(xmmT1, xmmT1, 0);
        armSHUFPS(xmmT1, xmmT1, 0);
//		xMOVAPS(ptr128[&mVU.regs().micro_clipflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_clipflags));

//		xMOVDZX(xmmT1, ptr32[&mVU.regs().VI[REG_MAC_FLAG].UL]);
        armAsm->Ldr(xmmT1, PTR_CPU(vuRegs[mVU.index].VI[REG_MAC_FLAG].UL));
//		xSHUF.PS(xmmT1, xmmT1, 0);
        armSHUFPS(xmmT1, xmmT1, 0);
//		xMOVAPS(ptr128[&mVU.regs().micro_macflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_macflags));

//		xMOVDZX(xmmT1, getFlagReg(fStatus));
        armAsm->Fmov(xmmT1.S(), getFlagReg(fStatus));
//		xSHUF.PS(xmmT1, xmmT1, 0);
        armSHUFPS(xmmT1, xmmT1, 0);
//		xMOVAPS(ptr128[&mVU.regs().micro_statusflags], xmmT1);
        armAsm->Str(xmmT1.Q(), PTR_CPU(vuRegs[mVU.index].micro_statusflags));
    }

//	xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
    armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));

	if ((isEbit && isEbit != 3)) // Clear 'is busy' Flags
	{
		if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack) {
//            xMOV(ptr32[&mVU.regs().nextBlockCycles], 0);
            armStorePtr(0, PTR_CPU(vuRegs[mVU.index].nextBlockCycles));
        }
		if (!mVU.index || !THREAD_VU1)
		{
//			xAND(ptr32[&VU0.VI[REG_VPU_STAT].UL], (isVU1 ? ~0x100 : ~0x001)); // VBS0/VBS1 flag
            armAnd(PTR_CPU(vuRegs[0].VI[REG_VPU_STAT].UL), (isVU1 ? ~0x100 : ~0x001));
		}
	}
	else if (isEbit)
	{
		if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack) {
//            xMOV(ptr32[&mVU.regs().nextBlockCycles], 0);
            armStorePtr(0, PTR_CPU(vuRegs[mVU.index].nextBlockCycles));
        }
	}

	if (isEbit != 2 && isEbit != 3) // Save PC, and Jump to Exit Point
	{
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUEBit);
            armEmitCall(reinterpret_cast<void*>(mVUEBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
	}
	memcpy(&mVUregs, &stateBackup, sizeof(mVUregs)); //Restore the state for the rest of the recompile
}

// Recompiles Code for Proper Flags and Q/P regs on Block Linkings
void mVUsetupBranch(mV, microFlagCycles& mFC)
{
	mVU.regAlloc->flushAll(); // Flush Allocated Regs
	mVUsetupFlags(mVU, mFC);  // Shuffle Flag Instances

	// Shuffle P/Q regs since every block starts at instance #0
	if (mVU.p || mVU.q) {
//        xPSHUF.D(xmmPQ, xmmPQ, shufflePQ);
        armPSHUFD(xmmPQ, xmmPQ, shufflePQ);
    }
	mVU.p = 0, mVU.q = 0;
}

void normBranchCompile(microVU& mVU, u32 branchPC)
{
	microBlock* pBlock;

    u32 branchPC_8 = branchPC >> 3; // branchPC / 8
	blockCreate(branchPC_8);
	pBlock = mVUblocks[branchPC_8]->search(mVU, (microRegInfo*)&mVUregs);
	if (pBlock) {
//        xJMP(pBlock->x86ptrStart);
        armEmitJmp(pBlock->x86ptrStart);
    }
	else {
        mVUcompile(mVU, branchPC, (uptr) &mVUregs);
    }
}

void normJumpCompile(mV, microFlagCycles& mFC, bool isEvilJump)
{
	memcpy(&mVUpBlock->pStateEnd, &mVUregs, sizeof(microRegInfo));
	mVUsetupBranch(mVU, mFC);
	mVUbackupRegs(mVU);

	if (!mVUpBlock->jumpCache) // Create the jump cache for this block
	{
		mVUpBlock->jumpCache = new microJumpCache[mProgSizeHalf]; // mProgSize / 2
	}

    if (isEvilJump)
    {
//		xMOV(arg1regd, ptr32[&mVU.evilBranch]);
        armAsm->Ldr(EAX, PTR_MVU(microVU[mVU.index].evilBranch));
//		xMOV(gprT1, ptr32[&mVU.evilevilBranch]);
        armAsm->Ldr(EEX, PTR_MVU(microVU[mVU.index].evilevilBranch));
//		xMOV(ptr32[&mVU.evilBranch], gprT1);
        armAsm->Str(EEX, PTR_MVU(microVU[mVU.index].evilBranch));
    }
    else
    {
//        xMOV(arg1regd, ptr32[&mVU.branch]);
        armAsm->Ldr(EAX, PTR_MVU(microVU[mVU.index].branch));
    }
    if (doJumpCaching) {
//        xLoadFarAddr(arg2reg, mVUpBlock);
        armMoveAddressToReg(RCX, mVUpBlock);
    }
    else
    {
//        xLoadFarAddr(arg2reg, &mVUpBlock->pStateEnd);
        armMoveAddressToReg(RCX, &mVUpBlock->pStateEnd);
//        armAsm->Ldr(RXVIXLSCRATCH, PTR_MVU(microVU[mVU.index].prog.IRinfo.pBlock));
//        armAsm->Add(RCX, RXVIXLSCRATCH, offsetof(microBlock, pStateEnd));
    }

	if (mVUup.eBit && isEvilJump) // E-bit EvilJump
	{
		//Xtreme G 3 does 2 conditional jumps, the first contains an E Bit on the first instruction
		//So if it is taken, you need to end the program, else you get infinite loops.
		mVUendProgram(mVU, &mFC, 2);
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], arg1regd);
        armAsm->Str(EAX, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
		if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUEBit);
            armEmitCall(reinterpret_cast<void*>(mVUEBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
	}

	if (!mVU.index) {
//        xFastCall((void *) (void (*)()) mVUcompileJIT<0>, arg1reg, arg2reg); //(u32 startPC, uptr pState)
        armEmitCall(reinterpret_cast<void*>(mVUcompileJIT<0>));
    }
	else {
//        xFastCall((void *) (void (*)()) mVUcompileJIT<1>, arg1reg, arg2reg);
        armEmitCall(reinterpret_cast<void*>(mVUcompileJIT<1>));
    }

	mVUrestoreRegs(mVU);
//	xJMP(gprT1q); // Jump to rec-code address
    armAsm->Br(gprT1q);
}

void normBranch(mV, microFlagCycles& mFC)
{
	// E-bit or T-Bit or D-Bit Branch
	if (mVUup.dBit && doDBitHandling)
	{
		// Flush register cache early to avoid double flush on both paths
		mVU.regAlloc->flushAll(false);

		u32 tempPC = iPC;
        if (mVU.index && THREAD_VU1) {
//            xTEST(ptr32[&vu1Thread.vuFBRST], (isVU1 ? 0x400 : 0x4));
            armAsm->Tst(armLoadPtr(PTR_MVU(vu1Thread.vuFBRST)), (isVU1 ? 0x400 : 0x4));
        }
        else {
//            xTEST(ptr32[&VU0.VI[REG_FBRST].UL], (isVU1 ? 0x400 : 0x4));
            armAsm->Tst(armLoadPtr(PTR_CPU(vuRegs[0].VI[REG_FBRST].UL)), (isVU1 ? 0x400 : 0x4));
        }
//		xForwardJump32 eJMP(Jcc_Zero);
        a64::Label eJMP;
        armAsm->B(&eJMP, a64::Condition::eq);
		if (!mVU.index || !THREAD_VU1)
		{
//			xOR(ptr32[&VU0.VI[REG_VPU_STAT].UL], (isVU1 ? 0x200 : 0x2));
            armOrr(PTR_CPU(vuRegs[0].VI[REG_VPU_STAT].UL), (isVU1 ? 0x200 : 0x2));
//			xOR(ptr32[&mVU.regs().flags], VUFLAG_INTCINTERRUPT);
            armOrr(PTR_CPU(vuRegs[mVU.index].flags), VUFLAG_INTCINTERRUPT);
		}
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUDTendProgram(mVU, &mFC, 1);
//		eJMP.SetTarget();
        armBind(&eJMP);
		iPC = tempPC;
	}
	if (mVUup.tBit)
	{
		// Flush register cache early to avoid double flush on both paths
		mVU.regAlloc->flushAll(false);

		u32 tempPC = iPC;
        if (mVU.index && THREAD_VU1) {
//            xTEST(ptr32[&vu1Thread.vuFBRST], (isVU1 ? 0x800 : 0x8));
            armAsm->Tst(armLoadPtr(PTR_MVU(vu1Thread.vuFBRST)), (isVU1 ? 0x800 : 0x8));
        }
        else {
//            xTEST(ptr32[&VU0.VI[REG_FBRST].UL], (isVU1 ? 0x800 : 0x8));
            armAsm->Tst(armLoadPtr(PTR_CPU(vuRegs[0].VI[REG_FBRST].UL)), (isVU1 ? 0x800 : 0x8));
        }
//		xForwardJump32 eJMP(Jcc_Zero);
        a64::Label eJMP;
        armAsm->B(&eJMP, a64::Condition::eq);
		if (!mVU.index || !THREAD_VU1)
		{
//			xOR(ptr32[&VU0.VI[REG_VPU_STAT].UL], (isVU1 ? 0x400 : 0x4));
            armOrr(PTR_CPU(vuRegs[0].VI[REG_VPU_STAT].UL), (isVU1 ? 0x400 : 0x4));
//			xOR(ptr32[&mVU.regs().flags], VUFLAG_INTCINTERRUPT);
            armOrr(PTR_CPU(vuRegs[mVU.index].flags), VUFLAG_INTCINTERRUPT);
		}
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUDTendProgram(mVU, &mFC, 1);
//		eJMP.SetTarget();
        armBind(&eJMP);
		iPC = tempPC;
	}
	if (mVUup.mBit)
	{
		DevCon.Warning("M-Bit on normal branch, report if broken");
		u32 tempPC = iPC;

		memcpy(&mVUpBlock->pStateEnd, &mVUregs, sizeof(microRegInfo));
//		xLoadFarAddr(rax, &mVUpBlock->pStateEnd);
        armMoveAddressToReg(RAX, &mVUpBlock->pStateEnd);
//        armAsm->Ldr(RXVIXLSCRATCH, PTR_MVU(microVU[mVU.index].prog.IRinfo.pBlock));
//        armAsm->Add(RAX, RXVIXLSCRATCH, offsetof(microBlock, pStateEnd));
//		xCALL((void*)mVU.copyPLState);
        armEmitCall(mVU.copyPLState);

		mVUsetupBranch(mVU, mFC);
		mVUendProgram(mVU, &mFC, 3);
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4;
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
        armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUEBit);
            armEmitCall(reinterpret_cast<void*>(mVUEBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
		iPC = tempPC;
	}
	if (mVUup.eBit)
	{
		if (mVUlow.badBranch)
			DevCon.Warning("End on evil Unconditional branch! - Not implemented! - If game broken report to PCSX2 Team");

		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUendProgram(mVU, &mFC, 1);
		return;
	}

	// Normal Branch
	mVUsetupBranch(mVU, mFC);
	normBranchCompile(mVU, branchAddr(mVU));
}

void condBranch(mV, microFlagCycles& mFC, a64::Condition JMPcc)
{
	mVUsetupBranch(mVU, mFC);

	if (mVUup.tBit)
	{
		DevCon.Warning("T-Bit on branch, please report if broken");
		u32 tempPC = iPC;
        if (mVU.index && THREAD_VU1) {
//            xTEST(ptr32[&vu1Thread.vuFBRST], (isVU1 ? 0x800 : 0x8));
            armAsm->Tst(armLoadPtr(PTR_MVU(vu1Thread.vuFBRST)), (isVU1 ? 0x800 : 0x8));
        }
        else {
//            xTEST(ptr32[&VU0.VI[REG_FBRST].UL], (isVU1 ? 0x800 : 0x8));
            armAsm->Tst(armLoadPtr(PTR_CPU(vuRegs[0].VI[REG_FBRST].UL)), (isVU1 ? 0x800 : 0x8));
        }
//		xForwardJump32 eJMP(Jcc_Zero);
        a64::Label eJMP;
        armAsm->B(&eJMP, a64::Condition::eq);
		if (!mVU.index || !THREAD_VU1)
		{
//			xOR(ptr32[&VU0.VI[REG_VPU_STAT].UL], (isVU1 ? 0x400 : 0x4));
            armOrr(PTR_CPU(vuRegs[0].VI[REG_VPU_STAT].UL), (isVU1 ? 0x400 : 0x4));
//			xOR(ptr32[&mVU.regs().flags], VUFLAG_INTCINTERRUPT);
            armOrr(PTR_CPU(vuRegs[mVU.index].flags), VUFLAG_INTCINTERRUPT);
		}
		mVUDTendProgram(mVU, &mFC, 2);
//		xCMP(ptr16[&mVU.branch], 0);
        armAsm->Cmp(armLdrsh(PTR_MVU(microVU[mVU.index].branch)), a64::wzr);
//		xForwardJump32 tJMP(xInvertCond((JccComparisonType)JMPcc));
        a64::Label tJMP;
        armAsm->B(&tJMP, a64::InvertCondition(JMPcc));
			incPC(4); // Set PC to First instruction of Non-Taken Side
//			xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
            armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
			if (mVU.index && THREAD_VU1) {
//                xFastCall((void *) mVUTBit);
                armEmitCall(reinterpret_cast<void*>(mVUTBit));
            }
//			xJMP(mVU.exitFunct);
            armEmitJmp(mVU.exitFunct);
//		tJMP.SetTarget();
        armBind(&tJMP);
		incPC(-4); // Go Back to Branch Opcode to get branchAddr
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
        armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUTBit);
            armEmitCall(reinterpret_cast<void*>(mVUTBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
//		eJMP.SetTarget();
        armBind(&eJMP);
		iPC = tempPC;
	}
	if (mVUup.dBit && doDBitHandling)
	{
		u32 tempPC = iPC;
        if (mVU.index  && THREAD_VU1) {
//            xTEST(ptr32[&vu1Thread.vuFBRST], (isVU1 ? 0x400 : 0x4));
            armAsm->Tst(armLoadPtr(PTR_MVU(vu1Thread.vuFBRST)), (isVU1 ? 0x400 : 0x4));
        }
        else {
//            xTEST(ptr32[&VU0.VI[REG_FBRST].UL], (isVU1 ? 0x400 : 0x4));
            armAsm->Tst(armLoadPtr(PTR_CPU(vuRegs[0].VI[REG_FBRST].UL)), (isVU1 ? 0x400 : 0x4));
        }
//		xForwardJump32 eJMP(Jcc_Zero);
        a64::Label eJMP;
        armAsm->B(&eJMP, a64::Condition::eq);
		if (!mVU.index || !THREAD_VU1)
		{
//			xOR(ptr32[&VU0.VI[REG_VPU_STAT].UL], (isVU1 ? 0x200 : 0x2));
            armOrr(PTR_CPU(vuRegs[0].VI[REG_VPU_STAT].UL), (isVU1 ? 0x200 : 0x2));
//			xOR(ptr32[&mVU.regs().flags], VUFLAG_INTCINTERRUPT);
            armOrr(PTR_CPU(vuRegs[mVU.index].flags), VUFLAG_INTCINTERRUPT);
		}
		mVUDTendProgram(mVU, &mFC, 2);
//		xCMP(ptr16[&mVU.branch], 0);
        armAsm->Cmp(armLdrsh(PTR_MVU(microVU[mVU.index].branch)), a64::wzr);
//		xForwardJump32 dJMP(xInvertCond((JccComparisonType)JMPcc));
        a64::Label dJMP;
        armAsm->B(&dJMP, a64::InvertCondition(JMPcc));
			incPC(4); // Set PC to First instruction of Non-Taken Side
//			xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
            armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
//			xJMP(mVU.exitFunct);
            armEmitJmp(mVU.exitFunct);
//		dJMP.SetTarget();
        armBind(&dJMP);
		incPC(-4); // Go Back to Branch Opcode to get branchAddr
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
        armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
//		eJMP.SetTarget();
        armBind(&eJMP);
		iPC = tempPC;
	}
	if (mVUup.mBit)
	{
		u32 tempPC = iPC;

		memcpy(&mVUpBlock->pStateEnd, &mVUregs, sizeof(microRegInfo));
//		xLoadFarAddr(rax, &mVUpBlock->pStateEnd);
        armMoveAddressToReg(RAX, &mVUpBlock->pStateEnd);
//        armAsm->Ldr(RXVIXLSCRATCH, PTR_MVU(microVU[mVU.index].prog.IRinfo.pBlock));
//        armAsm->Add(RAX, RXVIXLSCRATCH, offsetof(microBlock, pStateEnd));
//		xCALL((void*)mVU.copyPLState);
        armEmitCall(mVU.copyPLState);

		mVUendProgram(mVU, &mFC, 3);
//		xCMP(ptr16[&mVU.branch], 0);
        armAsm->Cmp(armLdrsh(PTR_MVU(microVU[mVU.index].branch)), a64::wzr);
//		xForwardJump32 dJMP((JccComparisonType)JMPcc);
        a64::Label dJMP;
        armAsm->B(&dJMP, JMPcc);
		incPC(4); // Set PC to First instruction of Non-Taken Side
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
        armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUEBit);
            armEmitCall(reinterpret_cast<void*>(mVUEBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
//		dJMP.SetTarget();
        armBind(&dJMP);
		incPC(-4); // Go Back to Branch Opcode to get branchAddr
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
        armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUEBit);
            armEmitCall(reinterpret_cast<void*>(mVUEBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
		iPC = tempPC;
	}
	if (mVUup.eBit) // Conditional Branch With E-Bit Set
	{
		if (mVUlow.evilBranch)
			DevCon.Warning("End on evil branch! - Not implemented! - If game broken report to PCSX2 Team");

		mVUendProgram(mVU, &mFC, 2);
//		xCMP(ptr16[&mVU.branch], 0);
        armAsm->Cmp(armLdrsh(PTR_MVU(microVU[mVU.index].branch)), a64::wzr);

		incPC(3);
//		xForwardJump32 eJMP(((JccComparisonType)JMPcc));
        a64::Label eJMP;
        armAsm->B(&eJMP, JMPcc);
			incPC(1); // Set PC to First instruction of Non-Taken Side
//			xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
            armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
            if (mVU.index && THREAD_VU1) {
    //                xFastCall((void *) mVUEBit);
                armEmitCall(reinterpret_cast<void*>(mVUEBit));
            }
//			xJMP(mVU.exitFunct);
            armEmitJmp(mVU.exitFunct);
//		eJMP.SetTarget();
        armBind(&eJMP);
		incPC(-4); // Go Back to Branch Opcode to get branchAddr

		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], xPC);
        armStorePtr(xPC, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUEBit);
            armEmitCall(reinterpret_cast<void*>(mVUEBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
		return;
	}
	else // Normal Conditional Branch
	{
//		xCMP(ptr16[&mVU.branch], 0);
        armAsm->Cmp(armLdrsh(PTR_MVU(microVU[mVU.index].branch)), a64::wzr);

		incPC(3);
		microBlock* bBlock;

		incPC2(1); // Check if Branch Non-Taken Side has already been recompiled

        int iPCHalf = iPC >> 1; // iPC / 2
		blockCreate(iPCHalf);
		bBlock = mVUblocks[iPCHalf]->search(mVU, (microRegInfo*)&mVUregs);

		incPC2(-1);
		if (bBlock) // Branch non-taken has already been compiled
		{
//			xJcc(xInvertCond((JccComparisonType)JMPcc), bBlock->x86ptrStart);
            armEmitCondBranch(a64::InvertCondition(JMPcc), bBlock->x86ptrStart);
			incPC(-3); // Go back to branch opcode (to get branch imm addr)
			normBranchCompile(mVU, branchAddr(mVU));
		}
		else
		{
//			s32* ajmp = xJcc32((JccComparisonType)JMPcc);
            ////////////////////////////////////////////////////////////
            a64::Label labelJump;
            armAsm->B(&labelJump, a64::InvertCondition(JMPcc));
            armAsm->Nop();
            s32* ajmp = (s32*)armGetCurrentCodePointer()-1;
            armBind(&labelJump);
            ////////////////////////////////////////////////////////////

			u32 bPC = iPC; // mVUcompile can modify iPC, mVUpBlock, and mVUregs so back them up

			microRegInfo regBackup{};
			memcpy(&regBackup, &mVUregs, sizeof(microRegInfo));

			incPC2(1); // Get PC for branch not-taken
			mVUcompile(mVU, xPC, (uptr)&mVUregs);

			iPC = bPC;
			incPC(-3); // Go back to branch opcode (to get branch imm addr)
			uptr jumpAddr = (uptr)mVUblockFetch(mVU, branchAddr(mVU), (uptr)&regBackup);
//			*ajmp = (jumpAddr - ((uptr)ajmp + 4));
            armEmitJmpPtr(ajmp, (void*)jumpAddr);
		}
	}
}

void normJump(mV, microFlagCycles& mFC)
{
	if (mVUup.mBit)
	{
		DevCon.Warning("M-Bit on Jump! Please report if broken");
	}
	if (mVUlow.constJump.isValid) // Jump Address is Constant
	{
		if (mVUup.eBit) // E-bit Jump
		{
			iPC = (mVUlow.constJump.regValue << 1) & (mVU.progMemMask); // mVUlow.constJump.regValue * 2
			mVUendProgram(mVU, &mFC, 1);
			return;
		}
		int jumpAddr = (mVUlow.constJump.regValue << 3) & (mVU.microMemSize - 8); // mVUlow.constJump.regValue * 8
		mVUsetupBranch(mVU, mFC);
		normBranchCompile(mVU, jumpAddr);
		return;
	}
	if (mVUup.dBit && doDBitHandling)
	{
		// Flush register cache early to avoid double flush on both paths
		mVU.regAlloc->flushAll(false);

        if (THREAD_VU1) {
//            xTEST(ptr32[&vu1Thread.vuFBRST], (isVU1 ? 0x400 : 0x4));
            armAsm->Tst(armLoadPtr(PTR_MVU(vu1Thread.vuFBRST)), (isVU1 ? 0x400 : 0x4));
        }
        else {
//            xTEST(ptr32[&VU0.VI[REG_FBRST].UL], (isVU1 ? 0x400 : 0x4));
            armAsm->Tst(armLoadPtr(PTR_CPU(vuRegs[0].VI[REG_FBRST].UL)), (isVU1 ? 0x400 : 0x4));
        }
//		xForwardJump32 eJMP(Jcc_Zero);
        a64::Label eJMP;
        armAsm->B(&eJMP, a64::Condition::eq);
		if (!mVU.index || !THREAD_VU1)
		{
//			xOR(ptr32[&VU0.VI[REG_VPU_STAT].UL], (isVU1 ? 0x200 : 0x2));
            armOrr(PTR_CPU(vuRegs[0].VI[REG_VPU_STAT].UL), (isVU1 ? 0x200 : 0x2));
//			xOR(ptr32[&mVU.regs().flags], VUFLAG_INTCINTERRUPT);
            armOrr(PTR_CPU(vuRegs[mVU.index].flags), VUFLAG_INTCINTERRUPT);
		}
		mVUDTendProgram(mVU, &mFC, 2);
//		xMOV(gprT1, ptr32[&mVU.branch]);
        armAsm->Ldr(gprT1, PTR_MVU(microVU[mVU.index].branch));
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], gprT1);
        armAsm->Str(gprT1, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
//		eJMP.SetTarget();
        armBind(&eJMP);
	}
	if (mVUup.tBit)
	{
		// Flush register cache early to avoid double flush on both paths
		mVU.regAlloc->flushAll(false);

        if (mVU.index && THREAD_VU1) {
//            xTEST(ptr32[&vu1Thread.vuFBRST], (isVU1 ? 0x800 : 0x8));
            armAsm->Tst(armLoadPtr(PTR_MVU(vu1Thread.vuFBRST)), (isVU1 ? 0x800 : 0x8));
        }
        else {
//            xTEST(ptr32[&VU0.VI[REG_FBRST].UL], (isVU1 ? 0x800 : 0x8));
            armAsm->Tst(armLoadPtr(PTR_CPU(vuRegs[0].VI[REG_FBRST].UL)), (isVU1 ? 0x800 : 0x8));
        }
//		xForwardJump32 eJMP(Jcc_Zero);
        a64::Label eJMP;
        armAsm->B(&eJMP, a64::Condition::eq);
		if (!mVU.index || !THREAD_VU1)
		{
//			xOR(ptr32[&VU0.VI[REG_VPU_STAT].UL], (isVU1 ? 0x400 : 0x4));
            armOrr(PTR_CPU(vuRegs[0].VI[REG_VPU_STAT].UL), (isVU1 ? 0x400 : 0x4));
//			xOR(ptr32[&mVU.regs().flags], VUFLAG_INTCINTERRUPT);
            armOrr(PTR_CPU(vuRegs[mVU.index].flags), VUFLAG_INTCINTERRUPT);
		}
		mVUDTendProgram(mVU, &mFC, 2);
//		xMOV(gprT1, ptr32[&mVU.branch]);
        armAsm->Ldr(gprT1, PTR_MVU(microVU[mVU.index].branch));
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], gprT1);
        armAsm->Str(gprT1, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUTBit);
            armEmitCall(reinterpret_cast<void*>(mVUTBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
//		eJMP.SetTarget();
        armBind(&eJMP);
	}
	if (mVUup.eBit) // E-bit Jump
	{
		mVUendProgram(mVU, &mFC, 2);
//		xMOV(gprT1, ptr32[&mVU.branch]);
        armAsm->Ldr(gprT1, PTR_MVU(microVU[mVU.index].branch));
//		xMOV(ptr32[&mVU.regs().VI[REG_TPC].UL], gprT1);
        armAsm->Str(gprT1, PTR_CPU(vuRegs[mVU.index].VI[REG_TPC].UL));
        if (mVU.index && THREAD_VU1) {
//            xFastCall((void *) mVUEBit);
            armEmitCall(reinterpret_cast<void*>(mVUEBit));
        }
//		xJMP(mVU.exitFunct);
        armEmitJmp(mVU.exitFunct);
	}
	else
	{
		normJumpCompile(mVU, mFC, false);
	}
}
