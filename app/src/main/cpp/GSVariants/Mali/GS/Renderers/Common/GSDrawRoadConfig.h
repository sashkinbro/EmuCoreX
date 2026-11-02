// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "GS/Renderers/Common/GSDevice.h"

inline GSDrawRoadDevice GSGetDrawRoadDevice(const GSDevice::FeatureSupport& f)
{
    return {.texture_barrier = f.texture_barrier,
        .framebuffer_fetch = f.framebuffer_fetch,
        .feedback_loop_layout = f.feedback_loop_layout,
        .fetch_orders_overlap = f.framebuffer_fetch_orders_overlap,
        .declared_loop_orders_overlap = f.declared_feedback_loop_orders_overlap,
        .carry = f.feedback_carry};
}

// The RT snapshot is shared by the primary and alpha pass.
inline bool GSNeedsRTSnapshotBinding(const GSHWDrawConfig& conf)
{
    return conf.require_one_barrier || (conf.alpha_second_pass.enable && conf.alpha_second_pass.require_one_barrier);
}

// Availability alone does not select the input road when the user chose copies.
inline bool GSUsesInTileFeedbackRoad(bool available, bool texture_barriers)
{
    return available && texture_barriers;
}

inline GSDrawRoad GSDecideHWDrawRoad(const GSHWDrawConfig& conf, const GSDevice::FeatureSupport& f)
{
    const auto& second = conf.alpha_second_pass;
    const bool color_attached = conf.rt && !conf.ps.HasColorROV();
    const bool depth_attached = conf.ds && !conf.ps.HasDepthROV();
    return GSDecideDrawRoad(GSGetDrawRoadDevice(f),
        {.reads_rt = color_attached && (conf.IsFeedbackLoopRT(conf.ps) || (second.enable && conf.IsFeedbackLoopRT(second.ps))),
            .second_pass_reads_rt = color_attached && second.enable && conf.IsFeedbackLoopRT(second.ps),
            .reads_depth = depth_attached && (conf.IsFeedbackLoopDepth(conf.ps) || (second.enable && conf.IsFeedbackLoopDepth(second.ps))),
            .samples_attached_depth = depth_attached && conf.tex == conf.ds,
            .one_barrier = GSNeedsRTSnapshotBinding(conf),
            .any_barrier = conf.require_one_barrier || conf.require_full_barrier ||
                (second.enable && (second.require_one_barrier || second.require_full_barrier)),
            .writes_depth = (depth_attached && conf.depth.zwe) || (second.enable && second.depth.zwe),
            .offset_read_hits_write = conf.offset_read_hits_write});
}
