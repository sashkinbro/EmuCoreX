// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

//------------------------------------------------------------------
// Micro VU Micromode Lower instructions
//------------------------------------------------------------------

//------------------------------------------------------------------
// DIV/SQRT/RSQRT
//------------------------------------------------------------------

static __fi OakMemOperand mVU_divFlag_oaknut(mV)
{
	return mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].divFlag)));
}

static __fi OakMemOperand mVU_glob_oaknut(s64 offset)
{
	return mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, mVUglob)) + offset);
}

static __fi OakMemOperand mVU_ss4_oaknut(s64 offset)
{
	return mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, mVUss4)) + offset);
}

static __fi void mVU_storeDivFlag_oaknut(mV, u32 value)
{
	// The large-offset store may use W16/W17 to form the address. Keep its
	// value in the reserved VU temp so callers can retain operands in both
	// scratch registers across the flag update.
	const oak::WReg flag = oakWRegister(VU_HOST_T1);
	oakAsm->MOV(flag, value);
	oakStore32(flag, mVU_divFlag_oaknut(mVU));
}

static __fi void mVU_testZero_oaknut(int xmmReg, int xmmTemp, int gprTemp)
{
	const oak::QReg temp_q = oakQRegister(xmmTemp);
	const oak::WReg temp_w = oakWRegister(gprTemp);

	oakAsm->EOR(temp_q.B16(), temp_q.B16(), temp_q.B16());
	oakAsm->FCMEQ(oakSRegister(xmmTemp), oakSRegister(xmmTemp), oakSRegister(xmmReg));
	oakAsm->FMOV(temp_w, oakSRegister(xmmTemp));
	oakAsm->CMP(temp_w, 0);
}

static __fi void mVU_prepareSqrtOperand_oaknut(mV, int xmmReg)
{
	const oak::QReg reg_q = oakQRegister(xmmReg);
	oak::Label no_invalid;
	oak::Label write_value;
	oak::Label write_zero;
	oak::Label done;

	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(xmmReg));
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH, 0x7fffffffu);

	// Interpreter path does ft = vuDouble(raw), then sqrt(fabs(ft)).
	// So -0 and negative denormals become +0 and must not raise I.
	oakAsm->TST(OAK_WSCRATCH, 0x80000000u);
	oakAsm->B(oak::util::EQ, no_invalid);
	oakAsm->CMP(OAK_WSCRATCH2, 0x00800000u);
	oakAsm->B(oak::util::LO, no_invalid);
	oakAsm->MOV(OAK_WSCRATCH, 0x7f800000u);
	oakAsm->CMP(OAK_WSCRATCH2, OAK_WSCRATCH);
	oakAsm->B(oak::util::HI, no_invalid);
	mVU_storeDivFlag_oaknut(mVU, divI);

	oakAsm->l(no_invalid);
	oakAsm->CMP(OAK_WSCRATCH2, 0x00800000u);
	oakAsm->B(oak::util::LO, write_zero);

	if (CHECK_VU_OVERFLOW(mVU.index))
	{
		oakAsm->MOV(OAK_WSCRATCH, 0x7f800000u);
		oakAsm->CMP(OAK_WSCRATCH2, OAK_WSCRATCH);
		oakAsm->B(oak::util::NE, write_value);
		oakAsm->MOV(OAK_WSCRATCH2, 0x7f7fffffu);
	}

	oakAsm->l(write_value);
	oakAsm->FMOV(oakSRegister(xmmReg), OAK_WSCRATCH2);
	oakAsm->B(done);

	oakAsm->l(write_zero);
	oakAsm->EOR(reg_q.B16(), reg_q.B16(), reg_q.B16());
	oakAsm->l(done);
}

static __fi void mVU_prepareMacroSqrtOperand_oaknut(mV, int xmmReg)
{
	oak::Label non_negative;
	oak::Label write_zero;
	oak::Label write_abs_without_flag;

	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(xmmReg));
	oakAsm->TST(OAK_WSCRATCH, 0x80000000u);
	oakAsm->B(oak::util::EQ, non_negative);
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH, 0x7fffffffu);
	// vuDouble() turns signed zero and denormals into signed zero before
	// SQRT, so they are not invalid even when the raw sign bit is set.
	oakAsm->CMP(OAK_WSCRATCH2, 0x00800000u);
	oakAsm->B(oak::util::LO, write_zero);
	if (!CHECK_VU_OVERFLOW(mVU.index))
	{
		// Without overflow clamping, negative NaNs remain NaNs and do not
		// satisfy the interpreter's ft < 0 comparison.
		oakAsm->MOV(OAK_WSCRATCH, 0x7f800000u);
		oakAsm->CMP(OAK_WSCRATCH2, OAK_WSCRATCH);
		oakAsm->B(oak::util::HI, write_abs_without_flag);
	}
	oakAsm->FMOV(oakSRegister(xmmReg), OAK_WSCRATCH2);
	// Store the absolute operand before mVU_storeDivFlag_oaknut() reuses
	// OAK_WSCRATCH for the flag value.
	mVU_storeDivFlag_oaknut(mVU, divI);
	oakAsm->B(non_negative);

	oakAsm->l(write_abs_without_flag);
	oakAsm->FMOV(oakSRegister(xmmReg), OAK_WSCRATCH2);
	oakAsm->B(non_negative);

	oakAsm->l(write_zero);
	oakAsm->EOR(oakQRegister(xmmReg).B16(), oakQRegister(xmmReg).B16(), oakQRegister(xmmReg).B16());
	oakAsm->l(non_negative);

	if (CHECK_VU_OVERFLOW(mVU.index))
	{
		oakAsm->MOV(OAK_WSCRATCH, 0x7f7fffffu);
		oakAsm->FMOV(OAK_SSCRATCH, OAK_WSCRATCH);
		oakAsm->FMINNM(oakSRegister(xmmReg), oakSRegister(xmmReg), OAK_SSCRATCH);
	}
}

static __fi void mVU_testVuZero_oaknut(int xmmReg)
{
	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(xmmReg));
	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x7f800000u);
	oakAsm->CMP(OAK_WSCRATCH, 0);
}

static __fi void mVU_makeRsqrtZeroSign_oaknut(int dst, int fs, int ft)
{
	const oak::QReg dst_q = oakQRegister(dst);
	oakAsm->EOR(dst_q.B16(), oakQRegister(fs).B16(), oakQRegister(ft).B16());
	mVUEmitSignbitVector_oaknut(OAK_QSCRATCH3);
	oakAsm->AND(dst_q.B16(), dst_q.B16(), OAK_QSCRATCH3.B16());
}

static __fi void mVU_makeRsqrtSignedMax_oaknut(int dst)
{
	mVUEmitMaxvalsVector_oaknut(OAK_QSCRATCH3);
	oakAsm->ORR(oakQRegister(dst).B16(), oakQRegister(dst).B16(), OAK_QSCRATCH3.B16());
}

static __fi void mVU_addScalar_oaknut(mV, int to, int from)
{
	mVU_clamp3Scalar_oaknut(mVU, to);
	mVU_clamp3Scalar_oaknut(mVU, from);
	oakAsm->FADD(oakSRegister(to), oakSRegister(to), oakSRegister(from));
	mVU_clamp4Scalar_oaknut(mVU, to);
}

static __fi void mVU_subScalar_oaknut(mV, int to, int from)
{
	mVU_clamp3Scalar_oaknut(mVU, to);
	mVU_clamp3Scalar_oaknut(mVU, from);
	oakAsm->FSUB(oakSRegister(to), oakSRegister(to), oakSRegister(from));
	mVU_clamp4Scalar_oaknut(mVU, to);
}

static __fi void mVU_mulScalar_oaknut(mV, int to, int from)
{
	mVU_clamp3Scalar_oaknut(mVU, to);
	mVU_clamp3Scalar_oaknut(mVU, from);
	oakAsm->FMUL(oakSRegister(to), oakSRegister(to), oakSRegister(from));
	mVU_clamp4Scalar_oaknut(mVU, to);
}

static __fi void mVU_divScalar_oaknut(mV, int to, int from)
{
	mVU_clamp3Scalar_oaknut(mVU, to);
	mVU_clamp3Scalar_oaknut(mVU, from);
	oakAsm->FDIV(oakSRegister(to), oakSRegister(to), oakSRegister(from));
	mVU_clamp4Scalar_oaknut(mVU, to);
}

static __fi void mVU_EFUdivScalarRaw_oaknut(int to, int from)
{
	oakAsm->FDIV(oakSRegister(to), oakSRegister(to), oakSRegister(from));
}

static __fi void mVU_mergeDivFlagToCop2Status_oaknut(mV)
{
	const oak::WReg status_w = oakWRegister(VU_HOST_F0);
	oakAsm->AND(status_w, status_w, ~0xc0000u);
	oakLoad32(OAK_WSCRATCH, mVU_divFlag_oaknut(mVU));
	oakAsm->ORR(status_w, status_w, OAK_WSCRATCH);
}

static void mVU_SQRT_direct_emit_oaknut(mP)
{
	const int Ft = mVU.regAlloc->allocRegId(_Ft_, 0, (1 << (3 - _Ftf_)));
	const bool host_denormals_enabled = !(mVU.index ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR).GetDenormalsAreZero();

	recBeginOaknutEmit();
	if (host_denormals_enabled)
		mVUClampDenormalScalarBits_oaknut(Ft);
	mVU_storeDivFlag_oaknut(mVU, 0);
	// Match legacy microVU VSQRT behavior: flag negative raw inputs, abs them, then clamp before FSQRT.
	mVU_prepareMacroSqrtOperand_oaknut(mVU, Ft);
	oakAsm->FSQRT(oakSRegister(Ft), oakSRegister(Ft));
	recEndOaknutEmit();

	writeQreg(Ft, mVUinfo.writeQ);

	if (mVU.cop2)
	{
		recBeginOaknutEmit();
		mVU_mergeDivFlagToCop2Status_oaknut(mVU);
		recEndOaknutEmit();
	}

	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_RSQRT_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	const int Ft = mVU.regAlloc->allocRegId(_Ft_, 0, (1 << (3 - _Ftf_)));
	const int t1 = mVU.regAlloc->allocRegId();
	const bool host_denormals_enabled = !(mVU.index ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR).GetDenormalsAreZero();

	recBeginOaknutEmit();
	if (host_denormals_enabled)
	{
		mVUClampDenormalScalarBits_oaknut(Fs);
		mVUClampDenormalScalarBits_oaknut(Ft);
	}
	if (CHECK_VU_OVERFLOW(mVU.index))
	{
		// vuDouble() converts exponent-255 operands to signed VU max before
		// RSQRT. Do this before the sign/zero paths so NaNs and infinities
		// follow the same arithmetic and flag behavior as the interpreter.
		mVUClamp1ScalarBits_oaknut(Fs);
		mVUClamp1ScalarBits_oaknut(Ft);
	}
	mVU_storeDivFlag_oaknut(mVU, 0);
	mVU_makeRsqrtZeroSign_oaknut(t1, Fs, Ft);
	mVU_prepareSqrtOperand_oaknut(mVU, Ft);
	oakAsm->FSQRT(oakSRegister(Ft), oakSRegister(Ft));
	mVU_testVuZero_oaknut(Ft);
	oak::Label normal_div;
	oakAsm->B(oak::util::NE, normal_div);

	mVU_storeDivFlag_oaknut(mVU, divD);
	mVU_testVuZero_oaknut(Fs);
	oak::Label signed_max;
	oakAsm->B(oak::util::NE, signed_max);
	mVU_storeDivFlag_oaknut(mVU, divD | divI);
	oakAsm->MOV(oakQRegister(Fs).Selem()[0], oakQRegister(t1).Selem()[0]);
	oak::Label done;
	oakAsm->B(done);

	oakAsm->l(signed_max);
	mVU_makeRsqrtSignedMax_oaknut(t1);
	oakAsm->MOV(oakQRegister(Fs).Selem()[0], oakQRegister(t1).Selem()[0]);
	oakAsm->B(done);

	oakAsm->l(normal_div);
	mVU_divScalar_oaknut(mVU, Fs, Ft);
	mVU_clamp1Scalar_oaknut(mVU, Fs, true);
	if (host_denormals_enabled)
		mVUClampDenormalScalarBits_oaknut(Fs);
	oakAsm->l(done);
	recEndOaknutEmit();

	writeQreg(Fs, mVUinfo.writeQ);

	if (mVU.cop2)
	{
		recBeginOaknutEmit();
		mVU_mergeDivFlagToCop2Status_oaknut(mVU);
		recEndOaknutEmit();
	}

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
	mVU.regAlloc->clearNeededXmmId(t1);
}

static void mVU_DIV_direct_emit_oaknut(mP)
{
	int Ft;
	if (_Ftf_)
		Ft = mVU.regAlloc->allocRegId(_Ft_, 0, (1 << (3 - _Ftf_)));
	else
		Ft = mVU.regAlloc->allocRegId(_Ft_);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	const int t1 = mVU.regAlloc->allocRegId();
	const bool host_denormals_enabled = !(mVU.index ? EmuConfig.Cpu.VU1FPCR : EmuConfig.Cpu.VU0FPCR).GetDenormalsAreZero();

	recBeginOaknutEmit();
	if (host_denormals_enabled)
	{
		mVUClampDenormalScalarBits_oaknut(Fs);
		mVUClampDenormalScalarBits_oaknut(Ft);
	}
	if (CHECK_VU_OVERFLOW(mVU.index))
	{
		// Match vuDouble() input conversion for the accurate clamp modes.
		// The bitwise path keeps the original sign of NaNs.
		mVUClamp1ScalarBits_oaknut(Fs);
		mVUClamp1ScalarBits_oaknut(Ft);
	}
	mVU_testZero_oaknut(Ft, t1, VU_HOST_T1);
	oak::Label cjmp;
	oakAsm->B(oak::util::EQ, cjmp);

	mVU_testZero_oaknut(Fs, t1, VU_HOST_T1);
	oak::Label ajmp;
	oakAsm->B(oak::util::EQ, ajmp);
	mVU_storeDivFlag_oaknut(mVU, divI);
	oak::Label bjmp;
	oakAsm->B(bjmp);
	oakAsm->l(ajmp);
	mVU_storeDivFlag_oaknut(mVU, divD);
	oakAsm->l(bjmp);

	oakAsm->EOR(oakQRegister(Fs).B16(), oakQRegister(Fs).B16(), oakQRegister(Ft).B16());
	mVUEmitSignbitVector_oaknut(OAK_QSCRATCH3);
	oakAsm->AND(oakQRegister(Fs).B16(), oakQRegister(Fs).B16(), OAK_QSCRATCH3.B16());
	mVUEmitMaxvalsVector_oaknut(OAK_QSCRATCH3);
	oakAsm->ORR(oakQRegister(Fs).B16(), oakQRegister(Fs).B16(), OAK_QSCRATCH3.B16());

	oak::Label djmp;
	oakAsm->B(djmp);
	oakAsm->l(cjmp);
	mVU_storeDivFlag_oaknut(mVU, 0);
	mVU_divScalar_oaknut(mVU, Fs, Ft);
	mVU_clamp1Scalar_oaknut(mVU, Fs, true);
	if (host_denormals_enabled)
		mVUClampDenormalScalarBits_oaknut(Fs);
	oakAsm->l(djmp);
	recEndOaknutEmit();

	writeQreg(Fs, mVUinfo.writeQ);

	if (mVU.cop2)
	{
		recBeginOaknutEmit();
		mVU_mergeDivFlagToCop2Status_oaknut(mVU);
		recEndOaknutEmit();
	}

	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(Ft);
	mVU.regAlloc->clearNeededXmmId(t1);
}

static void mVU_DIV_emit(mP)
{
	pass1 { mVUanalyzeFDIV(mVU, _Fs_, _Fsf_, _Ft_, _Ftf_, 7); }
	pass2
	{
		mVU_DIV_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("DIV Q, vf%02d%s, vf%02d%s", _Fs_, _Fsf_String, _Ft_, _Ftf_String); }
}

static void mVU_SQRT_emit(mP)
{
	pass1 { mVUanalyzeFDIV(mVU, 0, 0, _Ft_, _Ftf_, 7); }
	pass2
	{
		mVU_SQRT_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("SQRT Q, vf%02d%s", _Ft_, _Ftf_String); }
}

static void mVU_RSQRT_emit(mP)
{
	pass1 { mVUanalyzeFDIV(mVU, _Fs_, _Fsf_, _Ft_, _Ftf_, 13); }
	pass2
	{
		mVU_RSQRT_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("RSQRT Q, vf%02d%s, vf%02d%s", _Fs_, _Fsf_String, _Ft_, _Ftf_String); }
}


//------------------------------------------------------------------
// EATAN/EEXP/ELENG/ERCPR/ERLENG/ERSADD/ERSQRT/ESADD/ESIN/ESQRT/ESUM
//------------------------------------------------------------------

