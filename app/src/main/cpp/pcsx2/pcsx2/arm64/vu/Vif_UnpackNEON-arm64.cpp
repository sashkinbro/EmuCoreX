// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#include "Vif_UnpackNEON-arm64.h"
#include "common/Perf.h"

static void vifLoad32ToS(oak::QReg dst, OakMemOperand mem)
{
	oakLoadScalar32(dst.toS(), mem);
}

static void vifLoad64ToD(oak::QReg dst, OakMemOperand mem)
{
	oakLoadVector64(dst.toD(), mem);
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
// Exercises the actual narrow-load wrappers, including architectural upper
// clearing, without changing their production emission before the RED gate.
extern "C" u32 EmuCoreXOracleCompileVifNarrowLoad(u32 width, s64 offset, u8* code, u32 capacity)
{
    pxAssert((width==4||width==8)&&!oakHasBlock());
    oakSetAsmPtr(code,capacity);u8* const begin=oakStartBlock();
    oakAsm->MOVI(oak::util::Q0.B16(),0xff);
    if(width==4)vifLoad32ToS(oak::util::Q0,{OAK_XARG1,offset});
    else vifLoad64ToD(oak::util::Q0,{OAK_XARG1,offset});
    oakAsm->STR(oak::util::Q0,OAK_XARG2);oakAsm->RET();
    u8* const end=oakEndBlock();return static_cast<u32>(end-begin);
}
#endif

static void vifLoadQFromAddress(oak::QReg dst, const void* addr)
{
	oakMoveAddressToReg(OAK_XSCRATCH2, addr);
	oakAsm->LDR(dst, OAK_XSCRATCH2);
}

static void vifStoreQToAddress(oak::QReg src, const void* addr)
{
	oakMoveAddressToReg(OAK_XSCRATCH2, addr);
	oakAsm->STR(src, OAK_XSCRATCH2);
}

// =====================================================================================================
//  VifUnpackSSE_Base Section
// =====================================================================================================
VifUnpackNEON_Base::VifUnpackNEON_Base()
	: usn(false)
	, doMask(false)
	, UnpkLoopIteration(0)
	, UnpkNoOfIterations(0)
	, IsAligned(0)
	, dstIndirect{OAK_XARG1, 0}
	, srcIndirect{OAK_XARG2, 0}
	, workReg(VIF_Q_WORK)
	, destReg(VIF_Q_DEST)
	, workGprW(VIF_W_WORK)
{
}

void VifUnpackNEON_Base::xMovDest() const
{
	if (!IsWriteProtectedOp())
	{
		if (IsUnmaskedOp())
			oakStore128(destReg, dstIndirect);
		else
			doMaskWrite(destReg);
	}
}

void VifUnpackNEON_Base::xShiftR(oak::QReg regX, int n) const
{
	if (usn)
		oakAsm->USHR(regX.S4(), regX.S4(), n);
	else
		oakAsm->SSHR(regX.S4(), regX.S4(), n);
}

void VifUnpackNEON_Base::xPMOVXX8(oak::QReg regX) const
{
	// TODO(Stenzek): Check this
	vifLoad32ToS(regX, srcIndirect);

	if (usn)
	{
		oakAsm->USHLL(regX.H8(), regX.toD().B8(), 0);
		oakAsm->USHLL(regX.S4(), regX.toD().H4(), 0);
	}
	else
	{
		oakAsm->SSHLL(regX.H8(), regX.toD().B8(), 0);
		oakAsm->SSHLL(regX.S4(), regX.toD().H4(), 0);
	}
}

void VifUnpackNEON_Base::xPMOVXX16(oak::QReg regX) const
{
	vifLoad64ToD(regX, srcIndirect);

	if (usn)
		oakAsm->USHLL(regX.S4(), regX.toD().H4(), 0);
	else
		oakAsm->SSHLL(regX.S4(), regX.toD().H4(), 0);
}

void VifUnpackNEON_Base::xUPK_S_32() const
{
	if (UnpkLoopIteration == 0)
		oakLoad128(workReg, srcIndirect);

	if (IsInputMasked())
		return;

	switch (UnpkLoopIteration)
	{
		case 0:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[0]);
			break;
		case 1:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[1]);
			break;
		case 2:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[2]);
			break;
		case 3:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[3]);
			break;
	}
}

void VifUnpackNEON_Base::xUPK_S_16() const
{
	if (UnpkLoopIteration == 0)
		xPMOVXX16(workReg);

	if (IsInputMasked())
		return;

	switch (UnpkLoopIteration)
	{
		case 0:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[0]);
			break;
		case 1:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[1]);
			break;
		case 2:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[2]);
			break;
		case 3:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[3]);
			break;
	}
}

