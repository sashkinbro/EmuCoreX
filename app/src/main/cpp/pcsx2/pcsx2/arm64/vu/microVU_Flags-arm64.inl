// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

static __fi oak::WReg mVUFlagsOakW(int reg)
{
	return oakWRegister(reg);
}

static __fi void mVUFlagsMove32_emit_oaknut(int dst, int src)
{
	recBeginOaknutEmit();
	oakAsm->MOV(mVUFlagsOakW(dst), mVUFlagsOakW(src));
	recEndOaknutEmit();
}

static __fi void mVUFlagsMergeDivFlag_emit_oaknut(mV, int dst, int last, bool copy_last)
{
	recBeginOaknutEmit();
	const oak::WReg dst_w = mVUFlagsOakW(dst);
	if (copy_last)
		oakAsm->MOV(dst_w, mVUFlagsOakW(last));
	oakAsm->MOV(OAK_WSCRATCH, 0xfff3ffff);
	oakAsm->AND(dst_w, dst_w, OAK_WSCRATCH);
	oakLoad32(OAK_WSCRATCH,
		mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].divFlag))));
	oakAsm->ORR(dst_w, dst_w, OAK_WSCRATCH);
	recEndOaknutEmit();
}

static __fi void mVUFlagsShufpsInBlock_oaknut(oak::QReg dst, oak::QReg src, int pIndex)
{
	oakLoad128(OAK_QSCRATCH3,
		mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, shuffle.data[pIndex][1]))));
	oakAsm->MOV(OAK_QSCRATCH.B16(), dst.B16());
	oakAsm->MOV(OAK_QSCRATCH2.B16(), src.B16());
	oakAsm->TBX(dst.B16(), oak::List(OAK_QSCRATCH.B16(), OAK_QSCRATCH2.B16()), OAK_QSCRATCH3.B16());
}

static __fi void mVUFlagsShuffleStoredFlag_emit_oaknut(mV, s64 offset, int shuffle)
{
	recBeginOaknutEmit();
	oakLoad128(OAK_QSCRATCH, mVUAllocOakMvuMem(offset));
	mVUFlagsShufpsInBlock_oaknut(OAK_QSCRATCH, OAK_QSCRATCH, shuffle);
	oakStore128(OAK_QSCRATCH, mVUAllocOakMvuMem(offset));
	recEndOaknutEmit();
}

// Sets FDIV Flags at the proper time
__fi void mVUdivSet(mV)
{
	if (mVUinfo.doDivFlag)
	{
		const int reg32 = getFlagRegId(sFLAG.write);
		mVUFlagsMergeDivFlag_emit_oaknut(mVU, reg32, getFlagRegId(sFLAG.lastWrite), !sFLAG.doFlag);
	}
}

// Optimizes out unneeded status flag updates
// This can safely be done when there is an FSSET opcode
__fi void mVUstatusFlagOp(mV)
{
	int curPC = iPC;
	int i = mVUcount;
	bool runLoop = true;

	if (sFLAG.doFlag)
	{
		sFLAG.doNonSticky = true;
	}
	else
	{
		for (; i > 0; --i)
		{
			incPC2(-2);
			if (sFLAG.doNonSticky)
			{
				runLoop = false;
				break;
			}
			else if (sFLAG.doFlag)
			{
				sFLAG.doNonSticky = true;
				break;
			}
		}
	}
	if (runLoop)
	{
		for (; i > 0; --i)
		{
			incPC2(-2);

			if (sFLAG.doNonSticky)
				break;

			sFLAG.doFlag = false;
		}
	}
	iPC = curPC;
}

int findFlagInst(int* fFlag, int cycles)
{
	int i, j = 0, jValue = -1;
	for (i = 0; i < 4; ++i)
	{
		if ((fFlag[i] <= cycles) && (fFlag[i] > jValue))
		{
			j = i;
			jValue = fFlag[i];
		}
	}
	return j;
}

// Setup Last 4 instances of Status/Mac/Clip flags (needed for accurate block linking)
int sortFlag(int* fFlag, int* bFlag, int cycles)
{
	int lFlag = -5;
	int i, x = 0;
	for (i = 0; i < 4; ++i)
	{
		bFlag[i] = findFlagInst(fFlag, cycles);
		if (lFlag != bFlag[i])
			x++;
		lFlag = bFlag[i];
		cycles++;
	}
	return x; // Returns the number of Valid Flag Instances
}

