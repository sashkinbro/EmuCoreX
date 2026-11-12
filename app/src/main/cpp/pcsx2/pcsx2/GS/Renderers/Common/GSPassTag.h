// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once

// Debug-only (EMUCOREX_GS_PASS_TIMING=1): describes the HW draw in flight so the Vulkan
// device can attribute each render pass to the draw that opened it (GS_PASS_SIZES lines).
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && !defined(NDEBUG)
struct GSPassTag
{
	bool active = false;
	char kind[40] = {};
	unsigned fbp = 0, fbw = 0, fpsm = 0;
	unsigned tbp = 0, tbw = 0, tpsm = 0;
	bool tme = false;
	bool src_from_target = false, src_is_rt = false, src_downscaled = false, src_direct = false;
};
extern bool g_emucorex_pass_tag_on;
extern GSPassTag g_emucorex_pass_tag;
#endif
