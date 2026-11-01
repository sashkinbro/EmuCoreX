// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native actual-renderer stage fixture. Capabilities are CPU models;
// an uncreated device and equal-sized host textures never submit GPU work.
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "cpuinfo.h"
void GSRendererHW::TestFeedbackBlendStages(void (*check)(bool,const char*))
{
    cpuinfo_initialize();
    const bool isolated=!g_gs_renderer&&!g_texture_cache&&!g_gs_device;
    check(isolated,"GS feedback blend-stage proof owns isolated CPU renderer cache and uncreated device");
    if(!isolated)return;
    const auto settings=GSConfig;GSConfig=Pcsx2Config::GSOptions();
    GSConfig.AccurateBlendingUnit=AccBlendLevel::Basic;GSConfig.HWAccurateAlphaTest=true;
    g_gs_device=std::make_unique<GSDeviceVK>();
    g_gs_renderer=std::make_unique<GSRendererHW>();
    auto& r=*static_cast<GSRendererHW*>(g_gs_renderer.get());
    const ScopedGuard restore([&](){g_gs_device->SetColorClipTexture(nullptr);g_gs_renderer.reset();g_gs_device.reset();GSConfig=settings;});
    class HostTexture final:public GSTexture
    {
    public:
        HostTexture(Format format,Usage usage){m_size=GSVector2i(8,8);m_mipmap_levels=1;m_format=format;m_usage=usage;}
        void* GetNativeHandle() const override{return nullptr;}
        bool DoUpdate(const GSVector4i&,const void*,int,int) override{return false;}
        bool DoMap(GSMap&,const GSVector4i*,int) override{return false;}
        void Unmap() override{}
        void GenerateMipmap() override{}
#ifdef PCSX2_DEVBUILD
        void SetDebugName(std::string_view) override{}
#endif
    } color(GSTexture::Format::Color,GSTexture::RenderTarget),
      depth(GSTexture::Format::DepthStencil,GSTexture::DepthStencil),
      clip(GSTexture::Format::ColorClip,GSTexture::RenderTarget);
    GIFRegTEX0 tex0{};tex0.PSM=PSMCT32;tex0.TBW=1;
    GSTextureCache::Target rt(tex0,GSTextureCache::RenderTarget,GSVector2i(8,8),1.f,&color);
    const ScopedGuard detach([&](){g_gs_device->SetColorClipTexture(nullptr);rt.m_texture=nullptr;});
    rt.m_valid=GSVector4i(0,0,8,8);
    GSDevice::FeatureSupport features{};
    bool legalBlend=true,legalClip=true,legalMultipass=true,noAlphaOnly=true;
    u32 cases=0,hardwareClip=0,softwareClip=0,multipass=0,feedback=0;
    for(bool native:{false,true})for(bool dual:{false,true})
    for(bool existing:{false,true})for(bool full:{false,true})
    for(bool alphaFeedback:{false,true})for(bool clamp:{false,true})for(bool overlap:{false,true})
    for(u32 equation=0;equation<81;++equation)
    {
        features={};features.texture_barrier=native;features.multidraw_fb_copy=!native;
        features.framebuffer_fetch=true;features.framebuffer_fetch_orders_overlap=true;
        features.dual_source_blend=dual;features.depth_feedback=GSDevice::DepthFeedbackSupport::DepthAsRT;
        g_gs_device->SetFeaturesForTesting(features);
        r.ResetStates();r.m_conf.colclip_mode=GSHWDrawConfig::ColClipMode::NoModify;
        r.m_draw_env=&r.m_env;r.m_env.COLCLAMP.CLAMP=clamp;r.m_env.PABE.PABE=false;
        r.PRIM=&r.m_env.PRIM;r.m_env.PRIM.ABE=true;r.m_env.PRIM.AA1=false;r.m_env.PRIM.TME=false;r.m_env.PRIM.FGE=false;
        r.m_context->FRAME.PSM=PSMCT32;r.m_context->FRAME.FBW=1;r.m_context->FRAME.FBMSK=0;
        r.m_cached_ctx.FRAME=r.m_context->FRAME;
        r.m_context->ALPHA.A=equation/27;r.m_context->ALPHA.B=(equation/9)%3;
        r.m_context->ALPHA.C=(equation/3)%3;r.m_context->ALPHA.D=equation%3;r.m_context->ALPHA.FIX=192;
        r.m_cached_ctx.TEST=GIFRegTEST{};r.m_cached_ctx.TEST.ZTE=true;r.m_cached_ctx.TEST.ZTST=ZTST_GREATER;
        r.m_cached_ctx.TEST.ATE=alphaFeedback;r.m_cached_ctx.TEST.ATST=ATST_LESS;
        r.m_cached_ctx.TEST.AFAIL=AFAIL_FB_ONLY;r.m_cached_ctx.TEST.AREF=128;
        r.m_cached_ctx.ZBUF.ZMSK=false;r.m_cached_ctx.ZBUF.PSM=PSMZ32;
        r.m_vt.m_primclass=GS_TRIANGLE_CLASS;r.m_prim_overlap=overlap?PRIM_OVERLAP_YES:PRIM_OVERLAP_NO;
        r.m_vt.m_alpha={32,192,true};r.m_vt.m_eq.value=0;
        r.m_vt.m_min.c=GSVector4i(16,32,64,32);r.m_vt.m_max.c=GSVector4i(192,224,240,192);
        r.m_conf.rt=&color;r.m_conf.ds=&depth;r.m_conf.colormask.wrgba=15;
        r.m_conf.depth.ztst=ZTST_GREATER;r.m_conf.depth.zwe=true;r.m_conf.ps.no_color1=true;
        r.m_conf.require_full_barrier=full;r.m_r=GSVector4i(0,0,8,8);
        g_gs_device->SetColorClipTexture(existing?&clip:nullptr);
        DATEOptions date{};
        r.EmulateAlphaTest(date);
        bool scale=false;r.EmulateBlending(0,255,date,&rt,false,scale);
        const bool safe=(!r.m_conf.require_one_barrier&&!r.m_conf.require_full_barrier)||!r.m_conf.blend.enable;
        if(!safe&&legalBlend)std::fprintf(stderr,"GS_CLIP_BLEND equation=%u native=%u dual=%u existing=%u full=%u alpha=%u clamp=%u overlap=%u\n",equation,native,dual,existing,full,alphaFeedback,clamp,overlap);
        legalBlend&=safe;
        if(safe)GSApplyPlannedFeedbackBarriers(r.m_conf,features,RenderAPI::OpenGL,overlap,
            GSNeedsDirectDepthFeedbackBarriers(features,r.m_conf.ps.IsFeedbackLoopDepth()),false);
        const bool clipSafe=!r.m_conf.require_full_barrier||!r.m_conf.ps.colclip_hw;
        if(!clipSafe&&legalClip)std::fprintf(stderr,"GS_CLIP_FULL equation=%u native=%u dual=%u existing=%u full=%u alpha=%u clamp=%u overlap=%u\n",equation,native,dual,existing,full,alphaFeedback,clamp,overlap);
        legalClip&=clipSafe;legalMultipass&=!r.m_conf.require_full_barrier||!r.m_conf.blend_multi_pass.enable;
        hardwareClip+=r.m_conf.ps.colclip_hw;softwareClip+=r.m_conf.ps.colclip;
        multipass+=r.m_conf.blend_multi_pass.enable;feedback+=r.m_conf.ps.IsFeedbackLoopDepth();
        const bool primaryDepth=r.m_conf.ps.IsFeedbackLoopDepth();
        r.EmulateAlphaTestSecondPass();
        noAlphaOnly&=primaryDepth||!r.m_conf.alpha_second_pass.enable||!r.m_conf.alpha_second_pass.ps.IsFeedbackLoopDepth();
        ++cases;
    }
    check(legalBlend&&cases==10368,"GS actual alpha and blend stages retain framebuffer-fetch no-hardware-blend barrier precondition");
    check(legalClip&&hardwareClip>0&&softwareClip>0,"GS actual retained and fresh ColorClip stages satisfy full-barrier invariant without weakening assert");
    check(legalMultipass&&multipass>0,"GS actual generated blend multipass stages preserve the full-barrier exclusion");
    check(noAlphaOnly&&feedback>0,"GS actual alpha-second-pass generation does not introduce an unprepared alpha-only depth reader");
    std::printf("GS_CLIP_STAGES cases=%u hardwareClip=%u softwareClip=%u multipass=%u depthFeedback=%u\n",cases,hardwareClip,softwareClip,multipass,feedback);
}
extern "C" void EmuCoreXOracleFeedbackBlendStages(void (*check)(bool,const char*))
{
    GSRendererHW::TestFeedbackBlendStages(check);
}