void sortFullFlag(int* fFlag, int* bFlag)
{
	int m = std::max(std::max(fFlag[0], fFlag[1]), std::max(fFlag[2], fFlag[3]));
    int i, t;
	for (i = 0; i < 4; ++i)
	{
		t = 3 - (m - fFlag[i]);
		bFlag[i] = (t < 0) ? 0 : t + 1;
	}
}

#define sFlagCond (sFLAG.doFlag || mVUlow.isFSSET || mVUinfo.doDivFlag)
#define sHackCond (mVUsFlagHack && !sFLAG.doNonSticky)

// Note: Flag handling is 'very' complex, it requires full knowledge of how microVU recs work, so don't touch!
__fi void mVUsetFlags(mV, microFlagCycles& mFC)
{
	int endPC = iPC;
	u32 aCount = 0; // Amount of instructions needed to get valid mac flag instances for block linking
	//bool writeProtect = false;

	// Ensure last ~4+ instructions update mac/status flags (if next block's first 4 instructions will read them)
    int i;
	for (i = mVUcount; i > 0; --i, ++aCount)
	{
		if (sFLAG.doFlag)
		{

			if (__Mac)
			{
				mFLAG.doFlag = true;
				//writeProtect = true;
			}

			if (__Status)
			{
				sFLAG.doNonSticky = true;
				//writeProtect = true;
			}

			if (aCount >= 3)
			{
				break;
			}
		}
		incPC2(-2);
	}

	// Status/Mac Flags Setup Code
	int xS = 0, xM = 0, xC = 0;
    int cyclesAddFour;

	for (i = 0; i < 4; ++i)
	{
		mFC.xStatus[i] = i;
		mFC.xMac   [i] = i;
		mFC.xClip  [i] = i;
	}

	if (!(mVUpBlock->pState.needExactMatch & 1))
	{
		xS = (mVUpBlock->pState.flagInfo >> 2) & 3;
		mFC.xStatus[0] = -1;
		mFC.xStatus[1] = -1;
		mFC.xStatus[2] = -1;
		mFC.xStatus[3] = -1;
		mFC.xStatus[(xS - 1) & 3] = 0;
	}

	if (!(mVUpBlock->pState.needExactMatch & 2))
	{
		mFC.xMac[0] = -1;
		mFC.xMac[1] = -1;
		mFC.xMac[2] = -1;
		mFC.xMac[3] = -1;
	}

	if (!(mVUpBlock->pState.needExactMatch & 4))
	{
		xC = (mVUpBlock->pState.flagInfo >> 6) & 3;
		mFC.xClip[0] = -1;
		mFC.xClip[1] = -1;
		mFC.xClip[2] = -1;
		mFC.xClip[3] = -1;
		mFC.xClip[(xC - 1) & 3] = 0;
	}

	mFC.cycles = 0;
	u32 xCount = mVUcount; // Backup count
	iPC = mVUstartPC;
	for (mVUcount = 0; mVUcount < xCount; ++mVUcount)
	{
		if (mVUlow.isFSSET && !noFlagOpts)
		{
			if (__Status) // Don't Optimize out on the last ~4+ instructions
			{
				if ((xCount - mVUcount) > aCount)
					mVUstatusFlagOp(mVU);
			}
			else
				mVUstatusFlagOp(mVU);
		}
		mFC.cycles += mVUstall;

		sFLAG.read = doSFlagInsts ? findFlagInst(mFC.xStatus, mFC.cycles) : 0;
		mFLAG.read = doMFlagInsts ? findFlagInst(mFC.xMac,    mFC.cycles) : 0;
		cFLAG.read = doCFlagInsts ? findFlagInst(mFC.xClip,   mFC.cycles) : 0;

		sFLAG.write = doSFlagInsts ? xS : 0;
		mFLAG.write = doMFlagInsts ? xM : 0;
		cFLAG.write = doCFlagInsts ? xC : 0;

		sFLAG.lastWrite = doSFlagInsts ? (xS - 1) & 3 : 0;
		mFLAG.lastWrite = doMFlagInsts ? (xM - 1) & 3 : 0;
		cFLAG.lastWrite = doCFlagInsts ? (xC - 1) & 3 : 0;

		if (sHackCond)
		{
			sFLAG.doFlag = false;
		}

		if (sFLAG.doFlag)
		{
			if (noFlagOpts)
			{
				sFLAG.doNonSticky = true;
				mFLAG.doFlag = true;
			}
		}

        cyclesAddFour = mFC.cycles + 4;

		if (sFlagCond)
		{
			mFC.xStatus[xS] = cyclesAddFour;
			xS = (xS + 1) & 3;
		}

		if (mFLAG.doFlag)
		{
			mFC.xMac[xM] = cyclesAddFour;
			xM = (xM + 1) & 3;
		}

		if (cFLAG.doFlag)
		{
			mFC.xClip[xC] = cyclesAddFour;
			xC = (xC + 1) & 3;
		}

		mFC.cycles++;
		incPC2(2);
	}

	mVUregs.flagInfo |= ((__Status) ? 0 : (xS << 2));
	mVUregs.flagInfo |= /*((__Mac||1) ? 0 :*/ (xM << 4)/*)*/; //TODO: Optimise this? Might help with number of blocks.
	mVUregs.flagInfo |= ((__Clip)   ? 0 : (xC << 6));
	iPC = endPC;
}

