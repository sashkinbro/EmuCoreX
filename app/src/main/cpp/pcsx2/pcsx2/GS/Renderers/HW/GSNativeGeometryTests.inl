// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "GSCoherenceTestDevice.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
void GSRendererHW::TestNativeGeometry(void (*check)(bool, const char*))
{
    cpuinfo_initialize();
    const bool isolated = !g_gs_renderer && !g_texture_cache && !g_gs_device;
    check(isolated, "GS native geometry owns isolated renderer");
    if (!isolated) return;
    const auto saved = GSConfig;
    GSConfig = Pcsx2Config::GSOptions();
    GSConfig.LoadTextureReplacements = GSConfig.DumpReplaceableTextures = false;
    g_gs_device = std::make_unique<CoherenceDevice>();
    g_gs_renderer = std::make_unique<GSRendererHW>();
    auto& r = *static_cast<GSRendererHW*>(g_gs_renderer.get());
    const ScopedGuard restore([&]() { g_gs_renderer.reset(); g_gs_device.reset(); GSConfig = saved; });
    const auto setup = [&]<class T>(T& renderer, float scale) {
        if constexpr (std::is_same_v<decltype(renderer.SetupIA(scale, .01f, .01f, false, false)), bool>)
            return renderer.SetupIA(scale, .01f, .01f, false, false);
        else { renderer.SetupIA(scale, .01f, .01f, false, false); return true; }
    };
    const auto prepare = [&](GS_PRIM_CLASS cls, u32 count) {
        r.ResetStates(); r.m_conf = {};
        r.PRIM = &r.m_env.PRIM; r.m_env.PRIM.U64 = 0;
        r.m_context = &r.m_env.CTXT[0]; r.m_context->XYOFFSET.OFX = r.m_context->XYOFFSET.OFY = 0;
        r.m_draw_env = &r.m_env;
        r.m_vt.m_primclass = cls; r.m_vt.m_accurate_stq = false;
        r.m_vertex.head = 0; r.m_vertex.next = r.m_vertex.tail = count; r.m_index.tail = count;
        for (u32 i=0; i<count; ++i) { r.m_vertex.buff[i] = {}; r.m_index.buff[i] = i; }
    };
    prepare(GS_POINT_CLASS, 1);
    r.m_vertex.buff[0].XYZ.X = 8; r.m_vertex.buff[0].XYZ.Y = 23;
    setup(r, 1.0f);
    check(r.m_vertex.buff[0].XYZ.X == 16 && r.m_vertex.buff[0].XYZ.Y == 16,
          "GS actual native point placement rounds half pixels onto the GS pixel boundary");
    prepare(GS_POINT_CLASS, 1);
    r.m_vertex.buff[0].XYZ.X = r.m_vertex.buff[0].XYZ.Y = 24;
    r.m_conf.cb_vs.vertex_offset = GSVector2(.75f);
    setup(r, 2.0f);
    check(r.m_vertex.buff[0].XYZ.X == 32 && std::abs(r.m_conf.cb_vs.vertex_offset.x-.92f)<1e-6f,
          "GS actual upscaled point placement supplies centred figure offset independently of align mode");
    prepare(GS_LINE_CLASS, 2);
    r.m_vertex.buff[1].XYZ.X = 64;
    const bool line_drawn = setup(r, 1.0f);
    check(line_drawn && r.m_conf.topology == GSHWDrawConfig::Topology::Triangle && r.m_conf.nindices == 6,
          "GS actual native horizontal line converts to an exact pixel-run rectangle");
    prepare(GS_LINE_CLASS, 2);
    const bool empty_drawn = setup(r, 1.0f);
    check(!empty_drawn, "GS actual zero-length line submits no geometry");
    prepare(GS_POINT_CLASS, 1);
    GSConfig.UpscaleMultiplier = 2.0f;
    GSConfig.UserHacks_HalfPixelOffset = GSHalfPixelOffset::Special;
    r.m_env.PRIM.FST = 1; r.m_vertex.buff[0].U = 8;
    r.m_texture_shuffle = true;
    GIFRegTEX0 tex0{}; GSTextureCache::Source source(tex0, GIFRegTEXA{});
    source.m_scale = 2.0f;
    check((r.RealignTargetTextureCoordinate(&source) == GSVector4::zero()).mask() == 15,
          "GS actual texture-shuffle coordinates refuse duplicate target realignment");
    const auto grid_enabled = []<class T>(const T& ps) {
        if constexpr (requires { ps.native_texel_grid; }) return !!ps.native_texel_grid;
        return false;
    };
    GSPrivRegSet regs{}; r.SetRegsMem(reinterpret_cast<u8*>(&regs));
    for (const float target_scale : {1.0f, 1.5f, 2.0f})
    for (const bool linear : {false, true})
    {
        prepare(GS_SPRITE_CLASS, 2);
        r.m_texture_shuffle = r.m_channel_shuffle = false;
        r.m_env.PRIM.FST = r.m_env.PRIM.TME = true;
        r.m_context->TEX0 = GIFRegTEX0{};
        r.m_context->TEX0.TBW = 1; r.m_context->TEX0.TW = r.m_context->TEX0.TH = 4;
        r.m_context->TEX1 = GIFRegTEX1{};
        r.m_context->CLAMP = GIFRegCLAMP{};
        r.m_cached_ctx.TEX0 = r.m_context->TEX0;
        r.m_cached_ctx.CLAMP = r.m_context->CLAMP;
        r.m_cached_ctx.FRAME = GIFRegFRAME{};
        r.m_vertex.buff[1].XYZ.X = r.m_vertex.buff[1].XYZ.Y = 64;
        r.m_vertex.buff[1].U = 128; r.m_vertex.buff[1].V = 64;
        r.m_vt.m_filter.linear = r.m_vt.m_filter.opt_linear = linear;
        r.m_vt.m_min.t = GSVector4::zero(); r.m_vt.m_max.t = GSVector4(8,4,1,1);
        CoherenceTexture pixels(GSTexture::Texture,16,16,1,GSTexture::Format::Color);
        CoherenceTexture rendered(GSTexture::RenderTarget,32,32,1,GSTexture::Format::Color);
        GSTextureCache::Source grid_source(r.m_context->TEX0,GIFRegTEXA{});
        grid_source.m_texture = &pixels; grid_source.m_unscaled_size = GSVector2i(16,16);
        grid_source.m_scale = 1.0f;
        GSTextureCache::Target target(r.m_context->TEX0,GSTextureCache::RenderTarget,GSVector2i(16,16),target_scale,&rendered);
        const ScopedGuard detach([&](){grid_source.m_texture=nullptr;target.m_texture=nullptr;});
        GSDevice::RecycledTexture copy;
        r.EmulateTextureSampler(&target,nullptr,&grid_source,TextureMinMaxResult{},copy);
        check(grid_enabled(r.m_conf.ps) == (target_scale>1.0f && !linear),
              "GS actual sampler preserves native minifying texel ownership only above 1x with nearest filtering");
    }
    for(const bool opt_out : {false,true})
    {
        prepare(GS_TRIANGLE_CLASS,3);r.m_cached_ctx={};r.m_vt.m_eq.z=0;
        r.m_vt.m_min.p=GSVector4(0,0,1,0);r.m_vt.m_max.p=GSVector4(1,1,100,0);
        r.m_cached_ctx.ZBUF.PSM=PSMZ32;r.m_cached_ctx.ZBUF.ZMSK=0;
        r.m_cached_ctx.TEST.ZTE=1;r.m_cached_ctx.TEST.ZTST=ZTST_GREATER;
        static_cast<CoherenceDevice*>(g_gs_device.get())->SetDepthQuantizationOptOut(opt_out);
        r.EmulateZbuffer(nullptr);
        check(r.m_conf.ps.zfloor==!opt_out,
              "GS actual depth setup respects exact device quantization opt-out below the identity floor threshold");
    }
    const auto union_covered=[]<class T>(const T& renderer) {
        if constexpr(requires {renderer.m_primitive_union_covers_rect;}) return renderer.m_primitive_union_covers_rect;
        return false;
    };
    prepare(GS_SPRITE_CLASS,4);r.m_r=GSVector4i(0,0,4,4);
    r.m_vertex.buff[1].XYZ.X=r.m_vertex.buff[1].XYZ.Y=64;
    r.m_vertex.buff[3].XYZ.X=r.m_vertex.buff[3].XYZ.Y=64;
    r.CalculatePrimitiveCoversWithoutGaps();
    check(union_covered(r),"GS actual primitive coverage recognizes overlapping sprite union without claiming gap-free tiling");
    r.m_vertex.buff[1].XYZ.X=16;r.m_vertex.buff[2].XYZ.X=48;
    r.CalculatePrimitiveCoversWithoutGaps();
    check(!union_covered(r),"GS actual primitive coverage refuses a union with an interior hole");
    prepare(GS_TRIANGLE_CLASS,3);GSConfig.Dithering=1;r.m_conf.ps.dither=1;
    r.m_conf.cb_ps.ScaleFactor=GSVector4(1.0f,1.0f,1.5f,0.0f);
    auto& d=r.m_env.DIMX;
    d.DM00=-4;d.DM01=2;d.DM02=-3;d.DM03=3;
    d.DM10=0;d.DM11=-2;d.DM12=1;d.DM13=-1;
    d.DM20=-3;d.DM21=3;d.DM22=-4;d.DM23=2;
    d.DM30=1;d.DM31=-1;d.DM32=0;d.DM33=-2;
    r.EmulateDither();
    const auto phase=[]<class T>(const T& cb)->u32 {
        if constexpr(requires {cb.DitherPhase;}) return cb.DitherPhase;
        return 0;
    };
    check(phase(r.m_conf.cb_ps)!=0,"GS actual fractional-scale dither chooses the render target's native matrix phase");
    check(static_cast<CoherenceDevice*>(g_gs_device.get())->unexpected_calls == 0,
          "GS geometry fixtures issue no GPU operations");
}
