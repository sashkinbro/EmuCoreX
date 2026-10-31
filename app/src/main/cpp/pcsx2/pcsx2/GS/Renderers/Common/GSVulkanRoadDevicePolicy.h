// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "GS/Renderers/Common/GSDrawRoad.h"
#include "GS/Renderers/Common/GSFramebufferFetchPolicy.h"
#include "GS/Renderers/Common/GSSelfReadRoadPolicy.h"
#include "GS/Renderers/Common/GSDynamicFeedbackLoopPolicy.h"
#include <cctype>
#include <string>
#include <string_view>

// Focused adapter for the self-read rules in GSGPUDriverProfile.cpp. It keeps
// this core's existing coarse RuntimeGpuProfile API and does not tune pools.
namespace GSVulkanRoad
{
enum class Driver : u8 { Unknown, Arm, PanVK, Qualcomm, Turnip, Imagination, MesaPowerVR, Honeykrisp, Angle };
struct Identity
{
    u32 vendor = 0;
    Driver driver = Driver::Unknown;
    std::string_view name, driver_name, info, platform;
    bool mediatek = false;
};
struct Capabilities
{
    bool roaa = false, layout = false, dynamic_state = false, dynamic_command = false;
    bool push_descriptors = false;
    u32 max_push_descriptors = 0;
    s8 texture_barriers = -1;
    bool disable_fetch = false;
    bool android = true;
    // The legacy broad profile is read only by the behavior-neutral RED seam.
    bool mobile_profile = false;
};
struct Facts
{
    bool mali = false, adreno = false, powervr = false, broadcom = false, xclipse = false;
    bool malisx2 = false, avoid_layout = false, broken_roaa = false, copy_self_read = false;
    bool ordered_input = false, measured_layout = false, measured_barrier = false;
    bool driver_orders_loop = false, order_on_request = false, prefer_declared_barrier = false;
    bool avoid_push = false, adreno8xx_proprietary = false;
    Driver driver = Driver::Unknown;
    u32 model = 0, fix_generation = 0;
};
struct Selection
{
    GSSelfReadRoadDecision road;
    GSFeedbackCarry carry = GSFeedbackCarry::None;
    bool dynamic_loop = false, raster_order_on_overlap = false, push_descriptors = false;
    bool avoid_layout = false;
};
inline std::string Lower(std::string_view value)
{
    std::string out(value);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
inline u32 Number(std::string_view text, size_t& at)
{
    u32 value = 0;
    for (u32 n = 0; at < text.size() && n < 4 && std::isdigit(static_cast<unsigned char>(text[at])); ++n)
        value = value * 10 + static_cast<u32>(text[at++] - '0');
    return value;
}
inline u32 Model(std::string_view name, std::string_view prefix)
{
    const auto at = name.find(prefix);
    if (at == std::string_view::npos) return 0;
    size_t p = at + prefix.size();
    while (p < name.size() && !std::isdigit(static_cast<unsigned char>(name[p]))) ++p;
    return Number(name, p);
}
// Exact strict axfl tag grammar from ParseFixGeneration: token boundary,
// nonzero decimal generation (at most four digits), trailing hyphen.
inline u32 FixGeneration(std::string_view info)
{
    const auto lowered = Lower(info);
    constexpr std::string_view prefix = "git-axfl";
    for (size_t at = lowered.find(prefix); at != std::string::npos; at = lowered.find(prefix, at + 1))
    {
        if (at && std::isalnum(static_cast<unsigned char>(lowered[at - 1]))) continue;
        size_t p = at + prefix.size(), start = p;
        const u32 generation = Number(lowered, p);
        if (p != start && generation && p < lowered.size() && lowered[p] == '-') return generation;
    }
    return 0;
}
inline bool Revision44p1(std::string_view info)
{
    for (size_t at = info.find('r'); at != std::string::npos; at = info.find('r', at + 1))
    {
        size_t p = at + 1;
        const auto major = Number(info, p);
        if (p >= info.size() || info[p++] != 'p') continue;
        if (major == 44 && Number(info, p) == 1) return true;
    }
    return false;
}
inline Facts ResolveFacts(const Identity& id, bool roaa, u32 max_push)
{
    Facts f;
    f.mali = id.vendor == 0x13b5; f.adreno = id.vendor == 0x5143;
    f.powervr = id.vendor == 0x1010; f.broadcom = id.vendor == 0x14e4; f.xclipse = id.vendor == 0x144d;
    const auto name = Lower(id.name), info = Lower(id.info), platform = Lower(id.platform);
    const auto hints = Lower(std::string(id.driver_name) + " " + std::string(id.info));
    f.driver = id.driver;
    if (f.driver == Driver::Unknown)
    {
        if (hints.find("turnip") != std::string::npos || hints.find("freedreno") != std::string::npos) f.driver = Driver::Turnip;
        else if (hints.find("panvk") != std::string::npos || hints.find("panfrost") != std::string::npos) f.driver = Driver::PanVK;
        else if (hints.find("pvr mesa") != std::string::npos || hints.find("powervr mesa") != std::string::npos) f.driver = Driver::MesaPowerVR;
        else if (hints.find("angle") != std::string::npos) f.driver = Driver::Angle;
        else if (f.mali) f.driver = Driver::Arm;
        else if (f.adreno) f.driver = Driver::Qualcomm;
        else if (f.powervr) f.driver = Driver::Imagination;
    }
    f.model = f.adreno ? Model(name, "adreno") : Model(name, "mali-g");
    if (f.mali && !f.model) f.model = Model(name, "immortalis-g");
    f.malisx2 = info.find("malisx2") != std::string::npos;
    const bool mt6897 = platform.find("mt6897") != std::string::npos;
    const bool turnip = f.driver == Driver::Turnip, honeykrisp = id.driver == Driver::Honeykrisp;
    // ResolveVulkanDeviceRules uses raw driver IDs for these device guarantees.
    // Fallback classification below is only for profile/workaround/tag rules.
    const bool img = f.powervr && id.driver == Driver::Imagination;
    f.avoid_layout = (f.mali && id.info.find("r44p1") != std::string_view::npos) || img;
    f.ordered_input = img && f.avoid_layout;
    f.broken_roaa = f.mali && ((id.mediatek && !mt6897) || f.model == 57) && !(f.malisx2 && roaa);
    f.copy_self_read = (f.adreno && (turnip || f.driver == Driver::Qualcomm)) ||
        (f.mali && f.driver == Driver::Arm && Revision44p1(info) && !mt6897 && !f.malisx2);
    f.fix_generation = FixGeneration(id.info);
    const bool a6xx = f.model >= 650 && f.model < 700 && f.fix_generation >= 1;
    const bool a7xx = f.model >= 730 && f.model < 800 && f.fix_generation >= 2;
    f.driver_orders_loop = f.adreno && turnip && (a6xx || a7xx);
    f.order_on_request = f.adreno && turnip && a7xx;
    f.prefer_declared_barrier = f.adreno && turnip && f.model >= 700 && f.model < 800 && (f.model >= 730 || f.model == 722);
    f.adreno8xx_proprietary = f.adreno && id.driver == Driver::Qualcomm && f.model >= 800 && f.model < 900;
    f.measured_layout = id.driver == Driver::Turnip || honeykrisp;
    f.measured_barrier = honeykrisp;
    f.avoid_push = (f.mali && !(f.malisx2 && max_push >= 7)) ||
        (f.adreno && id.driver != Driver::Qualcomm && id.driver != Driver::Turnip);
    return f;
}
// The same decision is used before logical-device creation and after PFN load,
// before any descriptor layout, texture, pass or graphics pipeline is built.
inline Selection ResolveSelection(const Facts& f, const Capabilities& c)
{
    Selection out;
    out.avoid_layout = f.avoid_layout;
    const bool layout = c.layout && !out.avoid_layout;
    const auto fetch = DecideVulkanFramebufferFetch({
        .roaa_available = c.roaa, .user_disabled = c.disable_fetch,
        .is_mali = f.mali, .is_adreno = f.adreno, .is_xclipse = f.xclipse,
        .is_adreno8xx_proprietary = f.adreno8xx_proprietary,
        .broken_destination_read = f.broken_roaa,
        .any_vendor_trusted = c.android,
        .ordered_without_roaa = f.ordered_input && !layout});
    out.road = DecideSelfReadRoad({.in_tile_read_available = fetch.enabled,
        .layout_road_available = layout, .roaa_available = c.roaa,
        .rt_self_read_is_broken = f.copy_self_read,
        .driver_orders_declared_loop = f.driver_orders_loop && (!f.order_on_request || c.roaa),
        .driver_prefers_declared_loop_with_barriers = f.prefer_declared_barrier,
        .override_texture_barriers = c.texture_barriers});
    const bool sampler = out.road.spelling == GSSelfReadSpelling::FeedbackLoopLayout;
    out.carry = GSFeedbackCarryForDevice({.device_always_carries = f.broadcom,
        .device_is_measured_vendor = f.mali || f.ordered_input,
        .device_is_layout_road_vendor = f.adreno,
        .barriers_order_reads = out.road.texture_barrier,
        .device_is_barrier_road_vendor = f.measured_barrier,
        .framebuffer_fetch = out.road.in_tile_read, .feedback_loop_layout = sampler});
    out.dynamic_loop = GSDeclaresLoopPerDraw({.layout_road_live = sampler,
        .dynamic_state_available = c.dynamic_state && c.dynamic_command, .device_measured = f.measured_layout});
    out.raster_order_on_overlap = out.road.orders_overlapping_prims && !out.road.in_tile_read && f.order_on_request;
    out.push_descriptors = c.push_descriptors && c.max_push_descriptors >= 7 && !f.avoid_push;
    return out;
}
}
