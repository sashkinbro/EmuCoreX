// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once


//------------------------------------------------------------------
// mVUupdateFlags() - Updates status/mac flags
//------------------------------------------------------------------

#define AND_XYZW ((_XYZW_SS && modXYZW) ? (1) : (mFLAG.doFlag ? (_X_Y_Z_W) : (flipMask[_X_Y_Z_W])))
#define ADD_XYZW ((_XYZW_SS && modXYZW) ? (_X ? 3 : (_Y ? 2 : (_Z ? 1 : 0))) : 0)


//alignas(16) const u32 sse4_compvals[2][4] = {
//	{0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0x7f7fffff}, //1111
//	{0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff}, //1111
//};

const std::array<u16, 16> flipMask{0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};

static __fi OakMemOperand mVUUpperOakGlobMem(s64 offset)
{
	return mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, mVUglob)) + offset);
}

static __fi OakMemOperand mVUUpperOakSs4Mem(s64 offset)
{
	return mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, mVUss4)) + offset);
}

static __fi void mVUUpperPshufd_oaknut(int dst, int src, u8 imm)
{
	const oak::QReg dst_q = oakQRegister(dst);
	const oak::QReg src_q = oakQRegister(src);

	if (imm == 0x00) { oakAsm->DUP(dst_q.S4(), src_q.Selem()[0]); return; }
	if (imm == 0x55) { oakAsm->DUP(dst_q.S4(), src_q.Selem()[1]); return; }
	if (imm == 0xaa) { oakAsm->DUP(dst_q.S4(), src_q.Selem()[2]); return; }
	if (imm == 0xff) { oakAsm->DUP(dst_q.S4(), src_q.Selem()[3]); return; }
	if (imm == 0x1b)
	{
		oakAsm->REV64(OAK_QSCRATCH3.S4(), src_q.S4());
		oakAsm->EXT(dst_q.B16(), OAK_QSCRATCH3.B16(), OAK_QSCRATCH3.B16(), 8);
		return;
	}

	oakLoad128(OAK_QSCRATCH3,
		mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, shuffle.data[imm][0]))));
	oakAsm->MOV(OAK_QSCRATCH2.B16(), src_q.B16());
	oakAsm->TBL(dst_q.B16(), oak::List(OAK_QSCRATCH2.B16()), OAK_QSCRATCH3.B16());
}

enum clampModes
{
	cFt = 0x01,
	cFs = 0x02,
	cACC = 0x04,
};

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
static thread_local u64 s_vuLocalNaturalEmits = 0;
static thread_local bool s_vuLocalNaturalEnabled = true;
extern "C" void EmuCoreXOracleSetVULocalNatural(bool enabled) { s_vuLocalNaturalEnabled = enabled; }
static thread_local bool s_vuLocalReadonlyClipEnabled = true;
static thread_local u64 s_vuLocalReadonlyClipEmits = 0;
extern "C" void EmuCoreXOracleSetVULocalReadonlyClip(bool enabled) { s_vuLocalReadonlyClipEnabled = enabled; }
extern "C" u64 EmuCoreXOracleVULocalReadonlyClipEmits() { return s_vuLocalReadonlyClipEmits; }
static thread_local bool s_vuLocalIdentityEnabled = true;
static thread_local u64 s_vuLocalIdentityEmits = 0;
extern "C" void EmuCoreXOracleSetVULocalIdentity(bool enabled) { s_vuLocalIdentityEnabled = enabled; }
extern "C" u64 EmuCoreXOracleVULocalIdentityEmits() { return s_vuLocalIdentityEmits; }
static thread_local bool s_vuLocalZeroTailClampEnabled = true;
static thread_local u64 s_vuLocalZeroTailClampEmits = 0;
extern "C" void EmuCoreXOracleSetVUZeroTailClamp(bool enabled) { s_vuLocalZeroTailClampEnabled = enabled; }
extern "C" u64 EmuCoreXOracleVUZeroTailClampEmits() { return s_vuLocalZeroTailClampEmits; }
#endif

static __fi bool mVUUpperNatural_oaknut(mV)
{
	return mVU.regAlloc->getNaturalLane() >= 0;
}

class mVUUpperNaturalScope
{
	microRegAlloc& alloc;
	int previous;
public:
	explicit mVUUpperNaturalScope(mV, bool allowed = true) : alloc(*mVU.regAlloc)
	{
		bool enabled = mVUlocalNaturalEnabled() && allowed && isVU1 && !isCOP2 && _XYZW_SS2 && !_X && !clampE;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
		enabled &= s_vuLocalNaturalEnabled;
		if (enabled) ++s_vuLocalNaturalEmits;
#endif
		previous = alloc.setNaturalLane(enabled ? (_Y ? 1 : _Z ? 2 : 3) : -1);
	}
	~mVUUpperNaturalScope() { alloc.setNaturalLane(previous); }
};

// Called only after the body's Fs clamp. Numeric clamp under FZ leaves
// normal numbers and signed zero, so multiplying by canonical VF0.w is exact.
// Keep the VU1 A-form vuDouble path: its bit clamp leaves denormals unflushed.
static __fi bool mVUUpperSkipCanonicalLaneMul_oaknut(mV, int Fs, int lane, bool aform)
{
	if (!mVUlocalRuleEnabled(16) || _Ft_ != 0 || lane != 3 || clampE ||
		!CHECK_VU_OVERFLOW(mVU.index) || CHECK_VU_SIGN_OVERFLOW(mVU.index) ||
		(aform && isVU1) || (_XYZW_SS && !mVUUpperNatural_oaknut(mVU)) ||
		!mVU.regAlloc->checkVFClamp(Fs))
		return false;

	// COP2 macros execute under the EE policy, which may differ from VU0's.
	const FPControlRegister fpcr = isCOP2 ? EmuConfig.Cpu.FPUFPCR :
		(isVU1 ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR);
	constexpr u64 allowed = FPControlRegister::FZ_BIT | FPControlRegister::RMODE_BITS | (1ULL << 25);
	if (!fpcr.GetFlushToZero() || (fpcr.bitmask & ~allowed))
		return false;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	if (!s_vuLocalIdentityEnabled) return false;
	++s_vuLocalIdentityEmits;
#endif
	return true;
}

// Scalar FADD/FSUB/FMUL clear the result's upper 96 bits under this FPCR
// policy. Clamping those three zeros adds no exception or numerical change,
// so a vector clamp avoids the scalar path's final lane insertion.
static __fi void mVUUpperClampZeroTailNumeric_oaknut(int reg)
{
	mVUClamp1VectorFast_oaknut(reg);
}

