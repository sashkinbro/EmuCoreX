// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "../../core/state/CoreStateExposure.h"

inline Arm64BackendRuntime& GetArm64BackendRuntime()
{
	return CORE_STATE_RUNTIME_PACK.runtime;
}

inline Arm64BackendConfig& GetArm64BackendConfig()
{
	return GetArm64BackendRuntime().config;
}

inline void Arm64RefreshRuntimeBackendConfig()
{
	Arm64BackendConfig& config = GetArm64BackendConfig();
	config.FPUFPCR = EmuConfig.Cpu.FPUFPCR;
	config.FPUDivFPCR = EmuConfig.Cpu.FPUDivFPCR;
	config.VU0FPCR = EmuConfig.Cpu.VU0FPCR;
	config.VU1FPCR = EmuConfig.Cpu.VU1FPCR;
}

inline void Arm64PrepareRuntimeForStateLoad()
{
	// Runtime-only helper banks are rebuilt or mirrored explicitly after load.
}

inline void Arm64RepairRuntimeAfterStateLoad()
{
	Arm64RefreshRuntimeBackendConfig();
}

enum class Arm64DynBackpatchLoadStorePolicy
{
	UnsupportedHardFail,
};

enum class Arm64VuJitStateSerializationPolicy
{
	CompatibilityPlaceholder,
};

inline Arm64DynBackpatchLoadStorePolicy Arm64GetDynBackpatchLoadStorePolicy()
{
	return Arm64DynBackpatchLoadStorePolicy::UnsupportedHardFail;
}

inline bool Arm64SupportsDynBackpatchLoadStore()
{
	return false;
}

inline bool Arm64UsesHardFailDynBackpatchStub()
{
	return Arm64GetDynBackpatchLoadStorePolicy() == Arm64DynBackpatchLoadStorePolicy::UnsupportedHardFail;
}

inline Arm64VuJitStateSerializationPolicy Arm64GetVuJitStateSerializationPolicy()
{
	return Arm64VuJitStateSerializationPolicy::CompatibilityPlaceholder;
}

inline bool Arm64UsesVuJitPlaceholderState()
{
	return Arm64GetVuJitStateSerializationPolicy() == Arm64VuJitStateSerializationPolicy::CompatibilityPlaceholder;
}

inline bool Arm64HasStableVuJitStateSerialization()
{
	return false;
}

inline bool Arm64UsesCompatibilityVuJitPlaceholderState()
{
	return Arm64UsesVuJitPlaceholderState() && !Arm64HasStableVuJitStateSerialization();
}

inline bool Arm64ShouldSynchronizeVuThreadBeforePlaceholderFreezeOnSave()
{
	return true;
}

inline constexpr size_t Arm64GetVuJitPlaceholderStateBlockSize()
{
	return 96;
}

inline constexpr size_t Arm64GetVuJitPlaceholderStateBlockCount()
{
	return 2;
}
