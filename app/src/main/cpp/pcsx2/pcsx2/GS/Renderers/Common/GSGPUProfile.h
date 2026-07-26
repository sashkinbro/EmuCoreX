// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <cstdint>
#include <string>
#include <string_view>

enum class GpuProfileOverride : u8
{
	Auto,
	Mali,
	Adreno,
	PowerVR,
};

enum class RuntimeGpuProfile : u8
{
	Unknown,
	Mali,
	Adreno,
	PowerVR,
};

enum class MobileGpuArchitecture : u8
{
	Unknown,
	Adreno2xx,
	Adreno3xx,
	Adreno4xx,
	Adreno5xx,
	Adreno6xx,
	Adreno7xx,
	Adreno8xx,
	AdrenoX,
	MaliUtgard,
	MaliMidgard,
	MaliBifrost,
	MaliValhall1,
	MaliValhall2,
	MaliValhall3,
	MaliFifthGen,
	MaliG1,
	PowerVRSeries5,
	PowerVRRogue,
	PowerVRVolcanic,
	PowerVR,
};

enum class MobileGpuApi : u8
{
	Unknown,
	OpenGL,
	Vulkan,
};

// Keep this independent from VkDriverId so profile detection remains unit-testable without
// including Vulkan headers.
enum class MobileGpuDriver : u8
{
	Unknown,
	ArmProprietary,
	MesaPanVK,
	QualcommProprietary,
	MesaTurnip,
	ImaginationProprietary,
	MesaPowerVR,
	Angle,
};

enum class DriverProfileConfidence : u8
{
	Unknown,
	Vendor,
	Model,
	Driver,
	DriverVersion,
};

enum class DriverBug : u8
{
	BrokenBufferStreaming,
	BrokenUnsynchronizedMapping,
	BrokenNegatedBoolean,
	BrokenVectorBitwiseAnd,
	BrokenBitwiseOpNegation,
	BrokenPrimitiveRestart,
	BrokenPushDescriptors,
	BrokenProvokingVertex,
	BrokenAttachmentFeedbackLoopLayout,
	BrokenSubpassFeedback,
	BrokenColorWriteMaskWithDepthTest,
	BrokenDepthStencilDiscard,
	BrokenReversedDepthRange,
	SlowCachedReadbackMemory,
	SlowOptimalImageToBufferCopy,
	BrokenClearLoadOpRenderPass,
	Broken16BitTextureFormats,
	BrokenGenerateMipmapTallTexture,
	BrokenEmptyRenderPass,
	BrokenConstantLoad,
	BrokenUniformIndexing,
	BrokenVSync,
	BrokenMultithreadedShaderCompilation,
	BrokenDynamicRendering,
	BrokenImagelessFramebuffer,
	BrokenExtendedDynamicState,
	BrokenPrimitiveTopologyDynamicState,
	BrokenGraphicsPipelineLibrary,
	Count,
};

enum class DriverWorkaround : u8
{
	RewriteBooleanNegation,
	ScalarizeVectorBitwiseAnd,
	StoreBitwiseNegationInTemporary,
	UseDescriptorSets,
	DisableProvokingVertex,
	DisableAttachmentFeedbackLoopLayout,
	EmulateColorWriteMask,
	PreferCoherentReadback,
	AvoidClearLoadOpRenderPass,
	GenerateMipmapManuallyForTallTextures,
	RewriteUniformIndexing,
	ForceFifoPresent,
	AlignSwapchainWidthTo32,
	Count,
};

struct MobileDriverVersion
{
	u32 raw = 0;
	u16 major = 0;
	u16 minor = 0;
	u16 patch = 0;
	u32 build = 0;
	bool known = false;
	bool legacy_hash = false;
};

struct MobileDriverContext
{
	MobileGpuApi api = MobileGpuApi::Unknown;
	u32 vendor_id = 0;
	u32 device_id = 0;
	u32 driver_version = 0;
	u32 driver_id = 0;
	u32 api_version = 0;
	u32 android_sdk = 0;
	u32 max_draw_indirect_count = 0;
	std::string_view driver_name;
	std::string_view driver_info;
	std::string_view api_version_string;
};

struct MobileDriverProfile
{
	static constexpr u32 DATABASE_VERSION = 1;

	MobileGpuApi api = MobileGpuApi::Unknown;
	MobileGpuDriver driver = MobileGpuDriver::Unknown;
	MobileDriverVersion version;
	u64 bugs = 0;
	u64 workarounds = 0;
	u32 matched_rule_count = 0;
	DriverProfileConfidence confidence = DriverProfileConfidence::Unknown;
	bool conservative_fallback = true;
	std::string driver_name;

	constexpr bool HasBug(DriverBug bug) const
	{
		return (bugs & (u64{1} << static_cast<u8>(bug))) != 0;
	}

	constexpr bool UsesWorkaround(DriverWorkaround workaround) const
	{
		return (workarounds & (u64{1} << static_cast<u8>(workaround))) != 0;
	}
};

static_assert(static_cast<u8>(DriverBug::Count) <= 64);
static_assert(static_cast<u8>(DriverWorkaround::Count) <= 64);

struct MobileGsTuning
{
	bool constrained = true;
	bool prefer_new_textures = true;
	u32 pooled_targets = 96;
	u32 target_age = 8;
	u32 pooled_textures = 96;
	u32 texture_age = 6;
};

struct MobileGpuIdentity
{
	MobileGpuArchitecture architecture = MobileGpuArchitecture::Unknown;
	u16 model_number = 0;
	u8 core_count = 0;
	bool recognized = false;
	std::string name = "Unknown";
};

struct GpuProfileSelection
{
	GpuProfileOverride override_mode = GpuProfileOverride::Auto;
	RuntimeGpuProfile runtime_profile = RuntimeGpuProfile::Unknown;
	bool is_mediatek_soc = false;
	MobileGpuIdentity gpu;
	MobileGsTuning gs_tuning;
	MobileDriverProfile driver;
	std::string hints;
};

class GpuProfileDetector
{
public:
	static GpuProfileOverride ParseOverride(std::string_view value);
	static const char* OverrideToConfigString(GpuProfileOverride value);
	static const char* OverrideToString(GpuProfileOverride value);
	static const char* RuntimeProfileToString(RuntimeGpuProfile value);
	static const char* ArchitectureToString(MobileGpuArchitecture value);
	static const char* ApiToString(MobileGpuApi value);
	static const char* DriverToString(MobileGpuDriver value);
	static const char* BugToString(DriverBug value);
	static const char* WorkaroundToString(DriverWorkaround value);

	static GpuProfileSelection Resolve(std::string_view override_value, std::string_view gpu_vendor,
		std::string_view gpu_renderer_or_name);
	static GpuProfileSelection Resolve(std::string_view override_value, std::string_view gpu_vendor,
		std::string_view gpu_renderer_or_name, const MobileDriverContext& driver_context);
};
