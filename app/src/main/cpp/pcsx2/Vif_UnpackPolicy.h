// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Common.h"

enum class VifInvalidUnpackPolicy
{
	Stable,
	TransitionalRequiresHardwareValidation,
};

inline VifInvalidUnpackPolicy GetVifInvalidUnpackPolicy(int upknum)
{
	switch (upknum)
	{
		case 3:
		case 7:
		case 11:
			return VifInvalidUnpackPolicy::TransitionalRequiresHardwareValidation;

		default:
			return VifInvalidUnpackPolicy::Stable;
	}
}

inline bool VifUsesTransitionalInvalidUnpackPolicy(int upknum)
{
	return GetVifInvalidUnpackPolicy(upknum) ==
		VifInvalidUnpackPolicy::TransitionalRequiresHardwareValidation;
}

inline void AssertVifTransitionalInvalidUnpackPolicy(int upknum)
{
	pxAssertRel(VifUsesTransitionalInvalidUnpackPolicy(upknum),
		"VIF invalid-unpack policy unexpectedly changed.");
}

inline void WarnOnceAboutTransitionalVifUnpack(int upknum)
{
	AssertVifTransitionalInvalidUnpackPolicy(upknum);

	static bool s_warning_emitted[16] = {};
	pxAssert(upknum >= 0 && upknum < static_cast<int>(std::size(s_warning_emitted)));

	if (s_warning_emitted[upknum])
		return;

	s_warning_emitted[upknum] = true;
	Console.Warning("Vpu/Vif: Invalid Unpack %d", upknum);
}
