// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "cpuinfo.h"
#include "common/ScopedGuard.h"
void GSState::TestCullGrid(void (*check)(bool,const char*))
{
    cpuinfo_initialize();const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;});
    struct State final:GSState {void Draw() override {}};
    struct Case{float scale;GSHalfPixelOffset hpo;int left,right;bool accepted;};
    const Case cases[]={
        {1.0f,GSHalfPixelOffset::Normal,1,8,false},
        {1.0f,GSHalfPixelOffset::Normal,0,8,true},
        {2.0f,GSHalfPixelOffset::Native,5,7,false},
        {2.0f,GSHalfPixelOffset::Native,4,7,true},
        {2.0f,GSHalfPixelOffset::NativeWTexOffset,1,7,false},
        {2.0f,GSHalfPixelOffset::NativeWTexOffset,0,7,true},
        {2.0f,GSHalfPixelOffset::Normal,5,7,true},
        {1.5f,GSHalfPixelOffset::Native,10,11,true},
    };
    for(const auto& c:cases)
    {
        GSConfig=Pcsx2Config::GSOptions();GSConfig.UpscaleMultiplier=c.scale;GSConfig.UserHacks_HalfPixelOffset=c.hpo;
        auto owner=std::make_unique<State>();auto& r=*owner;
        r.m_env.PRIM.U64=0;r.m_env.PRIM.PRIM=GS_TRIANGLELIST;r.PRIM=&r.m_env.PRIM;
        r.m_context=&r.m_env.CTXT[0];r.m_draw_env=&r.m_env;r.m_xyof=GSVector4i::zero();
        r.m_context->XYOFFSET.U64=0;r.m_context->scissor.cull=GSVector4i(0,0,256,256);
        r.m_scissor_invalid=false;r.m_dirty_gs_regs=0;r.m_backed_up_ctx=0;
        r.m_vt.m_primclass=GS_TRIANGLE_CLASS;
        const int xy[3][2]={{c.left,4},{c.right,4},{c.left,32}};
        for(const auto& point:xy)
        {r.m_v={};r.m_v.XYZ.X=point[0];r.m_v.XYZ.Y=point[1];r.VertexKick<GS_TRIANGLELIST,false>(0);}
        const bool accepted=r.m_index.tail==3;
        if(accepted!=c.accepted)std::fprintf(stderr,"GS_CULL_GRID scale=%g hpo=%u span=%d..%d accepted=%u expected=%u\n",c.scale,static_cast<u32>(c.hpo),c.left,c.right,accepted,c.accepted);
        check(accepted==c.accepted,"GS actual vertex kick culls only triangles without a sample on the active device phase grid");
    }
}
