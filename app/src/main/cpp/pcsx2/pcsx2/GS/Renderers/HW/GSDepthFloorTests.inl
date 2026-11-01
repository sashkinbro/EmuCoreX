// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include <cmath>
#include "cpuinfo.h"
#include "common/ScopedGuard.h"

void GSRendererHW::TestDepthFloor(void (*check)(bool, const char*))
{
    cpuinfo_initialize();
    check(!g_texture_cache && !g_gs_renderer, "GS depth-floor fixture owns an isolated renderer");
    if (g_texture_cache || g_gs_renderer) return;
    const auto config = GSConfig;
    GSConfig = Pcsx2Config::GSOptions();
    const ScopedGuard restore([&]() { GSConfig = config; });
    auto renderer = std::make_unique<GSRendererHW>();
    auto& r = *renderer;
    bool identity = true, high = true, low = true, clamp = true;
    u32 math_cases = 0, draws = 0;
    // Independent float32 arithmetic witnesses around every exponent and
    // mantissa boundary from normalized Z=2^-9 through Z=1. At/above that
    // threshold the scaled value has no fractional float32 mantissa bits.
    for (u32 exponent = 118; exponent <= 127; ++exponent)
    for (u32 mantissa : {0u, 1u, 2u, 3u, 0x3fffffu, 0x400000u, 0x7ffffeu, 0x7fffffu})
    {
        const float z = std::bit_cast<float>((exponent << 23) | mantissa);
        const float result = std::floor(z * 0x1p32f) * 0x1p-32f;
        identity &= std::bit_cast<u32>(z) == std::bit_cast<u32>(result);
        ++math_cases;
    }
    // A below-threshold fractional Z is a negative control: the old floor
    // must remain available where it can change the interpolated result.
    const float fractional = std::bit_cast<float>((117u << 23) | 1u);
    identity &= fractional != std::floor(fractional * 0x1p32f) * 0x1p-32f;
    for (const u32 minimum : {0u, 1u, 0x7fffffu, 0x800000u, 0x800001u, 0x1000000u, 0x7fffffffu})
    for (const u32 format : {PSMZ32, PSMZ24, PSMZ16})
    for (const u32 primitive : {GS_POINT_CLASS, GS_LINE_CLASS, GS_TRIANGLE_CLASS, GS_SPRITE_CLASS})
    for (bool equal : {false, true})
    for (bool write : {false, true})
    for (const u32 test : {ZTST_NEVER, ZTST_ALWAYS, ZTST_GEQUAL, ZTST_GREATER})
    {
        r.ResetStates();
        r.m_cached_ctx = {};
        r.m_cached_ctx.TEST.ZTE = true;
        r.m_cached_ctx.TEST.ZTST = test;
        r.m_cached_ctx.ZBUF.ZMSK = !write;
        r.m_cached_ctx.ZBUF.PSM = format;
        r.m_env.PRIM.AA1 = false;
        r.m_vt.m_primclass = static_cast<GS_PRIM_CLASS>(primitive);
        r.m_vt.m_eq.z = equal;
        r.m_vt.m_min.p = GSVector4(0.0f, 0.0f, static_cast<float>(minimum), 0.0f);
        const u32 maximum = minimum + 0x10000u;
        r.m_vt.m_max.p = GSVector4(0.0f, 0.0f, static_cast<float>(maximum), 0.0f);
        r.EmulateZbuffer(nullptr);
        const bool flat = equal || primitive == GS_POINT_CLASS || primitive == GS_SPRITE_CLASS;
        const bool depth_write = write && test != ZTST_NEVER;
        const bool needed = depth_write || (!write && test == ZTST_GREATER);
        const bool expected_floor = !flat && minimum < 0x800000u && needed;
        (minimum < 0x800000u ? low : high) &= r.m_conf.ps.zfloor == expected_floor;
        const u32 limit = format == PSMZ32 ? 0xffffffffu : format == PSMZ24 ? 0x00ffffffu : 0xffffu;
        const bool large = static_cast<u32>(static_cast<float>(maximum)) > limit;
        const bool expected_clamp = depth_write && large && !flat;
        clamp &= r.m_conf.ps.zclamp == expected_clamp &&
            r.m_conf.cb_vs.max_depth == (depth_write && large && flat ? limit : 0xffffffffu) &&
            r.m_conf.ps.zwrite == (r.m_conf.ps.zfloor || expected_clamp);
        ++draws;
    }
    check(identity, "GS high-Z floor is float32 identity with a nonidentity low-Z negative control");
    check(high, "GS actual depth selector omits provably redundant high-Z fragment depth floors");
    check(low, "GS actual depth selector preserves nonidentity low-Z write and GREATER-test floors");
    check(clamp, "GS depth-floor elimination retains independent flat and interpolated 16/24/32-bit depth clamps");
    std::printf("GS_DEPTH_FLOOR math_cases=%u selector_cases=%u\n", math_cases, draws);
}
