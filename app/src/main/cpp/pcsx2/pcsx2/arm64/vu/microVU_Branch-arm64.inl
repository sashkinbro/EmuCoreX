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

static __fi OakMemOperand mVUBranchOakCpuMem(s64 offset)
{
	return {oak::util::X27, offset};
}

static __fi OakMemOperand mVUBranchOakMvuMem(s64 offset)
{
	return {oak::util::X28, offset};
}

static __fi void mVUBranchOakPshufd(oak::QReg dst, oak::QReg src, int pIndex)
{
	oakLoad128(OAK_QSCRATCH3, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, shuffle.data[pIndex][0]))));
	oakAsm->TBL(dst.B16(), oak::List(src.B16()), OAK_QSCRATCH3.B16());
}

static __fi void mVUBranchOakStoreXmmPQScalar(s64 offset)
{
	oakAsm->MOV(OAK_WSCRATCH, oakQRegister(VU_HOST_XMMPQ).Selem()[0]);
	oakStore32(OAK_WSCRATCH, mVUBranchOakCpuMem(offset));
}

static __fi void mVUSavePQRegs_emit_oaknut(mV, int qInst, int pInst)
{
	recBeginOaknutEmit();
	const oak::QReg pq = oakQRegister(VU_HOST_XMMPQ);

	if (qInst)
		mVUBranchOakPshufd(pq, pq, 0xe1);
	mVUBranchOakStoreXmmPQScalar(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_Q].UL)));
	mVUBranchOakPshufd(pq, pq, 0xe1);
	mVUBranchOakStoreXmmPQScalar(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].pending_q)));
	mVUBranchOakPshufd(pq, pq, 0xe1);

	if (isVU1)
	{
		if (pInst)
			mVUBranchOakPshufd(pq, pq, 0xb4);
		mVUBranchOakPshufd(pq, pq, 0xC6);
		mVUBranchOakStoreXmmPQScalar(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_P].UL)));
		mVUBranchOakPshufd(pq, pq, 0x87);
		mVUBranchOakStoreXmmPQScalar(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].pending_p)));
		mVUBranchOakPshufd(pq, pq, 0x27);
	}

	recEndOaknutEmit();
}

static __fi void mVUBranchEmitCall_oaknut(const void* fn)
{
	recBeginOaknutEmit();
	oakEmitCall(fn);
	recEndOaknutEmit();
}

static __fi void mVUBranchEmitJmp_oaknut(const void* fn)
{
	recBeginOaknutEmit();
	oakEmitJmp(fn);
	recEndOaknutEmit();
}

static __fi void mVUBranchEmitBrT1_oaknut()
{
	recBeginOaknutEmit();
	oakAsm->BR(oakXRegister(VU_HOST_T1));
	recEndOaknutEmit();
}

static __fi void mVUBranchClearLpState_emit_oaknut(mV)
{
	recBeginOaknutEmit();

	oak::Label done;
	oakLoad32(OAK_WSCRATCH,
		mVUBranchOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].prog.cleared))));
	oakAsm->CBNZ(OAK_WSCRATCH, done);

	static_assert((sizeof(microRegInfo) % 16) == 0);
	oakAsm->MOVI(OAK_QSCRATCH.B16(), 0);
	const s64 lp_state = static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].prog.lpState));
	for (size_t offset = 0; offset < sizeof(microRegInfo); offset += 16)
		oakStore128(OAK_QSCRATCH, mVUBranchOakMvuMem(lp_state + static_cast<s64>(offset)));

	oakAsm->l(done);
	recEndOaknutEmit();
}

static __fi void mVUBranchCallTBit_emit_oaknut()
{
	mVUBranchEmitCall_oaknut(reinterpret_cast<const void*>(mVUTBit));
}

static __fi void mVUBranchCallEBit_emit_oaknut()
{
	mVUBranchEmitCall_oaknut(reinterpret_cast<const void*>(mVUEBit));
}