static __fi void mVU_pshufd_oaknut(int dst, int src, u8 imm)
{
	const oak::QReg dst_q = oakQRegister(dst);
	const oak::QReg src_q = oakQRegister(src);
	oakLoad128(OAK_QSCRATCH3,
		mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, shuffle.data[imm][0]))));
	oakAsm->MOV(OAK_QSCRATCH2.B16(), src_q.B16());
	oakAsm->TBL(dst_q.B16(), oak::List(OAK_QSCRATCH2.B16()), OAK_QSCRATCH3.B16());
}

static __fi void mVU_flipPQ_oaknut(mV)
{
	mVU_pshufd_oaknut(VU_HOST_XMMPQ, VU_HOST_XMMPQ, mVUinfo.writeP ? 0x27 : 0xC6);
}

static __fi void mVU_EFUvuDoubleSS_oaknut(int reg)
{
	oak::Label done;
	oak::Label check_overflow;

	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(reg));
	oakAsm->MOV(OAK_WSCRATCH2, 0x7f800000);
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->CBNZ(OAK_WSCRATCH2, check_overflow);

	oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x80000000);
	oakAsm->FMOV(oakSRegister(reg), OAK_WSCRATCH);
	oakAsm->B(done);

	oakAsm->l(check_overflow);
	if (CHECK_VU_OVERFLOW(0))
	{
		oakAsm->MOV(OAK_WSCRATCH, 0x7f800000);
		oakAsm->CMP(OAK_WSCRATCH2, OAK_WSCRATCH);
		oakAsm->B(oak::util::NE, done);
		oakAsm->FMOV(OAK_WSCRATCH2, oakSRegister(reg));
		oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x80000000);
		oakAsm->MOV(OAK_WSCRATCH, 0x7f7fffff);
		oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, OAK_WSCRATCH);
		oakAsm->FMOV(oakSRegister(reg), OAK_WSCRATCH2);
	}

	oakAsm->l(done);
}

static __fi void mVU_EFUvuDoublePS_oaknut(int reg, int t1, int t2)
{
	mVUClampVuDoubleVectorBits_oaknut(reg, t1, t2, CHECK_VU_OVERFLOW(0));
}

static __fi void mVU_EFUreciprocalOrZero_oaknut(mV, int PQ, int one, int t1)
{
	const oak::QReg one_q = oakQRegister(one);
	const oak::QReg temp_q = oakQRegister(t1);
	oak::Label done;

	oakAsm->EOR(temp_q.B16(), temp_q.B16(), temp_q.B16());
	oakAsm->FCMEQ(oakSRegister(t1), oakSRegister(t1), oakSRegister(PQ));
	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(t1));
	oakAsm->CBNZ(OAK_WSCRATCH, done);

	oakAsm->EOR(one_q.B16(), one_q.B16(), one_q.B16());
	oakLoad32(OAK_WSCRATCH, mVU_glob_oaknut(offsetof(mVU_Globals, one)));
	oakAsm->FMOV(oakSRegister(one), OAK_WSCRATCH);
	mVU_EFUdivScalarRaw_oaknut(one, PQ);
	oakAsm->MOV(oakQRegister(PQ).Selem()[0], one_q.Selem()[0]);

	oakAsm->l(done);
}

static __fi void mVU_EFUsqrtNonnegative_oaknut(int PQ, int Fs)
{
	oak::Label negative;
	oak::Label done;

	oakAsm->FCMP(oakSRegister(Fs), 0.0);
	oakAsm->B(oak::util::LT, negative);
	oakAsm->FSQRT(oakSRegister(PQ), oakSRegister(Fs));
	oakAsm->B(done);

	oakAsm->l(negative);
	oakAsm->MOV(oakQRegister(PQ).Selem()[0], oakQRegister(Fs).Selem()[0]);

	oakAsm->l(done);
}

static __fi void mVU_EFUsqrtNonnegativeDouble_oaknut(int PQ)
{
	oak::Label negative;
	oak::Label done;

	oakAsm->FCMP(oakSRegister(PQ), 0.0);
	oakAsm->B(oak::util::VS, negative);
	oakAsm->B(oak::util::LT, negative);
	oakAsm->FCVT(OAK_DSCRATCH, oakSRegister(PQ));
	oakAsm->FSQRT(OAK_DSCRATCH, OAK_DSCRATCH);
	oakAsm->FCVT(OAK_SSCRATCH, OAK_DSCRATCH);
	oakAsm->MOV(oakQRegister(PQ).Selem()[0], OAK_QSCRATCH.Selem()[0]);
	oakAsm->B(done);

	oakAsm->l(negative);
	oakAsm->l(done);
}

static __fi void mVU_EFUrsqrtNonnegativeOrZero_oaknut(mV, int PQ, int Fs, int t1)
{
	oak::Label negative;
	oak::Label done;

	oakAsm->FCMP(oakSRegister(Fs), 0.0);
	oakAsm->B(oak::util::LT, negative);
	oakAsm->FSQRT(oakSRegister(PQ), oakSRegister(Fs));
	mVU_EFUreciprocalOrZero_oaknut(mVU, PQ, Fs, t1);
	oakAsm->B(done);

	oakAsm->l(negative);
	oakAsm->MOV(oakQRegister(PQ).Selem()[0], oakQRegister(Fs).Selem()[0]);

	oakAsm->l(done);
}

static __fi void mVU_EFUrsqrtNonnegativeOrZeroDouble_oaknut(mV, int PQ, int one, int t1)
{
	oak::Label negative;
	oak::Label done;

	oakAsm->FCMP(oakSRegister(PQ), 0.0);
	oakAsm->B(oak::util::VS, negative);
	oakAsm->B(oak::util::LT, negative);
	oakAsm->FCVT(OAK_DSCRATCH, oakSRegister(PQ));
	oakAsm->FSQRT(OAK_DSCRATCH, OAK_DSCRATCH);
	oakAsm->FCVT(OAK_SSCRATCH, OAK_DSCRATCH);
	oakAsm->MOV(oakQRegister(PQ).Selem()[0], OAK_QSCRATCH.Selem()[0]);
	mVU_EFUreciprocalOrZero_oaknut(mVU, PQ, one, t1);
	oakAsm->B(done);

	oakAsm->l(negative);
	oakAsm->l(done);
}

static __fi void mVU_sumXYZ_oaknut(int PQ, int Fs)
{
	constexpr int scratch = VU_HOST_XMMSCRATCH;
	const oak::SReg scratch_s = oakSRegister(scratch);
	oakAsm->MOV(oakQRegister(PQ).Selem()[0], oakQRegister(Fs).Selem()[0]);
	oakAsm->FMUL(oakSRegister(PQ), oakSRegister(PQ), oakSRegister(PQ));
	mVU_pshufd_oaknut(scratch, Fs, 0x01);
	oakAsm->FMUL(scratch_s, scratch_s, scratch_s);
	oakAsm->FADD(oakSRegister(PQ), oakSRegister(PQ), scratch_s);
	mVU_pshufd_oaknut(scratch, Fs, 0x02);
	oakAsm->FMUL(scratch_s, scratch_s, scratch_s);
	oakAsm->FADD(oakSRegister(PQ), oakSRegister(PQ), scratch_s);
}

static __fi void mVU_loadGlobSS_oaknut(int reg, s64 offset, bool clearReg = false)
{
	// LDR S looks shorter, but the canonical four-lane test found it 4-15%
	// slower for isolated loads and no robust gain across the 8-term EATAN chain.
	if (clearReg)
		oakAsm->EOR(oakQRegister(reg).B16(), oakQRegister(reg).B16(), oakQRegister(reg).B16());
	oakLoad32(OAK_WSCRATCH, mVU_glob_oaknut(offset));
	oakAsm->FMOV(oakSRegister(reg), OAK_WSCRATCH);
}

static __fi void mVU_mulGlobSS_oaknut(int reg, s64 offset)
{
	oakLoad32(OAK_WSCRATCH, mVU_glob_oaknut(offset));
	oakAsm->FMOV(OAK_SSCRATCH3, OAK_WSCRATCH);
	oakAsm->FMUL(oakSRegister(reg), oakSRegister(reg), OAK_SSCRATCH3);
}

static __fi void mVU_addGlobSS_oaknut(int reg, s64 offset)
{
	oakLoad32(OAK_WSCRATCH, mVU_glob_oaknut(offset));
	oakAsm->FMOV(OAK_SSCRATCH3, OAK_WSCRATCH);
	oakAsm->FADD(oakSRegister(reg), oakSRegister(reg), OAK_SSCRATCH3);
}

static __fi void mVU_EATANHelper_oaknut(mV, int PQ, int input2, int inputpow, s64 offset)
{
	mVU_mulScalar_oaknut(mVU, inputpow, input2);
	oakLoad32(OAK_WSCRATCH, mVU_glob_oaknut(offset));
	oakAsm->FMOV(OAK_SSCRATCH3, OAK_WSCRATCH);
	oakAsm->FMADD(oakSRegister(PQ), oakSRegister(inputpow), OAK_SSCRATCH3, oakSRegister(PQ));
}

static __fi void mVU_EATAN_oaknut(mV, int PQ, int Fs, int t1, int inputpow, int input2)
{
	oak::Label calculate;
	oak::Label finish;
	oak::Label not_pi2;

	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(Fs));
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH, 0x7fffffffu);
	oakAsm->MOV(OAK_WSCRATCH, 0x7f800000u);
	oakAsm->CMP(OAK_WSCRATCH2, OAK_WSCRATCH);
	oakAsm->B(oak::util::LO, not_pi2);
	oakAsm->MOV(OAK_WSCRATCH, 0x7fc00000u);
	oakAsm->FMOV(oakSRegister(PQ), OAK_WSCRATCH);
	oakAsm->B(finish);

	oakAsm->l(not_pi2);
	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(Fs));
	oakAsm->TST(OAK_WSCRATCH, 0x80000000u);
	oakAsm->B(oak::util::NE, calculate);
	oakAsm->MOV(OAK_WSCRATCH2, 0x3f7ffffeu);
	oakAsm->CMP(OAK_WSCRATCH, OAK_WSCRATCH2);
	oakAsm->B(oak::util::LO, calculate);
	oakAsm->MOV(OAK_WSCRATCH, 0x3fc90fd9u);
	oakAsm->FMOV(oakSRegister(PQ), OAK_WSCRATCH);
	oakAsm->B(finish);

	oakAsm->l(calculate);
	oakAsm->MOV(oakQRegister(input2).B16(), oakQRegister(Fs).B16());
	mVU_mulScalar_oaknut(mVU, input2, Fs);
	oakAsm->MOV(oakQRegister(inputpow).B16(), oakQRegister(input2).B16());
	mVU_mulScalar_oaknut(mVU, inputpow, Fs);
	oakAsm->MOV(oakQRegister(t1).B16(), oakQRegister(inputpow).B16());
	mVU_mulGlobSS_oaknut(t1, offsetof(mVU_Globals, T5));
	oakLoad32(OAK_WSCRATCH, mVU_glob_oaknut(offsetof(mVU_Globals, T1)));
	oakAsm->FMOV(OAK_SSCRATCH3, OAK_WSCRATCH);
	oakAsm->FMADD(oakSRegister(PQ), oakSRegister(Fs), OAK_SSCRATCH3, oakSRegister(t1));
	mVU_EATANHelper_oaknut(mVU, PQ, input2, inputpow, offsetof(mVU_Globals, T2));
	mVU_EATANHelper_oaknut(mVU, PQ, input2, inputpow, offsetof(mVU_Globals, T3));
	mVU_EATANHelper_oaknut(mVU, PQ, input2, inputpow, offsetof(mVU_Globals, T4));
	mVU_EATANHelper_oaknut(mVU, PQ, input2, inputpow, offsetof(mVU_Globals, T6));
	mVU_EATANHelper_oaknut(mVU, PQ, input2, inputpow, offsetof(mVU_Globals, T7));
	mVU_EATANHelper_oaknut(mVU, PQ, input2, inputpow, offsetof(mVU_Globals, T8));
	mVU_addGlobSS_oaknut(PQ, offsetof(mVU_Globals, Pi4));
	oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(PQ));
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH, 0x7fffffffu);
	oakAsm->MOV(OAK_WSCRATCH, 0x7f800000u);
	oakAsm->CMP(OAK_WSCRATCH2, OAK_WSCRATCH);
	oakAsm->B(oak::util::LO, finish);
	oakAsm->MOV(OAK_WSCRATCH, 0x7fc00000u);
	oakAsm->FMOV(oakSRegister(PQ), OAK_WSCRATCH);

	oakAsm->l(finish);
}

static void mVU_EATAN_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	const int t1 = mVU.regAlloc->allocRegId();
	const int t2 = mVU.regAlloc->allocRegId();
	const int t3 = mVU.regAlloc->allocRegId();
	const int P = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVU_EFUvuDoubleSS_oaknut(Fs);
	oakAsm->MOV(oakQRegister(P).Selem()[0], oakQRegister(Fs).Selem()[0]);
	mVU_loadGlobSS_oaknut(t1, offsetof(mVU_Globals, one));
	oakAsm->FSUB(oakSRegister(Fs), oakSRegister(Fs), oakSRegister(t1));
	oakAsm->FADD(oakSRegister(P), oakSRegister(P), oakSRegister(t1));
	mVU_EFUdivScalarRaw_oaknut(Fs, P);
	mVU_EATAN_oaknut(mVU, P, Fs, t1, t2, t3);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(P).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(t2);
	mVU.regAlloc->clearNeededXmmId(t3);
	mVU.regAlloc->clearNeededXmmId(P);
}

static void mVU_EATAN_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU1(mVU, _Fs_, _Fsf_, 54);
	}
	pass2
	{
		mVU_EATAN_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("EATAN P"); }
}


static void mVU_EATANxy_direct_emit_oaknut(mP)
{
	const int t1 = mVU.regAlloc->allocRegId(_Fs_, 0, 0xf);
	const int Fs = mVU.regAlloc->allocRegId();
	const int t2 = mVU.regAlloc->allocRegId();
	const int t3 = mVU.regAlloc->allocRegId();
	const int P = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVU_EFUvuDoublePS_oaknut(t1, Fs, t2);
	mVU_pshufd_oaknut(Fs, t1, 0x01);
	oakAsm->MOV(oakQRegister(P).Selem()[0], oakQRegister(Fs).Selem()[0]);
	mVU_subScalar_oaknut(mVU, Fs, t1);
	mVU_addScalar_oaknut(mVU, t1, P);
	mVU_EFUdivScalarRaw_oaknut(Fs, t1);
	mVU_EATAN_oaknut(mVU, P, Fs, t1, t2, t3);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(P).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(t2);
	mVU.regAlloc->clearNeededXmmId(t3);
	mVU.regAlloc->clearNeededXmmId(P);
}

static void mVU_EATANxy_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU2(mVU, _Fs_, 54);
	}
	pass2
	{
		mVU_EATANxy_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("EATANxy P"); }
}


static void mVU_EATANxz_direct_emit_oaknut(mP)
{
	const int t1 = mVU.regAlloc->allocRegId(_Fs_, 0, 0xf);
	const int Fs = mVU.regAlloc->allocRegId();
	const int t2 = mVU.regAlloc->allocRegId();
	const int t3 = mVU.regAlloc->allocRegId();
	const int P = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVU_EFUvuDoublePS_oaknut(t1, Fs, t2);
	mVU_pshufd_oaknut(Fs, t1, 0x02);
	oakAsm->MOV(oakQRegister(P).Selem()[0], oakQRegister(Fs).Selem()[0]);
	mVU_subScalar_oaknut(mVU, Fs, t1);
	mVU_addScalar_oaknut(mVU, t1, P);
	mVU_EFUdivScalarRaw_oaknut(Fs, t1);
	mVU_EATAN_oaknut(mVU, P, Fs, t1, t2, t3);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(P).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(t2);
	mVU.regAlloc->clearNeededXmmId(t3);
	mVU.regAlloc->clearNeededXmmId(P);
}

static void mVU_EATANxz_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU2(mVU, _Fs_, 54);
	}
	pass2
	{
		mVU_EATANxz_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("EATANxz P"); }
}


static __fi void mVU_eexpHelperD_oaknut(int P, int t1, int t2, int Fs, s64 offset)
{
	oakAsm->FMUL(oakDRegister(t2), oakDRegister(t2), oakDRegister(Fs));
	mVU_loadGlobSS_oaknut(t1, offset);
	oakAsm->FCVT(oakDRegister(t1), oakSRegister(t1));
	oakAsm->FMADD(oakDRegister(P), oakDRegister(t1), oakDRegister(t2), oakDRegister(P));
}

static __fi void mVU_esinHelperD_oaknut(int P, int t1, int t2, int Fs, s64 offset)
{
	oakAsm->FMUL(oakDRegister(t2), oakDRegister(t2), oakDRegister(Fs));
	oakAsm->FMUL(oakDRegister(t2), oakDRegister(t2), oakDRegister(Fs));
	mVU_loadGlobSS_oaknut(t1, offset);
	oakAsm->FCVT(oakDRegister(t1), oakSRegister(t1));
	oakAsm->FMUL(oakDRegister(t1), oakDRegister(t1), oakDRegister(t2));
	oakAsm->FADD(oakDRegister(P), oakDRegister(P), oakDRegister(t1));
}

