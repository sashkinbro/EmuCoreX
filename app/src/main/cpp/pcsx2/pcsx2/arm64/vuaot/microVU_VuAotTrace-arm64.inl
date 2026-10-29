// SPDX-FileCopyrightText: 2026 ARMSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Adapted from ARMSX2 aa52a1fdc7cd03fb02bd404ebaa9940944e7fc60.
static void mVUbuildBlockTrace(microVU& mVU, u32 startPC, vuaot::MvuBlockTrace& t)
{
	t.vuIndex = mVU.index;
	t.startPC = startPC;
	std::memcpy(t.entryState, &mVUpBlock->pState, sizeof(t.entryState));
	std::memcpy(t.endState, &mVUregs, sizeof(t.endState));
	t.count = mVUcount;
	t.cycles = mVUcycles;
	t.flagHack = mVUsFlagHack ? 1 : 0;
	t.needFlagFinalize = mVU.needFlagFinalize ? 1 : 0;
	const auto vf = [](const microVFreg& r) {
		vuaot::MvuVFRegTrace o;
		o.reg = r.reg; o.x = r.x; o.y = r.y; o.z = r.z; o.w = r.w;
		return o;
	};
	u32 w = mVUstartPC;
	for (u32 i = 0; i < mVUcount; i++)
	{
		const microOp& o = mVU.prog.IRinfo.info[w / 2];
		vuaot::MvuOpTrace x;
		x.pc = (w / 2) * 8;
		x.stall = o.stall;
		x.isBadOp = o.isBadOp; x.isEOB = o.isEOB; x.isBdelay = o.isBdelay;
		x.swapOps = o.swapOps; x.backupVF = o.backupVF;
		x.doXGKICK = o.doXGKICK; x.doDivFlag = o.doDivFlag; x.XGKICKPC = o.XGKICKPC;
		x.readQ = o.readQ; x.writeQ = o.writeQ; x.readP = o.readP; x.writeP = o.writeP;
		x.sFlag = {o.sFlag.doFlag, o.sFlag.doNonSticky, o.sFlag.write, o.sFlag.lastWrite, o.sFlag.read};
		x.mFlag = {o.mFlag.doFlag, o.mFlag.doNonSticky, o.mFlag.write, o.mFlag.lastWrite, o.mFlag.read};
		x.cFlag = {o.cFlag.doFlag, o.cFlag.doNonSticky, o.cFlag.write, o.cFlag.lastWrite, o.cFlag.read};
		x.eBit = o.uOp.eBit; x.iBit = o.uOp.iBit; x.mBit = o.uOp.mBit; x.tBit = o.uOp.tBit; x.dBit = o.uOp.dBit;
		x.uVFwrite = vf(o.uOp.VF_write); x.uVFread0 = vf(o.uOp.VF_read[0]); x.uVFread1 = vf(o.uOp.VF_read[1]);
		x.lVFwrite = vf(o.lOp.VF_write); x.lVFread0 = vf(o.lOp.VF_read[0]); x.lVFread1 = vf(o.lOp.VF_read[1]);
		x.VIwriteReg = o.lOp.VI_write.reg; x.VIwriteUsed = o.lOp.VI_write.used;
		x.VIread0Reg = o.lOp.VI_read[0].reg; x.VIread0Used = o.lOp.VI_read[0].used;
		x.VIread1Reg = o.lOp.VI_read[1].reg; x.VIread1Used = o.lOp.VI_read[1].used;
		x.constJumpValid = o.lOp.constJump.isValid; x.constJumpValue = o.lOp.constJump.regValue;
		x.branch = o.lOp.branch; x.kickcycles = o.lOp.kickcycles;
		x.badBranch = o.lOp.badBranch; x.evilBranch = o.lOp.evilBranch; x.isNOP = o.lOp.isNOP;
		x.isFSSET = o.lOp.isFSSET; x.noWriteVF = o.lOp.noWriteVF; x.backupVI = o.lOp.backupVI;
		x.memReadIs = o.lOp.memReadIs; x.memReadIt = o.lOp.memReadIt; x.readFlags = o.lOp.readFlags;
		x.isMemWrite = o.lOp.isMemWrite; x.isKick = o.lOp.isKick;
		t.ops.push_back(x);
		w = (w + 2) & mVU.progMemMask;
	}
}