void VifUnpackNEON_Base::xUPK_S_8() const
{
	if (UnpkLoopIteration == 0)
		xPMOVXX8(workReg);

	if (IsInputMasked())
		return;

	switch (UnpkLoopIteration)
	{
		case 0:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[0]);
			break;
		case 1:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[1]);
			break;
		case 2:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[2]);
			break;
		case 3:
			oakAsm->DUP(destReg.S4(), workReg.Selem()[3]);
			break;
	}
}

// The V2 + V3 unpacks have freaky behaviour, the manual claims "indeterminate".
// After testing on the PS2, it's very much determinate in 99% of cases
// and games like Lemmings, And1 Streetball rely on this data to be like this!
// I have commented after each shuffle to show what data is going where - Ref

void VifUnpackNEON_Base::xUPK_V2_32() const
{
	if (UnpkLoopIteration == 0)
	{
		oakLoad128(workReg, srcIndirect);

		if (IsInputMasked())
			return;

		oakAsm->DUP(destReg.D2(), workReg.Delem()[0]); //v1v0v1v0
		if (IsAligned)
			oakAsm->INS(destReg.Selem()[3], oak::util::WZR); //zero last word - tested on ps2
	}
	else
	{
		if (IsInputMasked())
			return;

		oakAsm->DUP(destReg.D2(), workReg.Delem()[1]); //v3v2v3v2
		if (IsAligned)
			oakAsm->INS(destReg.Selem()[3], oak::util::WZR); //zero last word - tested on ps2
	}
}

void VifUnpackNEON_Base::xUPK_V2_16() const
{
	if (UnpkLoopIteration == 0)
	{
		xPMOVXX16(workReg);

		if (IsInputMasked())
			return;

		oakAsm->DUP(destReg.D2(), workReg.Delem()[0]); //v1v0v1v0
	}
	else
	{
		if (IsInputMasked())
			return;

		oakAsm->DUP(destReg.D2(), workReg.Delem()[1]); //v3v2v3v2
	}
}

void VifUnpackNEON_Base::xUPK_V2_8() const
{
	if (UnpkLoopIteration == 0)
	{
		xPMOVXX8(workReg);

		if (IsInputMasked())
			return;

		oakAsm->DUP(destReg.D2(), workReg.Delem()[0]); //v1v0v1v0
	}
	else
	{
		if (IsInputMasked())
			return;

		oakAsm->DUP(destReg.D2(), workReg.Delem()[1]); //v3v2v3v2
	}
}

void VifUnpackNEON_Base::xUPK_V3_32() const
{
	if (IsInputMasked())
		return;

	oakLoad128(destReg, srcIndirect);
	if (UnpkLoopIteration != IsAligned)
		oakAsm->INS(destReg.Selem()[3], oak::util::WZR);
}

void VifUnpackNEON_Base::xUPK_V3_16() const
{
	if (IsInputMasked())
		return;

	xPMOVXX16(destReg);

	//With V3-16, it takes the first vector from the next position as the W vector
	//However - IF the end of this iteration of the unpack falls on a quadword boundary, W becomes 0
	//IsAligned is the position through the current QW in the vif packet
	//Iteration counts where we are in the packet.
	int result = (((UnpkLoopIteration / 4) + 1 + (4 - IsAligned)) & 0x3);

	if ((UnpkLoopIteration & 0x1) == 0 && result == 0)
		oakAsm->INS(destReg.Selem()[3], oak::util::WZR); //zero last word on QW boundary if whole 32bit word is used - tested on ps2
}

void VifUnpackNEON_Base::xUPK_V3_8() const
{
	if (IsInputMasked())
		return;

	xPMOVXX8(destReg);
	if (UnpkLoopIteration != IsAligned)
		oakAsm->INS(destReg.Selem()[3], oak::util::WZR);
}

void VifUnpackNEON_Base::xUPK_V4_32() const
{
	if (IsInputMasked())
		return;

	oakLoad128(destReg, srcIndirect);
}

void VifUnpackNEON_Base::xUPK_V4_16() const
{
	if (IsInputMasked())
		return;

	xPMOVXX16(destReg);
}

void VifUnpackNEON_Base::xUPK_V4_8() const
{
	if (IsInputMasked())
		return;

	xPMOVXX8(destReg);
}

