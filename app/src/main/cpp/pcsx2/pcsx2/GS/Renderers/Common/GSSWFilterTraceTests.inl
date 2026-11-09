// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "GS/Renderers/Common/GSRenderer.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
namespace {
class SWTraceOracleRenderer final:public GSRenderer {
    void Draw() override {}
    GSTexture* GetOutput(int,float&,int&) override {return nullptr;}
};
template<class T> bool SWTraceCross(const T& trace) {
    if constexpr(requires{trace.IsFilterCrossover();})return trace.IsFilterCrossover();
    return false;
}
template<class T> bool SWTraceLinearOnMag(const T& trace) {
    if constexpr(requires{trace.IsCrossoverLinearOnMag();})return trace.IsCrossoverLinearOnMag();
    return false;
}
}
extern "C" void EmuCoreXOracleSWFilterTrace(void(*check)(bool,const char*)) {
    const bool isolated=!g_gs_renderer&&!g_gs_device;check(isolated,"GS SW filter trace fixture requires isolated renderer globals");if(!isolated)return;
    cpuinfo_initialize();const auto config=GSConfig;const u64 draw=GSState::s_n;
    GSConfig=Pcsx2Config::GSOptions();
    ScopedGuard restore=[&](){GSConfig=config;GSState::s_n=draw;};
    auto state=std::make_unique<SWTraceOracleRenderer>();
    GIFRegPRIM prim{};prim.TME=1;prim.IIP=1;prim.PRIM=GS_TRIANGLELIST;
    GSDrawingContext context{};context.TEX0.TW=5;context.TEX0.TH=5;
    state->PRIM=&prim;state->m_context=&context;
    GSVertexTrace trace(state.get());
    GSVertex vertex[3]{};const u16 index[3]={0,1,2};
    for(int i=0;i<3;i++){vertex[i].XYZ.X=(i==1?32:0)*16;vertex[i].XYZ.Y=(i==2?16:0)*16;vertex[i].RGBAQ.Q=i==0?0.5f:i==1?1.0f:2.0f;vertex[i].RGBAQ.R=128;vertex[i].RGBAQ.A=128;}
    auto update=[&](bool mag,bool min,int mxl,int lcm,int k,BiFiltering mode){
        context.TEX1.MMAG=mag;context.TEX1.MMIN=min?1:0;context.TEX1.MXL=mxl;context.TEX1.LCM=lcm;context.TEX1.K=k;
        GSConfig.TextureFiltering=mode;trace.Update(vertex,index,3,3,GS_TRIANGLE_CLASS);
    };
    update(true,false,1,0,0,BiFiltering::PS2);
    check(SWTraceCross(trace)&&SWTraceLinearOnMag(trace)&&trace.IsLinear()&&trace.IsRealLinear(),"GS actual vertex trace publishes mixed Q filter crossover and magnification-linear polarity");
    update(false,true,1,0,0,BiFiltering::PS2);
    check(SWTraceCross(trace)&&!SWTraceLinearOnMag(trace)&&trace.IsLinear(),"GS actual vertex trace publishes minification-linear polarity without changing broad linear coverage");
    update(true,false,0,0,0,BiFiltering::PS2);
    check(!SWTraceCross(trace)&&trace.IsLinear(),"GS actual vertex trace ignores MMIN crossover at MXLzero");
    update(true,false,1,1,16,BiFiltering::PS2);
    check(!SWTraceCross(trace)&&!trace.IsLinear(),"GS actual vertex trace fixed positive LOD selects minification without crossover");
    update(false,true,1,1,-16,BiFiltering::PS2);
    check(!SWTraceCross(trace)&&!trace.IsLinear(),"GS actual vertex trace fixed negative LOD selects magnification without crossover");
    update(true,false,1,0,0,BiFiltering::Nearest);
    check(!SWTraceCross(trace)&&!trace.IsLinear()&&trace.IsRealLinear(),"GS actual vertex trace nearest override suppresses per-pixel crossover and preserves console-linear fact");
    update(false,true,1,0,0,BiFiltering::Forced);
    check(!SWTraceCross(trace)&&trace.IsLinear(),"GS actual vertex trace forced-linear override suppresses per-pixel crossover");
    update(true,true,1,0,0,BiFiltering::PS2);
    check(!SWTraceCross(trace)&&trace.IsLinear(),"GS actual vertex trace equal filters do not mark an observable crossover");
}