static __fi void mVUBranchClearNextBlockCycles_emit_oaknut(mV)
{
	recBeginOaknutEmit();
	oakStore32(oak::util::WZR,
		mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].nextBlockCycles))));
	recEndOaknutEmit();
}

static __fi void mVUBranchStoreTpcImm_emit_oaknut(mV, u32 tpc)
{
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, tpc);
	oakStore32(OAK_WSCRATCH,
		mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_TPC].UL))));
	recEndOaknutEmit();
}

static __fi void mVUBranchStoreTpcReg_emit_oaknut(mV, oak::WReg tpc)
{
	recBeginOaknutEmit();
	oakStore32(tpc,
		mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_TPC].UL))));
	recEndOaknutEmit();
}

static __fi void mVUBranchStoreTpcFromBranch_emit_oaknut(mV)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH,
		mVUBranchOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].branch))));
	oakStore32(OAK_WSCRATCH,
		mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_TPC].UL))));
	recEndOaknutEmit();
}

static __fi void mVUBranchTestFbrst_emit_oaknut(mV, u32 bits)
{
	recBeginOaknutEmit();
	if (mVU.index && THREAD_VU1)
		oakLoad32(OAK_WSCRATCH,
			mVUBranchOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, vu1Thread.vuFBRST))));
	else
		oakLoad32(OAK_WSCRATCH,
			mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_FBRST].UL))));
	oakAsm->TST(OAK_WSCRATCH, bits);
	recEndOaknutEmit();
}

static __fi void mVUBranchCmpBranchZero_emit_oaknut(mV)
{
	recBeginOaknutEmit();
	const OakMemOperand branch_mem =
		mVUBranchOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].branch)));
	oakAsm->MOV(OAK_XSCRATCH2, static_cast<u64>(branch_mem.offset));
	oakAsm->ADD(OAK_XSCRATCH2, branch_mem.base, OAK_XSCRATCH2);
	oakAsm->LDRSH(OAK_XSCRATCH, OAK_XSCRATCH2);
	oakAsm->CMP(OAK_XSCRATCH, 0);
	recEndOaknutEmit();
}

static __fi u8* mVUBranchCondPatchpoint_emit_oaknut()
{
	recBeginOaknutEmit();
	u8* branch = oakGetCurrentCodePointer();
	oakAsm->NOP();
	recEndOaknutEmit();
	return branch;
}

static __fi void mVUBranchPatchCondToHere_emit_oaknut(u8* branch, oak::Cond cond)
{
	recBeginOaknutEmit();
	oakPatchCondBranch(branch, oakGetCurrentCodePointer(), cond, false);
	recEndOaknutEmit();
}

static __fi void mVUBranchAndVu0VpuStat_emit_oaknut(u32 mask)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_VPU_STAT].UL))));
	oakAsm->MOV(OAK_WSCRATCH2, mask);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakStore32(OAK_WSCRATCH, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_VPU_STAT].UL))));
	recEndOaknutEmit();
}

static __fi void mVUBranchOrVu0VpuStat_emit_oaknut(u32 bits)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_VPU_STAT].UL))));
	oakAsm->ORR(OAK_WSCRATCH, OAK_WSCRATCH, bits);
	oakStore32(OAK_WSCRATCH, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].VI[REG_VPU_STAT].UL))));
	recEndOaknutEmit();
}

static __fi void mVUBranchOrVuFlags_emit_oaknut(mV, u32 bits)
{
	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].flags))));
	oakAsm->ORR(OAK_WSCRATCH, OAK_WSCRATCH, bits);
	oakStore32(OAK_WSCRATCH, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].flags))));
	recEndOaknutEmit();
}

static __fi void mVUBranchSetupPqShuffle_emit_oaknut(mV)
{
	recBeginOaknutEmit();
	mVUBranchOakPshufd(oakQRegister(VU_HOST_XMMPQ), oakQRegister(VU_HOST_XMMPQ), shufflePQ);
	recEndOaknutEmit();
}

