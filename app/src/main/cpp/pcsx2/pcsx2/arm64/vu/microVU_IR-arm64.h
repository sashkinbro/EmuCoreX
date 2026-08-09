// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include "arm64/OaknutHelpers-arm64.h"
#include "microVU-arm64.h"
#include <array>

struct regCycleInfo
{
	u8 x : 4;
	u8 y : 4;
	u8 z : 4;
	u8 w : 4;
};

static __fi OakMemOperand mVUIrOakCpuMem(s64 offset)
{
	return {oak::util::X27, offset};
}

static __fi void mVUIrLoadReg_oaknut(int reg, s64 offset, int xyzw)
{
	const oak::QReg dst_q = oakQRegister(reg);
	const oak::SReg dst_s = oakSRegister(reg);
	const oak::DReg dst_d = oakDRegister(reg);
	recBeginOaknutEmit();
	switch (xyzw & 0xf)
	{
		case 8:
			oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
			oakLoadScalar32(dst_s, mVUIrOakCpuMem(offset));
			break;
		case 4:
			oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
			oakLoadScalar32(dst_s, mVUIrOakCpuMem(offset + 4));
			break;
		case 2:
			oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
			oakLoadScalar32(dst_s, mVUIrOakCpuMem(offset + 8));
			break;
		case 1:
			oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
			oakLoadScalar32(dst_s, mVUIrOakCpuMem(offset + 12));
			break;
		case 12:
			oakLoadVector64(dst_d, mVUIrOakCpuMem(offset));
			break;
		default:
			oakLoad128(dst_q, mVUIrOakCpuMem(offset));
			break;
	}
	recEndOaknutEmit();
}

static __fi void mVUIrStoreLane_oaknut(const oak::QReg& src, s64 offset, int src_lane, int dst_lane)
{
	oakAsm->UMOV(OAK_WSCRATCH, src.Selem()[src_lane]);
	oakStore32(OAK_WSCRATCH, mVUIrOakCpuMem(offset + dst_lane * 4));
}

static __fi void mVUIrPshufd_oaknut(int dst, int src, u8 imm)
{
	const oak::QReg dst_q = oakQRegister(dst);
	const oak::QReg src_q = oakQRegister(src);
	oakLoad128(OAK_QSCRATCH3, mVUIrOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, shuffle.data[imm][0]))));
	oakAsm->MOV(OAK_QSCRATCH2.B16(), src_q.B16());
	oakAsm->TBL(dst_q.B16(), oak::List(OAK_QSCRATCH2.B16()), OAK_QSCRATCH3.B16());
}

static __fi void mVUIrSaveReg_oaknut(int reg, s64 offset, int xyzw, bool modXYZW)
{
	const oak::QReg src_q = oakQRegister(reg);
	const int mask = xyzw & 0xf;
	recBeginOaknutEmit();
	if (mask == 0xf)
	{
		oakStore128(src_q, mVUIrOakCpuMem(offset));
		recEndOaknutEmit();
		return;
	}

	if (mask == 4 || mask == 2 || mask == 1)
	{
		const int lane = (mask == 4) ? 1 : ((mask == 2) ? 2 : 3);
		mVUIrStoreLane_oaknut(src_q, offset, modXYZW ? 0 : lane, lane);
		recEndOaknutEmit();
		return;
	}

	if (mask & 8)
		mVUIrStoreLane_oaknut(src_q, offset, 0, 0);
	if (mask & 4)
		mVUIrStoreLane_oaknut(src_q, offset, 1, 1);
	if (mask & 2)
		mVUIrStoreLane_oaknut(src_q, offset, 2, 2);
	if (mask & 1)
		mVUIrStoreLane_oaknut(src_q, offset, 3, 3);
	recEndOaknutEmit();
}

static __fi void mVUIrMergeRegs_oaknut(int dest, int src, int xyzw, bool modXYZW)
{
	xyzw &= 0xf;
	if (dest == src || xyzw == 0)
		return;

	const oak::QReg dst_q = oakQRegister(dest);
	const oak::QReg src_q = oakQRegister(src);
	recBeginOaknutEmit();
	if (xyzw == 0xf)
	{
		oakAsm->MOV(dst_q.B16(), src_q.B16());
		recEndOaknutEmit();
		return;
	}

	if (modXYZW)
	{
		if (xyzw == 1)
		{
			oakAsm->MOV(dst_q.Selem()[3], src_q.Selem()[0]);
			recEndOaknutEmit();
			return;
		}
		if (xyzw == 2)
		{
			oakAsm->MOV(dst_q.Selem()[2], src_q.Selem()[0]);
			recEndOaknutEmit();
			return;
		}
		if (xyzw == 4)
		{
			oakAsm->MOV(dst_q.Selem()[1], src_q.Selem()[0]);
			recEndOaknutEmit();
			return;
		}
	}

	xyzw = ((xyzw & 1) << 3) | ((xyzw & 2) << 1) | ((xyzw & 4) >> 1) | ((xyzw & 8) >> 3);
	for (u32 i = 0; i < 4; ++i)
	{
		if (xyzw & (1u << i))
			oakAsm->MOV(dst_q.Selem()[i], src_q.Selem()[i]);
	}
	recEndOaknutEmit();
}

// microRegInfo is carefully ordered for faster compares.  The "important" information is
// housed in a union that is accessed via 'quick32' so that several u8 fields can be compared
// using a pair of 32-bit equalities.
// vi15 is only used if microVU const-prop is enabled (it is *not* by default).  When constprop
// is disabled the vi15 field acts as additional padding that is required for 16 byte alignment
// needed by the xmm compare.
union alignas(16) microRegInfo
{
	struct
	{
		union
		{
			struct
			{
				u8 needExactMatch; // If set, block needs an exact match of pipeline state
				u8 flagInfo;       // xC * 2 | xM * 2 | xS * 2 | 0 * 1 | fullFlag Valid * 1
				u8 q;
				u8 p;
				u8 xgkick;
				u8 viBackUp;       // VI reg number that was written to on branch-delay slot
				u8 blockType;      // 0 = Normal; 1,2 = Compile one instruction (E-bit/Branch Ending)
				u8 r;
			};
			u64 quick64[1];
			u32 quick32[2];
		};

