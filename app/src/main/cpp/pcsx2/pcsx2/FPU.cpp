// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "Config.h"

#include <cmath>
#include <cstring>

// Helper Macros
//****************************************************************

// IEEE 754 Values
#define PosInfinity 0x7f800000
#define NegInfinity 0xff800000
#define posFmax 0x7F7FFFFF
#define negFmax 0xFF7FFFFF


/*	Used in compare function to compensate for differences between IEEE 754 and the FPU.
	Setting it to ~0x00000000 = Compares Exact Value. (comment out this macro for faster Exact Compare method)
	Setting it to ~0x00000001 = Discards the least significant bit when comparing.
	Setting it to ~0x00000003 = Discards the least 2 significant bits when comparing... etc..  */
//#define comparePrecision ~0x00000001

// Operands
#define _Ft_         ( ( cpuRegs.code >> 16 ) & 0x1F )
#define _Fs_         ( ( cpuRegs.code >> 11 ) & 0x1F )
#define _Fd_         ( ( cpuRegs.code >>  6 ) & 0x1F )

// Floats
#define _FtValf_     fpuRegs.fpr[ _Ft_ ].f
#define _FsValf_     fpuRegs.fpr[ _Fs_ ].f
#define _FdValf_     fpuRegs.fpr[ _Fd_ ].f
#define _FAValf_     fpuRegs.ACC.f

// U32's
#define _FtValUl_    fpuRegs.fpr[ _Ft_ ].UL
#define _FsValUl_    fpuRegs.fpr[ _Fs_ ].UL
#define _FdValUl_    fpuRegs.fpr[ _Fd_ ].UL
#define _FAValUl_    fpuRegs.ACC.UL

// S32's - useful for ensuring sign extension when needed.
#define _FtValSl_    fpuRegs.fpr[ _Ft_ ].SL
#define _FsValSl_    fpuRegs.fpr[ _Fs_ ].SL
#define _FdValSl_    fpuRegs.fpr[ _Fd_ ].SL
#define _FAValSl_    fpuRegs.ACC.SL

// FPU Control Reg (FCR31)
#define _ContVal_    fpuRegs.fprc[ 31 ]

// FCR31 Flags
#define FPUflagC	0X00800000
#define FPUflagI	0X00020000
#define FPUflagD	0X00010000
#define FPUflagO	0X00008000
#define FPUflagU	0X00004000
#define FPUflagSI	0X00000040
#define FPUflagSD	0X00000020
#define FPUflagSO	0X00000010
#define FPUflagSU	0X00000008

//****************************************************************

// If we have an infinity value, then Overflow has occured.
bool checkOverflow(u32& xReg, u32 cFlagsToSet)
{
	if ((xReg & ~0x80000000) == PosInfinity) {
		/*Console.Warning( "FPU OVERFLOW!: Changing to +/-Fmax!!!!!!!!!!!!\n" );*/
		xReg = (xReg & 0x80000000) | posFmax;
		_ContVal_ |= (cFlagsToSet);
		return true;
	}
	else if (cFlagsToSet & FPUflagO)
		_ContVal_ &= ~FPUflagO;

	return false;
}

// If we have a denormal value, then Underflow has occured.
bool checkUnderflow(u32& xReg, u32 cFlagsToSet) {
	if ( ( (xReg & 0x7F800000) == 0 ) && ( (xReg & 0x007FFFFF) != 0 ) ) {
		/*Console.Warning( "FPU UNDERFLOW!: Changing to +/-0!!!!!!!!!!!!\n" );*/
		xReg &= 0x80000000;
		_ContVal_ |= (cFlagsToSet);
		return true;
	}
	else if (cFlagsToSet & FPUflagU)
		_ContVal_ &= ~FPUflagU;

	return false;
}

__fi u32 fp_max(u32 a, u32 b)
{
	return ((s32)a < 0 && (s32)b < 0) ? std::min<s32>(a, b) : std::max<s32>(a, b);
}

__fi u32 fp_min(u32 a, u32 b)
{
	return ((s32)a < 0 && (s32)b < 0) ? std::max<s32>(a, b) : std::min<s32>(a, b);
}

