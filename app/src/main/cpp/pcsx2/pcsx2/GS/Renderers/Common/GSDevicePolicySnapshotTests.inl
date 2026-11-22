// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Debug-only SNAPSHOT of every GPU-identity-driven decision the GS device policies make, for a
// fixed table of GPU identities. It is the baseline for the GPU-name gate refactor: after each
// refactor step the dump is compared with GSDevicePolicySnapshot.golden.inc and any difference is
// printed, so what a step changed is visible line by line.
//
// Pure: no device, no GL/Vulkan context. Policy functions that live in headers are called
// directly. Decisions that are inline expressions in GSDeviceVK.cpp / GSDeviceOGL.cpp (D32 DATE
// fallback, cached stream rings, warmup worker limit, GL Qualcomm checks, vendor
// hint) are mirrored here with the source line they copy; when a refactor moves them behind a
// policy function, repoint the mirror at that function and the golden must stay identical.
//
// Run: tests/Run-JitOracle.ps1 -Suite gs-device-policy-snapshot [-RemoteEnv EMUCOREX_POLICY_SNAPSHOT_DUMP=1]
// Regenerate the golden: run with EMUCOREX_POLICY_SNAPSHOT_DUMP=1, copy the text between
// POLICY_SNAPSHOT_BEGIN / POLICY_SNAPSHOT_END into the golden file between its R"GOLDEN( ... )GOLDEN" lines.
#include "GS/Renderers/Common/GSVulkanRoadDevicePolicy.h"
#include "GS/Renderers/Common/GSVulkanDevicePolicy.h"
#include "GS/Renderers/Common/GSBlendConstantPolicy.h"
#include "GS/Renderers/Common/GSGPUProfile.h"
#include "GS/Renderers/Vulkan/VKSwapchainCreate.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace PolicySnapshot
{
struct Fixture
{
	const char* id;
	u32 vendor;
	u32 device_id;
	bool driver_props; // VK_KHR_driver_properties present (GetRoadIdentity gates driver/name/info on it)
	int driver_id; // raw VkDriverId
	const char* name; // VkPhysicalDeviceProperties::deviceName
	const char* driver_name;
	const char* info;
	u32 version; // VkPhysicalDeviceProperties::driverVersion
	const char* platform; // m_road_platform_hints
	bool mediatek; // GpuProfileDetector::DetectMediaTekSoC() on that device
	const char* gl_vendor;
	const char* gl_renderer;
	const char* gl_version;
	bool log_caps; // real device: the capture.log capabilities are known
	bool log_roaa;
	u32 log_push;
};

constexpr u32 MV(u32 a, u32 b, u32 c) { return (a << 22) | (b << 12) | c; }

static const Fixture kFixtures[] = {
	// Real devices. Strings from scripts/gs-oracle/results/*/capture.log (device-property lines).
	{"adreno840-qcom-oneplus-CPH2747", 0x5143, 0, true, VK_DRIVER_ID_QUALCOMM_PROPRIETARY, "Adreno (TM) 840",
		"Qualcomm Technologies Inc. Adreno Vulkan Driver",
		"Driver Build: 1c927b3c50, I90301f9f20, 1786608725\nDate: 08/13/26\nCompiler Version: E031.50.19.34\nDriver Branch: ",
		MV(512, 842, 53), "QTI sun qcom OnePlus CPH2747 arm64-v8a", false,
		"Qualcomm", "Adreno (TM) 840", "OpenGL ES 3.2 V@0842.52 (GIT@1c927b3c50, I90301f9f20, 1786608725) (Date:08/13/26)",
		true, true, 32},
	{"adreno750-qcom-lenovo-TB710FU", 0x5143, 0, true, VK_DRIVER_ID_QUALCOMM_PROPRIETARY, "Adreno (TM) 750",
		"Qualcomm Technologies Inc. Adreno Vulkan Driver",
		"Driver Build: 346fca22e2, I26e0f54d9e, 1748952422\nDate: 06/03/25\nCompiler Version: E031.45.02.25\nDriver Branch: ",
		MV(512, 762, 37), "QTI pineapple qcom Lenovo TB710FU arm64-v8a", false,
		"Qualcomm", "Adreno (TM) 750", "OpenGL ES 3.2 V@0762.37 (GIT@346fca22e2, I26e0f54d9e, 1748952422) (Date:06/03/25)",
		true, false, 32},
	{"mali-g57-r40p0-arm-anbernic-RG556", 0x13b5, 0, true, VK_DRIVER_ID_ARM_PROPRIETARY, "Mali-G57",
		"ARM proprietary", "v1.r40p0-01eac0.b0251c048237dcd59e6be15fba11a31a", MV(40, 0, 0),
		"UNISOC ums9620_2h10 ums9620 Anbernic RG556 arm64-v8a", false,
		"ARM", "Mali-G57", "OpenGL ES 3.2 v1.r40p0-01eac0.b0251c048237dcd59e6be15fba11a31a",
		true, true, 0},
	// Synthetic identities (structure is real, exact driver strings are plausible, not captured).
	{"adreno730-turnip-axfl2", 0x5143, 0, true, VK_DRIVER_ID_MESA_TURNIP, "Turnip Adreno (TM) 730",
		"turnip Mesa driver", "Mesa 26.2.0-devel (git-axfl2-1a2b3c4d5e)", MV(26, 2, 0),
		"QTI taro qcom arm64-v8a", false,
		"freedreno", "Turnip Adreno (TM) 730", "OpenGL ES 3.2 Mesa 26.2.0-devel", false, false, 0},
	{"adreno730-turnip-plain", 0x5143, 0, true, VK_DRIVER_ID_MESA_TURNIP, "Turnip Adreno (TM) 730",
		"turnip Mesa driver", "Mesa 25.3.0", MV(25, 3, 0), "QTI taro qcom arm64-v8a", false,
		"freedreno", "Turnip Adreno (TM) 730", "OpenGL ES 3.2 Mesa 25.3.0", false, false, 0},
	{"adreno740-turnip-axfl1", 0x5143, 0, true, VK_DRIVER_ID_MESA_TURNIP, "Turnip Adreno (TM) 740",
		"turnip Mesa driver", "Mesa 26.1.0-devel (git-axfl1-0f0e0d0c0b)", MV(26, 1, 0),
		"QTI kalama qcom arm64-v8a", false,
		"freedreno", "Turnip Adreno (TM) 740", "OpenGL ES 3.2 Mesa 26.1.0-devel", false, false, 0},
	{"adreno650-turnip-axfl1", 0x5143, 0, true, VK_DRIVER_ID_MESA_TURNIP, "Turnip Adreno (TM) 650",
		"turnip Mesa driver", "Mesa 26.1.0-devel (git-axfl1-9a8b7c6d5e)", MV(26, 1, 0),
		"QTI kona qcom arm64-v8a", false,
		"freedreno", "Turnip Adreno (TM) 650", "OpenGL ES 3.2 Mesa 26.1.0-devel", false, false, 0},
	{"adreno650-turnip-plain", 0x5143, 0, true, VK_DRIVER_ID_MESA_TURNIP, "Turnip Adreno (TM) 650",
		"turnip Mesa driver", "Mesa 25.3.0", MV(25, 3, 0), "QTI kona qcom arm64-v8a", false,
		"freedreno", "Turnip Adreno (TM) 650", "OpenGL ES 3.2 Mesa 25.3.0", false, false, 0},
	{"adreno650-turnip-unmapped-driver-id", 0x5143, 0, true, 0, "Turnip Adreno (TM) 650",
		"turnip Mesa driver", "Mesa 25.3.0", MV(25, 3, 0), "QTI kona qcom arm64-v8a", false,
		"freedreno", "Turnip Adreno (TM) 650", "OpenGL ES 3.2 Mesa 25.3.0", false, false, 0},
	{"adreno610-turnip", 0x5143, 0, true, VK_DRIVER_ID_MESA_TURNIP, "Turnip Adreno (TM) 610",
		"turnip Mesa driver", "Mesa 25.1.0", MV(25, 1, 0), "QTI bengal qcom arm64-v8a", false,
		"freedreno", "Turnip Adreno (TM) 610", "OpenGL ES 3.2 Mesa 25.1.0", false, false, 0},
	{"adreno840-qcom-no-driver-properties", 0x5143, 0, false, 0, "Adreno (TM) 840", "", "", MV(512, 842, 53),
		"QTI sun qcom arm64-v8a", false,
		"Qualcomm", "Adreno (TM) 840", "OpenGL ES 3.2 V@0842.52 (GIT@1c927b3c50)", false, false, 0},
	{"adreno-chipid-only-650", 0x5143, 0x06050000u, true, VK_DRIVER_ID_QUALCOMM_PROPRIETARY, "Adreno",
		"Qualcomm Technologies Inc. Adreno Vulkan Driver", "", MV(512, 500, 1), "", false,
		"Qualcomm", "Adreno", "OpenGL ES 3.2 V@0500.1", false, false, 0},
	{"adreno-x1-85-windows-arm", 0x5143, 0, true, VK_DRIVER_ID_QUALCOMM_PROPRIETARY, "Qualcomm(R) Adreno(TM) X1-85 GPU",
		"Qualcomm Technologies Inc. Adreno Vulkan Driver", "", MV(512, 800, 1), "", false,
		"Qualcomm", "Adreno (TM) X1-85", "OpenGL ES 3.2 V@0800.1", false, false, 0},
	{"mali-g57-r44p1-arm", 0x13b5, 0, true, VK_DRIVER_ID_ARM_PROPRIETARY, "Mali-G57",
		"ARM proprietary", "v1.r44p1-00eac0.0123456789abcdef0123456789abcdef", MV(44, 1, 0),
		"Samsung exynos arm64-v8a", false,
		"ARM", "Mali-G57", "OpenGL ES 3.2 v1.r44p1-00eac0.0123456789abcdef0123456789abcdef", false, false, 0},
	{"mali-g57-malisx2-0.5.1", 0x13b5, 0, true, VK_DRIVER_ID_ARM_PROPRIETARY, "Mali-G57",
		"MaliSX2", "malisx2-0.5.1-308fe121c7", MV(40, 0, 0), "UNISOC ums9620_2h10 ums9620 arm64-v8a", false,
		"ARM", "Mali-G57", "OpenGL ES 3.2 v1.r40p0-01eac0", false, false, 0},
	{"mali-g720-mediatek-mt6989", 0x13b5, 0, true, VK_DRIVER_ID_ARM_PROPRIETARY, "Mali-G720-Immortalis MC12",
		"ARM proprietary", "v1.r48p0-00eac0.0123456789abcdef0123456789abcdef", MV(48, 0, 0),
		"MediaTek mt6989 Dimensity 9300 arm64-v8a", true,
		"ARM", "Mali-G720-Immortalis MC12", "OpenGL ES 3.2 v1.r48p0-00eac0", false, false, 0},
	{"mali-g615-mediatek-mt6897", 0x13b5, 0, true, VK_DRIVER_ID_ARM_PROPRIETARY, "Mali-G615 MC6",
		"ARM proprietary", "v1.r48p0-00eac0.0123456789abcdef0123456789abcdef", MV(48, 0, 0),
		"MediaTek mt6897 Dimensity 8300 arm64-v8a", true,
		"ARM", "Mali-G615 MC6", "OpenGL ES 3.2 v1.r48p0-00eac0", false, false, 0},
	{"mali-g715-tensor-google", 0x13b5, 0, true, VK_DRIVER_ID_ARM_PROPRIETARY, "Mali-G715",
		"ARM proprietary", "v1.r47p0-00eac0.0123456789abcdef0123456789abcdef", MV(47, 0, 0),
		"Google Pixel 9 zuma tensor arm64-v8a", false,
		"ARM", "Mali-G715", "OpenGL ES 3.2 v1.r47p0-00eac0", false, false, 0},
	{"mali-g610-panvk-mesa", 0x13b5, 0, true, VK_DRIVER_ID_MESA_PANVK, "Mali-G610",
		"panvk Mesa driver", "Mesa 25.1.0", MV(25, 1, 0), "Rockchip rk3588 arm64-v8a", false,
		"Mesa", "Mali-G610", "OpenGL ES 3.1 Mesa 25.1.0", false, false, 0},
	{"xclipse940-samsung", 0x144d, 0, true, 21 /* VK_DRIVER_ID_SAMSUNG_PROPRIETARY */, "Samsung Xclipse 940",
		"Samsung proprietary", "", MV(2, 0, 0), "Samsung s5e9945 exynos arm64-v8a", false,
		"Samsung Electronics Co., Ltd.", "Samsung Xclipse 940", "OpenGL ES 3.2 ANGLE", false, false, 0},
	{"powervr-bxm-8-256-imagination", 0x1010, 0, true, VK_DRIVER_ID_IMAGINATION_PROPRIETARY,
		"PowerVR B-Series BXM-8-256", "Imagination Technologies", "1.18@6234021", MV(1, 18, 0),
		"Mediatek mt8188 arm64-v8a", true,
		"Imagination Technologies", "PowerVR B-Series BXM-8-256", "OpenGL ES 3.2 build 24.1@6234021", false, false, 0},
	{"powervr-bxm-8-256-mesa", 0x1010, 0, true, VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA,
		"PowerVR B-Series BXM-8-256", "pvr mesa driver", "Mesa 25.2.0 (PowerVR Mesa)", MV(25, 2, 0), "", false,
		"Mesa", "PowerVR B-Series BXM-8-256", "OpenGL ES 3.2 Mesa 25.2.0", false, false, 0},
	{"powervr-dxt-48-1536-pixel10-angle", 0x1010, 0, true, VK_DRIVER_ID_IMAGINATION_PROPRIETARY,
		"PowerVR D-Series DXT-48-1536", "Imagination Technologies", "1.20@7000000", MV(1, 20, 0),
		"Google Pixel 10 tensor g5 arm64-v8a", false,
		"Google (Imagination Technologies)", "ANGLE (Imagination Technologies, Vulkan 1.3.0 (PowerVR D-Series DXT-48-1536 (0x00000000)), Imagination Technologies)",
		"OpenGL ES 3.2 ANGLE (Imagination Technologies, Vulkan 1.3.0)", false, false, 0},
	{"apple-m2-honeykrisp", 0x106b, 0, true, VK_DRIVER_ID_MESA_HONEYKRISP, "Apple M2",
		"honeykrisp Mesa driver", "Mesa 25.3.0", MV(25, 3, 0), "", false,
		"Apple", "Apple M2", "OpenGL 4.1 Metal", false, false, 0},
	{"broadcom-v3d-v3dv", 0x14e4, 0, true, VK_DRIVER_ID_MESA_V3DV, "V3D 7.1.7",
		"V3DV Mesa", "Mesa 25.0.0", MV(25, 0, 0), "Raspberry Pi 5 bcm2712 arm64-v8a", false,
		"Broadcom", "V3D 7.1.7", "OpenGL ES 3.1 Mesa 25.0.0", false, false, 0},
	{"unknown-vendor", 0x1234, 0, false, 0, "Mystery GPU 9000", "", "", 0, "", false,
		"Acme", "Mystery GPU 9000", "OpenGL ES 3.2", false, false, 0},
	// No VK_KHR_driver_properties (old Android drivers): the vendor ID alone must classify the driver, so
	// these keep the Arm / Apple / V3DV rules exactly as the vendor-keyed code gave them.
	{"mali-g57-no-driver-properties", 0x13b5, 0, false, 0, "Mali-G57", "", "", 0,
		"UNISOC ums9620_2h10 ums9620 arm64-v8a", false,
		"ARM", "Mali-G57", "OpenGL ES 3.2 v1.r40p0-01eac0", false, false, 0},
	{"apple-m2-no-driver-properties", 0x106b, 0, false, 0, "Apple M2", "", "", 0, "", false,
		"Apple", "Apple M2", "OpenGL 4.1 Metal", false, false, 0},
	{"broadcom-v3d-no-driver-properties", 0x14e4, 0, false, 0, "V3D 7.1.7", "", "", 0,
		"Raspberry Pi 5 bcm2712 arm64-v8a", false,
		"Broadcom", "V3D 7.1.7", "OpenGL ES 3.1 Mesa 25.0.0", false, false, 0},
};

struct CapSet
{
	const char* name;
	bool roaa, layout, dynamic_state, dynamic_command, push;
	u32 max_push;
	s8 barriers;
	bool disable_fetch;
};

static const CapSet kCapSets[] = {
	{"all-ext", true, true, true, true, true, 32, -1, false},
	{"no-ext", false, false, false, false, false, 0, -1, false},
	{"layout-no-roaa", false, true, true, true, true, 32, -1, false},
	{"barriers-forced-on", true, true, true, true, true, 32, 1, false},
	{"barriers-forced-off", true, true, true, true, true, 32, 0, false},
	{"fetch-user-disabled", true, true, true, true, true, 32, -1, true},
};

// Mirror of GSDeviceVK::GetRoadIdentity (driver ID switch).
static GSVulkanRoad::Driver MapDriver(const Fixture& f)
{
	using Driver = GSVulkanRoad::Driver;
	if (!f.driver_props)
		return Driver::Unknown;
	switch (f.driver_id)
	{
		case VK_DRIVER_ID_ARM_PROPRIETARY: return Driver::Arm;
		case VK_DRIVER_ID_MESA_PANVK: return Driver::PanVK;
		case VK_DRIVER_ID_QUALCOMM_PROPRIETARY: return Driver::Qualcomm;
		case VK_DRIVER_ID_MESA_TURNIP: return Driver::Turnip;
		case VK_DRIVER_ID_IMAGINATION_PROPRIETARY: return Driver::Imagination;
		case VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA: return Driver::MesaPowerVR;
		case VK_DRIVER_ID_MESA_HONEYKRISP: return Driver::Honeykrisp;
		default: return Driver::Unknown;
	}
}

static GSVulkanRoad::Identity MakeIdentity(const Fixture& f)
{
	return {.vendor = f.vendor, .driver = MapDriver(f), .name = f.name,
		.driver_name = f.driver_props ? std::string_view(f.driver_name) : std::string_view{},
		.info = f.driver_props ? std::string_view(f.info) : std::string_view{},
		.platform = f.platform, .mediatek = f.mediatek, .device_id = f.device_id,
		.raw_driver_id = f.driver_props ? static_cast<u32>(f.driver_id) : 0u};
}

// Mirror of GetVulkanVendorHint in GSDeviceVK.cpp.
static const char* VendorHint(u32 vendor_id)
{
	switch (vendor_id)
	{
		case 0x13B5u: return "ARM Mali";
		case 0x5143u: return "Qualcomm Adreno";
		case 0x1010u: return "Imagination PowerVR";
		case 0x144Du: return "Samsung Xclipse";
		case 0x18D1u: return "Google Tensor";
		default: return "";
	}
}

// Mirror of IsMaliSX2DriverInfo in GSDeviceVK.cpp (case-insensitive "malisx2").
static bool IsMaliSX2DriverInfo(std::string_view info)
{
	return GSVulkanRoad::Lower(info).find("malisx2") != std::string::npos;
}

// Mirrors of IsQualcommProprietaryGLDriver / RequiresSingleRGBA8EXTFetch in GSDeviceOGL.cpp.
static bool GLQualcommProprietary(const std::string& vendor, const std::string& renderer, const std::string& version)
{
	const std::string hints = GSVulkanRoad::Lower(vendor + " " + renderer + " " + version);
	if (hints.find("turnip") != std::string::npos || hints.find("freedreno") != std::string::npos ||
		hints.find("angle") != std::string::npos)
		return false;
	return GpuProfileDetector::Detect(vendor, renderer) == RuntimeGpuProfile::Adreno;
}
// Mirror of IsQualcommFamilyGLDriver (Qualcomm proprietary, freedreno, ANGLE on Qualcomm).
static bool GLQualcommFamily(const std::string& vendor, const std::string& renderer, const std::string& version)
{
	if (GLQualcommProprietary(vendor, renderer, version))
		return true;
	const std::string hints = GSVulkanRoad::Lower(vendor + " " + renderer + " " + version);
	const bool freedreno_or_angle = hints.find("turnip") != std::string::npos ||
		hints.find("freedreno") != std::string::npos || hints.find("angle") != std::string::npos;
	return freedreno_or_angle && GpuProfileDetector::Detect(vendor, renderer) == RuntimeGpuProfile::Adreno;
}
static bool GLSingleRGBA8EXTFetch(const std::string& vendor, const std::string& renderer, const std::string& version)
{
	return GLQualcommProprietary(vendor, renderer, version) && std::string_view(version).starts_with("OpenGL ES 3.2 V@0842.52 ");
}

static const char* RoadName(GSSelfReadRoad r)
{
	switch (r) { case GSSelfReadRoad::Copy: return "Copy"; case GSSelfReadRoad::InPassBarrier: return "InPassBarrier"; case GSSelfReadRoad::InPassOrdered: return "InPassOrdered"; }
	return "?";
}
static const char* SpellName(GSSelfReadSpelling s)
{
	switch (s) { case GSSelfReadSpelling::Clone: return "Clone"; case GSSelfReadSpelling::InputAttachment: return "InputAttachment"; case GSSelfReadSpelling::FeedbackLoopLayout: return "FeedbackLoopLayout"; }
	return "?";
}
static const char* CarryName(GSFeedbackCarry c)
{
	switch (c) { case GSFeedbackCarry::None: return "None"; case GSFeedbackCarry::NonReaders: return "NonReaders"; case GSFeedbackCarry::All: return "All"; }
	return "?";
}
static const char* DriverName(GSVulkanRoad::Driver d)
{
	using D = GSVulkanRoad::Driver;
	switch (d)
	{
		case D::Unknown: return "Unknown"; case D::Arm: return "Arm"; case D::PanVK: return "PanVK";
		case D::Qualcomm: return "Qualcomm"; case D::Turnip: return "Turnip"; case D::Imagination: return "Imagination";
		case D::MesaPowerVR: return "MesaPowerVR"; case D::Honeykrisp: return "Honeykrisp"; case D::Angle: return "Angle";
	}
	return "?";
}
static const char* BackendName(GSFramebufferFetchBackend b)
{
	switch (b) { case GSFramebufferFetchBackend::None: return "None"; case GSFramebufferFetchBackend::ARM: return "ARM"; case GSFramebufferFetchBackend::EXT: return "EXT"; }
	return "?";
}
static const char* VetoName(GSFramebufferFetchVeto v)
{
	switch (v) { case GSFramebufferFetchVeto::None: return "None"; case GSFramebufferFetchVeto::NoExtension: return "NoExtension"; case GSFramebufferFetchVeto::DriverBlocklist: return "DriverBlocklist"; case GSFramebufferFetchVeto::UserSetting: return "UserSetting"; }
	return "?";
}

static std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static std::string Fmt(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}

static std::string Flags(std::initializer_list<std::pair<const char*, bool>> flags)
{
	std::string out = "[";
	bool first = true;
	for (const auto& f : flags)
	{
		if (!f.second)
			continue;
		if (!first)
			out += ',';
		out += f.first;
		first = false;
	}
	return out + "]";
}

static std::string OneLine(const char* text)
{
	std::string out;
	for (const char* p = text; *p; ++p)
		out += (*p == '\n') ? '|' : *p;
	return out;
}

static std::string FactsText(const GSVulkanRoad::Facts& f)
{
	return Fmt("facts driver=%s model=%u fix_generation=%u flags=", DriverName(f.driver), f.model, f.fix_generation) +
		Flags({{"mali", f.mali}, {"adreno", f.adreno}, {"powervr", f.powervr}, {"broadcom", f.broadcom},
			{"xclipse", f.xclipse}, {"malisx2", f.malisx2}, {"avoid_layout", f.avoid_layout},
			{"broken_roaa", f.broken_roaa}, {"copy_self_read", f.copy_self_read}, {"ordered_input", f.ordered_input},
			{"measured_layout", f.measured_layout}, {"measured_barrier", f.measured_barrier},
			{"driver_orders_loop", f.driver_orders_loop}, {"order_on_request", f.order_on_request},
			{"prefer_declared_barrier", f.prefer_declared_barrier}, {"avoid_push", f.avoid_push},
			{"adreno8xx_proprietary", f.adreno8xx_proprietary}});
}

static std::string SelectionText(const GSVulkanRoad::Selection& s)
{
	const auto& r = s.road;
	return Fmt("road=%s spelling=%s carry=%s ", RoadName(r.road), SpellName(r.spelling), CarryName(s.carry)) +
		"flags=" +
		Flags({{"texture_barrier", r.texture_barrier}, {"in_tile_read", r.in_tile_read},
			{"force_feedback_loop_layout", r.force_feedback_loop_layout},
			{"orders_overlapping_prims", r.orders_overlapping_prims}, {"loop_declared", r.loop_declared},
			{"selected_by_driver_fact", r.selected_by_driver_fact}, {"arm_applied", r.arm_applied},
			{"arm_unavailable", r.arm_unavailable}, {"dynamic_loop", s.dynamic_loop},
			{"raster_order_on_overlap", s.raster_order_on_overlap}, {"push_descriptors", s.push_descriptors},
			{"avoid_layout", s.avoid_layout}});
}

static void DumpFixture(std::string& out, const Fixture& fx)
{
	const auto identity = MakeIdentity(fx);
	out += Fmt("[%s]\n", fx.id);
	out += Fmt("vk.identity vendor=0x%04x device_id=0x%08x driver_props=%d driver_id=%d name='%s' driver_name='%s' info='%s' version=0x%08x platform='%s' mediatek=%d\n",
		fx.vendor, fx.device_id, fx.driver_props, fx.driver_id, fx.name, fx.driver_name, OneLine(fx.info).c_str(), fx.version, fx.platform, fx.mediatek);
	out += Fmt("vk.runtime_profile=%s mediatek_hint(platform)=%d adreno_generation=%u\n",
		GpuProfileDetector::RuntimeProfileToString(GpuProfileDetector::Detect(VendorHint(fx.vendor), fx.name)),
		GpuProfileDetector::LooksLikeMediaTekSoC(fx.platform), GpuProfileDetector::ParseAdrenoGeneration(fx.name));

	// FinalizeDriverCapabilities: facts resolved with (roaa=false, push=0), platform rules from raw properties.
	const auto raw_facts = GSVulkanRoad::ResolveFacts(identity, false, 0);
	const auto rules = GSVulkanDevicePolicy::Resolve({fx.vendor, fx.device_id, fx.version,
		static_cast<u32>(fx.driver_id), fx.name, raw_facts.driver == GSVulkanRoad::Driver::Arm});
	out += Fmt("vk.platform_rules ") +
		Flags({{"broken_timestamps", rules.broken_timestamps},
			{"broken_colormask_with_depth", rules.broken_colormask_with_depth},
			{"broken_mad_deinterlace", rules.broken_mad_deinterlace},
			{"prefer_coherent_readback", rules.prefer_coherent_readback},
			{"no_ps2_z_quantization", rules.no_ps2_z_quantization}}) + "\n";
	// Inline rules in GSDeviceVK.cpp: D32 DATE fallback,
	// PreferCachedStreamRings, WarmupWorkerLimit, IsWarmupBlocklisted (static part, Imagination proprietary only),
	// broken_blend_constant (CheckFeatures), swapchain MaliSX2 rule (VKSwapChain.cpp).
	const bool provoking_off = rules.broken_provoking_vertex_last; // GSVulkanDevicePolicy rule, applied in FinalizeDriverCapabilities
	const bool d32_date = raw_facts.adreno && raw_facts.driver == GSVulkanRoad::Driver::Turnip &&
		(fx.version == 0 || fx.version < VK_MAKE_VERSION(26, 2, 0));
	const bool cached_rings = raw_facts.adreno && raw_facts.model == 610 && raw_facts.driver == GSVulkanRoad::Driver::Turnip;
	out += Fmt("vk.inline provoking_vertex_disabled=%d d32_date_fallback=%d prefer_cached_stream_rings=%d warmup_workers=%u warmup_blocklisted_static=%d broken_blend_constant=%d swapchain_present_modes=%d\n",
		provoking_off, d32_date, cached_rings, fx.vendor == 0x5143 ? 2u : 1u, raw_facts.driver == GSVulkanRoad::Driver::Imagination,
		GSBlendConstantPolicy::DeviceDefect(true, raw_facts.adreno, raw_facts.driver == GSVulkanRoad::Driver::Turnip),
		Vulkan::ShouldUseSwapchainPresentModes(true, IsMaliSX2DriverInfo(fx.driver_props ? fx.info : "")));

	// GL identity.
	const std::string vendor = fx.gl_vendor, renderer = fx.gl_renderer, version = fx.gl_version;
	const RuntimeGpuProfile profile = GpuProfileDetector::Detect(vendor, renderer);
	out += Fmt("gl.identity vendor='%s' renderer='%s' version='%s'\n", fx.gl_vendor, fx.gl_renderer, fx.gl_version);
	out += Fmt("gl.runtime_profile=%s adreno_generation=%u qualcomm_proprietary=%d single_rgba8_ext_fetch=%d per_primitive_fb_copy(gles,no_barrier)=%d\n",
		GpuProfileDetector::RuntimeProfileToString(profile), GpuProfileDetector::ParseAdrenoGeneration(renderer),
		GLQualcommProprietary(vendor, renderer, version), GLSingleRGBA8EXTFetch(vendor, renderer, version),
		GLUsesPerPrimitiveFbCopy(false, true));
	for (const auto ext : {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}, std::pair{false, false}})
	{
		const auto d = DecideGLFramebufferFetch(ext.first, ext.second, false, false, false);
		out += Fmt("gl.fetch arm=%d ext=%d enabled=%d backend=%s veto=%s overlap_ordered=%d\n",
			ext.first, ext.second, d.enabled, BackendName(d.backend), VetoName(d.veto),
			FbFetchOrdersOverlappingPrims(d.backend));
	}

	// Road decisions per capability set.
	auto emit = [&](const char* name, const CapSet& c) {
		const auto facts = GSVulkanRoad::ResolveFacts(identity, c.roaa, c.max_push);
		GSVulkanRoad::Capabilities caps;
		caps.roaa = c.roaa; caps.layout = c.layout; caps.dynamic_state = c.dynamic_state;
		caps.dynamic_command = c.dynamic_command; caps.push_descriptors = c.push; caps.max_push_descriptors = c.max_push;
		caps.texture_barriers = c.barriers; caps.disable_fetch = c.disable_fetch; caps.android = true;
		caps.mobile_profile = GpuProfileDetector::Detect(VendorHint(fx.vendor), fx.name) == RuntimeGpuProfile::Mobile;
		const auto sel = GSVulkanRoad::ResolveSelection(facts, caps);
		out += Fmt("caps[%s roaa=%d layout=%d dyn=%d cmd=%d push=%d/%u barriers=%d disable_fetch=%d]\n", name, c.roaa,
			c.layout, c.dynamic_state, c.dynamic_command, c.push, c.max_push, static_cast<int>(c.barriers), c.disable_fetch);
		out += "  " + FactsText(facts) + "\n";
		out += "  sel " + SelectionText(sel) + "\n";
	};
	for (const CapSet& c : kCapSets)
		emit(c.name, c);
	if (fx.log_caps)
	{
		const CapSet logged{"device-log", fx.log_roaa, false, false, false, fx.log_push != 0, fx.log_push, -1, false};
		emit(logged.name, logged);
	}
}

