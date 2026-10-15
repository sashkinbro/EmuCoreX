// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "vtlb.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/ee/iCore-arm64.h"
#include "arm64/ee/iR5900-arm64.h"

#include "common/Perf.h"

using namespace vtlb_private;
#if !defined(__ANDROID__)
using namespace x86Emitter;
#endif

// we need enough for a 32-bit jump forwards (5 bytes)
//static constexpr u32 LOADSTORE_PADDING = 5;

static u32 GetAllocatedGPRBitmask()
{
	u32 i, mask = 0;
	for (i = 0; i < iREGCNT_GPR; ++i)
	{
		if (x86regs[i].inuse)
			mask |= (1u << i);
	}
	return mask;
}

static u32 GetAllocatedXMMBitmask()
{
	u32 i, mask = 0;
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (xmmregs[i].inuse)
			mask |= (1u << i);
	}
	return mask;
}

static u32 GetCurrentGuestPC()
{
	return g_recompilingDelaySlot ? pc : (pc - 4);
}

static void vtlbWriteGuestPC_emit_oaknut(u32 guest_pc)
{
	oakAsm->MOV(OAK_WSCRATCH, guest_pc);
	oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.pc))});
}

/*
	// Pseudo-Code For the following Dynarec Implementations -->

	u32 vmv = vmap[addr>>VTLB_PAGE_BITS].raw();
	sptr ppf=addr+vmv;
	if (!(ppf<0))
	{
		data[0]=*reinterpret_cast<DataType*>(ppf);
		if (DataSize==128)
			data[1]=*reinterpret_cast<DataType*>(ppf+8);
		return 0;
	}
	else
	{
		//has to: translate, find function, call function
		u32 hand=(u8)vmv;
		u32 paddr=(ppf-hand) << 1;
		return reinterpret_cast<TemplateHelper<DataSize,false>::HandlerType*>(RWFT[TemplateHelper<DataSize,false>::sidx][0][hand])(paddr,data);
	}

	// And in ASM it looks something like this -->

	mov eax,ecx;
	shr eax,VTLB_PAGE_BITS;
	mov rax,[rax*wordsize+vmap];
	add rcx,rax;
	js _fullread;

	//these are wrong order, just an example ...
	mov [rax],ecx;
	mov ecx,[rdx];
	mov [rax+4],ecx;
	mov ecx,[rdx+4];
	mov [rax+4+4],ecx;
	mov ecx,[rdx+4+4];
	mov [rax+4+4+4+4],ecx;
	mov ecx,[rdx+4+4+4+4];
	///....

	jmp cont;
	_fullread:
	movzx eax,al;
	sub   ecx,eax;
	call [eax+stuff];
	cont:
	........

*/

namespace vtlb_private
{
	static void DynGen_PrepRegs_emit_oaknut(int addr_reg, int value_reg, u32 sz, bool xmm,
		bool ir_fastmem_load = false)
	{
		using namespace oak::util;

		EE::Profiler.EmitMem();

		if (ir_fastmem_load)
		{
			pxAssert(value_reg < 0 && sz == 32 && !xmm);
		}
		else
		{
			_freeX86reg(EE_HOST_RCX);
		}

		if (value_reg >= 0)
		{
			if (sz == 128)
			{
				oakAsm->MOV(W1, oakWRegister(addr_reg));
				pxAssert(xmm);
				_freeXMMreg(1);
				oakAsm->MOV(oakQRegister(1).B16(), oakQRegister(value_reg).B16());
			}
			else if (xmm)
			{
				oakAsm->MOV(W1, oakWRegister(addr_reg));
				pxAssert(sz == 32);
				_freeX86reg(EE_HOST_RDX);
				oakAsm->FMOV(W2, oakSRegister(value_reg));
			}
			else
			{
				if (value_reg == EE_HOST_RCX)
				{
					if (addr_reg == EE_HOST_RDX)
					{
						oakAsm->MOV(W16, W2);
						_freeX86reg(EE_HOST_RDX);
						oakAsm->MOV(X2, X1);
						oakAsm->MOV(W1, W16);
					}
					else
					{
						_freeX86reg(EE_HOST_RDX);
						oakAsm->MOV(X2, X1);
						oakAsm->MOV(W1, oakWRegister(addr_reg));
					}
				}
				else
				{
					oakAsm->MOV(W1, oakWRegister(addr_reg));
					_freeX86reg(EE_HOST_RDX);
					if (value_reg != EE_HOST_RDX)
						oakAsm->MOV(X2, oakXRegister(value_reg));
				}
			}
		}
		else
		{
			oakAsm->MOV(W1, oakWRegister(addr_reg));
		}

		oakAsm->MOV(W0, W1);
		oakAsm->LSR(W0, W0, VTLB_PAGE_BITS);
		oakLoad64(X16, {X27, static_cast<s64>(offsetof(cpuRegistersPack, vtlbdata.vmap))});
		oakAsm->LDR(X0, X16, X0, oak::IndexExt::LSL, 3);
		oakAsm->ADDS(X1, X1, X0);
	}

	static void DynGen_DirectRead_emit_oaknut(u32 bits, bool sign)
	{
		using namespace oak::util;

		pxAssert(bits == 8 || bits == 16 || bits == 32 || bits == 64 || bits == 128);
		switch (bits)
		{
			case 8:
				sign ? oakAsm->LDRSB(X0, X1) : oakAsm->LDRB(W0, X1);
				break;

			case 16:
				sign ? oakAsm->LDRSH(X0, X1) : oakAsm->LDRH(W0, X1);
				break;

			case 32:
				sign ? oakAsm->LDRSW(X0, X1) : oakAsm->LDR(W0, X1);
				break;

			case 64:
				oakAsm->LDR(X0, X1);
				break;

			case 128:
				oakAsm->LDR(Q0, X1);
				break;

			jNO_DEFAULT
		}
	}

	static void DynGen_DirectWrite_emit_oaknut(u32 bits)
	{
		using namespace oak::util;

		switch (bits)
		{
			case 8:
				oakAsm->STRB(W2, X1);
				break;

			case 16:
				oakAsm->STRH(W2, X1);
				break;

			case 32:
				oakAsm->STR(W2, X1);
				break;

			case 64:
				oakAsm->STR(X2, X1);
				break;

			case 128:
				oakAsm->STR(Q1, X1);
				break;

			jNO_DEFAULT
		}
	}
} // namespace vtlb_private

static bool hasBeenCalled = false;
static constexpr u32 INDIRECT_DISPATCHER_SIZE = 128;
static constexpr u32 INDIRECT_DISPATCHERS_SIZE = 2 * 5 * 2 * INDIRECT_DISPATCHER_SIZE;
alignas(__pagesize) static u8 m_IndirectDispatchers[__pagesize];

static oak::XReg vtlbFastmemBase_emit_oaknut()
{
	return oak::util::X19;
}

static bool vtlbCanUseConstFastmem()
{
	return CHECK_FASTMEM && !vtlb_IsFaultingPC(GetCurrentGuestPC());
}

