// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "GSCoherenceTestDevice.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
#if __has_include("GS/Renderers/Common/GSFastStencilShadow.h")
#include "GS/Renderers/Common/GSFastStencilShadow.h"
#define EMUCOREX_HAS_FAST_STENCIL_TEST 1
#endif
void GSRendererHW::TestStencilCounter(void (*check)(bool,const char*))
{
    cpuinfo_initialize();const bool isolated=!g_gs_renderer&&!g_texture_cache&&!g_gs_device;
    check(isolated,"GS stencil counter fixture owns isolated renderer");if(!isolated)return;
    const auto saved=GSConfig;GSConfig=Pcsx2Config::GSOptions();GSConfig.LoadTextureReplacements=GSConfig.DumpReplaceableTextures=false;
    g_gs_device=std::make_unique<CoherenceDevice>();g_gs_renderer=std::make_unique<GSRendererHW>();
    auto& r=*static_cast<GSRendererHW*>(g_gs_renderer.get());auto& device=*static_cast<CoherenceDevice*>(g_gs_device.get());
    const ScopedGuard restore([&](){g_gs_renderer.reset();g_gs_device.reset();GSConfig=saved;});
    CoherenceTexture texture(GSTexture::RenderTarget,64,64,1,GSTexture::Format::Color);
    GIFRegTEX0 tex0{};tex0.TBP0=0x1000;tex0.TBW=1;tex0.PSM=PSMCT32;
    GSTextureCache::Target rt(tex0,GSTextureCache::RenderTarget,GSVector2i(64,64),1.0f,&texture);
    const ScopedGuard detach([&](){rt.m_texture=nullptr;});
    [&]<class Renderer>(Renderer& render) {
        if constexpr(requires{render.SetupStencilCounterDraw(&rt);render.m_conf.stencil_counter.enable;})
        {
            GSVertex vertices[12]{};u16 indices[12]{};
            const auto prepare=[&](){
                render.m_conf={};render.ResetStates();render.m_env.Reset();
                render.PRIM=&render.m_env.PRIM;render.m_draw_env=&render.m_env;render.m_context=&render.m_env.CTXT[0];
                render.m_env.PRIM.PRIM=GS_TRIANGLELIST;render.m_env.PRIM.ABE=1;
                render.m_context->ALPHA.A=0;render.m_context->ALPHA.B=2;render.m_context->ALPHA.C=2;
                render.m_context->ALPHA.D=1;render.m_context->ALPHA.FIX=128;render.m_env.COLCLAMP.CLAMP=0;
                render.m_conf.rt=&texture;render.m_conf.ps.colclip_hw=1;render.m_conf.ps.tfx=4;
                render.m_conf.colclip_mode=GSHWDrawConfig::ColClipMode::ConvertAndResolve;
                render.m_conf.colormask.wrgba=7;render.m_conf.topology=GSHWDrawConfig::Topology::Triangle;
                render.m_conf.indices_per_prim=3;render.m_conf.nindices=12;render.m_conf.nverts=12;
                render.m_conf.verts=vertices;render.m_conf.indices=indices;
                for(u32 i=0;i<12;i++){indices[i]=i;vertices[i].RGBAQ.U32[0]=(i/3)%2?0x80fdfeffu:0x80030201u;}
                device.SetStencilCounterCap(true);
            };
            prepare();const bool accepted=render.SetupStencilCounterDraw(&rt);
            const u16 expected[12]={0,1,2,6,7,8,3,4,5,9,10,11};
            check(accepted&&render.m_conf.stencil_counter.enable&&render.m_conf.stencil_counter.minus_start==6&&
                render.m_conf.stencil_counter.step[0]==1&&render.m_conf.stencil_counter.step[1]==2&&render.m_conf.stencil_counter.step[2]==3&&
                std::memcmp(render.m_conf.indices,expected,sizeof(expected))==0,
                "GS actual counter producer groups signs with exact independent per-channel modulo steps");
            bool refused=true;
            for(int variant=0;variant<8;variant++) {
                prepare();
                if(variant==0)device.SetStencilCounterCap(false);
                if(variant==1)render.m_conf.depth.zwe=1;
                if(variant==2)render.m_env.PRIM.TME=1;
                if(variant==3)render.m_conf.ps.fbmask=1;
                if(variant==4)render.m_conf.ps.scanmsk=2;
                if(variant==5)render.m_conf.colormask.wrgba=8;
                if(variant==6)for(auto& v:vertices)v.RGBAQ.U32[0]=0x80030201u;
                if(variant==7){render.m_conf.colormask.wa=1;vertices[5].RGBAQ.A=127;}
                refused&=!render.SetupStencilCounterDraw(&rt)&&!render.m_conf.stencil_counter.enable;
            }
            check(refused,"GS actual counter producer refuses unsupported devices depth writes textures masks discard empty RGB one sign and varying alpha");
        }
        else check(false,"GS production exact colour-wrap stencil counter consumer is present");
    }(r);
#ifdef EMUCOREX_HAS_FAST_STENCIL_TEST
    bool devices=true;
    for(auto api:{RenderAPI::OpenGL,RenderAPI::Vulkan})for(bool dual:{false,true})
    for(auto road:{GSSelfReadRoad::Copy,GSSelfReadRoad::InPassBarrier,GSSelfReadRoad::InPassOrdered})
    for(bool declared:{false,true})for(bool measured:{false,true})
        devices&=GSFastStencilShadow::DeviceQualifies({api,dual,road,declared,measured})==
            (api==RenderAPI::Vulkan&&dual&&(road==GSSelfReadRoad::Copy||declared||(road==GSSelfReadRoad::InPassBarrier&&measured)));
    check(devices,"GS alpha shadow counter selects only implemented Vulkan dual-output measured self-read roads");
    [&]<class Renderer>(Renderer& render){
        if constexpr(requires{render.IsColClipCounterDraw();})
        {
            render.m_env.Reset();render.PRIM=&render.m_env.PRIM;render.m_context=&render.m_env.CTXT[0];
            auto& a=render.m_context->ALPHA;a.A=0;a.B=2;a.C=2;a.D=1;a.FIX=128;
            render.m_vertex.tail=2;render.m_vertex.buff[0].RGBAQ.U32[0]=0x80010101;
            render.m_vertex.buff[1].RGBAQ.U32[0]=0x80ffffff;
            bool exact=render.IsColClipCounterDraw();render.m_vertex.buff[1].RGBAQ.G=254;
            exact&=!render.IsColClipCounterDraw();render.m_vertex.buff[1].RGBAQ.G=255;
            render.m_env.PRIM.TME=1;exact&=!render.IsColClipCounterDraw();render.m_env.PRIM.TME=0;
            render.m_vertex.buff[1].RGBAQ.U32[0]=0x80010101;exact&=!render.IsColClipCounterDraw();
            check(exact&&!GSConfig.UserHacks_NativeCounterDraws,"GS native-resolution counter is opt-in and classifies exact untextured plus-minus RGB steps");
        }else check(false,"GS native-resolution counter production consumer exists");
    }(r);
#else
    check(false,"GS alpha shadow and optional native-resolution counter consumers exist");
#endif
    check(device.unexpected_calls==0,"GS stencil counter selection submits no GPU work");
}
