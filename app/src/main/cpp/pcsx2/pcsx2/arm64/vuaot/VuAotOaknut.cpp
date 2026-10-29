// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+
#include "VuAotOaknut.h"
namespace vuaot
{
	thread_local a64::MacroAssembler* armAsm = nullptr;
	thread_local ArmAddressRecorder* armAddressRecorder = nullptr;
	void armMoveAddressToReg(oaknut::XReg r, const void* p)
	{
		u8* at = armGetCurrentCodePointer();
		const u64 value = reinterpret_cast<uintptr_t>(p);
		auto& cg = armAsm->Raw();
		// Fixed shape makes every absolute pointer independently relocatable.
		cg.MOVZ(r, {static_cast<u16>(value), oaknut::MovImm16Shift::SHL_0});
		cg.MOVK(r, {static_cast<u16>(value >> 16), oaknut::MovImm16Shift::SHL_16});
		cg.MOVK(r, {static_cast<u16>(value >> 32), oaknut::MovImm16Shift::SHL_32});
		cg.MOVK(r, {static_cast<u16>(value >> 48), oaknut::MovImm16Shift::SHL_48});
		if (armAddressRecorder)
			armAddressRecorder->OnMovRun(at, 4, p);
	}
	static void Branch(const void* p, bool call)
	{
		u8* at = armGetCurrentCodePointer();
		const int64_t delta = static_cast<int64_t>(reinterpret_cast<uintptr_t>(p)) - static_cast<int64_t>(reinterpret_cast<uintptr_t>(at));
		if ((delta & 3) == 0 && delta >= -(1LL << 27) && delta < (1LL << 27))
		{
			if (call)
				armAsm->Raw().BL(p);
			else
				armAsm->Raw().B(p);
			if (armAddressRecorder)
				armAddressRecorder->OnBranch(at, p, ArmAddressRecorder::BranchForm::Rel26);
		}
		else
		{
			armMoveAddressToReg(oaknut::XReg{17}, p);
			if (call)
				armAsm->Raw().BLR(oaknut::XReg{17});
			else
				armAsm->Raw().BR(oaknut::XReg{17});
		}
	}
	void armEmitCall(const void* p) { Branch(p, true); }
	void armEmitJmp(const void* p) { Branch(p, false); }
} // namespace vuaot