		u32 xgkickcycles;
		u8 unused;
		u8 vi15v; // 'vi15' constant is valid
		u16 vi15; // Constant Prop Info for vi15

		struct
		{
			u8 VI[16];
			regCycleInfo VF[32];
		};
	};

	u128 full128[96 / sizeof(u128)];
	u64  full64[96 / sizeof(u64)];
	u32  full32[96 / sizeof(u32)];
};

// Note: mVUcustomSearch needs to be updated if this is changed
static_assert(sizeof(microRegInfo) == 96, "microRegInfo was not 96 bytes");

struct microProgram;
struct microJumpCache
{
	microJumpCache() : prog(NULL), x86ptrStart(NULL) {}
	microProgram* prog; // Program to which the entry point below is part of
	void* x86ptrStart;  // Start of code (Entry point for block)
};

struct alignas(16) microBlock
{
	microRegInfo    pState;      // Detailed State of Pipeline
	microRegInfo    pStateEnd;   // Detailed State of Pipeline at End of Block (needed by JR/JALR opcodes)
	u8*             x86ptrStart; // Start of code (Entry point for block)
	microJumpCache* jumpCache;   // Will point to an array of entry points of size [16k/8] if block ends in JR/JALR
	u64             execution_count;
	u32             guest_size;
	u32             host_size;
};

struct microTempRegInfo
{
	regCycleInfo VF[2]; // Holds cycle info for Fd, VF[0] = Upper Instruction, VF[1] = Lower Instruction
	u8 VFreg[2];        // Index of the VF reg
	u8 VI;              // Holds cycle info for Id
	u8 VIreg;           // Index of the VI reg
	u8 q;               // Holds cycle info for Q reg
	u8 p;               // Holds cycle info for P reg
	u8 r;               // Holds cycle info for R reg (Will never cause stalls, but useful to know if R is modified)
	u8 xgkick;          // Holds the cycle info for XGkick
};

struct microVFreg
{
	u8 reg; // Reg Index
	u8 x;   // X vector read/written to?
	u8 y;   // Y vector read/written to?
	u8 z;   // Z vector read/written to?
	u8 w;   // W vector read/written to?
};

struct microVIreg
{
	u8 reg;  // Reg Index
	u8 used; // Reg is Used? (Read/Written)
};

struct microConstInfo
{
	u8  isValid;  // Is the constant in regValue valid?
	u32 regValue; // Constant Value
};

struct microUpperOp
{
	bool eBit;             // Has E-bit set
	bool iBit;             // Has I-bit set
	bool mBit;             // Has M-bit set
	bool tBit;             // Has T-bit set
	bool dBit;             // Has D-bit set
	microVFreg VF_write;   // VF Vectors written to by this instruction
	microVFreg VF_read[2]; // VF Vectors read by this instruction
};

struct microLowerOp
{
	microVFreg VF_write;      // VF Vectors written to by this instruction
	microVFreg VF_read[2];    // VF Vectors read by this instruction
	microVIreg VI_write;      // VI reg written to by this instruction
	microVIreg VI_read[2];    // VI regs read by this instruction
	microConstInfo constJump; // Constant Reg Info for JR/JARL instructions
	u32  branch;     // Branch Type (0 = Not a Branch, 1 = B. 2 = BAL, 3~8 = Conditional Branches, 9 = JR, 10 = JALR)
	u32  kickcycles; // Number of xgkick cycles accumulated by this instruction
	bool badBranch;  // This instruction is a Branch who has another branch in its Delay Slot
	bool evilBranch; // This instruction is a Branch in a Branch Delay Slot (Instruction after badBranch)
	bool isNOP;      // This instruction is a NOP
	bool isFSSET;    // This instruction is a FSSET
	bool noWriteVF;  // Don't write back the result of a lower op to VF reg if upper op writes to same reg (or if VF = 0)
	bool backupVI;   // Backup VI reg to memory if modified before branch (branch uses old VI value unless opcode is ILW or ILWR)
	bool memReadIs;  // Read Is (VI reg) from memory (used by branches)
	bool memReadIt;  // Read If (VI reg) from memory (used by branches)
	bool readFlags;  // Current Instruction reads Status, Mac, or Clip flags
	bool isMemWrite; // Current Instruction writes to VU memory
	bool isKick;     // Op is a kick so don't count kick cycles
};

struct microFlagInst
{
	bool doFlag;      // Update Flag on this Instruction
	bool doNonSticky; // Update O,U,S,Z (non-sticky) bits on this Instruction (status flag only)
	u8   write;       // Points to the instance that should be written to (s-stage write)
	u8   lastWrite;   // Points to the instance that was last written to (most up-to-date flag)
	u8   read;        // Points to the instance that should be read by a lower instruction (t-stage read)
};

struct microFlagCycles
{
	int xStatus[4];
	int xMac[4];
	int xClip[4];
	int cycles;
};

