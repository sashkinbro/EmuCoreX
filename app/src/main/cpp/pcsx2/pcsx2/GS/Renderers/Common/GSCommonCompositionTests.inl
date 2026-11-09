// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Executes actual Merge/output/crop/pool consumers. No real GPU or app cache required.
#if __has_include("GSPresentCapPolicy.h")
#include "GSPresentCapPolicy.h"
#endif
namespace {
class CompositionDevice final : public PresentationDevice
{
public:
    u32 merges=0, boosts=0, fxaas=0, clear_colour=0;
    CompositionDevice() { m_window_info.surface_width=640;m_window_info.surface_height=480; }
    void DoMerge(GSTexture* t[],GSVector4* s,GSTexture* target,GSVector4* d,const MergeTopBand* bands,
        const GSRegPMODE& p,const GSRegEXTBUF& e,u32 c,Filter f) override
    {
        ++merges;clear_colour=c;
        PresentationDevice::DoMerge(t,s,target,d,bands,p,e,c,f);
        auto* out=static_cast<PresentationTexture*>(target);
        std::fill(out->pixels.begin(),out->pixels.end(),c);
    }
    void DoFXAA(GSTexture*,GSTexture*) override { ++fxaas; }
    void DoShadeBoost(GSTexture*,GSTexture*,const float[4]) override { ++boosts; }
    u32 Frame()const{return m_frame;}
    size_t Pooled()const{return GetPoolMemoryUsage()>0?1:0;}
    void ExpirePooled(){m_frame+=1000;}
};
class CompositionRenderer final : public GSRenderer
{
public:
    PresentationTexture output{GSTexture::RenderTarget,128,128,1,GSTexture::Format::Color};
    float scale=1;bool missing=false;u32 resolves=0;
    void Draw()override{}
    float GetUpscaleMultiplier()override{return scale;}
    GSTexture* GetOutput(int,float& s,int& y)override{++resolves;s=scale;y=0;return missing?nullptr:&output;}
};
template<class Options> void CheckCompositionTrim(Options& o,void(*check)(bool,const char*))
{
    PresentationTexture t(GSTexture::RenderTarget,64,64,1,GSTexture::Format::Color);
    o.UpscaleMultiplier=2;std::fill(std::begin(o.Crop),std::end(o.Crop),0);
    if constexpr(requires{ o.TrimUpscaledEdges; })
    {
        check(o.TrimUpscaledEdges,"GS upscaled edge trim defaults on");
        check((CalculateDrawSrcRect(&t,GSVector2i(64,64))==GSVector4i(2,2,62,62)).alltrue(),"GS actual crop trims one native pixel on all four upscaled edges");
        o.Crop[0]=1;o.Crop[1]=2;o.Crop[2]=3;o.Crop[3]=4;
        check((CalculateDrawSrcRect(&t,GSVector2i(64,64))==GSVector4i(4,6,56,54)).alltrue(),"GS actual trim adds to user crop at current scale");
        o.TrimUpscaledEdges=false;
        check((CalculateDrawSrcRect(&t,GSVector2i(64,64))==GSVector4i(2,4,58,56)).alltrue(),"GS actual edge trim remains user-disableable");
        o.TrimUpscaledEdges=true;o.UpscaleMultiplier=1;std::fill(std::begin(o.Crop),std::end(o.Crop),0);
        check((CalculateDrawSrcRect(&t,GSVector2i(0,0))==GSVector4i(0,0,64,64)).alltrue(),"GS actual native-resolution crop retains zero-real-size safe fallback without trimming");
    }
    else check(false,"GS actual upscaled-edge crop and setting consumers are available");
    o.UpscaleMultiplier=1;std::fill(std::begin(o.Crop),std::end(o.Crop),0);
}
template<class Device> void CheckCompositionPool(Device& d,void(*check)(bool,const char*))
{
    if constexpr(requires{d.AgePoolAfterPresentCapSkip();})
    {
        d.PurgePool();auto* t=d.CreateRenderTarget(7,9,GSTexture::Format::Color,false);d.Recycle(t);d.ExpirePooled();
        const u32 before=d.Frame();
        for(int i=0;i<3;i++)d.AgePoolAfterPresentCapSkip();
        check(d.Frame()==before+3&&d.Pooled()==1,"GS cap skip advances each frame but defers aged-pool scan for at most three skips");
        d.AgePoolAfterPresentCapSkip();check(d.Frame()==before+4&&d.Pooled()==0,"GS fourth cap skip performs bounded real texture cleanup");
        d.AgePoolAfterPresentCapSkip();d.PurgePool();t=d.CreateRenderTarget(7,9,GSTexture::Format::Color,false);d.Recycle(t);d.ExpirePooled();
        for(int i=0;i<3;i++)d.AgePoolAfterPresentCapSkip();
        check(d.Pooled()==1,"GS real PurgePool resets cap cleanup phase");d.AgePoolAfterPresentCapSkip();check(d.Pooled()==0,"GS pool cleanup runs by fourth skip after purge reset");
    }
    else check(false,"GS actual present-cap pool aging consumer is available");
}
}
void GSRenderer::TestCommonComposition(void(*check)(bool,const char*))
{
    const bool isolated=!g_gs_renderer&&!g_gs_device;check(isolated,"GS Common composition requires isolated globals");if(!isolated)return;
    cpuinfo_initialize();const auto settings=GSConfig;const u64 draw=GSState::s_n;
    GSConfig=Pcsx2Config::GSOptions();GSConfig.PCRTCOffsets=false;GSConfig.ShadeBoost=GSConfig.FXAA=GSConfig.ShaderChainEnabled=false;
    g_gs_device=std::make_unique<CompositionDevice>();g_gs_renderer=std::make_unique<CompositionRenderer>();
    ScopedGuard restore=[&](){g_gs_renderer.reset();g_gs_device->Destroy();g_gs_device.reset();GSConfig=settings;GSState::s_n=draw;};
    auto& r=*static_cast<CompositionRenderer*>(g_gs_renderer.get());auto& d=*static_cast<CompositionDevice*>(g_gs_device.get());
    GSPrivRegSet regs{};r.SetRegsMem(reinterpret_cast<u8*>(&regs));regs.SMODE1.CMOD=2;regs.SYNCV.VFP=0;
    regs.PMODE.EN2=1;regs.PMODE.ALP=0xaa;regs.BGCOLOR.U32[0]=0x00332211;
    r.PCRTCDisplays.videomode=static_cast<int>(GSVideoMode::NTSC);
    auto& circuit=r.PCRTCDisplays.PCRTCDisplays[1];circuit.enabled=true;circuit.displayRect=GSVector4i(0,0,64,64);circuit.framebufferRect=GSVector4i(0,0,64,64);circuit.magnification=GSVector2i(1,1);
    r.missing=true;check(!r.Merge(0),"GS actual enabled circuit with unavailable output remains blank");
    circuit.enabled=false;regs.PMODE.EN2=0;r.missing=false;
    const u32 calls=d.merges;const bool bg=r.Merge(0);
    check(bg&&d.merges==calls+1&&d.clear_colour==0xaa332211&&d.GetCurrent()!=nullptr,"GS actual Merge outputs BGCOLOR plus PMODE alpha with both circuits disabled");
    if(bg&&d.GetCurrent()) {const auto& p=static_cast<PresentationTexture*>(d.GetCurrent())->pixels;check(!p.empty()&&std::all_of(p.begin(),p.end(),[](u32 v){return v==0xaa332211;}),"GS actual Common merge delivers BGCOLOR across every composition pixel");}
    circuit.enabled=true;regs.PMODE.EN2=1;regs.PMODE.ALP=255;r.Merge(0);
    CheckCompositionTrim(GSConfig,check);
#ifdef GS_PRESENT_CAP_MERGE_MODES
    const bool old_opt=GSGetPresentCapRenderSkip();ScopedGuard reset_opt=[&](){GSSetPresentCapRenderSkip(old_opt);};
    GSSetPresentCapRenderSkip(false);check(r.SelectMergeModeForVSync(true)==MergeMode::Full,"GS actual cap render-skip defaults to host opt-in");
    check(!r.isReallyInterlaced(),"GS fixture progressive branch uses actual CMOD/VFP field predicate");
    GSSetPresentCapRenderSkip(true);check(r.SelectMergeModeForVSync(false)==MergeMode::Full,"GS actual manual and duplicate skips retain full composition");
    check(r.SelectMergeModeForVSync(true)==MergeMode::SkipFinalComposition,"GS actual progressive cap skip selects final-composition bypass");
    regs.EXTWRITE.WRITE=1;check(r.SelectMergeModeForVSync(true)==MergeMode::Full,"GS actual feedback write prevents composition bypass");regs.EXTWRITE.WRITE=0;
    r.m_snapshot="fixture";check(r.SelectMergeModeForVSync(true)==MergeMode::Full,"GS actual snapshot request prevents composition bypass");r.m_snapshot.clear();
#ifndef EMUCOREX_DISABLE_GS_DIAGNOSTICS
    r.m_dump_frames=1;check(r.SelectMergeModeForVSync(true)==MergeMode::Full,"GS actual dump request prevents composition bypass");r.m_dump_frames=0;
#endif
    r.m_scanmask_used=2;const u32 merges=d.merges,resolves=r.resolves;const u64 n=s_n;auto* current=d.GetCurrent();
    check(r.Merge<MergeMode::SkipFinalComposition>(0)&&d.merges==merges&&r.resolves>resolves&&s_n==n+1&&r.m_scanmask_used==1&&d.GetCurrent()==current,"GS actual progressive bypass still resolves output and ages scanmask without replacing current composition");
    r.missing=true;check(!r.Merge<MergeMode::SkipFinalComposition>(0),"GS actual progressive bypass still rejects missing enabled output");r.missing=false;
    regs.SYNCV.VFP=1;regs.SMODE2.INT=1;regs.SMODE2.FFMD=1;r.PCRTCDisplays.interlaced=1;r.PCRTCDisplays.FFMD=1;r.m_scanmask_used=0;
    check(r.isReallyInterlaced(),"GS fixture interlaced branch restores actual CMOD/VFP field predicate");
    check(r.SelectMergeModeForVSync(true)==MergeMode::InterlaceHistoryOnly,"GS actual interlaced cap skip retains history update");
    GSConfig.ShadeBoost=GSConfig.FXAA=true;d.interlace_shaders.clear();const u32 b=d.boosts,f=d.fxaas,m=d.merges;
    r.Merge<MergeMode::InterlaceHistoryOnly>(1);
    check(d.merges==m+1&&!d.interlace_shaders.empty()&&d.boosts==b&&d.fxaas==f,"GS actual history-only merge updates deinterlace banks while omitting optional postprocessing");
    r.Merge(0);check(d.boosts==b+1&&d.fxaas==f+1,"GS actual full merge retains both visual postprocess consumers");
#else
    check(false,"GS actual VSync cap guards and specialized merge consumers are available");
#endif
    CheckCompositionPool(d,check);r.Destroy();
}
extern "C" void EmuCoreXOracleCommonComposition(void(*check)(bool,const char*)){GSRenderer::TestCommonComposition(check);}