static void mVU_EEXP_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	const int t1 = mVU.regAlloc->allocRegId();
	const int t2 = mVU.regAlloc->allocRegId();
	const int P = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVU_EFUvuDoubleSS_oaknut(Fs);
	mVU_loadGlobSS_oaknut(P, offsetof(mVU_Globals, one), true);
	oakAsm->MOV(oakQRegister(t2).B16(), oakQRegister(Fs).B16());
	mVU_loadGlobSS_oaknut(t1, offsetof(mVU_Globals, E1));
	oakAsm->FMADD(oakSRegister(P), oakSRegister(t1), oakSRegister(t2), oakSRegister(P));
	oakAsm->FCVT(oakDRegister(P), oakSRegister(P));
	oakAsm->FCVT(oakDRegister(Fs), oakSRegister(Fs));
	oakAsm->MOV(oakQRegister(t2).B16(), oakQRegister(Fs).B16());
	mVU_eexpHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, E2));
	mVU_eexpHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, E3));
	mVU_eexpHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, E4));
	mVU_eexpHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, E5));
	mVU_eexpHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, E6));
	oakAsm->FCVT(oakSRegister(P), oakDRegister(P));
	oakAsm->FCVT(oakDRegister(P), oakSRegister(P));
	oakAsm->MOV(oakQRegister(t2).B16(), oakQRegister(P).B16());
	oakAsm->FMUL(oakDRegister(P), oakDRegister(P), oakDRegister(P));
	oakAsm->FMUL(oakDRegister(P), oakDRegister(P), oakDRegister(t2));
	oakAsm->FMUL(oakDRegister(P), oakDRegister(P), oakDRegister(t2));
	oakAsm->FCVT(oakSRegister(P), oakDRegister(P));
	mVU_EFUvuDoubleSS_oaknut(P);
	mVU_loadGlobSS_oaknut(Fs, offsetof(mVU_Globals, one), true);
	mVU_EFUdivScalarRaw_oaknut(Fs, P);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(Fs).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(t2);
	mVU.regAlloc->clearNeededXmmId(P);
}

static void mVU_EEXP_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU1(mVU, _Fs_, _Fsf_, 44);
	}
	pass2
	{
		mVU_EEXP_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("EEXP P"); }
}

static void mVU_ELENG_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int t1 = mVU.regAlloc->allocRegId();
	const int t2 = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVU_EFUvuDoublePS_oaknut(Fs, t1, t2);
	oakAsm->MOV(oakQRegister(t1).Selem()[0], oakQRegister(Fs).Selem()[1]);
	oakAsm->FMUL(oakSRegister(t1), oakSRegister(t1), oakSRegister(t1));
	oakAsm->MOV(OAK_QSCRATCH.Selem()[0], oakQRegister(Fs).Selem()[0]);
	oakAsm->FMADD(oakSRegister(t1), OAK_SSCRATCH, OAK_SSCRATCH, oakSRegister(t1));
	oakAsm->MOV(OAK_QSCRATCH.Selem()[0], oakQRegister(Fs).Selem()[2]);
	oakAsm->FMADD(oakSRegister(t1), OAK_SSCRATCH, OAK_SSCRATCH, oakSRegister(t1));
	oakAsm->FSQRT(oakSRegister(t1), oakSRegister(t1));
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(t1).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(t2);
}

static void mVU_ELENG_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU2(mVU, _Fs_, 18);
	}
	pass2
	{
		mVU_ELENG_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ELENG P"); }
}


static void mVU_ERCPR_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	const int t1 = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVU_EFUvuDoubleSS_oaknut(Fs);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(Fs).Selem()[0]);
	mVU_EFUreciprocalOrZero_oaknut(mVU, VU_HOST_XMMPQ, Fs, t1);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
}

static void mVU_ERCPR_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU1(mVU, _Fs_, _Fsf_, 12);
	}
	pass2
	{
		mVU_ERCPR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ERCPR P"); }
}


static void mVU_ERLENG_direct_emit_oaknut(mP)
{
	const int FsRaw = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int Fs = mVU.regAlloc->allocRegId();
	const int len = mVU.regAlloc->allocRegId();
	const int t1 = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	// Keep the interpreter's x*x + y*y + z*z evaluation order. This matters
	// under round-to-zero even when both additions are emitted as FMADDs.
	oakAsm->MOV(oakQRegister(Fs).B16(), oakQRegister(FsRaw).B16());
	mVU_EFUvuDoublePS_oaknut(Fs, len, t1);
	oakAsm->MOV(oakQRegister(len).Selem()[0], oakQRegister(Fs).Selem()[0]);
	oakAsm->FMUL(oakSRegister(len), oakSRegister(len), oakSRegister(len));
	oakAsm->MOV(OAK_QSCRATCH.Selem()[0], oakQRegister(Fs).Selem()[1]);
	oakAsm->FMADD(oakSRegister(len), OAK_SSCRATCH, OAK_SSCRATCH, oakSRegister(len));
	oakAsm->MOV(OAK_QSCRATCH.Selem()[0], oakQRegister(Fs).Selem()[2]);
	oakAsm->FMADD(oakSRegister(len), OAK_SSCRATCH, OAK_SSCRATCH, oakSRegister(len));
	oakAsm->FSQRT(oakSRegister(len), oakSRegister(len));
	mVU_EFUreciprocalOrZero_oaknut(mVU, len, Fs, t1);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(len).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FsRaw);
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(len);
	mVU.regAlloc->clearNeededXmmId(t1);
}

static void mVU_ERLENG_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU2(mVU, _Fs_, 24);
	}
	pass2
	{
		mVU_ERLENG_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ERLENG P"); }
}


static void mVU_ERSADD_direct_emit_oaknut(mP)
{
	const int FsRaw = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int x = mVU.regAlloc->allocRegId();
	const int y = mVU.regAlloc->allocRegId();
	const int z = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(x).Selem()[0], oakQRegister(FsRaw).Selem()[0]);
	oakAsm->MOV(oakQRegister(y).Selem()[0], oakQRegister(FsRaw).Selem()[1]);
	oakAsm->MOV(oakQRegister(z).Selem()[0], oakQRegister(FsRaw).Selem()[2]);
	mVU_EFUvuDoubleSS_oaknut(x);
	mVU_EFUvuDoubleSS_oaknut(y);
	mVU_EFUvuDoubleSS_oaknut(z);
	oakAsm->FMUL(oakSRegister(y), oakSRegister(y), oakSRegister(y));
	oakAsm->FMADD(oakSRegister(y), oakSRegister(x), oakSRegister(x), oakSRegister(y));
	oakAsm->FMADD(oakSRegister(y), oakSRegister(z), oakSRegister(z), oakSRegister(y));
	mVU_EFUreciprocalOrZero_oaknut(mVU, y, x, z);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(y).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FsRaw);
	mVU.regAlloc->clearNeededXmmId(x);
	mVU.regAlloc->clearNeededXmmId(y);
	mVU.regAlloc->clearNeededXmmId(z);
}

static void mVU_ERSADD_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU2(mVU, _Fs_, 18);
	}
	pass2
	{
		mVU_ERSADD_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ERSADD P"); }
}


static void mVU_ERSQRT_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	const int t1 = mVU.regAlloc->allocRegId();
	const int P = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(P).Selem()[0], oakQRegister(Fs).Selem()[0]);
	mVU_EFUvuDoubleSS_oaknut(P);
	mVU_EFUrsqrtNonnegativeOrZeroDouble_oaknut(mVU, P, Fs, t1);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(P).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(P);
}

static void mVU_ERSQRT_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU1(mVU, _Fs_, _Fsf_, 18);
	}
	pass2
	{
		mVU_ERSQRT_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ERSQRT P"); }
}


static void mVU_ESADD_direct_emit_oaknut(mP)
{
	const int FsRaw = mVU.regAlloc->allocRegId(_Fs_, 0, _X_Y_Z_W);
	const int x = mVU.regAlloc->allocRegId();
	const int y = mVU.regAlloc->allocRegId();
	const int z = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	oakAsm->MOV(oakQRegister(x).Selem()[0], oakQRegister(FsRaw).Selem()[0]);
	oakAsm->MOV(oakQRegister(y).Selem()[0], oakQRegister(FsRaw).Selem()[1]);
	oakAsm->MOV(oakQRegister(z).Selem()[0], oakQRegister(FsRaw).Selem()[2]);
	mVU_EFUvuDoubleSS_oaknut(x);
	mVU_EFUvuDoubleSS_oaknut(y);
	mVU_EFUvuDoubleSS_oaknut(z);
	oakAsm->FMUL(oakSRegister(y), oakSRegister(y), oakSRegister(y));
	oakAsm->FMADD(oakSRegister(y), oakSRegister(x), oakSRegister(x), oakSRegister(y));
	oakAsm->FMADD(oakSRegister(y), oakSRegister(z), oakSRegister(z), oakSRegister(y));
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(y).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(FsRaw);
	mVU.regAlloc->clearNeededXmmId(x);
	mVU.regAlloc->clearNeededXmmId(y);
	mVU.regAlloc->clearNeededXmmId(z);
}

static void mVU_ESADD_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU2(mVU, _Fs_, 11);
	}
	pass2
	{
		mVU_ESADD_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ESADD P"); }
}


static void mVU_ESIN_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	const int t1 = mVU.regAlloc->allocRegId();
	const int t2 = mVU.regAlloc->allocRegId();
	const int P = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVU_EFUvuDoubleSS_oaknut(Fs);
	oakAsm->MOV(oakQRegister(P).Selem()[0], oakQRegister(Fs).Selem()[0]);
	oakAsm->FCVT(oakDRegister(P), oakSRegister(P));
	oakAsm->FCVT(oakDRegister(Fs), oakSRegister(Fs));
	oakAsm->MOV(oakQRegister(t2).B16(), oakQRegister(Fs).B16());
	mVU_esinHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, S2));
	mVU_esinHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, S3));
	mVU_esinHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, S4));
	mVU_esinHelperD_oaknut(P, t1, t2, Fs, offsetof(mVU_Globals, S5));
	oakAsm->FCVT(oakSRegister(P), oakDRegister(P));
	mVU_EFUvuDoubleSS_oaknut(P);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(P).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(t2);
	mVU.regAlloc->clearNeededXmmId(P);
}

static void mVU_ESIN_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU1(mVU, _Fs_, _Fsf_, 29);
	}
	pass2
	{
		mVU_ESIN_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ESIN P"); }
}


static void mVU_ESQRT_direct_emit_oaknut(mP);

static void mVU_ESQRT_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU1(mVU, _Fs_, _Fsf_, 12);
	}
	pass2
	{
		mVU_ESQRT_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ESQRT P"); }
}


static void mVU_ESQRT_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	recBeginOaknutEmit();
	mVU_EFUvuDoubleSS_oaknut(Fs);
	mVU_EFUsqrtNonnegativeDouble_oaknut(Fs);
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(Fs).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_ESUM_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, 0xf);
	const int t1 = mVU.regAlloc->allocRegId();
	const int t2 = mVU.regAlloc->allocRegId();
	recBeginOaknutEmit();
	mVU_EFUvuDoublePS_oaknut(Fs, t1, t2);
	oakAsm->MOV(oakQRegister(t2).Selem()[0], oakQRegister(Fs).Selem()[0]);
	mVU_pshufd_oaknut(t1, Fs, 0x01);
	oakAsm->FADD(oakSRegister(t2), oakSRegister(t2), oakSRegister(t1));
	mVU_pshufd_oaknut(t1, Fs, 0x02);
	oakAsm->FADD(oakSRegister(t2), oakSRegister(t2), oakSRegister(t1));
	mVU_pshufd_oaknut(t1, Fs, 0x03);
	oakAsm->FADD(oakSRegister(t2), oakSRegister(t2), oakSRegister(t1));
	mVU_flipPQ_oaknut(mVU);
	oakAsm->MOV(oakQRegister(VU_HOST_XMMPQ).Selem()[0], oakQRegister(t2).Selem()[0]);
	mVU_flipPQ_oaknut(mVU);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
	mVU.regAlloc->clearNeededXmmId(t1);
	mVU.regAlloc->clearNeededXmmId(t2);
}

static void mVU_ESUM_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeEFU2(mVU, _Fs_, 12);
	}
	pass2
	{
		mVU_ESUM_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ESUM P"); }
}


//------------------------------------------------------------------
// FCAND/FCEQ/FCGET/FCOR/FCSET
//------------------------------------------------------------------

static __fi void mVU_normalizeVIWrite_oaknut(int reg)
{
	recBeginOaknutEmit();
	oakAsm->UXTH(oakWRegister(reg), oakWRegister(reg));
	recEndOaknutEmit();
}

static void mVU_FCAND_direct_emit_oaknut(mP)
{
	const int dst = mVU.regAlloc->allocGPRId(-1, 1, mVUlow.backupVI);
	mVUallocCFLAGa(mVU, dst, cFLAG.read);
	const oak::WReg dst_w = oakWRegister(dst);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, _Imm24_);
	oakAsm->AND(dst_w, dst_w, OAK_WSCRATCH);
	oakAsm->MOV(OAK_WSCRATCH, 0xffffff);
	oakAsm->ADD(dst_w, dst_w, OAK_WSCRATCH);
	oakAsm->LSR(dst_w, dst_w, 24);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(dst);
	mVU.regAlloc->clearNeeded(dst);
}

static void mVU_FCAND_emit(mP)
{
	pass1 { mVUanalyzeCflag(mVU, 1); }
	pass2
	{
		mVU_FCAND_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FCAND vi01, $%x", _Imm24_); }
	pass4 { mVUregs.needExactMatch |= 4; }
}

static void mVU_FCEQ_direct_emit_oaknut(mP)
{
	const int dst = mVU.regAlloc->allocGPRId(-1, 1, mVUlow.backupVI);
	mVUallocCFLAGa(mVU, dst, cFLAG.read);
	const oak::WReg dst_w = oakWRegister(dst);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, _Imm24_);
	oakAsm->EOR(dst_w, dst_w, OAK_WSCRATCH);
	oakAsm->SUB(dst_w, dst_w, 1);
	oakAsm->LSR(dst_w, dst_w, 31);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(dst);
	mVU.regAlloc->clearNeeded(dst);
}

