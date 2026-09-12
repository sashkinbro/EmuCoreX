// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once
#include <bitset>
#include <vector>

#include "arm64/OaknutHelpers-arm64.h"

//------------------------------------------------------------------
// Micro VU - Misc Functions
//------------------------------------------------------------------

struct mVURegSaveLayout {
    struct GprPair {
        int r1;
        int r2; // -1 if none (e.g. paired with XZR)
        int offset;
    };
    struct XmmPair {
        int r1;
        int r2; // -1 if none
        int offset;
    };

    std::vector<GprPair> gpr_saves;
    std::vector<XmmPair> xmm_saves;
    int stack_size = 0;
};

static inline mVURegSaveLayout mVUGetRegSaveLayout(microVU& mVU, bool onlyNeeded)
{
    mVURegSaveLayout layout;
    std::vector<int> gprs_to_save;
    for (int i = 0; i < static_cast<int>(iREGCNT_GPR); ++i)
    {
        if (!oakIsCallerSaved(i) || i == 4)
            continue;

        // STATUS instances are pinned outside the VI allocator. They remain
        // live across helper calls even when no guest VI register is cached
        // here, and all four use caller-saved AAPCS64 registers.
        const bool statusInstance = i == VU_HOST_F0 || i == VU_HOST_F1 ||
            i == VU_HOST_F2 || i == VU_HOST_F3;
        if (!onlyNeeded || statusInstance || mVU.regAlloc->checkCachedGPR(i)) {
            gprs_to_save.push_back(i);
        }
    }

    std::vector<int> xmmregsToSave;
    for (int i = 0; i < mVU.regAlloc->getXmmCount(); ++i)
    {
        if (!oakIsCallerSavedXmm(i))
            continue;

        if (!onlyNeeded || mVU.regAlloc->checkCachedReg(i) || VU_HOST_XMMPQ == i) {
            xmmregsToSave.push_back(i);
        }
    }

    int current_offset = 0;

    // Lay out GPRs (8 bytes each, stored in pairs)
    for (size_t idx = 0; idx < gprs_to_save.size(); idx += 2) {
        if (idx + 1 < gprs_to_save.size()) {
            layout.gpr_saves.push_back({gprs_to_save[idx], gprs_to_save[idx + 1], current_offset});
            current_offset += 16;
        } else {
            layout.gpr_saves.push_back({gprs_to_save[idx], -1, current_offset});
            current_offset += 16;
        }
    }

    // Lay out ARM64 vector regs (16 bytes each, stored in pairs)
    for (size_t idx = 0; idx < xmmregsToSave.size(); idx += 2) {
        if (idx + 1 < xmmregsToSave.size()) {
            layout.xmm_saves.push_back({xmmregsToSave[idx], xmmregsToSave[idx + 1], current_offset});
            current_offset += 32;
        } else {
            layout.xmm_saves.push_back({xmmregsToSave[idx], -1, current_offset});
            current_offset += 16;
        }
    }

    layout.stack_size = current_offset;
    return layout;
}

static inline mVURegSaveLayout mVUGetWaitMTVULayout(microVU& mVU)
{
    mVURegSaveLayout layout;
    std::vector<int> gprs_to_save;
    for (int i = 0; i < static_cast<int>(iREGCNT_GPR); ++i)
    {
        if (!oakIsCallerSaved(i) || i == 4)
            continue;
        if (i == VU_HOST_T2)
            continue;
        gprs_to_save.push_back(i);
    }
    // Also save X30 (LR)
    gprs_to_save.push_back(30);

    std::vector<int> xmmregsToSave;
    for (int i = 0; i < mVU.regAlloc->getXmmCount(); ++i)
    {
        if (!oakIsCallerSavedXmm(i))
            continue;
        xmmregsToSave.push_back(i);
    }

    int current_offset = 0;

    // Lay out GPRs (8 bytes each, stored in pairs)
    for (size_t idx = 0; idx < gprs_to_save.size(); idx += 2) {
        if (idx + 1 < gprs_to_save.size()) {
            layout.gpr_saves.push_back({gprs_to_save[idx], gprs_to_save[idx + 1], current_offset});
            current_offset += 16;
        } else {
            layout.gpr_saves.push_back({gprs_to_save[idx], -1, current_offset});
            current_offset += 16;
        }
    }

    // Lay out ARM64 vector regs (16 bytes each, stored in pairs)
    for (size_t idx = 0; idx < xmmregsToSave.size(); idx += 2) {
        if (idx + 1 < xmmregsToSave.size()) {
            layout.xmm_saves.push_back({xmmregsToSave[idx], xmmregsToSave[idx + 1], current_offset});
            current_offset += 32;
        } else {
            layout.xmm_saves.push_back({xmmregsToSave[idx], -1, current_offset});
            current_offset += 16;
        }
    }

    layout.stack_size = current_offset;
    return layout;
}

