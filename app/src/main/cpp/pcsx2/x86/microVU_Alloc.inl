// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

//------------------------------------------------------------------
// Micro VU - Pass 2 Functions
//------------------------------------------------------------------

//------------------------------------------------------------------
// Flag Allocators
//------------------------------------------------------------------

__fi static const x32& getFlagReg(uint fInst)
{
	static const x32* const gprFlags[4] = {&gprF0, &gprF1, &gprF2, &gprF3};
	pxAssert(fInst < 4);
	return *gprFlags[fInst];
}

__fi void setBitSFLAG(const x32& reg, const x32& regT, int bitTest, int bitSet)
{
//	xTEST(regT, bitTest);
    armAsm->Tst(a64::WRegister(regT), bitTest);
//	xForwardJZ8 skip;
    a64::Label skip;
    armAsm->B(&skip, a64::Condition::eq);
//	xOR(reg, bitSet);
    armAsm->Orr(a64::WRegister(reg), a64::WRegister(reg), bitSet);
//	skip.SetTarget();
    armBind(&skip);
}

__fi void setBitFSEQ(const x32& reg, int bitX)
{
//	xTEST(reg, bitX);
    armAsm->Tst(a64::WRegister(reg), bitX);
//	xForwardJump8 skip(Jcc_Zero);
    a64::Label skip;
    armAsm->B(&skip, a64::Condition::eq);
//	xOR(reg, bitX);
    armAsm->Orr(a64::WRegister(reg), a64::WRegister(reg), bitX);
//	skip.SetTarget();
    armBind(&skip);
}

__fi void mVUallocSFLAGa(const x32& reg, int fInstance)
{
//	xMOV(reg, getFlagReg(fInstance));
    armAsm->Mov(a64::WRegister(reg), a64::WRegister(getFlagReg(fInstance)));
}

__fi void mVUallocSFLAGb(const x32& reg, int fInstance)
{
//	xMOV(getFlagReg(fInstance), reg);
    armAsm->Mov(a64::WRegister(getFlagReg(fInstance)), a64::WRegister(reg));
}

// Normalize Status Flag
__ri void mVUallocSFLAGc(const x32& reg, const x32& regT, int fInstance)
{
//	xXOR(reg, reg);
    armAsm->Eor(a64::WRegister(reg), a64::WRegister(reg), a64::WRegister(reg));
	mVUallocSFLAGa(regT, fInstance);
	setBitSFLAG(reg, regT, 0x0f00, 0x0001); // Z  Bit
	setBitSFLAG(reg, regT, 0xf000, 0x0002); // S  Bit
	setBitSFLAG(reg, regT, 0x000f, 0x0040); // ZS Bit
	setBitSFLAG(reg, regT, 0x00f0, 0x0080); // SS Bit
//	xAND(regT, 0xffff0000); // DS/DI/OS/US/D/I/O/U Bits
    armAsm->And(a64::WRegister(regT), a64::WRegister(regT), 0xffff0000);
//	xSHR(regT, 14);
    armAsm->Lsr(a64::WRegister(regT), a64::WRegister(regT), 14);
//	xOR(reg, regT);
    armAsm->Orr(a64::WRegister(reg), a64::WRegister(reg), a64::WRegister(regT));
}

// Denormalizes Status Flag; destroys tmp1/tmp2
__ri void mVUallocSFLAGd(const a64::MemOperand memAddr, const x32& reg = EAX, const x32& tmp1 = ECX, const x32& tmp2 = EDX)
{
//	xMOV(tmp2, ptr32[memAddr]);
    armAsm->Ldr(a64::WRegister(tmp2), memAddr);
//	xMOV(reg, tmp2);
    armAsm->Mov(a64::WRegister(reg), a64::WRegister(tmp2));
//	xSHR(reg, 3);
    armAsm->Lsr(a64::WRegister(reg), a64::WRegister(reg), 3);
//	xAND(reg, 0x18);
    armAsm->And(a64::WRegister(reg), a64::WRegister(reg), 0x18);

//	xMOV(tmp1, tmp2);
    armAsm->Mov(a64::WRegister(tmp1), a64::WRegister(tmp2));
//	xSHL(tmp1, 11);
    armAsm->Lsl(a64::WRegister(tmp1), a64::WRegister(tmp1), 11);
//	xAND(tmp1, 0x1800);
    armAsm->And(a64::WRegister(tmp1), a64::WRegister(tmp1), 0x1800);
//	xOR(reg, tmp1);
    armAsm->Orr(a64::WRegister(reg), a64::WRegister(reg), a64::WRegister(tmp1));

//	xSHL(tmp2, 14);
    armAsm->Lsl(a64::WRegister(tmp2), a64::WRegister(tmp2), 14);
//	xAND(tmp2, 0x3cf0000);
    armAsm->And(a64::WRegister(tmp2), a64::WRegister(tmp2), 0x3cf0000);
//	xOR(reg, tmp2);
    armAsm->Orr(a64::WRegister(reg), a64::WRegister(reg), a64::WRegister(tmp2));
}