#define getFlagReg2Id(x) ((bStatus[0] == x) ? getFlagRegId(x) : VU_HOST_T1)
#define getFlagReg3Id(x) ((gFlag == x) ? VU_HOST_T1 : getFlagRegId(x))
#define getFlagReg4Id(x) ((gFlag == x) ? VU_HOST_T1 : VU_HOST_T2)
#define shuffleMac     ((bMac[3] << 6) | (bMac[2] << 4) | (bMac[1] << 2) | bMac[0])
#define shuffleClip    ((bClip[3] << 6) | (bClip[2] << 4) | (bClip[1] << 2) | bClip[0])

// Recompiles Code for Proper Flags on Block Linkings
__fi void mVUsetupFlags(mV, microFlagCycles& mFC)
{
	if (mVUregs.flagInfo & 1)
	{
		if (mVUregs.needExactMatch)
			DevCon.Error("mVU ERROR!!!");
	}

	if (doSFlagInsts && __Status)
	{
		int bStatus[4];
		int sortRegs = sortFlag(mFC.xStatus, bStatus, mFC.cycles);
		// DevCon::Status("sortRegs = %d", params sortRegs);
        // Note: Emitter will optimize out mov(reg1, reg1) cases...
        if (sortRegs == 1)
        {
            mVUFlagsMove32_emit_oaknut(VU_HOST_F0, getFlagRegId(bStatus[0]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F1, getFlagRegId(bStatus[1]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F2, getFlagRegId(bStatus[2]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F3, getFlagRegId(bStatus[3]));
        }
        else if (sortRegs == 2)
        {
            mVUFlagsMove32_emit_oaknut(VU_HOST_T1, getFlagRegId(bStatus[3]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F0, getFlagRegId(bStatus[0]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F1, getFlagReg2Id(bStatus[1]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F2, getFlagReg2Id(bStatus[2]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F3, VU_HOST_T1);
        }
        else if (sortRegs == 3)
        {
            int gFlag = (bStatus[0] == bStatus[1]) ? bStatus[2] : bStatus[1];
            mVUFlagsMove32_emit_oaknut(VU_HOST_T1, getFlagRegId(gFlag));
            mVUFlagsMove32_emit_oaknut(VU_HOST_T2, getFlagRegId(bStatus[3]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F0, getFlagRegId(bStatus[0]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F1, getFlagReg3Id(bStatus[1]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F2, getFlagReg4Id(bStatus[2]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F3, VU_HOST_T2);
        }
        else
        {
            const int temp3 = mVU.regAlloc->allocGPRId();
            mVUFlagsMove32_emit_oaknut(VU_HOST_T1, getFlagRegId(bStatus[0]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_T2, getFlagRegId(bStatus[1]));
            mVUFlagsMove32_emit_oaknut(temp3, getFlagRegId(bStatus[2]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F3, getFlagRegId(bStatus[3]));
            mVUFlagsMove32_emit_oaknut(VU_HOST_F0, VU_HOST_T1);
            mVUFlagsMove32_emit_oaknut(VU_HOST_F1, VU_HOST_T2);
            mVUFlagsMove32_emit_oaknut(VU_HOST_F2, temp3);
            mVU.regAlloc->clearNeeded(temp3);
        }
	}

	if (doMFlagInsts && __Mac)
	{
		int bMac[4];
		sortFlag(mFC.xMac, bMac, mFC.cycles);
		mVUFlagsShuffleStoredFlag_emit_oaknut(mVU,
			static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].macFlag)), shuffleMac);
	}

	if (doCFlagInsts && __Clip)
	{
		int bClip[4];
		sortFlag(mFC.xClip, bClip, mFC.cycles);
		mVUFlagsShuffleStoredFlag_emit_oaknut(mVU,
			static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].clipFlag)), shuffleClip);
	}
}

#define shortBranch() \
	{ \
		if ((branch == 3) || (branch == 4)) /*Branches*/ \
		{ \
			_mVUflagPass(mVU, aBranchAddr, sCount + found, found, v); \
			if (branch == 3) /*Non-conditional Branch*/ \
				break; \
			branch = 0; \
		} \
		else if (branch == 5) /*JR/JARL*/ \
		{ \
			if (sCount + found < 4) \
				mVUregs.needExactMatch |= 7; \
			break; \
		} \
		else /*E-Bit End*/ \
			break; \
	}

// Scan through instructions and check if flags are read (FSxxx, FMxxx, FCxxx opcodes)
void _mVUflagPass(mV, u32 startPC, u32 sCount, u32 found, std::vector<u32>& v)
{
    u32 i, e = v.size();
	for (i = 0; i < e; ++i)
	{
		if (v[i] == startPC)
			return; // Prevent infinite recursion
	}
	v.push_back(startPC);

	int oldPC = iPC;
	int oldBranch = mVUbranch;
	int aBranchAddr = 0;
	iPC = startPC >> 2; // startPC / 4
	mVUbranch = 0;
    int branch;
	for (branch = 0; sCount < 4; sCount += found)
	{
		mVUregs.needExactMatch &= 7;
		incPC(1);
		mVUopU(mVU, 3);
		found |= (mVUregs.needExactMatch & 8) >> 3;
		mVUregs.needExactMatch &= 7;
		if (curI & _Ebit_)
		{
			// The E-bit epilogue reads the final architectural flags even
			// without an explicit FS/FM/FC instruction. Keep incoming flag
			// instances alive when the next block can exit before replacing
			// them; marking only the terminating block loses earlier writes.
			mVUregs.needExactMatch |= 7;
			branch = 1;
		}
		if (curI & _Tbit_)
		{
			branch = 6;
		}
		if ((curI & _Dbit_) && doDBitHandling)
		{
			branch = 6;
		}
		if (!(curI & _Ibit_))
		{
			incPC(-1);
			mVUopL(mVU, 3);
			incPC(1);
		}

		if (branch >= 2)
		{
			shortBranch();
		}
		else if (branch == 1)
		{
			branch = 2;
		}
		if (mVUbranch)
		{
			branch = ((mVUbranch > 8) ? (5) : ((mVUbranch < 3) ? 3 : 4));
			incPC(-1);
			aBranchAddr = branchAddr(mVU);
			incPC(1);
			mVUbranch = 0;
		}
		incPC(1);
		if ((mVUregs.needExactMatch & 7) == 7)
			break;
	}
	iPC = oldPC;
	mVUbranch = oldBranch;
	mVUregs.needExactMatch &= 7;
	setCode();
}

void mVUflagPass(mV, u32 startPC, u32 sCount = 0, u32 found = 0)
{
	std::vector<u32> v;
	_mVUflagPass(mVU, startPC, sCount, found, v);
}

// Checks if the first ~4 instructions of a block will read flags
void mVUsetFlagInfo(mV)
{
	if (noFlagOpts)
	{
		mVUregs.needExactMatch = 0x7;
		mVUregs.flagInfo = 0x0;
		return;
	}
	if (mVUbranch <= 2) // B/BAL
	{
		incPC(-1);
		mVUflagPass(mVU, branchAddr(mVU));
		incPC(1);

		mVUregs.needExactMatch &= 0x7;
	}
	else if (mVUbranch <= 8) // Conditional Branch
	{
		incPC(-1); // Branch Taken
		mVUflagPass(mVU, branchAddr(mVU));
		int backupFlagInfo = mVUregs.needExactMatch;
		mVUregs.needExactMatch = 0;

		incPC(4); // Branch Not Taken
		mVUflagPass(mVU, xPC);
		incPC(-3);

		mVUregs.needExactMatch |= backupFlagInfo;
		mVUregs.needExactMatch &= 0x7;
	}
	else // JR/JALR
	{
		if (!doConstProp || !mVUlow.constJump.isValid)
		{
			mVUregs.needExactMatch |= 0x7;
		}
		else
		{
			mVUflagPass(mVU, (mVUlow.constJump.regValue * 8) & (mVU.microMemSize - 8));
		}
		mVUregs.needExactMatch &= 0x7;
	}
}
