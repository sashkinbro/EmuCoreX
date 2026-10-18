// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0

#include "OaknutHelpers-arm64.h"

#include "common/Assertions.h"
#include "common/HostSys.h"

#include <bit>
#include <cstring>
#include <new>

static bool oakIsSigned9(s64 value)
{
	return value >= -256 && value <= 255;
}

static bool oakIsUnsignedScaled(s64 value, u32 scale, u32 bits)
{
	if (value < 0 || (value & (static_cast<s64>(scale) - 1)) != 0)
		return false;

	const u64 encoded = static_cast<u64>(value) >> std::countr_zero(scale);
	return encoded < (1ull << bits);
}

static oak::XReg oakScratchAvoiding(oak::XReg reg)
{
	return (reg.index() == OAK_XSCRATCH.index()) ? OAK_XSCRATCH2 : OAK_XSCRATCH;
}

static oak::XReg oakScratchAvoiding(oak::XReg first, oak::XReg second)
{
	if (first.index() != OAK_XSCRATCH.index() && second.index() != OAK_XSCRATCH.index())
		return OAK_XSCRATCH;

	if (first.index() != OAK_XSCRATCH2.index() && second.index() != OAK_XSCRATCH2.index())
		return OAK_XSCRATCH2;

	pxFailRel("No free oak scratch register for memory helper");
	return OAK_XSCRATCH;
}

oak::WReg oakWRegister(int n)
{
	pxAssert(static_cast<unsigned>(n) < 32);
	return oak::WReg(n);
}

oak::XReg oakXRegister(int n)
{
	pxAssert(static_cast<unsigned>(n) < 32);
	return oak::XReg(n);
}

oak::SReg oakSRegister(int n)
{
	pxAssert(static_cast<unsigned>(n) < 32);
	return oak::SReg(n);
}

oak::DReg oakDRegister(int n)
{
	pxAssert(static_cast<unsigned>(n) < 32);
	return oak::DReg(n);
}

oak::QReg oakQRegister(int n)
{
	pxAssert(static_cast<unsigned>(n) < 32);
	return oak::QReg(n);
}

bool oakIsCalleeSavedRegister(int reg)
{
	return reg >= 19;
}

bool oakIsCallerSaved(int id)
{
#if defined(__ANDROID__)
	return id <= 15;
#else
#ifdef _WIN32
	return id <= 2 || (id >= 8 && id <= 11);
#else
	return id <= 2 || id == 6 || id == 7 || (id >= 8 && id <= 11);
#endif
#endif
}

bool oakIsCallerSavedXmm(int id)
{
#if defined(__ANDROID__)
	return true;
#else
#ifdef _WIN32
	return id < 6;
#else
	return true;
#endif
#endif
}

thread_local oak::CodeGenerator* oakAsm;
thread_local u8* oakAsmPtr;
thread_local size_t oakAsmCapacity;
alignas(oak::CodeGenerator) static thread_local unsigned char oakAsmStorage[sizeof(oak::CodeGenerator)];

void oakSetAsmPtr(void* ptr, size_t capacity)
{
	pxAssert(!oakAsm);
	oakAsmPtr = static_cast<u8*>(ptr);
	oakAsmCapacity = capacity;
}

void oakAlignAsmPtr()
{
	static constexpr uintptr_t ALIGNMENT = 16;
	u8* new_ptr = reinterpret_cast<u8*>((reinterpret_cast<uintptr_t>(oakAsmPtr) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1));
	pxAssert(static_cast<size_t>(new_ptr - oakAsmPtr) <= oakAsmCapacity);
	oakAsmCapacity -= new_ptr - oakAsmPtr;
	oakAsmPtr = new_ptr;
}

u8* oakStartBlock()
{
	oakAlignAsmPtr();

	HostSys::BeginCodeWrite();

	pxAssert(!oakAsm);
	oakAsm = new (static_cast<void*>(oakAsmStorage)) oak::CodeGenerator(reinterpret_cast<u32*>(oakAsmPtr));
	return oakAsmPtr;
}

u8* oakEndBlock()
{
	pxAssert(oakAsm);

	const u32 size = static_cast<u32>(oakAsm->offset());
	pxAssert(size < oakAsmCapacity);

	oakAsm->~CodeGenerator();
	oakAsm = nullptr;

	HostSys::EndCodeWrite();
	HostSys::FlushInstructionCache(oakAsmPtr, size);

	oakAsmPtr += size;
	oakAsmCapacity -= size;
	return oakAsmPtr;
}

void oakMoveAddressToReg(oak::XReg reg, const void* addr)
{
	pxAssert(oakAsm);
	oakAsm->MOVP2R(reg, addr);
}

template<unsigned Bits>
static bool oakBranchTargetInRange(const void* ptr)
{
	// Unsigned subtraction also handles backwards targets without subtracting
	// unrelated pointers or overflowing signed address arithmetic.
	const uintptr_t offset = reinterpret_cast<uintptr_t>(ptr) -
		reinterpret_cast<uintptr_t>(oakGetCurrentCodePointer());
	constexpr uintptr_t reach = uintptr_t{1} << (Bits - 1);
	return (offset & 3) == 0 && offset + reach < reach * 2;
}

