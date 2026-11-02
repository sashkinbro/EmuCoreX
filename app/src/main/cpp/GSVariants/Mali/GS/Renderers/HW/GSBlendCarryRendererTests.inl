// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Actual blend/opaque stage and actual renderer submission policy. CPU profiles
// never reach the backend: the native-only submission capture consumes them.
#include "GS/Renderers/Common/GSPassScheduler.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "cpuinfo.h"
void GSRendererHW::TestBlendCarry(void (*check)(bool,const char*))
{
    cpuinfo_initialize();
    const bool isolated=!g_gs_renderer&&!g_texture_cache&&!g_gs_device;
    check(isolated,"GS blend carry owns isolated CPU renderer and uncreated device");
    if(!isolated)return;
    const auto settings=GSConfig;GSConfig=Pcsx2Config::GSOptions();
    GSConfig.AccurateBlendingUnit=AccBlendLevel::Basic;
    g_gs_device=std::make_unique<GSDeviceVK>();g_gs_renderer=std::make_unique<GSRendererHW>();
    auto& r=*static_cast<GSRendererHW*>(g_gs_renderer.get());
    const ScopedGuard restore([&](){g_gs_renderer.reset();g_gs_device.reset();GSConfig=settings;});
    GSDevice::FeatureSupport features{};features.texture_barrier=true;features.dual_source_blend=true;
    g_gs_device->SetFeaturesForTesting(features);
    class HostTexture final:public GSTexture
    {
    public:
        HostTexture(){m_size=GSVector2i(8,8);m_mipmap_levels=1;m_format=Format::Color;m_usage=RenderTarget;}
        void* GetNativeHandle() const override{return nullptr;}
        bool DoUpdate(const GSVector4i&,const void*,int,int) override{return false;}
        bool DoMap(GSMap&,const GSVector4i*,int) override{return false;}
        void Unmap() override{}
        void GenerateMipmap() override{}
#ifdef PCSX2_DEVBUILD
        void SetDebugName(std::string_view) override{}
#endif
    } color,other;
    GIFRegTEX0 tex0{};tex0.PSM=PSMCT32;tex0.TBW=1;
    GSTextureCache::Target rt(tex0,GSTextureCache::RenderTarget,GSVector2i(8,8),1.f,&color);
    GSTextureCache::Target otherRT(tex0,GSTextureCache::RenderTarget,GSVector2i(8,8),1.f,&other);
    const ScopedGuard detach([&](){r.m_last_rt=nullptr;rt.m_texture=otherRT.m_texture=nullptr;});
    rt.m_valid=otherRT.m_valid=GSVector4i(0,0,8,8);
    const GSVertex vertices[3]{};const u16 indices[3]={0,1,2};
    struct Prepared{GSHWDrawConfig config;bool identity;};
    const auto prepare=[&](u32 minimum,u32 maximum,bool abe=true,bool aa1=false,u32 mask=15){
        r.ResetStates();r.m_conf.colclip_mode=GSHWDrawConfig::ColClipMode::NoModify;
        r.PRIM=&r.m_env.PRIM;r.m_draw_env=&r.m_env;
        r.m_env.PRIM.ABE=abe;r.m_env.PRIM.AA1=aa1;r.m_env.PRIM.TME=false;r.m_env.PRIM.FGE=false;
        r.m_env.COLCLAMP.CLAMP=true;r.m_env.PABE.PABE=false;
        r.m_context->ALPHA.A=0;r.m_context->ALPHA.B=1;r.m_context->ALPHA.C=0;r.m_context->ALPHA.D=1;
        r.m_context->FRAME.PSM=PSMCT32;r.m_context->FRAME.FBMSK=0;r.m_context->FRAME.FBW=1;
        r.m_cached_ctx.FRAME=r.m_context->FRAME;r.m_cached_ctx.TEST=GIFRegTEST{};
        r.m_vt.m_primclass=GS_TRIANGLE_CLASS;r.m_prim_overlap=PRIM_OVERLAP_YES;
        r.m_vt.m_alpha={static_cast<int>(minimum),static_cast<int>(maximum),true};r.m_vt.m_eq.value=0;
        r.m_vt.m_min.c=GSVector4i(16,32,64,minimum);r.m_vt.m_max.c=GSVector4i(192,224,240,maximum);
        r.m_conf.rt=&color;r.m_conf.colormask.wrgba=mask;r.m_conf.depth=GSHWDrawConfig::DepthStencilSelector::NoDepth();
        r.m_conf.ps.tfx=4;r.m_conf.ps.no_color1=true;r.m_conf.topology=GSHWDrawConfig::Topology::Triangle;
        r.m_conf.verts=vertices;r.m_conf.indices=indices;r.m_conf.nverts=r.m_conf.nindices=3;r.m_conf.indices_per_prim=3;
        r.m_conf.drawarea=r.m_conf.scissor=GSVector4i(0,0,8,8);
        DATEOptions date{};bool scale=false;
        const bool identity=r.EmulateBlendingWithIdentity(0,128,date,&rt,false,scale);
        return Prepared{r.m_conf,identity};
    };
    struct Capture{std::vector<GSHWDrawConfig> draws;} capture;
    r.m_blend_carry_capture=[](const GSHWDrawConfig& config,void* context){static_cast<Capture*>(context)->draws.push_back(config);};
    r.m_blend_carry_capture_context=&capture;
    const auto submit=[&](const Prepared& draw,GSTextureCache::Target* target=nullptr){
        r.m_conf=draw.config;r.SubmitDrawPrims(target?target:&rt,nullptr,draw.identity);
    };
    const Prepared donor=prepare(32,96),identity=prepare(128,128);
    const bool donorShape=!donor.identity&&donor.config.blend.enable&&donor.config.ps.blend_mix==1&&
        donor.config.ps.blend_a==0&&donor.config.ps.blend_b==2&&donor.config.ps.blend_c==0&&donor.config.ps.blend_d==2;
    check(donorShape&&identity.identity&&!identity.config.blend.enable,
        "GS actual renderer generates final mix1 B/D2 donor and exact alpha128 opaque identity");
    if(!donorShape)return;
    bool exact=true;
    for(int source:{0,1,127,128,254,255})for(int destination:{0,1,127,128,254,255})
        exact&=((source-destination)*128)/128+destination==source;
    exact&=!prepare(127,127).identity&&!prepare(129,129).identity&&!prepare(128,128,false).identity&&
        !prepare(128,128,true,true).identity&&!prepare(128,128,true,false,8).identity;
    check(exact,"GS actual opaque stage marker and independent integer identity reject alpha127/129 ABE AA1 and RGB mask controls");
    r.InvalidateBlendCarry();capture.draws.clear();
    submit(donor);submit(identity);submit(identity);submit(donor);
    bool adopted=capture.draws.size()==4;
    if(adopted)
    {
        GSHWDrawConfig expected;std::memcpy(&expected,&identity.config,sizeof(expected));
        expected.ps=donor.config.ps;expected.blend=donor.config.blend;
        expected.road=GSDecideHWDrawRoad(expected,features);
        constexpr size_t paddingBegin=offsetof(GSHWDrawConfig,blend_multi_pass)+sizeof(GSHWDrawConfig::BlendMultiPass);
        constexpr size_t paddingEnd=offsetof(GSHWDrawConfig,cb_vs);
        constexpr size_t alignment=alignof(GSHWDrawConfig::VSConstantBuffer);
        static_assert(paddingBegin<paddingEnd&&paddingEnd==((paddingBegin+alignment-1)&~(alignment-1)));
        // Only the compiler's inter-member alignment gap is indeterminate.
        // All declared fields, pointed-to addresses and complete CB bytes remain
        // part of this raw snapshot comparison.
        std::memset(reinterpret_cast<u8*>(&expected)+paddingBegin,0,paddingEnd-paddingBegin);
        for(u32 draw:{1u,2u})
        {
            GSHWDrawConfig actual;std::memcpy(&actual,&capture.draws[draw],sizeof(actual));
            std::memset(reinterpret_cast<u8*>(&actual)+paddingBegin,0,paddingEnd-paddingBegin);
            const auto* a=reinterpret_cast<const u8*>(&actual);
            const auto* e=reinterpret_cast<const u8*>(&expected);
            for(u32 at=0;at<sizeof(expected);++at)if(a[at]!=e[at])
            {
                std::fprintf(stderr,"GS_BLEND_SNAPSHOT draw=%u offset=%u actual=%02x expected=%02x size=%zu ps=%zu vs=%zu blend=%zu road=%zu cbvs=%zu cbps=%zu clip=%zu\n",
                    draw,at,a[at],e[at],sizeof(expected),offsetof(GSHWDrawConfig,ps),offsetof(GSHWDrawConfig,vs),offsetof(GSHWDrawConfig,blend),
                    offsetof(GSHWDrawConfig,road),offsetof(GSHWDrawConfig,cb_vs),offsetof(GSHWDrawConfig,cb_ps),offsetof(GSHWDrawConfig,colclip_mode));
                break;
            }
            adopted&=std::memcmp(&actual,&expected,sizeof(expected))==0;
        }
    }
    check(adopted,"GS actual renderer submission adopts only the preceding compatible donor blend into both identities");
    const auto samePipeline=[](const GSHWDrawConfig& a,const GSHWDrawConfig& b){
        return a.ps==b.ps&&a.vs.key==b.vs.key&&a.blend.key==b.blend.key&&a.colormask.key==b.colormask.key&&a.depth.key==b.depth.key;
    };
    u32 transitions=capture.draws.empty()?0:1;
    for(u32 i=1;i<capture.draws.size();++i)transitions+=!samePipeline(capture.draws[i-1],capture.draws[i]);
    check(transitions==1&&capture.draws.size()==4,
        "GS actual renderer selected pipeline state remains continuous across donor identity identity donor CPU submissions");
    r.InvalidateBlendCarry();capture.draws.clear();submit(donor);
    auto differentTopology=identity;differentTopology.config.topology=GSHWDrawConfig::Topology::Point;
    submit(differentTopology);submit(identity);
    check(capture.draws.size()==3&&!capture.draws[1].blend.enable&&!capture.draws.back().blend.enable,
        "GS actual renderer declines another topology instead of creating an unnecessary carried blend pipeline");
    // Negative inputs use the exact same real submission entry; a refusal must
    // also break the chain so a following identity cannot revive stale state.
    bool rejected=true;u32 negatives=0;
    for(u32 variant=0;variant<21;++variant)
    {
        r.InvalidateBlendCarry();capture.draws.clear();submit(donor);
        auto changed=identity;
        switch(variant)
        {
            case 0:changed.config.ps.dither=1;break;
            case 1:changed.config.ps.fbmask=true;break;
            case 2:changed.config.ps.colclip=true;break;
            case 3:changed.config.ps.colclip_hw=true;break;
            case 4:changed.config.ps.shuffle=true;break;
            case 5:changed.config.ps.color_feedback=true;break;
            case 6:changed.config.ps.depth_feedback=true;break;
            case 7:changed.config.ps.rov_color=true;break;
            case 8:changed.config.alpha_second_pass.enable=true;break;
            case 9:changed.config.blend_multi_pass.enable=true;break;
            case 10:changed.config.tex_hazard=GSHWDrawConfig::TEX_HAZARD_RT;break;
            case 11:changed.config.tex=&color;break;
            case 12:changed.config.raster_order=true;break;
            case 13:changed.config.colormask.wrgba=7;break;
            case 14:changed.config.depth.zwe=true;break;
            case 15:changed.config.vs.key^=1;break;
            case 16:changed.config.rt=&other;break;
            case 17:changed.config.ps.af_in_src1=true;break;
            case 18:changed.config.ps.pabe=true;break;
            case 19:changed.config.destination_alpha=GSHWDrawConfig::DestinationAlphaMode::Full;break;
            case 20:changed.config.ps.round_inv=true;break;
        }
        submit(changed,variant==16?&otherRT:&rt);submit(identity);
        rejected&=capture.draws.size()==3&&capture.draws[1].ps==changed.config.ps&&
            capture.draws[1].blend.key==changed.config.blend.key&&!capture.draws.back().blend.enable;
        ++negatives;
    }
    check(rejected&&negatives==21,"GS actual renderer rejects incompatible final states and never revives the donor after refusal");
    bool lifecycle=true;
    for(u32 seam=0;seam<5;++seam)
    {
        r.InvalidateBlendCarry();capture.draws.clear();submit(donor);
        if(seam==0)r.Reset(true);
        if(seam==1)r.PurgeTextureCache(true,true,true);
        if(seam==2){r.BeginHLEHardwareDraw(&color,nullptr,1.f,nullptr,1.f,GSVector4i(0,0,8,8));r.EndHLEHardwareDraw();}
        if(seam==3){r.m_channel_shuffle_width=1;submit(identity);r.m_channel_shuffle_width=0;r.m_last_rt=nullptr;}
        if(seam==4){GSConfig.CoalesceRenderPasses=true;submit(identity);GSConfig.CoalesceRenderPasses=false;}
        const auto fresh=prepare(128,128);submit(fresh);
        lifecycle&=!capture.draws.empty()&&!capture.draws.back().blend.enable;
    }
    check(lifecycle,"GS actual CPU reset purge HLE held-channel and potentially deferred submission seams clear blend carry");
    r.InvalidateBlendCarry();capture.draws.clear();submit(donor);
    features.dual_source_blend=false;g_gs_device->SetFeaturesForTesting(features);
    submit(identity);
    const bool declined=!capture.draws.back().blend.enable&&capture.draws.back().ps==identity.config.ps;
    features.dual_source_blend=true;g_gs_device->SetFeaturesForTesting(features);submit(identity);
    check(declined&&!capture.draws.back().blend.enable,
        "GS actual renderer drops a retained capable donor when current dual-source support disappears and never revives it");
    features.dual_source_blend=false;g_gs_device->SetFeaturesForTesting(features);
    const auto unsupported=prepare(32,96);r.InvalidateBlendCarry();capture.draws.clear();submit(unsupported);submit(prepare(128,128));
    check(unsupported.config.ps.blend_mix!=1&&!capture.draws.back().blend.enable,
        "GS actual no-dual-source blend fallback never manufactures a carry donor or submits unsupported GPU work");
    std::printf("GS_BLEND_CARRY structural_transitions=%u negative_cases=%u captured_only=1\n",transitions,negatives);
}
extern "C" void EmuCoreXOracleBlendCarry(void (*check)(bool,const char*))
{
    GSRendererHW::TestBlendCarry(check);
}
// Native real-device bridge: physical features are never replaced. The original
// stage outputs form the legacy stream; actual SubmitDrawPrims produces carry.
bool GSRendererHW::PrepareBlendCarryGPU(GSTexture* target,const GSVertex* vertices,const u16* indices,
    GSHWDrawConfig* legacy,GSHWDrawConfig* carried,bool interpolate)
{
    cpuinfo_initialize();
    if(!g_gs_device||g_gs_renderer||g_texture_cache||!target||!g_gs_device->Features().dual_source_blend)return false;
    const auto settings=GSConfig;GSConfig=Pcsx2Config::GSOptions();GSConfig.AccurateBlendingUnit=AccBlendLevel::Basic;
    g_gs_renderer=std::make_unique<GSRendererHW>();
    auto& r=*static_cast<GSRendererHW*>(g_gs_renderer.get());
    ScopedGuard restore=[&](){g_gs_renderer.reset();GSConfig=settings;};
    GIFRegTEX0 tex0{};tex0.PSM=PSMCT32;tex0.TBW=1;
    GSTextureCache::Target rt(tex0,GSTextureCache::RenderTarget,target->GetSize(),1.f,target);
    ScopedGuard detach=[&](){rt.m_texture=nullptr;r.m_last_rt=nullptr;};
    rt.m_valid=target->GetRect();
    struct Capture{GSHWDrawConfig* output;u32 count=0;} capture{carried};
    r.m_blend_carry_capture=[](const GSHWDrawConfig& config,void* context){
        auto& c=*static_cast<Capture*>(context);if(c.count<4)c.output[c.count++]=config;
    };
    r.m_blend_carry_capture_context=&capture;
    bool shapes=true;
    for(u32 draw=0;draw<4;++draw)
    {
        const auto& vertex=vertices[draw*9+3];const u32 alpha=vertex.RGBAQ.A;
        r.ResetStates();r.m_conf.colclip_mode=GSHWDrawConfig::ColClipMode::NoModify;
        r.PRIM=&r.m_env.PRIM;r.m_draw_env=&r.m_env;r.m_env.PRIM.ABE=true;r.m_env.PRIM.IIP=interpolate;
        r.m_env.COLCLAMP.CLAMP=true;r.m_context->ALPHA.A=0;r.m_context->ALPHA.B=1;
        r.m_context->ALPHA.C=0;r.m_context->ALPHA.D=1;r.m_context->FRAME.PSM=PSMCT32;r.m_context->FRAME.FBW=1;
        r.m_cached_ctx.FRAME=r.m_context->FRAME;r.m_cached_ctx.TEST=GIFRegTEST{};
        r.m_vt.m_primclass=GS_TRIANGLE_CLASS;r.m_prim_overlap=PRIM_OVERLAP_YES;
        r.m_vt.m_alpha={alpha==128?128:32,alpha==128?128:96,true};r.m_vt.m_eq.value=0;
        r.m_vt.m_min.c=r.m_vt.m_max.c=GSVector4i(vertex.RGBAQ.R,vertex.RGBAQ.G,vertex.RGBAQ.B,alpha);
        r.m_vt.m_min.c.a=alpha==128?128:32;r.m_vt.m_max.c.a=alpha==128?128:96;
        r.m_conf.rt=target;r.m_conf.colormask.wrgba=15;r.m_conf.depth=GSHWDrawConfig::DepthStencilSelector::NoDepth();
        r.m_conf.ps.tfx=4;r.m_conf.ps.no_color1=true;r.m_conf.vs.iip=interpolate;r.m_conf.ps.iip=interpolate;r.m_conf.topology=GSHWDrawConfig::Topology::Triangle;
        r.m_conf.verts=vertices+draw*9;r.m_conf.indices=indices;r.m_conf.nverts=r.m_conf.nindices=9;
        r.m_conf.indices_per_prim=3;r.m_conf.drawarea=r.m_conf.scissor=target->GetRect();
        r.m_conf.cb_vs.vertex_scale=GSVector2(1.f/32.f,1.f/32.f);
        r.m_conf.cb_vs.vertex_offset=GSVector2(1.f,1.f);r.m_conf.cb_vs.max_depth=0xffffffff;
        DATEOptions date{};bool scale=false;
        const bool identity=r.EmulateBlendingWithIdentity(0,128,date,&rt,false,scale);
        if(identity!=(draw==1||draw==2)||(!identity&&r.m_conf.ps.blend_mix!=1))
            std::fprintf(stderr,"GS_BLEND_GPU_STAGE draw=%u identity=%u mix=%u abcd=%u%u%u%u one=%u full=%u fetch=%u\n",draw,unsigned(identity),r.m_conf.ps.blend_mix,r.m_conf.ps.blend_a,r.m_conf.ps.blend_b,r.m_conf.ps.blend_c,r.m_conf.ps.blend_d,unsigned(r.m_conf.require_one_barrier),unsigned(r.m_conf.require_full_barrier),unsigned(g_gs_device->Features().framebuffer_fetch));
        shapes&=identity==(draw==1||draw==2);
        if(!identity)shapes&=r.m_conf.blend.enable&&r.m_conf.ps.blend_mix==1&&r.m_conf.ps.blend_b==2&&r.m_conf.ps.blend_d==2;
        legacy[draw]=r.m_conf;legacy[draw].road=GSDecideHWDrawRoad(legacy[draw],g_gs_device->Features());
        r.SubmitDrawPrims(&rt,nullptr,identity);
    }
    return shapes&&capture.count==4;
}
extern "C" bool EmuCoreXPrepareBlendCarryGPU(GSTexture* target,const GSVertex* vertices,const u16* indices,
    GSHWDrawConfig* legacy,GSHWDrawConfig* carried,bool interpolate)
{
    return GSRendererHW::PrepareBlendCarryGPU(target,vertices,indices,legacy,carried,interpolate);
}
