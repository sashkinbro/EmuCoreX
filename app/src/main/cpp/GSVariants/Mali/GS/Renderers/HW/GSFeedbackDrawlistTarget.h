// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "GS/Renderers/HW/GSTextureCache.h"

// Depth-only feedback has no colour target but uses the same draw scale.
inline const GSTextureCache::Target* GSResolveFeedbackDrawlistTarget(
    const GSTextureCache::Target* rt, const GSTextureCache::Target* ds)
{
    return rt ? rt : ds;
}