void oakEmitJmp(const void* ptr)
{
	pxAssert(oakAsm);
	if (oakBranchTargetInRange<28>(ptr))
	{
		oakAsm->B(ptr);
		return;
	}
	oakMoveAddressToReg(OAK_XSCRATCH, ptr);
	oakAsm->BR(OAK_XSCRATCH);
}

void oakEmitCall(const void* ptr)
{
	pxAssert(oakAsm);
	if (oakBranchTargetInRange<28>(ptr))
	{
		oakAsm->BL(ptr);
		return;
	}
	oakMoveAddressToReg(OAK_XSCRATCH, ptr);
	oakAsm->BLR(OAK_XSCRATCH);
}

void oakEmitCondBranch(oak::Cond cond, const void* ptr)
{
	pxAssert(oakAsm);
	if (static_cast<unsigned>(cond) < 14 && oakBranchTargetInRange<21>(ptr))
	{
		oakAsm->B(cond, ptr);
		return;
	}
	oak::Label branch_not_taken;
	oakAsm->B(oak::invert(cond), branch_not_taken);
	oakEmitJmp(ptr);
	oakAsm->l(branch_not_taken);
}

void oakEmitCbz(oak::WReg reg, const void* ptr)
{
	pxAssert(oakAsm);
	if (oakBranchTargetInRange<21>(ptr))
	{
		oakAsm->CBZ(reg, ptr);
		return;
	}
	oak::Label branch_not_taken;
	oakAsm->CBNZ(reg, branch_not_taken);
	oakEmitJmp(ptr);
	oakAsm->l(branch_not_taken);
}

void oakEmitCbz(oak::XReg reg, const void* ptr)
{
	pxAssert(oakAsm);
	if (oakBranchTargetInRange<21>(ptr))
	{
		oakAsm->CBZ(reg, ptr);
		return;
	}
	oak::Label branch_not_taken;
	oakAsm->CBNZ(reg, branch_not_taken);
	oakEmitJmp(ptr);
	oakAsm->l(branch_not_taken);
}

void oakEmitCbnz(oak::WReg reg, const void* ptr)
{
	pxAssert(oakAsm);
	if (oakBranchTargetInRange<21>(ptr))
	{
		oakAsm->CBNZ(reg, ptr);
		return;
	}
	oak::Label branch_not_taken;
	oakAsm->CBZ(reg, branch_not_taken);
	oakEmitJmp(ptr);
	oakAsm->l(branch_not_taken);
}

void oakEmitCbnz(oak::XReg reg, const void* ptr)
{
	pxAssert(oakAsm);
	if (oakBranchTargetInRange<21>(ptr))
	{
		oakAsm->CBNZ(reg, ptr);
		return;
	}
	oak::Label branch_not_taken;
	oakAsm->CBZ(reg, branch_not_taken);
	oakEmitJmp(ptr);
	oakAsm->l(branch_not_taken);
}

void oakEmitJmpPtr(void* code, const void* dst, bool flush_icache)
{
	const s64 displacement = static_cast<s64>(reinterpret_cast<intptr_t>(dst) - reinterpret_cast<intptr_t>(code));
	pxAssert((displacement & 3) == 0);
	pxAssert(displacement >= -(1ll << 27) && displacement < (1ll << 27));

	const u32 imm26 = static_cast<u32>((displacement >> 2) & 0x03ffffff);
	const u32 encoded = 0x14000000u | imm26;
	std::memcpy(code, &encoded, sizeof(encoded));

	if (flush_icache)
		HostSys::FlushInstructionCache(code, sizeof(encoded));
}

void oakPatchCondBranch(void* code, const void* dst, oak::Cond cond, bool flush_icache)
{
	const s64 displacement = static_cast<s64>(reinterpret_cast<intptr_t>(dst) - reinterpret_cast<intptr_t>(code));
	pxAssert((displacement & 3) == 0);
	pxAssert(displacement >= -(1ll << 20) && displacement < (1ll << 20));

	const u32 imm19 = static_cast<u32>((displacement >> 2) & 0x0007ffff);
	const u32 encoded = 0x54000000u | (imm19 << 5) | static_cast<u32>(cond);
	std::memcpy(code, &encoded, sizeof(encoded));

	if (flush_icache)
		HostSys::FlushInstructionCache(code, sizeof(encoded));
}

void oakAddSignedImm(oak::WReg dst, oak::WReg src, s32 imm, oak::WReg scratch)
{
	pxAssert(oakAsm);
	if (imm == 0)
	{
		if (dst.index() != src.index())
			oakAsm->MOV(dst, src);
		return;
	}

	if (imm > 0 && imm <= 4095)
	{
		oakAsm->ADD(dst, src, static_cast<u32>(imm));
		return;
	}

	if (imm < 0 && imm >= -4095)
	{
		oakAsm->SUB(dst, src, static_cast<u32>(-imm));
		return;
	}

	oakAsm->MOV(scratch, static_cast<u32>(imm));
	oakAsm->ADD(dst, src, scratch);
}