static void mVU_FCEQ_emit(mP)
{
	pass1 { mVUanalyzeCflag(mVU, 1); }
	pass2
	{
		mVU_FCEQ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FCEQ vi01, $%x", _Imm24_); }
	pass4 { mVUregs.needExactMatch |= 4; }
}

static void mVU_FCGET_direct_emit_oaknut(mP)
{
	const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	mVUallocCFLAGa(mVU, regT, cFLAG.read);
	recBeginOaknutEmit();
	oakAsm->AND(oakWRegister(regT), oakWRegister(regT), 0xfff);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(regT);
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_FCGET_emit(mP)
{
	pass1 { mVUanalyzeCflag(mVU, _It_); }
	pass2
	{
		mVU_FCGET_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FCGET vi%02d", _Ft_); }
	pass4 { mVUregs.needExactMatch |= 4; }
}

static void mVU_FCOR_direct_emit_oaknut(mP)
{
	const int dst = mVU.regAlloc->allocGPRId(-1, 1, mVUlow.backupVI);
	mVUallocCFLAGa(mVU, dst, cFLAG.read);
	const oak::WReg dst_w = oakWRegister(dst);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, _Imm24_);
	oakAsm->ORR(dst_w, dst_w, OAK_WSCRATCH);
	oakAsm->ADD(dst_w, dst_w, 1);
	oakAsm->LSR(dst_w, dst_w, 24);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(dst);
	mVU.regAlloc->clearNeeded(dst);
}

static void mVU_FCOR_emit(mP)
{
	pass1 { mVUanalyzeCflag(mVU, 1); }
	pass2
	{
		mVU_FCOR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FCOR vi01, $%x", _Imm24_); }
	pass4 { mVUregs.needExactMatch |= 4; }
}

static void mVU_FCSET_direct_emit_oaknut(mP)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakWRegister(VU_HOST_T1), _Imm24_);
	recEndOaknutEmit();
	mVUallocCFLAGb(mVU, VU_HOST_T1, cFLAG.write);
}

static void mVU_FCSET_emit(mP)
{
	pass1 { cFLAG.doFlag = true; }
	pass2
	{
		mVU_FCSET_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FCSET $%x", _Imm24_); }
}


//------------------------------------------------------------------
// FMAND/FMEQ/FMOR
//------------------------------------------------------------------

static void mVU_FMAND_direct_emit_oaknut(mP)
{
	mVUallocMFLAGa(mVU, VU_HOST_T1, mFLAG.read);
	const int regT = mVU.regAlloc->allocGPRId(_Is_, _It_, mVUlow.backupVI);
	recBeginOaknutEmit();
	oakAsm->AND(oakWRegister(regT), oakWRegister(regT), oakWRegister(VU_HOST_T1));
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(regT);
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_FMAND_emit(mP)
{
	pass1 { mVUanalyzeMflag(mVU, _Is_, _It_); }
	pass2
	{
		mVU_FMAND_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FMAND vi%02d, vi%02d", _Ft_, _Fs_); }
	pass4 { mVUregs.needExactMatch |= 2; }
}

static void mVU_FMEQ_direct_emit_oaknut(mP)
{
	mVUallocMFLAGa(mVU, VU_HOST_T1, mFLAG.read);
	const int regT = mVU.regAlloc->allocGPRId(_Is_, _It_, mVUlow.backupVI);
	const oak::WReg regT_w = oakWRegister(regT);
	recBeginOaknutEmit();
	oakAsm->EOR(regT_w, regT_w, oakWRegister(VU_HOST_T1));
	oakAsm->SUB(regT_w, regT_w, 1);
	oakAsm->LSR(regT_w, regT_w, 31);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(regT);
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_FMEQ_emit(mP)
{
	pass1 { mVUanalyzeMflag(mVU, _Is_, _It_); }
	pass2
	{
		mVU_FMEQ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FMEQ vi%02d, vi%02d", _Ft_, _Fs_); }
	pass4 { mVUregs.needExactMatch |= 2; }
}

static void mVU_FMOR_direct_emit_oaknut(mP)
{
	mVUallocMFLAGa(mVU, VU_HOST_T1, mFLAG.read);
	const int regT = mVU.regAlloc->allocGPRId(_Is_, _It_, mVUlow.backupVI);
	recBeginOaknutEmit();
	oakAsm->ORR(oakWRegister(regT), oakWRegister(regT), oakWRegister(VU_HOST_T1));
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(regT);
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_FMOR_emit(mP)
{
	pass1 { mVUanalyzeMflag(mVU, _Is_, _It_); }
	pass2
	{
		mVU_FMOR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FMOR vi%02d, vi%02d", _Ft_, _Fs_); }
	pass4 { mVUregs.needExactMatch |= 2; }
}


//------------------------------------------------------------------
// FSAND/FSEQ/FSOR/FSSET
//------------------------------------------------------------------

static void mVU_FSAND_direct_emit_oaknut(mP)
{
	const int reg = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	mVUallocSFLAGc(reg, VU_HOST_T1, sFLAG.read);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, _Imm12_);
	oakAsm->AND(oakWRegister(reg), oakWRegister(reg), OAK_WSCRATCH);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(reg);
	mVU.regAlloc->clearNeeded(reg);
}

static void mVU_FSAND_emit(mP)
{
	pass1 { mVUanalyzeSflag(mVU, _It_); }
	pass2
	{
		mVU_FSAND_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FSAND vi%02d, $%x", _Ft_, _Imm12_); }
	pass4 { mVUregs.needExactMatch |= 1; }
}

static void mVU_FSOR_direct_emit_oaknut(mP)
{
	const int reg = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	mVUallocSFLAGc(reg, VU_HOST_T2, sFLAG.read);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, _Imm12_);
	oakAsm->ORR(oakWRegister(reg), oakWRegister(reg), OAK_WSCRATCH);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(reg);
	mVU.regAlloc->clearNeeded(reg);
}

static void mVU_FSOR_emit(mP)
{
	pass1 { mVUanalyzeSflag(mVU, _It_); }
	pass2
	{
		mVU_FSOR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FSOR vi%02d, $%x", _Ft_, _Imm12_); }
	pass4 { mVUregs.needExactMatch |= 1; }
}

static void mVU_FSEQ_direct_emit_oaknut(mP)
{
	int imm = 0;
	if (_Imm12_ & 0x0001) imm |= 0x0000f00; // Z
	if (_Imm12_ & 0x0002) imm |= 0x000f000; // S
	if (_Imm12_ & 0x0004) imm |= 0x0010000; // U
	if (_Imm12_ & 0x0008) imm |= 0x0020000; // O
	if (_Imm12_ & 0x0010) imm |= 0x0040000; // I
	if (_Imm12_ & 0x0020) imm |= 0x0080000; // D
	if (_Imm12_ & 0x0040) imm |= 0x000000f; // ZS
	if (_Imm12_ & 0x0080) imm |= 0x00000f0; // SS
	if (_Imm12_ & 0x0100) imm |= 0x0400000; // US
	if (_Imm12_ & 0x0200) imm |= 0x0800000; // OS
	if (_Imm12_ & 0x0400) imm |= 0x1000000; // IS
	if (_Imm12_ & 0x0800) imm |= 0x2000000; // DS

	const int reg = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	mVUallocSFLAGa(reg, sFLAG.read);
	setBitFSEQ(reg, 0x0f00); // Z  bit
	setBitFSEQ(reg, 0xf000); // S  bit
	setBitFSEQ(reg, 0x000f); // ZS bit
	setBitFSEQ(reg, 0x00f0); // SS bit
	const oak::WReg reg_w = oakWRegister(reg);
	recBeginOaknutEmit();
	oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(imm));
	oakAsm->EOR(reg_w, reg_w, OAK_WSCRATCH);
	oakAsm->SUB(reg_w, reg_w, 1);
	oakAsm->LSR(reg_w, reg_w, 31);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(reg);
	mVU.regAlloc->clearNeeded(reg);
}

static void mVU_FSEQ_emit(mP)
{
	pass1 { mVUanalyzeSflag(mVU, _It_); }
	pass2
	{
		mVU_FSEQ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FSEQ vi%02d, $%x", _Ft_, _Imm12_); }
	pass4 { mVUregs.needExactMatch |= 1; }
}

static void mVU_FSSET_direct_emit_oaknut(mP)
{
	int imm = 0;
	if (_Imm12_ & 0x0040) imm |= 0x000000f; // ZS
	if (_Imm12_ & 0x0080) imm |= 0x00000f0; // SS
	if (_Imm12_ & 0x0100) imm |= 0x0400000; // US
	if (_Imm12_ & 0x0200) imm |= 0x0800000; // OS
	if (_Imm12_ & 0x0400) imm |= 0x1000000; // IS
	if (_Imm12_ & 0x0800) imm |= 0x2000000; // DS
	if (!(sFLAG.doFlag || mVUinfo.doDivFlag))
	{
		mVUallocSFLAGa(getFlagRegId(sFLAG.write), sFLAG.lastWrite); // Get Prev Status Flag
	}
	const oak::WReg reg_w = oakWRegister(getFlagRegId(sFLAG.write));
	recBeginOaknutEmit();
	oakAsm->AND(reg_w, reg_w, 0xfff00);
	if (imm)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(imm));
		oakAsm->ORR(reg_w, reg_w, OAK_WSCRATCH);
	}
	recEndOaknutEmit();
}

static void mVU_FSSET_emit(mP)
{
	pass1 { mVUanalyzeFSSET(mVU); }
	pass2
	{
		mVU_FSSET_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("FSSET $%x", _Imm12_); }
}


//------------------------------------------------------------------
// IADD/IADDI/IADDIU/IAND/IOR/ISUB/ISUBIU
//------------------------------------------------------------------

static void mVU_IADD_direct_emit_oaknut(mP)
{
	if (_Is_ == 0 || _It_ == 0)
	{
		const int regS = mVU.regAlloc->allocGPRId(_Is_ ? _Is_ : _It_, -1);
		const int regD = mVU.regAlloc->allocGPRId(-1, _Id_, mVUlow.backupVI);
		recBeginOaknutEmit();
		oakAsm->MOV(oakWRegister(regD), oakWRegister(regS));
		recEndOaknutEmit();
		mVU.regAlloc->clearNeeded(regD);
		mVU.regAlloc->clearNeeded(regS);
	}
	else
	{
		const int regT = mVU.regAlloc->allocGPRId(_It_, -1);
		const int regS = mVU.regAlloc->allocGPRId(_Is_, _Id_, mVUlow.backupVI);
		recBeginOaknutEmit();
		oakAsm->ADD(oakWRegister(regS), oakWRegister(regS), oakWRegister(regT));
		recEndOaknutEmit();
		mVU_normalizeVIWrite_oaknut(regS);
		mVU.regAlloc->clearNeeded(regS);
		mVU.regAlloc->clearNeeded(regT);
	}
}

static void mVU_IADD_emit(mP)
{
	pass1 { mVUanalyzeIALU1(mVU, _Id_, _Is_, _It_); }
	pass2
	{
		mVU_IADD_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IADD vi%02d, vi%02d, vi%02d", _Fd_, _Fs_, _Ft_); }
}

static void mVU_IADDI_load_ibithack_imm5_emit_oaknut(mV, oak::WReg dst)
{
	oakLoad64(OAK_XSCRATCH, {oak::util::X27,
		static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Micro))});
	oakLoad32(dst, {OAK_XSCRATCH, static_cast<s64>(mVU.prog.IRinfo.curPC * sizeof(u32))});
	oakAsm->LSL(dst, dst, 21);
	oakAsm->ASR(dst, dst, 27);
}

static void mVU_IADDI_direct_emit_oaknut(mP)
{
	if (_Is_ == 0)
	{
		const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
		const oak::WReg regTW = oakWRegister(regT);
		recBeginOaknutEmit();
		if (!EmuConfig.Gamefixes.IbitHack)
		{
			if (_Imm5_ != 0)
				oakAsm->MOV(regTW, static_cast<u32>(_Imm5_));
			else
				oakAsm->EOR(regTW, regTW, regTW);
		}
		else
		{
			mVU_IADDI_load_ibithack_imm5_emit_oaknut(mVU, regTW);
		}
		recEndOaknutEmit();
		mVU_normalizeVIWrite_oaknut(regT);
		mVU.regAlloc->clearNeeded(regT);
	}
	else
	{
		const int regS = mVU.regAlloc->allocGPRId(_Is_, _It_, mVUlow.backupVI);
		const oak::WReg regSW = oakWRegister(regS);
		recBeginOaknutEmit();
		if (!EmuConfig.Gamefixes.IbitHack)
		{
			if (_Imm5_ > 0)
				oakAsm->ADD(regSW, regSW, static_cast<u32>(_Imm5_));
			else if (_Imm5_ < 0)
				oakAsm->SUB(regSW, regSW, static_cast<u32>(-_Imm5_));
		}
		else
		{
			mVU_IADDI_load_ibithack_imm5_emit_oaknut(mVU, oakWRegister(VU_HOST_T1));
			oakAsm->ADD(regSW, regSW, oakWRegister(VU_HOST_T1));
		}
		recEndOaknutEmit();
		mVU_normalizeVIWrite_oaknut(regS);
		mVU.regAlloc->clearNeeded(regS);
	}
}

static void mVU_IADDI_emit(mP)
{
	pass1 { mVUanalyzeIADDI(mVU, _Is_, _It_, _Imm5_); }
	pass2
	{
		mVU_IADDI_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IADDI vi%02d, vi%02d, %d", _Ft_, _Fs_, _Imm5_); }
}

static void mVU_IADDIU_load_ibithack_imm15_emit_oaknut(mV, oak::WReg dst)
{
	oakLoad64(OAK_XSCRATCH, {oak::util::X27,
		static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Micro))});
	oakLoad32(dst, {OAK_XSCRATCH, static_cast<s64>(mVU.prog.IRinfo.curPC * sizeof(u32))});
	oakAsm->MOV(OAK_WSCRATCH2, dst);
	oakAsm->LSR(OAK_WSCRATCH2, OAK_WSCRATCH2, 10);
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x7800);
	oakAsm->AND(dst, dst, 0x7ff);
	oakAsm->ORR(dst, dst, OAK_WSCRATCH2);
}

static void mVU_IADDIU_direct_emit_oaknut(mP)
{
	if (_Is_ == 0)
	{
		const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
		const oak::WReg regTW = oakWRegister(regT);
		recBeginOaknutEmit();
		if (!EmuConfig.Gamefixes.IbitHack)
		{
			if (_Imm15_ != 0)
				oakAsm->MOV(regTW, static_cast<u32>(_Imm15_));
			else
				oakAsm->EOR(regTW, regTW, regTW);
		}
		else
		{
			mVU_IADDIU_load_ibithack_imm15_emit_oaknut(mVU, regTW);
		}
		recEndOaknutEmit();
		mVU.regAlloc->clearNeeded(regT);
	}
	else
	{
		const int regS = mVU.regAlloc->allocGPRId(_Is_, _It_, mVUlow.backupVI);
		const oak::WReg regSW = oakWRegister(regS);
		recBeginOaknutEmit();
		if (!EmuConfig.Gamefixes.IbitHack)
		{
			if (_Imm15_ != 0)
			{
				oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(_Imm15_));
				oakAsm->ADD(regSW, regSW, OAK_WSCRATCH);
			}
		}
		else
		{
			mVU_IADDIU_load_ibithack_imm15_emit_oaknut(mVU, OAK_WSCRATCH);
			oakAsm->ADD(regSW, regSW, OAK_WSCRATCH);
		}
		recEndOaknutEmit();
		mVU_normalizeVIWrite_oaknut(regS);
		mVU.regAlloc->clearNeeded(regS);
	}
}

static void mVU_IADDIU_emit(mP)
{
	pass1 { mVUanalyzeIADDI(mVU, _Is_, _It_, _Imm15_); }
	pass2
	{
		mVU_IADDIU_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IADDIU vi%02d, vi%02d, %d", _Ft_, _Fs_, _Imm15_); }
}

static void mVU_IAND_direct_emit_oaknut(mP)
{
	const int regT = mVU.regAlloc->allocGPRId(_It_, -1);
	const int regS = mVU.regAlloc->allocGPRId(_Is_, _Id_, mVUlow.backupVI);
	if (_It_ != _Is_)
	{
		recBeginOaknutEmit();
		oakAsm->AND(oakWRegister(regS), oakWRegister(regS), oakWRegister(regT));
		recEndOaknutEmit();
	}
	mVU_normalizeVIWrite_oaknut(regS);
	mVU.regAlloc->clearNeeded(regS);
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_IAND_emit(mP)
{
	pass1 { mVUanalyzeIALU1(mVU, _Id_, _Is_, _It_); }
	pass2
	{
		mVU_IAND_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IAND vi%02d, vi%02d, vi%02d", _Fd_, _Fs_, _Ft_); }
}

static void mVU_IOR_direct_emit_oaknut(mP)
{
	const int regT = mVU.regAlloc->allocGPRId(_It_, -1);
	const int regS = mVU.regAlloc->allocGPRId(_Is_, _Id_, mVUlow.backupVI);
	if (_It_ != _Is_)
	{
		recBeginOaknutEmit();
		oakAsm->ORR(oakWRegister(regS), oakWRegister(regS), oakWRegister(regT));
		recEndOaknutEmit();
	}
	mVU_normalizeVIWrite_oaknut(regS);
	mVU.regAlloc->clearNeeded(regS);
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_IOR_emit(mP)
{
	pass1 { mVUanalyzeIALU1(mVU, _Id_, _Is_, _It_); }
	pass2
	{
		mVU_IOR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IOR vi%02d, vi%02d, vi%02d", _Fd_, _Fs_, _Ft_); }
}

static void mVU_ISUB_direct_emit_oaknut(mP)
{
	if (_It_ != _Is_)
	{
		const int regT = mVU.regAlloc->allocGPRId(_It_, -1);
		const int regS = mVU.regAlloc->allocGPRId(_Is_, _Id_, mVUlow.backupVI);
		recBeginOaknutEmit();
		oakAsm->SUB(oakWRegister(regS), oakWRegister(regS), oakWRegister(regT));
		recEndOaknutEmit();
		mVU_normalizeVIWrite_oaknut(regS);
		mVU.regAlloc->clearNeeded(regS);
		mVU.regAlloc->clearNeeded(regT);
	}
	else
	{
		const int regD = mVU.regAlloc->allocGPRId(-1, _Id_, mVUlow.backupVI);
		recBeginOaknutEmit();
		oakAsm->EOR(oakWRegister(regD), oakWRegister(regD), oakWRegister(regD));
		recEndOaknutEmit();
		mVU_normalizeVIWrite_oaknut(regD);
		mVU.regAlloc->clearNeeded(regD);
	}
}

static void mVU_ISUB_emit(mP)
{
	pass1 { mVUanalyzeIALU1(mVU, _Id_, _Is_, _It_); }
	pass2
	{
		mVU_ISUB_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ISUB vi%02d, vi%02d, vi%02d", _Fd_, _Fs_, _Ft_); }
}

static void mVU_ISUBIU_load_ibithack_imm15_emit_oaknut(mV, oak::WReg dst)
{
	oakLoad64(OAK_XSCRATCH, {oak::util::X27,
		static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Micro))});
	oakLoad32(dst, {OAK_XSCRATCH, static_cast<s64>(mVU.prog.IRinfo.curPC * sizeof(u32))});
	oakAsm->MOV(OAK_WSCRATCH2, dst);
	oakAsm->LSR(OAK_WSCRATCH2, OAK_WSCRATCH2, 10);
	oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x7800);
	oakAsm->AND(dst, dst, 0x7ff);
	oakAsm->ORR(dst, dst, OAK_WSCRATCH2);
}

static void mVU_ISUBIU_direct_emit_oaknut(mP)
{
	const int regS = mVU.regAlloc->allocGPRId(_Is_, _It_, mVUlow.backupVI);
	const oak::WReg regSW = oakWRegister(regS);
	recBeginOaknutEmit();
	if (!EmuConfig.Gamefixes.IbitHack)
	{
		if (_Imm15_ != 0)
		{
			oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(_Imm15_));
			oakAsm->SUB(regSW, regSW, OAK_WSCRATCH);
		}
	}
	else
	{
		mVU_ISUBIU_load_ibithack_imm15_emit_oaknut(mVU, OAK_WSCRATCH);
		oakAsm->SUB(regSW, regSW, OAK_WSCRATCH);
	}
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(regS);
	mVU.regAlloc->clearNeeded(regS);
}

static void mVU_ISUBIU_emit(mP)
{
	pass1 { mVUanalyzeIALU2(mVU, _Is_, _It_); }
	pass2
	{
		mVU_ISUBIU_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ISUBIU vi%02d, vi%02d, %d", _Ft_, _Fs_, _Imm15_); }
}


//------------------------------------------------------------------
// MFIR/MFP/MOVE/MR32/MTIR
//------------------------------------------------------------------

static void mVU_MFIR_direct_emit_oaknut(mP)
{
	const int Ft = mVU.regAlloc->allocRegId(-1, _Ft_, _X_Y_Z_W);
	const oak::QReg dst = oakQRegister(Ft);
	if (_Is_ != 0)
	{
		const int regS = mVU.regAlloc->allocGPRId(_Is_, -1);
		const oak::WReg src = oakWRegister(regS);
		recBeginOaknutEmit();
		oakAsm->SXTH(OAK_WSCRATCH, src);
		oakAsm->EOR(dst.B16(), dst.B16(), dst.B16());
		oakAsm->FMOV(oakSRegister(Ft), OAK_WSCRATCH);
		if (!_XYZW_SS)
			oakAsm->DUP(dst.S4(), dst.Selem()[0]);
		recEndOaknutEmit();
		mVU.regAlloc->clearNeeded(regS);
	}
	else
	{
		recBeginOaknutEmit();
		oakAsm->EOR(dst.B16(), dst.B16(), dst.B16());
		recEndOaknutEmit();
	}
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_MFIR_emit(mP)
{
	pass1
	{
		if (!_Ft_)
		{
			mVUlow.isNOP = true;
		}
		analyzeVIreg1(mVU, _Is_, mVUlow.VI_read[0]);
		analyzeReg2  (mVU, _Ft_, mVUlow.VF_write, 1);
	}
	pass2
	{
		mVU_MFIR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("MFIR.%s vf%02d, vi%02d", _XYZW_String, _Ft_, _Fs_); }
}

static void mVU_MFP_direct_emit_oaknut(mP)
{
	const int Ft = mVU.regAlloc->allocRegId(-1, _Ft_, _X_Y_Z_W);
	recBeginOaknutEmit();
	oakAsm->DUP(oakQRegister(Ft).S4(), oakQRegister(VU_HOST_XMMPQ).Selem()[2 + mVUinfo.readP]);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_MFP_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeMFP(mVU, _Ft_);
	}
	pass2
	{
		mVU_MFP_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("MFP.%s vf%02d, P", _XYZW_String, _Ft_); }
}

static void mVU_MOVE_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _Ft_, _X_Y_Z_W);
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_MOVE_emit(mP)
{
	pass1 { mVUanalyzeMOVE(mVU, _Fs_, _Ft_); }
	pass2
	{
		mVU_MOVE_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("MOVE.%s vf%02d, vf%02d", _XYZW_String, _Ft_, _Fs_); }
}

static void mVU_MTIR_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
	const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	recBeginOaknutEmit();
	oakAsm->FMOV(oakWRegister(regT), oakSRegister(Fs));
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(regT);
	mVU.regAlloc->clearNeeded(regT);
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_MR32_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_);
	const int Ft = mVU.regAlloc->allocRegId(-1, _Ft_, _X_Y_Z_W);
	const oak::QReg src = oakQRegister(Fs);
	const oak::QReg dst = oakQRegister(Ft);

	recBeginOaknutEmit();
	if (_XYZW_SS)
	{
		oakAsm->DUP(dst.S4(), src.Selem()[(_X ? 1 : (_Y ? 2 : (_Z ? 3 : 0)))]);
	}
	else
	{
		oakAsm->MOV(OAK_QSCRATCH.Selem()[0], src.Selem()[1]);
		oakAsm->MOV(OAK_QSCRATCH.Selem()[1], src.Selem()[2]);
		oakAsm->MOV(OAK_QSCRATCH.Selem()[2], src.Selem()[3]);
		oakAsm->MOV(OAK_QSCRATCH.Selem()[3], src.Selem()[0]);
		oakAsm->MOV(dst.B16(), OAK_QSCRATCH.B16());
	}
	recEndOaknutEmit();

	mVU.regAlloc->clearNeededXmmId(Ft);
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_MR32_emit(mP)
{
	pass1 { mVUanalyzeMR32(mVU, _Fs_, _Ft_); }
	pass2
	{
		mVU_MR32_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("MR32.%s vf%02d, vf%02d", _XYZW_String, _Ft_, _Fs_); }
}

static void mVU_MTIR_emit(mP)
{
	pass1
	{
		if (!_It_)
			mVUlow.isNOP = true;

		analyzeReg5(mVU, _Fs_, _Fsf_, mVUlow.VF_read[0]);
		analyzeVIreg2(mVU, _It_, mVUlow.VI_write, 1);
	}
	pass2
	{
		mVU_MTIR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("MTIR vi%02d, vf%02d%s", _Ft_, _Fs_, _Fsf_String); }
}

static void mVU_moveVIToGPR_oaknut(mV, int dst, int vi, bool signext = false)
{
	const oak::WReg dst_w = oakWRegister(dst);
	if (vi == 0)
	{
		recBeginOaknutEmit();
		oakAsm->EOR(dst_w, dst_w, dst_w);
		recEndOaknutEmit();
		return;
	}

	const int src = mVU.regAlloc->allocGPRId(vi);
	recBeginOaknutEmit();
	if (signext)
		oakAsm->SXTH(dst_w, oakWRegister(src));
	else
		oakAsm->UXTH(dst_w, oakWRegister(src));
	recEndOaknutEmit();
	mVU.regAlloc->clearNeeded(src);
}

static void mVU_moveVIToAddressGPR_oaknut(mV, int dst, int vi)
{
	const oak::WReg dst_w = oakWRegister(dst);
	if (vi == 0)
	{
		recBeginOaknutEmit();
		oakAsm->EOR(dst_w, dst_w, dst_w);
		recEndOaknutEmit();
		return;
	}

	const int src = mVU.regAlloc->allocGPRId(vi);
	if (dst != src)
	{
		recBeginOaknutEmit();
		oakAsm->MOV(dst_w, oakWRegister(src));
		recEndOaknutEmit();
	}
	mVU.regAlloc->clearNeeded(src);
}

static void mVUaddrFix_oaknut(mV, int gprReg)
{
	const oak::XReg addr_x = oakXRegister(gprReg);
	const oak::WReg addr_w = oakWRegister(gprReg);
	recBeginOaknutEmit();
	if (isVU1)
	{
		oakAsm->UBFIZ(addr_w, addr_w, 4, 10);
	}
	else
	{
		oak::Label vu1_reg_map;
		oak::Label done;
		oakAsm->TST(addr_w, 0x400);
		oakAsm->B(oak::Cond::NE, vu1_reg_map);
		oakAsm->AND(addr_w, addr_w, 0xff);
		oakAsm->LSL(addr_x, addr_x, 4);
		oakAsm->B(done);

		oakAsm->l(vu1_reg_map);
		if (THREAD_VU1)
			oakEmitCall(reinterpret_cast<void*>(mVU.waitMTVU));
		oakAsm->AND(addr_w, addr_w, 0x3f);
		oakAsm->LSL(addr_x, addr_x, 4);
		// The common memory emitters add VU0.Mem after this helper. Convert the
		// mapped VU1 VF/VI address to an offset from that runtime pointer; using
		// the offset of the Mem field itself is invalid because Mem is external.
		oakAsm->MOV(OAK_XSCRATCH,
			static_cast<u64>(offsetof(cpuRegistersPack, vuRegs[1].VF)));
		oakAsm->ADD(addr_x, addr_x, oak::util::X27);
		oakAsm->ADD(addr_x, addr_x, OAK_XSCRATCH);
		oakLoad64(OAK_XSCRATCH,
			mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[0].Mem))));
		oakAsm->SUB(addr_x, addr_x, OAK_XSCRATCH);

		oakAsm->l(done);
	}
	recEndOaknutEmit();
}

