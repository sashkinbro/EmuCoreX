// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native CPU test: exercises the real EmulateTextureShuffleAndFbmask consumer.
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
#include "GSCoherenceTestDevice.h"

void GSRendererHW::TestExactAlphaMask(void (*check)(bool,const char*))
{
    cpuinfo_initialize();
    const bool isolated=!g_gs_renderer&&!g_texture_cache&&!g_gs_device;
    check(isolated,"GS exact alpha mask owns isolated CPU renderer and uncreated backend");
    if(!isolated)return;
    const auto saved=GSConfig;GSConfig=Pcsx2Config::GSOptions();
    GSConfig.AccurateBlendingUnit=AccBlendLevel::Basic;
    GSConfig.LoadTextureReplacements=false;GSConfig.DumpReplaceableTextures=false;
    g_gs_device=std::make_unique<GSDeviceVK>();g_gs_renderer=std::make_unique<GSRendererHW>();
    auto& r=*static_cast<GSRendererHW*>(g_gs_renderer.get());
    const ScopedGuard restore([&](){g_gs_renderer.reset();g_gs_device.reset();GSConfig=saved;});
    GIFRegTEX0 tex{};tex.TBP0=0x1000;tex.TBW=1;tex.PSM=PSMCT32;
    GSTextureCache::Target rt(tex,GSTextureCache::RenderTarget,GSVector2i(64,64),1.f,nullptr);
    rt.m_valid=GSVector4i(0,0,64,64);
    struct Case{const char* name;u8 bits,value,masked,lo,hi;bool scaled;u8 format;bool remove;};
    constexpr Case cases[]={
        {"known-zero-bit7",0x80,0,0x80,32,63,false,PSMCT32,true},
        {"known-one-bit7",0x80,0x80,0x80,128,192,false,PSMCT32,true},
        {"known-low-bits",0x7f,0,0x7f,0,0,false,PSMCT32,true},
        {"known-varying-source-substitution",0x80,0,0x80,64,192,false,PSMCT32,true},
        {"unknown-target",0,0,0x80,0,0,false,PSMCT32,false},
        {"only-some-held-bits-known",0x40,0,0xc0,0,0,false,PSMCT32,false},
        {"scaled-alpha",0xff,0,0x80,0,0,true,PSMCT32,false},
        {"16-bit-target",0xff,0,0x80,0,0,false,PSMCT16,false},
    };
    for(const auto& c:cases)
    {
        r.m_conf={};r.ResetStates();r.m_env={};r.PRIM=&r.m_env.PRIM;r.m_draw_env=&r.m_env;r.m_context=&r.m_env.CTXT[0];
        r.m_cached_ctx={};r.m_cached_ctx.FRAME.PSM=PSMCT32;r.m_cached_ctx.FRAME.FBMSK=static_cast<u32>(c.masked)<<24;
        r.m_context->FRAME=r.m_cached_ctx.FRAME;r.m_context->FBA.FBA=0;
        r.m_env.PRIM.PRIM=GS_TRIANGLELIST;r.m_env.PRIM.ABE=0;r.m_env.PRIM.AA1=0;r.m_env.PRIM.TME=0;
        r.m_texture_shuffle=false;r.m_channel_shuffle=false;
        r.m_vt.m_alpha.min=c.lo;r.m_vt.m_alpha.max=c.hi;r.m_vt.m_alpha.valid=true;
        rt.m_TEX0.PSM=c.format;rt.m_alpha_known={c.bits,c.value};rt.m_alpha_known_via_union=false;rt.m_rt_alpha_scale=c.scaled;
        r.EmulateTextureShuffleAndFbmask(&rt,nullptr);
        // A held substitution also takes the mask off here; after-blend settlement
        // is a separate fixture, since the old renderer has no new helper to call.
        const bool removed=!r.m_conf.ps.fbmask;
        const bool correct=removed==c.remove&&r.m_conf.colormask.wa;
        if(!correct)std::fprintf(stderr,"GS_EXACT_ALPHA case=%s mask=%02x known=%02x/%02x source=%u..%u shaderMask=%u barrier=%u expectedRemoved=%u\n",
            c.name,c.masked,c.bits,c.value,c.lo,c.hi,r.m_conf.ps.fbmask,r.m_conf.require_one_barrier,c.remove);
        check(correct,c.name);
    }
#include "GSExactAlphaPostTests.inl"

}