static __fi void mVUUpperClampZeroTailResult_oaknut(mV, int reg)
{
	const FPControlRegister fpcr = isCOP2 ? EmuConfig.Cpu.FPUFPCR :
		(isVU1 ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR);
	constexpr u64 allowed = FPControlRegister::FZ_BIT | FPControlRegister::RMODE_BITS | (1ULL << 25);
	bool eligible = mVUlocalRuleEnabled(11) && clampE && !CHECK_VU_SIGN_OVERFLOW(mVU.index) &&
		mVU.regAlloc->checkVFClamp(reg) && !(fpcr.bitmask & ~allowed);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	eligible &= s_vuLocalZeroTailClampEnabled;
	if (eligible) ++s_vuLocalZeroTailClampEmits;
#endif
	if (eligible) mVUUpperClampZeroTailNumeric_oaknut(reg);
	else mVUUpperClamp4Scalar_oaknut(mVU, reg);
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
extern "C" u32 EmuCoreXOracleEmitZeroTailClamp(bool optimized)
{
	const u8* start = oakGetCurrentCodePointer();
	recBeginOaknutEmit();
	if (optimized) mVUUpperClampZeroTailNumeric_oaknut(0);
	else mVUClamp1ScalarFast_oaknut(0);
	recEndOaknutEmit();
	return static_cast<u32>(oakGetCurrentCodePointer() - start);
}
#endif

// Within a natural-lane body each private operand is consumed full width.
static __fi void mVUUpperClampOperandScalar_oaknut(mV, int reg, bool extra)
{
	if (mVUUpperNatural_oaknut(mVU)) mVUUpperClamp2Vector_oaknut(mVU, reg, extra);
	else mVUUpperClamp2Scalar_oaknut(mVU, reg, extra);
}

static __fi void mVUUpperAddPs_oaknut(mV, int to, int from);
static __fi void mVUUpperSubPs_oaknut(mV, int to, int from);

static __fi bool mVUUpperWillClampFt_oaknut(mV, int clampType)
{
	return clampE || ((clampType & cFt) && !clampE && (CHECK_VU_OVERFLOW(mVU.index) || CHECK_VU_SIGN_OVERFLOW(mVU.index)));
}

static __fi void mVUUpperMovmskps_oaknut(const oak::WReg& dst, const oak::QReg& src, bool mask_ready = false)
{
	// The only caller passes a 0/all-ones lane mask (a CMEQ/CMHS result), so the
	// per-lane AND with mac_mask already yields {0,1,2,4,8} lanes and the sign
	// extension pass the old sequence needed is redundant.
	if (!mask_ready)
		oakLoad128(OAK_QSCRATCH3, mVUUpperOakSs4Mem(offsetof(mVU_SSE4, mac_mask)));
	oakAsm->AND(OAK_QSCRATCH2.B16(), src.B16(), OAK_QSCRATCH3.B16());
	oakAsm->ADDV(OAK_SSCRATCH, OAK_QSCRATCH2.S4());
	oakAsm->FMOV(dst, OAK_SSCRATCH);
}

static __fi void mVUUpperMaskActiveLanes_oaknut(const oak::WReg& reg, u32 active_mask)
{
	// Contiguous low-four-bit masks are AArch64 logical immediates. Using them
	// directly removes MOV+AND pairs: 160 million canonical operations matched,
	// and all 7 long benchmark runs improved by 22-35% for the mask operation.
	switch (active_mask)
	{
		case 0:
			oakAsm->MOV(reg, oak::util::WZR);
			return;
		case 1:
		case 2:
		case 3:
		case 4:
		case 6:
		case 7:
		case 8:
		case 12:
		case 14:
		case 15:
			oakAsm->AND(reg, reg, active_mask);
			return;
		default:
			oakAsm->MOV(OAK_WSCRATCH, active_mask);
			oakAsm->AND(reg, reg, OAK_WSCRATCH);
			return;
	}
}

// Only a completely overwritten arithmetic destination can consume a clone.
static __fi int mVUUpperStepSource_oaknut(mV, int dst)
{
	const int source = mVU.regAlloc->takeCloneSource(dst);
	return source >= 0 ? source : dst;
}

static __fi void mVUUpperAddSs_oaknut(mV, int to, int from)
{
	if (mVUUpperNatural_oaknut(mVU))
	{
		mVUUpperAddPs_oaknut(mVU, to, from);
		return;
	}
	mVUUpperClamp3Scalar_oaknut(mVU, to);
	mVUUpperClamp3Scalar_oaknut(mVU, from);
	const int source = mVUUpperStepSource_oaknut(mVU, to);
	oakAsm->FADD(oakSRegister(to), oakSRegister(source), oakSRegister(from == to ? source : from));
	mVUUpperClampZeroTailResult_oaknut(mVU, to);
}

static __fi bool mVUUpperPrepareVectorClampLimits_oaknut(mV, int first, int second, int third = -1)
{
	const bool limits_ready = clampE && (mVU.regAlloc->checkVFClamp(first) ||
		mVU.regAlloc->checkVFClamp(second) ||
		(third >= 0 && mVU.regAlloc->checkVFClamp(third)));
	if (limits_ready)
	{
		// Sharing one LDP across adjacent vector clamps matched 242,331,648
		// lanes. Bitwise improved in 18/21 and numeric in 21/21 long runs,
		// normally by 3-15%, with no runtime condition added to generated code.
		mVULoadClampLimits_oaknut(OAK_QSCRATCH3, OAK_QSCRATCH);
	}
	return limits_ready;
}

static __fi void mVUUpperAddPs_oaknut(mV, int to, int from)
{
	const oak::QReg to_q = oakQRegister(to);
	const bool limits_ready = mVUUpperPrepareVectorClampLimits_oaknut(mVU, to, from);
	mVUUpperClamp3Vector_oaknut(mVU, to, limits_ready);
	mVUUpperClamp3Vector_oaknut(mVU, from, limits_ready);
	const int source = mVUUpperStepSource_oaknut(mVU, to);
	oakAsm->FADD(to_q.S4(), oakQRegister(source).S4(), oakQRegister(from == to ? source : from).S4());
	mVUUpperClamp4Vector_oaknut(mVU, to, limits_ready);
}

static __fi void mVUUpperSubSs_oaknut(mV, int to, int from)
{
	if (mVUUpperNatural_oaknut(mVU))
	{
		mVUUpperSubPs_oaknut(mVU, to, from);
		return;
	}
	mVUUpperClamp3Scalar_oaknut(mVU, to);
	mVUUpperClamp3Scalar_oaknut(mVU, from);
	const int source = mVUUpperStepSource_oaknut(mVU, to);
	oakAsm->FSUB(oakSRegister(to), oakSRegister(source), oakSRegister(from == to ? source : from));
	mVUUpperClampZeroTailResult_oaknut(mVU, to);
}

static __fi void mVUUpperMulPs_oaknut(mV, int to, int from)
{
	const oak::QReg to_q = oakQRegister(to);
	const bool limits_ready = mVUUpperPrepareVectorClampLimits_oaknut(mVU, to, from);
	mVUUpperClamp3Vector_oaknut(mVU, to, limits_ready);
	mVUUpperClamp3Vector_oaknut(mVU, from, limits_ready);
	const int source = mVUUpperStepSource_oaknut(mVU, to);
	oakAsm->FMUL(to_q.S4(), oakQRegister(source).S4(), oakQRegister(from == to ? source : from).S4());
	mVUUpperClamp4Vector_oaknut(mVU, to, limits_ready);
}

static __fi void mVUUpperSubPs_oaknut(mV, int to, int from)
{
	const oak::QReg to_q = oakQRegister(to);
	const bool limits_ready = mVUUpperPrepareVectorClampLimits_oaknut(mVU, to, from);
	mVUUpperClamp3Vector_oaknut(mVU, to, limits_ready);
	mVUUpperClamp3Vector_oaknut(mVU, from, limits_ready);
	const int source = mVUUpperStepSource_oaknut(mVU, to);
	oakAsm->FSUB(to_q.S4(), oakQRegister(source).S4(), oakQRegister(from == to ? source : from).S4());
	mVUUpperClamp4Vector_oaknut(mVU, to, limits_ready);
}

static __fi void mVUUpperMulSs_oaknut(mV, int to, int from)
{
	if (mVUUpperNatural_oaknut(mVU))
	{
		mVUUpperMulPs_oaknut(mVU, to, from);
		return;
	}
	mVUUpperClamp3Scalar_oaknut(mVU, to);
	mVUUpperClamp3Scalar_oaknut(mVU, from);
	const int source = mVUUpperStepSource_oaknut(mVU, to);
	oakAsm->FMUL(oakSRegister(to), oakSRegister(source), oakSRegister(from == to ? source : from));
	mVUUpperClampZeroTailResult_oaknut(mVU, to);
}

static __fi void mVUUpperFmlaPs_oaknut(mV, int acc, int left, int right)
{
	const oak::QReg acc_q = oakQRegister(acc);
	const oak::QReg left_q = oakQRegister(left);
	const oak::QReg right_q = oakQRegister(right);
	const bool limits_ready = mVUUpperPrepareVectorClampLimits_oaknut(mVU, acc, left, right);
	mVUUpperClamp3Vector_oaknut(mVU, acc, limits_ready);
	mVUUpperClamp3Vector_oaknut(mVU, left, limits_ready);
	mVUUpperClamp3Vector_oaknut(mVU, right, limits_ready);
	oakAsm->FMUL(OAK_QSCRATCH.S4(), left_q.S4(), right_q.S4());
	oakAsm->FADD(acc_q.S4(), acc_q.S4(), OAK_QSCRATCH.S4());
	mVUUpperClamp4Vector_oaknut(mVU, acc);
}

static __fi void mVUUpperFmlaSs_oaknut(mV, int acc, int left, int right)
{
	if (mVUUpperNatural_oaknut(mVU))
	{
		mVUUpperFmlaPs_oaknut(mVU, acc, left, right);
		return;
	}
	mVUUpperClamp3Scalar_oaknut(mVU, acc);
	mVUUpperClamp3Scalar_oaknut(mVU, left);
	mVUUpperClamp3Scalar_oaknut(mVU, right);
	oakAsm->FMUL(OAK_SSCRATCH, oakSRegister(left), oakQRegister(right).Selem()[0]);
	oakAsm->FADD(OAK_SSCRATCH, oakSRegister(acc), OAK_SSCRATCH);
	oakAsm->MOV(oakQRegister(acc).Selem()[0], OAK_QSCRATCH.Selem()[0]);
	mVUUpperClamp4Scalar_oaknut(mVU, acc);
}

static __fi void mVUUpperFmlaPsLane_oaknut(int acc, int left, int right, int lane)
{
	oakAsm->FMUL(OAK_QSCRATCH.S4(), oakQRegister(left).S4(), oakQRegister(right).Selem()[lane]);
	oakAsm->FADD(oakQRegister(acc).S4(), oakQRegister(acc).S4(), OAK_QSCRATCH.S4());
}

static __fi void mVUUpperFmlaSsLane_oaknut(mV, int acc, int left, int right, int lane)
{
	if (mVUUpperNatural_oaknut(mVU))
	{
		mVUUpperFmlaPsLane_oaknut(acc, left, right, lane);
		return;
	}
	oakAsm->FMUL(OAK_SSCRATCH, oakSRegister(left), oakQRegister(right).Selem()[lane]);
	oakAsm->FADD(OAK_SSCRATCH, oakSRegister(acc), OAK_SSCRATCH);
	oakAsm->MOV(oakQRegister(acc).Selem()[0], OAK_QSCRATCH.Selem()[0]);
}

static __fi void mVUUpperPrepareVuDoubleBounds_oaknut(mV)
{
	// Keep both bounds in reserved scratch across adjacent clamps. Load them
	// after any mVUUpperPshufd_oaknut(), which uses the same Q29 scratch.
	if (CHECK_VU_OVERFLOW(mVU.index))
		mVULoadClampLimits_oaknut(OAK_QSCRATCH3, OAK_QSCRATCH2);
}

static __fi bool mVUUpperUseHardwareInputFlush_oaknut(u32 index)
{
	if (index != 1)
		return false;

	// VU1's dispatcher installs this FPCR before entering compiled code. These
	// private operands feed FP arithmetic immediately, so FZ performs the same
	// signed input flush without changing cached source VFs or inactive lanes.
	// Preserve explicit normalization for trapping or alternative FPCR modes.
	constexpr u64 allowed = FPControlRegister::FZ_BIT | FPControlRegister::RMODE_BITS | (1ULL << 25);
	const FPControlRegister fpcr = EmuConfig.Cpu.VU1FPCR;
	return fpcr.GetDenormalsAreZero() && (fpcr.bitmask & ~allowed) == 0;
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
extern "C" u32 EmuCoreXOracleEmitUpperInputFlush(u32 index, bool overflow, bool reference, u32 lane)
{
	const u8* start = oakGetCurrentCodePointer();
	recBeginOaknutEmit();
	if (lane < 4)
		mVUClampVuDoubleBroadcastBits_oaknut(0, 0, lane, overflow,
			reference || !mVUUpperUseHardwareInputFlush_oaknut(index));
	else
		mVUClampVuDoubleVectorBits_oaknut(0, 2, overflow, false,
			reference || !mVUUpperUseHardwareInputFlush_oaknut(index));
	oakAsm->FADD(oak::util::Q0.S4(), oak::util::Q0.S4(), oak::util::Q1.S4());
	recEndOaknutEmit();
	return static_cast<u32>(oakGetCurrentCodePointer() - start);
}
#endif

static __fi void mVUUpperVuDoubleVector_oaknut(mV, int reg, int t1, bool bounds_ready = false)
{
	mVUClampVuDoubleVectorBits_oaknut(reg, t1, CHECK_VU_OVERFLOW(mVU.index), bounds_ready,
		!mVUUpperUseHardwareInputFlush_oaknut(mVU.index));
}

static __fi void mVUUpperMulPsLane_oaknut(int to, int from, int lane)
{
	oakAsm->FMUL(oakQRegister(to).S4(), oakQRegister(to).S4(), oakQRegister(from).Selem()[lane]);
}

static __fi void mVUUpperMulSsLane_oaknut(mV, int to, int from, int lane)
{
	if (mVUUpperNatural_oaknut(mVU))
	{
		mVUUpperMulPsLane_oaknut(to, from, lane);
		return;
	}
	oakAsm->FMUL(oakSRegister(to), oakSRegister(to), oakQRegister(from).Selem()[lane]);
}

static __fi void mVU_FMACbAddPs_oaknut(mV, int acc, int fs, int ft)
{
	mVUUpperMulPs_oaknut(mVU, fs, ft);
	mVUUpperAddPs_oaknut(mVU, acc, fs);
}

static __fi void mVU_FMACbAddSs_oaknut(mV, int acc, int fs, int ft)
{
	mVUUpperMulSs_oaknut(mVU, fs, ft);
	mVUUpperAddSs_oaknut(mVU, acc, fs);
}

static __fi void mVU_FMACbSubPs_oaknut(mV, int acc, int fs, int ft)
{
	mVUUpperMulPs_oaknut(mVU, fs, ft);
	mVUUpperSubPs_oaknut(mVU, acc, fs);
}

static __fi void mVU_FMACbSubSs_oaknut(mV, int acc, int fs, int ft)
{
	mVUUpperMulSs_oaknut(mVU, fs, ft);
	mVUUpperSubSs_oaknut(mVU, acc, fs);
}

static __fi void mVUUpperFmlsPs_oaknut(mV, int acc, int left, int right)
{
	const oak::QReg acc_q = oakQRegister(acc);
	const oak::QReg left_q = oakQRegister(left);
	const oak::QReg right_q = oakQRegister(right);
	const bool limits_ready = mVUUpperPrepareVectorClampLimits_oaknut(mVU, acc, left, right);
	mVUUpperClamp3Vector_oaknut(mVU, acc, limits_ready);
	mVUUpperClamp3Vector_oaknut(mVU, left, limits_ready);
	mVUUpperClamp3Vector_oaknut(mVU, right, limits_ready);
	oakAsm->FMUL(OAK_QSCRATCH.S4(), left_q.S4(), right_q.S4());
	oakAsm->FSUB(acc_q.S4(), acc_q.S4(), OAK_QSCRATCH.S4());
	mVUUpperClamp4Vector_oaknut(mVU, acc);
}

static __fi void mVUUpperFmlsSs_oaknut(mV, int acc, int left, int right)
{
	if (mVUUpperNatural_oaknut(mVU))
	{
		mVUUpperFmlsPs_oaknut(mVU, acc, left, right);
		return;
	}
	mVUUpperClamp3Scalar_oaknut(mVU, acc);
	mVUUpperClamp3Scalar_oaknut(mVU, left);
	mVUUpperClamp3Scalar_oaknut(mVU, right);
	oakAsm->FMUL(OAK_SSCRATCH, oakSRegister(left), oakQRegister(right).Selem()[0]);
	oakAsm->FSUB(OAK_SSCRATCH, oakSRegister(acc), OAK_SSCRATCH);
	oakAsm->MOV(oakQRegister(acc).Selem()[0], OAK_QSCRATCH.Selem()[0]);
	mVUUpperClamp4Scalar_oaknut(mVU, acc);
}

static __fi void mVUUpperMergeRegs_oaknut(int dest, int src, int xyzw, bool modXYZW = false)
{
	xyzw &= 0xf;
	if (dest == src || xyzw == 0)
		return;

	const oak::QReg dst_q = oakQRegister(dest);
	const oak::QReg src_q = oakQRegister(src);

	if (xyzw == 0xf)
	{
		oakAsm->MOV(dst_q.B16(), src_q.B16());
		return;
	}

	if (modXYZW)
	{
		if (xyzw == 1)
		{
			oakAsm->MOV(dst_q.Selem()[3], src_q.Selem()[0]);
			return;
		}
		if (xyzw == 2)
		{
			oakAsm->MOV(dst_q.Selem()[2], src_q.Selem()[0]);
			return;
		}
		if (xyzw == 4)
		{
			oakAsm->MOV(dst_q.Selem()[1], src_q.Selem()[0]);
			return;
		}
	}

	xyzw = ((xyzw & 1) << 3) | ((xyzw & 2) << 1) | ((xyzw & 4) >> 1) | ((xyzw & 8) >> 3);
	for (u32 i = 0; i < 4; ++i)
	{
		if (xyzw & (1u << i))
			oakAsm->MOV(dst_q.Selem()[i], src_q.Selem()[i]);
	}
}

static __fi void mVUUpperDupLane_oaknut(int dest, int src, int lane)
{
	oakAsm->DUP(oakQRegister(dest).S4(), oakQRegister(src).Selem()[lane]);
}

static __fi void mVUUpperShufPsImm0_oaknut(int dest, int src)
{
	const oak::QReg dest_q = oakQRegister(dest);
	const oak::QReg src_q = oakQRegister(src);
	oakAsm->MOV(OAK_QSCRATCH2.B16(), src_q.B16());
	oakAsm->MOV(dest_q.Selem()[1], dest_q.Selem()[0]);
	oakAsm->MOV(dest_q.Selem()[2], OAK_QSCRATCH2.Selem()[0]);
	oakAsm->MOV(dest_q.Selem()[3], OAK_QSCRATCH2.Selem()[0]);
}

static __fi void mVUUpperMaxMinSelect_oaknut(const oak::QReg& dest, const oak::QReg& src,
	const oak::QReg& mask, const oak::QReg& negative, bool maximum, bool one_nonnegative = false)
{
	// VF0 and its broadcasts contain only +0/+1 raw words. With either
	// operand nonnegative, the both-negative reversal is impossible.
	if (mVUlocalRuleEnabled(3) && one_nonnegative)
	{
		if (maximum) oakAsm->SMAX(dest.S4(), dest.S4(), src.S4());
		else oakAsm->SMIN(dest.S4(), dest.S4(), src.S4());
		return;
	}
	// Signed raw-bit comparison has the required order except when both values
	// are negative. Invert that predicate in those lanes, then select the
	// original bits. Equal negative inputs may choose either identical operand.
	// Integer-only selection preserves signed zeros, denormals and NaN payloads.
	oakAsm->AND(negative.B16(), dest.B16(), src.B16());
	oakAsm->SSHR(negative.S4(), negative.S4(), 31);
	if (maximum)
		oakAsm->CMGT(mask.S4(), dest.S4(), src.S4());
	else
		oakAsm->CMGT(mask.S4(), src.S4(), dest.S4());
	oakAsm->EOR(mask.B16(), mask.B16(), negative.B16());
	oakAsm->BIF(dest.B16(), src.B16(), mask.B16());
}

static __fi void mVUUpperMaxScalar_oaknut(int dest, int src, bool one_nonnegative = false)
{
	mVUUpperMaxMinSelect_oaknut(oakQRegister(dest), oakQRegister(src), OAK_QSCRATCH, OAK_QSCRATCH2, true, one_nonnegative);
}

static __fi void mVUUpperMaxVector_oaknut(int dest, int src, bool one_nonnegative = false)
{
	mVUUpperMaxMinSelect_oaknut(oakQRegister(dest), oakQRegister(src), OAK_QSCRATCH, OAK_QSCRATCH2, true, one_nonnegative);
}

static __fi void mVUUpperMiniScalar_oaknut(int dest, int src, bool one_nonnegative = false)
{
	mVUUpperMaxMinSelect_oaknut(oakQRegister(dest), oakQRegister(src), OAK_QSCRATCH, OAK_QSCRATCH2, false, one_nonnegative);
}

static __fi void mVUUpperMiniVector_oaknut(int dest, int src, bool one_nonnegative = false)
{
	mVUUpperMaxMinSelect_oaknut(oakQRegister(dest), oakQRegister(src), OAK_QSCRATCH, OAK_QSCRATCH2, false, one_nonnegative);
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
extern "C" u32 EmuCoreXOracleEmitMaxMinSelect(bool maximum, bool scalar, bool alias)
{
	const u8* start = oakGetCurrentCodePointer();
	recBeginOaknutEmit();
	const int src = alias ? 0 : 1;
	if (maximum)
	{
		if (scalar) mVUUpperMaxScalar_oaknut(0, src);
		else mVUUpperMaxVector_oaknut(0, src);
	}
	else
	{
		if (scalar) mVUUpperMiniScalar_oaknut(0, src);
		else mVUUpperMiniVector_oaknut(0, src);
	}
	recEndOaknutEmit();
	return static_cast<u32>(oakGetCurrentCodePointer() - start);
}
#endif

static __fi void mVUUpperGetQreg_oaknut(int dest, int qInstance)
{
	mVUUpperDupLane_oaknut(dest, VU_HOST_XMMPQ, qInstance);
}

static constexpr int VU_HOST_NO_XMM = -1;

// Weight vector for one flag pack, off the pinned mVUglob base (x27):
//   reverse   - MAC path: lane i weighs bit (3 - i), PS2 MAC order
//   keepMask  - destination field mask (x86's AND_XYZW)
//   shift     - single-scalar rotate (x86's SHIFT_XYZW), folded into the weight
static __fi OakMemOperand mVUmacWeightVecMem_oaknut(u32 keepMask, bool reverse, int shift)
{
	const s64 base = static_cast<s64>(offsetof(cpuRegistersPack, mVUglob)) +
		static_cast<s64>(offsetof(mVU_Globals, macWeights));
	if (shift != 0)
		return mVUAllocOakCpuMem(base + static_cast<s64>(offsetof(mVU_MacWeights, bySSShift[shift][0])));
	return mVUAllocOakCpuMem(base + static_cast<s64>(offsetof(mVU_MacWeights, byMask[reverse ? 1 : 0][keepMask & 0xF][0])));
}

static __fi int mVUUpperFlagSource_oaknut(mV, int reg)
{
	// All three reserved SIMD scratches belong to the flag reduction. Keep
	// its source in the allocator slot through the later overflow test.
	(void)mVU;
	return reg;
}

static void mVUupdateFlags_oaknut(mV, int reg, int regT1in = VU_HOST_NO_XMM, int regT2in = VU_HOST_NO_XMM, bool modXYZW = true)
{
	const int mReg = VU_HOST_T1;
	const int sReg = getFlagRegId(sFLAG.write);
	(void)regT1in;
	(void)regT2in;

	if (!sFLAG.doFlag && !mFLAG.doFlag)
		return;

	if (sFLAG.doFlag)
	{
		if (sFLAG.doNonSticky)
			mVUclearNonStickyStatus_oaknut(oakWRegister(sReg), oakWRegister(getFlagRegId(sFLAG.lastWrite)));
		else
			mVUallocSFLAGa(sReg, sFLAG.lastWrite);
	}

	const bool natural = mVUUpperNatural_oaknut(mVU) && _XYZW_SS;
	const int source_lane = natural ? mVU.regAlloc->getNaturalLane() : 0;
	const oak::QReg src_q = oakQRegister(mVUUpperFlagSource_oaknut(mVU, reg));
	const oak::QReg vSign = OAK_QSCRATCH3;
	const oak::QReg vZero = OAK_QSCRATCH2;
	const oak::WReg mac_w = oakWRegister(mReg);
	const oak::WReg status_w = oakWRegister(sReg);
	const oak::WReg temp_w = oakWRegister(VU_HOST_T2);

	// One SLI + AND(weights) + ADDV packs the whole 8-bit MAC word. The MAC path
	// reverses lane order through the weight vector instead of a PSHUF; the
	// single-scalar rotate and the destination field mask ride there too.
	const bool macPath = mFLAG.doFlag && !(_XYZW_SS && modXYZW);
	const bool doOverflowHack = sFLAG.doFlag && CHECK_VUOVERFLOWHACK;
	const int foldShift = (mFLAG.doFlag && !doOverflowHack) ? ADD_XYZW : 0;
	const u32 keepMask = macPath ? static_cast<u32>(_X_Y_Z_W)
		: ((_XYZW_SS && modXYZW) ? 1u : static_cast<u32>(flipMask[_X_Y_Z_W]));
	// Overflow predicates are computed on the unreversed result, so the movemask
	// lane mask is the forward one.
	const u32 overflowMask = (_XYZW_SS && modXYZW) ? (1u << source_lane) : static_cast<u32>(flipMask[_X_Y_Z_W]);

	recBeginOaknutEmit();
	oakAsm->CMLT(vSign.S4(), src_q.S4(), 0);
	oakAsm->FCMEQ(vZero.S4(), src_q.S4(), 0.0);
	oakAsm->SLI(vZero.S4(), vSign.S4(), 4);
	oakLoad128(vSign, mVUmacWeightVecMem_oaknut(keepMask, macPath, foldShift));
	if (source_lane != 0)
	{
		// The scalar weight is in lane zero. Move the weight to the result's
		// natural lane; do not move the result into a scratch that ADDV clobbers.
		oakAsm->EXT(vSign.B16(), vSign.B16(), vSign.B16(), (4 - source_lane) * 4);
	}
	oakAsm->AND(vZero.B16(), vZero.B16(), vSign.B16());
	oakAsm->ADDV(OAK_SSCRATCH, vZero.S4());
	oakAsm->FMOV(mac_w, OAK_SSCRATCH);

	// Overflow flags are generated for unclamped results as well, matching
	// VU_MAC_UPDATE; the overflow hack keeps its own path.
	const bool want_overflow_flags = CHECK_VUOVERFLOWHACK || !CHECK_VU_OVERFLOW(mVU.index);
	if ((sFLAG.doFlag || mFLAG.doFlag) && want_overflow_flags)
	{
		oak::Label no_overflow;
		if (CHECK_VUOVERFLOWHACK)
		{
			oakLoad128(OAK_QSCRATCH3, mVUUpperOakSs4Mem(offsetof(mVU_SSE4, sse4_compvals[0][0])));
			oakAsm->FACGE(vZero.S4(), src_q.S4(), OAK_QSCRATCH3.S4());
		}
		else
		{
			// Match VU_MAC_UPDATE for unclamped results, including NaNs.
			// (v << 1) >= 0xFF000000 unsigned is exactly exp == 255, and unlike
			// FP comparisons the integer test cannot raise FPCR exception flags.
			oakAsm->SHL(vZero.S4(), src_q.S4(), 1);
			oakAsm->MOVI(OAK_QSCRATCH3.S4(), 0xff, oak::LslSymbol::LSL, 24);
			oakAsm->CMHS(vZero.S4(), vZero.S4(), OAK_QSCRATCH3.S4());
		}
		mVUUpperMovmskps_oaknut(temp_w, vZero);
		mVUUpperMaskActiveLanes_oaknut(temp_w, overflowMask);
		oakAsm->CBZ(temp_w, no_overflow);
		if (sFLAG.doFlag)
		{
			if (sFLAG.doNonSticky)
			{
				oakAsm->MOV(OAK_WSCRATCH, 0x820000);
				oakAsm->ORR(status_w, status_w, OAK_WSCRATCH);
			}
			else
			{
				// Single-bit logical immediate: no scratch materialization.
				oakAsm->ORR(status_w, status_w, 0x800000);
			}
		}
		if (mFLAG.doFlag)
		{
			// Fold the lane shift into the ORR instead of shifting in place.
			oakAsm->ORR(mac_w, mac_w, temp_w, oak::util::LSL, static_cast<unsigned>(12 + foldShift - source_lane));
		}
		oakAsm->l(no_overflow);
	}

	// The gamefix packs at the unshifted position and rotates the whole word
	// afterwards, exactly like the x86 path.
	if (doOverflowHack && _XYZW_SS && modXYZW && !_W)
		oakAsm->LSL(mac_w, mac_w, ADD_XYZW);
	recEndOaknutEmit();

	if (mFLAG.doFlag)
		mVUallocMFLAGb(mVU, mReg, mFLAG.write);

	if (sFLAG.doFlag)
	{
		recBeginOaknutEmit();
		oakAsm->AND(mac_w, mac_w, 0xff);
		oakAsm->ORR(status_w, status_w, mac_w);
		if (sFLAG.doNonSticky)
			oakAsm->ORR(status_w, status_w, mac_w, oak::util::LSL, 8);
		recEndOaknutEmit();
	}
}

static bool mVUtryEmitNoLaneFmacFlags_oaknut(mV)
{
	if (_X_Y_Z_W != 0)
		return false;

	if (!sFLAG.doFlag && !mFLAG.doFlag)
		return true;

	if (sFLAG.doFlag)
	{
		const int sReg = getFlagRegId(sFLAG.write);
		mVUallocSFLAGa(sReg, sFLAG.lastWrite);
		if (sFLAG.doNonSticky)
		{
			recBeginOaknutEmit();
			oakAsm->MOV(OAK_WSCRATCH, 0xfffc00ff);
			oakAsm->AND(oakWRegister(sReg), oakWRegister(sReg), OAK_WSCRATCH);
			recEndOaknutEmit();
		}
	}

	if (mFLAG.doFlag)
	{
		const int mReg = VU_HOST_T1;
		recBeginOaknutEmit();
		oakAsm->EOR(oakWRegister(mReg), oakWRegister(mReg), oakWRegister(mReg));
		recEndOaknutEmit();
		mVUallocMFLAGb(mVU, mReg, mFLAG.write);
	}
	return true;
}

static bool mVUtrySkipNoLaneWrite_oaknut(mV)
{
	return _X_Y_Z_W == 0;
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
extern "C" u64 EmuCoreXOracleVULocalNaturalEmits()
{
	return s_vuLocalNaturalEmits;
}
#endif

//------------------------------------------------------------------
// Helper Macros and Functions
//------------------------------------------------------------------

// ADD Opcode
static void mVU_ADD_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	int Ft;
	int tempFt;
	const bool needsFullFtForClamp = clampE;

	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (needsFullFtForClamp)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);
	// Normal clamping must sanitize both ADD operands like vuDouble.
	// Use a private Ft copy because the source VF remains architecturally unchanged.
	const bool needsOperandClamp = (!clampE && CHECK_VU_OVERFLOW(mVU.index)) ||
		(isVU0 && isCOP2 && _X_Y_Z_W == 0xe);
	int FtDouble = Ft;
	int t1 = VU_HOST_NO_XMM;
	if (needsOperandClamp)
	{
		FtDouble = mVU.regAlloc->allocRegId();
		if (!mVUUpperUseHardwareInputFlush_oaknut(mVU.index))
			t1 = mVU.regAlloc->allocRegId();
	}

	recBeginOaknutEmit();
	if (needsOperandClamp)
	{
		oakAsm->MOV(oakQRegister(FtDouble).B16(), oakQRegister(Ft).B16());
		mVUUpperPrepareVuDoubleBounds_oaknut(mVU);
		mVUUpperVuDoubleVector_oaknut(mVU, Fs, t1, true);
		mVUUpperVuDoubleVector_oaknut(mVU, FtDouble, t1, true);
	}
	if (_XYZW_SS)
		mVUUpperAddSs_oaknut(mVU, Fs, FtDouble);
	else
		mVUUpperAddPs_oaknut(mVU, Fs, FtDouble);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, tempFt);

	mVU.regAlloc->clearNeededXmmId(Fs);
	if (needsOperandClamp)
	{
		mVU.regAlloc->clearNeededXmmId(t1);
		mVU.regAlloc->clearNeededXmmId(FtDouble);
	}
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_ADD_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, _Ft_); }
	pass2
	{
		mVU_ADD_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("ADD");
		mVUlogFd();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// ADDA Opcode
static void mVU_ADDA_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	int Ft;
	int tempFt;
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (clampE)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperAddSs_oaknut(mVU, Fs, Ft);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, tempFt);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		// ADDA never reads ACC. Commit the scalar result directly instead of
		// copying ACC out and back around the operation. The canonical AArch64
		// test covered all lanes in 8 FPCR modes (8,131,072 cases) bit-exactly.
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperAddSs_oaknut(mVU, Fs, Ft);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperAddPs_oaknut(mVU, Fs, Ft);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFt);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_ADDA_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_ADDA_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("ADDA");
		mVUlogACC();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// ADDAi Opcode
