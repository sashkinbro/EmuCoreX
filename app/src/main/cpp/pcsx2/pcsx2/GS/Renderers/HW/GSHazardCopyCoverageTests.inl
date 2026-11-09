// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Focused production-proof and allocation-path checks, using the existing CPU coherence device.
#include "GSCoherenceTestDevice.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
#include <limits>

void GSRendererHW::TestHazardCopyCoverage(void (*check)(bool, const char*))
{
    const bool isolated = !g_gs_renderer && !g_texture_cache && !g_gs_device;
    check(isolated, "GS hazard coverage owns isolated renderer/cache/device globals");
    if (!isolated) return;
    cpuinfo_initialize();
    const auto settings = GSConfig;
    const u64 saved_draw = GSState::s_n, saved_transfer = GSState::s_transfer_n;
    GSConfig = Pcsx2Config::GSOptions();
    g_gs_device = std::make_unique<CoherenceDevice>();
    g_gs_renderer = std::make_unique<GSRendererHW>();
    const ScopedGuard restore([&]() {
        g_gs_renderer.reset();
        g_gs_device->Destroy();
        g_gs_device.reset();
        GSConfig = settings;
        GSState::s_n = saved_draw; GSState::s_transfer_n = saved_transfer;
    });
    auto& r = *GSRendererHW::GetInstance();
    auto& device = *static_cast<CoherenceDevice*>(g_gs_device.get());
    device.allow_stretch = device.allow_copy = true;
    GSDevice::FeatureSupport features = {};
    features.texture_feedback_requires_copy = true;
    device.SetFeaturesForTesting(features);
    CoherenceTexture texture(GSTexture::RenderTarget, 64, 64, 1, GSTexture::Format::Color);
    CoherenceTexture depth(GSTexture::DepthStencil, 64, 64, 1, GSTexture::Format::DepthStencil);
    GIFRegTEX0 tex0 = {};
    tex0.TBW = 1; tex0.TW = tex0.TH = 6; tex0.PSM = PSMCT32;
    GSTextureCache::Target target(tex0, GSTextureCache::RenderTarget, GSVector2i(64, 64), 1.0f, &texture);
    GSTextureCache::Source source(tex0, GIFRegTEXA{});
    const ScopedGuard detach([&]() {
        r.m_conf.tex = r.m_conf.rt = r.m_conf.ds = nullptr;
        source.m_texture = nullptr; target.m_texture = nullptr;
    });
    const auto prepare = [&](bool fst = true, float scale = 1.0f) {
        r.ResetStates(); r.m_conf = GSHWDrawConfig{};
        GSConfig = Pcsx2Config::GSOptions();
        GSConfig.UserHacks_HalfPixelOffset = GSHalfPixelOffset::Normal;
        GSConfig.TriFilter = TriFiltering::Off; GSConfig.MaxAnisotropy = 1;
        GSConfig.AccurateBlendingUnit = AccBlendLevel::Minimum;
        r.m_env.PRIM = GIFRegPRIM{}; r.m_env.PRIM.PRIM = GS_SPRITE; r.m_env.PRIM.FST = fst; r.PRIM = &r.m_env.PRIM;
        r.m_context = &r.m_env.CTXT[0];
        r.m_cached_ctx.TEX0 = tex0;
        r.m_cached_ctx.FRAME = GIFRegFRAME{}; r.m_cached_ctx.FRAME.FBW = 1;
        r.m_cached_ctx.CLAMP = GIFRegCLAMP{};
        r.m_cached_ctx.CLAMP.WMS = r.m_cached_ctx.CLAMP.WMT = CLAMP_CLAMP;
        r.m_vt.m_primclass = GS_SPRITE_CLASS; r.m_vt.m_filter = {};
        r.m_vt.m_eq.q = true; r.m_vt.m_accurate_stq = false;
        r.m_downscale_source = r.m_texture_shuffle = r.m_channel_shuffle = false;
        r.m_userhacks_tcoffset = r.m_sprite_edge_clamp = false;
        r.m_r = GSVector4i(0, 0, 32, 32);
        r.m_vertex.head = 0; r.m_vertex.next = r.m_vertex.tail = r.m_index.tail = 2;
        for (int i = 0; i < 2; ++i) {
            r.m_index.buff[i] = i; r.m_vertex.buff[i] = {};
            r.m_vertex.buff[i].U = r.m_vertex.buff[i].V = i ? 380 : 132;
            r.m_vertex.buff[i].ST.S = r.m_vertex.buff[i].ST.T = (i ? 23.75f : 8.25f) / 64.0f;
            r.m_vertex.buff[i].RGBAQ.Q = 1.0f;
        }
        source.m_texture = &texture; source.m_target = true;
        source.m_target_direct = source.m_shared_texture = true;
        source.m_from_target = &target; source.m_TEX0 = source.m_from_target_TEX0 = tex0;
        source.m_unscaled_size = GSVector2i(64, 64); source.m_scale = scale; source.m_region = {};
        r.m_conf.tex = r.m_conf.rt = &texture;
    };
    const auto covered = [&](float scale = 1.0f, GSVector4 rect = GSVector4(4, 4, 28, 28)) {
        return r.HazardCopyCoversSampling(&target, nullptr, &source, rect * scale,
            GSVector2i(64, 64), GSVector2i(static_cast<int>(64 * scale), static_cast<int>(64 * scale)), scale);
    };
    for (float scale : {1.0f, 1.5f, 2.0f, 3.0f}) {
        for (bool fst : {false, true}) {
            prepare(fst, scale);
            check(covered(scale), "GS hazard proof covers UV/constant-STQ at native/fractional/integer scale");
            r.m_vt.m_filter.opt_linear = true;
            check(covered(scale), "GS hazard proof covers bilinear footprint with copied margin");
            r.m_vt.m_filter.opt_linear = false; GSConfig.TriFilter = TriFiltering::Forced;
            check(covered(scale), "GS hazard proof covers forced-trilinear bilinear footprint");
            check(!covered(scale, GSVector4(9, 4, 28, 28)), "GS hazard proof rejects an uncovered sampled edge");
        }
    }
    prepare(false);
    r.m_vertex.buff[1].RGBAQ.Q = 2.0f;
    check(!covered(), "GS hazard proof rejects varying Q");
    for (float q : {0.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        r.m_vertex.buff[0].RGBAQ.Q = r.m_vertex.buff[1].RGBAQ.Q = q;
        check(!covered(), "GS hazard proof rejects zero/nonfinite constant Q");
    }
    prepare(false);
    r.m_vertex.buff[0].ST.S = std::numeric_limits<float>::quiet_NaN();
    check(!covered(), "GS hazard proof rejects a mixed finite/NaN vertex range");
    prepare(false);
    r.m_vertex.buff[0].ST.S = -2.0f / 64.0f;
    check(covered(1.0f, GSVector4(0, 4, 28, 28)), "GS hazard proof models clamp-to-edge");
    r.m_cached_ctx.CLAMP.WMS = CLAMP_REPEAT;
    check(!covered(1.0f, GSVector4(0, 4, 28, 28)), "GS hazard proof refuses repeat wrapping to uncopied edge");
    prepare(false);
    r.m_vertex.buff[0].ST.S = r.m_vertex.buff[0].ST.T = 0;
    r.m_vertex.buff[1].ST.S = r.m_vertex.buff[1].ST.T = 60.0f / 64.0f;
    r.m_cached_ctx.CLAMP.WMS = r.m_cached_ctx.CLAMP.WMT = CLAMP_REGION_CLAMP;
    r.m_cached_ctx.CLAMP.MINU = r.m_cached_ctx.CLAMP.MINV = 8;
    r.m_cached_ctx.CLAMP.MAXU = r.m_cached_ctx.CLAMP.MAXV = 24;
    check(covered(1.0f, GSVector4(7, 7, 26, 26)), "GS hazard proof models per-tap region clamp");
    prepare();
    r.m_vertex.buff[0].U = 129;
    check(!covered(1.0f, GSVector4(8, 4, 28, 28)), "GS hazard proof includes unknown-device four-bit sampler slack");
    features.sub_texel_precision_bits = 8; device.SetFeaturesForTesting(features);
    check(covered(1.0f, GSVector4(8, 4, 28, 28)), "GS hazard proof uses reported sampler subtexel precision");
    features.sub_texel_precision_bits = 0; device.SetFeaturesForTesting(features);
    bool GSRendererHW::* const exclusions[] = {&GSRendererHW::m_downscale_source, &GSRendererHW::m_texture_shuffle,
        &GSRendererHW::m_channel_shuffle, &GSRendererHW::m_userhacks_tcoffset, &GSRendererHW::m_sprite_edge_clamp};
    for (bool GSRendererHW::*flag : exclusions) {
        prepare(); r.*flag = true;
        check(!covered(), "GS hazard proof retains coordinate-rewrite exclusions");
    }
    prepare(); r.m_vt.m_primclass = GS_TRIANGLE_CLASS; r.m_env.PRIM.AA1 = true;
    check(!covered(), "GS hazard proof excludes AA1 attribute extrapolation");
    r.m_env.PRIM.ABE = true;
    check(!covered(), "GS hazard proof excludes blended AA1 attribute extrapolation");
    prepare(false); r.m_vt.m_accurate_stq = true;
    check(!covered(), "GS hazard proof excludes accurate-STQ vertex predivision");
    prepare(); r.m_cached_ctx.CLAMP.WMT = CLAMP_REGION_REPEAT;
    check(!covered(), "GS hazard proof excludes region repeat");
    prepare(); r.m_cached_ctx.TEX0.TW = 5;
    check(!covered(), "GS hazard proof excludes STScale texture-size mismatch");
    prepare(); r.m_vt.m_primclass = GS_TRIANGLE_CLASS; GSConfig.MaxAnisotropy = 2;
    check(!covered(), "GS hazard proof excludes anisotropic taps");
    prepare(); source.m_texture = &depth;
    check(!covered(), "GS hazard proof excludes depth sampling");
    prepare(); r.m_cached_ctx.TEX0.PSM = PSMT8;
    check(!covered(), "GS hazard proof excludes paletted manual deswizzle");
    prepare(); source.m_from_target_TEX0.PSM = PSMZ32;
    check(!covered(), "GS hazard proof excludes depth-format manual deswizzle");
    prepare(); r.m_index.tail = 0;
    check(!covered(), "GS hazard proof rejects empty geometry");
    prepare();
    check(!r.HazardCopyCoversSampling(&target, nullptr, &source, GSVector4(4, 4, 28, 28),
        GSVector2i(63, 64), GSVector2i(95, 96), 1.5f), "GS hazard proof rejects rounded fractional texture dimensions");
    prepare(true, 2.0f); GSConfig.UserHacks_HalfPixelOffset = GSHalfPixelOffset::NativeWTexOffset;
    r.m_r = GSVector4i(0, 0, 128, 128);
    GSVector2 offset(0, 0); r.ApplyNativeWTexOffset(&source, &target, nullptr, offset);
    const float expected = (1.0f - ((0.5f / (64 * 2.0f)) * 64)) * 8.0f;
    check(offset.x == expected && offset.y == expected, "GS hazard and sampler share exact NativeWTexOffset arithmetic");
    r.m_vertex.buff[1].XYZ.X = 8; offset = GSVector2(0, 0);
    r.ApplyNativeWTexOffset(&source, &target, nullptr, offset);
    check(offset.x == 0 && offset.y == expected, "GS NativeWTexOffset keeps independent far-edge phases");

    // The actual hazard path must pass clear=false only when its copied rectangle proves coverage.
    // CoherenceDevice's exact copy leaves poison outside the copied range, making the dead data explicit.
    for (bool exclude : {false, true}) {
        prepare(); r.m_userhacks_tcoffset = exclude;
        device.PurgePool(); device.copies = 0;
        for (u32 i = 0; i < texture.pixels.size(); ++i) texture.pixels[i] = 0x80000000u | i;
        TextureMinMaxResult tmm = {GSVector4i(4, 4, 28, 28), 0};
        GSTextureCache::SourceRegion region = {}; bool target_region = false;
        GSVector2i size(64, 64); float scale = 1.0f; GSDevice::RecycledTexture snapshot;
        r.HandleTextureHazards(&target, nullptr, &source, tmm, region, target_region, size, scale, snapshot);
        check(snapshot && device.copies == 1, "GS hazard coverage executes the real partial-copy allocation path");
        if (snapshot) {
            check(snapshot->GetState() == (exclude ? GSTexture::State::Cleared : GSTexture::State::Invalidated),
                "GS hazard allocation omits only the proven-dead clear and retains excluded fallback clear");
            const auto* copy = static_cast<const CoherenceTexture*>(snapshot.get());
            check(copy->pixels[0] == 0xcdcdcdcdu && copy->pixels[8 * 64 + 8] == texture.pixels[8 * 64 + 8],
                "GS hazard exact copy leaves outside poison while populating sampled texels");
        }
    }
    check(device.unexpected_calls == 0 && device.invalid_copies == 0,
        "GS hazard coverage fixture performs valid copies without submission/present/readback");
}