// Mirror of IsArmOrImaginationProprietaryGLDriver in GSDeviceOGL.cpp.
static bool GLArmOrImaginationProprietary(const std::string& vendor, const std::string& renderer, const std::string& version)
{
	const std::string hints = GSVulkanRoad::Lower(vendor + " " + renderer + " " + version);
	if (hints.find("mesa") != std::string::npos || hints.find("panfrost") != std::string::npos)
		return false;
	return GpuProfileDetector::Detect(vendor, renderer) == RuntimeGpuProfile::Mobile;
}

// Facts the profile-to-fact refactor (steps 2 and 3) derives. Kept in their own trailing section so the
// per-fixture sections above stay comparable with older goldens.
static void DumpDerived(std::string& out, const Fixture& fx)
{
	const auto identity = MakeIdentity(fx);
	const auto raw_facts = GSVulkanRoad::ResolveFacts(identity, false, 0);
	const auto rules = GSVulkanDevicePolicy::Resolve({fx.vendor, fx.device_id, fx.version,
		static_cast<u32>(fx.driver_id), fx.name, raw_facts.driver == GSVulkanRoad::Driver::Arm});
	out += Fmt("%s vk.shader_workarounds scalarize_vector_bitwise_and=%d store_bitwise_negation_in_temporary=%d gl.shader_workarounds=%d adreno_generation_unified=%u\n",
		fx.id, rules.scalarize_vector_bitwise_and, rules.store_bitwise_negation_in_temporary,
		GLArmOrImaginationProprietary(fx.gl_vendor, fx.gl_renderer, fx.gl_version), GSVulkanRoad::AdrenoGeneration(identity));
	// Driver facts that replaced vendor terms (steps 5-6): V3DV carry, Arm/PanVK measured carry and depth
	// nudge, Adreno-hardware depth-read rule (GSDeviceVK::ResolveDepthSamplingFeatures), Qualcomm-family GL
	// depth-feedback Auto choice. no_ps2_z_quantization is the platform rule above, restated per driver.
	out += Fmt("%s vk.driver_facts v3dv=%d arm_driver=%d no_ps2_z_quantization=%d depth_read_unvalidated=%d gl.qualcomm_family=%d\n",
		fx.id, raw_facts.v3dv, raw_facts.arm_driver, rules.no_ps2_z_quantization,
		raw_facts.adreno,
		GLQualcommFamily(fx.gl_vendor, fx.gl_renderer, fx.gl_version));
	// GSDeviceVK::UseGeneralAttachmentLayout = framebuffer_fetch || texture_barrier || feedback-loop layout
	// (the last implies texture_barrier), per capability set.
	for (const CapSet& c : kCapSets)
	{
		const auto facts = GSVulkanRoad::ResolveFacts(identity, c.roaa, c.max_push);
		GSVulkanRoad::Capabilities caps;
		caps.roaa = c.roaa; caps.layout = c.layout; caps.dynamic_state = c.dynamic_state;
		caps.dynamic_command = c.dynamic_command; caps.push_descriptors = c.push; caps.max_push_descriptors = c.max_push;
		caps.texture_barriers = c.barriers; caps.disable_fetch = c.disable_fetch; caps.android = true;
		const auto sel = GSVulkanRoad::ResolveSelection(facts, caps);
		out += Fmt("%s vk.general_attachment_layout[%s]=%d\n", fx.id, c.name, sel.road.in_tile_read || sel.road.texture_barrier);
	}
	// The startup fetch overlap self-test (GSDeviceVK::RunFetchOverlapProbe) on the all-ext capability set:
	// Passed lifts the name-keyed denials, Failed turns fetch off. NotRun is the all-ext line above.
	for (const auto probe : {GSFetchOverlapProbe::Passed, GSFetchOverlapProbe::Failed})
	{
		const CapSet& c = kCapSets[0];
		const auto facts = GSVulkanRoad::ResolveFacts(identity, c.roaa, c.max_push);
		GSVulkanRoad::Capabilities caps;
		caps.roaa = c.roaa; caps.layout = c.layout; caps.dynamic_state = c.dynamic_state;
		caps.dynamic_command = c.dynamic_command; caps.push_descriptors = c.push; caps.max_push_descriptors = c.max_push;
		caps.texture_barriers = c.barriers; caps.disable_fetch = c.disable_fetch; caps.android = true;
		caps.overlap_probe = probe;
		const auto sel = GSVulkanRoad::ResolveSelection(facts, caps);
		out += Fmt("%s vk.fetch_probe[%s all-ext] road=%s in_tile_read=%d texture_barrier=%d carry=%s\n", fx.id,
			probe == GSFetchOverlapProbe::Passed ? "passed" : "failed", RoadName(sel.road.road), sel.road.in_tile_read,
			sel.road.texture_barrier, CarryName(sel.carry));
	}
	if (fx.log_caps)
	{
		const CapSet logged{"device-log", fx.log_roaa, false, false, false, fx.log_push != 0, fx.log_push, -1, false};
		const auto facts = GSVulkanRoad::ResolveFacts(identity, logged.roaa, logged.max_push);
		GSVulkanRoad::Capabilities caps;
		caps.roaa = logged.roaa; caps.push_descriptors = logged.push; caps.max_push_descriptors = logged.max_push;
		caps.android = true;
		const auto sel = GSVulkanRoad::ResolveSelection(facts, caps);
		out += Fmt("%s vk.general_attachment_layout[device-log]=%d\n", fx.id, sel.road.in_tile_read || sel.road.texture_barrier);
	}
}