struct microOp
{
	u8   stall;          // Info on how much current instruction stalled
	bool isBadOp;        // Cur Instruction is a bad opcode (not a legal instruction)
	bool isEOB;          // Cur Instruction is last instruction in block (End of Block)
	bool isBdelay;       // Cur Instruction in Branch Delay slot
	bool swapOps;        // Run Lower Instruction before Upper Instruction
	bool backupVF;       // Backup mVUlow.VF_write.reg, and restore it before the Upper Instruction is called
	bool doXGKICK;       // Do XGKICK transfer on this instruction
	u32  XGKICKPC;       // The PC in which the XGKick has taken place, so if we break early (before it) we don run it.
	bool doDivFlag;      // Transfer Div flag to Status Flag on this instruction
	int  readQ;          // Q instance for reading
	int  writeQ;         // Q instance for writing
	int  readP;          // P instance for reading
	int  writeP;         // P instance for writing
	microFlagInst sFlag; // Status Flag Instance Info
	microFlagInst mFlag; // Mac    Flag Instance Info
	microFlagInst cFlag; // Clip   Flag Instance Info
	microUpperOp  uOp;   // Upper Op Info
	microLowerOp  lOp;   // Lower Op Info
};

template <u32 pSize>
struct microIR
{
	microBlock       block;           // Block/Pipeline info
	microBlock*      pBlock;          // Pointer to a block in mVUblocks
	microTempRegInfo regsTemp;        // Temp Pipeline info (used so that new pipeline info isn't conflicting between upper and lower instructions in the same cycle)
	microOp          info[pSize / 2]; // Info for Instructions in current block
	microConstInfo   constReg[16];    // Simple Const Propagation Info for VI regs within blocks
	u8  branch;
	u32 cycles;    // Cycles for current block
	u32 count;     // Number of VU 64bit instructions ran (starts at 0 for each block)
	u32 curPC;     // Current PC
	u32 startPC;   // Start PC for Cur Block
	u32 sFlagHack; // Optimize out all Status flag updates if microProgram doesn't use Status flags
};

//------------------------------------------------------------------
// Reg Alloc
//------------------------------------------------------------------


struct microMapXMM
{
	int  VFreg;    // VF Reg Number Stored (-1 = Temp; 0 = vf0 and will not be written back; 32 = ACC; 33 = I reg)
	int  xyzw;     // xyzw to write back (0 = Don't write back anything AND cached vfReg has all vectors valid)
	int  count;    // Count of when last used
	bool isNeeded; // Is needed for current instruction
	bool isZero;   // Register was loaded from VF00 and doesn't need clamping
};

struct microMapGPR
{
	int VIreg;
	int count;
	bool isNeeded;
	bool dirty;
	bool isZeroExtended;
	bool usable;
};

class microRegAlloc
{
protected:
#if defined(__ANDROID__)
	static const int xmmTotal = 29; // Q29-Q31 are Oaknut scratch registers.
#else
	static const int xmmTotal = iREGCNT_XMM - 1; // PQ register is reserved
#endif
	static const int gprTotal = iREGCNT_GPR;

	std::array<microMapXMM, xmmTotal> xmmMap;
	std::array<microMapGPR, gprTotal> gprMap;

	int         counter; // Current allocation count
	int         index;   // VU0 or VU1

	// DO NOT REMOVE THIS.
	// This is here for a reason. MSVC likes to turn global writes into a load+conditional move+store.
	// That creates a race with the EE thread when we're compiling on the VU thread, even though
	// regAllocCOP2 is false. By adding another level of indirection, it emits a branch instead.
	_xmmregs*   pxmmregs;

	bool        regAllocCOP2;    // Local COP2 check

	// Helper functions to get VU regs
//	VURegs& regs() const { return ::g_cpuRegistersPack.vuRegs[index]; }
//	__fi REG_VI& getVI(uint reg) const { return regs().VI[reg]; }
//	__fi VECTOR& getVF(uint reg) const { return regs().VF[reg]; }

	bool isAllocatableXmm(int reg) const
	{
		if (reg < 0 || reg >= xmmTotal || reg == VU_HOST_XMMPQ)
			return false;

		if (regAllocCOP2)
			return reg < static_cast<int>(iREGCNT_XMM);

#if defined(__ANDROID__)
		// Standalone VU blocks are entered through a normal AArch64 call. Keep
		// generated code off D8-D15 so the dispatcher does not have to preserve them.
		if (reg >= 8 && reg <= 15)
			return false;
#endif

		return true;
	}

	__ri void loadIreg(int reg, int xyzw)
	{
        int i;
		for (i = 0; i < gprTotal; ++i)
		{
			if (gprMap[i].VIreg == REG_I)
			{
				recBeginOaknutEmit();
				const oak::QReg dst_q = oakQRegister(reg);
				oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
				oakAsm->FMOV(oakSRegister(reg), oakWRegister(i));
				if (!_XYZWss(xyzw)) {
					oakAsm->DUP(dst_q.S4(), dst_q.Selem()[0]);
                }
				recEndOaknutEmit();
                return;
			}
		}

		recBeginOaknutEmit();
		const oak::QReg dst_q = oakQRegister(reg);
		oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
		oakLoad32(OAK_WSCRATCH, mVUIrOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VI[REG_I]))));
		oakAsm->FMOV(oakSRegister(reg), OAK_WSCRATCH);
		if (!_XYZWss(xyzw)) {
			oakAsm->DUP(dst_q.S4(), dst_q.Selem()[0]);
        }
		recEndOaknutEmit();
	}

	int findFreeRegRec(int startIdx)
	{
        int i;
		for (i = startIdx; i < xmmTotal; ++i)
		{
			if (isAllocatableXmm(i) && !xmmMap[i].isNeeded)
			{
				int x = findFreeRegRec(i + 1);
				if (x == -1)
					return i;
				return ((xmmMap[i].count < xmmMap[x].count) ? i : x);
			}
		}
		return -1;
	}

	int findFreeReg(int vfreg)
	{
		if (regAllocCOP2)
		{
			return _allocVFtoXMMreg(vfreg, 0);
		}

        int i;
		for (i = 0; i < xmmTotal; ++i)
		{
			if (isAllocatableXmm(i) && !xmmMap[i].isNeeded && (xmmMap[i].VFreg < 0))
			{
				return i; // Reg is not needed and was a temp reg
			}
		}
		int x = findFreeRegRec(0);
		pxAssertMsg(x >= 0, "microVU register allocation failure!");
		return x;
	}

	int findFreeGPRRec(int startIdx)
	{
        int i;
		for (i = startIdx; i < gprTotal; ++i)
		{
			if (gprMap[i].usable && !gprMap[i].isNeeded)
			{
				int x = findFreeGPRRec(i + 1);
				if (x == -1)
					return i;
				return ((gprMap[i].count < gprMap[x].count) ? i : x);
			}
		}
		return -1;
	}

	int findFreeGPR(int vireg)
	{
		if (regAllocCOP2)
			return _allocX86reg(X86TYPE_VIREG, vireg, MODE_COP2);

        int i;
		for (i = 0; i < gprTotal; ++i)
		{
			if (gprMap[i].usable && !gprMap[i].isNeeded && (gprMap[i].VIreg < 0))
			{
				return i; // Reg is not needed and was a temp reg
			}
		}
		int x = findFreeGPRRec(0);
		pxAssertMsg(x >= 0, "microVU register allocation failure!");
		return x;
	}

	void writeVIBackup(int reg);

