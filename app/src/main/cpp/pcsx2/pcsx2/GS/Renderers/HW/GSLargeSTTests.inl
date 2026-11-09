// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Exercise the real Draw entry before the PS2 24-bit DATE early-out.
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
void GSRendererHW::TestLargeST(void (*check)(bool, const char*))
{
    cpuinfo_initialize();
    const bool isolated = !g_gs_renderer && !g_texture_cache && !g_gs_device;
    check(isolated, "GS large-ST fixture owns isolated renderer");
    if (!isolated) return;
    const auto saved = GSConfig;
    GSConfig = Pcsx2Config::GSOptions();
    GSConfig.LoadTextureReplacements = GSConfig.DumpReplaceableTextures = false;
    g_gs_device = std::make_unique<GSDeviceVK>();
    g_gs_renderer = std::make_unique<GSRendererHW>();
    auto& r = *static_cast<GSRendererHW*>(g_gs_renderer.get());
    const ScopedGuard restore([&]() { g_gs_renderer.reset(); g_gs_device.reset(); GSConfig = saved; });
    for (const bool enabled : {false, true})
    {
        r.ResetStates();
        r.PRIM = &r.m_env.PRIM;
        r.m_env.PRIM.PRIM = GS_SPRITE;
        r.m_env.PRIM.TME = true;
        r.m_env.PRIM.FST = false;
        r.m_draw_env = &r.m_env;
        r.m_context = &r.m_env.CTXT[0];
        r.m_context->FRAME.PSM = PSMCT24;
        r.m_context->TEST.DATE = true;
        r.m_context->TEX0.TW = r.m_context->TEX0.TH = 4;
        r.m_context->CLAMP.WMS = r.m_context->CLAMP.WMT = CLAMP_REPEAT;
        r.m_vertex.head = 0; r.m_vertex.next = r.m_vertex.tail = 2;
        r.m_index.tail = 2;
        for (int i = 0; i < 2; ++i)
        {
            r.m_vertex.buff[i] = {};
            r.m_vertex.buff[i].ST.S = 100.0f;
            r.m_vertex.buff[i].ST.T = 200.0f;
            r.m_vertex.buff[i].RGBAQ.Q = i ? 2.0f : 1.0f;
            r.m_vertex.buff[i].XYZ.X = r.m_vertex.buff[i].XYZ.Y = (i + 1) * 16;
            r.m_index.buff[i] = i;
        }
        r.m_vt.m_primclass = GS_SPRITE_CLASS;
        r.m_vt.m_min.p = GSVector4(1.0f);
        r.m_vt.m_max.p = GSVector4(2.0f);
        r.m_vt.m_min.t = GSVector4(800.0f, 1600.0f, 2.0f, 2.0f);
        r.m_vt.m_max.t = r.m_vt.m_min.t;
        r.m_vt.nan.value = 0;
        GSConfig.UserHacks_RewriteLargeSTCoords = enabled;
        r.Draw();
        check(r.m_vertex.buff[0].ST.S == (enabled ? 16.0f : 100.0f) &&
              r.m_vertex.buff[1].ST.T == (enabled ? 16.0f : 200.0f),
              enabled ? "GS actual Draw rewrites large ST even for repeat mode when opted in" :
                        "GS actual Draw preserves ST when rewrite is disabled");
        check(r.m_vertex.buff[1].RGBAQ.Q == (enabled ? 1.0f : 2.0f),
              "GS large-ST rewrites both coordinates with canonical Q only when enabled");
        if (enabled)
            check(r.m_vt.m_min.t.x == 256.0f && r.m_vt.m_max.t.y == 256.0f,
                  "GS rewritten sprite trace uses the second vertex Q");
    }
}