// x86 JIT parity helpers (iFPU.cpp fpuFloat/fpuFloat2/fpuFloat3 and MAXSS/MINSS semantics).
// The interpreter follows the x86 recompiler for ABS/NEG/MAX/MIN/SQRT/RSQRT/CFC1 so every
// execution engine agrees: no O/U clearing, clamp-mode dependent Inf/NaN handling.
__fi u32 fpuClampSign(u32 x) // fpuFloat3: +-Inf/NaN -> +-fMax by sign bit
{
	if ((s32)x > (s32)posFmax)
		x = posFmax;
	if (x > 0xff7fffffu)
		x = 0xff7fffffu;
	return x;
}

__fi u32 fpuClampFloat(u32 x) // fpuFloat: any NaN -> +fMax, +-Inf -> +-fMax (CHECK_FPU_OVERFLOW only)
{
	if (!CHECK_FPU_OVERFLOW)
		return x;
	if ((x & 0x7fffffffu) > 0x7f800000u)
		return posFmax;
	if ((x & 0x7fffffffu) == 0x7f800000u)
		return (x & 0x80000000u) | posFmax;
	return x;
}

__fi u32 fpuSseDaz(u32 x) // denormal source operands are treated as zero
{
	return (x & 0x7f800000u) ? x : (x & 0x80000000u);
}

__fi bool fpuSseNaN(u32 x) { return (x & 0x7fffffffu) > 0x7f800000u; }

__fi s32 fpuSseKey(u32 x) // monotonic integer key for non-NaN floats
{
	return (s32)x < 0 ? -(s32)(x & 0x7fffffffu) : (s32)x;
}

// MAXSS/MINSS(a, b): NaN operand or both zero -> b.
__fi u32 fpuSseMaxMin(u32 a, u32 b, bool isMax)
{
	if (fpuSseNaN(a) || fpuSseNaN(b))
		return b;
	a = fpuSseDaz(a);
	b = fpuSseDaz(b);
	if (!(a & 0x7fffffffu) && !(b & 0x7fffffffu))
		return b;
	const s32 ka = fpuSseKey(a), kb = fpuSseKey(b);
	return (isMax ? ka > kb : ka < kb) ? a : b;
}

/*	Checks if Divide by Zero will occur. (z/y = x)
	cFlagsToSet1 = Flags to set if (z != 0)
	cFlagsToSet2 = Flags to set if (z == 0)
	( Denormals are counted as "0" )
*/
bool checkDivideByZero(u32& xReg, u32 yDivisorReg, u32 zDividendReg, u32 cFlagsToSet1, u32 cFlagsToSet2) {

	if ( (yDivisorReg & 0x7F800000) == 0 ) {
		_ContVal_ |= ( (zDividendReg & 0x7F800000) == 0 ) ? cFlagsToSet2 : cFlagsToSet1;
		xReg = ( (yDivisorReg ^ zDividendReg) & 0x80000000 ) | posFmax;
		return true;
	}

	return false;
}

/*	Clears the "Cause Flags" of the Control/Status Reg
	The "EE Core Users Manual" implies that all the Cause flags are cleared every instruction...
	But, the "EE Core Instruction Set Manual" says that only certain Cause Flags are cleared
	for specific instructions... I'm just setting them to clear when the Instruction Set Manual
	says to... (cottonvibes)
*/
#define clearFPUFlags(cFlags) {  \
	_ContVal_ &= ~( cFlags ) ;  \
}

#ifdef comparePrecision
// This compare discards the least-significant bit(s) in order to solve some rounding issues.
	#define C_cond_S(cond) {  \
		FPRreg tempA, tempB;  \
		tempA.UL = _FsValUl_ & comparePrecision;  \
		tempB.UL = _FtValUl_ & comparePrecision;  \
		_ContVal_ = ( ( tempA.f ) cond ( tempB.f ) ) ?  \
					( _ContVal_ | FPUflagC ) :  \
					( _ContVal_ & ~FPUflagC );  \
	}