static __fi void mVU_addPointerOffset_oaknut(const oak::XReg& reg, s64 offset)
{
	if (offset == 0)
		return;

	if (offset > 0 && offset <= 4095)
	{
		oakAsm->ADD(reg, reg, static_cast<u32>(offset));
	}
	else
	{
		oakAsm->MOV(OAK_XSCRATCH, static_cast<u64>(offset));
		oakAsm->ADD(reg, reg, OAK_XSCRATCH);
	}
}

static bool mVU_tryConstantMemoryBase_oaknut(mP, int vi, s32 imm, s32 byte_offset, s64& offset)
{
	if (EmuConfig.Gamefixes.IbitHack || vi != 0)
		return false;

	const s32 addr = imm;
	if (isVU0 && (addr & 0x400))
		return false;

	offset = (isVU1 ? ((addr & 0x3ffu) << 4) : ((addr & 0xffu) << 4)) + byte_offset;
	recBeginOaknutEmit();
	oakLoad64(oakXRegister(VU_HOST_T2),
		mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Mem))));
	recEndOaknutEmit();
	return true;
}

static bool mVU_tryConstantMemoryAddress_oaknut(mP, int vi, s32 imm, s32 byte_offset)
{
	s64 offset = 0;
	if (!mVU_tryConstantMemoryBase_oaknut(mVU, recPass, vi, imm, byte_offset, offset))
		return false;

	recBeginOaknutEmit();
	mVU_addPointerOffset_oaknut(oakXRegister(VU_HOST_T2), offset);
	recEndOaknutEmit();
	return true;
}

static void mVU_makeMemoryAddress_oaknut(mP, int addr, int vi, s32 imm, s32 byte_offset)
{
	if (mVU_tryConstantMemoryAddress_oaknut(mVU, recPass, vi, imm, byte_offset))
		return;

	mVU_moveVIToAddressGPR_oaknut(mVU, addr, vi);
	const oak::WReg addr_w = oakWRegister(addr);
	recBeginOaknutEmit();
	if (!EmuConfig.Gamefixes.IbitHack)
	{
		if (imm != 0)
		{
			if (imm > 0 && imm <= 4095)
			{
				oakAsm->ADD(addr_w, addr_w, static_cast<u32>(imm));
			}
			else if (imm < 0 && imm >= -4095)
			{
				oakAsm->SUB(addr_w, addr_w, static_cast<u32>(-imm));
			}
			else
			{
				oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(imm));
				oakAsm->ADD(addr_w, addr_w, OAK_WSCRATCH);
			}
		}
	}
	else
	{
		oakLoad64(OAK_XSCRATCH,
			mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Micro))));
		oakLoad32(OAK_WSCRATCH,
			{OAK_XSCRATCH, static_cast<s64>(mVU.prog.IRinfo.curPC * sizeof(u32))});
		oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, 21);
		oakAsm->ASR(OAK_WSCRATCH, OAK_WSCRATCH, 21);
		oakAsm->ADD(addr_w, addr_w, OAK_WSCRATCH);
	}
	recEndOaknutEmit();

	mVUaddrFix_oaknut(mVU, addr);

	recBeginOaknutEmit();
	oakLoad64(oakXRegister(VU_HOST_T2),
		mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Mem))));
	mVU_addPointerOffset_oaknut(oakXRegister(VU_HOST_T2), byte_offset);
	oakAsm->ADD(oakXRegister(VU_HOST_T2), oakXRegister(VU_HOST_T2), oakXRegister(addr));
	recEndOaknutEmit();
}

static bool mVU_makeIndexedQwordMemoryAddress_oaknut(mP, int addr, int vi, s32 imm)
{
	if (!isVU1 || EmuConfig.Gamefixes.IbitHack || vi == 0)
		return false;

	mVU_moveVIToAddressGPR_oaknut(mVU, addr, vi);
	const oak::WReg addr_w = oakWRegister(addr);
	recBeginOaknutEmit();
	if (imm > 0 && imm <= 4095)
	{
		oakAsm->ADD(addr_w, addr_w, static_cast<u32>(imm));
	}
	else if (imm < 0 && imm >= -4095)
	{
		oakAsm->SUB(addr_w, addr_w, static_cast<u32>(-imm));
	}
	else if (imm != 0)
	{
		oakAsm->MOV(OAK_WSCRATCH, static_cast<u32>(imm));
		oakAsm->ADD(addr_w, addr_w, OAK_WSCRATCH);
	}
	oakAsm->AND(addr_w, addr_w, 0x3ff);
	oakLoad64(oakXRegister(VU_HOST_T2),
		mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Mem))));
	recEndOaknutEmit();
	return true;
}

static void mVU_makeRegisterMemoryAddress_oaknut(mP, int addr, int vi, s32 byte_offset = 0)
{
	if (mVU_tryConstantMemoryAddress_oaknut(mVU, recPass, vi, 0, byte_offset))
		return;

	mVU_moveVIToAddressGPR_oaknut(mVU, addr, vi);
	mVUaddrFix_oaknut(mVU, addr);
	recBeginOaknutEmit();
	oakLoad64(oakXRegister(VU_HOST_T2),
		mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Mem))));
	mVU_addPointerOffset_oaknut(oakXRegister(VU_HOST_T2), byte_offset);
	oakAsm->ADD(oakXRegister(VU_HOST_T2), oakXRegister(VU_HOST_T2), oakXRegister(addr));
	recEndOaknutEmit();
}

static void mVU_storeWordLanes_oaknut(oak::WReg src, oak::XReg base, bool write_x, bool write_y, bool write_z, bool write_w)
{
	oakAsm->UXTH(OAK_WSCRATCH, src);
	if (write_x) oakStore32(OAK_WSCRATCH, {base, 0});
	if (write_y) oakStore32(OAK_WSCRATCH, {base, 4});
	if (write_z) oakStore32(OAK_WSCRATCH, {base, 8});
	if (write_w) oakStore32(OAK_WSCRATCH, {base, 12});
}

static void mVU_loadVectorFromAddress_oaknut(int dst, oak::XReg base, int xyzw, s64 byte_offset = 0)
{
	const oak::QReg dst_q = oakQRegister(dst);
	const oak::SReg dst_s = oakSRegister(dst);
	const oak::DReg dst_d = oakDRegister(dst);
	switch (xyzw & 0xf)
	{
		case 8:
			oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
			oakLoadScalar32(dst_s, {base, byte_offset + 0});
			break;
		case 4:
			oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
			oakLoadScalar32(dst_s, {base, byte_offset + 4});
			break;
		case 2:
			oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
			oakLoadScalar32(dst_s, {base, byte_offset + 8});
			break;
		case 1:
			oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
			oakLoadScalar32(dst_s, {base, byte_offset + 12});
			break;
		case 12:
			oakLoadVector64(dst_d, {base, byte_offset});
			break;
		default:
			oakLoad128(dst_q, {base, byte_offset});
			break;
	}
}

static bool mVU_canUseIndexedQwordLoad_oaknut(int xyzw)
{
	switch (xyzw & 0xf)
	{
		case 0:
		case 1:
		case 2:
		case 4:
		case 8:
			return false;
		default:
			return true;
	}
}

static int mVU_singleLaneWordOffset_oaknut(int xyzw)
{
	switch (xyzw & 0xf)
	{
		case 8:
			return 0;
		case 4:
			return 1;
		case 2:
			return 2;
		case 1:
			return 3;
		default:
			return -1;
	}
}

static bool mVU_loadSingleLaneIndexedQword_oaknut(mP, int dst, int addr, int vi, s32 imm, int xyzw)
{
	const int lane_word = mVU_singleLaneWordOffset_oaknut(xyzw);
	if (lane_word < 0)
		return false;
	if (!mVU_makeIndexedQwordMemoryAddress_oaknut(mVU, recPass, addr, vi, imm))
		return false;

	const oak::WReg addr_w = oakWRegister(addr);
	const oak::QReg dst_q = oakQRegister(dst);
	recBeginOaknutEmit();
	oakAsm->EOR(dst_q.B16(), dst_q.B16(), dst_q.B16());
	oakAsm->LSL(addr_w, addr_w, 2);
	if (lane_word != 0)
		oakAsm->ADD(addr_w, addr_w, static_cast<u32>(lane_word));
	oakAsm->LDR(oakSRegister(dst), oakXRegister(VU_HOST_T2), addr_w, oak::util::UXTW, 2);
	recEndOaknutEmit();
	return true;
}