// Backup Volatile Regs (EAX, ECX, EDX, MM0~7, XMM0~7, are all volatile according to 32bit Win/Linux ABI)
__fi void mVUbackupRegs(microVU& mVU, bool toMemory = false, bool onlyNeeded = false)
{
    if (toMemory)
    {
        recBeginOaknutEmit();

        mVURegSaveLayout layout = mVUGetRegSaveLayout(mVU, onlyNeeded);
        if (layout.stack_size > 0)
        {
            oakAsm->SUB(oak::util::SP, oak::util::SP, layout.stack_size);

            for (const auto& save : layout.gpr_saves) {
                if (save.r2 != -1) {
                    oakAsm->STP(oakXRegister(save.r1), oakXRegister(save.r2), oak::util::SP, oak::SOffset<10, 3>(save.offset));
                } else {
                    oakAsm->STP(oakXRegister(save.r1), oak::util::XZR, oak::util::SP, oak::SOffset<10, 3>(save.offset));
                }
            }

            for (const auto& save : layout.xmm_saves) {
                if (save.r2 != -1) {
                    oakAsm->STP(oakQRegister(save.r1), oakQRegister(save.r2), oak::util::SP, oak::SOffset<11, 4>(save.offset));
                } else {
                    oakAsm->STR(oakQRegister(save.r1), oak::util::SP, oak::POffset<16, 4>(save.offset));
                }
            }
        }

        recEndOaknutEmit();
    }
    else
	{
		// TODO(Stenzek): get rid of xmmbackup
		mVU.regAlloc->flushAll(); // Flush Regalloc
		recBeginOaknutEmit();
		oakStore128(oakQRegister(VU_HOST_XMMPQ),
			{oak::util::X28, static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].xmmBackup[VU_HOST_XMMPQ][0]))});
        recEndOaknutEmit();
    }
}

// Restore Volatile Regs
__fi void mVUrestoreRegs(microVU& mVU, bool fromMemory = false, bool onlyNeeded = false)
{
    if (fromMemory)
    {
        recBeginOaknutEmit();

        mVURegSaveLayout layout = mVUGetRegSaveLayout(mVU, onlyNeeded);
        if (layout.stack_size > 0)
        {
            for (const auto& save : layout.xmm_saves) {
                if (save.r2 != -1) {
                    oakAsm->LDP(oakQRegister(save.r1), oakQRegister(save.r2), oak::util::SP, oak::SOffset<11, 4>(save.offset));
                } else {
                    oakAsm->LDR(oakQRegister(save.r1), oak::util::SP, oak::POffset<16, 4>(save.offset));
                }
            }

            for (const auto& save : layout.gpr_saves) {
                if (save.r2 != -1) {
                    oakAsm->LDP(oakXRegister(save.r1), oakXRegister(save.r2), oak::util::SP, oak::SOffset<10, 3>(save.offset));
                } else {
                    oakAsm->LDP(oakXRegister(save.r1), oak::util::XZR, oak::util::SP, oak::SOffset<10, 3>(save.offset));
                }
            }

            oakAsm->ADD(oak::util::SP, oak::util::SP, layout.stack_size);
        }

        recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		oakLoad128(oakQRegister(VU_HOST_XMMPQ),
			{oak::util::X28, static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].xmmBackup[VU_HOST_XMMPQ][0]))});
        recEndOaknutEmit();
    }
}