static void mVU_ADDAi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperAddSs_oaknut(mVU, Fs, Fi);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, Fi);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperAddSs_oaknut(mVU, Fs, Fi);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperAddPs_oaknut(mVU, Fs, Fi);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, Fi);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_ADDAi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_ADDAi_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("ADDAi");
		mVUlogACC();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// ADDAq Opcode
static void mVU_ADDAq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperAddSs_oaknut(mVU, Fs, Fq);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, tempFq);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperAddSs_oaknut(mVU, Fs, Fq);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperAddPs_oaknut(mVU, Fs, Fq);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFq);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fq);
}

static void mVU_ADDAq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_ADDAq_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("ADDAq");
		mVUlogACC();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_ADDA_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperAddSs_oaknut(mVU, Fs, FtL);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, FtL);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperAddSs_oaknut(mVU, Fs, FtL);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperAddPs_oaknut(mVU, Fs, FtL);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, FtL);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_ADDAx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_ADDA_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("ADDA"); mVUlogACC(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_ADDAy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_ADDA_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("ADDA"); mVUlogACC(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_ADDAz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_ADDA_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("ADDA"); mVUlogACC(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_ADDAw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_ADDA_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("ADDA"); mVUlogACC(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MADDAi Opcode
static void mVU_MADDAi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, 32, 0xf, false);

	if (_XYZW_SS || _X_Y_Z_W == 0xf)
	{
		if (_XYZW_SS)
		{
			const int tempACC = mVU.regAlloc->allocRegId();
			recBeginOaknutEmit();
			const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
			else
				oakAsm->MOV(oakQRegister(tempACC).Selem()[0], oakQRegister(ACC).Selem()[laneIdx]);
			mVU_FMACbAddSs_oaknut(mVU, tempACC, Fs, Fi);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, tempACC, Fs, Fi);
			recBeginOaknutEmit();
			// Keep the scalar accumulator in the work register and commit one lane.
			// This removes two full-vector moves while preserving ACC's other lanes.
			oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(tempACC).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
			recEndOaknutEmit();
			mVU.regAlloc->clearNeededXmmId(tempACC);
		}
		else
		{
			recBeginOaknutEmit();
			mVU_FMACbAddPs_oaknut(mVU, ACC, Fs, Fi);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, ACC, Fs, Fi);
		}
	}
	else
	{
		const int tempACC = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
		mVU_FMACbAddPs_oaknut(mVU, tempACC, Fs, Fi);
		mVUUpperMergeRegs_oaknut(ACC, tempACC, _X_Y_Z_W);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, Fi);
		mVU.regAlloc->clearNeededXmmId(tempACC);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_MADDAi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2
	{
		mVU_MADDAi_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("MADDAi");
		mVUlogACC();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MADDA Opcode
static void mVU_MADDA_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	int Ft;
	int tempFt;
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (clampE)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, 32, 0xf, false);

	if (_XYZW_SS || _X_Y_Z_W == 0xf)
	{
		if (_XYZW_SS2)
		{
			const int tempACC = mVU.regAlloc->allocRegId();
			recBeginOaknutEmit();
			const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
			else
				oakAsm->MOV(oakQRegister(tempACC).Selem()[0], oakQRegister(ACC).Selem()[laneIdx]);
			mVU_FMACbAddSs_oaknut(mVU, tempACC, Fs, Ft);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, tempACC, Fs, tempFt);
			recBeginOaknutEmit();
			oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(tempACC).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
			recEndOaknutEmit();
			mVU.regAlloc->clearNeededXmmId(tempACC);
		}
		else
		{
			recBeginOaknutEmit();
			if (_XYZW_SS)
				mVU_FMACbAddSs_oaknut(mVU, ACC, Fs, Ft);
			else
				mVU_FMACbAddPs_oaknut(mVU, ACC, Fs, Ft);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFt);
		}
	}
	else
	{
		const int tempACC = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
		mVU_FMACbAddPs_oaknut(mVU, tempACC, Fs, Ft);
		mVUUpperMergeRegs_oaknut(ACC, tempACC, _X_Y_Z_W);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFt);
		mVU.regAlloc->clearNeededXmmId(tempACC);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_MADDA_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MADDA_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MADDA");
		mVUlogACC();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MADDAq Opcode
static void mVU_MADDAq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, 32, 0xf, false);

	if (_XYZW_SS || _X_Y_Z_W == 0xf)
	{
		if (_XYZW_SS2)
		{
			const int tempACC = mVU.regAlloc->allocRegId();
			recBeginOaknutEmit();
			const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
			else
				oakAsm->MOV(oakQRegister(tempACC).Selem()[0], oakQRegister(ACC).Selem()[laneIdx]);
			mVU_FMACbAddSs_oaknut(mVU, tempACC, Fs, Fq);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, tempACC, Fs, tempFq);
			recBeginOaknutEmit();
			oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(tempACC).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
			recEndOaknutEmit();
			mVU.regAlloc->clearNeededXmmId(tempACC);
		}
		else
		{
			recBeginOaknutEmit();
			if (_XYZW_SS)
				mVU_FMACbAddSs_oaknut(mVU, ACC, Fs, Fq);
			else
				mVU_FMACbAddPs_oaknut(mVU, ACC, Fs, Fq);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFq);
		}
	}
	else
	{
		const int tempACC = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
		mVU_FMACbAddPs_oaknut(mVU, tempACC, Fs, Fq);
		mVUUpperMergeRegs_oaknut(ACC, tempACC, _X_Y_Z_W);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFq);
		mVU.regAlloc->clearNeededXmmId(tempACC);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fq);
}

static void mVU_MADDAq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_MADDAq_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MADDAq");
		mVUlogACC();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MADDA lane Opcodes