static __fi void mVUBranchLoadJumpArgs_emit_oaknut(mV, bool isEvilJump, const void* blockArg)
{
	recBeginOaknutEmit();
	if (isEvilJump)
	{
		oakLoad32(oak::util::W0, mVUBranchOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].evilBranch))));
		oakLoad32(oak::util::W4, mVUBranchOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].evilevilBranch))));
		oakStore32(oak::util::W4, mVUBranchOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].evilBranch))));
	}
	else
	{
		oakLoad32(oak::util::W0, mVUBranchOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].branch))));
	}
	oakMoveAddressToReg(oak::util::X1, blockArg);
	recEndOaknutEmit();
}

static __fi void mVUBranchTryJumpCacheFastPath_emit_oaknut(mV)
{
	static_assert(sizeof(microJumpCache) == 16);
	static_assert(offsetof(microJumpCache, prog) == 0);
	static_assert(offsetof(microJumpCache, x86ptrStart) == 8);

	recBeginOaknutEmit();

	oak::Label miss;
	const oak::XReg start_pc = oak::util::X0;
	const oak::XReg block = oak::util::X1;
	const oak::XReg jump_cache = oak::util::X2;
	const oak::XReg pc_index = oak::util::X3;
	const oak::XReg cached_prog = oak::util::X4;
	const oak::XReg quick_prog = oak::util::X5;

	oakLoad64(jump_cache, {block, static_cast<s64>(offsetof(microBlock, jumpCache))});
	oakAsm->CBZ(jump_cache, miss);

	oakAsm->LSR(pc_index, start_pc, 3);
	oakAsm->ADD(jump_cache, jump_cache, pc_index, oak::util::LSL, 4);
	oakLoad64(cached_prog, {jump_cache, static_cast<s64>(offsetof(microJumpCache, prog))});
	oakAsm->CBZ(cached_prog, miss);

	oakMoveAddressToReg(quick_prog, mVU.prog.quick);
	static_assert(sizeof(microProgramQuick) == 8);
	oakAsm->ADD(quick_prog, quick_prog, pc_index, oak::util::LSL, 3);
	oakLoad64(quick_prog, {quick_prog, static_cast<s64>(offsetof(microProgramQuick, prog))});
	oakAsm->CMP(cached_prog, quick_prog);
	oakAsm->B(oak::Cond::NE, miss);

	oakLoad64(oakXRegister(VU_HOST_T1), {jump_cache, static_cast<s64>(offsetof(microJumpCache, x86ptrStart))});
	recEndOaknutEmit();

	mVUrestoreRegs(mVU);
	mVUBranchEmitBrT1_oaknut();

	recBeginOaknutEmit();
	oakAsm->l(miss);
	recEndOaknutEmit();
}

static __fi void mVUBranchCopyPipelineState_emit_oaknut(mV, const void* state)
{
	mVUbackupRegs(mVU, true, true);
	recBeginOaknutEmit();
	oakMoveAddressToReg(oak::util::X0, state);
	oakEmitCall(mVU.copyPLState);
	recEndOaknutEmit();
	mVUrestoreRegs(mVU, true, true);
}

