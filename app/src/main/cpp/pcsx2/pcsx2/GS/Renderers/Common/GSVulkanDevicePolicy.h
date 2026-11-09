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
struct Rules {
    bool broken_timestamps=false;
    bool broken_colormask_with_depth=false;
    bool broken_mad_deinterlace=false;
    bool prefer_coherent_readback=false;
    bool no_ps2_z_quantization=false;
};
constexpr Rules Resolve(const Facts& f) {
    const bool mali=f.vendor==0x13b5;
    const bool adreno=f.vendor==0x5143;
    // VkDriverId MESA_TURNIP=18; use literal in pure CPU policy to avoid Vulkan headers.
    const bool turnip=f.driver==18;
    const u32 major=f.version>>22,minor=(f.version>>12)&0x3ff,patch=f.version&0xfff;
    const bool known=f.version!=0&&patch==0&&major<=100;
    const bool measured_revision=known&&major==44&&minor==1;
    return {
        mali&&f.name.find("Mali-G615")!=std::string_view::npos,
        adreno&&!turnip&&(f.device<0x06000000u||f.version<0x801EA000u),
        mali&&f.name.find("Mali-G57")!=std::string_view::npos,
        mali&&f.arm_proprietary&&!measured_revision,
        mali||f.vendor==0x106b,
    };
}
}