static void mVU_MADDA_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane, bool updateFlags = true)
{
	mVUUpperNaturalScope natural_scope(mVU);
	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, 32, 0xf, false);

	if (_XYZW_SS || _X_Y_Z_W == 0xf)
	{
		if (_XYZW_SS)
		{
			const int tempACC = mVU.regAlloc->allocRegId();
			recBeginOaknutEmit();
			const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
			else
				oakAsm->MOV(oakQRegister(tempACC).Selem()[0], oakQRegister(ACC).Selem()[laneIdx]);
			mVUUpperClampAccLaneFs_oaknut(mVU, Fs, !mVUUpperNatural_oaknut(mVU));
			mVU_FMACbAddSs_oaknut(mVU, tempACC, Fs, FtL);
			recEndOaknutEmit();
			if (updateFlags)
				mVUupdateFlags_oaknut(mVU, tempACC, Fs, FtL);
			recBeginOaknutEmit();
			oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(tempACC).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
			recEndOaknutEmit();
			mVU.regAlloc->clearNeededXmmId(tempACC);
		}
		else
		{
			recBeginOaknutEmit();
			mVUUpperClampAccLaneFs_oaknut(mVU, Fs, false);
			mVU_FMACbAddPs_oaknut(mVU, ACC, Fs, FtL);
			recEndOaknutEmit();
			if (updateFlags)
				mVUupdateFlags_oaknut(mVU, ACC, Fs, FtL);
		}
	}
	else
	{
		const int tempACC = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
		mVUUpperClampAccLaneFs_oaknut(mVU, Fs, false);
		mVU_FMACbAddPs_oaknut(mVU, tempACC, Fs, FtL);
		mVUUpperMergeRegs_oaknut(ACC, tempACC, _X_Y_Z_W);
		recEndOaknutEmit();
		if (updateFlags)
			mVUupdateFlags_oaknut(mVU, ACC, Fs, FtL);
		mVU.regAlloc->clearNeededXmmId(tempACC);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_MADDAx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MADDA_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("MADDA"); mVUlogACC(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MADDAy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MADDA_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("MADDA"); mVUlogACC(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MADDAz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MADDA_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("MADDA"); mVUlogACC(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MADDAw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MADDA_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("MADDA"); mVUlogACC(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MADD Opcode
static void mVU_MADD_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	int Ft;
	int tempFt;
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (clampE)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int ACC = mVU.regAlloc->allocRegId(32);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);
	const int tempFs = mVU.regAlloc->allocRegId();

	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(tempFs).B16(), oakQRegister(Fs).B16());
	oakAsm->MOV(oakQRegister(Fs).B16(), oakQRegister(ACC).B16());
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
		mVUUpperPshufd_oaknut(Fs, Fs, shuffleSS(_X_Y_Z_W));
	if (_XYZW_SS)
		mVUUpperFmlaSs_oaknut(mVU, Fs, tempFs, Ft);
	else
		mVUUpperFmlaPs_oaknut(mVU, Fs, tempFs, Ft);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(tempFs);

	mVUupdateFlags_oaknut(mVU, Fs, tempFt);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
	mVU.regAlloc->clearNeededXmmId(ACC);
}

static void mVU_MADD_cop2_emit_oaknut(mP)
{
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	int FtRaw;
	if (_XYZW_SS2)
		FtRaw = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
	else
		FtRaw = mVU.regAlloc->allocRegId(_Ft_);

	const int Ft = mVU.regAlloc->allocRegId();
	const int ACC = mVU.regAlloc->allocRegId(32);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);
	const int tempFs = mVU.regAlloc->allocRegId();
	const int t1 = mVU.regAlloc->allocRegId();

	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(Ft).B16(), oakQRegister(FtRaw).B16());
	oakAsm->MOV(oakQRegister(tempFs).B16(), oakQRegister(Fs).B16());
	mVUUpperPrepareVuDoubleBounds_oaknut(mVU);
	mVUUpperVuDoubleVector_oaknut(mVU, Ft, t1, true);
	mVUUpperVuDoubleVector_oaknut(mVU, tempFs, t1, true);
	oakAsm->MOV(oakQRegister(Fs).B16(), oakQRegister(ACC).B16());
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
		mVUUpperPshufd_oaknut(Fs, Fs, shuffleSS(_X_Y_Z_W));
	// The SS2 shuffle above may clobber Q29 (negative bound); without it the values
	// loaded by PrepareVuDoubleBounds are still live and must not be rebuilt.
	mVUUpperVuDoubleVector_oaknut(mVU, Fs, t1, !_XYZW_SS2);
	if (_XYZW_SS)
	{
		oakAsm->FMUL(OAK_SSCRATCH, oakSRegister(tempFs), oakQRegister(Ft).Selem()[0]);
		oakAsm->FADD(oakSRegister(Fs), oakSRegister(Fs), OAK_SSCRATCH);
	}
	else
	{
		oakAsm->FMUL(OAK_QSCRATCH.S4(), oakQRegister(tempFs).S4(), oakQRegister(Ft).S4());
		oakAsm->FADD(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), OAK_QSCRATCH.S4());
	}
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(tempFs);
	mVUupdateFlags_oaknut(mVU, Fs, Ft);

	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
	mVU.regAlloc->clearNeededXmmId(FtRaw);
	mVU.regAlloc->clearNeededXmmId(ACC);
}

static void mVU_MADD_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { if (isVU0 && isCOP2) mVU_MADD_cop2_emit_oaknut(mVU, recPass); else mVU_MADD_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MADD");
		mVUlogFd();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MADDi Opcode
static void mVU_MADDi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const bool needsVu0Exact = isVU0 && !isCOP2 && (_X_Y_Z_W == 0xf);
	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);
	const int tempFs = mVU.regAlloc->allocRegId();
	const int t1 = needsVu0Exact ? mVU.regAlloc->allocRegId() : VU_HOST_NO_XMM;

	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(tempFs).B16(), oakQRegister(Fs).B16());
	oakAsm->MOV(oakQRegister(Fs).B16(), oakQRegister(ACC).B16());
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
		mVUUpperPshufd_oaknut(Fs, Fs, shuffleSS(_X_Y_Z_W));
	if (needsVu0Exact)
	{
		mVUUpperPrepareVuDoubleBounds_oaknut(mVU);
		mVUUpperVuDoubleVector_oaknut(mVU, Fs, t1, true);
		mVUUpperVuDoubleVector_oaknut(mVU, tempFs, t1, true);
		mVUUpperVuDoubleVector_oaknut(mVU, Fi, t1, true);
	}
	if (_XYZW_SS)
		mVUUpperFmlaSs_oaknut(mVU, Fs, tempFs, Fi);
	else
		mVUUpperFmlaPs_oaknut(mVU, Fs, tempFs, Fi);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(tempFs);

	mVUupdateFlags_oaknut(mVU, Fs, Fi);

	if (needsVu0Exact)
	{
		mVU.regAlloc->clearNeededXmmId(t1);
	}
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
	mVU.regAlloc->clearNeededXmmId(ACC);
}

static void mVU_MADDi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2
	{
		mVU_MADDi_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("MADDi");
		mVUlogFd();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MADDq Opcode
static void mVU_MADDq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int ACC = mVU.regAlloc->allocRegId(32);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);
	const int tempFs = mVU.regAlloc->allocRegId();

	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(tempFs).B16(), oakQRegister(Fs).B16());
	oakAsm->MOV(oakQRegister(Fs).B16(), oakQRegister(ACC).B16());
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
		mVUUpperPshufd_oaknut(Fs, Fs, shuffleSS(_X_Y_Z_W));
	if (_XYZW_SS)
		mVUUpperFmlaSs_oaknut(mVU, Fs, tempFs, Fq);
	else
		mVUUpperFmlaPs_oaknut(mVU, Fs, tempFs, Fq);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(tempFs);

	mVUupdateFlags_oaknut(mVU, Fs, tempFq);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fq);
	mVU.regAlloc->clearNeededXmmId(ACC);
}

static void mVU_MADDq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2 { mVU_MADDq_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MADDq");
		mVUlogFd();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MADD lane Opcodes
static void mVU_MADD_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	mVUUpperNaturalScope natural_scope(mVU);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const bool needsVu1MaddLaneVuDouble = isVU1 && (lane == 0 || lane == 3);
	const bool useFtLane = !clampE && !needsVu1MaddLaneVuDouble;
	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	int FtL = VU_HOST_NO_XMM;
	if (!useFtLane)
	{
		FtL = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		if (needsVu1MaddLaneVuDouble)
			mVUClampVuDoubleBroadcastBits_oaknut(FtL, FtRaw, lane, CHECK_VU_OVERFLOW(mVU.index),
				!mVUUpperUseHardwareInputFlush_oaknut(mVU.index));
		else
			mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
		recEndOaknutEmit();
		mVU.regAlloc->clearNeededXmmId(FtRaw);
	}

	const int ACC = mVU.regAlloc->allocRegId(32);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);
	const int tempFs = mVU.regAlloc->allocRegId();
	const int t1 = (needsVu1MaddLaneVuDouble && !mVUUpperUseHardwareInputFlush_oaknut(mVU.index)) ?
		mVU.regAlloc->allocRegId() : VU_HOST_NO_XMM;

	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(tempFs).B16(), oakQRegister(Fs).B16());
	oakAsm->MOV(oakQRegister(Fs).B16(), oakQRegister(ACC).B16());
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
		mVUUpperPshufd_oaknut(Fs, Fs, shuffleSS(_X_Y_Z_W));
	if (needsVu1MaddLaneVuDouble)
	{
		mVUUpperPrepareVuDoubleBounds_oaknut(mVU);
		mVUUpperVuDoubleVector_oaknut(mVU, Fs, t1, true);
		mVUUpperVuDoubleVector_oaknut(mVU, tempFs, t1, true);
	}
	if (_XYZW_SS)
	{
		if (useFtLane)
			mVUUpperFmlaSsLane_oaknut(mVU, Fs, tempFs, FtRaw, lane);
		else
			mVUUpperFmlaSs_oaknut(mVU, Fs, tempFs, FtL);
	}
	else
	{
		if (useFtLane)
			mVUUpperFmlaPsLane_oaknut(Fs, tempFs, FtRaw, lane);
		else
			mVUUpperFmlaPs_oaknut(mVU, Fs, tempFs, FtL);
	}
	if (needsVu1MaddLaneVuDouble)
	{
		mVUUpperClampVu1MaddLaneResult_oaknut(Fs, _XYZW_SS && !mVUUpperNatural_oaknut(mVU));
	}
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, useFtLane ? tempFs : FtL);

	if (needsVu1MaddLaneVuDouble)
	{
		mVU.regAlloc->clearNeededXmmId(t1);
	}
	mVU.regAlloc->clearNeededXmmId(tempFs);
	mVU.regAlloc->clearNeededXmmId(Fs);
	if (useFtLane)
		mVU.regAlloc->clearNeededXmmId(FtRaw);
	else
		mVU.regAlloc->clearNeededXmmId(FtL);
	mVU.regAlloc->clearNeededXmmId(ACC);
}

static void mVU_MADDx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MADD_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("MADD"); mVUlogFd(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MADDy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MADD_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("MADD"); mVUlogFd(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MADDz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MADD_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("MADD"); mVUlogFd(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MADDw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MADD_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("MADD"); mVUlogFd(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MSUB Opcode
static void mVU_MSUB_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const bool needsVu1XyzwVuDouble = isVU1 && !isCOP2 && _XYZW_PS;

	int Ft;
	int tempFt;
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (clampE)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int Fd = mVU.regAlloc->allocRegId(32, _Fd_, _X_Y_Z_W);
	const int FsWork = needsVu1XyzwVuDouble ? mVU.regAlloc->allocRegId() : Fs;
	const int FtWork = needsVu1XyzwVuDouble ? mVU.regAlloc->allocRegId() : VU_HOST_NO_XMM;
	const int t1 = (needsVu1XyzwVuDouble && !mVUUpperUseHardwareInputFlush_oaknut(mVU.index)) ?
		mVU.regAlloc->allocRegId() : VU_HOST_NO_XMM;

	recBeginOaknutEmit();
	if (needsVu1XyzwVuDouble)
	{
		oakAsm->MOV(oakQRegister(FsWork).B16(), oakQRegister(Fs).B16());
		oakAsm->MOV(oakQRegister(FtWork).B16(), oakQRegister(Ft).B16());
		mVUUpperPrepareVuDoubleBounds_oaknut(mVU);
		mVUUpperVuDoubleVector_oaknut(mVU, Fd, t1, true);
		mVUUpperVuDoubleVector_oaknut(mVU, FsWork, t1, true);
		mVUUpperVuDoubleVector_oaknut(mVU, FtWork, t1, true);
	}
	if (isCOP2)
	{
		if (_XYZW_SS)
			mVUUpperClampOperandScalar_oaknut(mVU, FsWork, false);
		else
			mVUUpperClamp2Vector_oaknut(mVU, FsWork, false);
	}
	if (_XYZW_SS)
		mVUUpperFmlsSs_oaknut(mVU, Fd, FsWork, Ft);
	else
	{
		mVUUpperFmlsPs_oaknut(mVU, Fd, FsWork, needsVu1XyzwVuDouble ? FtWork : Ft);
		if (needsVu1XyzwVuDouble)
		{
			mVUUpperClampVu1XyzwMsubResult_oaknut(mVU, Fd);
		}
	}
	recEndOaknutEmit();

	if (needsVu1XyzwVuDouble)
		mVUupdateFlags_oaknut(mVU, Fd, tempFt);
	else
		mVUupdateFlags_oaknut(mVU, Fd, Fs, tempFt);

	if (needsVu1XyzwVuDouble)
	{
		mVU.regAlloc->clearNeededXmmId(t1);
		mVU.regAlloc->clearNeededXmmId(FtWork);
		mVU.regAlloc->clearNeededXmmId(FsWork);
	}

	mVU.regAlloc->clearNeededXmmId(Fd);
	mVU.regAlloc->clearNeededXmmId(Ft);
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_MSUB_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MSUB_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MSUB");
		mVUlogFd();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MSUBi Opcode
static void mVU_MSUBi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int Fd = mVU.regAlloc->allocRegId(32, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperFmlsSs_oaknut(mVU, Fd, Fs, Fi);
	else
		mVUUpperFmlsPs_oaknut(mVU, Fd, Fs, Fi);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fd, Fs, Fi);

	mVU.regAlloc->clearNeededXmmId(Fd);
	mVU.regAlloc->clearNeededXmmId(Fi);
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_MSUBi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2 { mVU_MSUBi_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MSUBi");
		mVUlogFd();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MSUBq Opcode
static void mVU_MSUBq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int Fd = mVU.regAlloc->allocRegId(32, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperFmlsSs_oaknut(mVU, Fd, Fs, Fq);
	else
		mVUUpperFmlsPs_oaknut(mVU, Fd, Fs, Fq);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fd, Fs, tempFq);

	mVU.regAlloc->clearNeededXmmId(Fd);
	mVU.regAlloc->clearNeededXmmId(Fq);
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_MSUBq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2 { mVU_MSUBq_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MSUBq");
		mVUlogFd();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MSUB_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	mVUUpperNaturalScope natural_scope(mVU);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int Fd = mVU.regAlloc->allocRegId(32, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperFmlsSs_oaknut(mVU, Fd, Fs, FtL);
	else
		mVUUpperFmlsPs_oaknut(mVU, Fd, Fs, FtL);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fd, Fs, FtL);

	mVU.regAlloc->clearNeededXmmId(Fd);
	mVU.regAlloc->clearNeededXmmId(FtL);
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_MSUBx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MSUB_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("MSUB"); mVUlogFd(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MSUBy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MSUB_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("MSUB"); mVUlogFd(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MSUBz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MSUB_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("MSUB"); mVUlogFd(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MSUBw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MSUB_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("MSUB"); mVUlogFd(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MSUBA Opcode
static void mVU_MSUBA_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	int Ft;
	int tempFt;
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (clampE)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, 32, 0xf, false);

	if (_XYZW_SS || _X_Y_Z_W == 0xf)
	{
		if (_XYZW_SS2)
		{
			const int tempACC = mVU.regAlloc->allocRegId();
			recBeginOaknutEmit();
			if (isCOP2)
				mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
			const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
			else
				oakAsm->MOV(oakQRegister(tempACC).Selem()[0], oakQRegister(ACC).Selem()[laneIdx]);
			mVUUpperFmlsSs_oaknut(mVU, tempACC, Fs, Ft);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, tempACC, Fs, tempFt);
			recBeginOaknutEmit();
			oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(tempACC).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
			recEndOaknutEmit();
			mVU.regAlloc->clearNeededXmmId(tempACC);
		}
		else
		{
			recBeginOaknutEmit();
			if (isCOP2)
			{
				if (_XYZW_SS)
					mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
				else
					mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
			}
			if (_XYZW_SS)
				mVUUpperFmlsSs_oaknut(mVU, ACC, Fs, Ft);
			else
				mVU_FMACbSubPs_oaknut(mVU, ACC, Fs, Ft);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFt);
		}
	}
	else
	{
		const int tempACC = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		if (isCOP2)
			mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
		mVU_FMACbSubPs_oaknut(mVU, tempACC, Fs, Ft);
		mVUUpperMergeRegs_oaknut(ACC, tempACC, _X_Y_Z_W);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFt);
		mVU.regAlloc->clearNeededXmmId(tempACC);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_MSUBA_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, _Ft_); }
	pass2
	{
		if (isVU0 && !isCOP2 && _X_Y_Z_W == 0xe)
			mVU.regAlloc->flushAll();
		mVU_MSUBA_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("MSUBA");
		mVUlogACC();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MSUBAi Opcode
static void mVU_MSUBAi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, 32, 0xf, false);

	if (_XYZW_SS || _X_Y_Z_W == 0xf)
	{
		if (_XYZW_SS2)
		{
			const int tempACC = mVU.regAlloc->allocRegId();
			recBeginOaknutEmit();
			const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
			else
				oakAsm->MOV(oakQRegister(tempACC).Selem()[0], oakQRegister(ACC).Selem()[laneIdx]);
			mVU_FMACbSubSs_oaknut(mVU, tempACC, Fs, Fi);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, tempACC, Fs, Fi);
			recBeginOaknutEmit();
			oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(tempACC).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
			recEndOaknutEmit();
			mVU.regAlloc->clearNeededXmmId(tempACC);
		}
		else
		{
			recBeginOaknutEmit();
			if (_XYZW_SS)
				mVU_FMACbSubSs_oaknut(mVU, ACC, Fs, Fi);
			else
				mVU_FMACbSubPs_oaknut(mVU, ACC, Fs, Fi);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, ACC, Fs, Fi);
		}
	}
	else
	{
		const int tempACC = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
		mVU_FMACbSubPs_oaknut(mVU, tempACC, Fs, Fi);
		mVUUpperMergeRegs_oaknut(ACC, tempACC, _X_Y_Z_W);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, Fi);
		mVU.regAlloc->clearNeededXmmId(tempACC);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_MSUBAi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_MSUBAi_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MSUBAi");
		mVUlogACC();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MSUBAq Opcode
static void mVU_MSUBAq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU);
	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, 32, 0xf, false);

	if (_XYZW_SS || _X_Y_Z_W == 0xf)
	{
		if (_XYZW_SS2)
		{
			const int tempACC = mVU.regAlloc->allocRegId();
			recBeginOaknutEmit();
			const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
			else
				oakAsm->MOV(oakQRegister(tempACC).Selem()[0], oakQRegister(ACC).Selem()[laneIdx]);
			mVU_FMACbSubSs_oaknut(mVU, tempACC, Fs, Fq);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, tempACC, Fs, tempFq);
			recBeginOaknutEmit();
			oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(tempACC).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
			recEndOaknutEmit();
			mVU.regAlloc->clearNeededXmmId(tempACC);
		}
		else
		{
			recBeginOaknutEmit();
			if (_XYZW_SS)
				mVU_FMACbSubSs_oaknut(mVU, ACC, Fs, Fq);
			else
				mVU_FMACbSubPs_oaknut(mVU, ACC, Fs, Fq);
			recEndOaknutEmit();
			mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFq);
		}
	}
	else
	{
		const int tempACC = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
		mVU_FMACbSubPs_oaknut(mVU, tempACC, Fs, Fq);
		mVUUpperMergeRegs_oaknut(ACC, tempACC, _X_Y_Z_W);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFq);
		mVU.regAlloc->clearNeededXmmId(tempACC);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fq);
}