public:
	microRegAlloc(int _index)
	{
		index = _index;

		// mark gpr registers as usable
		gprMap.fill({0, 0, false, false, false, false});

        uint i, T1 = VU_HOST_T1, T2 = VU_HOST_T2, F0 = VU_HOST_F0, F1 = VU_HOST_F1, F2 = VU_HOST_F2, F3 = VU_HOST_F3;
		for (i = 0; i < gprTotal; ++i)
		{
			if (i == T1 || i == T2 || i == F0 || i == F1 || i == F2 || i == F3
                || i == 4 //i == rsp.GetId()
                || i == 16 || i == 17 || i == 18
				|| i == VU_HOST_MEMBASE
                || i >= iREGCNT_GPR
            ) {
				continue;
			}

            // 19,20,21,22,23,24 <= callee

			gprMap[i].usable = true;
		}

		reset(false);
	}

	// Fully resets the regalloc by clearing all cached data
	void reset(bool cop2mode)
	{
		// we run this at the of cop2, so don't free fprs
		regAllocCOP2 = false;

        int i;
		for (i = 0; i < xmmTotal; ++i)
			clearReg(i);
		for (i = 0; i < gprTotal; ++i)
			clearGPR(i);

		counter = 0;
		regAllocCOP2 = cop2mode;
		pxmmregs = cop2mode ? xmmregs : nullptr;

		if (cop2mode)
		{
			for (i = 0; i < static_cast<int>(iREGCNT_XMM); ++i)
			{
				if (!isAllocatableXmm(i))
					continue;

				if (!pxmmregs[i].inuse || pxmmregs[i].type != XMMTYPE_VFREG)
					continue;

				// we shouldn't have any temp registers in here.. except for PQ, which
				// isn't allocated here yet.
				// pxAssertRel(fprregs[i].reg >= 0, "Valid full register preserved");
				if (pxmmregs[i].reg >= 0)
				{
					pxAssert(pxmmregs[i].reg >= 0);
					pxmmregs[i].needed = false;
					xmmMap[i].isNeeded = false;
					xmmMap[i].VFreg = pxmmregs[i].reg;
					xmmMap[i].xyzw = ((pxmmregs[i].mode & MODE_WRITE) != 0) ? 0xf : 0x0;
				}
			}

			for (i = 0; i < gprTotal; ++i)
			{
				if (!x86regs[i].inuse || x86regs[i].type != X86TYPE_VIREG)
					continue;

				// pxAssertRel(armregs[i].reg >= 0, "Valid full register preserved");
				if (x86regs[i].reg >= 0)
				{
					x86regs[i].needed = false;
					gprMap[i].isNeeded = false;
					gprMap[i].isZeroExtended = false;
					gprMap[i].VIreg = x86regs[i].reg;
					gprMap[i].dirty = ((x86regs[i].mode & MODE_WRITE) != 0);
				}
			}
		}

	}

	int getXmmCount()
	{
		return xmmTotal;
	}

	int getFreeXmmCount()
	{
		int i, count = 0;

		for (i = 0; i < xmmTotal; ++i)
		{
			if (isAllocatableXmm(i) && !xmmMap[i].isNeeded && (xmmMap[i].VFreg < 0))
				count++;
		}

		return count;
	}

	bool hasRegVF(int vfreg)
	{
        int i;
		for (i = 0; i < xmmTotal; ++i)
		{
			if (xmmMap[i].VFreg == vfreg)
				return true;
		}

		return false;
	}

	int getRegVF(int i)
	{
		return (i < xmmTotal) ? xmmMap[i].VFreg : -1;
	}

	int getGPRCount()
	{
		return gprTotal;
	}

	int getFreeGPRCount()
	{
		int i, count = 0;

		for (i = 0; i < gprTotal; ++i)
		{
			if (gprMap[i].usable && (gprMap[i].VIreg < 0))
				count++;
		}

		return count;
	}

	bool hasRegVI(int vireg)
	{
        int i;
		for (i = 0; i < gprTotal; ++i)
		{
			if (gprMap[i].VIreg == vireg)
				return true;
		}

		return false;
	}

	int getRegVI(int i)
	{
		return (i < gprTotal) ? gprMap[i].VIreg : -1;
	}

	// Flushes all allocated registers (i.e. writes-back to memory all modified registers).
	// If clearState is 0, then it keeps cached reg data valid
	// If clearState is 1, then it invalidates all cached reg data after write-back
	void flushAll(bool clearState = true)
	{
        int i;
		for (i = 0; i < xmmTotal; ++i)
		{
			writeBackXmmRegId(i);
			if (clearState)
				clearReg(i);
		}

		for (i = 0; i < gprTotal; ++i)
		{
			writeBackReg(i, true);
			if (clearState)
				clearGPR(i);
		}
	}

	void flushCallerSavedRegisters(bool clearNeeded = false)
	{
        int i;
		for (i = 0; i < xmmTotal; ++i)
		{
			if (!oakIsCallerSavedXmm(i))
				continue;

			writeBackXmmRegId(i);
			if (clearNeeded || !xmmMap[i].isNeeded)
				clearReg(i);
		}

		for (i = 0; i < gprTotal; ++i)
		{
			if (!oakIsCallerSaved(i))
				continue;

			writeBackReg(i, true);
			if (clearNeeded || !gprMap[i].isNeeded)
				clearGPR(i);
		}
	}

	void flushPartialForCOP2()
	{
        int i;
		for (i = 0; i < xmmTotal; ++i)
		{
			microMapXMM& clear = xmmMap[i];

			// toss away anything which is not a full cached register
			if (pxmmregs[i].inuse && pxmmregs[i].type == XMMTYPE_VFREG)
			{
				// Should've been done in clearNeeded()
				if (clear.xyzw != 0 && clear.xyzw != 0xf)
                    writeBackXmmRegId(i, false);

				if (clear.VFreg <= 0)
				{
					// temps really shouldn't be here..
					_freeXMMreg(i);
				}
			}

			// needed gets cleared in iCore.
			clear = {-1, 0, 0, false, false};
		}

		for (i = 0; i < gprTotal; ++i)
		{
			microMapGPR& clear = gprMap[i];
			if (clear.VIreg < 0)
				clearGPR(i);
		}
	}

	void TDwritebackAll()
	{
		// NOTE: We don't clear state here, this happens in an optional branch

        int i;
		for (i = 0; i < xmmTotal; ++i)
		{
			microMapXMM& mapX = xmmMap[i];

			if ((mapX.VFreg > 0) && mapX.xyzw) // Reg was modified and not Temp or vf0
			{
				if (mapX.VFreg == 33) {
					recBeginOaknutEmit();
					oakAsm->UMOV(OAK_WSCRATCH, oakQRegister(i).Selem()[0]);
					oakStore32(OAK_WSCRATCH, mVUIrOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VI[REG_I]))));
					recEndOaknutEmit();
                }
				else if (mapX.VFreg == 32) {
//                    mVUsaveReg(xmm(i), ptr[&regs().ACC], mapX.xyzw, 1);
					mVUIrSaveReg_oaknut(i, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].ACC)), mapX.xyzw, true);
                }
				else {
//                    mVUsaveReg(xmm(i), ptr[&getVF(mapX.VFreg)], mapX.xyzw, 1);
					mVUIrSaveReg_oaknut(i, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VF[mapX.VFreg])), mapX.xyzw, true);
                }
			}
		}

		for (i = 0; i < gprTotal; ++i) {
            writeBackReg(i, false);
        }
	}

	bool checkVFClamp(int regId)
	{
		if (regId != VU_HOST_XMMPQ && ((xmmMap[regId].VFreg == 33 && !EmuConfig.Gamefixes.IbitHack) || xmmMap[regId].isZero))
			return false;
		else
			return true;
	}

	bool checkCachedReg(int regId)
	{
		if (regId < xmmTotal)
			return xmmMap[regId].VFreg >= 0;
		else
			return false;
	}

	bool checkCachedGPR(int regId)
	{
		if (regId < gprTotal)
			return gprMap[regId].VIreg >= 0 || gprMap[regId].isNeeded;
		else
			return false;
	}

	void clearReg(int regId)
	{
		microMapXMM& clear = xmmMap[regId];
		if (regAllocCOP2 && regId < static_cast<int>(iREGCNT_XMM) && (clear.isNeeded || clear.VFreg >= 0))
		{
			pxAssert(pxmmregs[regId].type == XMMTYPE_VFREG);
			pxmmregs[regId].inuse = false;
		}

		clear = {-1, 0, 0, false, false};
	}

	void clearRegVF(int VFreg)
	{
        int i;
		for (i = 0; i < xmmTotal; ++i)
		{
			if (xmmMap[i].VFreg == VFreg)
				clearReg(i);
		}
	}

	void clearRegVFDuplicates(int keepReg, int VFreg)
	{
        int i;
		for (i = 0; i < xmmTotal; ++i)
		{
			if (i != keepReg && !xmmMap[i].isNeeded && xmmMap[i].VFreg == VFreg)
				clearReg(i);
		}
	}

	void clearRegCOP2(int xmmReg)
	{
		if (regAllocCOP2)
			clearReg(xmmReg);
	}

	void updateCOP2AllocState(int rn)
	{
		if (!regAllocCOP2)
			return;

		const bool dirty = (xmmMap[rn].VFreg > 0 && xmmMap[rn].xyzw != 0);
		pxAssert(rn < static_cast<int>(iREGCNT_XMM));
		pxAssert(pxmmregs[rn].type == XMMTYPE_VFREG);
		pxmmregs[rn].reg = xmmMap[rn].VFreg;
		pxmmregs[rn].mode = dirty ? (MODE_READ | MODE_WRITE) : MODE_READ;
		pxmmregs[rn].needed = xmmMap[rn].isNeeded;
	}

	// Writes back modified reg to memory.
	// If all vectors modified, then keeps the VF reg cached in the xmm register.
	// If reg was not modified, then keeps the VF reg cached in the xmm register.
	void writeBackXmmRegId(int reg_code, bool invalidateRegs = true)
	{
		microMapXMM& mapX = xmmMap[reg_code];

		if ((mapX.VFreg > 0) && mapX.xyzw) // Reg was modified and not Temp or vf0
		{
			if (mapX.VFreg == 33) {
				recBeginOaknutEmit();
				oakAsm->UMOV(OAK_WSCRATCH, oakQRegister(reg_code).Selem()[0]);
				oakStore32(OAK_WSCRATCH, mVUIrOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VI[REG_I]))));
				recEndOaknutEmit();
            }
			else if (mapX.VFreg == 32) {
//                mVUsaveReg(reg, ptr[&regs().ACC], mapX.xyzw, true);
				mVUIrSaveReg_oaknut(reg_code, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].ACC)), mapX.xyzw, true);
            }
			else {
//                mVUsaveReg(reg, ptr[&getVF(mapX.VFreg)], mapX.xyzw, true);
				mVUIrSaveReg_oaknut(reg_code, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VF[mapX.VFreg])), mapX.xyzw, true);
            }

			if (invalidateRegs)
			{
                int i;
				for (i = 0; i < xmmTotal; ++i)
				{
					microMapXMM& mapI = xmmMap[i];

					if ((i == reg_code) || mapI.isNeeded)
						continue;

					if (mapI.VFreg == mapX.VFreg)
					{
						if (mapI.xyzw && mapI.xyzw < 0xf)
							DevCon.Error("microVU Error: writeBackReg() [%d]", mapI.VFreg);
						clearReg(i); // Invalidate any Cached Regs of same vf Reg
					}
				}
			}
			if (mapX.xyzw == 0xf) // Make Cached Reg if All Vectors were Modified
			{
				mapX.count    = counter;
				mapX.xyzw     = 0;
				mapX.isNeeded = false;
				updateCOP2AllocState(reg_code);
				return;
			}
			clearReg(reg_code);
		}
		else if (mapX.xyzw) // Clear reg if modified and is VF0 or temp reg...
		{
			clearReg(reg_code);
		}
	}

	// Use this when done using the allocated register, it clears its "Needed" status.
	// The register that was written to, should be cleared before other registers are cleared.
	// This is to guarantee proper merging between registers... When a written-to reg is cleared,
	// it invalidates other cached registers of the same VF reg, and merges partial-vector
	// writes into them.
	void clearNeededXmmId(int reg_code)
	{
		if ((reg_code < 0) || (reg_code >= xmmTotal) || (reg_code == VU_HOST_XMMPQ))
			return;

		microMapXMM& clear = xmmMap[reg_code];
		clear.isNeeded = false;
		if (clear.xyzw) // Reg was modified
		{
			if (clear.VFreg > 0)
			{
				int mergeRegs = 0;
				if (clear.xyzw < 0xf) // Try to merge partial writes
					mergeRegs = 1;

                int i;
				for (i = 0; i < xmmTotal; ++i) // Invalidate any other read-only regs of same vfReg
				{
					if (i == reg_code)
						continue;

					microMapXMM& mapI = xmmMap[i];
					if (mapI.VFreg == clear.VFreg)
					{
						if (mapI.xyzw && mapI.xyzw < 0xf)
						{
							DevCon.Error("microVU Error: clearNeeded() [%d]", mapI.VFreg);
						}
						if (mergeRegs == 1)
						{
							mVUIrMergeRegs_oaknut(i, reg_code, clear.xyzw, true);
							mapI.xyzw  = 0xf;
							mapI.count = counter;
							mergeRegs  = 2;
							updateCOP2AllocState(i);
						}
						else
							clearReg(i); // Clears when mergeRegs is 0 or 2
					}
				}
				if (mergeRegs == 2) // Clear Current Reg if Merged
					clearReg(reg_code);
				else if (mergeRegs == 1) // Write Back Partial Writes if couldn't merge
					writeBackXmmRegId(reg_code);
			}
			else
				clearReg(reg_code); // If Reg was temp or vf0, then invalidate itself
		}
		else if (regAllocCOP2 && clear.VFreg < 0)
		{
			// free on the EE side
			pxAssert(pxmmregs[reg_code].type == XMMTYPE_VFREG);
			pxmmregs[reg_code].inuse = false;
		}
	}

	// vfLoadReg  = VF reg to be loaded to the xmm register
	// vfWriteReg = VF reg that the returned xmm register will be considered as
	// xyzw       = XYZW vectors that will be modified (and loaded)
	// cloneWrite = When loading a reg that will be written to, it copies it to its own xmm reg instead of overwriting the cached one...
	// Notes:
	// To load a temp reg use the default param values, vfLoadReg = -1 and vfWriteReg = -1.
	// To load a full reg which won't be modified and you want cached, specify vfLoadReg >= 0 and vfWriteReg = -1
	// To load a reg which you don't want written back or cached, specify vfLoadReg >= 0 and vfWriteReg = 0
	int allocRegId(int vfLoadReg = -1, int vfWriteReg = -1, int xyzw = 0, bool cloneWrite = true)
	{
		counter++;
		if (vfLoadReg >= 0) // Search For Cached Regs
		{
            int i;
			for (i = 0; i < xmmTotal; ++i)
			{
				microMapXMM& mapI = xmmMap[i];
				if ((mapI.VFreg == vfLoadReg)
				 && (!mapI.xyzw                           // Reg Was Not Modified
				  || (mapI.VFreg && (mapI.xyzw == 0xf)))) // Reg Had All Vectors Modified and != VF0
				{
					int z = i;
					if (vfWriteReg >= 0) // Reg will be modified
					{
						if (cloneWrite) // Clone Reg so as not to use the same Cached Reg
						{
							z = findFreeReg(vfWriteReg);
							writeBackXmmRegId(z);

                            if (xyzw == 4) {
								recBeginOaknutEmit();
								mVUIrPshufd_oaknut(z, i, 1);
								recEndOaknutEmit();
                            }
                            else if (xyzw == 2) {
								recBeginOaknutEmit();
								mVUIrPshufd_oaknut(z, i, 2);
								recEndOaknutEmit();
                            }
                            else if (xyzw == 1) {
								recBeginOaknutEmit();
								mVUIrPshufd_oaknut(z, i, 3);
								recEndOaknutEmit();
                            }
                            else if (z != i) {
								recBeginOaknutEmit();
								oakAsm->MOV(oakQRegister(z).B16(), oakQRegister(i).B16());
								recEndOaknutEmit();
                            }

							mapI.count = counter; // Reg i was used, so update counter
						}
						else // Don't clone reg, but shuffle to adjust for SS ops
						{
							if ((vfLoadReg != vfWriteReg) || (xyzw != 0xf))
								writeBackXmmRegId(i);

                            if (xyzw == 4) {
								recBeginOaknutEmit();
								mVUIrPshufd_oaknut(i, i, 1);
								recEndOaknutEmit();
                            }
                            else if (xyzw == 2) {
								recBeginOaknutEmit();
								mVUIrPshufd_oaknut(i, i, 2);
								recEndOaknutEmit();
                            }
                            else if (xyzw == 1) {
								recBeginOaknutEmit();
								mVUIrPshufd_oaknut(i, i, 3);
								recEndOaknutEmit();
                            }
						}
						xmmMap[z].VFreg = vfWriteReg;
						xmmMap[z].xyzw = xyzw;
						xmmMap[z].isZero = (vfLoadReg == 0);
					}
					xmmMap[z].count = counter;
					xmmMap[z].isNeeded = true;
					updateCOP2AllocState(z);

                    return z;
				}
			}
		}

		int x = findFreeReg((vfWriteReg >= 0) ? vfWriteReg : vfLoadReg);
		writeBackXmmRegId(x);

		if (vfWriteReg >= 0) // Reg Will Be Modified (allow partial reg loading)
		{
			if ((vfLoadReg == 0) && !(xyzw & 1)) {
				recBeginOaknutEmit();
				oakAsm->EOR(oakQRegister(x).B16(), oakQRegister(x).B16(), oakQRegister(x).B16());
				recEndOaknutEmit();
            }
			else if (vfLoadReg == 33) {
                loadIreg(x, xyzw);
            }
			else if (vfLoadReg == 32) {
//                mVUloadReg(xmmX, ptr[&regs().ACC], xyzw);
				mVUIrLoadReg_oaknut(x, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].ACC)), xyzw);
            }
			else if (vfLoadReg >= 0) {
//                mVUloadReg(xmmX, ptr[&getVF(vfLoadReg)], xyzw);
				mVUIrLoadReg_oaknut(x, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VF[vfLoadReg])), xyzw);
            }

			xmmMap[x].VFreg = vfWriteReg;
			xmmMap[x].xyzw  = xyzw;
		}
		else // Reg Will Not Be Modified (always load full reg for caching)
		{
			if (vfLoadReg == 33) {
                loadIreg(x, 0xf);
            }
			else if (vfLoadReg == 32) {
				mVUIrLoadReg_oaknut(x, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].ACC)), 0xf);
            }
			else if (vfLoadReg >= 0) {
				mVUIrLoadReg_oaknut(x, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VF[vfLoadReg])), 0xf);
            }

			xmmMap[x].VFreg = vfLoadReg;
			xmmMap[x].xyzw  = 0;
		}
		xmmMap[x].isZero = (vfLoadReg == 0);
		xmmMap[x].count    = counter;
		xmmMap[x].isNeeded = true;
		updateCOP2AllocState(x);
		return x;
	}

	void clearGPR(int regId)
	{
		microMapGPR& clear = gprMap[regId];

		if (regAllocCOP2)
		{
			if (x86regs[regId].inuse && x86regs[regId].type == X86TYPE_VIREG)
			{
				pxAssert(x86regs[regId].reg == clear.VIreg);
				_freeX86regWithoutWriteback(regId);
			}
		}

		clear.VIreg = -1;
		clear.count = 0;
		clear.isNeeded = 0;
		clear.dirty = false;
		clear.isZeroExtended = false;
	}

	void clearGPRCOP2(int regId)
	{
		if (regAllocCOP2)
			clearGPR(regId);
	}

	void writeBackReg(int regId, bool clearDirty)
	{
		microMapGPR& mapX = gprMap[regId];
		pxAssert(mapX.usable || !mapX.dirty);
		if (mapX.dirty)
		{
			pxAssert(mapX.VIreg > 0);
			if (mapX.VIreg < 16) {
				recBeginOaknutEmit();
				oakStore16(oakWRegister(regId), {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VI[mapX.VIreg]))});
				recEndOaknutEmit();
            }
			if (clearDirty)
			{
				mapX.dirty = false;
				updateCOP2AllocState(regId);
			}
		}
	}

	void clearNeeded(int reg_code)
	{
		pxAssert(reg_code < gprTotal);
		microMapGPR& clear = gprMap[reg_code];
		clear.isNeeded = false;
		if (regAllocCOP2)
			x86regs[reg_code].needed = false;
	}

	void unbindAnyVIAllocations(int reg, bool& backup)
	{
        int i, j;
		for (i = 0; i < gprTotal; ++i)
		{
			microMapGPR& mapI = gprMap[i];
			if (mapI.VIreg == reg)
			{
				if (backup)
				{
					writeVIBackup(i);
					backup = false;
				}

				// if it's needed, we just unbind the allocation and preserve it, otherwise clear
				if (mapI.isNeeded)
				{
					if (regAllocCOP2)
					{
						pxAssert(x86regs[i].type == X86TYPE_VIREG && x86regs[i].reg == static_cast<u8>(mapI.VIreg));
						x86regs[i].reg = -1;
					}

					mapI.VIreg = -1;
					mapI.dirty = false;
					mapI.isZeroExtended = false;
				}
				else
				{
					clearGPR(i);
				}

				// shouldn't be any others...
				for (j = i + 1; j < gprTotal; ++j)
				{
					pxAssert(gprMap[j].VIreg != reg);
				}

				break;
			}
		}
	}

	int allocGPR(int viLoadReg = -1, int viWriteReg = -1, bool backup = false, bool zext_if_dirty = false)
	{
		// TODO: When load != write, we should check whether load is used later, and if so, copy it.

		const int this_counter = regAllocCOP2 ? (g_x86AllocCounter++) : (counter++);
		if (viLoadReg == 0 || viWriteReg == 0)
		{
			// write zero register as temp and discard later
			if (viWriteReg == 0)
			{
				int x = findFreeGPR(-1);
				writeBackReg(x, true);
				recBeginOaknutEmit();
				const oak::WReg gprX = oakWRegister(x);
				oakAsm->EOR(gprX, gprX, gprX);
				recEndOaknutEmit();
				gprMap[x].VIreg = -1;
				gprMap[x].dirty = false;
				gprMap[x].count = this_counter;
				gprMap[x].isNeeded = true;
				gprMap[x].isZeroExtended = true;
				return x;
			}
		}

		if (viLoadReg >= 0) // Search For Cached Regs
		{
            int i;
			for (i = 0; i < gprTotal; ++i)
			{
				microMapGPR& mapI = gprMap[i];
				if (mapI.VIreg == viLoadReg)
				{
					// Do this first, there is a case where when loadReg != writeReg, the findFreeGPR can steal the loadReg
					gprMap[i].count = this_counter;

					if (viWriteReg >= 0) // Reg will be modified
					{
						if (viLoadReg != viWriteReg)
						{
							// kill any allocations of viWriteReg
							unbindAnyVIAllocations(viWriteReg, backup);

							// allocate a new register for writing to
							int x = findFreeGPR(viWriteReg);

							writeBackReg(x, true);
							const oak::WReg gprX = oakWRegister(x);

							// writeReg not cached, needs backing up
							if (backup && gprMap[x].VIreg != viWriteReg)
							{
								recBeginOaknutEmit();
								oakLoad16(gprX, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VI[viWriteReg]))});
								recEndOaknutEmit();
								writeVIBackup(x);
								backup = false;
							}

							recBeginOaknutEmit();
							if (zext_if_dirty) {
								oakAsm->UXTH(gprX, oakWRegister(i));
							}
							else {
								oakAsm->MOV(gprX, oakWRegister(i));
							}
							recEndOaknutEmit();
							gprMap[x].isZeroExtended = zext_if_dirty;
							std::swap(x, i);
						}
						else
						{
							// writing to it, no longer zero extended
							gprMap[i].isZeroExtended = false;
						}

						gprMap[i].VIreg = viWriteReg;
						gprMap[i].dirty = true;
					}
					else if (zext_if_dirty && !gprMap[i].isZeroExtended)
					{
						const oak::WReg reg32 = oakWRegister(i);
						recBeginOaknutEmit();
						oakAsm->UXTH(reg32, reg32);
						recEndOaknutEmit();
						gprMap[i].isZeroExtended = true;
					}

					gprMap[i].isNeeded = true;

					if (backup)
						writeVIBackup(i);

					if (regAllocCOP2)
					{
						pxAssert(x86regs[i].inuse && x86regs[i].type == X86TYPE_VIREG);
						x86regs[i].reg = gprMap[i].VIreg;
						x86regs[i].mode = gprMap[i].dirty ? (MODE_WRITE | MODE_READ) : (MODE_READ);
					}

					return i;
				}
			}
		}

		if (viWriteReg >= 0) // Writing a new value, make sure this register isn't cached already
			unbindAnyVIAllocations(viWriteReg, backup);

		int x = findFreeGPR(viLoadReg);
		writeBackReg(x, true);
		const oak::WReg gprX = oakWRegister(x);

		// Special case: we need to back up the destination register, but it might not have already
		// been cached. If so, we need to load the old value from state and back it up. Otherwise,
		// it's going to get lost when we eventually write this register back.
		if (backup && viLoadReg >= 0 && viWriteReg > 0 && viLoadReg != viWriteReg)
		{
			recBeginOaknutEmit();
			oakLoad16(gprX, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VI[viWriteReg]))});
			recEndOaknutEmit();
			writeVIBackup(x);
			backup = false;
		}

		if (viLoadReg > 0) {
			recBeginOaknutEmit();
			oakLoad16(gprX, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VI[viLoadReg]))});
			recEndOaknutEmit();
		}
		else if (viLoadReg == 0) {
			recBeginOaknutEmit();
			oakAsm->EOR(gprX, gprX, gprX);
			recEndOaknutEmit();
		}

		gprMap[x].VIreg = viLoadReg;
		gprMap[x].isZeroExtended = true;
		if (viWriteReg >= 0)
		{
			gprMap[x].VIreg = viWriteReg;
			gprMap[x].dirty = true;
			gprMap[x].isZeroExtended = false;

			if (backup)
			{
                if (viLoadReg < 0 && viWriteReg > 0) {
					recBeginOaknutEmit();
					oakLoad16(gprX, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[index].VI[viWriteReg]))});
					recEndOaknutEmit();
                }

				writeVIBackup(x);
			}
		}

		gprMap[x].count = this_counter;
		gprMap[x].isNeeded = true;

		if (regAllocCOP2)
		{
			pxAssert(x86regs[x].inuse && x86regs[x].type == X86TYPE_VIREG);
			x86regs[x].reg = gprMap[x].VIreg;
			x86regs[x].mode = gprMap[x].dirty ? (MODE_WRITE | MODE_READ) : (MODE_READ);
		}

		return x;
	}

	int allocGPRId(int viLoadReg = -1, int viWriteReg = -1, bool backup = false, bool zext_if_dirty = false)
	{
		return allocGPR(viLoadReg, viWriteReg, backup, zext_if_dirty);
	}
};

