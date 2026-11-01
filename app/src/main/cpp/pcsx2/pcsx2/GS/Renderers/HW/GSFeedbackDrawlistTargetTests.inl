// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unreferenced native CPU fixture. No GPU object or synthetic GPU command.
#include "cpuinfo.h"
#include "GSFeedbackDrawlistTarget.h"
extern "C" void EmuCoreXOracleFeedbackDrawlistTargets(void (*check)(bool,const char*))
{
    cpuinfo_initialize();
    const bool isolated=!g_gs_renderer&&!g_texture_cache&&!g_gs_device;
    check(isolated,"GS feedback drawlist scale fixture owns isolated renderer and texture cache without a device");
    if(!isolated)return;
    const auto config=GSConfig;
    GSConfig=Pcsx2Config::GSOptions();
    g_gs_renderer=std::make_unique<GSRendererHW>();
    const ScopedGuard restore([&](){g_gs_renderer.reset();GSConfig=config;});
    GIFRegTEX0 tex0{};tex0.PSM=PSMCT32;tex0.TBW=1;
    // Null textures deliberately make these real cache-target metadata objects
    // non-owning. No backend allocation/recycle occurs on their destruction.
    GSTextureCache::Target rt(tex0,GSTextureCache::RenderTarget,GSVector2i(8,8),1.5f,nullptr);
    tex0.PSM=PSMZ32;
    GSTextureCache::Target ds(tex0,GSTextureCache::DepthStencil,GSVector2i(8,8),2.f,nullptr);
    const auto* depth=GSResolveFeedbackDrawlistTarget(nullptr,&ds);
    check(depth==&ds&&depth&&depth->GetScale()==2.f,
        "GS actual feedback drawlist resolver selects two-times depth target when no RT exists");
    const auto* color=GSResolveFeedbackDrawlistTarget(&rt,&ds);
    check(color==&rt&&color&&color->GetScale()==1.5f&&GSResolveFeedbackDrawlistTarget(nullptr,nullptr)==nullptr,
        "GS feedback drawlist resolver retains RT priority and unreachable no-target control without dereferencing null");
}
