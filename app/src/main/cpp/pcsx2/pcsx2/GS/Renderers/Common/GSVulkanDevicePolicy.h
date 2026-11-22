// SPDX-FileCopyrightText: 2026 ARMSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "common/Pcsx2Defs.h"
#include <string_view>
// Correctness facts do not change the public unified runtime GPU identity.
// Adapted 1:1 from fc4ab0b8 GSGPUDriverProfile::ResolveVulkanDeviceRules and
// vk-arm-slow-cached-readback-before/after-r44p1, preserving exact raw predicates.
namespace GSVulkanDevicePolicy {
struct Facts {
    u32 vendor=0, device=0, version=0, driver=0;
    std::string_view name;
    bool arm_proprietary=false;
};
// VkDriverId values, spelled as literals so this pure CPU policy needs no Vulkan header.
// GSDeviceVK.cpp static_asserts them against the real enum.
inline constexpr u32 kDriverImaginationProprietary=7;
inline constexpr u32 kDriverQualcommProprietary=8;
inline constexpr u32 kDriverMesaTurnip=18;
struct Rules {
    bool broken_timestamps=false;
    bool broken_colormask_with_depth=false;
    bool broken_mad_deinterlace=false;
    bool prefer_coherent_readback=false;
    bool no_ps2_z_quantization=false;
    // NAMED DRIVER-BUG RULE: VK_EXT_provoking_vertex (provokingVertexLast) is distrusted on the
    // Qualcomm proprietary Vulkan driver, every version. The vendor ID alone is not enough: Turnip
    // on the same hardware keeps the extension. There is no recorded evidence of a fixed driver
    // build (the oracle suites only use driverVersion 512.900), so the rule has no version bound;
    // add one here, with the build that was verified, once a fixed driver is measured.
    bool broken_provoking_vertex_last=false;
    // Compiler workarounds for shader text: ARM's proprietary compiler miscompiles vector bitwise
    // AND (gpu_bitwise_and is scalarized), and Imagination's proprietary compiler needs a negated
    // bitwise result stored in a temporary. Both are cheap anywhere, so they apply to those two
    // proprietary drivers only. Qualcomm, Mesa (Turnip, PanVK, PVR, V3DV, Honeykrisp) and unknown
    // drivers get the plain expressions.
    bool scalarize_vector_bitwise_and=false;
    bool store_bitwise_negation_in_temporary=false;
};
constexpr Rules Resolve(const Facts& f) {
    const bool mali=f.vendor==0x13b5;
    const bool adreno=f.vendor==0x5143;
    // VkDriverId MESA_TURNIP=18; use literal in pure CPU policy to avoid Vulkan headers.
    const bool turnip=f.driver==kDriverMesaTurnip;
    const bool powervr=f.vendor==0x1010;
    const bool shader_workaround=(mali&&f.arm_proprietary)||(powervr&&f.driver==kDriverImaginationProprietary);
    const u32 major=f.version>>22,minor=(f.version>>12)&0x3ff,patch=f.version&0xfff;
    const bool known=f.version!=0&&patch==0&&major<=100;
    const bool measured_revision=known&&major==44&&minor==1;
    return {
        mali&&f.name.find("Mali-G615")!=std::string_view::npos,
        adreno&&!turnip&&(f.device<0x06000000u||f.version<0x801EA000u),
        mali&&f.name.find("Mali-G57")!=std::string_view::npos,
        mali&&f.arm_proprietary&&!measured_revision,
        mali||f.vendor==0x106b,
        adreno&&f.driver==kDriverQualcommProprietary,
        shader_workaround,
        shader_workaround,
    };
}
}
