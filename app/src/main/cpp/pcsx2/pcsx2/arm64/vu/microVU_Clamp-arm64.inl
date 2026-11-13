// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

//------------------------------------------------------------------
// Micro VU - ARM64 Oaknut Clamp Functions
//------------------------------------------------------------------

static __fi OakMemOperand mVUClampOakSs4Mem(s64 offset)
{
	return mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, mVUss4)) + offset);
}

static __fi void mVUUpperClamp1VectorIf_oaknut(mV, int reg, bool bClampE, bool canClamp, bool limits_ready = false)
{
	if (((!clampE && CHECK_VU_OVERFLOW(mVU.index)) || (clampE && bClampE)) && canClamp)
	{
		mVUClamp1VectorFast_oaknut(reg, limits_ready);
	}
}

static __fi void mVUUpperClamp2VectorIf_oaknut(mV, int reg, bool bClampE, bool canClamp, bool limits_ready = false)
{
	// x86 mVUclamp2 parity: only the preserve-sign mode uses the bitwise
	// integer clamp. Every other mode falls through to mVUclamp1's
	// FMINNM/FMAXNM clamp, which folds NaNs to +max instead of keeping the
	// sign. Modes 1 and 2 therefore match the upstream fast path, while the
	// interpreter's sign-preserving conversion stays reachable through mode 3.
	const bool signClamp = ((!clampE && CHECK_VU_SIGN_OVERFLOW(mVU.index)) ||
		(clampE && bClampE && CHECK_VU_SIGN_OVERFLOW(mVU.index))) &&
		canClamp;
	if (signClamp)
		mVUClamp1VectorBits_oaknut(reg, limits_ready);
	else
		mVUUpperClamp1VectorIf_oaknut(mVU, reg, bClampE, canClamp, limits_ready);
}

static __fi void mVUUpperClamp2Vector_oaknut(mV, int reg, bool bClampE)
{
	mVUUpperClamp2VectorIf_oaknut(mVU, reg, bClampE, mVU.regAlloc->checkVFClamp(reg));
}

static __fi void mVUUpperClamp3Vector_oaknut(mV, int reg, bool limits_ready = false)
{
	const bool canClamp = mVU.regAlloc->checkVFClamp(reg);
	if (clampE && canClamp)
		mVUUpperClamp2VectorIf_oaknut(mVU, reg, true, true, limits_ready);
}

static __fi void mVUUpperClamp4Vector_oaknut(mV, int reg, bool limits_ready = false)
{
	const bool canClamp = mVU.regAlloc->checkVFClamp(reg);
	if (clampE && !CHECK_VU_SIGN_OVERFLOW(mVU.index) && canClamp)
		mVUUpperClamp1VectorIf_oaknut(mVU, reg, true, true, limits_ready);
}

static __fi void mVUUpperClamp1ScalarIf_oaknut(mV, int reg, bool bClampE, bool canClamp)
{
	if (((!clampE && CHECK_VU_OVERFLOW(mVU.index)) || (clampE && bClampE)) && canClamp)
	{
		mVUClamp1ScalarFast_oaknut(reg);
	}
}

static __fi void mVUUpperClamp2ScalarIf_oaknut(mV, int reg, bool bClampE, bool canClamp)
{
	// See mVUUpperClamp2VectorIf_oaknut: x86 mVUclamp2 parity, bitwise only in
	// preserve-sign mode and mVUclamp1 otherwise.
	const bool signClamp = ((!clampE && CHECK_VU_SIGN_OVERFLOW(mVU.index)) ||
		(clampE && bClampE && CHECK_VU_SIGN_OVERFLOW(mVU.index))) &&
		canClamp;
	if (signClamp)
	{
		// These masks intentionally preserve Y/Z/W. An adjacent LDP copy matched
		// 27,165,824 vectors, but showed no stable gain and was often slower.
		const oak::QReg reg_q = oakQRegister(reg);
		oakLoad128(OAK_QSCRATCH3, mVUClampOakSs4Mem(offsetof(mVU_SSE4, sse4_maxvals[0][0])));
		oakAsm->SMIN(reg_q.S4(), reg_q.S4(), OAK_QSCRATCH3.S4());
		oakLoad128(OAK_QSCRATCH3, mVUClampOakSs4Mem(offsetof(mVU_SSE4, sse4_minvals[0][0])));
		oakAsm->UMIN(reg_q.S4(), reg_q.S4(), OAK_QSCRATCH3.S4());
	}
	else
		mVUUpperClamp1ScalarIf_oaknut(mVU, reg, bClampE, canClamp);
}