void VifUnpackNEON_Base::xUPK_V4_5() const
{
	if (IsInputMasked())
		return;

	oakLoad16(workGprW, srcIndirect);
	oakAsm->LSL(workGprW, workGprW, 3); // ABG|R5.000
	oakAsm->DUP(destReg.S4(), workGprW); // x|x|x|R
	oakAsm->LSR(workGprW, workGprW, 8); // ABG
	oakAsm->LSL(workGprW, workGprW, 3); // AB|G5.000
	oakAsm->INS(destReg.Selem()[1], workGprW); // x|x|G|R
	oakAsm->LSR(workGprW, workGprW, 8); // AB
	oakAsm->LSL(workGprW, workGprW, 3); // A|B5.000
	oakAsm->INS(destReg.Selem()[2], workGprW); // x|B|G|R
	oakAsm->LSR(workGprW, workGprW, 8); // A
	oakAsm->LSL(workGprW, workGprW, 7); // A.0000000
	oakAsm->INS(destReg.Selem()[3], workGprW); // A|B|G|R
	oakAsm->SHL(destReg.S4(), destReg.S4(), 24); // can optimize to
	oakAsm->USHR(destReg.S4(), destReg.S4(), 24); // single AND...
}

void VifUnpackNEON_Base::xUnpack(int upknum) const
{
	switch (upknum)
	{
		case 0:
			xUPK_S_32();
			break;
		case 1:
			xUPK_S_16();
			break;
		case 2:
			xUPK_S_8();
			break;

		case 4:
			xUPK_V2_32();
			break;
		case 5:
			xUPK_V2_16();
			break;
		case 6:
			xUPK_V2_8();
			break;

		case 8:
			xUPK_V3_32();
			break;
		case 9:
			xUPK_V3_16();
			break;
		case 10:
			xUPK_V3_8();
			break;

		case 12:
			xUPK_V4_32();
			break;
		case 13:
			xUPK_V4_16();
			break;
		case 14:
			xUPK_V4_8();
			break;
		case 15:
			xUPK_V4_5();
			break;

		case 3:
		case 7:
		case 11:
			// TODO: Needs hardware testing.
			// Dynasty Warriors 5: Empire  - Player 2 chose a character menu.
			Console.Warning("Vpu/Vif: Invalid Unpack %d", upknum);
			break;
	}
}

// =====================================================================================================
//  VifUnpackSSE_Simple
// =====================================================================================================

VifUnpackNEON_Simple::VifUnpackNEON_Simple(bool usn_, bool domask_, int curCycle_)
{
	curCycle = curCycle_;
	usn = usn_;
	doMask = domask_;
	IsAligned = true;
}

void VifUnpackNEON_Simple::doMaskWrite(oak::QReg regX) const
{
	oakLoad128(VIF_Q_TEMP, dstIndirect);

	int offX = std::min(curCycle, 3);
	vifLoadQFromAddress(OAK_QSCRATCH3, nVifMask[0][offX]);
	vifLoadQFromAddress(OAK_QSCRATCH, nVifMask[1][offX]);
	vifLoadQFromAddress(OAK_QSCRATCH2, nVifMask[2][offX]);
	oakAsm->AND(regX.B16(), regX.B16(), OAK_QSCRATCH3.B16());
	oakAsm->AND(VIF_Q_TEMP.B16(), VIF_Q_TEMP.B16(), OAK_QSCRATCH.B16());
	oakAsm->ORR(regX.B16(), regX.B16(), OAK_QSCRATCH2.B16());
	oakAsm->ORR(regX.B16(), regX.B16(), VIF_Q_TEMP.B16());
	oakStore128(regX, dstIndirect);
}

// ecx = dest, edx = src
static void nVifGen(int usn, int mask, int curCycle)
{

	int usnpart = usn * 2 * 16;
	int maskpart = mask * 16;

	VifUnpackNEON_Simple vpugen(!!usn, !!mask, curCycle);

	for (int i = 0; i < 16; ++i)
	{
		nVifCall& ucall(nVifUpk[((usnpart + maskpart + i) * 4) + curCycle]);
		ucall = NULL;
		if (nVifT[i] == 0)
			continue;

		ucall = (nVifCall)oakStartBlock();
		vpugen.xUnpack(i);
		vpugen.xMovDest();
		oakAsm->RET();
		oakEndBlock();
	}
}

void VifUnpackSSE_Init()
{
	oakSetAsmPtr(SysMemory::GetVIFUnpackRec(), SysMemory::GetVIFUnpackRecEnd() - SysMemory::GetVIFUnpackRec());

	for (int a = 0; a < 2; a++)
	{
		for (int b = 0; b < 2; b++)
		{
			for (int c = 0; c < 4; c++)
			{
				nVifGen(a, b, c);
			}
		}
	}

	Perf::any.Register(SysMemory::GetVIFUnpackRec(), oakGetAsmPtr() - SysMemory::GetVIFUnpackRec(), "VIF Unpack");
}
