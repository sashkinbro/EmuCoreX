// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include "common/Pcsx2Defs.h"

#include "oaknut/oaknut.hpp"

namespace oak = oaknut;

oak::WReg oakWRegister(int n);
oak::XReg oakXRegister(int n);
oak::SReg oakSRegister(int n);
oak::DReg oakDRegister(int n);
oak::QReg oakQRegister(int n);
bool oakIsCalleeSavedRegister(int reg);
bool oakIsCallerSaved(int id);
bool oakIsCallerSavedXmm(int id);

#define OAK_WARG1 oak::util::W0
#define OAK_WARG2 oak::util::W1
#define OAK_WARG3 oak::util::W2
#define OAK_WARG4 oak::util::W3
#define OAK_XARG1 oak::util::X0
#define OAK_XARG2 oak::util::X1
#define OAK_XARG3 oak::util::X2
#define OAK_XARG4 oak::util::X3

#define OAK_XSCRATCH oak::util::X16
#define OAK_WSCRATCH oak::util::W16
#define OAK_XSCRATCH2 oak::util::X17
#define OAK_WSCRATCH2 oak::util::W17

// Callee-saved and outside the EE/VU register allocators. Both JIT entry
// paths pin it to mVUglob so hot clamp sequences can load min/max with LDP.
#define OAK_XVUCLAMP oak::util::X26

#define OAK_QSCRATCH oak::util::Q30
#define OAK_DSCRATCH oak::util::D30
#define OAK_SSCRATCH oak::util::S30
#define OAK_QSCRATCH2 oak::util::Q31
#define OAK_DSCRATCH2 oak::util::D31
#define OAK_SSCRATCH2 oak::util::S31
#define OAK_QSCRATCH3 oak::util::Q29
#define OAK_DSCRATCH3 oak::util::D29
#define OAK_SSCRATCH3 oak::util::S29

extern thread_local oak::CodeGenerator* oakAsm;
extern thread_local u8* oakAsmPtr;
extern thread_local size_t oakAsmCapacity;

static __fi bool oakHasBlock()
{
	return oakAsm != nullptr;
}

static __fi u8* oakGetCurrentCodePointer()
{
	return static_cast<u8*>(oakAsmPtr) + oakAsm->offset();
}

static __fi u8* oakGetAsmPtr()
{
	return oakAsmPtr;
}

void oakSetAsmPtr(void* ptr, size_t capacity);
void oakAlignAsmPtr();
u8* oakStartBlock();
u8* oakEndBlock();

void oakMoveAddressToReg(oak::XReg reg, const void* addr);
void oakEmitJmp(const void* ptr);
void oakEmitCall(const void* ptr);
void oakEmitCondBranch(oak::Cond cond, const void* ptr);
void oakEmitCbz(oak::WReg reg, const void* ptr);
void oakEmitCbz(oak::XReg reg, const void* ptr);
void oakEmitCbnz(oak::WReg reg, const void* ptr);
void oakEmitCbnz(oak::XReg reg, const void* ptr);
void oakEmitJmpPtr(void* code, const void* dst, bool flush_icache = true);
void oakPatchCondBranch(void* code, const void* dst, oak::Cond cond, bool flush_icache = true);
void oakAddSignedImm(oak::WReg dst, oak::WReg src, s32 imm, oak::WReg scratch);
void oakAddSignedImm(oak::XReg dst, oak::XReg src, s64 imm, oak::XReg scratch);

struct OakMemOperand
{
	oak::XReg base;
	s64 offset;
};

// Large-offset memory helpers may clobber OAK_XSCRATCH.
void oakLoad16(oak::WReg dst, OakMemOperand mem);
void oakLoad32(oak::WReg dst, OakMemOperand mem);
void oakLoadScalar32(oak::SReg dst, OakMemOperand mem);
void oakLoad64(oak::XReg dst, OakMemOperand mem);
void oakLoadVector64(oak::DReg dst, OakMemOperand mem);
void oakLoad128(oak::QReg dst, OakMemOperand mem);
void oakStore32(oak::WReg src, OakMemOperand mem);
void oakStoreScalar32(oak::SReg src, OakMemOperand mem);
void oakStore64(oak::XReg src, OakMemOperand mem);
void oakStore16(oak::WReg src, OakMemOperand mem);
void oakStore128(oak::QReg src, OakMemOperand mem);

void oakEmitSmokeReturn42();
void oakEmitSmokeFpAddReturn();

// Assertion-only wrappers around Oaknut code generation blocks.
// Force-inlined to eliminate function call overhead across hundreds of
// call sites per block. Used by EE, IOP and VU recompilers.
static __fi u8* recBeginOaknutEmit()
{
	return oakGetCurrentCodePointer();
}
static __fi void recEndOaknutEmit()
{
}