static __fi void mVUUpperClamp2Scalar_oaknut(mV, int reg, bool bClampE)
{
	mVUUpperClamp2ScalarIf_oaknut(mVU, reg, bClampE, mVU.regAlloc->checkVFClamp(reg));
}

static __fi void mVUUpperClamp3Scalar_oaknut(mV, int reg)
{
	const bool canClamp = mVU.regAlloc->checkVFClamp(reg);
	if (clampE && canClamp)
		mVUUpperClamp2ScalarIf_oaknut(mVU, reg, true, true);
	else if (isVU0 && canClamp && !EmuConfig.Cpu.VU0FPCR.GetDenormalsAreZero())
		mVUClampDenormalScalarBits_oaknut(reg);
}

static __fi void mVUUpperClamp4Scalar_oaknut(mV, int reg)
{
	const bool canClamp = mVU.regAlloc->checkVFClamp(reg);
	if (clampE && !CHECK_VU_SIGN_OVERFLOW(mVU.index) && canClamp)
		mVUUpperClamp1ScalarIf_oaknut(mVU, reg, true, true);
	else if (isVU0 && canClamp && !EmuConfig.Cpu.VU0FPCR.GetDenormalsAreZero())
		mVUClampDenormalScalarBits_oaknut(reg);
}

//------------------------------------------------------------------
// Lower micromode clamp wrappers
//------------------------------------------------------------------

static __fi void mVU_clamp1ScalarIf_oaknut(mV, int reg, bool bClampE, bool canClamp)
{
	if (((!clampE && CHECK_VU_OVERFLOW(mVU.index)) || (clampE && bClampE)) && canClamp)
	{
		mVUClamp1ScalarFast_oaknut(reg);
	}
}

static __fi void mVU_clamp1Scalar_oaknut(mV, int reg, bool bClampE)
{
	mVU_clamp1ScalarIf_oaknut(mVU, reg, bClampE, mVU.regAlloc->checkVFClamp(reg));
}

static __fi void mVU_clamp1Vector_oaknut(mV, int reg, bool bClampE)
{
	if (((!clampE && CHECK_VU_OVERFLOW(mVU.index)) || (clampE && bClampE)) && mVU.regAlloc->checkVFClamp(reg))
	{
		mVUClamp1VectorFast_oaknut(reg);
	}
}

static __fi void mVU_clamp2ScalarIf_oaknut(mV, int reg, bool bClampE, bool canClamp)
{
	// Lower operand clamp: x86 mVUclamp2 parity, see the upper wrapper.
	const bool signClamp = ((!clampE && CHECK_VU_SIGN_OVERFLOW(mVU.index)) ||
		(clampE && bClampE && CHECK_VU_SIGN_OVERFLOW(mVU.index))) &&
		canClamp;
	if (signClamp)
		mVUClamp1ScalarBits_oaknut(reg);
	else
		mVU_clamp1ScalarIf_oaknut(mVU, reg, bClampE, canClamp);
}

static __fi void mVU_clamp3Scalar_oaknut(mV, int reg)
{
	const bool canClamp = mVU.regAlloc->checkVFClamp(reg);
	if (clampE && canClamp)
		mVU_clamp2ScalarIf_oaknut(mVU, reg, true, true);
}

static __fi void mVU_clamp4Scalar_oaknut(mV, int reg)
{
	const bool canClamp = mVU.regAlloc->checkVFClamp(reg);
	if (clampE && !CHECK_VU_SIGN_OVERFLOW(mVU.index) && canClamp)
		mVU_clamp1ScalarIf_oaknut(mVU, reg, true, true);
}



//------------------------------------------------------------------
// Opcode-specific clamp fixups
//------------------------------------------------------------------

static __fi void mVUUpperClampAccLaneFs_oaknut(mV, int reg, bool scalar)
{
	// x86 mVUclamp2(Fs) for MULAx/y/z, MADDAx/y/z/w: the numeric clamp at Normal
	// and above, or the sign-preserving bit clamp at Preserve Sign.
	if (scalar)
		mVUUpperClamp2Scalar_oaknut(mVU, reg, false);
	else
		mVUUpperClamp2Vector_oaknut(mVU, reg, false);
}