static __fi OakMemOperand mVUClampOakGlobMem(s64 offset)
{
	return mVUIrOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, mVUglob)) + offset);
}

static __fi void mVUEmitVectorConstant32_oaknut(const oak::QReg& dst, u32 value)
{
	oakAsm->MOV(OAK_WSCRATCH, value);
	oakAsm->DUP(dst.S4(), OAK_WSCRATCH);
}

static __fi void mVUEmitExponentVector_oaknut(const oak::QReg& dst)
{
	mVUEmitVectorConstant32_oaknut(dst, 0x7f800000u);
}

static __fi void mVUEmitSignbitVector_oaknut(const oak::QReg& dst)
{
	mVUEmitVectorConstant32_oaknut(dst, 0x80000000u);
}

static __fi void mVUEmitMaxvalsVector_oaknut(const oak::QReg& dst)
{
	mVUEmitVectorConstant32_oaknut(dst, 0x7f7fffffu);
}

static __fi void mVUEmitAbsclipVector_oaknut(const oak::QReg& dst)
{
	mVUEmitVectorConstant32_oaknut(dst, 0x7fffffffu);
}

static __fi void mVUEmitI32MaxFVector_oaknut(const oak::QReg& dst)
{
	mVUEmitVectorConstant32_oaknut(dst, 0x4effffffu);
}

static __fi void mVULoadClampLimits_oaknut(const oak::QReg& min_dst, const oak::QReg& max_dst)
{
	static_assert(offsetof(mVU_Globals, maxvals) == offsetof(mVU_Globals, minvals) + 16);
	oakAsm->LDP(min_dst, max_dst, OAK_XVUCLAMP,
		oak::SOffset<11, 4>(static_cast<s64>(offsetof(mVU_Globals, minvals))));
}

static __fi void mVUClampVuDoubleVectorBits_oaknut(int reg, int exponent_reg, int mask_reg,
	bool clamp_overflow, bool maxvals_ready = false)
{
	// Adjacent exact FMAC clamps can retain maxvals in Q30 instead of rebuilding it.
	// The canonical AArch64 batch test matched 39,145,728 lanes bit-for-bit; 19/21
	// pinned-core benchmark runs were faster, normally by 2-9%.
	const oak::QReg reg_q = oakQRegister(reg);
	const oak::QReg exponent_q = oakQRegister(exponent_reg);
	const oak::QReg mask_q = oakQRegister(mask_reg);

	// Keep the numeric exponent for both the denormal and overflow tests.
	// A left shift discards the sign before the exponent is moved to bits 0..7.
	oakAsm->SHL(exponent_q.S4(), reg_q.S4(), 1);
	oakAsm->USHR(exponent_q.S4(), exponent_q.S4(), 24);

	// Convert exponent==0 to a magnitude mask and leave only the original sign.
	oakAsm->CMEQ(mask_q.S4(), exponent_q.S4(), 0);
	oakAsm->USHR(mask_q.S4(), mask_q.S4(), 1);
	oakAsm->BIC(reg_q.B16(), reg_q.B16(), mask_q.B16());

	if (clamp_overflow)
	{
		// exponent==255 is equivalent to the low byte of ~exponent being zero.
		oakAsm->MVN(exponent_q.B16(), exponent_q.B16());
		oakAsm->SHL(exponent_q.S4(), exponent_q.S4(), 24);
		oakAsm->CMEQ(exponent_q.S4(), exponent_q.S4(), 0);

		// Form signed max without materializing a separate sign-bit vector.
		oakAsm->USHR(mask_q.S4(), reg_q.S4(), 31);
		oakAsm->SHL(mask_q.S4(), mask_q.S4(), 31);
		if (!maxvals_ready)
			mVUEmitMaxvalsVector_oaknut(OAK_QSCRATCH2);
		oakAsm->ORR(mask_q.B16(), mask_q.B16(), OAK_QSCRATCH2.B16());
		oakAsm->BIT(reg_q.B16(), mask_q.B16(), exponent_q.B16());
	}
}

