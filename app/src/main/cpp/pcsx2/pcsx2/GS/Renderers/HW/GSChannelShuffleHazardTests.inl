// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include from GSRendererHW.cpp under EMUCOREX_ENABLE_NATIVE_SELF_TESTS.
#include "GSCoherenceTestDevice.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"

void GSRendererHW::TestChannelShuffleHazards(void (*check)(bool, const char*))
{
    const bool isolated = !g_gs_renderer && !g_texture_cache && !g_gs_device;
    check(isolated, "GS shuffle hazards require isolated renderer/cache/device globals");
    if (!isolated)
        return;
    cpuinfo_initialize();
    const auto settings = GSConfig;
    const u64 saved_draw = GSState::s_n;
    const u64 saved_transfer = GSState::s_transfer_n;
    GSConfig = Pcsx2Config::GSOptions();
    GSConfig.AccurateBlendingUnit = AccBlendLevel::Basic;
    g_gs_device = std::make_unique<CoherenceDevice>();
    g_gs_renderer = std::make_unique<GSRendererHW>();
    const ScopedGuard restore([&]() {
        g_gs_renderer.reset();
        g_gs_device->Destroy();
        g_gs_device.reset();
        GSConfig = settings;
        GSState::s_n = saved_draw;
        GSState::s_transfer_n = saved_transfer;
    });
    auto& renderer = *GSRendererHW::GetInstance();
    auto& device = *static_cast<CoherenceDevice*>(g_gs_device.get());
    device.allow_stretch = device.allow_copy = true;
    GSDevice::FeatureSupport features = {};
    features.texture_feedback_requires_copy = true;
    features.texture_barrier = false;
    features.multidraw_fb_copy = false;
    device.SetFeaturesForTesting(features);

    CoherenceTexture source_texture(GSTexture::RenderTarget, 512, 448, 1, GSTexture::Format::Color);
    CoherenceTexture destination_texture(GSTexture::RenderTarget, 512, 896, 1, GSTexture::Format::Color);
    CoherenceTexture depth_texture(GSTexture::DepthStencil, 512, 896, 1, GSTexture::Format::DepthStencil);
    GIFRegTEX0 source_tex0 = {};
    source_tex0.TBP0 = 0x2400;
    source_tex0.TBW = 8;
    source_tex0.PSM = PSMCT32;
    GIFRegTEX0 destination_tex0 = source_tex0;
    destination_tex0.TBP0 = 0;
    GIFRegTEX0 depth_tex0 = source_tex0;
    depth_tex0.TBP0 = 0x3000;
    depth_tex0.PSM = PSMZ32;
    GSTextureCache::Target source_target(source_tex0, GSTextureCache::RenderTarget, GSVector2i(512, 448), 1.0f, &source_texture);
    GSTextureCache::Target destination_target(destination_tex0, GSTextureCache::RenderTarget, GSVector2i(512, 896), 1.0f, &destination_texture);
    GSTextureCache::Target depth_target(depth_tex0, GSTextureCache::DepthStencil, GSVector2i(512, 896), 1.0f, &depth_texture);
    GIFRegTEX0 lookup_tex0 = source_tex0;
    lookup_tex0.TBP0 = 0x2900;
    lookup_tex0.TBW = 1;
    lookup_tex0.PSM = PSMT8;
    lookup_tex0.TW = 9;
    lookup_tex0.TH = 9;
    GSTextureCache::Source source(lookup_tex0, GIFRegTEXA{});
    const ScopedGuard detach([&]() {
        renderer.m_conf.tex = renderer.m_conf.rt = renderer.m_conf.ds = nullptr;
        source.m_texture = nullptr;
        source_target.m_texture = destination_target.m_texture = depth_target.m_texture = nullptr;
    });
    source.m_texture = &source_texture;
    source.m_from_target = &source_target;
    source.m_target = source.m_target_direct = source.m_shared_texture = true;
    source.m_unscaled_size = GSVector2i(512, 448);
    source.m_scale = 1.0f;
    source_target.m_valid = GSVector4i(0, 0, 512, 448);
    destination_target.m_valid = depth_target.m_valid = GSVector4i(0, 0, 512, 896);

    const auto prepare = [&](bool source_is_rt, bool source_offset) {
        renderer.ResetStates();
        renderer.m_conf = GSHWDrawConfig{};
        device.PurgePool();
        device.allocations = device.copies = device.invalid_copies = 0;
        device.stretches.clear();
        renderer.m_channel_shuffle = true;
        renderer.m_texture_shuffle = false;
        renderer.m_downscale_source = false;
        renderer.m_in_target_draw = false;
        renderer.m_prim_overlap = PRIM_OVERLAP_YES;
        renderer.m_r = GSVector4i(0, 0, 512, 448);
        renderer.m_vt.m_min.t = GSVector4::zero();
        renderer.m_vt.m_max.t = GSVector4(512, 448, 0, 0);
        renderer.m_cached_ctx.TEX0 = lookup_tex0;
        renderer.m_cached_ctx.TEX0.TBP0 = source_offset ? 0x2900 : 0x2400;
        renderer.m_cached_ctx.FRAME = GIFRegFRAME{};
        renderer.m_cached_ctx.FRAME.FBP = (source_is_rt ? 0x2400 : 0) >> 5;
        renderer.m_cached_ctx.FRAME.FBW = 8;
        renderer.m_cached_ctx.FRAME.PSM = PSMCT32;
        renderer.m_cached_ctx.CLAMP = GIFRegCLAMP{};
        renderer.m_cached_ctx.CLAMP.WMS = CLAMP_CLAMP;
        renderer.m_cached_ctx.CLAMP.WMT = CLAMP_CLAMP;
        renderer.m_conf.tex = &source_texture;
        renderer.m_conf.rt = source_is_rt ? &source_texture : &destination_texture;
        source.m_region = {};
    };
    const TextureMinMaxResult tmm = {GSVector4i(0, 0, 512, 448), 0};
    const auto region = []() {
        GSTextureCache::SourceRegion value = {};
        value.SetX(0, 512);
        value.SetY(0, 448);
        return value;
    };

    // Captured Hitman Blood Money geometry: 0x500 blocks /32 /8 = five
    // 32-row pages, so the shader must read row y+160 from this source.
    // Neither active framebuffer aliases it, even on a device that snapshots
    // actual feedback. Calling the production handler must retain that binding.
    for (bool with_depth : {false, true})
    {
        prepare(false, true);
        renderer.m_conf.ds = with_depth ? &depth_texture : nullptr;
        auto source_region = region();
        bool target_region = true;
        GSVector2i unscaled_size(1, 1);
        float scale = 0.0f;
        GSDevice::RecycledTexture snapshot;
        renderer.HandleTextureHazards(&destination_target, with_depth ? &depth_target : nullptr,
            &source, tmm, source_region, target_region, unscaled_size, scale, snapshot);
        check(renderer.m_conf.tex == &source_texture && !snapshot,
            "GS offset shuffle retains unrelated source on feedback-copy devices");
        check(unscaled_size == GSVector2i(512, 448) && scale == 1.0f,
            "GS unrelated offset shuffle uses source dimensions and scale");
        check(renderer.m_conf.cb_ps.ChannelShuffleOffset.x == 0.0f &&
                renderer.m_conf.cb_ps.ChannelShuffleOffset.y == 160.0f,
            "GS unrelated offset shuffle addresses source row y+160");
        check(!target_region && source_region.bits == 0,
            "GS unrelated offset shuffle consumes the region after setting shader offset");
        check(device.allocations == 0 && device.copies == 0 && device.stretches.empty(),
            "GS unrelated offset shuffle requires no snapshot allocation or blit");
        check(!renderer.m_conf.require_one_barrier && !renderer.m_conf.require_full_barrier,
            "GS unrelated offset shuffle creates no feedback barrier requirement");
    }

    // An attached source really is feedback: removing its snapshot would make
    // the positive case pass by skipping all hazard handling. Retain the source
    // coordinate system in the snapshot and keep its 160-row shader offset.
    {
        prepare(true, true);
        auto source_region = region();
        bool target_region = true;
        GSVector2i unscaled_size(1, 1);
        float scale = 0.0f;
        GSDevice::RecycledTexture snapshot;
        renderer.HandleTextureHazards(&source_target, nullptr, &source, tmm,
            source_region, target_region, unscaled_size, scale, snapshot);
        check(snapshot && renderer.m_conf.tex == snapshot.get() && renderer.m_conf.tex != &source_texture,
            "GS offset shuffle snapshots a source attached as the current render target");
        check(device.allocations == 1 && device.copies + device.stretches.size() == 1,
            "GS attached offset shuffle executes one snapshot allocation and copy or blit");
        check(unscaled_size == GSVector2i(512, 448) &&
                renderer.m_conf.cb_ps.ChannelShuffleOffset.y == 160.0f,
            "GS attached offset shuffle snapshot preserves source dimensions and shader offset");
    }

    // Same-pixel channel feedback already has a safe tex-is-fb route; it must
    // stay available after fixing the unrelated-source offset path.
    {
        prepare(true, false);
        GSTextureCache::SourceRegion source_region = {};
        bool target_region = false;
        GSVector2i unscaled_size(1, 1);
        float scale = 0.0f;
        GSDevice::RecycledTexture snapshot;
        renderer.HandleTextureHazards(&source_target, nullptr, &source, tmm,
            source_region, target_region, unscaled_size, scale, snapshot);
        check(renderer.m_conf.ps.tex_is_fb && !renderer.m_conf.tex && !snapshot,
            "GS non-offset self shuffle retains the tex-is-fb route");
        check(device.allocations == 0 && device.copies == 0 && device.stretches.empty(),
            "GS non-offset tex-is-fb shuffle allocates no snapshot");
    }
    check(device.unexpected_calls == 0 && device.invalid_copies == 0,
        "GS shuffle hazard fixture performs no GPU submission, present, or readback calls");
}

extern "C" void EmuCoreXOracleTestChannelShuffleHazards(void (*check)(bool, const char*))
{
    GSRendererHW::TestChannelShuffleHazards(check);
    GSRendererHW::TestHazardCopyCoverage(check);
}
