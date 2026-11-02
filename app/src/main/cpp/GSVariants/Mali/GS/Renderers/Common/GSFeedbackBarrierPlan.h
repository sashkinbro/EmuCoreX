// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "GS/Renderers/Common/GSDrawRoadConfig.h"

// Shared by the actual HW planner and the native plannerв†’backend oracle.
inline void GSApplyPlannedFeedbackBarriers(GSHWDrawConfig& config,
    const GSDevice::FeatureSupport& features, RenderAPI api, bool overlap,
    bool need_barriers_for_depth, bool channel_shuffle)
{
    if (!features.framebuffer_fetch && !features.declared_feedback_loop_orders_overlap)
        return;
    if (features.framebuffer_fetch && (config.require_one_barrier || config.require_full_barrier))
        pxAssert(!config.blend.enable);
    // OGL chooses a legal backend per draw and per alpha pass. Preserve the
    // planner's ordering request until that backend can decide what to drop.
    const bool drop = api == RenderAPI::OpenGL ? false : api == RenderAPI::Vulkan ?
        GSDrawDropsBarriers(GSGetDrawRoadDevice(features), config.tex_hazard == GSHWDrawConfig::TEX_HAZARD_RT,
            overlap, need_barriers_for_depth) : !need_barriers_for_depth;
    if (drop)
    {
        config.raster_order = features.declared_loop_overlap_needs_raster_order && config.require_full_barrier &&
            overlap && !config.ps.shuffle && !channel_shuffle;
        config.require_one_barrier = config.require_full_barrier = false;
    }
}