static void mVU_MSUBAq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_MSUBAq_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MSUBAq");
		mVUlogACC();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MSUBA_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane, bool updateFlags = true)
{
	mVUUpperNaturalScope natural_scope(mVU);
	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, 32, 0xf, false);
	const bool useMaskedVectorPath = isVU0 && isCOP2 && lane == 1 && _XYZW_SS && !_W;

	if ((_XYZW_SS && !useMaskedVectorPath) || _X_Y_Z_W == 0xf)
	{
		if (_XYZW_SS)
		{
			const int tempACC = mVU.regAlloc->allocRegId();
			recBeginOaknutEmit();
			const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
			else
				oakAsm->MOV(oakQRegister(tempACC).Selem()[0], oakQRegister(ACC).Selem()[laneIdx]);
			mVU_FMACbSubSs_oaknut(mVU, tempACC, Fs, FtL);
			recEndOaknutEmit();
			if (updateFlags)
				mVUupdateFlags_oaknut(mVU, tempACC, Fs, FtL);
			recBeginOaknutEmit();
			// The canonical scalar-ACC test covered MADD/MSUB in every lane and
			// FPCR mode (40,655,360 cases) with bit-exact results and flags input.
			oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(tempACC).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
			recEndOaknutEmit();
			mVU.regAlloc->clearNeededXmmId(tempACC);
		}
		else
		{
			recBeginOaknutEmit();
			if (_XYZW_SS)
				mVU_FMACbSubSs_oaknut(mVU, ACC, Fs, FtL);
			else
				mVU_FMACbSubPs_oaknut(mVU, ACC, Fs, FtL);
			recEndOaknutEmit();
			if (updateFlags)
				mVUupdateFlags_oaknut(mVU, ACC, Fs, FtL);
		}
	}
	else
	{
		const int tempACC = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		oakAsm->MOV(oakQRegister(tempACC).B16(), oakQRegister(ACC).B16());
		mVU_FMACbSubPs_oaknut(mVU, tempACC, Fs, FtL);
		mVUUpperMergeRegs_oaknut(ACC, tempACC, _X_Y_Z_W);
		recEndOaknutEmit();
		if (updateFlags)
			mVUupdateFlags_oaknut(mVU, ACC, Fs, FtL);
		mVU.regAlloc->clearNeededXmmId(tempACC);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_MSUBAx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MSUBA_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("MSUBA"); mVUlogACC(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MSUBAy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MSUBA_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("MSUBA"); mVUlogACC(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MSUBAz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MSUBA_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("MSUBA"); mVUlogACC(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MSUBAw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MSUBA_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("MSUBA"); mVUlogACC(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MAX Opcode
static bool mVUTrySharedMaxMin(mV, bool maximum, u32 rhs_lane = 4, bool scalar_i = false)
{
	if (!_Fd_ || mVUtrySkipNoLaneWrite_oaknut(mVU) || !mVUSharedIrEnabled(mVU.index)) return false;
	static thread_local ir::Function fn;
	if (fn.blocks.empty())
	{
		ir::Builder init(fn);
		init.CreateBlock(0);
		fn.blocks[0].insts.reserve(18);
		fn.value_types.reserve(17);
	}
	fn.blocks[0].insts.clear();
	fn.blocks[0].guest_start = mVU.prog.IRinfo.curPC * sizeof(u32);
	fn.value_types.resize(1);
	fn.next_value = 1;
	ir::Builder b(fn);
	b.SetBlock(1);
	b.SetDelaySlot(mVUinfo.isBdelay);
	const u32 lhs = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, _Fs_);
	const u32 raw_rhs = scalar_i ?
		b.Emit(ir::Op::ReadVuScalarBits, ir::Type::I32, {}, static_cast<u64>(ir::VuScalar::I)) :
		b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, _Ft_);
	const u32 rhs = scalar_i ? b.Emit1(ir::Op::VSplat32, ir::Type::V4I32, raw_rhs) :
		rhs_lane < 4 ? b.Emit1(ir::Op::VShuffle, ir::Type::V4I32, raw_rhs, rhs_lane * 0x55u) : raw_rhs;
	const u32 shift31 = b.ConstI32(31), shift1 = b.ConstI32(1);
	const u32 sign_lhs = b.Emit2(ir::Op::VShrS, ir::Type::V4I32, lhs, shift31);
	const u32 flip_lhs = b.Emit2(ir::Op::VShrU, ir::Type::V4I32, sign_lhs, shift1);
	const u32 key_lhs = b.Emit2(ir::Op::VXor, ir::Type::V4I32, lhs, flip_lhs);
	const u32 sign_rhs = b.Emit2(ir::Op::VShrS, ir::Type::V4I32, rhs, shift31);
	const u32 flip_rhs = b.Emit2(ir::Op::VShrU, ir::Type::V4I32, sign_rhs, shift1);
	const u32 key_rhs = b.Emit2(ir::Op::VXor, ir::Type::V4I32, rhs, flip_rhs);
	const u32 choose_lhs = b.Emit2(ir::Op::VCmpLtS, ir::Type::V4I32,
		maximum ? key_rhs : key_lhs, maximum ? key_lhs : key_rhs);
	const u32 selected_lhs = b.Emit2(ir::Op::VAnd, ir::Type::V4I32, lhs, choose_lhs);
	const u32 choose_rhs = b.Emit1(ir::Op::VNot, ir::Type::V4I32, choose_lhs);
	const u32 selected_rhs = b.Emit2(ir::Op::VAnd, ir::Type::V4I32, rhs, choose_rhs);
	const u32 result = b.Emit2(ir::Op::VOr, ir::Type::V4I32, selected_lhs, selected_rhs);
	b.Emit1(ir::Op::WriteVfBitsMasked, ir::Type::Void, result, _Fd_, _X_Y_Z_W);
	b.Return();
	Arm64Ir::BoundVfMaxMinPlan plan;
	std::string error;
	if (!Arm64Ir::PlanBoundVfMaxMinBlock(fn, &plan, &error)) return false;
	// Preserve both full source bindings before requesting a writable partial
	// destination. microVU owns WAW/backup handling and packed-lane writeback.
	const int lhs_reg = mVU.regAlloc->allocRegId(plan.lhs_vf);
	// Allocator identity 33 is the cached architectural I word, not VF33.
	// Bind it read-only and full-width before allocating a masked VF writer.
	const int rhs_reg = mVU.regAlloc->allocRegId(plan.rhs_kind == Arm64Ir::BoundVfMaxMinRhs::ScalarI ? 33 : plan.rhs_vf);
	const int dst_reg = mVU.regAlloc->allocRegId(-1, plan.dst_vf, plan.lane_mask);
	Arm64Ir::LowerOutput out;
	const bool lowered = Arm64Ir::LowerBoundVfMaxMinPlan(plan, lhs_reg, rhs_reg, dst_reg,
		&out, &error, Arm64Ir::BoundVfMaxMinLayout::MicroVuPartial);
	pxAssertRel(lowered, error.c_str());
	// Release the writer first so the allocator merges/invalidate aliases.
	// Repeated source identities may share the same cached host register.
	mVU.regAlloc->clearNeededXmmId(dst_reg);
	if (lhs_reg != dst_reg) mVU.regAlloc->clearNeededXmmId(lhs_reg);
	if (rhs_reg != dst_reg && rhs_reg != lhs_reg) mVU.regAlloc->clearNeededXmmId(rhs_reg);
	if (lowered)
	{
		mVURecordSharedIr(mVU.index, out.host_size);
		if (scalar_i) mVURecordSharedMaxMinI(mVU.index, out.host_size, maximum);
		else if (rhs_lane == 4) mVURecordSharedMaxMin(mVU.index, out.host_size);
		else mVURecordSharedMaxMinBroadcast(mVU.index, out.host_size, rhs_lane, maximum);
	}
	return lowered;
}

static void mVU_MAX_direct_emit_oaknut(mP)
{
	if (mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;

	int Ft;
	if (_XYZW_SS2)
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
	else if (clampE)
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
	else
		Ft = mVU.regAlloc->allocRegId(_Ft_);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperMaxScalar_oaknut(Fs, Ft, _Fs_ == 0 || _Ft_ == 0);
	else
		mVUUpperMaxVector_oaknut(Fs, Ft, _Fs_ == 0 || _Ft_ == 0);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_MAX_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2
	{
		if (!mVUTrySharedMaxMin(mVU, true))
			mVU_MAX_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("MAX");
		mVUlogFd();
		mVUlogFt();
	}
}

// MAXi Opcode
static void mVU_MAXi_direct_emit_oaknut(mP)
{
	if (mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;

	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperMaxScalar_oaknut(Fs, Fi, _Fs_ == 0);
	else
		mVUUpperMaxVector_oaknut(Fs, Fi, _Fs_ == 0);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_MAXi_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, true, 4, true)) mVU_MAXi_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MAXi");
		mVUlogFd();
		mVUlogI();
	}
}

static void mVU_MAX_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	if (mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;

	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperMaxScalar_oaknut(Fs, FtL, _Fs_ == 0 || _Ft_ == 0);
	else
		mVUUpperMaxVector_oaknut(Fs, FtL, _Fs_ == 0 || _Ft_ == 0);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_MAXx_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, true, 0)) mVU_MAX_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("MAX"); mVUlogFd(); mVUlog(", vf%02dx", _Ft_); }
}

static void mVU_MAXy_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, true, 1)) mVU_MAX_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("MAX"); mVUlogFd(); mVUlog(", vf%02dy", _Ft_); }
}

static void mVU_MAXz_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, true, 2)) mVU_MAX_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("MAX"); mVUlogFd(); mVUlog(", vf%02dz", _Ft_); }
}

static void mVU_MAXw_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, true, 3)) mVU_MAX_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("MAX"); mVUlogFd(); mVUlog(", vf%02dw", _Ft_); }
}

static void mVU_MINI_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	if (mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;

	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperMiniScalar_oaknut(Fs, FtL, _Fs_ == 0 || _Ft_ == 0);
	else
		mVUUpperMiniVector_oaknut(Fs, FtL, _Fs_ == 0 || _Ft_ == 0);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_MINIw_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, false, 3)) mVU_MINI_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("MINI"); mVUlogFd(); mVUlog(", vf%02dw", _Ft_); }
}

// MINI Opcode
static void mVU_MINI_direct_emit_oaknut(mP)
{
	if (mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;

	int Ft;
	if (_XYZW_SS2)
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
	else if (clampE)
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
	else
		Ft = mVU.regAlloc->allocRegId(_Ft_);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperMiniScalar_oaknut(Fs, Ft, _Fs_ == 0 || _Ft_ == 0);
	else
		mVUUpperMiniVector_oaknut(Fs, Ft, _Fs_ == 0 || _Ft_ == 0);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_MINI_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2
	{
		if (!mVUTrySharedMaxMin(mVU, false))
			mVU_MINI_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("MINI");
		mVUlogFd();
		mVUlogFt();
	}
}

// MINIi Opcode
static void mVU_MINIi_direct_emit_oaknut(mP)
{
	if (mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;

	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperMiniScalar_oaknut(Fs, Fi, _Fs_ == 0);
	else
		mVUUpperMiniVector_oaknut(Fs, Fi, _Fs_ == 0);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_MINIi_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, false, 4, true)) mVU_MINIi_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MINIi");
		mVUlogFd();
		mVUlogI();
	}
}

static void mVU_MINIx_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, false, 0)) mVU_MINI_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("MINI"); mVUlogFd(); mVUlog(", vf%02dx", _Ft_); }
}

static void mVU_MINIy_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, false, 1)) mVU_MINI_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("MINI"); mVUlogFd(); mVUlog(", vf%02dy", _Ft_); }
}

static void mVU_MINIz_emit(mP)
{
	pass1
	{
		mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_);
		sFLAG.doFlag = false;
	}
	pass2 { if (!mVUTrySharedMaxMin(mVU, false, 2)) mVU_MINI_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("MINI"); mVUlogFd(); mVUlog(", vf%02dz", _Ft_); }
}

// ADDi Opcode
static void mVU_ADDi_triace_hack_oaknut(int to, int from)
{
	// Keep the mask-and-FADD paths. A direct-select candidate required extra
	// handling for zeroed upper lanes and signaling NaNs; the safe form matched
	// all 65,536 exponent pairs in 8 FPCR modes but was slower in 6/7 runs.
	const oak::QReg to_q = oakQRegister(to);
	const oak::QReg from_q = oakQRegister(from);
	const oak::WReg to_bits = OAK_WSCRATCH;
	const oak::WReg from_bits = OAK_WSCRATCH2;
	oak::Label case_neg_big;
	oak::Label case_end1;
	oak::Label case_end2;

	oakAsm->FMOV(to_bits, oakSRegister(to));
	oakAsm->FMOV(from_bits, oakSRegister(from));
	// Extracting the exponent directly saves two runtime instructions from
	// every scalar ADDi using the Tri-Ace bit-accuracy fix.
	oakAsm->UBFX(to_bits, to_bits, 23, 8);
	oakAsm->UBFX(from_bits, from_bits, 23, 8);
	oakAsm->SUB(from_bits, from_bits, to_bits);

	oakAsm->CMN(from_bits, 25);
	oakAsm->B(oak::Cond::LE, case_neg_big);
	oakAsm->CMP(from_bits, 25);
	oakAsm->B(oak::Cond::LT, case_end1);

	oakLoad128(OAK_QSCRATCH3, mVUUpperOakSs4Mem(offsetof(mVU_SSE4, sseMasks.ADD_SS)));
	oakAsm->AND(to_q.B16(), to_q.B16(), OAK_QSCRATCH3.B16());
	oakAsm->B(case_end2);

	oakAsm->l(case_neg_big);
	oakLoad128(OAK_QSCRATCH3, mVUUpperOakSs4Mem(offsetof(mVU_SSE4, sseMasks.ADD_SS)));
	oakAsm->AND(from_q.B16(), from_q.B16(), OAK_QSCRATCH3.B16());

	oakAsm->l(case_end1);
	oakAsm->l(case_end2);
	oakAsm->FADD(oakSRegister(to), oakSRegister(to), oakSRegister(from));
}

