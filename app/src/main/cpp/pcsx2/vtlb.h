// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "vtlbDef.h"
#include "common/HostSys.h"

static const uptr VTLB_AllocUpperBounds = _1gb * 2;

extern bool vtlb_Core_Alloc();
extern void vtlb_Core_Free();
extern void vtlb_Alloc_Ppmap();
extern void vtlb_Init();
extern void vtlb_Shutdown();
extern void vtlb_Reset();
extern void vtlb_ResetFastmem();

extern vtlbHandler vtlb_NewHandler();

extern vtlbHandler vtlb_RegisterHandler(
	vtlbMemR8FP* r8,vtlbMemR16FP* r16,vtlbMemR32FP* r32,vtlbMemR64FP* r64,vtlbMemR128FP* r128,
	vtlbMemW8FP* w8,vtlbMemW16FP* w16,vtlbMemW32FP* w32,vtlbMemW64FP* w64,vtlbMemW128FP* w128
);

extern void vtlb_ReassignHandler( vtlbHandler rv,
	vtlbMemR8FP* r8,vtlbMemR16FP* r16,vtlbMemR32FP* r32,vtlbMemR64FP* r64,vtlbMemR128FP* r128,
	vtlbMemW8FP* w8,vtlbMemW16FP* w16,vtlbMemW32FP* w32,vtlbMemW64FP* w64,vtlbMemW128FP* w128
);


extern void vtlb_MapHandler(vtlbHandler handler,u32 start,u32 size);
extern void vtlb_MapBlock(void* base,u32 start,u32 size,u32 blocksize=0);
extern void* vtlb_GetPhyPtr(u32 paddr);
//extern void vtlb_Mirror(u32 new_region,u32 start,u32 size); // -> not working yet :(
extern u32  vtlb_V2P(u32 vaddr);
extern void vtlb_DynV2P();

//virtual mappings
extern void vtlb_VMap(u32 vaddr,u32 paddr,u32 sz);
extern void vtlb_VMapBuffer(u32 vaddr,void* buffer,u32 sz);
extern void vtlb_VMapUnmap(u32 vaddr,u32 sz);
extern bool vtlb_ResolveFastmemMapping(uptr* addr);
extern bool vtlb_GetGuestAddress(uptr host_addr, u32* guest_addr);
extern void vtlb_UpdateFastmemProtection(u32 paddr, u32 size, PageProtectionMode prot);
extern bool vtlb_BackpatchLoadStore(uptr code_address, uptr fault_address);

extern void vtlb_ClearLoadStoreInfo();
extern void vtlb_AddLoadStoreInfo(uptr code_address, u32 code_size, u32 guest_pc, u32 gpr_bitmask, u32 fpr_bitmask, u8 address_register, u8 data_register, u8 size_in_bits, bool is_signed, bool is_load, bool is_fpr);
extern void vtlb_DynBackpatchLoadStore(uptr code_address, u32 code_size, u32 guest_pc, u32 guest_addr, u32 gpr_bitmask, u32 fpr_bitmask, u8 address_register, u8 data_register, u8 size_in_bits, bool is_signed, bool is_load, bool is_fpr);
extern bool vtlb_IsFaultingPC(u32 guest_pc);

//Memory functions

template< typename DataType >
extern DataType vtlb_memRead(u32 mem);
extern RETURNS_R128 vtlb_memRead128(u32 mem);

template< typename DataType >
extern void vtlb_memWrite(u32 mem, DataType value);
extern void TAKES_R128 vtlb_memWrite128(u32 mem, r128 value);

// "Safe" variants of vtlb, designed for external tools.
// These routines only access the various RAM, and will not call handlers
// which has the potential to change hardware state.
template <typename DataType>
extern DataType vtlb_ramRead(u32 mem);
template <typename DataType>
extern bool vtlb_ramWrite(u32 mem, const DataType& value);

// NOTE: Does not call MMIO handlers.
extern int vtlb_memSafeCmpBytes(u32 mem, const void* src, u32 size);
extern bool vtlb_memSafeReadBytes(u32 mem, void* dst, u32 size);
extern bool vtlb_memSafeWriteBytes(u32 mem, const void* src, u32 size);

using vtlb_ReadRegAllocCallback = int(*)();
extern int vtlb_DynGenReadNonQuad(u32 bits, bool sign, bool xmm, int addr_reg, vtlb_ReadRegAllocCallback dest_reg_alloc = nullptr);
extern int vtlb_DynGenReadNonQuad_Const(u32 bits, bool sign, bool xmm, u32 addr_const, vtlb_ReadRegAllocCallback dest_reg_alloc = nullptr);
extern int vtlb_DynGenReadQuad(u32 bits, int addr_reg, vtlb_ReadRegAllocCallback dest_reg_alloc = nullptr);
extern int vtlb_DynGenReadQuad_Const(u32 bits, u32 addr_const, vtlb_ReadRegAllocCallback dest_reg_alloc = nullptr);

extern void vtlb_DynGenWrite(u32 sz, bool xmm, int addr_reg, int value_reg);
extern void vtlb_DynGenWrite_Const(u32 bits, bool xmm, u32 addr_const, int value_reg);

extern void vtlb_DynGenDispatchers();

enum vtlb_ProtectionMode
{
	ProtMode_None = 0, // page is 'unaccounted' -- neither protected nor unprotected
	ProtMode_Write, // page is under write protection (exception handler)
	ProtMode_Manual, // page is under manual protection (self-checked at execution)
	ProtMode_NotRequired // page doesn't require any protection
};

extern vtlb_ProtectionMode mmap_GetRamPageInfo(u32 paddr);
extern void mmap_MarkCountedRamPage(u32 paddr);
extern void mmap_ResetBlockTracking();

// --------------------------------------------------------------------------------------
//  Goemon game fix
// --------------------------------------------------------------------------------------
struct GoemonTlb {
	u32 valid;
	u32 unk1; // could be physical address also
	u32 unk2;
	u32 low_add;
	u32 physical_add;
	u32 unk3; // likely the size
	u32 high_add;
	u32 key; // uniq number attached to an allocation
	u32 unk5;
};
