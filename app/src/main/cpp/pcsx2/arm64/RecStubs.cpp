// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#include "common/Console.h"
#include "MTVU.h"
#include "SaveState.h"
#include "arm64/policy/Arm64RuntimePolicy.h"
#include "vtlb.h"

#include "common/Assertions.h"

namespace
{
void Arm64AssertDynBackpatchStubContract()
{
	pxAssertRel(
		Arm64GetDynBackpatchLoadStorePolicy() == Arm64DynBackpatchLoadStorePolicy::UnsupportedHardFail,
		"ARM64 dyn backpatch policy unexpectedly changed.");
	pxAssertRel(!Arm64SupportsDynBackpatchLoadStore(), "ARM64 dyn backpatch unexpectedly marked supported.");
	pxAssertRel(Arm64UsesHardFailDynBackpatchStub(), "ARM64 dyn backpatch failure mode unexpectedly changed.");
}

void Arm64FailMissingDynBackpatchLoadStore(uptr code_address, u32 code_size, u32 guest_pc, u32 guest_addr)
{
	Arm64AssertDynBackpatchStubContract();
	pxFailRel("ARM64 dyn backpatch load/store is unsupported (guest_pc=%x guest_addr=%x code=%p size=%u).",
		guest_pc, guest_addr, reinterpret_cast<void*>(code_address), code_size);
}

void Arm64AssertVuJitPlaceholderStateContract()
{
	pxAssertRel(
		Arm64GetVuJitStateSerializationPolicy() == Arm64VuJitStateSerializationPolicy::CompatibilityPlaceholder,
		"ARM64 VU JIT serialization policy unexpectedly changed.");
	pxAssertRel(Arm64UsesCompatibilityVuJitPlaceholderState(), "ARM64 VU JIT placeholder path unexpectedly disabled.");
}

void Arm64PrepareVuJitPlaceholderStateForFreeze(SaveStateBase& state)
{
	if (state.IsSaving() && Arm64ShouldSynchronizeVuThreadBeforePlaceholderFreezeOnSave())
		vu1Thread.WaitVU();
}

bool Arm64FreezeVuJitPlaceholderStateBlocks(SaveStateBase& state)
{
	// Transitional placeholder until ARM64 VU JIT state gets a real ownership-backed format.
	return state.FreezePlaceholderBlocks(
		Arm64GetVuJitPlaceholderStateBlockSize(),
		Arm64GetVuJitPlaceholderStateBlockCount());
}

bool Arm64FreezeCompatibilityVuJitState(SaveStateBase& state)
{
	Arm64AssertVuJitPlaceholderStateContract();
	Arm64PrepareVuJitPlaceholderStateForFreeze(state);
	return Arm64FreezeVuJitPlaceholderStateBlocks(state);
}
}  // namespace

void vtlb_DynBackpatchLoadStore(uptr code_address, u32 code_size, u32 guest_pc, u32 guest_addr, u32 gpr_bitmask, u32 fpr_bitmask, u8 address_register, u8 data_register, u8 size_in_bits, bool is_signed, bool is_load, bool is_fpr)
{
	Arm64FailMissingDynBackpatchLoadStore(code_address, code_size, guest_pc, guest_addr);
}

bool SaveStateBase::vuJITFreeze()
{
	return Arm64FreezeCompatibilityVuJitState(*this);
}