static void mVU_ADDi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, !CHECK_VUADDSUBHACK);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
	{
		if (!CHECK_VUADDSUBHACK)
			mVUUpperAddSs_oaknut(mVU, Fs, Fi);
		else
			mVU_ADDi_triace_hack_oaknut(Fs, Fi);
	}
	else
	{
		mVUUpperAddPs_oaknut(mVU, Fs, Fi);
	}
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, Fi);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_ADDi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2
	{
		mVU_ADDi_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("ADDi");
		mVUlogFd();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// ADDq Opcode
static void mVU_ADDq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperAddSs_oaknut(mVU, Fs, Fq);
	else
		mVUUpperAddPs_oaknut(mVU, Fs, Fq);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, tempFq);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fq);
}

static void mVU_ADDq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2
	{
		mVU_ADDq_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("ADDq");
		mVUlogFd();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_ADD_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);
	recBeginOaknutEmit();
	if (_XYZW_SS)
		mVUUpperAddSs_oaknut(mVU, Fs, FtL);
	else
		mVUUpperAddPs_oaknut(mVU, Fs, FtL);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, FtL);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_ADDx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_ADD_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("ADD"); mVUlogFd(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_ADDy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_ADD_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("ADD"); mVUlogFd(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_ADDz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_ADD_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("ADD"); mVUlogFd(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_ADDw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_ADD_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("ADD"); mVUlogFd(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// SUB Opcode
static void mVU_SUB_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	// x - x is only unconditionally zero when exponent-255 inputs are clamped.
	// In Normal mode, infinities and NaNs must still go through FSUB so their
	// result and MAC/STATUS flags match the interpreter.
	if (_Ft_ == _Fs_ && CHECK_VU_OVERFLOW(mVU.index))
	{
		const int Fs = mVU.regAlloc->allocRegId(-1, _Fd_, _X_Y_Z_W);
		recBeginOaknutEmit();
		oakAsm->EOR(oakQRegister(Fs).B16(), oakQRegister(Fs).B16(), oakQRegister(Fs).B16());
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs);
		mVU.regAlloc->clearNeededXmmId(Fs);
		return;
	}

	int Ft;
	int tempFt;
	const int clampType = _XYZW_PS ? (cFs | cFt) : 0;
	const bool needsFullFtForClamp = mVUUpperWillClampFt_oaknut(mVU, clampType);

	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (needsFullFtForClamp)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_PS)
	{
		if (_XYZW_SS)
		{
			mVUUpperClampOperandScalar_oaknut(mVU, Ft, false);
			mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
		}
		else
		{
			mVUUpperClamp2Vector_oaknut(mVU, Ft, false);
			mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		}
	}
	if (_XYZW_SS)
		mVUUpperSubSs_oaknut(mVU, Fs, Ft);
	else
		mVUUpperSubPs_oaknut(mVU, Fs, Ft);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, tempFt);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_SUB_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, _Ft_); }
	pass2
	{
		mVU_SUB_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("SUB");
		mVUlogFd();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// SUBi Opcode
static void mVU_SUBi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_PS)
	{
		if (_XYZW_SS)
		{
			mVUUpperClampOperandScalar_oaknut(mVU, Fi, false);
			mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
		}
		else
		{
			mVUUpperClamp2Vector_oaknut(mVU, Fi, false);
			mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		}
	}
	if (_XYZW_SS)
		mVUUpperSubSs_oaknut(mVU, Fs, Fi);
	else
		mVUUpperSubPs_oaknut(mVU, Fs, Fi);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, Fi);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_SUBi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2
	{
		mVU_SUBi_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("SUBi");
		mVUlogFd();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// SUBq Opcode
static void mVU_SUBq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_PS)
	{
		if (_XYZW_SS)
		{
			mVUUpperClampOperandScalar_oaknut(mVU, Fq, false);
			mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
		}
		else
		{
			mVUUpperClamp2Vector_oaknut(mVU, Fq, false);
			mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		}
	}
	if (_XYZW_SS)
		mVUUpperSubSs_oaknut(mVU, Fs, Fq);
	else
		mVUUpperSubPs_oaknut(mVU, Fs, Fq);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, tempFq);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fq);
}

static void mVU_SUBq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2
	{
		mVU_SUBq_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("SUBq");
		mVUlogFd();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_SUB_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_PS)
	{
		if (_XYZW_SS)
		{
			mVUUpperClampOperandScalar_oaknut(mVU, FtL, false);
			mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
		}
		else
		{
			mVUUpperClamp2Vector_oaknut(mVU, FtL, false);
			mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		}
	}
	if (_XYZW_SS)
		mVUUpperSubSs_oaknut(mVU, Fs, FtL);
	else
		mVUUpperSubPs_oaknut(mVU, Fs, FtL);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, FtL);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_SUBx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_SUB_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("SUB"); mVUlogFd(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_SUBy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_SUB_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("SUB"); mVUlogFd(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_SUBz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_SUB_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("SUB"); mVUlogFd(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_SUBw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_SUB_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("SUB"); mVUlogFd(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// SUBA Opcode
static void mVU_SUBA_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (_Ft_ == _Fs_ && CHECK_VU_OVERFLOW(mVU.index))
	{
		const int Fs = mVU.regAlloc->allocRegId(-1, 32, _X_Y_Z_W);
		recBeginOaknutEmit();
		oakAsm->EOR(oakQRegister(Fs).B16(), oakQRegister(Fs).B16(), oakQRegister(Fs).B16());
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs);
		mVU.regAlloc->clearNeededXmmId(Fs);
		return;
	}

	int Ft;
	int tempFt;
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (clampE)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperSubSs_oaknut(mVU, Fs, Ft);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, tempFt);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		// SUBA, like ADDA, does not consume ACC. A single lane insert preserves
		// the other 96 bits without a temporary full-vector copy.
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperSubSs_oaknut(mVU, Fs, Ft);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperSubPs_oaknut(mVU, Fs, Ft);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFt);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_SUBA_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_SUBA_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("SUBA");
		mVUlogACC();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// SUBAi Opcode
static void mVU_SUBAi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperSubSs_oaknut(mVU, Fs, Fi);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, Fi);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperSubSs_oaknut(mVU, Fs, Fi);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperSubPs_oaknut(mVU, Fs, Fi);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, Fi);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_SUBAi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_SUBAi_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("SUBAi");
		mVUlogACC();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// SUBAq Opcode
static void mVU_SUBAq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperSubSs_oaknut(mVU, Fs, Fq);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, tempFq);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperSubSs_oaknut(mVU, Fs, Fq);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperSubPs_oaknut(mVU, Fs, Fq);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFq);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fq);
}

static void mVU_SUBAq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_SUBAq_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("SUBAq");
		mVUlogACC();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_SUBA_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	const int FtL = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FtRaw);

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperSubSs_oaknut(mVU, Fs, FtL);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, FtL);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperSubSs_oaknut(mVU, Fs, FtL);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperSubPs_oaknut(mVU, Fs, FtL);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, FtL);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_SUBAx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_SUBA_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("SUBA"); mVUlogACC(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_SUBAy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_SUBA_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("SUBA"); mVUlogACC(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_SUBAz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_SUBA_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("SUBA"); mVUlogACC(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_SUBAw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_SUBA_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("SUBA"); mVUlogACC(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MUL Opcode
static void mVU_MUL_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	int Ft;
	int tempFt;
	const bool clampFt = _XYZW_PS;
	const bool willClampFt = clampE || (clampFt && !clampE && (CHECK_VU_OVERFLOW(mVU.index) || CHECK_VU_SIGN_OVERFLOW(mVU.index)));

	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (willClampFt)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
	{
		if (clampFt)
			mVUUpperClampOperandScalar_oaknut(mVU, Ft, false);
		mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
		mVUUpperMulSs_oaknut(mVU, Fs, Ft);
	}
	else
	{
		if (clampFt)
			mVUUpperClamp2Vector_oaknut(mVU, Ft, false);
		mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		mVUUpperMulPs_oaknut(mVU, Fs, Ft);
	}
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, tempFt);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_MUL_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, _Ft_); }
	pass2
	{
		mVU_MUL_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("MUL");
		mVUlogFd();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MULA Opcode
static void mVU_MULA_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	int Ft;
	int tempFt;
	if (_XYZW_SS2 && !mVUUpperNatural_oaknut(mVU))
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
		tempFt = Ft;
	}
	else if (clampE)
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0xf);
		tempFt = Ft;
	}
	else
	{
		Ft = mVU.regAlloc->allocRegId(_Ft_);
		tempFt = VU_HOST_NO_XMM;
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperMulSs_oaknut(mVU, Fs, Ft);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, tempFt);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		// The 24,393,216-case canonical add/sub/mul test verified the direct
		// commit and preservation of every untouched ACC lane in all FPCR modes.
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperMulSs_oaknut(mVU, Fs, Ft);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperMulPs_oaknut(mVU, Fs, Ft);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFt);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_MULA_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MULA_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MULA");
		mVUlogACC();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MULAi Opcode
static void mVU_MULAi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperMulSs_oaknut(mVU, Fs, Fi);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, Fi);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperMulSs_oaknut(mVU, Fs, Fi);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperMulPs_oaknut(mVU, Fs, Fi);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, Fi);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_MULAi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_MULAi_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MULAi");
		mVUlogACC();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MULAq Opcode
static void mVU_MULAq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	int Fq;
	int tempFq;
	if (!clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		recBeginOaknutEmit();
		mVUUpperMulSs_oaknut(mVU, Fs, Fq);
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, Fs, tempFq);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
		{
			mVUUpperMulSs_oaknut(mVU, Fs, Fq);
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			mVUUpperMulPs_oaknut(mVU, Fs, Fq);
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, tempFq);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fq);
}

static void mVU_MULAq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, 0, _Fs_, 0); }
	pass2 { mVU_MULAq_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("MULAq");
		mVUlogACC();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MULA lane Opcodes
static void mVU_MULA_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	const bool shouldClampFt = (lane == 3) && _XYZW_PS && mVUUpperWillClampFt_oaknut(mVU, cFt);
	const bool useFtLane = !shouldClampFt;
	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	int FtL = VU_HOST_NO_XMM;
	if (!useFtLane)
	{
		FtL = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
		recEndOaknutEmit();
		mVU.regAlloc->clearNeededXmmId(FtRaw);
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVUlocalRuleEnabled(15) && _XYZW_PS ? Fs :
		mVU.regAlloc->allocRegId(_XYZW_PS ? -1 : 32, 32, 0xf, false);

	if (_XYZW_SS2)
	{
		const int flagsTemp = (useFtLane && (sFLAG.doFlag || mFLAG.doFlag)) ?
			mVU.regAlloc->allocRegId() : VU_HOST_NO_XMM;
		recBeginOaknutEmit();
		mVUUpperClampAccLaneFs_oaknut(mVU, Fs, !mVUUpperNatural_oaknut(mVU));
		if (useFtLane)
			mVUUpperMulSsLane_oaknut(mVU, Fs, FtRaw, lane);
		else
			mVUUpperMulSs_oaknut(mVU, Fs, FtL);
		recEndOaknutEmit();
		// FtRaw is a cached guest VF register, while mVUupdateFlags overwrites its
		// first temporary. Keep the direct ACC lane commit, but use an uninitialized
		// host temp for flags; allocation emits no runtime instruction. GTA SA VU0
		// and Tekken 5 VU1 save tests exposed the stale-cache corruption here.
		mVUupdateFlags_oaknut(mVU, Fs, useFtLane ? flagsTemp : FtL);
		recBeginOaknutEmit();
		const int laneIdx = (_X ? 0 : (_Y ? 1 : (_Z ? 2 : 3)));
		oakAsm->MOV(oakQRegister(ACC).Selem()[laneIdx], oakQRegister(Fs).Selem()[mVUUpperNatural_oaknut(mVU) ? laneIdx : 0]);
		recEndOaknutEmit();
		mVU.regAlloc->clearNeededXmmId(flagsTemp);
	}
	else
	{
		recBeginOaknutEmit();
		if (_XYZW_SS)
			mVUUpperClampAccLaneFs_oaknut(mVU, Fs, !mVUUpperNatural_oaknut(mVU));
		else
			mVUUpperClampAccLaneFs_oaknut(mVU, Fs, false);
		const bool identity = mVUUpperSkipCanonicalLaneMul_oaknut(mVU, Fs, lane, true);
		if (_XYZW_SS)
		{
			if (!identity)
			{
				if (useFtLane) mVUUpperMulSsLane_oaknut(mVU, Fs, FtRaw, lane);
				else mVUUpperMulSs_oaknut(mVU, Fs, FtL);
			}
			oakAsm->MOV(oakQRegister(ACC).Selem()[0], oakQRegister(Fs).Selem()[0]);
		}
		else
		{
			if (!identity)
			{
				if (useFtLane) mVUUpperMulPsLane_oaknut(Fs, FtRaw, lane);
				else mVUUpperMulPs_oaknut(mVU, Fs, FtL);
			}
			if (mVUlocalRuleEnabled(15) && _XYZW_PS)
				mVU.regAlloc->rebindFullWrite(Fs, 32);
			else
				mVUUpperMergeRegs_oaknut(ACC, Fs, _X_Y_Z_W);
		}
		recEndOaknutEmit();
		mVUupdateFlags_oaknut(mVU, ACC, Fs, useFtLane ? VU_HOST_NO_XMM : FtL);
	}

	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	if (useFtLane)
		mVU.regAlloc->clearNeededXmmId(FtRaw);
	else
		mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_MULAx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MULA_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("MULA"); mVUlogACC(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MULAy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MULA_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("MULA"); mVUlogACC(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MULAz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MULA_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("MULA"); mVUlogACC(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MULAw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, 0, _Fs_, _Ft_); }
	pass2 { mVU_MULA_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("MULA"); mVUlogACC(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MULi Opcode
static void mVU_MULi_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const int Fi = mVU.regAlloc->allocRegId(33, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
	{
		if (_XYZW_PS)
			mVUUpperClampOperandScalar_oaknut(mVU, Fi, false);
		mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
		mVUUpperMulSs_oaknut(mVU, Fs, Fi);
	}
	else
	{
		if (_XYZW_PS)
			mVUUpperClamp2Vector_oaknut(mVU, Fi, false);
		mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		mVUUpperMulPs_oaknut(mVU, Fs, Fi);
	}
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, Fi);

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Fi);
}

static void mVU_MULi_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2
	{
		mVU_MULi_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("MULi");
		mVUlogFd();
		mVUlogI();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MULq Opcode
static void mVU_MULq_direct_emit_oaknut(mP)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const bool useQLane = !clampE && !_XYZW_PS;
	int Fq = VU_HOST_NO_XMM;
	int tempFq = VU_HOST_NO_XMM;
	if (!useQLane && !clampE && _XYZW_SS && !mVUinfo.readQ && !mVUUpperNatural_oaknut(mVU))
	{
		Fq = VU_HOST_XMMPQ;
		tempFq = VU_HOST_NO_XMM;
	}
	else if (!useQLane)
	{
		Fq = mVU.regAlloc->allocRegId();
		tempFq = Fq;
		recBeginOaknutEmit();
		mVUUpperGetQreg_oaknut(Fq, mVUinfo.readQ);
		recEndOaknutEmit();
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
	{
		if (_XYZW_PS && !useQLane)
			mVUUpperClampOperandScalar_oaknut(mVU, Fq, false);
		mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
		if (useQLane)
		{
			if (mVUUpperNatural_oaknut(mVU))
				oakAsm->FMUL(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), oakQRegister(VU_HOST_XMMPQ).Selem()[mVUinfo.readQ]);
			else
				oakAsm->FMUL(oakSRegister(Fs), oakSRegister(Fs), oakQRegister(VU_HOST_XMMPQ).Selem()[mVUinfo.readQ]);
		}
		else
			mVUUpperMulSs_oaknut(mVU, Fs, Fq);
	}
	else
	{
		if (_XYZW_PS && !useQLane)
			mVUUpperClamp2Vector_oaknut(mVU, Fq, false);
		mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		if (useQLane)
			oakAsm->FMUL(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), oakQRegister(VU_HOST_XMMPQ).Selem()[mVUinfo.readQ]);
		else
			mVUUpperMulPs_oaknut(mVU, Fs, Fq);
	}
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, useQLane ? VU_HOST_NO_XMM : tempFq);

	mVU.regAlloc->clearNeededXmmId(Fs);
	if (Fq != VU_HOST_NO_XMM)
		mVU.regAlloc->clearNeededXmmId(Fq);
}

static void mVU_MULq_emit(mP)
{
	pass1 { mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, 0); }
	pass2
	{
		mVU_MULq_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("MULq");
		mVUlogFd();
		mVUlogQ();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}

// MUL lane Opcodes
static void mVU_MUL_lane_direct_emit_oaknut(microVU& mVU, int recPass, int lane)
{
	mVUUpperNaturalScope natural_scope(mVU, true);
	if (mVUtryEmitNoLaneFmacFlags_oaknut(mVU))
		return;

	const bool shouldClampFt = _XYZW_PS && mVUUpperWillClampFt_oaknut(mVU, cFt);
	const bool useFtLane = !shouldClampFt;
	const int FtRaw = mVU.regAlloc->allocRegId(_Ft_);
	int FtL = VU_HOST_NO_XMM;
	if (!useFtLane)
	{
		FtL = mVU.regAlloc->allocRegId();
		recBeginOaknutEmit();
		mVUUpperDupLane_oaknut(FtL, FtRaw, lane);
		recEndOaknutEmit();
		mVU.regAlloc->clearNeededXmmId(FtRaw);
	}

	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	if (_XYZW_SS)
	{
		if (!useFtLane) {
			if (_XYZW_PS)
				mVUUpperClampOperandScalar_oaknut(mVU, FtL, false);
		}
		mVUUpperClampOperandScalar_oaknut(mVU, Fs, false);
		if (!mVUUpperSkipCanonicalLaneMul_oaknut(mVU, Fs, lane, false))
		{
			if (useFtLane) mVUUpperMulSsLane_oaknut(mVU, Fs, FtRaw, lane);
			else mVUUpperMulSs_oaknut(mVU, Fs, FtL);
		}
	}
	else
	{
		if (!useFtLane) {
			if (_XYZW_PS)
				mVUUpperClamp2Vector_oaknut(mVU, FtL, false);
		}
		mVUUpperClamp2Vector_oaknut(mVU, Fs, false);
		if (!mVUUpperSkipCanonicalLaneMul_oaknut(mVU, Fs, lane, false))
		{
			if (useFtLane) mVUUpperMulPsLane_oaknut(Fs, FtRaw, lane);
			else mVUUpperMulPs_oaknut(mVU, Fs, FtL);
		}
	}
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, Fs, useFtLane ? VU_HOST_NO_XMM : FtL);

	mVU.regAlloc->clearNeededXmmId(Fs);
	if (useFtLane)
		mVU.regAlloc->clearNeededXmmId(FtRaw);
	else
		mVU.regAlloc->clearNeededXmmId(FtL);
}