static void mVU_storeVectorLanes_oaknut(int src, oak::XReg base, int xyzw, bool scalarSource = false, s64 byte_offset = 0)
{
	const oak::QReg src_q = oakQRegister(src);
	if ((xyzw & 0xf) == 0xf)
	{
		oakStore128(src_q, {base, byte_offset});
		return;
	}

	if (xyzw & 8)
	{
		oakAsm->UMOV(OAK_WSCRATCH, src_q.Selem()[0]);
		oakStore32(OAK_WSCRATCH, {base, byte_offset + 0});
	}
	if (xyzw & 4)
	{
		oakAsm->UMOV(OAK_WSCRATCH, src_q.Selem()[scalarSource ? 0 : 1]);
		oakStore32(OAK_WSCRATCH, {base, byte_offset + 4});
	}
	if (xyzw & 2)
	{
		oakAsm->UMOV(OAK_WSCRATCH, src_q.Selem()[scalarSource ? 0 : 2]);
		oakStore32(OAK_WSCRATCH, {base, byte_offset + 8});
	}
	if (xyzw & 1)
	{
		oakAsm->UMOV(OAK_WSCRATCH, src_q.Selem()[scalarSource ? 0 : 3]);
		oakStore32(OAK_WSCRATCH, {base, byte_offset + 12});
	}
}

static void mVU_makePostIncrementMemoryAddress_oaknut(mP, int addr, int vi, bool signext)
{
	if (vi)
	{
		const int reg = mVU.regAlloc->allocGPRId(vi, vi, mVUlow.backupVI);
		const oak::WReg addr_w = oakWRegister(addr);
		const oak::WReg reg_w = oakWRegister(reg);
		recBeginOaknutEmit();
		if (signext)
			oakAsm->SXTH(addr_w, reg_w);
		else
			oakAsm->UXTH(addr_w, reg_w);
		oakAsm->ADD(reg_w, reg_w, 1);
		recEndOaknutEmit();
		mVU_normalizeVIWrite_oaknut(reg);
		mVU.regAlloc->clearNeeded(reg);
	}
	else
	{
		recBeginOaknutEmit();
		oakLoad64(oakXRegister(VU_HOST_T2),
			mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Mem))));
		recEndOaknutEmit();
		return;
	}

	mVUaddrFix_oaknut(mVU, addr);
	recBeginOaknutEmit();
	oakLoad64(oakXRegister(VU_HOST_T2),
		mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Mem))));
	oakAsm->ADD(oakXRegister(VU_HOST_T2), oakXRegister(VU_HOST_T2), oakXRegister(addr));
	recEndOaknutEmit();
}

static void mVU_makePreDecrementMemoryAddress_oaknut(mP, int addr, int vi, bool signext)
{
	if (vi || isVU0)
	{
		const oak::WReg addr_w = oakWRegister(addr);
		if (vi)
		{
			const int reg = mVU.regAlloc->allocGPRId(vi, vi, mVUlow.backupVI);
			const oak::WReg reg_w = oakWRegister(reg);
			recBeginOaknutEmit();
			oakAsm->SUB(reg_w, reg_w, 1);
			if (signext)
				oakAsm->SXTH(addr_w, reg_w);
			else
				oakAsm->UXTH(addr_w, reg_w);
			recEndOaknutEmit();
			mVU_normalizeVIWrite_oaknut(reg);
			mVU.regAlloc->clearNeeded(reg);
		}
		else
		{
			recBeginOaknutEmit();
			// VI0 is immutable, so pre-decrement addressing still resolves to zero.
			oakAsm->EOR(addr_w, addr_w, addr_w);
			recEndOaknutEmit();
		}

		mVUaddrFix_oaknut(mVU, addr);
		recBeginOaknutEmit();
		oakLoad64(oakXRegister(VU_HOST_T2),
			mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[mVU.index].Mem))));
		oakAsm->ADD(oakXRegister(VU_HOST_T2), oakXRegister(VU_HOST_T2), oakXRegister(addr));
		recEndOaknutEmit();
	}
	else
	{
		mVU_makeRegisterMemoryAddress_oaknut(mVU, recPass, addr, 0);
	}
}

static void mVU_postIncrementVI_oaknut(mP, int vi)
{
	if (!vi)
		return;

	const int reg = mVU.regAlloc->allocGPRId(vi, vi, mVUlow.backupVI);
	recBeginOaknutEmit();
	oakAsm->ADD(oakWRegister(reg), oakWRegister(reg), 1);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(reg);
	mVU.regAlloc->clearNeeded(reg);
}

static void mVU_preDecrementVI_oaknut(mP, int vi)
{
	if (!vi)
		return;

	const int reg = mVU.regAlloc->allocGPRId(vi, vi, mVUlow.backupVI);
	recBeginOaknutEmit();
	oakAsm->SUB(oakWRegister(reg), oakWRegister(reg), 1);
	recEndOaknutEmit();
	mVU_normalizeVIWrite_oaknut(reg);
	mVU.regAlloc->clearNeeded(reg);
}

//------------------------------------------------------------------
// ILW/ILWR
//------------------------------------------------------------------

static void mVU_ILW_direct_emit_oaknut(mP)
{
	mVU_makeMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _Is_, _Imm11_, 0);
	const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	recBeginOaknutEmit();
	oakLoad16(oakWRegister(regT), {oakXRegister(VU_HOST_T2), static_cast<s64>(offsetSS)});
	recEndOaknutEmit();
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_ILW_emit(mP)
{
	pass1
	{
		if (!_It_)
			mVUlow.isNOP = true;

		analyzeVIreg1(mVU, _Is_, mVUlow.VI_read[0]);
		analyzeVIreg2(mVU, _It_, mVUlow.VI_write, 4);
	}
	pass2
	{
		mVU_ILW_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ILW.%s vi%02d, vi%02d + %d", _XYZW_String, _Ft_, _Fs_, _Imm11_); }
}

static void mVU_ILWR_direct_emit_oaknut(mP)
{
	mVU_makeRegisterMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _Is_, 0);
	const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	recBeginOaknutEmit();
	oakLoad16(oakWRegister(regT), {oakXRegister(VU_HOST_T2), static_cast<s64>(offsetSS)});
	recEndOaknutEmit();
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_ILWR_emit(mP)
{
	pass1
	{
		if (!_It_)
			mVUlow.isNOP = true;

		analyzeVIreg1(mVU, _Is_, mVUlow.VI_read[0]);
		analyzeVIreg2(mVU, _It_, mVUlow.VI_write, 4);
	}
	pass2
	{
		mVU_ILWR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ILWR.%s vi%02d, vi%02d", _XYZW_String, _Ft_, _Fs_); }
}

//------------------------------------------------------------------
// ISW/ISWR
//------------------------------------------------------------------

static void mVU_ISW_direct_emit_oaknut(mP)
{
	mVU_makeMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _Is_, _Imm11_, 0);
	const int regT = mVU.regAlloc->allocGPRId(_It_, -1, false, true);
	recBeginOaknutEmit();
	mVU_storeWordLanes_oaknut(oakWRegister(regT), oakXRegister(VU_HOST_T2), _X, _Y, _Z, _W);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_ISW_emit(mP)
{
	pass1
	{
		mVUlow.isMemWrite = true;
		analyzeVIreg1(mVU, _Is_, mVUlow.VI_read[0]);
		analyzeVIreg1(mVU, _It_, mVUlow.VI_read[1]);
	}
	pass2
	{
		mVU_ISW_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ISW.%s vi%02d, vi%02d + %d", _XYZW_String, _Ft_, _Fs_, _Imm11_); }
}

static void mVU_ISWR_direct_emit_oaknut(mP)
{
	mVU_makeRegisterMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _Is_);
	const int regT = mVU.regAlloc->allocGPRId(_It_, -1, false, true);
	recBeginOaknutEmit();
	mVU_storeWordLanes_oaknut(oakWRegister(regT), oakXRegister(VU_HOST_T2), _X, _Y, _Z, _W);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_ISWR_emit(mP)
{
	pass1
	{
		mVUlow.isMemWrite = true;
		analyzeVIreg1(mVU, _Is_, mVUlow.VI_read[0]);
		analyzeVIreg1(mVU, _It_, mVUlow.VI_read[1]);
	}
	pass2
	{
		mVU_ISWR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("ISWR.%s vi%02d, vi%02d", _XYZW_String, _Ft_, _Fs_); }
}


//------------------------------------------------------------------
// LQ/LQD/LQI
//------------------------------------------------------------------

static void mVU_LQ_direct_emit_oaknut(mP)
{
	if (mVUlow.noWriteVF || !_Ft_ || !_X_Y_Z_W)
		return;

	const int Ft = mVU.regAlloc->allocRegId(-1, _Ft_, _X_Y_Z_W);
	s64 constant_offset = 0;
	if (mVU_tryConstantMemoryBase_oaknut(mVU, recPass, _Is_, _Imm11_, 0, constant_offset))
	{
		recBeginOaknutEmit();
		mVU_loadVectorFromAddress_oaknut(Ft, oakXRegister(VU_HOST_T2), _X_Y_Z_W, constant_offset);
		recEndOaknutEmit();
	}
	else if (mVU_canUseIndexedQwordLoad_oaknut(_X_Y_Z_W) && mVU_makeIndexedQwordMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _Is_, _Imm11_))
	{
		recBeginOaknutEmit();
		oakAsm->LDR(oakQRegister(Ft), oakXRegister(VU_HOST_T2), oakWRegister(VU_HOST_T1), oak::util::UXTW, 4);
		recEndOaknutEmit();
	}
	else if (mVU_loadSingleLaneIndexedQword_oaknut(mVU, recPass, Ft, VU_HOST_T1, _Is_, _Imm11_, _X_Y_Z_W))
	{
	}
	else
	{
		mVU_makeMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _Is_, _Imm11_, 0);
		recBeginOaknutEmit();
		mVU_loadVectorFromAddress_oaknut(Ft, oakXRegister(VU_HOST_T2), _X_Y_Z_W);
		recEndOaknutEmit();
	}
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_LQ_emit(mP)
{
	pass1 { mVUanalyzeLQ(mVU, _Ft_, _Is_, _X_Y_Z_W, false); }
	pass2
	{
		mVU_LQ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("LQ.%s vf%02d, vi%02d + %d", _XYZW_String, _Ft_, _Fs_, _Imm11_); }
}

static void mVU_LQD_direct_emit_oaknut(mP)
{
	if (mVUlow.noWriteVF || !_Ft_ || !_X_Y_Z_W)
	{
		mVU_preDecrementVI_oaknut(mVU, recPass, _Is_);
		return;
	}

	mVU_makePreDecrementMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _Is_, true);
	const int Ft = mVU.regAlloc->allocRegId(-1, _Ft_, _X_Y_Z_W);
	recBeginOaknutEmit();
	mVU_loadVectorFromAddress_oaknut(Ft, oakXRegister(VU_HOST_T2), _X_Y_Z_W);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_LQD_emit(mP)
{
	pass1 { mVUanalyzeLQ(mVU, _Ft_, _Is_, _X_Y_Z_W, true); }
	pass2
	{
		mVU_LQD_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("LQD.%s vf%02d, --vi%02d", _XYZW_String, _Ft_, _Is_); }
}

static void mVU_LQI_direct_emit_oaknut(mP)
{
	if (mVUlow.noWriteVF || !_Ft_ || !_X_Y_Z_W)
	{
		mVU_postIncrementVI_oaknut(mVU, recPass, _Is_);
		return;
	}

	mVU_makePostIncrementMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _Is_, true);
	const int Ft = mVU.regAlloc->allocRegId(-1, _Ft_, _X_Y_Z_W);
	recBeginOaknutEmit();
	mVU_loadVectorFromAddress_oaknut(Ft, oakXRegister(VU_HOST_T2), _X_Y_Z_W);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Ft);
}

static void mVU_LQI_emit(mP)
{
	pass1 { mVUanalyzeLQ(mVU, _Ft_, _Is_, _X_Y_Z_W, true); }
	pass2
	{
		mVU_LQI_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("LQI.%s vf%02d, vi%02d++", _XYZW_String, _Ft_, _Fs_); }
}


//------------------------------------------------------------------
// SQ/SQD/SQI
//------------------------------------------------------------------

static void mVU_SQ_direct_emit_oaknut(mP)
{
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _XYZW_PS ? -1 : 0, _X_Y_Z_W);
	s64 constant_offset = 0;
	if (mVU_tryConstantMemoryBase_oaknut(mVU, recPass, _It_, _Imm11_, 0, constant_offset))
	{
		recBeginOaknutEmit();
		mVU_storeVectorLanes_oaknut(Fs, oakXRegister(VU_HOST_T2), _X_Y_Z_W, _XYZW_SS, constant_offset);
		recEndOaknutEmit();
	}
	else if ((_X_Y_Z_W == 0xf) && !_XYZW_SS && mVU_makeIndexedQwordMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _It_, _Imm11_))
	{
		recBeginOaknutEmit();
		oakAsm->STR(oakQRegister(Fs), oakXRegister(VU_HOST_T2), oakWRegister(VU_HOST_T1), oak::util::UXTW, 4);
		recEndOaknutEmit();
	}
	else
	{
		mVU_makeMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _It_, _Imm11_, 0);
		recBeginOaknutEmit();
		mVU_storeVectorLanes_oaknut(Fs, oakXRegister(VU_HOST_T2), _X_Y_Z_W, _XYZW_SS);
		recEndOaknutEmit();
	}
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_SQ_emit(mP)
{
	pass1 { mVUanalyzeSQ(mVU, _Fs_, _It_, false); }
	pass2
	{
		mVU_SQ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("SQ.%s vf%02d, vi%02d + %d", _XYZW_String, _Fs_, _Ft_, _Imm11_); }
}

static void mVU_SQD_direct_emit_oaknut(mP)
{
	mVU_makePreDecrementMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _It_, false);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _XYZW_PS ? -1 : 0, _X_Y_Z_W);
	recBeginOaknutEmit();
	mVU_storeVectorLanes_oaknut(Fs, oakXRegister(VU_HOST_T2), _X_Y_Z_W, _XYZW_SS);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_SQD_emit(mP)
{
	pass1 { mVUanalyzeSQ(mVU, _Fs_, _It_, true); }
	pass2
	{
		mVU_SQD_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("SQD.%s vf%02d, --vi%02d", _XYZW_String, _Fs_, _Ft_); }
}

static void mVU_SQI_direct_emit_oaknut(mP)
{
	mVU_makePostIncrementMemoryAddress_oaknut(mVU, recPass, VU_HOST_T1, _It_, false);
	const int Fs = mVU.regAlloc->allocRegId(_Fs_, _XYZW_PS ? -1 : 0, _X_Y_Z_W);
	recBeginOaknutEmit();
	mVU_storeVectorLanes_oaknut(Fs, oakXRegister(VU_HOST_T2), _X_Y_Z_W, _XYZW_SS);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeededXmmId(Fs);
}

static void mVU_SQI_emit(mP)
{
	pass1 { mVUanalyzeSQ(mVU, _Fs_, _It_, true); }
	pass2
	{
		mVU_SQI_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("SQI.%s vf%02d, vi%02d++", _XYZW_String, _Fs_, _Ft_); }
}


//------------------------------------------------------------------
// RINIT/RGET/RNEXT/RXOR
//------------------------------------------------------------------

static OakMemOperand mVU_R_mem_oaknut(mV)
{
	return mVUAllocOakCpuMem(static_cast<s64>(mVU.index ?
		offsetof(cpuRegistersPack, vuRegs[1].VI[REG_R].UL) :
		offsetof(cpuRegistersPack, vuRegs[0].VI[REG_R].UL)));
}

static void mVU_RINIT_direct_emit_oaknut(mP)
{
	if (_Fs_ || (_Fsf_ == 3))
	{
		const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
		recBeginOaknutEmit();
		oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(Fs));
		oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x007fffff);
		oakAsm->ORR(OAK_WSCRATCH, OAK_WSCRATCH, 0x3f800000);
		oakStore32(OAK_WSCRATCH, mVU_R_mem_oaknut(mVU));
		recEndOaknutEmit();
		mVU.regAlloc->clearNeededXmmId(Fs);
	}
	else
	{
		recBeginOaknutEmit();
		oakAsm->MOV(OAK_WSCRATCH, 0x3f800000);
		oakStore32(OAK_WSCRATCH, mVU_R_mem_oaknut(mVU));
		recEndOaknutEmit();
	}
}

static void mVU_RINIT_emit(mP)
{
	pass1 { mVUanalyzeR1(mVU, _Fs_, _Fsf_); }
	pass2
	{
		mVU_RINIT_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("RINIT R, vf%02d%s", _Fs_, _Fsf_String); }
}

static void mVU_RGET_direct_emit_oaknut(mP)
{
	int Ft = -1;
	if (!mVUlow.noWriteVF)
		Ft = mVU.regAlloc->allocRegId(-1, _Ft_, _X_Y_Z_W);

	recBeginOaknutEmit();
	oakLoad32(OAK_WSCRATCH, mVU_R_mem_oaknut(mVU));
	if (!mVUlow.noWriteVF)
	{
		const oak::QReg dst = oakQRegister(Ft);
		oakAsm->EOR(dst.B16(), dst.B16(), dst.B16());
		oakAsm->FMOV(oakSRegister(Ft), OAK_WSCRATCH);
		if (!_XYZW_SS)
			oakAsm->DUP(dst.S4(), dst.Selem()[0]);
		recEndOaknutEmit();
		mVU.regAlloc->clearNeededXmmId(Ft);
	}
	else
	{
		recEndOaknutEmit();
	}
}