static std::string BuildSnapshot()
{
	std::string out;
	out += "# GS device policy snapshot v1. Regenerate: see GSDevicePolicySnapshotTests.inl\n";
	out += "[global]\n";
	for (const bool mali_sx2 : {false, true})
		out += Fmt("vk.swapchain_present_modes(maintenance=1 malisx2=%d)=%d\n", mali_sx2, Vulkan::ShouldUseSwapchainPresentModes(true, mali_sx2));
	out += Fmt("vk.swapchain_present_modes(maintenance=0 malisx2=0)=%d\n", Vulkan::ShouldUseSwapchainPresentModes(false, false));
	for (const bool barrier : {false, true})
		for (const bool tiler : {false, true})
			out += Fmt("gl.per_primitive_fb_copy(barrier=%d tiler=%d)=%d\n", barrier, tiler, GLUsesPerPrimitiveFbCopy(barrier, tiler));
	for (const Fixture& fx : kFixtures)
		DumpFixture(out, fx);
	out += "[derived]\n";
	for (const Fixture& fx : kFixtures)
		DumpDerived(out, fx);
	return out;
}

static std::vector<std::string> SplitLines(const std::string& text)
{
	std::vector<std::string> lines;
	size_t start = 0;
	while (start < text.size())
	{
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		lines.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return lines;
}

// Line diff via LCS; prints "-golden" / "+actual" with the fixture header for context.
static size_t PrintDiff(const std::string& golden, const std::string& actual)
{
	const auto a = SplitLines(golden), b = SplitLines(actual);
	std::vector<std::vector<u32>> lcs(a.size() + 1, std::vector<u32>(b.size() + 1, 0));
	for (size_t i = a.size(); i-- > 0;)
		for (size_t j = b.size(); j-- > 0;)
			lcs[i][j] = (a[i] == b[j]) ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
	size_t i = 0, j = 0, changes = 0;
	std::string section = "(start)", caps = "";
	while (i < a.size() || j < b.size())
	{
		if (i < a.size() && j < b.size() && a[i] == b[j])
		{
			if (!a[i].empty() && a[i][0] == '[')
				section = a[i];
			else if (a[i].rfind("caps[", 0) == 0)
				caps = a[i];
			++i; ++j;
			continue;
		}
		const bool del = j >= b.size() || (i < a.size() && lcs[i + 1][j] >= lcs[i][j + 1]);
		if (changes < 200)
			std::printf("POLICY_SNAPSHOT_DIFF %s %s\n  %c %s\n", section.c_str(), caps.c_str(), del ? '-' : '+', (del ? a[i] : b[j]).c_str());
		++changes;
		del ? ++i : ++j;
	}
	return changes;
}

static const char kGolden[] =
#include "GSDevicePolicySnapshot.golden.inc"
;
} // namespace PolicySnapshot

extern "C" void EmuCoreXOracleDevicePolicySnapshot(void (*check)(bool, const char*))
{
	const std::string actual = PolicySnapshot::BuildSnapshot();
	if (const char* dump = std::getenv("EMUCOREX_POLICY_SNAPSHOT_DUMP"); dump && *dump)
		std::printf("POLICY_SNAPSHOT_BEGIN\n%sPOLICY_SNAPSHOT_END\n", actual.c_str());
	// The golden file is a raw string starting with a newline after R"GOLDEN(.
	std::string golden = PolicySnapshot::kGolden;
	if (!golden.empty() && golden.front() == '\n')
		golden.erase(0, 1);
	golden.erase(std::remove(golden.begin(), golden.end(), '\r'), golden.end()); // tolerate CRLF checkout
	const bool equal = golden == actual;
	if (!equal)
	{
		const size_t changes = PolicySnapshot::PrintDiff(golden, actual);
		std::printf("POLICY_SNAPSHOT_DIFF_COUNT %zu (golden %zu bytes, actual %zu bytes)\n", changes, golden.size(), actual.size());
	}
	check(equal, "GS device policy snapshot matches GSDevicePolicySnapshot.golden.inc for every GPU identity fixture");
}