#else
// Used for Comparing; This compares if the floats are exactly the same.
	#define C_cond_S(cond) {  \
	   _ContVal_ = ( fpuDouble(_FsValUl_) cond fpuDouble(_FtValUl_) ) ?  \
				   ( _ContVal_ | FPUflagC ) :  \
				   ( _ContVal_ & ~FPUflagC );  \
	}
#endif

// Conditional Branch
#define BC1(cond)                               \
   if ( ( _ContVal_ & FPUflagC ) cond 0 ) {   \
      intDoBranch( _BranchTarget_ );            \
   }

// Conditional Branch
#define BC1L(cond)                              \
   if ( ( _ContVal_ & FPUflagC ) cond 0 ) {   \
      intDoBranch( _BranchTarget_ );            \
   } else cpuRegs.pc += 4;

namespace R5900 {
namespace Interpreter {
namespace OpcodeImpl {
namespace COP1 {

//****************************************************************
// FPU Opcodes
//****************************************************************

float fpuDouble(u32 f)
{
	switch(f & 0x7f800000){
		case 0x0:
			f &= 0x80000000;
			return *(float*)&f;
			break;
		case 0x7f800000:
			f = (f & 0x80000000)|0x7f7fffff;
			return *(float*)&f;
			break;
		default:
			return *(float*)&f;
			break;
	}
}

// EE FPU add/sub guard-bit emulation (ported from ARMSX2 fpuGuardMask/eeGuardedAddSub, GPL-3).
// With d = expA - expB (raw exponent fields), the smaller operand is shifted out
// before the add: it loses its low (|d|-1) bits, and past 24 only its sign
// remains. |d| <= 1 masks nothing. Gated on CHECK_FPU_GUARDED like the JIT.
static void fpuGuardMask(u32& a, u32& b)
{
	const s32 diff = (s32)((a >> 23) & 0xFF) - (s32)((b >> 23) & 0xFF);

	if (diff >= 25)
		b &= 0x80000000;
	else if (diff >= 2)
		b &= 0xffffffffu << (diff - 1);
	else if (diff <= -25)
		a &= 0x80000000;
	else if (diff <= -2)
		a &= 0xffffffffu << (-diff - 1);
}

// Operands arrive as fpuDouble()-clamped words. Returns the sum rounded by the
// host FPU under the current FPCR, like the plain float op it replaces.
static float eeGuardedAddSub(u32 a, u32 b, bool issub)
{
	if (CHECK_FPU_GUARDED)
		fpuGuardMask(a, b);
	FPRreg fa, fb;
	fa.UL = a;
	fb.UL = b;
	return issub ? fa.f - fb.f : fa.f + fb.f;
}

static u32 fpuDouble2u(u32 f)
{
	const float v = fpuDouble(f);
	u32 r;
	std::memcpy(&r, &v, sizeof(r));
	return r;
}

void ABS_S() {
	u32 r = _FsValUl_ & 0x7fffffff;
	if (CHECK_FPU_OVERFLOW && r > posFmax)
		r = posFmax;
	_FdValUl_ = r;
}

void ADD_S() {
	_FdValf_  = eeGuardedAddSub( fpuDouble2u( _FsValUl_ ), fpuDouble2u( _FtValUl_ ), false );
	if (checkOverflow( _FdValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FdValUl_, FPUflagU | FPUflagSU);
}

void ADDA_S() {
	_FAValf_  = eeGuardedAddSub( fpuDouble2u( _FsValUl_ ), fpuDouble2u( _FtValUl_ ), false );
	if (checkOverflow( _FAValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FAValUl_, FPUflagU | FPUflagSU);
}

void BC1F() {
	BC1(==);
}

void BC1FL() {
	BC1L(==); // Equal to 0
}

void BC1T() {
	BC1(!=);
}

void BC1TL() {
	BC1L(!=); // different from 0
}

void C_EQ() {
	C_cond_S(==);
}

void C_F() {
	clearFPUFlags( FPUflagC ); //clears C regardless
}

void C_LE() {
	C_cond_S(<=);
}

void C_LT() {
	C_cond_S(<);
}

void CFC1() {
	if (!_Rt_) return;

	if (_Fs_ >= 16)
		cpuRegs.GPR.r[_Rt_].SD[0] = (s32)((fpuRegs.fprc[31] & 0x0083c078) | 0x01000001);	// x86 JIT: drop always-zero bits, set always-one bits, sign extend
	else
		cpuRegs.GPR.r[_Rt_].SD[0] = (s32)fpuRegs.fprc[0];
}

void CTC1() {
	if ( _Fs_ != 31 ) return;
	fpuRegs.fprc[_Fs_] = cpuRegs.GPR.r[_Rt_].UL[0];
}

void CVT_S() {
	_FdValf_ = (float)_FsValSl_;
}

void CVT_W() {
	if ( ( _FsValUl_ & 0x7F800000 ) <= 0x4E800000 ) { _FdValSl_ = (s32)_FsValf_; }
	else if ( ( _FsValUl_ & 0x80000000 ) == 0 ) { _FdValUl_ = 0x7fffffff; }
	else { _FdValUl_ = 0x80000000; }
}

void DIV_S() {
	if (checkDivideByZero( _FdValUl_, _FtValUl_, _FsValUl_, FPUflagD | FPUflagSD, FPUflagI | FPUflagSI)) return;
	{
		const FPControlRegisterBackup fpcr_backup(EmuConfig.Cpu.FPUDivFPCR);
		_FdValf_ = fpuDouble( _FsValUl_ ) / fpuDouble( _FtValUl_ );
	}
	if (checkOverflow( _FdValUl_, 0)) return;
	checkUnderflow( _FdValUl_, 0);
}

/*	The Instruction Set manual has an overly complicated way of
	determining the flags that are set. Hopefully this shorter
	method provides a similar outcome and is faster. (cottonvibes)
*/
void MADD_S() {
	FPRreg temp;
	temp.f = fpuDouble( _FsValUl_ ) * fpuDouble( _FtValUl_ );
	_FdValf_  = eeGuardedAddSub( fpuDouble2u( _FAValUl_ ), fpuDouble2u( temp.UL ), false );
	if (checkOverflow( _FdValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FdValUl_, FPUflagU | FPUflagSU);
}

void MADDA_S() {
	FPRreg prod;
	prod.f = fpuDouble( _FsValUl_ ) * fpuDouble( _FtValUl_ );
	_FAValf_  = eeGuardedAddSub( fpuDouble2u( _FAValUl_ ), fpuDouble2u( prod.UL ), false );
	if (checkOverflow( _FAValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FAValUl_, FPUflagU | FPUflagSU);
}

void MAX_S() {
	u32 a = _FsValUl_, b = _FtValUl_;
	if (CHECK_FPU_OVERFLOW) { a = fpuClampSign(a); b = fpuClampSign(b); }
	_FdValUl_ = fpuSseMaxMin(a, b, true);
}

void MFC1() {
	if ( !_Rt_ ) return;
	cpuRegs.GPR.r[_Rt_].SD[0] = _FsValSl_;		// sign extension into 64bit
}

void MIN_S() {
	u32 a = _FsValUl_, b = _FtValUl_;
	if (CHECK_FPU_OVERFLOW) { a = fpuClampSign(a); b = fpuClampSign(b); }
	_FdValUl_ = fpuSseMaxMin(a, b, false);
}

void MOV_S() {
	_FdValUl_ = _FsValUl_;
}

void MSUB_S() {
	FPRreg temp;
	temp.f = fpuDouble( _FsValUl_ ) * fpuDouble( _FtValUl_ );
	_FdValf_  = eeGuardedAddSub( fpuDouble2u( _FAValUl_ ), fpuDouble2u( temp.UL ), true );
	if (checkOverflow( _FdValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FdValUl_, FPUflagU | FPUflagSU);
}

void MSUBA_S() {
	FPRreg prod;
	prod.f = fpuDouble( _FsValUl_ ) * fpuDouble( _FtValUl_ );
	_FAValf_  = eeGuardedAddSub( fpuDouble2u( _FAValUl_ ), fpuDouble2u( prod.UL ), true );
	if (checkOverflow( _FAValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FAValUl_, FPUflagU | FPUflagSU);
}

void MTC1() {
	_FsValUl_ = cpuRegs.GPR.r[_Rt_].UL[0];
}

void MUL_S() {
	_FdValf_  = fpuDouble( _FsValUl_ ) * fpuDouble( _FtValUl_ );
	if (checkOverflow( _FdValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FdValUl_, FPUflagU | FPUflagSU);
}

void MULA_S() {
	_FAValf_  = fpuDouble( _FsValUl_ ) * fpuDouble( _FtValUl_ );
	if (checkOverflow( _FAValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FAValUl_, FPUflagU | FPUflagSU);
}

void NEG_S() {
	_FdValUl_ = fpuClampSign(_FsValUl_ ^ 0x80000000);
}

void RSQRT_S() {
	clearFPUFlags(FPUflagD | FPUflagI);

	u32 t = _FtValUl_;
	const u32 sv = _FsValUl_;
	if (t & 0x80000000) {
		_ContVal_ |= FPUflagI | FPUflagSI;
		t &= 0x7fffffff;
	}
	if ((t & 0x7F800000) == 0) { // Ft is zero (Denormals are Zero)
		_ContVal_ |= ((sv & 0x7F800000) == 0) ? (FPUflagI | FPUflagSI) : (FPUflagD | FPUflagSD);
		_FdValUl_ = (sv & 0x80000000) | posFmax;
		return;
	}

	u32 s2 = sv;
	if (CHECK_FPU_EXTRA_OVERFLOW) {
		if (t > posFmax)
			t = posFmax;
		s2 = fpuClampSign(s2);
	}
	FPRreg ft, fs, res;
	ft.UL = t;
	fs.UL = s2;
	res.f = fs.f / sqrtf(ft.f);
	_FdValUl_ = fpuClampFloat(res.UL);
}

void SQRT_S() {
	clearFPUFlags(FPUflagI | FPUflagD);

	u32 t = _FtValUl_;
	if (t & 0x80000000) {
		_ContVal_ |= FPUflagI | FPUflagSI;
		t &= 0x7fffffff;
	}
	if (CHECK_FPU_OVERFLOW && t > posFmax)
		t = posFmax;
	FPRreg ft, res;
	ft.UL = t;
	res.f = sqrtf(ft.f);
	_FdValUl_ = CHECK_FPU_EXTRA_OVERFLOW ? fpuClampFloat(res.UL) : res.UL;
}

void SUB_S() {
	_FdValf_  = eeGuardedAddSub( fpuDouble2u( _FsValUl_ ), fpuDouble2u( _FtValUl_ ), true );
	if (checkOverflow( _FdValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FdValUl_, FPUflagU | FPUflagSU);
}

void SUBA_S() {
	_FAValf_  = eeGuardedAddSub( fpuDouble2u( _FsValUl_ ), fpuDouble2u( _FtValUl_ ), true );
	if (checkOverflow( _FAValUl_, FPUflagO | FPUflagSO)) return;
	checkUnderflow( _FAValUl_, FPUflagU | FPUflagSU);
}

}	// End Namespace COP1

/////////////////////////////////////////////////////////////////////
// COP1 (FPU)  Load/Store Instructions

// These are actually EE opcodes but since they're related to FPU registers and such they
// seem more appropriately located here.

void LWC1() {
	u32 addr;
	addr = cpuRegs.GPR.r[_Rs_].UL[0] + (s16)(cpuRegs.code & 0xffff);	// force sign extension to 32bit
	if (addr & 0x00000003) { Console.Error( "FPU (LWC1 Opcode): Invalid Unaligned Memory Address" ); return; }  // Should signal an exception?
	fpuRegs.fpr[_Rt_].UL = memRead32(addr);
}

void SWC1() {
	u32 addr;
	addr = cpuRegs.GPR.r[_Rs_].UL[0] + (s16)(cpuRegs.code & 0xffff);	// force sign extension to 32bit
	if (addr & 0x00000003) { Console.Error( "FPU (SWC1 Opcode): Invalid Unaligned Memory Address" ); return; }  // Should signal an exception?
	memWrite32(addr, fpuRegs.fpr[_Rt_].UL);
}

} } }