static void mVU_MULx_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MUL_lane_direct_emit_oaknut(mVU, recPass, 0); }
	pass3 { mVUlog("MUL"); mVUlogFd(); mVUlog(", vf%02dx", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MULy_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MUL_lane_direct_emit_oaknut(mVU, recPass, 1); }
	pass3 { mVUlog("MUL"); mVUlogFd(); mVUlog(", vf%02dy", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MULz_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MUL_lane_direct_emit_oaknut(mVU, recPass, 2); }
	pass3 { mVUlog("MUL"); mVUlogFd(); mVUlog(", vf%02dz", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

static void mVU_MULw_emit(mP)
{
	pass1 { mVUanalyzeFMAC3(mVU, _Fd_, _Fs_, _Ft_); }
	pass2 { mVU_MUL_lane_direct_emit_oaknut(mVU, recPass, 3); }
	pass3 { mVUlog("MUL"); mVUlogFd(); mVUlog(", vf%02dw", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 8; }
}

// ABS Opcode
static void mVU_ABS_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	mVUEmitAbsclipVector_oaknut(OAK_QSCRATCH3);
	oakAsm->AND(oakQRegister(Fs).B16(), oakQRegister(Fs).B16(), OAK_QSCRATCH3.B16());
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static bool mVUTrySharedABS(mV)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU) || !mVUSharedIrEnabled(mVU.index)) return false;
	static thread_local ir::Function fn;
	if (fn.blocks.empty())
	{
		ir::Builder init(fn);
		init.CreateBlock(0);
		fn.blocks[0].insts.reserve(4);
		fn.value_types.reserve(3);
	}
	fn.blocks[0].insts.clear();
	fn.blocks[0].guest_start = mVU.prog.IRinfo.curPC * sizeof(u32);
	fn.value_types.resize(1);
	fn.next_value = 1;
	ir::Builder b(fn);
	b.SetBlock(1);
	b.SetDelaySlot(mVUinfo.isBdelay);
	const u32 source = b.Emit(ir::Op::ReadVf, ir::Type::V4F32, {}, _Fs_);
	const u32 absolute = b.Emit1(ir::Op::VAbs, ir::Type::V4F32, source);
	b.Emit1(ir::Op::WriteVfMasked, ir::Type::Void, absolute, _Ft_, _X_Y_Z_W);
	b.Return();
	Arm64Ir::BoundVfUnaryPlan plan;
	std::string error;
	if (!Arm64Ir::PlanBoundVfUnary(fn, &plan, &error)) return false;
	// Bind the full raw source before requesting the masked destination. The
	// lowerer handles MicroVuPartial's single-lane S0 packing from this full Q.
	const int source_reg = mVU.regAlloc->allocRegId(plan.src_vf);
	const int destination_reg = mVU.regAlloc->allocRegId(-1, plan.dst_vf, plan.lane_mask);
	Arm64Ir::LowerOutput out;
	const bool lowered = Arm64Ir::LowerBoundVfUnaryPlan(plan, source_reg, destination_reg,
		&out, &error, Arm64Ir::BoundVfUnaryLayout::MicroVuPartial);
	pxAssertRel(lowered, error.c_str());
	mVU.regAlloc->clearNeededXmmId(destination_reg);
	mVU.regAlloc->clearNeededXmmId(source_reg);
	mVURecordSharedIr(mVU.index, out.host_size);
	mVURecordSharedAbs(mVU.index, out.host_size);
	return true;
}

static void mVU_ABS_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedABS(mVU))
			mVU_ABS_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("ABS");
		mVUlogFtFs();
	}
}

// CLIP Opcode
static void mVU_CLIP_reference_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, 0xf);
	const int Ft = mVU.regAlloc->allocRegId(_Ft_, 0, 0x1);
	const int t1 = mVU.regAlloc->allocRegId();
	const int t2 = mVU.regAlloc->allocRegId();

	mVUallocCFLAGa(mVU, VU_HOST_T1, cFLAG.lastWrite);

	const oak::QReg fs_q = oakQRegister(Fs);
	const oak::QReg ft_q = oakQRegister(Ft);
	const oak::QReg t1_q = oakQRegister(t1);
	const oak::QReg t2_q = oakQRegister(t2);
	const oak::WReg clip_w = oakWRegister(VU_HOST_T1);
	const oak::WReg mask_w = oakWRegister(VU_HOST_T2);

	recBeginOaknutEmit();
	oakAsm->DUP(ft_q.S4(), ft_q.Selem()[0]);
	oakAsm->LSL(clip_w, clip_w, 6);

	mVUEmitExponentVector_oaknut(t1_q);
	oakAsm->AND(t1_q.B16(), t1_q.B16(), fs_q.B16());
	oakAsm->MOVI(t2_q.B16(), 0);
	oakAsm->CMEQ(t1_q.S4(), t1_q.S4(), t2_q.S4());
	oakAsm->BIC(t1_q.B16(), fs_q.B16(), t1_q.B16());
	mVUEmitAbsclipVector_oaknut(OAK_QSCRATCH3);
	oakAsm->AND(ft_q.B16(), ft_q.B16(), OAK_QSCRATCH3.B16());
	mVUEmitExponentVector_oaknut(t2_q);
	oakAsm->AND(t2_q.B16(), t2_q.B16(), ft_q.B16());
	oakAsm->MOVI(OAK_QSCRATCH.B16(), 0);
	oakAsm->CMEQ(t2_q.S4(), t2_q.S4(), OAK_QSCRATCH.S4());
	oakAsm->MOV(OAK_WSCRATCH, 0x007fffff);
	oakAsm->DUP(OAK_QSCRATCH.S4(), OAK_WSCRATCH);
	oakAsm->BSL(t2_q.B16(), OAK_QSCRATCH.B16(), ft_q.B16());
	oakAsm->MOV(ft_q.B16(), t2_q.B16());

	mVUEmitSignbitVector_oaknut(fs_q);
	oakAsm->EOR(fs_q.B16(), fs_q.B16(), t1_q.B16());
	oakAsm->CMGT(t1_q.S4(), t1_q.S4(), ft_q.S4());
	oakAsm->CMGT(fs_q.S4(), fs_q.S4(), ft_q.S4());
	// PMOVMSKB equivalent: per-lane spread weights map the 0/all-ones compare
	// results to the VU clip layout (bit 2i = +axis, bit 2i+1 = -axis). The
	// negative weights are the positive ones shifted by one, and the upper
	// lanes stay masked off exactly as the old 0x3f truncation did.
	oakLoad128(OAK_QSCRATCH3, mVUUpperOakSs4Mem(offsetof(mVU_SSE4, clip_mask)));
	oakAsm->SHL(OAK_QSCRATCH2.S4(), OAK_QSCRATCH3.S4(), 1);
	oakAsm->AND(t1_q.B16(), t1_q.B16(), OAK_QSCRATCH3.B16());
	oakAsm->AND(fs_q.B16(), fs_q.B16(), OAK_QSCRATCH2.B16());
	oakAsm->ADDV(OAK_SSCRATCH, t1_q.S4());
	oakAsm->ADDV(OAK_SSCRATCH2, fs_q.S4());
	oakAsm->FMOV(mask_w, OAK_SSCRATCH);
	oakAsm->FMOV(OAK_WSCRATCH, OAK_SSCRATCH2);
	oakAsm->ORR(mask_w, mask_w, OAK_WSCRATCH);
	oakAsm->AND(mask_w, mask_w, 0x3f);
	oakAsm->AND(clip_w, clip_w, 0xffffff);
	oakAsm->ORR(clip_w, clip_w, mask_w);
	recEndOaknutEmit();

	mVUallocCFLAGb(mVU, VU_HOST_T1, cFLAG.write);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(t2);
}

static void mVU_CLIP_direct_emit_oaknut(mP)
{
	bool readonly = mVUlocalRuleEnabled(12);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	readonly &= s_vuLocalReadonlyClipEnabled;
#endif
	if (!readonly)
	{
		mVU_CLIP_reference_emit_oaknut(mVU, recPass);
		return;
	}

	// Keep full raw cached inputs readonly. The shared integer lowering owns
	// only Q29-Q31/W16-W17 and the scheduled CLIP-history destination.
	const int Fs = mVU.regAlloc->allocRegId(_Fs_);
	const int Ft = mVU.regAlloc->allocRegId(_Ft_);
	mVUallocCFLAGa(mVU, VU_HOST_T1, cFLAG.lastWrite);
	const Arm64Ir::BoundVfClipPlan plan{static_cast<u8>(_Fs_), static_cast<u8>(_Ft_), mVUinfo.isBdelay != 0};
	Arm64Ir::LowerOutput out;
	std::string error;
	const bool lowered = Arm64Ir::LowerBoundVfClipPlan(plan, Fs, Ft, VU_HOST_T1, &out, &error);
	if (lowered)
		mVUallocCFLAGb(mVU, VU_HOST_T1, cFLAG.write);
	mVU.regAlloc->clearNeededXmmId(Fs);
	if (Ft != Fs) mVU.regAlloc->clearNeededXmmId(Ft);
	if (!lowered)
	{
		mVU_CLIP_reference_emit_oaknut(mVU, recPass);
		return;
	}
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	++s_vuLocalReadonlyClipEmits;
#endif
}

static bool mVUTrySharedClip(mV)
{
	if (!mVUSharedIrEnabled(mVU.index)) return false;
	static thread_local ir::Function fn;
	if (fn.blocks.empty())
	{
		ir::Builder init(fn);
		init.CreateBlock(0);
		fn.blocks[0].insts.reserve(11);
		fn.value_types.reserve(10);
	}
	fn.blocks[0].insts.clear();
	fn.blocks[0].guest_start = mVU.prog.IRinfo.curPC * sizeof(u32);
	fn.value_types.resize(1);
	fn.next_value = 1;
	ir::Builder b(fn);
	b.SetBlock(1);
	b.SetDelaySlot(mVUinfo.isBdelay);
	const u32 fs = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, _Fs_);
	const u32 ft = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, _Ft_);
	const u32 mask = b.Emit2(ir::Op::VClip, ir::Type::I32, fs, ft);
	const u32 old = b.Emit(ir::Op::ReadVuClip, ir::Type::I32, {}, 0);
	const u32 shift = b.ConstI32(6);
	const u32 history = b.Emit2(ir::Op::Shl, ir::Type::I32, old, shift);
	const u32 joined = b.Emit2(ir::Op::Or, ir::Type::I32, history, mask);
	const u32 keep24 = b.ConstI32(0xffffff);
	const u32 next = b.Emit2(ir::Op::And, ir::Type::I32, joined, keep24);
	b.Emit1(ir::Op::WriteVuClip, ir::Type::Void, next, 0);
	b.Return();
	Arm64Ir::BoundVfClipPlan plan;
	std::string error;
	if (!Arm64Ir::PlanBoundVfClipBlock(fn, &plan, &error)) return false;
	// Bind full read-only words before the scheduled CLIP ring-slot access.
	const int lhs = mVU.regAlloc->allocRegId(plan.lhs_vf);
	const int rhs = mVU.regAlloc->allocRegId(plan.rhs_vf);
	mVUallocCFLAGa(mVU, VU_HOST_T1, cFLAG.lastWrite);
	Arm64Ir::LowerOutput out;
	const bool lowered = Arm64Ir::LowerBoundVfClipPlan(plan, lhs, rhs, VU_HOST_T1, &out, &error);
	pxAssertRel(lowered, error.c_str());
	mVUallocCFLAGb(mVU, VU_HOST_T1, cFLAG.write);
	mVU.regAlloc->clearNeededXmmId(rhs);
	if (lhs != rhs) mVU.regAlloc->clearNeededXmmId(lhs);
	mVURecordSharedIr(mVU.index, out.host_size);
	mVURecordSharedClip(mVU.index, out.host_size);
	return true;
}

static void mVU_CLIP_emit(mP)
{
	pass1 { mVUanalyzeFMAC4(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedClip(mVU))
			mVU_CLIP_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("CLIP");
		mVUlogCLIP();
	}
}

// Cross-product permutation primitives. Source bindings stay read-only.
static __fi void mVUopPermuteFs_oaknut(int dst, int src, bool write_w)
{
	pxAssert(dst != src);
	const oak::QReg to = oakQRegister(dst), from = oakQRegister(src);
	oakAsm->EXT(to.B16(), from.B16(), from.B16(), 4);
	oakAsm->MOV(to.Selem()[2], from.Selem()[0]);
	if (write_w)
		oakAsm->MOV(to.Selem()[3], from.Selem()[3]);
}