void oakAddSignedImm(oak::XReg dst, oak::XReg src, s64 imm, oak::XReg scratch)
{
	pxAssert(oakAsm);
	if (imm == 0)
	{
		if (dst.index() != src.index())
			oakAsm->MOV(dst, src);
		return;
	}

	if (imm > 0 && imm <= 4095)
	{
		oakAsm->ADD(dst, src, static_cast<u32>(imm));
		return;
	}

	if (imm < 0 && imm >= -4095)
	{
		oakAsm->SUB(dst, src, static_cast<u32>(-imm));
		return;
	}

	oakAsm->MOV(scratch, static_cast<u64>(imm));
	oakAsm->ADD(dst, src, scratch);
}

void oakLoad32(oak::WReg dst, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (oakIsUnsignedScaled(mem.offset, 4, 12))
		return oakAsm->LDR(dst, mem.base, oak::POffset<14, 2>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->LDUR(dst, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->LDR(dst, scratch);
}

void oakLoadScalar32(oak::SReg dst, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (oakIsUnsignedScaled(mem.offset, 4, 12))
		return oakAsm->LDR(dst, mem.base, oak::POffset<14, 2>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->LDUR(dst, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->LDR(dst, scratch);
}

void oakLoad64(oak::XReg dst, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (oakIsUnsignedScaled(mem.offset, 8, 12))
		return oakAsm->LDR(dst, mem.base, oak::POffset<15, 3>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->LDUR(dst, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->LDR(dst, scratch);
}

void oakLoadVector64(oak::DReg dst, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (oakIsUnsignedScaled(mem.offset, 8, 12))
		return oakAsm->LDR(dst, mem.base, oak::POffset<15, 3>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->LDUR(dst, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->LDR(dst, scratch);
}

void oakLoad16(oak::WReg dst, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (oakIsUnsignedScaled(mem.offset, 2, 12))
		return oakAsm->LDRH(dst, mem.base, oak::POffset<13, 1>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->LDURH(dst, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->LDRH(dst, scratch);
}

void oakLoad128(oak::QReg dst, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (oakIsUnsignedScaled(mem.offset, 16, 12))
		return oakAsm->LDR(dst, mem.base, oak::POffset<16, 4>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->LDUR(dst, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->LDR(dst, scratch);
}

void oakStore32(oak::WReg src, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (src.index() == mem.base.index())
	{
		const oak::XReg scratch = oakScratchAvoiding(mem.base);
		oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
		oakAsm->ADD(scratch, mem.base, scratch);
		return oakAsm->STR(src, scratch);
	}
	if (oakIsUnsignedScaled(mem.offset, 4, 12))
		return oakAsm->STR(src, mem.base, oak::POffset<14, 2>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->STUR(src, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(src.toX(), mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->STR(src, scratch);
}

void oakStoreScalar32(oak::SReg src, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (oakIsUnsignedScaled(mem.offset, 4, 12))
		return oakAsm->STR(src, mem.base, oak::POffset<14, 2>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->STUR(src, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->STR(src, scratch);
}

void oakStore64(oak::XReg src, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (src.index() == mem.base.index())
	{
		const oak::XReg scratch = oakScratchAvoiding(mem.base);
		oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
		oakAsm->ADD(scratch, mem.base, scratch);
		return oakAsm->STR(src, scratch);
	}
	if (oakIsUnsignedScaled(mem.offset, 8, 12))
		return oakAsm->STR(src, mem.base, oak::POffset<15, 3>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->STUR(src, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(src, mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->STR(src, scratch);
}

void oakStore16(oak::WReg src, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (src.index() == mem.base.index())
	{
		const oak::XReg scratch = oakScratchAvoiding(mem.base);
		oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
		oakAsm->ADD(scratch, mem.base, scratch);
		return oakAsm->STRH(src, scratch);
	}
	if (oakIsUnsignedScaled(mem.offset, 2, 12))
		return oakAsm->STRH(src, mem.base, oak::POffset<13, 1>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->STURH(src, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(src.toX(), mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->STRH(src, scratch);
}

void oakStore128(oak::QReg src, OakMemOperand mem)
{
	pxAssert(oakAsm);
	if (oakIsUnsignedScaled(mem.offset, 16, 12))
		return oakAsm->STR(src, mem.base, oak::POffset<16, 4>(mem.offset));
	if (oakIsSigned9(mem.offset))
		return oakAsm->STUR(src, mem.base, oak::SOffset<9, 0>(mem.offset));

	const oak::XReg scratch = oakScratchAvoiding(mem.base);
	oakAsm->MOV(scratch, static_cast<u64>(mem.offset));
	oakAsm->ADD(scratch, mem.base, scratch);
	oakAsm->STR(src, scratch);
}

void oakEmitSmokeReturn42()
{
	pxAssert(oakAsm);
	using namespace oak::util;
	oakAsm->MOV(W0, 42);
	oakAsm->RET();
}

void oakEmitSmokeFpAddReturn()
{
	pxAssert(oakAsm);
	using namespace oak::util;
	oakAsm->FADD(S0, S1, S2);
	oakAsm->RET();
}