static __fi void mVUSaveFlagRegs_emit_oaknut(mV, int fStatus, int fMac, int fClip, bool backupFlagInstances)
{
	mVUallocSFLAGc(VU_HOST_T1, VU_HOST_T2, fStatus);
	recBeginOaknutEmit();
	oakStore32(oakWRegister(VU_HOST_T1),
		mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_STATUS_FLAG].UL))));
	recEndOaknutEmit();

	mVUallocMFLAGa(mVU, VU_HOST_T1, fMac);
	mVUallocCFLAGa(mVU, VU_HOST_T2, fClip);

	recBeginOaknutEmit();
	oakStore32(oakWRegister(VU_HOST_T1),
		mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_MAC_FLAG].UL))));
	oakStore32(oakWRegister(VU_HOST_T2),
		mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_CLIP_FLAG].UL))));

	const oak::QReg flag = oakQRegister(VU_HOST_XMMT1);
	if (backupFlagInstances)
	{
		oakLoad128(flag, {oak::util::X28, static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].macFlag))});
		oakStore128(flag, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_macflags))));
		oakLoad128(flag, {oak::util::X28, static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].clipFlag))});
		oakStore128(flag, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_clipflags))));

		oakStore32(oakWRegister(VU_HOST_F0), mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_statusflags[0]))));
		oakStore32(oakWRegister(VU_HOST_F1), mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_statusflags[1]))));
		oakStore32(oakWRegister(VU_HOST_F2), mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_statusflags[2]))));
		oakStore32(oakWRegister(VU_HOST_F3), mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_statusflags[3]))));
	}
	else
	{
		oakLoad32(OAK_WSCRATCH, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_CLIP_FLAG].UL))));
		oakAsm->DUP(flag.S4(), OAK_WSCRATCH);
		oakStore128(flag, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_clipflags))));

		oakLoad32(OAK_WSCRATCH, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].VI[REG_MAC_FLAG].UL))));
		oakAsm->DUP(flag.S4(), OAK_WSCRATCH);
		oakStore128(flag, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_macflags))));

		oakAsm->MOV(OAK_WSCRATCH, oakWRegister(getFlagRegId(fStatus)));
		oakAsm->DUP(flag.S4(), OAK_WSCRATCH);
		oakStore128(flag, mVUBranchOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].micro_statusflags))));
	}
	recEndOaknutEmit();
}

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
			mVU_XGKICK_DELAY_oaknut(mVU);
		}
		if (isVU1 && CHECK_XGKICKHACK)
		{
			mVUlow.kickcycles = 99;
			mVU_XGKICK_SYNC_oaknut(mVU, true);
		}
		mVUBranchClearLpState_emit_oaknut(mVU);
	}

	mVUSavePQRegs_emit_oaknut(mVU, qInst, pInst);

	mVUSaveFlagRegs_emit_oaknut(mVU, fStatus, fMac, fClip, !isEbit);

	if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack) {
		mVUBranchClearNextBlockCycles_emit_oaknut(mVU);
    }

	mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);

	if (isEbit) // Clear 'is busy' Flags
	{
		if (!mVU.index || !THREAD_VU1)
		{
			mVUBranchAndVu0VpuStat_emit_oaknut(isVU1 ? ~0x100u : ~0x001u);
		}
	}

	if (isEbit != 2) // Save PC, and Jump to Exit Point
	{
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallTBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
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
			mVU_XGKICK_DELAY_oaknut(mVU);
		}
		if (isVU1 && CHECK_XGKICKHACK)
		{
			mVUlow.kickcycles = 99;
			mVU_XGKICK_SYNC_oaknut(mVU, true);
		}
		mVUBranchClearLpState_emit_oaknut(mVU);
	}

	mVUSavePQRegs_emit_oaknut(mVU, qInst, pInst);

	mVUSaveFlagRegs_emit_oaknut(mVU, fStatus, fMac, fClip, !isEbit || isEbit == 3);

	mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);

	if ((isEbit && isEbit != 3)) // Clear 'is busy' Flags
	{
		if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack) {
			mVUBranchClearNextBlockCycles_emit_oaknut(mVU);
        }
		if (!mVU.index || !THREAD_VU1)
		{
			mVUBranchAndVu0VpuStat_emit_oaknut(isVU1 ? ~0x100u : ~0x001u);
		}
	}
	else if (isEbit)
	{
		if (EmuConfig.Gamefixes.VUSyncHack || EmuConfig.Gamefixes.FullVU0SyncHack) {
			mVUBranchClearNextBlockCycles_emit_oaknut(mVU);
        }
	}

	if (isEbit != 2 && isEbit != 3) // Save PC, and Jump to Exit Point
	{
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallEBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
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
		mVUBranchSetupPqShuffle_emit_oaknut(mVU);
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
		mVUBranchEmitJmp_oaknut(pBlock->x86ptrStart);
    }
	else {
        mVUcompile(mVU, branchPC, (uptr)&mVUregs);
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

	mVUBranchLoadJumpArgs_emit_oaknut(mVU, isEvilJump,
		doJumpCaching ? static_cast<const void*>(mVUpBlock) : static_cast<const void*>(&mVUpBlock->pStateEnd));

	if (mVUup.eBit && isEvilJump) // E-bit EvilJump
	{
		//Xtreme G 3 does 2 conditional jumps, the first contains an E Bit on the first instruction
		//So if it is taken, you need to end the program, else you get infinite loops.
		mVUendProgram(mVU, &mFC, 2);
		mVUBranchStoreTpcReg_emit_oaknut(mVU, oak::util::W0);
		if (mVU.index && THREAD_VU1) {
			mVUBranchCallEBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
	}

	if (doJumpCaching && !doJumpAsSameProgram)
		mVUBranchTryJumpCacheFastPath_emit_oaknut(mVU);

	if (!mVU.index) {
		mVUBranchEmitCall_oaknut(reinterpret_cast<const void*>(mVUcompileJIT<0>));
    }
	else {
		mVUBranchEmitCall_oaknut(reinterpret_cast<const void*>(mVUcompileJIT<1>));
    }

	mVUrestoreRegs(mVU);
	mVUBranchEmitBrT1_oaknut();
}

void normBranch(mV, microFlagCycles& mFC)
{
	// E-bit or T-Bit or D-Bit Branch
	if (mVUup.dBit && doDBitHandling)
	{
		// Flush register cache early to avoid double flush on both paths
		mVU.regAlloc->flushAll(false);

		u32 tempPC = iPC;
		mVUBranchTestFbrst_emit_oaknut(mVU, isVU1 ? 0x400u : 0x4u);
		u8* eJMP = mVUBranchCondPatchpoint_emit_oaknut();
		if (!mVU.index || !THREAD_VU1)
		{
			mVUBranchOrVu0VpuStat_emit_oaknut(isVU1 ? 0x200u : 0x2u);
			mVUBranchOrVuFlags_emit_oaknut(mVU, VUFLAG_INTCINTERRUPT);
		}
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUDTendProgram(mVU, &mFC, 1);
//		eJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(eJMP, oak::Cond::EQ);
		iPC = tempPC;
	}
	if (mVUup.tBit)
	{
		// Flush register cache early to avoid double flush on both paths
		mVU.regAlloc->flushAll(false);

		u32 tempPC = iPC;
		mVUBranchTestFbrst_emit_oaknut(mVU, isVU1 ? 0x800u : 0x8u);
		u8* eJMP = mVUBranchCondPatchpoint_emit_oaknut();
		if (!mVU.index || !THREAD_VU1)
		{
			mVUBranchOrVu0VpuStat_emit_oaknut(isVU1 ? 0x400u : 0x4u);
			mVUBranchOrVuFlags_emit_oaknut(mVU, VUFLAG_INTCINTERRUPT);
		}
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUDTendProgram(mVU, &mFC, 1);
//		eJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(eJMP, oak::Cond::EQ);
		iPC = tempPC;
	}
	if (mVUup.mBit)
	{
		DevCon.Warning("M-Bit on normal branch, report if broken");
		u32 tempPC = iPC;

		memcpy(&mVUpBlock->pStateEnd, &mVUregs, sizeof(microRegInfo));
		mVUBranchCopyPipelineState_emit_oaknut(mVU, &mVUpBlock->pStateEnd);

		mVUsetupBranch(mVU, mFC);
		mVUendProgram(mVU, &mFC, 3);
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4;
		mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallEBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
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

void condBranch(mV, microFlagCycles& mFC, oak::Cond JMPcc)
{
	mVUsetupBranch(mVU, mFC);

	if (mVUup.tBit)
	{
		DevCon.Warning("T-Bit on branch, please report if broken");
		u32 tempPC = iPC;
		mVUBranchTestFbrst_emit_oaknut(mVU, isVU1 ? 0x800u : 0x8u);
		u8* eJMP = mVUBranchCondPatchpoint_emit_oaknut();
		if (!mVU.index || !THREAD_VU1)
		{
			mVUBranchOrVu0VpuStat_emit_oaknut(isVU1 ? 0x400u : 0x4u);
			mVUBranchOrVuFlags_emit_oaknut(mVU, VUFLAG_INTCINTERRUPT);
		}
		mVUDTendProgram(mVU, &mFC, 2);
		mVUBranchCmpBranchZero_emit_oaknut(mVU);
		u8* tJMP = mVUBranchCondPatchpoint_emit_oaknut();
			incPC(4); // Set PC to First instruction of Non-Taken Side
			mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
			if (mVU.index && THREAD_VU1) {
				mVUBranchCallTBit_emit_oaknut();
            }
			mVUBranchEmitJmp_oaknut(mVU.exitFunct);
//		tJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(tJMP, oak::invert(JMPcc));
		incPC(-4); // Go Back to Branch Opcode to get branchAddr
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallTBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
//		eJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(eJMP, oak::Cond::EQ);
		iPC = tempPC;
	}
	if (mVUup.dBit && doDBitHandling)
	{
		u32 tempPC = iPC;
		mVUBranchTestFbrst_emit_oaknut(mVU, isVU1 ? 0x400u : 0x4u);
		u8* eJMP = mVUBranchCondPatchpoint_emit_oaknut();
		if (!mVU.index || !THREAD_VU1)
		{
			mVUBranchOrVu0VpuStat_emit_oaknut(isVU1 ? 0x200u : 0x2u);
			mVUBranchOrVuFlags_emit_oaknut(mVU, VUFLAG_INTCINTERRUPT);
		}
		mVUDTendProgram(mVU, &mFC, 2);
		mVUBranchCmpBranchZero_emit_oaknut(mVU);
		u8* dJMP = mVUBranchCondPatchpoint_emit_oaknut();
			incPC(4); // Set PC to First instruction of Non-Taken Side
			mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
			mVUBranchEmitJmp_oaknut(mVU.exitFunct);
//		dJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(dJMP, oak::invert(JMPcc));
		incPC(-4); // Go Back to Branch Opcode to get branchAddr
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
//		eJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(eJMP, oak::Cond::EQ);
		iPC = tempPC;
	}
	if (mVUup.mBit)
	{
		u32 tempPC = iPC;

		memcpy(&mVUpBlock->pStateEnd, &mVUregs, sizeof(microRegInfo));
		mVUBranchCopyPipelineState_emit_oaknut(mVU, &mVUpBlock->pStateEnd);

		mVUendProgram(mVU, &mFC, 3);
		mVUBranchCmpBranchZero_emit_oaknut(mVU);
		u8* dJMP = mVUBranchCondPatchpoint_emit_oaknut();
		incPC(4); // Set PC to First instruction of Non-Taken Side
		mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallEBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
//		dJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(dJMP, JMPcc);
		incPC(-4); // Go Back to Branch Opcode to get branchAddr
		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallEBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
		iPC = tempPC;
	}
	if (mVUup.eBit) // Conditional Branch With E-Bit Set
	{
		if (mVUlow.evilBranch)
			DevCon.Warning("End on evil branch! - Not implemented! - If game broken report to PCSX2 Team");

		mVUendProgram(mVU, &mFC, 2);
		mVUBranchCmpBranchZero_emit_oaknut(mVU);

		incPC(3);
		u8* eJMP = mVUBranchCondPatchpoint_emit_oaknut();
			incPC(1); // Set PC to First instruction of Non-Taken Side
			mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
            if (mVU.index && THREAD_VU1) {
				mVUBranchCallEBit_emit_oaknut();
            }
			mVUBranchEmitJmp_oaknut(mVU.exitFunct);
//		eJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(eJMP, JMPcc);
		incPC(-4); // Go Back to Branch Opcode to get branchAddr

		iPC = branchAddr(mVU) >> 2; // branchAddr(mVU) / 4
		mVUBranchStoreTpcImm_emit_oaknut(mVU, xPC);
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallEBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
		return;
	}
	else // Normal Conditional Branch
	{
		mVUBranchCmpBranchZero_emit_oaknut(mVU);

		incPC(3);
		microBlock* bBlock;

		incPC2(1); // Check if Branch Non-Taken Side has already been recompiled

        int iPCHalf = iPC >> 1; // iPC / 2
		blockCreate(iPCHalf);
		bBlock = mVUblocks[iPCHalf]->search(mVU, (microRegInfo*)&mVUregs);

		incPC2(-1);
		if (bBlock) // Branch non-taken has already been compiled
		{
			recBeginOaknutEmit();
			oakEmitCondBranch(oak::invert(JMPcc), bBlock->x86ptrStart);
			recEndOaknutEmit();
			incPC(-3); // Go back to branch opcode (to get branch imm addr)
			normBranchCompile(mVU, branchAddr(mVU));
		}
		else
		{
//			s32* ajmp = xJcc32((JccComparisonType)JMPcc);
			////////////////////////////////////////////////////////////
			u8* skipTaken = mVUBranchCondPatchpoint_emit_oaknut();
			u8* ajmp = mVUBranchCondPatchpoint_emit_oaknut();
			mVUBranchPatchCondToHere_emit_oaknut(skipTaken, oak::invert(JMPcc));
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
			oakEmitJmpPtr(ajmp, reinterpret_cast<void*>(jumpAddr), false);
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

		mVUBranchTestFbrst_emit_oaknut(mVU, isVU1 ? 0x400u : 0x4u);
		u8* eJMP = mVUBranchCondPatchpoint_emit_oaknut();
		if (!mVU.index || !THREAD_VU1)
		{
			mVUBranchOrVu0VpuStat_emit_oaknut(isVU1 ? 0x200u : 0x2u);
			mVUBranchOrVuFlags_emit_oaknut(mVU, VUFLAG_INTCINTERRUPT);
		}
		mVUDTendProgram(mVU, &mFC, 2);
		mVUBranchStoreTpcFromBranch_emit_oaknut(mVU);
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
//		eJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(eJMP, oak::Cond::EQ);
	}
	if (mVUup.tBit)
	{
		// Flush register cache early to avoid double flush on both paths
		mVU.regAlloc->flushAll(false);

		mVUBranchTestFbrst_emit_oaknut(mVU, isVU1 ? 0x800u : 0x8u);
		u8* eJMP = mVUBranchCondPatchpoint_emit_oaknut();
		if (!mVU.index || !THREAD_VU1)
		{
			mVUBranchOrVu0VpuStat_emit_oaknut(isVU1 ? 0x400u : 0x4u);
			mVUBranchOrVuFlags_emit_oaknut(mVU, VUFLAG_INTCINTERRUPT);
		}
		mVUDTendProgram(mVU, &mFC, 2);
		mVUBranchStoreTpcFromBranch_emit_oaknut(mVU);
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallTBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
//		eJMP.SetTarget();
		mVUBranchPatchCondToHere_emit_oaknut(eJMP, oak::Cond::EQ);
	}
	if (mVUup.eBit) // E-bit Jump
	{
		mVUendProgram(mVU, &mFC, 2);
		mVUBranchStoreTpcFromBranch_emit_oaknut(mVU);
        if (mVU.index && THREAD_VU1) {
			mVUBranchCallEBit_emit_oaknut();
        }
		mVUBranchEmitJmp_oaknut(mVU.exitFunct);
	}
	else
	{
		normJumpCompile(mVU, mFC, false);
	}
}