static __fi void mVUopPermuteFt_oaknut(int dst, int src, bool write_w)
{
	pxAssert(dst != src);
	const oak::QReg to = oakQRegister(dst), from = oakQRegister(src);
	oakAsm->EXT(to.B16(), from.B16(), from.B16(), 12);
	oakAsm->MOV(to.Selem()[0], from.Selem()[2]);
	if (write_w)
		oakAsm->MOV(to.Selem()[3], oak::util::WZR);
}

// OPMULA Opcode
static void mVU_OPMULA_reference_emit_oaknut(mP)
{
	const u32 original_code = mVU.code;
	mVU.code = (mVU.code & ~(0xfu << 21)) | (0xeu << 21);

	const int Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 32, _X_Y_Z_W);

	recBeginOaknutEmit();
	mVUUpperPshufd_oaknut(Fs, Fs, 0xC9);
	mVUUpperPshufd_oaknut(Ft, Ft, 0xD2);
	mVUUpperMulPs_oaknut(mVU, Fs, Ft);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Ft);
	mVUupdateFlags_oaknut(mVU, Fs);
	mVU.regAlloc->clearNeededXmmId(Fs);

	mVU.code = original_code;
}

static void mVU_OPMULA_direct_emit_oaknut(mP)
{
	if (!mVUlocalRuleEnabled(10))
	{
		mVU_OPMULA_reference_emit_oaknut(mVU, recPass);
		return;
	}
	const u32 original_code = mVU.code;
	mVU.code = (mVU.code & ~(0xfu << 21)) | (0xeu << 21);

	const int rawFs = mVU.regAlloc->allocRegId(_Fs_);
	const int rawFt = mVU.regAlloc->allocRegId(_Ft_);
	const int Ft = mVU.regAlloc->allocRegId();
	const int Fs = mVU.regAlloc->allocRegId(-1, 32, _X_Y_Z_W);

	recBeginOaknutEmit();
	mVUopPermuteFs_oaknut(Fs, rawFs, false);
	mVUopPermuteFt_oaknut(Ft, rawFt, false);
	mVUUpperMulPs_oaknut(mVU, Fs, Ft);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(rawFs);
	if (rawFt != rawFs) mVU.regAlloc->clearNeededXmmId(rawFt);
	mVU.regAlloc->clearNeededXmmId(Ft);
	mVUupdateFlags_oaknut(mVU, Fs);
	mVU.regAlloc->clearNeededXmmId(Fs);

	mVU.code = original_code;
}

static void mVU_OPMULA_emit(mP)
{
	pass1
	{
		const u32 original_code = mVU.code;
		mVU.code = (mVU.code & ~(0xfu << 21)) | (0xeu << 21);
		mVUanalyzeFMAC1(mVU, 0, _Fs_, _Ft_);
		mVU.code = original_code;
	}
	pass2 { mVU_OPMULA_direct_emit_oaknut(mVU, recPass); }
	pass3
	{
		mVUlog("OPMULA");
		mVUlogACC();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}


// OPMSUB Opcode
static void mVU_OPMSUB_reference_emit_oaknut(mP)
{
	const u32 original_code = mVU.code;
	mVU.code = (mVU.code & ~(0xfu << 21)) | (0xeu << 21);

	const int Ft = mVU.regAlloc->allocRegId(_Ft_, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	mVUUpperPshufd_oaknut(Fs, Fs, 0xC9);
	mVUUpperPshufd_oaknut(Ft, Ft, 0xD2);
	mVUUpperMulPs_oaknut(mVU, Fs, Ft);
	mVUUpperSubPs_oaknut(mVU, ACC, Fs);
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
	mVUupdateFlags_oaknut(mVU, ACC);
	mVU.regAlloc->clearNeededXmmId(ACC);

	mVU.code = original_code;
}

static void mVU_OPMSUB_direct_emit_oaknut(mP)
{
	if (!mVUlocalRuleEnabled(10))
	{
		mVU_OPMSUB_reference_emit_oaknut(mVU, recPass);
		return;
	}
	const u32 original_code = mVU.code;
	mVU.code = (mVU.code & ~(0xfu << 21)) | (0xeu << 21);

	const int rawFs = mVU.regAlloc->allocRegId(_Fs_);
	const int rawFt = mVU.regAlloc->allocRegId(_Ft_);
	const int Ft = mVU.regAlloc->allocRegId();
	const int Fs = mVU.regAlloc->allocRegId(-1, 0, _X_Y_Z_W);
	const int ACC = mVU.regAlloc->allocRegId(32, _Fd_, _X_Y_Z_W);

	recBeginOaknutEmit();
	mVUopPermuteFs_oaknut(Fs, rawFs, false);
	mVUopPermuteFt_oaknut(Ft, rawFt, false);
	mVUUpperMulPs_oaknut(mVU, Fs, Ft);
	mVUUpperSubPs_oaknut(mVU, ACC, Fs);
	recEndOaknutEmit();

	mVUupdateFlags_oaknut(mVU, ACC);
	// Commit the writer before releasing cached readers. Fd may alias a dirty
	// full source; releasing that old mapping first would invalidate ACC's
	// new partial result instead of merging it into the whole cached vector.
	mVU.regAlloc->clearNeededXmmId(ACC);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(rawFs);
	if (rawFt != rawFs) mVU.regAlloc->clearNeededXmmId(rawFt);
	mVU.regAlloc->clearNeededXmmId(Ft);

	mVU.code = original_code;
}

static void mVU_OPMSUB_emit(mP)
{
	pass1
	{
		const u32 original_code = mVU.code;
		mVU.code = (mVU.code & ~(0xfu << 21)) | (0xeu << 21);
		mVUanalyzeFMAC1(mVU, _Fd_, _Fs_, _Ft_);
		mVU.code = original_code;
	}
	pass2
	{
		mVU_OPMSUB_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("OPMSUB");
		mVUlogFd();
		mVUlogFt();
	}
	pass4 { mVUregs.needExactMatch |= 8; }
}


// FTOI0 Opcode
static bool mVUTrySharedFTOI(mV, u32 fractional_bits)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU) || !mVUSharedIrEnabled(mVU.index)) return false;
	static thread_local ir::Function fn;
	if (fn.blocks.empty())
	{
		ir::Builder init(fn);
		init.CreateBlock(0);
		fn.blocks[0].insts.reserve(4);
		fn.value_types.reserve(3);
	}
	fn.blocks[0].insts.clear();
	fn.blocks[0].guest_start = mVU.prog.IRinfo.curPC * sizeof(u32);
	fn.value_types.resize(1);
	fn.next_value = 1;
	ir::Builder b(fn);
	b.SetBlock(1);
	b.SetDelaySlot(mVUinfo.isBdelay);
	const u32 source = b.Emit(ir::Op::ReadVf, ir::Type::V4F32, {}, _Fs_);
	ir::Op convert_op = ir::Op::VF2I0;
	switch (fractional_bits)
	{
		case 0: convert_op = ir::Op::VF2I0; break;
		case 4: convert_op = ir::Op::VF2I4; break;
		case 12: convert_op = ir::Op::VF2I12; break;
		case 15: convert_op = ir::Op::VF2I15; break;
		default: return false;
	}
	const u32 converted = b.Emit1(convert_op, ir::Type::V4I32, source);
	b.Emit1(ir::Op::WriteVfBitsMasked, ir::Type::Void, converted, _Ft_, _X_Y_Z_W);
	b.Return();
	Arm64Ir::BoundVfToIntPlan plan;
	std::string error;
	if (!Arm64Ir::PlanBoundVfToInt(fn, &plan, &error)) return false;
	// Bind the whole source before the masked destination. The bound lowerer
	// handles lane rotation and packed S0 results for MicroVuPartial.
	const int source_reg = mVU.regAlloc->allocRegId(plan.src_vf);
	const int destination_reg = mVU.regAlloc->allocRegId(-1, plan.dst_vf, plan.lane_mask);
	Arm64Ir::LowerOutput out;
	const bool lowered = Arm64Ir::LowerBoundVfToIntPlan(plan, source_reg, destination_reg,
		&out, &error, Arm64Ir::BoundVfConvertLayout::MicroVuPartial);
	pxAssertRel(lowered, error.c_str());
	mVU.regAlloc->clearNeededXmmId(destination_reg);
	mVU.regAlloc->clearNeededXmmId(source_reg);
	mVURecordSharedIr(mVU.index, out.host_size);
	mVURecordSharedFtoi(mVU.index, out.host_size);
	return true;
}

static void mVU_FTOI_saturate_oaknut(int Fs)
{
	const oak::QReg fs_q = oakQRegister(Fs);
	// FCVTZS already saturates every finite overflow and infinity. Cap NaN
	// bit patterns to signed finite maxima first so their original sign also
	// selects INT_MIN/INT_MAX, rather than ARM's NaN-to-zero conversion.
	mVULoadClampLimits_oaknut(OAK_QSCRATCH3, OAK_QSCRATCH2);
	oakAsm->SMIN(fs_q.S4(), fs_q.S4(), OAK_QSCRATCH2.S4());
	oakAsm->UMIN(fs_q.S4(), fs_q.S4(), OAK_QSCRATCH3.S4());
	oakAsm->FCVTZS(fs_q.S4(), fs_q.S4());
}

static void mVU_FTOI0_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	mVU_FTOI_saturate_oaknut(Fs);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_FTOI0_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedFTOI(mVU, 0))
			mVU_FTOI0_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("FTOI0");
		mVUlogFtFs();
	}
}

// FTOI4 Opcode
static void mVU_FTOI4_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	oakLoad128(OAK_QSCRATCH3, mVUUpperOakGlobMem(offsetof(mVU_Globals, FTOI_4)));
	oakAsm->FMUL(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), OAK_QSCRATCH3.S4());
	mVU_FTOI_saturate_oaknut(Fs);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_FTOI4_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedFTOI(mVU, 4))
			mVU_FTOI4_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("FTOI4");
		mVUlogFtFs();
	}
}

// FTOI12 Opcode
static void mVU_FTOI12_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	oakLoad128(OAK_QSCRATCH3, mVUUpperOakGlobMem(offsetof(mVU_Globals, FTOI_12)));
	oakAsm->FMUL(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), OAK_QSCRATCH3.S4());
	mVU_FTOI_saturate_oaknut(Fs);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_FTOI12_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedFTOI(mVU, 12))
			mVU_FTOI12_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("FTOI12");
		mVUlogFtFs();
	}
}

// FTOI15 Opcode
static void mVU_FTOI15_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	oakLoad128(OAK_QSCRATCH3, mVUUpperOakGlobMem(offsetof(mVU_Globals, FTOI_15)));
	oakAsm->FMUL(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), OAK_QSCRATCH3.S4());
	mVU_FTOI_saturate_oaknut(Fs);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_FTOI15_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedFTOI(mVU, 15))
			mVU_FTOI15_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("FTOI15");
		mVUlogFtFs();
	}
}

// ITOF0 Opcode
static bool mVUTrySharedITOF(mV, u32 fractional_bits)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU) || !mVUSharedIrEnabled(mVU.index)) return false;
	static thread_local ir::Function fn;
	if (fn.blocks.empty())
	{
		ir::Builder init(fn);
		init.CreateBlock(0);
		fn.blocks[0].insts.reserve(4);
		fn.value_types.reserve(3);
	}
	fn.blocks[0].insts.clear();
	fn.blocks[0].guest_start = mVU.prog.IRinfo.curPC * sizeof(u32);
	fn.value_types.resize(1);
	fn.next_value = 1;
	ir::Builder b(fn);
	b.SetBlock(1);
	b.SetDelaySlot(mVUinfo.isBdelay);
	const u32 source = b.Emit(ir::Op::ReadVfBits, ir::Type::V4I32, {}, _Fs_);
	ir::Op convert_op = ir::Op::VI2F0;
	switch (fractional_bits)
	{
		case 0: convert_op = ir::Op::VI2F0; break;
		case 4: convert_op = ir::Op::VI2F4; break;
		case 12: convert_op = ir::Op::VI2F12; break;
		case 15: convert_op = ir::Op::VI2F15; break;
		default: return false;
	}
	const u32 converted = b.Emit1(convert_op, ir::Type::V4F32, source);
	b.Emit1(ir::Op::WriteVfMasked, ir::Type::Void, converted, _Ft_, _X_Y_Z_W);
	b.Return();
	Arm64Ir::BoundVfConvertPlan plan;
	std::string error;
	if (!Arm64Ir::PlanBoundVfConvert(fn, &plan, &error)) return false;
	// Match the upper allocator: read the complete raw source before acquiring
	// the masked destination; MicroVuPartial extracts the selected lane once.
	const int source_reg = mVU.regAlloc->allocRegId(plan.src_vf);
	const int destination_reg = mVU.regAlloc->allocRegId(-1, plan.dst_vf, plan.lane_mask);
	Arm64Ir::LowerOutput out;
	const bool lowered = Arm64Ir::LowerBoundVfConvertPlan(plan, source_reg, destination_reg,
		&out, &error, Arm64Ir::BoundVfConvertLayout::MicroVuPartial);
	pxAssertRel(lowered, error.c_str());
	mVU.regAlloc->clearNeededXmmId(destination_reg);
	mVU.regAlloc->clearNeededXmmId(source_reg);
	mVURecordSharedIr(mVU.index, out.host_size);
	mVURecordSharedItof(mVU.index, out.host_size);
	return true;
}

static void mVU_ITOF0_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	oakAsm->SCVTF(oakQRegister(Fs).S4(), oakQRegister(Fs).S4());
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_ITOF0_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedITOF(mVU, 0))
			mVU_ITOF0_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("ITOF0");
		mVUlogFtFs();
	}
}

// ITOF4 Opcode
static void mVU_ITOF4_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	oakAsm->SCVTF(oakQRegister(Fs).S4(), oakQRegister(Fs).S4());
	oakLoad128(OAK_QSCRATCH3, mVUUpperOakGlobMem(offsetof(mVU_Globals, ITOF_4)));
	oakAsm->FMUL(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), OAK_QSCRATCH3.S4());
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_ITOF4_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedITOF(mVU, 4))
			mVU_ITOF4_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("ITOF4");
		mVUlogFtFs();
	}
}

// ITOF12 Opcode
static void mVU_ITOF12_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	oakAsm->SCVTF(oakQRegister(Fs).S4(), oakQRegister(Fs).S4());
	oakLoad128(OAK_QSCRATCH3, mVUUpperOakGlobMem(offsetof(mVU_Globals, ITOF_12)));
	oakAsm->FMUL(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), OAK_QSCRATCH3.S4());
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_ITOF12_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedITOF(mVU, 12))
			mVU_ITOF12_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("ITOF12");
		mVUlogFtFs();
	}
}

// ITOF15 Opcode
static void mVU_ITOF15_direct_emit_oaknut(mP)
{
	if (!_Ft_ || mVUtrySkipNoLaneWrite_oaknut(mVU))
		return;
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W, !((_Fs_ == _Ft_) && (_X_Y_Z_W == 0xf)));
	recBeginOaknutEmit();
	oakAsm->SCVTF(oakQRegister(Fs).S4(), oakQRegister(Fs).S4());
	oakLoad128(OAK_QSCRATCH3, mVUUpperOakGlobMem(offsetof(mVU_Globals, ITOF_15)));
	oakAsm->FMUL(oakQRegister(Fs).S4(), oakQRegister(Fs).S4(), OAK_QSCRATCH3.S4());
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_ITOF15_emit(mP)
{
	pass1 { mVUanalyzeFMAC2(mVU, _Fs_, _Ft_); }
	pass2
	{
		if (!mVUTrySharedITOF(mVU, 15))
			mVU_ITOF15_direct_emit_oaknut(mVU, recPass);
	}
	pass3
	{
		mVUlog("ITOF15");
		mVUlogFtFs();
	}
}

// NOP Opcode
static void mVU_NOP_emit(mP)
{
	pass3 { mVUlog("NOP"); }
}

//------------------------------------------------------------------
// Micro VU Micromode Upper instructions
//------------------------------------------------------------------