static void mVU_RGET_emit(mP)
{
	pass1 { mVUanalyzeR2(mVU, _Ft_, true); }
	pass2
	{
		mVU_RGET_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("RGET.%s vf%02d, R", _XYZW_String, _Ft_); }
}

static void mVU_RNEXT_direct_emit_oaknut(mP)
{
	if (!_Ft_)
	{
		return;
	}

	const int temp3 = mVU.regAlloc->allocGPRId();
	const oak::WReg rnd = oakWRegister(temp3);
	int Ft = -1;
	if (!mVUlow.noWriteVF)
		Ft = mVU.regAlloc->allocRegId(-1, _Ft_, _X_Y_Z_W);

	recBeginOaknutEmit();
	oakLoad32(rnd, mVU_R_mem_oaknut(mVU));
	oakAsm->EOR(OAK_WSCRATCH, rnd, rnd, oak::LogShift::LSR, 18);
	oakAsm->LSL(rnd, rnd, 1);
	oakAsm->BFXIL(rnd, OAK_WSCRATCH, 4, 1);
	oakAsm->AND(rnd, rnd, 0x007fffff);
	oakAsm->ORR(rnd, rnd, 0x3f800000);
	oakStore32(rnd, mVU_R_mem_oaknut(mVU));
	if (!mVUlow.noWriteVF)
	{
		const oak::QReg dst = oakQRegister(Ft);
		oakAsm->EOR(dst.B16(), dst.B16(), dst.B16());
		oakAsm->FMOV(oakSRegister(Ft), rnd);
		if (!_XYZW_SS)
			oakAsm->DUP(dst.S4(), dst.Selem()[0]);
		recEndOaknutEmit();
		mVU.regAlloc->clearNeededXmmId(Ft);
	}
	else
	{
		recEndOaknutEmit();
	}

	mVU.regAlloc->clearNeeded(temp3);
}

static void mVU_RNEXT_emit(mP)
{
	pass1
	{
		if (!_Ft_)
		{
			mVUlow.isNOP = 1;
			return;
		}
		mVUanalyzeR2(mVU, _Ft_, false);
	}
	pass2
	{
		mVU_RNEXT_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("RNEXT.%s vf%02d, R", _XYZW_String, _Ft_); }
}

static void mVU_RXOR_direct_emit_oaknut(mP)
{
	if (_Fs_ || (_Fsf_ == 3))
	{
		const int Fs = mVU.regAlloc->allocRegId(_Fs_, 0, (1 << (3 - _Fsf_)));
		recBeginOaknutEmit();
		oakAsm->FMOV(OAK_WSCRATCH, oakSRegister(Fs));
		oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x7fffff);
		oakLoad32(OAK_WSCRATCH2, mVU_R_mem_oaknut(mVU));
		oakAsm->EOR(OAK_WSCRATCH2, OAK_WSCRATCH2, OAK_WSCRATCH);
		oakAsm->AND(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x007fffff);
		oakAsm->ORR(OAK_WSCRATCH2, OAK_WSCRATCH2, 0x3f800000);
		oakStore32(OAK_WSCRATCH2, mVU_R_mem_oaknut(mVU));
		recEndOaknutEmit();
		mVU.regAlloc->clearNeededXmmId(Fs);
	}
	else
	{
		recBeginOaknutEmit();
		oakLoad32(OAK_WSCRATCH, mVU_R_mem_oaknut(mVU));
		oakAsm->AND(OAK_WSCRATCH, OAK_WSCRATCH, 0x007fffff);
		oakAsm->ORR(OAK_WSCRATCH, OAK_WSCRATCH, 0x3f800000);
		oakStore32(OAK_WSCRATCH, mVU_R_mem_oaknut(mVU));
		recEndOaknutEmit();
	}
}

static void mVU_RXOR_emit(mP)
{
	pass1 { mVUanalyzeR1(mVU, _Fs_, _Fsf_); }
	pass2
	{
		mVU_RXOR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("RXOR R, vf%02d%s", _Fs_, _Fsf_String); }
}


//------------------------------------------------------------------
// WaitP/WaitQ
//------------------------------------------------------------------

static void mVU_WAITP_direct_emit_oaknut(mP)
{
}

static void mVU_WAITP_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUstall = std::max(mVUstall, (u8)((mVUregs.p) ? (mVUregs.p - 1) : 0));
	}
	pass2 { mVU_WAITP_direct_emit_oaknut(mVU, recPass); }
	pass3 { mVUlog("WAITP"); }
}

static void mVU_WAITQ_direct_emit_oaknut(mP)
{
}

static void mVU_WAITQ_emit(mP)
{
	pass1 { mVUstall = std::max(mVUstall, mVUregs.q); }
	pass2 { mVU_WAITQ_direct_emit_oaknut(mVU, recPass); }
	pass3 { mVUlog("WAITQ"); }
}


//------------------------------------------------------------------
// XTOP/XITOP
//------------------------------------------------------------------

static void mVU_XTOP_direct_emit_oaknut(mP)
{
	const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	const oak::WReg dst = oakWRegister(regT);
	recBeginOaknutEmit();
	if (mVU.index && THREAD_VU1)
	{
		oakLoad16(dst, mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, vu1Thread.vifRegs.top))));
	}
	else
	{
		const VIFregisters& vif = mVU.index ? vif1Regs : vif0Regs;
		oakMoveAddressToReg(OAK_XSCRATCH, &vif.top);
		oakAsm->LDRH(dst, OAK_XSCRATCH);
	}
	recEndOaknutEmit();
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_XTOP_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}

		if (!_It_)
			mVUlow.isNOP = true;

		analyzeVIreg2(mVU, _It_, mVUlow.VI_write, 1);
	}
	pass2
	{
		mVU_XTOP_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("XTOP vi%02d", _Ft_); }
}

static void mVU_XITOP_direct_emit_oaknut(mP)
{
	const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
	const oak::WReg dst = oakWRegister(regT);
	recBeginOaknutEmit();
	if (mVU.index && THREAD_VU1)
	{
		oakLoad16(dst, mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, vu1Thread.vifRegs.itop))));
	}
	else
	{
		const VIFregisters& vif = mVU.index ? vif1Regs : vif0Regs;
		oakMoveAddressToReg(OAK_XSCRATCH, &vif.itop);
		oakAsm->LDRH(dst, OAK_XSCRATCH);
	}
	oakAsm->AND(dst, dst, isVU1 ? 0x3ff : 0xff);
	recEndOaknutEmit();
	mVU.regAlloc->clearNeeded(regT);
}

static void mVU_XITOP_emit(mP)
{
	pass1
	{
		if (!_It_)
			mVUlow.isNOP = true;

		analyzeVIreg2(mVU, _It_, mVUlow.VI_write, 1);
	}
	pass2
	{
		mVU_XITOP_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("XITOP vi%02d", _Ft_); }
}


//------------------------------------------------------------------
// XGkick
//------------------------------------------------------------------

void mVU_XGKICK_(u32 addr)
{
	addr = (addr & 0x3ff) * 16;
	u32 diff = 0x4000 - addr;
	u32 size = gifUnit.GetGSPacketSize(GIF_PATH_1, g_cpuRegistersPack.vuRegs[1].Mem, addr, ~0u, true);

	if (!size)
		return;

	if (THREAD_VU1)
	{
		const u32 first = std::min<u32>(size, 0x10);
		if (first < size)
			gifUnit.gifPath[GIF_PATH_1].CopyGSPacketData(&g_cpuRegistersPack.vuRegs[1].Mem[addr], first, true);
		else
			gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, &g_cpuRegistersPack.vuRegs[1].Mem[addr], first, true);

		addr = (addr + first) & 0x3FFF;
		diff -= first;
		size -= first;

		if (!size)
			return;
	}

	if (size > diff)
	{
		gifUnit.gifPath[GIF_PATH_1].CopyGSPacketData(&g_cpuRegistersPack.vuRegs[1].Mem[addr], diff, true);
		gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, &g_cpuRegistersPack.vuRegs[1].Mem[0], size - diff, true);
	}
	else
	{
		gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, &g_cpuRegistersPack.vuRegs[1].Mem[addr], size, true);
	}
}

void _vuXGKICKTransfermVU(bool flush)
{
	while (VU1.xgkickenable && (flush || VU1.xgkickcyclecount >= 2))
	{
		u32 transfersize = 0;

		if (VU1.xgkicksizeremaining == 0)
		{
			//VUM_LOG("XGKICK reading new tag from %x", VU1.xgkickaddr);
			u32 size = gifUnit.GetGSPacketSize(GIF_PATH_1, g_cpuRegistersPack.vuRegs[1].Mem, VU1.xgkickaddr, ~0u, flush);
			VU1.xgkicksizeremaining = size & 0xFFFF;
			VU1.xgkickendpacket = size >> 31;
			VU1.xgkickdiff = 0x4000 - VU1.xgkickaddr;

			if (VU1.xgkicksizeremaining == 0)
			{
				//VUM_LOG("Invalid GS packet size returned, cancelling XGKick");
				VU1.xgkickenable = false;
				break;
			}
			//else
				//VUM_LOG("XGKICK New tag size %d bytes EOP %d", VU1.xgkicksizeremaining, VU1.xgkickendpacket);
		}

		if (!flush)
		{
			transfersize = std::min(VU1.xgkicksizeremaining, VU1.xgkickcyclecount * 8);
			transfersize = std::min(transfersize, VU1.xgkickdiff);
		}
		else
		{
			transfersize = VU1.xgkicksizeremaining;
			transfersize = std::min(transfersize, VU1.xgkickdiff);
		}

		//VUM_LOG("XGKICK Transferring %x bytes from %x size %x", transfersize, VU1.xgkickaddr, VU1.xgkicksizeremaining);

		// Would be "nicer" to do the copy until it's all up, however this really screws up PATH3 masking stuff
		// So lets just do it the other way :)
		if (THREAD_VU1)
		{
			if (transfersize < VU1.xgkicksizeremaining)
				gifUnit.gifPath[GIF_PATH_1].CopyGSPacketData(&g_cpuRegistersPack.vuRegs[1].Mem[VU1.xgkickaddr], transfersize, true);
			else
				gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, &g_cpuRegistersPack.vuRegs[1].Mem[VU1.xgkickaddr], transfersize, true);
		}
		else
		{
			gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, &g_cpuRegistersPack.vuRegs[1].Mem[VU1.xgkickaddr], transfersize, true);
		}

		if (flush)
			VU1.cycle += transfersize / 8;

		VU1.xgkickcyclecount -= transfersize / 8;

		VU1.xgkickaddr = (VU1.xgkickaddr + transfersize) & 0x3FFF;
		VU1.xgkicksizeremaining -= transfersize;
		VU1.xgkickdiff = 0x4000 - VU1.xgkickaddr;

		if (VU1.xgkickendpacket && !VU1.xgkicksizeremaining)
		//	VUM_LOG("XGKICK next addr %x left size %x", VU1.xgkickaddr, VU1.xgkicksizeremaining);
		//else
		{
			//VUM_LOG("XGKICK transfer finished");
			VU1.xgkickenable = false;
			// Check if VIF is waiting for the GIF to not be busy
		}
	}
}

static __fi void mVU_XGKICK_backupNeededRegs_oaknut(mV)
{
	mVURegSaveLayout layout = mVUGetRegSaveLayout(mVU, true);
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
}

static __fi void mVU_XGKICK_restoreNeededRegs_oaknut(mV)
{
	mVURegSaveLayout layout = mVUGetRegSaveLayout(mVU, true);
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
}

static __fi void mVU_XGKICK_SYNC_oaknut(mV, bool flush)
{
	mVU.regAlloc->flushCallerSavedRegisters();

	// Add the single cycle remainder after this instruction, some games do the store
	// on the second instruction after the kick and that needs to go through first
	// but that's VERY close..
	recBeginOaknutEmit();
	oak::Label skipxgkick;
	oak::Label needcycles;
	const OakMemOperand enable_mem = mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].xgkickenable)));
	const OakMemOperand count_mem = mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].xgkickcyclecount)));

	oakLoad32(OAK_WSCRATCH, enable_mem);
	oakAsm->TST(OAK_WSCRATCH, 0x1);
	oakAsm->B(oak::Cond::EQ, skipxgkick);

	oakLoad32(OAK_WSCRATCH, count_mem);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, static_cast<u32>(mVUlow.kickcycles - 1));
	oakStore32(OAK_WSCRATCH, count_mem);
	oakAsm->CMP(OAK_WSCRATCH, 2);
	oakAsm->B(oak::Cond::LT, needcycles);

	mVU_XGKICK_backupNeededRegs_oaknut(mVU);
	oakAsm->MOV(OAK_WARG1, flush ? 1 : 0);
	oakEmitCall(reinterpret_cast<void*>(_vuXGKICKTransfermVU));
	mVU_XGKICK_restoreNeededRegs_oaknut(mVU);

	oakAsm->l(needcycles);
	oakLoad32(OAK_WSCRATCH, count_mem);
	oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, 1);
	oakStore32(OAK_WSCRATCH, count_mem);
	oakAsm->l(skipxgkick);
	recEndOaknutEmit();
}

static __fi void mVU_XGKICK_DELAY_oaknut(mV)
{
	mVU.regAlloc->flushCallerSavedRegisters();

	recBeginOaknutEmit();
	mVU_XGKICK_backupNeededRegs_oaknut(mVU);
#if 0 // XGkick Break - ToDo: Change "SomeGifPathValue" to w/e needs to be tested
	xTEST (ptr32[&SomeGifPathValue], 1); // If '1', breaks execution
	xMOV  (ptr32[&mVU.resumePtrXG], (uptr)xGetPtr() + 10 + 6);
	xJcc32(Jcc_NotZero, (uptr)mVU.exitFunctXG - ((uptr)xGetPtr()+6));
#endif
	oakLoad32(OAK_WARG1, mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].VIxgkick))));
	oakEmitCall(reinterpret_cast<void*>(mVU_XGKICK_));
	mVU_XGKICK_restoreNeededRegs_oaknut(mVU);
	recEndOaknutEmit();
}

static void mVU_XGKICK_direct_emit_oaknut(mP)
{
	if (CHECK_XGKICKHACK)
	{
		mVUlow.kickcycles = 99;
		mVU_XGKICK_SYNC_oaknut(mVU, true);
		mVUlow.kickcycles = 0;
	}
	if (mVUinfo.doXGKICK)
	{
		mVU_XGKICK_DELAY_oaknut(mVU);
		mVUinfo.doXGKICK = false;
	}

	const int regS = mVU.regAlloc->allocGPRId(_Is_, -1);
	recBeginOaknutEmit();
	const oak::WReg src = oakWRegister(regS);
	if (!CHECK_XGKICKHACK)
	{
		oakStore32(src, mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].VIxgkick))));
	}
	else
	{
		oakAsm->MOV(OAK_WSCRATCH, 1);
		oakStore32(OAK_WSCRATCH, mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].xgkickenable))));
		oakAsm->EOR(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH);
		oakStore32(OAK_WSCRATCH, mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].xgkickendpacket))));
		oakStore32(OAK_WSCRATCH, mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].xgkicksizeremaining))));
		oakStore32(OAK_WSCRATCH, mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].xgkickcyclecount))));

		oakLoad32(OAK_WSCRATCH, mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].totalCycles))));
		oakLoad32(OAK_WSCRATCH2, mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].cycles))));
		oakAsm->SUB(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
		oakLoad32(OAK_WSCRATCH2, mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].cycle))));
		oakAsm->ADD(OAK_WSCRATCH, OAK_WSCRATCH, OAK_WSCRATCH2);
		oakStore32(OAK_WSCRATCH, mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].xgkicklastcycle))));

		oakAsm->AND(OAK_WSCRATCH, src, 0x3ff);
		oakAsm->LSL(OAK_WSCRATCH, OAK_WSCRATCH, 4);
		oakStore32(OAK_WSCRATCH, mVUAllocOakCpuMem(static_cast<s64>(offsetof(cpuRegistersPack, vuRegs[1].xgkickaddr))));
	}
	recEndOaknutEmit();
	mVU.regAlloc->clearNeeded(regS);
}

static void mVU_XGKICK_emit(mP)
{
	pass1
	{
		if (isVU0)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUanalyzeXGkick(mVU, _Is_, 1);
	}
		pass2
	{
		mVU_XGKICK_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("XGKICK vi%02d", _Fs_); }
}


//------------------------------------------------------------------
// Branches/Jumps
//------------------------------------------------------------------