static __fi void mVUClampDenormalScalarBits_oaknut(int reg)
{
	oak::Label done;

	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(reg));
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH, 0x7f800000u);
	oakAsm->CBNZ(OAK_WSCRATCH2, done);
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x80000000u);
	oakAsm->INS(oakQRegister(reg).Selem()[0], OAK_WSCRATCH);
	oakAsm->l(done);
}

static __fi void mVUClampDenormalVectorBits_oaknut(int reg)
{
	const oak::QReg reg_q = oakQRegister(reg);

	// Build a per-lane 0x7fffffff mask only where the exponent is zero,
	// then clear the magnitude while retaining the sign bit. This avoids
	// materializing both exponent and sign vectors in every generated block.
	oakAsm->SHL(OAK_QSCRATCH2.S4(), reg_q.S4(), 1);
	oakAsm->USHR(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), 24);
	oakAsm->CMEQ(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), 0);
	oakAsm->SHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), 1);
	oakAsm->USHR(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), 1);
	oakAsm->BIC(reg_q.B16(), reg_q.B16(), OAK_QSCRATCH2.B16());
}


static __fi void mVUClamp1ScalarBits_oaknut(int reg)
{
	oakAsm->MOV(OAK_QSCRATCH2.B16(), oakQRegister(reg).B16());
	mVULoadClampLimits_oaknut(OAK_QSCRATCH3, OAK_QSCRATCH);
	oakAsm->SMIN(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), OAK_QSCRATCH.S4());
	oakAsm->UMIN(OAK_QSCRATCH2.S4(), OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4());
	oakAsm->MOV(oakQRegister(reg).Selem()[0], OAK_QSCRATCH2.Selem()[0]);
}

static __fi void mVUClamp1ScalarFast_oaknut(int reg)
{
	mVULoadClampLimits_oaknut(OAK_QSCRATCH3, OAK_QSCRATCH);
	oakAsm->FMINNM(OAK_SSCRATCH2, oakSRegister(reg), OAK_SSCRATCH);
	oakAsm->FMAXNM(OAK_SSCRATCH2, OAK_SSCRATCH2, OAK_SSCRATCH3);
	oakAsm->MOV(oakQRegister(reg).Selem()[0], OAK_QSCRATCH2.Selem()[0]);
}

static __fi void mVUClamp1VectorFast_oaknut(int reg, bool limits_ready = false)
{
	const oak::QReg reg_q = oakQRegister(reg);

	if (!limits_ready)
		mVULoadClampLimits_oaknut(OAK_QSCRATCH3, OAK_QSCRATCH);
	oakAsm->FMINNM(reg_q.S4(), reg_q.S4(), OAK_QSCRATCH.S4());
	oakAsm->FMAXNM(reg_q.S4(), reg_q.S4(), OAK_QSCRATCH3.S4());
}

static __fi void mVUClamp1VectorBits_oaknut(int reg, bool limits_ready = false)
{
	const oak::QReg reg_q = oakQRegister(reg);

	if (!limits_ready)
		mVULoadClampLimits_oaknut(OAK_QSCRATCH3, OAK_QSCRATCH);
	oakAsm->SMIN(reg_q.S4(), reg_q.S4(), OAK_QSCRATCH.S4());
	oakAsm->UMIN(reg_q.S4(), reg_q.S4(), OAK_QSCRATCH3.S4());
}

static void mVUTBit()
{
	u32 old = vu1Thread.mtvuInterrupts.fetch_or(VU_Thread::InterruptFlagVUTBit, std::memory_order_release);
	if (old & VU_Thread::InterruptFlagVUTBit)
		DevCon.Warning("Old TBit not registered");
}

static void mVUEBit()
{
	vu1Thread.mtvuInterrupts.fetch_or(VU_Thread::InterruptFlagVUEBit, std::memory_order_release);
}
static inline u32 branchAddr(const mV)
{
	pxAssumeMsg(islowerOP, "MicroVU: Expected Lower OP code for valid branch addr.");
    // return ((((iPC + 2) + (_Imm11_ * 2)) & mVU.progMemMask) * 4)
	return ((((iPC + 2) + (_Imm11_ << 1)) & mVU.progMemMask) << 2);
}

static void mVUwaitMTVU()
{
	vu1Thread.WaitVU();
}