static void vtlbFastmemReadU8_emit_oaknut(oak::XReg dst, oak::XReg addr)
{
	oakAsm->LDRB(dst.toW(), vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemReadS8_emit_oaknut(oak::XReg dst, oak::XReg addr)
{
	oakAsm->LDRSB(dst, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemReadU16_emit_oaknut(oak::XReg dst, oak::XReg addr)
{
	oakAsm->LDRH(dst.toW(), vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemReadS16_emit_oaknut(oak::XReg dst, oak::XReg addr)
{
	oakAsm->LDRSH(dst, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemReadU32_emit_oaknut(oak::XReg dst, oak::XReg addr)
{
	oakAsm->LDR(dst.toW(), vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemReadS32_emit_oaknut(oak::XReg dst, oak::XReg addr)
{
	oakAsm->LDRSW(dst, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemReadU64_emit_oaknut(oak::XReg dst, oak::XReg addr)
{
	oakAsm->LDR(dst, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemReadF32_emit_oaknut(oak::SReg dst, oak::XReg addr)
{
	oakAsm->LDR(dst, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemReadQ128_emit_oaknut(oak::QReg dst, oak::XReg addr)
{
	oakAsm->LDR(dst, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemWriteU8_emit_oaknut(oak::XReg src, oak::XReg addr)
{
	oakAsm->STRB(src.toW(), vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemWriteU16_emit_oaknut(oak::XReg src, oak::XReg addr)
{
	oakAsm->STRH(src.toW(), vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemWriteU32_emit_oaknut(oak::XReg src, oak::XReg addr)
{
	oakAsm->STR(src.toW(), vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemWriteU64_emit_oaknut(oak::XReg src, oak::XReg addr)
{
	oakAsm->STR(src, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemWriteF32_emit_oaknut(oak::SReg src, oak::XReg addr)
{
	oakAsm->STR(src, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbFastmemWriteQ128_emit_oaknut(oak::QReg src, oak::XReg addr)
{
	oakAsm->STR(src, vtlbFastmemBase_emit_oaknut(), addr.toW(), oak::IndexExt::UXTW);
}

static void vtlbConstFastmemAddress_emit_oaknut(u32 addr_const)
{
	oakAsm->MOV(oak::util::W1, addr_const);
}

static oak::XReg vtlbConstDirectPtr_emit_oaknut(u32 addr_const)
{
	using namespace oak::util;

	oakAsm->MOV(W1, addr_const);
	oakAsm->LSR(W0, W1, VTLB_PAGE_BITS);
	oakLoad64(X16, {X27, static_cast<s64>(offsetof(cpuRegistersPack, vtlbdata.vmap))});
	oakAsm->LDR(X0, X16, X0, oak::IndexExt::LSL, 3);
	oakAsm->ADD(X1, X1, X0);
	return X1;
}

static void vtlbConstDirectReadU8_emit_oaknut(oak::XReg dst, oak::XReg ptr)
{
	oakAsm->LDRB(dst.toW(), ptr);
}

static void vtlbConstDirectReadS8_emit_oaknut(oak::XReg dst, oak::XReg ptr)
{
	oakAsm->LDRSB(dst, ptr);
}

static void vtlbConstDirectReadU16_emit_oaknut(oak::XReg dst, oak::XReg ptr)
{
	oakAsm->LDRH(dst.toW(), ptr);
}

static void vtlbConstDirectReadS16_emit_oaknut(oak::XReg dst, oak::XReg ptr)
{
	oakAsm->LDRSH(dst, ptr);
}

static void vtlbConstDirectReadU32_emit_oaknut(oak::XReg dst, oak::XReg ptr)
{
	oakAsm->LDR(dst.toW(), ptr);
}

static void vtlbConstDirectReadS32_emit_oaknut(oak::XReg dst, oak::XReg ptr)
{
	oakAsm->LDRSW(dst, ptr);
}

static void vtlbConstDirectReadU64_emit_oaknut(oak::XReg dst, oak::XReg ptr)
{
	oakAsm->LDR(dst, ptr);
}

static void vtlbConstDirectReadF32_emit_oaknut(oak::SReg dst, oak::XReg ptr)
{
	oakAsm->LDR(dst, ptr);
}

static void vtlbConstDirectReadQ128_emit_oaknut(oak::QReg dst, oak::XReg ptr)
{
	oakAsm->LDR(dst, ptr);
}

static void vtlbConstDirectWriteU8_emit_oaknut(oak::XReg src, oak::XReg ptr)
{
	oakAsm->STRB(src.toW(), ptr);
}

static void vtlbConstDirectWriteU16_emit_oaknut(oak::XReg src, oak::XReg ptr)
{
	oakAsm->STRH(src.toW(), ptr);
}

static void vtlbConstDirectWriteU32_emit_oaknut(oak::XReg src, oak::XReg ptr)
{
	oakAsm->STR(src.toW(), ptr);
}

static void vtlbConstDirectWriteU64_emit_oaknut(oak::XReg src, oak::XReg ptr)
{
	oakAsm->STR(src, ptr);
}

static void vtlbConstDirectWriteF32_emit_oaknut(oak::SReg src, oak::XReg ptr)
{
	oakAsm->STR(src, ptr);
}

static void vtlbConstDirectWriteQ128_emit_oaknut(oak::QReg src, oak::XReg ptr)
{
	oakAsm->STR(src, ptr);
}

static void vtlbCallConstReadHandler_emit_oaknut(u32 paddr, const void* handler)
{
	recFlushReccycle();
	oakAsm->MOV(oak::util::W0, paddr);
	oakEmitCall(handler);
	recReloadReccycle();
}

static void vtlbFinishReadHandlerGpr_emit_oaknut(oak::XReg dst, u32 bits, bool sign)
{
	switch (bits)
	{
		case 8:
			sign ? oakAsm->SXTB(dst, oak::util::W0) : oakAsm->UXTB(dst.toW(), oak::util::W0);
			break;

		case 16:
			sign ? oakAsm->SXTH(dst, oak::util::W0) : oakAsm->UXTH(dst.toW(), oak::util::W0);
			break;

		case 32:
			sign ? oakAsm->SXTW(dst, oak::util::W0) : oakAsm->MOV(dst.toW(), oak::util::W0);
			break;

		case 64:
			oakAsm->MOV(dst, oak::util::X0);
			break;

		jNO_DEFAULT
	}
}

static void vtlbFinishReadHandlerF32_emit_oaknut(oak::SReg dst)
{
	oakAsm->FMOV(dst, oak::util::W0);
}

static void vtlbFinishReadHandlerQ128_emit_oaknut(oak::QReg dst)
{
	oakAsm->MOV(dst.B16(), oak::util::Q0.B16());
}

static bool vtlbCanDirectReadConstHw32(u32 paddr)
{
	if (paddr == INTC_STAT)
		return !EmuConfig.Speedhacks.IntcStat;

	return paddr == INTC_MASK || paddr == DMAC_STAT || paddr == DMAC_PCR;
}

static void vtlbReadHw32Gpr_emit_oaknut(oak::XReg dst, u32 paddr, bool sign)
{
	oakMoveAddressToReg(oak::util::X16, &psHu32(paddr));
	sign ? oakAsm->LDRSW(dst, oak::util::X16) : oakAsm->LDR(dst.toW(), oak::util::X16);
}

static void vtlbReadHw32F32_emit_oaknut(oak::SReg dst, u32 paddr)
{
	oakMoveAddressToReg(oak::util::X16, &psHu32(paddr));
	oakAsm->LDR(dst, oak::util::X16);
}

static bool vtlbCanDirectWriteConstHw32(u32 paddr)
{
	return paddr == INTC_STAT;
}

static void vtlbWriteHw32Gpr_emit_oaknut(u32 paddr, oak::XReg value)
{
	oakMoveAddressToReg(oak::util::X16, &psHu32(paddr));

	if (paddr == INTC_STAT)
	{
		oakAsm->LDR(oak::util::W17, oak::util::X16);
		oakAsm->BIC(oak::util::W17, oak::util::W17, value.toW());
		oakAsm->STR(oak::util::W17, oak::util::X16);
	}
}

static void vtlbWriteDmacStatGpr_emit_oaknut(oak::XReg value)
{
	oakMoveAddressToReg(oak::util::X16, &psHu16(DMAC_STAT));
	oakAsm->LDRH(oak::util::W17, oak::util::X16);
	oakAsm->BIC(oak::util::W17, oak::util::W17, value.toW());
	oakAsm->STRH(oak::util::W17, oak::util::X16);

	oakMoveAddressToReg(oak::util::X16, &psHu16(DMAC_STAT + 2));
	oakAsm->LDRH(oak::util::W17, oak::util::X16);
	oakAsm->EOR(oak::util::W17, oak::util::W17, value.toW(), oak::LogShift::LSR, 16);
	oakAsm->STRH(oak::util::W17, oak::util::X16);
}

static void vtlbCallConstWriteHandlerGpr_emit_oaknut(u32 paddr, oak::XReg value, const void* handler)
{
	recFlushReccycle();
	if (value.index() == oak::util::X0.index())
	{
		oakAsm->MOV(oak::util::X1, value);
		oakAsm->MOV(oak::util::W0, paddr);
	}
	else
	{
		oakAsm->MOV(oak::util::W0, paddr);
		oakAsm->MOV(oak::util::X1, value);
	}
	oakEmitCall(handler);
	recReloadReccycle();
}

static void vtlbCallConstWriteHandlerF32_emit_oaknut(u32 paddr, oak::SReg value, const void* handler)
{
	recFlushReccycle();
	oakAsm->MOV(oak::util::W0, paddr);
	oakAsm->FMOV(oak::util::W1, value);
	oakEmitCall(handler);
	recReloadReccycle();
}

static void vtlbCallConstWriteHandlerQ128_emit_oaknut(u32 paddr, oak::QReg value, const void* handler)
{
	recFlushReccycle();
	oakAsm->MOV(oak::util::W0, paddr);
	oakAsm->MOV(oak::util::Q0.B16(), value.B16());
	oakEmitCall(handler);
	recReloadReccycle();
}

static void vtlbV2P_emit_oaknut()
{
	using namespace oak::util;

	oakAsm->MOV(W0, W1);
	oakAsm->AND(W1, W1, VTLB_PAGE_MASK);
	oakAsm->LSR(W0, W0, VTLB_PAGE_BITS);
	oakLoad64(X16, {X27, static_cast<s64>(offsetof(cpuRegistersPack, vtlbdata.ppmap))});
	oakAsm->LDR(W0, X16, X0, oak::IndexExt::LSL, 2);
	oakAsm->ORR(W0, W0, W1);
}

static void vtlbBackpatchSubSp_emit_oaknut(u32 stack_size)
{
	oakAsm->SUB(oak::util::SP, oak::util::SP, stack_size);
}

static void vtlbBackpatchAddSp_emit_oaknut(u32 stack_size)
{
	oakAsm->ADD(oak::util::SP, oak::util::SP, stack_size);
}

static void vtlbBackpatchSaveQ_emit_oaknut(oak::QReg reg, u32 stack_offset)
{
	oakAsm->STR(reg, oak::util::SP, oak::POffset<16, 4>(stack_offset));
}

static void vtlbBackpatchSaveX_emit_oaknut(oak::XReg reg, u32 stack_offset)
{
	oakAsm->STR(reg, oak::util::SP, oak::POffset<15, 3>(stack_offset));
}

static void vtlbBackpatchRestoreQ_emit_oaknut(oak::QReg reg, u32 stack_offset)
{
	oakAsm->LDR(reg, oak::util::SP, oak::POffset<16, 4>(stack_offset));
}

static void vtlbBackpatchRestoreX_emit_oaknut(oak::XReg reg, u32 stack_offset)
{
	oakAsm->LDR(reg, oak::util::SP, oak::POffset<15, 3>(stack_offset));
}

static void vtlbBackpatchMoveLoadResult_emit_oaknut(u8 data_register, u8 size_in_bits, bool is_xmm)
{
	if (size_in_bits == 128)
	{
		if (data_register != xmm0.index())
			oakAsm->MOV(oakQRegister(data_register).B16(), oak::util::Q0.B16());
	}
	else if (is_xmm)
	{
		pxAssert(size_in_bits == 32);
		oakAsm->FMOV(oakSRegister(data_register), oak::util::W0);
	}
	else
	{
		if (data_register != EE_HOST_RAX)
			oakAsm->MOV(oakXRegister(data_register), oak::util::X0);
	}
}

// ------------------------------------------------------------------------
// mode        - 0 for read, 1 for write!
// operandsize - 0 thru 4 represents 8, 16, 32, 64, and 128 bits.
//
static u8* GetIndirectDispatcherPtr(int mode, int operandsize, int sign = 0)
{
	pxAssert(mode || operandsize >= 3 ? !sign : true);

	return &m_IndirectDispatchers[(mode * (8 * INDIRECT_DISPATCHER_SIZE)) + (sign * 5 * INDIRECT_DISPATCHER_SIZE) +
								  (operandsize * INDIRECT_DISPATCHER_SIZE)];
}

template <typename GenDirectFn>
static void DynGen_HandlerTest_emit_oaknut(const GenDirectFn& gen_direct, int mode, int bits, bool sign = false)
{
	using namespace oak::util;

	int szidx = 0;
	switch (bits)
	{
		case 8: szidx = 0; break;
		case 16: szidx = 1; break;
		case 32: szidx = 2; break;
		case 64: szidx = 3; break;
		case 128: szidx = 4; break;
		jNO_DEFAULT;
	}

	oak::Label to_handler;
	oak::Label done;
	oakAsm->B(MI, to_handler);
	gen_direct();
	oakAsm->B(done);
	oakAsm->l(to_handler);
	oakEmitCall(GetIndirectDispatcherPtr(mode, szidx, sign));
	oakAsm->l(done);
}

static void DynGen_IndirectTlbDispatcherOaknut(int mode, int bits, bool sign)
{
	using namespace oak::util;

#ifdef _WIN32
	#error Oaknut indirect VTLB dispatcher currently supports the Android/Linux ABI path.
#else
	oakAsm->STP(X30, XZR, SP, oak::PreIndexed{}, oak::SOffset<10, 3>(-16));
#endif

	oakAsm->UXTB(W4, W0);
	if (wordsize != 8)
		oakAsm->SUB(W1, W1, 0x80000000);
	oakAsm->SUB(W1, W1, W4);

	oakAsm->MOV(X0, X1);
	oakAsm->MOV(X1, X2);
	if (mode && bits == 4)
		oakAsm->MOV(V0.B16(), V1.B16());

	oakAsm->MOV(X16, reinterpret_cast<uptr>(vtlbdata.RWFT[bits][mode]));
	oakAsm->LDR(X4, X16, X4, oak::IndexExt::LSL, 3);
	recFlushReccycle();
	oakAsm->BLR(X4);
	recReloadReccycle();

	if (!mode)
	{
		if (bits == 0)
		{
			if (sign)
				oakAsm->SXTB(X0, W0);
			else
				oakAsm->UXTB(W0, W0);
		}
		else if (bits == 1)
		{
			if (sign)
				oakAsm->SXTH(X0, W0);
			else
				oakAsm->UXTH(W0, W0);
		}
		else if (bits == 2)
		{
			if (sign)
				oakAsm->SXTW(X0, W0);
		}
	}

#ifdef _WIN32
	#error Oaknut indirect VTLB dispatcher currently supports the Android/Linux ABI path.
#else
	oakAsm->LDP(X30, XZR, SP, oak::PostIndexed{}, oak::SOffset<10, 3>(16));
#endif

	oakAsm->RET();
}

u8* vtlb_DynGenDispatchers(u8* code_start)
{
    if (!hasBeenCalled)
    {
        hasBeenCalled = true;
        HostSys::MemProtect(m_IndirectDispatchers, __pagesize, PageAccess_ReadWrite());

        // clear the buffer to 0xcc (easier debugging).
        std::memset(m_IndirectDispatchers, 0xcc, __pagesize);

        int mode, bits, sign;
        for (mode = 0; mode < 2; ++mode) {
            for (bits = 0; bits < 5; ++bits) {
                for (sign = 0; sign < (!mode && bits < 3 ? 2 : 1); ++sign) {
                    oakSetAsmPtr(GetIndirectDispatcherPtr(mode, bits, !!sign), INDIRECT_DISPATCHER_SIZE);
                    oakStartBlock();
                    ////
                    DynGen_IndirectTlbDispatcherOaknut(mode, bits, !!sign);
                    ////
                    oakEndBlock();
                }
            }
        }
        HostSys::MemProtect(m_IndirectDispatchers, __pagesize, PageAccess_ExecOnly());
    }

    Perf::any.Register(m_IndirectDispatchers, __pagesize, "TLB Dispatcher");
    //// copy code
    HostSys::BeginCodeWrite();
    memcpy(code_start, m_IndirectDispatchers, INDIRECT_DISPATCHERS_SIZE);
    HostSys::EndCodeWrite();
    HostSys::FlushInstructionCache(code_start, INDIRECT_DISPATCHERS_SIZE);

	return code_start + INDIRECT_DISPATCHERS_SIZE;
}

//////////////////////////////////////////////////////////////////////////////////////////
//                            Dynarec Load Implementations
// ------------------------------------------------------------------------
// Recompiled input registers:
//   ecx - source address to read from
//   Returns read value in eax.
int vtlb_DynGenReadNonQuad(u32 bits, bool sign, bool xmm, int addr_reg, vtlb_ReadRegAllocCallback dest_reg_alloc)
{
    pxAssume(bits <= 64);

    int x86_dest_reg;
	const u32 guest_pc = GetCurrentGuestPC();
    if (!CHECK_FASTMEM || vtlb_IsFaultingPC(guest_pc))
    {
        iFlushCall(FLUSH_FULLVTLB);

        recBeginOaknutEmit();
		vtlbWriteGuestPC_emit_oaknut(guest_pc);
        DynGen_PrepRegs_emit_oaknut(addr_reg, -1, bits, xmm);
        DynGen_HandlerTest_emit_oaknut([bits, sign]() { DynGen_DirectRead_emit_oaknut(bits, sign); }, 0, bits, sign && bits < 64);
        recEndOaknutEmit();

        if (!xmm)
        {
//			x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(eax), eax.GetId());
            x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(EE_HOST_RAX), EE_HOST_RAX);
            recBeginOaknutEmit();
            vtlbFinishReadHandlerGpr_emit_oaknut(oakXRegister(x86_dest_reg), bits, sign);
            recEndOaknutEmit();
        }
        else
        {
            // we shouldn't be loading any FPRs which aren't 32bit..
            // we use MOVD here despite it being floating-point data, because we're going int->float reinterpret.
            pxAssert(bits == 32);
            x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeXMMreg(0), 0);
            recBeginOaknutEmit();
            vtlbFinishReadHandlerF32_emit_oaknut(oakSRegister(x86_dest_reg));
            recEndOaknutEmit();
        }

        return x86_dest_reg;
    }

    const u8* codeStart;

    if (!xmm)
    {
//		x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(eax), eax.GetId());
        x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(EE_HOST_RAX), EE_HOST_RAX);
        codeStart = oakGetCurrentCodePointer();

        recBeginOaknutEmit();
        const oak::XReg x86reg = oakXRegister(x86_dest_reg);
        const oak::XReg x86addr = oakXRegister(addr_reg);
        switch (bits)
        {
            case 8:
//			    sign ? xMOVSX(x86reg, ptr8[RFASTMEMBASE + x86addr]) : xMOVZX(xRegister32(x86reg), ptr8[RFASTMEMBASE + x86addr]);
                sign ? vtlbFastmemReadS8_emit_oaknut(x86reg, x86addr) : vtlbFastmemReadU8_emit_oaknut(x86reg, x86addr);
                break;
            case 16:
//			    sign ? xMOVSX(x86reg, ptr16[RFASTMEMBASE + x86addr]) : xMOVZX(xRegister32(x86reg), ptr16[RFASTMEMBASE + x86addr]);
                sign ? vtlbFastmemReadS16_emit_oaknut(x86reg, x86addr) : vtlbFastmemReadU16_emit_oaknut(x86reg, x86addr);
                break;
            case 32:
//			    sign ? xMOVSX(x86reg, ptr32[RFASTMEMBASE + x86addr]) : xMOV(xRegister32(x86reg), ptr32[RFASTMEMBASE + x86addr]);
                sign ? vtlbFastmemReadS32_emit_oaknut(x86reg, x86addr) : vtlbFastmemReadU32_emit_oaknut(x86reg, x86addr);
                break;
            case 64:
                vtlbFastmemReadU64_emit_oaknut(x86reg, x86addr);
                break;

            jNO_DEFAULT
        }
        recEndOaknutEmit();
    }
    else
    {
        pxAssert(bits == 32);
        x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeXMMreg(0), 0);
        codeStart = oakGetCurrentCodePointer();
        recBeginOaknutEmit();
        vtlbFastmemReadF32_emit_oaknut(oakSRegister(x86_dest_reg), oakXRegister(addr_reg));
        recEndOaknutEmit();
    }

    vtlb_AddLoadStoreInfo((uptr)codeStart, static_cast<u32>(oakGetCurrentCodePointer() - codeStart),
                          GetCurrentGuestPC(), GetAllocatedGPRBitmask(), GetAllocatedXMMBitmask(),
                          static_cast<u8>(addr_reg), static_cast<u8>(x86_dest_reg),
                          static_cast<u8>(bits), sign, true, xmm);

    return x86_dest_reg;
}

// ------------------------------------------------------------------------
// Recompiled input registers:
//   ecx - source address to read from
//   Returns read value in eax.
//
// TLB lookup is performed in const, with the assumption that the COP0/TLB will clear the
// recompiler if the TLB is changed.
//
int vtlb_DynGenReadNonQuad_Const(u32 bits, bool sign, bool xmm, u32 addr_const, vtlb_ReadRegAllocCallback dest_reg_alloc)
{
    int x86_dest_reg;
	const u8* codeStart = nullptr;
	bool used_fastmem = false;
	auto vmv = vtlbdata.vmap[addr_const >> VTLB_PAGE_BITS];
	if (!vmv.isHandler(addr_const))
	{
        if (!xmm)
        {
//			x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(eax), eax.GetId());
            x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(EE_HOST_RAX), EE_HOST_RAX);

            recBeginOaknutEmit();
            const oak::XReg regX = oakXRegister(x86_dest_reg);
            if (vtlbCanUseConstFastmem())
            {
                vtlbConstFastmemAddress_emit_oaknut(addr_const);
                codeStart = oakGetCurrentCodePointer();
                switch (bits)
                {
                    case 8:
                        sign ? vtlbFastmemReadS8_emit_oaknut(regX, oak::util::X1) : vtlbFastmemReadU8_emit_oaknut(regX, oak::util::X1);
                        break;

                    case 16:
                        sign ? vtlbFastmemReadS16_emit_oaknut(regX, oak::util::X1) : vtlbFastmemReadU16_emit_oaknut(regX, oak::util::X1);
                        break;

                    case 32:
                        sign ? vtlbFastmemReadS32_emit_oaknut(regX, oak::util::X1) : vtlbFastmemReadU32_emit_oaknut(regX, oak::util::X1);
                        break;

                    case 64:
                        vtlbFastmemReadU64_emit_oaknut(regX, oak::util::X1);
                        break;
                }
                used_fastmem = true;
            }
            else
            {
                const oak::XReg ptr = vtlbConstDirectPtr_emit_oaknut(addr_const);
                switch (bits)
                {
                    case 8:
//				sign ? xMOVSX(xRegister64(x86_dest_reg), ptr8[(u8*)ppf]) : xMOVZX(xRegister32(x86_dest_reg), ptr8[(u8*)ppf]);
                        sign ? vtlbConstDirectReadS8_emit_oaknut(regX, ptr) : vtlbConstDirectReadU8_emit_oaknut(regX, ptr);
                        break;

                    case 16:
//				sign ? xMOVSX(xRegister64(x86_dest_reg), ptr16[(u16*)ppf]) : xMOVZX(xRegister32(x86_dest_reg), ptr16[(u16*)ppf]);
                        sign ? vtlbConstDirectReadS16_emit_oaknut(regX, ptr) : vtlbConstDirectReadU16_emit_oaknut(regX, ptr);
                        break;

                    case 32:
//				sign ? xMOVSX(xRegister64(x86_dest_reg), ptr32[(u32*)ppf]) : xMOV(xRegister32(x86_dest_reg), ptr32[(u32*)ppf]);
                        sign ? vtlbConstDirectReadS32_emit_oaknut(regX, ptr) : vtlbConstDirectReadU32_emit_oaknut(regX, ptr);
                        break;

                    case 64:
                        vtlbConstDirectReadU64_emit_oaknut(regX, ptr);
                        break;
                }
            }
            recEndOaknutEmit();
        }
        else
        {
            x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeXMMreg(0), 0);
            recBeginOaknutEmit();
            if (vtlbCanUseConstFastmem())
            {
                vtlbConstFastmemAddress_emit_oaknut(addr_const);
                codeStart = oakGetCurrentCodePointer();
                vtlbFastmemReadF32_emit_oaknut(oakSRegister(x86_dest_reg), oak::util::X1);
                used_fastmem = true;
            }
            else
                vtlbConstDirectReadF32_emit_oaknut(oakSRegister(x86_dest_reg), vtlbConstDirectPtr_emit_oaknut(addr_const));
            recEndOaknutEmit();
        }
    }
    else
    {
        // has to: translate, find function, call function
        u32 paddr = vmv.assumeHandlerGetPAddr(addr_const);

        int szidx = 0;
        switch (bits)
        {
            case  8: szidx = 0; break;
            case 16: szidx = 1; break;
            case 32: szidx = 2; break;
            case 64: szidx = 3; break;
        }

        // Shortcut for heavily polled EE interrupt registers.
        if ((bits == 32) && vtlbCanDirectReadConstHw32(paddr))
        {
//			x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(eax), eax.GetId());
            x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(EE_HOST_RAX), EE_HOST_RAX);
            if (!xmm)
            {
                recBeginOaknutEmit();
                vtlbReadHw32Gpr_emit_oaknut(oakXRegister(x86_dest_reg), paddr, sign);
                recEndOaknutEmit();
            }
            else
            {
                recBeginOaknutEmit();
                vtlbReadHw32F32_emit_oaknut(oakSRegister(x86_dest_reg), paddr);
                recEndOaknutEmit();
            }
        }
        else
        {
			const u32 guest_pc = GetCurrentGuestPC();
            iFlushCall(FLUSH_FULLVTLB);

            recBeginOaknutEmit();
			vtlbWriteGuestPC_emit_oaknut(guest_pc);
            vtlbCallConstReadHandler_emit_oaknut(paddr, vmv.assumeHandlerGetRaw(szidx, false));
            recEndOaknutEmit();

            if (!xmm)
            {
//				x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(eax), eax.GetId());
                x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeX86reg(EE_HOST_RAX), EE_HOST_RAX);

                recBeginOaknutEmit();
                const oak::XReg regX = oakXRegister(x86_dest_reg);
                switch (bits)
                {
                    // save REX prefix by using 32bit dest for zext
                    case 8:
//					sign ? xMOVSX(xRegister64(x86_dest_reg), al) : xMOVZX(xRegister32(x86_dest_reg), al);
                        vtlbFinishReadHandlerGpr_emit_oaknut(regX, 8, sign);
                        break;

                    case 16:
//					sign ? xMOVSX(xRegister64(x86_dest_reg), ax) : xMOVZX(xRegister32(x86_dest_reg), ax);
                        vtlbFinishReadHandlerGpr_emit_oaknut(regX, 16, sign);
                        break;

                    case 32:
//					sign ? xMOVSX(xRegister64(x86_dest_reg), eax) : xMOV(xRegister32(x86_dest_reg), eax);
                        vtlbFinishReadHandlerGpr_emit_oaknut(regX, 32, sign);
                        break;

                    case 64:
                        vtlbFinishReadHandlerGpr_emit_oaknut(regX, 64, false);
                        break;
                }
                recEndOaknutEmit();
            }
            else
            {
                x86_dest_reg = dest_reg_alloc ? dest_reg_alloc() : (_freeXMMreg(0), 0);
                recBeginOaknutEmit();
                vtlbFinishReadHandlerF32_emit_oaknut(oakSRegister(x86_dest_reg));
                recEndOaknutEmit();
            }
        }
    }

	if (used_fastmem)
	{
		vtlb_AddLoadStoreInfo((uptr)codeStart, static_cast<u32>(oakGetCurrentCodePointer() - codeStart),
			GetCurrentGuestPC(), GetAllocatedGPRBitmask(), GetAllocatedXMMBitmask(),
			static_cast<u8>(EE_HOST_RCX), static_cast<u8>(x86_dest_reg),
			static_cast<u8>(bits), sign, true, xmm);
	}

    return x86_dest_reg;
}

int vtlb_DynGenReadQuad(u32 bits, int addr_reg, vtlb_ReadRegAllocCallback dest_reg_alloc)
{
	pxAssume(bits == 128);

	const u32 guest_pc = GetCurrentGuestPC();
	if (!CHECK_FASTMEM || vtlb_IsFaultingPC(guest_pc))
	{
		iFlushCall(FLUSH_FULLVTLB);

        recBeginOaknutEmit();
		vtlbWriteGuestPC_emit_oaknut(guest_pc);
        DynGen_PrepRegs_emit_oaknut(addr_reg, -1, bits, true);
		DynGen_HandlerTest_emit_oaknut([bits]() {DynGen_DirectRead_emit_oaknut(bits, false); },  0, bits);
        recEndOaknutEmit();

		const int reg = dest_reg_alloc ? dest_reg_alloc() : (_freeXMMreg(0), 0); // Handler returns in xmm0
		if (reg >= 0) {
            recBeginOaknutEmit();
            vtlbFinishReadHandlerQ128_emit_oaknut(oakQRegister(reg));
            recEndOaknutEmit();
        }

		return reg;
	}

	const int reg = dest_reg_alloc ? dest_reg_alloc() : (_freeXMMreg(0), 0); // Handler returns in xmm0
	const u8* codeStart = oakGetCurrentCodePointer();

    recBeginOaknutEmit();
    vtlbFastmemReadQ128_emit_oaknut(oakQRegister(reg), oakXRegister(addr_reg));
    recEndOaknutEmit();

	vtlb_AddLoadStoreInfo((uptr)codeStart, static_cast<u32>(oakGetCurrentCodePointer() - codeStart),
		GetCurrentGuestPC(), GetAllocatedGPRBitmask(), GetAllocatedXMMBitmask(),
		static_cast<u8>(addr_reg), static_cast<u8>(reg),
		static_cast<u8>(bits), false, true, true);

	return reg;
}


// ------------------------------------------------------------------------
// TLB lookup is performed in const, with the assumption that the COP0/TLB will clear the
// recompiler if the TLB is changed.
int vtlb_DynGenReadQuad_Const(u32 bits, u32 addr_const, vtlb_ReadRegAllocCallback dest_reg_alloc)
{
	pxAssert(bits == 128);

	EE::Profiler.EmitConstMem(addr_const);

	int reg;
	auto vmv = vtlbdata.vmap[addr_const >> VTLB_PAGE_BITS];
	if (!vmv.isHandler(addr_const))
	{
		reg = dest_reg_alloc ? dest_reg_alloc() : (_freeXMMreg(0), 0);
		if (reg >= 0) {
			const u8* codeStart = nullptr;
			bool used_fastmem = false;
            recBeginOaknutEmit();
            if (vtlbCanUseConstFastmem())
			{
				vtlbConstFastmemAddress_emit_oaknut(addr_const);
				codeStart = oakGetCurrentCodePointer();
                vtlbFastmemReadQ128_emit_oaknut(oakQRegister(reg), oak::util::X1);
				used_fastmem = true;
			}
            else
                vtlbConstDirectReadQ128_emit_oaknut(oakQRegister(reg), vtlbConstDirectPtr_emit_oaknut(addr_const));
            recEndOaknutEmit();

			if (used_fastmem)
			{
				vtlb_AddLoadStoreInfo((uptr)codeStart, static_cast<u32>(oakGetCurrentCodePointer() - codeStart),
					GetCurrentGuestPC(), GetAllocatedGPRBitmask(), GetAllocatedXMMBitmask(),
					static_cast<u8>(EE_HOST_RCX), static_cast<u8>(reg),
					static_cast<u8>(bits), false, true, true);
			}
        }
	}
	else
	{
		// has to: translate, find function, call function
		u32 paddr = vmv.assumeHandlerGetPAddr(addr_const);

		const int szidx = 4;
		const u32 guest_pc = GetCurrentGuestPC();
		iFlushCall(FLUSH_FULLVTLB);

        recBeginOaknutEmit();
		vtlbWriteGuestPC_emit_oaknut(guest_pc);
        vtlbCallConstReadHandler_emit_oaknut(paddr, vmv.assumeHandlerGetRaw(szidx, 0));
        recEndOaknutEmit();

		reg = dest_reg_alloc ? dest_reg_alloc() : (_freeXMMreg(0), 0);
        recBeginOaknutEmit();
        vtlbFinishReadHandlerQ128_emit_oaknut(oakQRegister(reg));
        recEndOaknutEmit();
	}

	return reg;
}

//////////////////////////////////////////////////////////////////////////////////////////
//                            Dynarec Store Implementations

void vtlb_DynGenWrite(u32 sz, bool xmm, int addr_reg, int value_reg)
{
	const u32 guest_pc = GetCurrentGuestPC();
	if (!CHECK_FASTMEM || vtlb_IsFaultingPC(guest_pc))
	{
		iFlushCall(FLUSH_FULLVTLB);

		recBeginOaknutEmit();
		vtlbWriteGuestPC_emit_oaknut(guest_pc);
		DynGen_PrepRegs_emit_oaknut(addr_reg, value_reg, sz, xmm);
		DynGen_HandlerTest_emit_oaknut([sz]() { DynGen_DirectWrite_emit_oaknut(sz); }, 1, sz);
		recEndOaknutEmit();
		return;
	}

	const u8* codeStart = oakGetCurrentCodePointer();

    if (!xmm)
    {
        recBeginOaknutEmit();
        const oak::XReg regX = oakXRegister(value_reg);
        const oak::XReg vaddr_reg = oakXRegister(addr_reg);
        switch (sz)
        {
            case 8:
                vtlbFastmemWriteU8_emit_oaknut(regX, vaddr_reg);
                break;
            case 16:
                vtlbFastmemWriteU16_emit_oaknut(regX, vaddr_reg);
                break;
            case 32:
                vtlbFastmemWriteU32_emit_oaknut(regX, vaddr_reg);
                break;
            case 64:
                vtlbFastmemWriteU64_emit_oaknut(regX, vaddr_reg);
                break;

            jNO_DEFAULT
        }
        recEndOaknutEmit();
    }
    else
    {
        pxAssert(sz == 32 || sz == 128);

        recBeginOaknutEmit();
        const oak::XReg vaddr_reg = oakXRegister(addr_reg);
        switch (sz)
        {
            case 32:
                vtlbFastmemWriteF32_emit_oaknut(oakSRegister(value_reg), vaddr_reg);
                break;
            case 128:
                vtlbFastmemWriteQ128_emit_oaknut(oakQRegister(value_reg), vaddr_reg);
                break;

            jNO_DEFAULT
        }
        recEndOaknutEmit();
    }

	vtlb_AddLoadStoreInfo((uptr)codeStart, static_cast<u32>(oakGetCurrentCodePointer() - codeStart),
		GetCurrentGuestPC(), GetAllocatedGPRBitmask(), GetAllocatedXMMBitmask(),
		static_cast<u8>(addr_reg), static_cast<u8>(value_reg),
		static_cast<u8>(sz), false, false, xmm);
}


// ------------------------------------------------------------------------
// Generates code for a store instruction, where the address is a known constant.
// TLB lookup is performed in const, with the assumption that the COP0/TLB will clear the
// recompiler if the TLB is changed.
void vtlb_DynGenWrite_Const(u32 bits, bool xmm, u32 addr_const, int value_reg)
{
	EE::Profiler.EmitConstMem(addr_const);

	const u8* codeStart = nullptr;
	bool used_fastmem = false;
	auto vmv = vtlbdata.vmap[addr_const >> VTLB_PAGE_BITS];
	if (!vmv.isHandler(addr_const))
	{
		if (!xmm)
		{
            recBeginOaknutEmit();
            oak::XReg regX = oakXRegister(value_reg);
            if (vtlbCanUseConstFastmem() && value_reg != EE_HOST_RCX)
			{
                vtlbConstFastmemAddress_emit_oaknut(addr_const);
                codeStart = oakGetCurrentCodePointer();
                switch (bits)
                {
                    case 8:
                        vtlbFastmemWriteU8_emit_oaknut(regX, oak::util::X1);
                        break;

                    case 16:
                        vtlbFastmemWriteU16_emit_oaknut(regX, oak::util::X1);
                        break;

                    case 32:
                        vtlbFastmemWriteU32_emit_oaknut(regX, oak::util::X1);
                        break;

                    case 64:
                        vtlbFastmemWriteU64_emit_oaknut(regX, oak::util::X1);
                        break;
                }
                used_fastmem = true;
			}
            else
            {
                if (regX.index() == oak::util::X0.index() || regX.index() == oak::util::X1.index())
                {
                    oakAsm->MOV(OAK_XSCRATCH2, regX);
                    regX = OAK_XSCRATCH2;
                }
                const oak::XReg ptr = vtlbConstDirectPtr_emit_oaknut(addr_const);
                switch (bits)
                {
                    case 8:
                        vtlbConstDirectWriteU8_emit_oaknut(regX, ptr);
                        break;

                    case 16:
                        vtlbConstDirectWriteU16_emit_oaknut(regX, ptr);
                        break;

                    case 32:
                        vtlbConstDirectWriteU32_emit_oaknut(regX, ptr);
                        break;

                    case 64:
                        vtlbConstDirectWriteU64_emit_oaknut(regX, ptr);
                        break;

                        jNO_DEFAULT
                }
            }
            recEndOaknutEmit();
		}
		else
		{
            recBeginOaknutEmit();
            if (vtlbCanUseConstFastmem() && bits == 128)
            {
                vtlbConstFastmemAddress_emit_oaknut(addr_const);
                codeStart = oakGetCurrentCodePointer();
                vtlbFastmemWriteQ128_emit_oaknut(oakQRegister(value_reg), oak::util::X1);
                used_fastmem = true;
            }
            else if (vtlbCanUseConstFastmem() && bits == 32)
            {
                vtlbConstFastmemAddress_emit_oaknut(addr_const);
                codeStart = oakGetCurrentCodePointer();
                vtlbFastmemWriteF32_emit_oaknut(oakSRegister(value_reg), oak::util::X1);
                used_fastmem = true;
            }
            else
			{
                const oak::XReg ptr = vtlbConstDirectPtr_emit_oaknut(addr_const);
                switch (bits)
                {
                    case 32:
                        vtlbConstDirectWriteF32_emit_oaknut(oakSRegister(value_reg), ptr);
                        break;

                    case 128:
                        vtlbConstDirectWriteQ128_emit_oaknut(oakQRegister(value_reg), ptr);
                        break;

                        jNO_DEFAULT
                }
            }
            recEndOaknutEmit();
		}
	}
	else
	{
		// has to: translate, find function, call function
		u32 paddr = vmv.assumeHandlerGetPAddr(addr_const);

		int szidx = 0;
		switch (bits)
		{
			case 8:
				szidx = 0;
				break;
			case 16:
				szidx = 1;
				break;
			case 32:
				szidx = 2;
				break;
			case 64:
				szidx = 3;
				break;
			case 128:
				szidx = 4;
				break;
		}

		if ((bits == 32) && !xmm && vtlbCanDirectWriteConstHw32(paddr))
		{
			recBeginOaknutEmit();
			vtlbWriteHw32Gpr_emit_oaknut(paddr, oakXRegister(value_reg));
			recEndOaknutEmit();
			return;
		}

		const u32 guest_pc = GetCurrentGuestPC();
		iFlushCall(FLUSH_FULLVTLB);

        _freeX86reg(EE_HOST_RCX);

		if ((bits == 32) && !xmm && paddr == DMAC_STAT)
		{
			_freeX86reg(EE_HOST_RDX);
			recBeginOaknutEmit();
			recFlushReccycle();
			vtlbWriteGuestPC_emit_oaknut(guest_pc);
			vtlbWriteDmacStatGpr_emit_oaknut(oakXRegister(value_reg));
			oakEmitCall(reinterpret_cast<const void*>(cpuTestDMACInts));
			recReloadReccycle();
			recEndOaknutEmit();
			return;
		}

		if (bits == 128)
		{
			pxAssert(xmm);
			_freeXMMreg(0);
            recBeginOaknutEmit();
			vtlbWriteGuestPC_emit_oaknut(guest_pc);
            vtlbCallConstWriteHandlerQ128_emit_oaknut(paddr, oakQRegister(value_reg), vmv.assumeHandlerGetRaw(szidx, true));
            recEndOaknutEmit();
		}
		else if (xmm)
		{
			pxAssert(bits == 32);
            _freeX86reg(EE_HOST_RDX);
            recBeginOaknutEmit();
			vtlbWriteGuestPC_emit_oaknut(guest_pc);
            vtlbCallConstWriteHandlerF32_emit_oaknut(paddr, oakSRegister(value_reg), vmv.assumeHandlerGetRaw(szidx, true));
            recEndOaknutEmit();
		}
		else
		{
            _freeX86reg(EE_HOST_RDX);
            recBeginOaknutEmit();
			vtlbWriteGuestPC_emit_oaknut(guest_pc);
            vtlbCallConstWriteHandlerGpr_emit_oaknut(paddr, oakXRegister(value_reg), vmv.assumeHandlerGetRaw(szidx, true));
            recEndOaknutEmit();
		}
	}

	if (used_fastmem)
	{
		vtlb_AddLoadStoreInfo((uptr)codeStart, static_cast<u32>(oakGetCurrentCodePointer() - codeStart),
			GetCurrentGuestPC(), GetAllocatedGPRBitmask(), GetAllocatedXMMBitmask(),
			static_cast<u8>(EE_HOST_RCX), static_cast<u8>(value_reg),
			static_cast<u8>(bits), false, false, xmm);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////
//							Extra Implementations

//   ecx - virtual address
//   Returns physical address in eax.
//   Clobbers edx
void vtlb_DynV2P()
{
    recBeginOaknutEmit();
    vtlbV2P_emit_oaknut();
    recEndOaknutEmit();
}

static void DynBackpatchLoadStoreImpl(uptr code_address, u32 code_size, u32 guest_pc, u32 guest_addr,
	u32 gpr_bitmask, u32 fpr_bitmask, u8 address_register, u8 data_register,
	u8 size_in_bits, bool is_signed, bool is_load, bool is_xmm, bool ir_fastmem_load,
	bool ir_delay_slot = false)
{
	static constexpr u32 GPR_SIZE = 8;
	static constexpr u32 XMM_SIZE = 16;

	// on win32, we need to reserve an additional 32 bytes shadow space when calling out to C
#ifdef _WIN32
	static constexpr u32 SHADOW_SIZE = 32;
#else
	static constexpr u32 SHADOW_SIZE = 0;
#endif

    std::bitset<iREGCNT_GPR> stack_gpr;
    std::bitset<iREGCNT_XMM> stack_xmm;

	u8* thunk = recBeginThunk();

	// save regs
	u32 i, stack_offset;
    u32 num_gprs = 0, num_fprs = 0;

	for (i = 0; i < iREGCNT_GPR; ++i)
	{
        if ((gpr_bitmask & (1u << i)) && oakIsCallerSaved(i) && (!is_load || is_xmm || data_register != i)) {
            num_gprs++;
            stack_gpr.set(i);
        }
	}
	for (i = 0; i < iREGCNT_XMM; ++i)
	{
		if (fpr_bitmask & (1u << i) && oakIsCallerSavedXmm(i) && (!is_load || !is_xmm || data_register != i)) {
            num_fprs++;
            stack_xmm.set(i);
        }
	}

	const u32 stack_size = (((num_gprs + 1) & ~1u) * GPR_SIZE) + (num_fprs * XMM_SIZE) + SHADOW_SIZE;
	if (stack_size > 0)
	{
        recBeginOaknutEmit();
        vtlbBackpatchSubSp_emit_oaknut(stack_size);

		stack_offset = SHADOW_SIZE;
        for (i = 0; i < iREGCNT_XMM; ++i)
        {
            if(stack_xmm[i])
			{
                vtlbBackpatchSaveQ_emit_oaknut(oakQRegister(i), stack_offset);
                stack_offset += XMM_SIZE;
            }
        }
        ////
        for (i = 0; i < iREGCNT_GPR; ++i)
        {
            if(stack_gpr[i])
            {
                vtlbBackpatchSaveX_emit_oaknut(oakXRegister(i), stack_offset);
                stack_offset += GPR_SIZE;
            }
        }
        recEndOaknutEmit();
	}

	if (is_load)
	{
		recBeginOaknutEmit();
		vtlbWriteGuestPC_emit_oaknut(guest_pc);
		if (ir_fastmem_load)
		{
			// IR fast loads may omit hot PC/BD stores. Establish exact fault
			// context before any VTLB handler, including nonreturning ones.
			oakAsm->MOV(OAK_WSCRATCH, ir_delay_slot ? 1u : 0u);
			oakStore32(OAK_WSCRATCH, {oak::util::X27, static_cast<s64>(offsetof(cpuRegistersPack, cpuRegs.IsDelaySlot))});
		}
		DynGen_PrepRegs_emit_oaknut(address_register, -1, size_in_bits, is_xmm, ir_fastmem_load);
		DynGen_HandlerTest_emit_oaknut([size_in_bits, is_signed]() {DynGen_DirectRead_emit_oaknut(size_in_bits, is_signed); },  0, size_in_bits, is_signed && size_in_bits <= 32);
		recEndOaknutEmit();

		if (size_in_bits == 128)
		{
            recBeginOaknutEmit();
            vtlbBackpatchMoveLoadResult_emit_oaknut(data_register, size_in_bits, is_xmm);
            recEndOaknutEmit();
		}
		else
		{
			if (is_xmm)
			{
                recBeginOaknutEmit();
                vtlbBackpatchMoveLoadResult_emit_oaknut(data_register, size_in_bits, is_xmm);
                recEndOaknutEmit();
			}
			else
			{
                recBeginOaknutEmit();
                vtlbBackpatchMoveLoadResult_emit_oaknut(data_register, size_in_bits, is_xmm);
                recEndOaknutEmit();
			}
		}
	}
	else
	{
		recBeginOaknutEmit();
		vtlbWriteGuestPC_emit_oaknut(guest_pc);
		DynGen_PrepRegs_emit_oaknut(address_register, data_register, size_in_bits, is_xmm);
		DynGen_HandlerTest_emit_oaknut([size_in_bits]() { DynGen_DirectWrite_emit_oaknut(size_in_bits); }, 1, size_in_bits);
		recEndOaknutEmit();
	}

	// restore regs
	if (stack_size > 0)
	{
		stack_offset = SHADOW_SIZE;
        recBeginOaknutEmit();
		for (i = 0; i < iREGCNT_XMM; ++i)
		{
            if(stack_xmm[i])
			{
                vtlbBackpatchRestoreQ_emit_oaknut(oakQRegister(i), stack_offset);
				stack_offset += XMM_SIZE;
			}
		}
        ////
		for (i = 0; i < iREGCNT_GPR; ++i)
		{
            if(stack_gpr[i])
			{
                vtlbBackpatchRestoreX_emit_oaknut(oakXRegister(i), stack_offset);
				stack_offset += GPR_SIZE;
			}
		}

        vtlbBackpatchAddSp_emit_oaknut(stack_size);
        recEndOaknutEmit();
	}

    recBeginOaknutEmit();
    oakEmitJmp(reinterpret_cast<const void*>(code_address + code_size));
    recEndOaknutEmit();

	recEndThunk();

	// backpatch to a jump to the slowmem handler
//	x86Ptr = (u8*)code_address;
    oakEmitJmpPtr((void*)code_address, thunk, true);
}

void vtlb_DynBackpatchLoadStore(uptr code_address, u32 code_size, u32 guest_pc, u32 guest_addr,
	u32 gpr_bitmask, u32 fpr_bitmask, u8 address_register, u8 data_register,
	u8 size_in_bits, bool is_signed, bool is_load, bool is_xmm)
{
	DynBackpatchLoadStoreImpl(code_address, code_size, guest_pc, guest_addr,
		gpr_bitmask, fpr_bitmask, address_register, data_register, size_in_bits,
		is_signed, is_load, is_xmm, false);
}

#if defined(ARCH_ARM64) && defined(__ANDROID__)
void vtlb_DynBackpatchLoad32Ir(uptr code_address, u32 guest_pc, u32 guest_addr, bool delay_slot)
{
	constexpr u32 IR_LIVE_Q2_Q7 = 0xFC;
	constexpr u8 IR_REGISTER_W0 = 0;
	constexpr u8 IR_LOAD_BITS = 32;
	DynBackpatchLoadStoreImpl(code_address, 4, guest_pc, guest_addr, 0,
		IR_LIVE_Q2_Q7, IR_REGISTER_W0, IR_REGISTER_W0, IR_LOAD_BITS,
		false, true, false, true, delay_slot);
}
#endif