void setBranchA(mP, int x, int _x_)
{
	bool isBranchDelaySlot = false;

	incPC(-2);
	if (mVUlow.branch)
		isBranchDelaySlot = true;
	incPC(2);

	pass1
	{
		if (_Imm11_ == 1 && !_x_ && !isBranchDelaySlot)
		{
			mVUlow.isNOP = true;
			return;
		}
		mVUbranch     = x;
		mVUlow.branch = x;
	}
	pass2 { if (_Imm11_ == 1 && !_x_ && !isBranchDelaySlot) { return; } mVUbranch = x; }
	pass3 { mVUbranch = x; }
	pass4 { if (_Imm11_ == 1 && !_x_ && !isBranchDelaySlot) { return; } mVUbranch = x; }
}

static __fi OakMemOperand mVU_branch_oaknut(mV)
{
	return mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].branch)));
}

static __fi OakMemOperand mVU_badBranch_oaknut(mV)
{
	return mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].badBranch)));
}

static __fi OakMemOperand mVU_evilBranch_oaknut(mV)
{
	return mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].evilBranch)));
}

static __fi OakMemOperand mVU_evilevilBranch_oaknut(mV)
{
	return mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].evilevilBranch)));
}

static __fi OakMemOperand mVU_VIbackup_oaknut(mV)
{
	return mVUAllocOakMvuMem(static_cast<s64>(offsetof(vuRegistersPack, microVU[mVU.index].VIbackup)));
}

static __fi void mVU_storeImm32_oaknut(OakMemOperand mem, u32 value)
{
	oakAsm->MOV(OAK_WSCRATCH, value);
	oakStore32(OAK_WSCRATCH, mem);
}

static __fi void mVU_storeBranchAddr_oaknut(mV, OakMemOperand mem)
{
	mVU_storeImm32_oaknut(mem, branchAddr(mVU));
}

static __fi void mVU_loadVIBackupToGPR_oaknut(mV, int dst);
static __fi void mVU_storeGPRToBranchMem_oaknut(int src, OakMemOperand mem);
static __fi void mVU_loadBranchLink_oaknut(mV, int dst, bool from_evil);
static __fi void mVU_writeBranchLinkImm_oaknut(mV, int dst);
static __fi void mVU_loadBranchSource_oaknut(mV, int dst, int vi, bool from_backup);
static __fi void mVU_xorBranchSource_oaknut(mV, int dst, int vi, bool from_backup);

static __fi void mVU_loadVIBackupToGPR_oaknut(mV, int dst)
{
	recBeginOaknutEmit();
	oakLoad32(oakWRegister(dst), mVU_VIbackup_oaknut(mVU));
	recEndOaknutEmit();
}

static __fi void mVU_storeGPRToBranchMem_oaknut(int src, OakMemOperand mem)
{
	oakStore32(oakWRegister(src), mem);
}

static __fi void mVU_cmpBranchSourceZero_oaknut(int src)
{
	oakAsm->SXTH(OAK_WSCRATCH, oakWRegister(src));
	oakAsm->CMP(OAK_WSCRATCH, 0);
}

void condEvilBranch_oaknut(mV, oak::Cond jump_cond)
{
	if (mVUlow.badBranch)
	{
		recBeginOaknutEmit();
		mVU_storeGPRToBranchMem_oaknut(VU_HOST_T1, mVU_branch_oaknut(mVU));
		mVU_storeBranchAddr_oaknut(mVU, mVU_badBranch_oaknut(mVU));
		oak::Label cJMP;
		mVU_cmpBranchSourceZero_oaknut(VU_HOST_T1);
		oakAsm->B(jump_cond, cJMP);
		incPC(4);
		mVU_storeImm32_oaknut(mVU_badBranch_oaknut(mVU), xPC);
		incPC(-4);
		oakAsm->l(cJMP);
		recEndOaknutEmit();
		return;
	}
	if (isEvilBlock)
	{
		recBeginOaknutEmit();
		mVU_storeBranchAddr_oaknut(mVU, mVU_evilevilBranch_oaknut(mVU));
		oak::Label cJMP;
		mVU_cmpBranchSourceZero_oaknut(VU_HOST_T1);
		oakAsm->B(jump_cond, cJMP);
		oakLoad32(oakWRegister(VU_HOST_T1), mVU_evilBranch_oaknut(mVU));
		oakAsm->ADD(oakWRegister(VU_HOST_T1), oakWRegister(VU_HOST_T1), 8);
		mVU_storeGPRToBranchMem_oaknut(VU_HOST_T1, mVU_evilevilBranch_oaknut(mVU));
		oakAsm->l(cJMP);
		recEndOaknutEmit();
	}
	else
	{
		recBeginOaknutEmit();
		mVU_storeBranchAddr_oaknut(mVU, mVU_evilBranch_oaknut(mVU));
		oak::Label cJMP;
		mVU_cmpBranchSourceZero_oaknut(VU_HOST_T1);
		oakAsm->B(jump_cond, cJMP);
		oakLoad32(oakWRegister(VU_HOST_T1), mVU_badBranch_oaknut(mVU));
		oakAsm->ADD(oakWRegister(VU_HOST_T1), oakWRegister(VU_HOST_T1), 8);
		mVU_storeGPRToBranchMem_oaknut(VU_HOST_T1, mVU_evilBranch_oaknut(mVU));
		oakAsm->l(cJMP);
		recEndOaknutEmit();
		incPC(-2);
		if (mVUlow.branch >= 9)
			DevCon.Warning("Conditional in JALR/JR delay slot - If game broken report to PCSX2 Team");
		incPC(2);
	}
}

static __fi void mVU_storeNormBranchTarget_oaknut(mV)
{
	recBeginOaknutEmit();
	if (mVUlow.badBranch)
		mVU_storeBranchAddr_oaknut(mVU, mVU_badBranch_oaknut(mVU));
	if (mVUlow.evilBranch)
	{
		if (isEvilBlock)
			mVU_storeBranchAddr_oaknut(mVU, mVU_evilevilBranch_oaknut(mVU));
		else
			mVU_storeBranchAddr_oaknut(mVU, mVU_evilBranch_oaknut(mVU));
	}
	recEndOaknutEmit();
}

static __fi void mVU_loadBranchLink_oaknut(mV, int dst, bool from_evil)
{
	recBeginOaknutEmit();
	const oak::WReg dst_w = oakWRegister(dst);
	if (from_evil)
		oakLoad32(dst_w, mVU_evilBranch_oaknut(mVU));
	else
		oakLoad32(dst_w, mVU_badBranch_oaknut(mVU));
	oakAsm->ADD(dst_w, dst_w, 8);
	oakAsm->LSR(dst_w, dst_w, 3);
	recEndOaknutEmit();
}

static __fi void mVU_writeBranchLinkImm_oaknut(mV, int dst)
{
	recBeginOaknutEmit();
	oakAsm->MOV(oakWRegister(dst), static_cast<u32>(bSaveAddr));
	recEndOaknutEmit();
}

static void mVU_B_direct_emit_oaknut(mP)
{
	mVU_storeNormBranchTarget_oaknut(mVU);
}

static void mVU_B_emit(mP)
{
	setBranchA(mX, 1, 0);
	pass1 { mVUanalyzeNormBranch(mVU, 0, false); }
	pass2
	{
		mVU_B_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("B [<a href=\"#addr%04x\">%04x</a>]", branchAddr(mVU), branchAddr(mVU)); }
}

static void mVU_BAL_direct_emit_oaknut(mP)
{
	if (!_It_)
	{
		mVU_storeNormBranchTarget_oaknut(mVU);
		return;
	}

	if (!mVUlow.evilBranch)
	{
		const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
		mVU_writeBranchLinkImm_oaknut(mVU, regT);
		mVU.regAlloc->clearNeeded(regT);
	}
	else
	{
		incPC(-2);
		DevCon.Warning("Linking BAL from %s branch taken/not taken target! - If game broken report to PCSX2 Team", branchSTR[mVUlow.branch & 0xf]);
		incPC(2);

		const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
		mVU_loadBranchLink_oaknut(mVU, regT, isEvilBlock);
		mVU.regAlloc->clearNeeded(regT);
	}

	mVU_storeNormBranchTarget_oaknut(mVU);
}

static void mVU_BAL_emit(mP)
{
	setBranchA(mX, 2, _It_);
	pass1 { mVUanalyzeNormBranch(mVU, _It_, true); }
	pass2
	{
		mVU_BAL_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("BAL vi%02d [<a href=\"#addr%04x\">%04x</a>]", _Ft_, branchAddr(mVU), branchAddr(mVU)); }
}

static __fi void mVU_loadBranchSource_oaknut(mV, int dst, int vi, bool from_backup)
{
	if (from_backup)
		mVU_loadVIBackupToGPR_oaknut(mVU, dst);
	else
		mVU_moveVIToGPR_oaknut(mVU, dst, vi);
}

static __fi void mVU_xorBranchSource_oaknut(mV, int dst, int vi, bool from_backup)
{
	if (from_backup)
	{
		recBeginOaknutEmit();
		oakLoad32(OAK_WSCRATCH, mVU_VIbackup_oaknut(mVU));
		oakAsm->EOR(oakWRegister(dst), oakWRegister(dst), OAK_WSCRATCH);
		recEndOaknutEmit();
	}
	else
	{
		const int regT = mVU.regAlloc->allocGPRId(vi);
		recBeginOaknutEmit();
		oakAsm->EOR(oakWRegister(dst), oakWRegister(dst), oakWRegister(regT));
		recEndOaknutEmit();
		mVU.regAlloc->clearNeeded(regT);
	}
}

static __fi void mVU_storeOrCondBranch_oaknut(mV, oak::Cond cond)
{
	if (!(isBadOrEvil))
	{
		recBeginOaknutEmit();
		mVU_storeGPRToBranchMem_oaknut(VU_HOST_T1, mVU_branch_oaknut(mVU));
		recEndOaknutEmit();
	}
	else
	{
		condEvilBranch_oaknut(mVU, cond);
	}
}

static void mVU_IBEQ_direct_emit_oaknut(mP)
{
	mVU_loadBranchSource_oaknut(mVU, VU_HOST_T1, _Is_, mVUlow.memReadIs);
	mVU_xorBranchSource_oaknut(mVU, VU_HOST_T1, _It_, mVUlow.memReadIt);
	mVU_storeOrCondBranch_oaknut(mVU, oak::Cond::EQ);
}

static void mVU_IBEQ_emit(mP)
{
	setBranchA(mX, 3, 0);
	pass1 { mVUanalyzeCondBranch2(mVU, _Is_, _It_); }
	pass2
	{
		mVU_IBEQ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IBEQ vi%02d, vi%02d [<a href=\"#addr%04x\">%04x</a>]", _Ft_, _Fs_, branchAddr(mVU), branchAddr(mVU)); }
}

static void mVU_IBGEZ_direct_emit_oaknut(mP)
{
	mVU_loadBranchSource_oaknut(mVU, VU_HOST_T1, _Is_, mVUlow.memReadIs);
	mVU_storeOrCondBranch_oaknut(mVU, oak::Cond::GE);
}

static void mVU_IBGEZ_emit(mP)
{
	setBranchA(mX, 4, 0);
	pass1 { mVUanalyzeCondBranch1(mVU, _Is_); }
	pass2
	{
		mVU_IBGEZ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IBGEZ vi%02d [<a href=\"#addr%04x\">%04x</a>]", _Fs_, branchAddr(mVU), branchAddr(mVU)); }
}

static void mVU_IBGTZ_direct_emit_oaknut(mP)
{
	mVU_loadBranchSource_oaknut(mVU, VU_HOST_T1, _Is_, mVUlow.memReadIs);
	mVU_storeOrCondBranch_oaknut(mVU, oak::Cond::GT);
}

static void mVU_IBGTZ_emit(mP)
{
	setBranchA(mX, 5, 0);
	pass1 { mVUanalyzeCondBranch1(mVU, _Is_); }
	pass2
	{
		mVU_IBGTZ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IBGTZ vi%02d [<a href=\"#addr%04x\">%04x</a>]", _Fs_, branchAddr(mVU), branchAddr(mVU)); }
}

static void mVU_IBLEZ_direct_emit_oaknut(mP)
{
	mVU_loadBranchSource_oaknut(mVU, VU_HOST_T1, _Is_, mVUlow.memReadIs);
	mVU_storeOrCondBranch_oaknut(mVU, oak::Cond::LE);
}

static void mVU_IBLEZ_emit(mP)
{
	setBranchA(mX, 6, 0);
	pass1 { mVUanalyzeCondBranch1(mVU, _Is_); }
	pass2
	{
		mVU_IBLEZ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IBLEZ vi%02d [<a href=\"#addr%04x\">%04x</a>]", _Fs_, branchAddr(mVU), branchAddr(mVU)); }
}

static void mVU_IBLTZ_direct_emit_oaknut(mP)
{
	mVU_loadBranchSource_oaknut(mVU, VU_HOST_T1, _Is_, mVUlow.memReadIs);
	mVU_storeOrCondBranch_oaknut(mVU, oak::Cond::LT);
}

static void mVU_IBLTZ_emit(mP)
{
	setBranchA(mX, 7, 0);
	pass1 { mVUanalyzeCondBranch1(mVU, _Is_); }
	pass2
	{
		mVU_IBLTZ_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IBLTZ vi%02d [<a href=\"#addr%04x\">%04x</a>]", _Fs_, branchAddr(mVU), branchAddr(mVU)); }
}

static void mVU_IBNE_direct_emit_oaknut(mP)
{
	mVU_loadBranchSource_oaknut(mVU, VU_HOST_T1, _Is_, mVUlow.memReadIs);
	mVU_xorBranchSource_oaknut(mVU, VU_HOST_T1, _It_, mVUlow.memReadIt);
	mVU_storeOrCondBranch_oaknut(mVU, oak::Cond::NE);
}

static void mVU_IBNE_emit(mP)
{
	setBranchA(mX, 8, 0);
	pass1 { mVUanalyzeCondBranch2(mVU, _Is_, _It_); }
	pass2
	{
		mVU_IBNE_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("IBNE vi%02d, vi%02d [<a href=\"#addr%04x\">%04x</a>]", _Ft_, _Fs_, branchAddr(mVU), branchAddr(mVU)); }
}

void normJumpPass2(mV)
{
	if (!mVUlow.constJump.isValid || mVUlow.evilBranch)
	{
		mVU_moveVIToGPR_oaknut(mVU, VU_HOST_T1, _Is_);
		recBeginOaknutEmit();
		oakAsm->LSL(oakWRegister(VU_HOST_T1), oakWRegister(VU_HOST_T1), 3);
		oakAsm->AND(oakWRegister(VU_HOST_T1), oakWRegister(VU_HOST_T1), mVU.microMemSize - 8);

		if (!mVUlow.evilBranch)
		{
			mVU_storeGPRToBranchMem_oaknut(VU_HOST_T1, mVU_branch_oaknut(mVU));
		}
		else
		{
			if (isEvilBlock)
				mVU_storeGPRToBranchMem_oaknut(VU_HOST_T1, mVU_evilevilBranch_oaknut(mVU));
			else
				mVU_storeGPRToBranchMem_oaknut(VU_HOST_T1, mVU_evilBranch_oaknut(mVU));
		}
		//If delay slot is conditional, it uses badBranch to go to its target
		if (mVUlow.badBranch)
		{
			mVU_storeGPRToBranchMem_oaknut(VU_HOST_T1, mVU_badBranch_oaknut(mVU));
		}
		recEndOaknutEmit();
	}
}

static void mVU_JR_direct_emit_oaknut(mP)
{
	normJumpPass2(mVU);
}

static void mVU_JR_emit(mP)
{
	mVUbranch = 9;
	pass1 { mVUanalyzeJump(mVU, _Is_, 0, false); }
	pass2
	{
		mVU_JR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("JR [vi%02d]", _Fs_); }
}

static void mVU_JALR_direct_emit_oaknut(mP)
{
	normJumpPass2(mVU);
	if (!_It_)
	{
		return;
	}

	if (!mVUlow.evilBranch)
	{
		const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
		mVU_writeBranchLinkImm_oaknut(mVU, regT);
		mVU.regAlloc->clearNeeded(regT);
	}
	if (mVUlow.evilBranch)
	{
		const int regT = mVU.regAlloc->allocGPRId(-1, _It_, mVUlow.backupVI);
		if (isEvilBlock)
		{
			mVU_loadBranchLink_oaknut(mVU, regT, true);
		}
		else
		{
			incPC(-2);
			DevCon.Warning("Linking JALR from %s branch taken/not taken target! - If game broken report to PCSX2 Team", branchSTR[mVUlow.branch & 0xf]);
			incPC(2);
			mVU_loadBranchLink_oaknut(mVU, regT, false);
		}
		mVU.regAlloc->clearNeeded(regT);
	}
}

static void mVU_JALR_emit(mP)
{
	mVUbranch = 10;
	pass1 { mVUanalyzeJump(mVU, _Is_, _It_, 1); }
	pass2
	{
		mVU_JALR_direct_emit_oaknut(mVU, recPass);
	}
	pass3 { mVUlog("JALR vi%02d, [vi%02d]", _Ft_, _Fs_); }
}