__fi void mVUallocMFLAGa(mV, const x32& reg, int fInstance)
{
//	xMOVZX(reg, ptr16[&mVU.macFlag[fInstance]]);
    armAsm->Ldrh(a64::WRegister(reg), PTR_MVU(microVU[mVU.index].macFlag[fInstance]));
}

__fi void mVUallocMFLAGb(mV, const x32& reg, int fInstance)
{
	//xAND(reg, 0xffff);
    if (fInstance < 4) {
//        xMOV(ptr32[&mVU.macFlag[fInstance]], reg);         // microVU
        armAsm->Str(a64::WRegister(reg), PTR_MVU(microVU[mVU.index].macFlag[fInstance]));
    }
    else               {
//        xMOV(ptr32[&mVU.regs().VI[REG_MAC_FLAG].UL], reg); // macroVU
        armAsm->Str(a64::WRegister(reg), PTR_CPU(vuRegs[mVU.index].VI[REG_MAC_FLAG].UL));
    }
}

__fi void mVUallocCFLAGa(mV, const x32& reg, int fInstance)
{
    if (fInstance < 4) {
//        xMOV(reg, ptr32[&mVU.clipFlag[fInstance]]);         // microVU
        armAsm->Ldr(a64::WRegister(reg), PTR_MVU(microVU[mVU.index].clipFlag[fInstance]));
    }
    else               {
//        xMOV(reg, ptr32[&mVU.regs().VI[REG_CLIP_FLAG].UL]); // macroVU
        armAsm->Ldr(a64::WRegister(reg), PTR_CPU(vuRegs[mVU.index].VI[REG_CLIP_FLAG].UL));
    }
}

__fi void mVUallocCFLAGb(mV, const x32& reg, int fInstance)
{
    if (fInstance < 4) {
//        xMOV(ptr32[&mVU.clipFlag[fInstance]], reg);         // microVU
        armAsm->Str(a64::WRegister(reg), PTR_MVU(microVU[mVU.index].clipFlag[fInstance]));
    }
    else               {
//        xMOV(ptr32[&mVU.regs().VI[REG_CLIP_FLAG].UL], reg); // macroVU
        armAsm->Str(a64::WRegister(reg), PTR_CPU(vuRegs[mVU.index].VI[REG_CLIP_FLAG].UL));
    }
}

//------------------------------------------------------------------
// VI Reg Allocators
//------------------------------------------------------------------

void microRegAlloc::writeVIBackup(const a64::Register& reg)
{
	microVU& mVU = index ? microVU1 : microVU0;
//	xMOV(ptr32[&mVU.VIbackup], xRegister32(reg));
    armAsm->Str(a64::WRegister(reg), PTR_MVU(microVU[mVU.index].VIbackup));
}

//------------------------------------------------------------------
// P/Q Reg Allocators
//------------------------------------------------------------------

__fi void getPreg(mV, const xmm& reg)
{
	mVUunpack_xyzw(reg, xmmPQ, (2 + mVUinfo.readP));
}

__fi void getQreg(const xmm& reg, int qInstance)
{
	mVUunpack_xyzw(reg, xmmPQ, qInstance);
}

__ri void writeQreg(const xmm& reg, int qInstance)
{
    if (qInstance) {
//        xINSERTPS(xmmPQ, reg, _MM_MK_INSERTPS_NDX(0, 1, 0));
        armAsm->Ins(xmmPQ.V4S(), 1, reg.V4S(), 0);
    }
    else {
//        xMOVSS(xmmPQ, reg);
        armAsm->Mov(xmmPQ.S(), 0, reg.S(), 0);
    }
}
