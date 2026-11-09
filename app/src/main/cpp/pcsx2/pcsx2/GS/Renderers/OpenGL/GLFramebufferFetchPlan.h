// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "GS/Renderers/Common/GSFramebufferFetchPolicy.h"

enum class GLDepthFetch : unsigned char { Sampler, EXT, ARM };
struct GLFramebufferFetchCaps
{
    GSFramebufferFetchBackend preferred = GSFramebufferFetchBackend::None;
    bool ext = false, arm_depth = false, arm_mrt = false, adreno = false;
    // Observed physical restriction, independent of coarse/forced GPU profile.
    bool ext_single_rgba8 = false;
};
struct GLFramebufferFetchPlan
{
    unsigned char key = 0;
    constexpr GSFramebufferFetchBackend Color() const { return static_cast<GSFramebufferFetchBackend>(key & 3); }
    constexpr GLDepthFetch Depth() const { return static_cast<GLDepthFetch>((key >> 2) & 3); }
    constexpr bool operator==(const GLFramebufferFetchPlan&) const = default;
};

// Draw facts, not extension advertisement, decide whether a builtin is legal.
constexpr GLFramebufferFetchPlan DecideGLFramebufferFetchPlan(const GLFramebufferFetchCaps& caps,
    bool reads_color, bool reads_depth, bool depth_as_rt, bool color0_unorm, bool mrt, bool color0_rgba8 = true)
{
    auto color = GSFramebufferFetchBackend::None;
    auto depth = GLDepthFetch::Sampler;
    if (reads_color)
    {
        if (caps.preferred == GSFramebufferFetchBackend::ARM)
        {
            if (color0_unorm && (!mrt || caps.arm_mrt)) color = GSFramebufferFetchBackend::ARM;
            // Mobile's EXT arm is deliberately not a fallback from an
            // ineligible ARM draw: that is precisely its broken driver path.
        }
        else if (caps.preferred == GSFramebufferFetchBackend::EXT)
        {
            if (!caps.ext_single_rgba8 || (color0_rgba8 && !mrt))
                color = GSFramebufferFetchBackend::EXT;
        }
    }
    if (reads_depth && caps.preferred != GSFramebufferFetchBackend::None)
    {
        if (!depth_as_rt && caps.arm_depth) depth = GLDepthFetch::ARM;
        else if (depth_as_rt && caps.ext && caps.preferred == GSFramebufferFetchBackend::EXT &&
            !(caps.adreno && color == GSFramebufferFetchBackend::EXT) && !(caps.ext_single_rgba8 && mrt))
            depth = GLDepthFetch::EXT;
    }
    return {static_cast<unsigned char>(static_cast<unsigned>(color) | (static_cast<unsigned>(depth) << 2))};
}
