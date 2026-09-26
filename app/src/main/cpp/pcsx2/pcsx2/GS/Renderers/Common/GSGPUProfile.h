// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <string_view>

// Every recognised mobile GPU renders through one unified, driver-agnostic tile path:
//  - Mobile: Mali, Immortalis and PowerVR.
//  - Adreno: identity only. Rendering is identical to the Mobile path; the value is kept
//    separate because a few optional features explicitly require Adreno hardware (frame
//    generation, custom Vulkan drivers).
// There are no model tables and no per-driver-version rules; every part resolves to the same
// tile-friendly behaviour, and actual performance scales with the hardware itself.
enum class RuntimeGpuProfile : u8
{
	Unknown,
	Mobile,
	Adreno,
};

// True for every recognised mobile GPU (Mali, Immortalis, PowerVR and Adreno). The profile
// value only carries identity; rendering never forks on it.
constexpr bool UsesMobileGpuPath(RuntimeGpuProfile value)
{
	return value != RuntimeGpuProfile::Unknown;
}

class GpuProfileDetector
{
public:
	static const char* RuntimeProfileToString(RuntimeGpuProfile value);

	// Classifies the GPU from the renderer/device name and the vendor string. The renderer
	// name is authoritative; the vendor string is only consulted when the name is not
	// conclusive. Every recognised mobile part resolves to a mobile profile, and all of them
	// share the same rendering path.
	static RuntimeGpuProfile Detect(std::string_view gpu_vendor, std::string_view gpu_renderer_or_name);

	// Returns the Adreno generation (7 for 7xx, 8 for 8xx, 9 for the X series) parsed from a
	// renderer/device name, or 0 when the name is not a recognizable Adreno part.
	static u32 ParseAdrenoGeneration(std::string_view gpu_renderer_or_name);

	// True when the hint text identifies a MediaTek SoC part. Exposed for tests; the Android
	// property scan below uses the same matching.
	static bool LooksLikeMediaTekSoC(std::string_view hints);

	// Scans the Android system properties. This identifies the SoC (used by the MediaTek ANGLE
	// option and the per-game Tekken 5 fix), never the GPU family.
	static bool DetectMediaTekSoC();
};
