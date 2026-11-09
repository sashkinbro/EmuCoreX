// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Real detector, real GSDevice stretch/download seam. GPU completion is injected;
// no reference algorithm is duplicated and any Flush call is observed as a failure.
#if __has_include("GS/Renderers/Common/GSFieldShiftDetector.h")
#include "GS/Renderers/Common/GSFieldShiftDetector.h"
namespace {
struct FieldProbeTrace { bool ready=true,map_ok=true;u32 copies=0,polls=0,maps=0,flushes=0,destroyed=0; };
class FieldProbeDownload final : public GSDownloadTexture {
    FieldProbeTrace& trace;std::vector<u32> pixels;
public:
    FieldProbeDownload(u32 w,u32 h,FieldProbeTrace& t):GSDownloadTexture(w,h,GSTexture::Format::Color),trace(t),pixels(size_t(w)*h){}
    ~FieldProbeDownload() override {++trace.destroyed;}
    void DoCopyFromTexture(const GSVector4i&,GSTexture* source,const GSVector4i&,u32,bool) override {
        pixels=static_cast<PresentationTexture*>(source)->pixels;m_current_pitch=m_width*4;m_needs_flush=true;++trace.copies;
    }
    bool Map(const GSVector4i&) override {++trace.maps;m_map_pointer=reinterpret_cast<const u8*>(pixels.data());return trace.map_ok;}
    void Unmap() override {m_map_pointer=nullptr;}
    bool Poll() override {++trace.polls;if(trace.ready)m_needs_flush=false;return trace.ready;}
    void Flush() override {++trace.flushes;}
#ifdef PCSX2_DEVBUILD
    void SetDebugName(std::string_view) override {}
#endif
};
class FieldProbeDevice final : public PresentationDevice {
public:
    FieldProbeTrace trace;
    std::unique_ptr<GSDownloadTexture> CreateDownloadTexture(u32 w,u32 h,GSTexture::Format) override {
        return std::make_unique<FieldProbeDownload>(w,h,trace);
    }
    void DoStretchRect(GSTexture* source,const GSVector4&,GSTexture* destination,const GSVector4&,ShaderConvertSelector,Filter) override {
        auto& src=*static_cast<PresentationTexture*>(source);auto& dst=*static_cast<PresentationTexture*>(destination);
        for(int y=0;y<dst.GetHeight();++y)for(int x=0;x<dst.GetWidth();++x)
            dst.pixels[size_t(y)*dst.GetWidth()+x]=src.pixels[size_t(y)*src.GetHeight()/dst.GetHeight()*src.GetWidth()+size_t(x)*src.GetWidth()/dst.GetWidth()];
    }
};
}
static void TestActualFieldShiftLifecycle(void(*check)(bool,const char*)) {
    auto saved_device=std::move(g_gs_device);g_gs_device=std::make_unique<FieldProbeDevice>();
    auto& device=*static_cast<FieldProbeDevice*>(g_gs_device.get());
    ScopedGuard restore_device=[&](){g_gs_device->Destroy();g_gs_device=std::move(saved_device);};
    PresentationTexture source(GSTexture::RenderTarget,64,128,1,GSTexture::Format::Color);
    for(int y=0;y<128;++y)for(int x=0;x<64;++x) {
        const u32 v=u32((y*37+x*11)%251);source.pixels[size_t(y)*64+x]=v|(v<<8)|(v<<16)|0xff000000u;
    }
    GSFieldShiftDetector detector;
    detector.Update(&source,{64,128},2,0,0);detector.Update(&source,{64,128},2,0,1);detector.Update(&source,{64,128},2,0,0);
    check(!detector.IsPending()&&!detector.IsProbing()&&!detector.WantsShift(),
        "GS actual detector consumes opposite-field asynchronous signatures and decides no shift for identical structured pictures");
    const auto initial=device.trace.copies;
    for(int i=0;i<599;i++)detector.Update(&source,{64,128},2,0,i&1);
    check(device.trace.copies==initial,"GS actual detector rests 599 fields without another probe copy");
    detector.Update(&source,{64,128},2,0,1);
    check(detector.IsProbing()&&!detector.WantsShift()&&device.trace.copies==initial+1,
        "GS actual detector starts bounded recheck at field600 while retaining prior no-shift decision");
    detector.Reset();check(detector.IsPending()&&detector.IsProbing()&&detector.WantsShift(),
        "GS actual detector reset drops decision signatures and in-flight readback ownership");
    detector.Update(&source,{64,128},2,0,0);detector.Update(&source,{64,128},2,0,1);detector.Update(&source,{64,128},2,0,0);
    detector.Update(&source,{64,96},2,0,1);
    check(detector.IsProbing()&&!detector.WantsShift(),"GS actual detector video-size change starts new round and keeps current decision pending evidence");
    detector.Reset();device.trace.ready=false;
    for(int i=0;i<12;i++)detector.Update(&source,{64,128},2,0,i&1);
    check(!detector.IsProbing()&&detector.WantsShift(),
        "GS actual detector abandons noncompleting readbacks after bounded polling with default shift");
    check(device.trace.flushes==0,"GS actual detector never flushes submits or blocks to collect signatures");
    detector.Reset();device.trace.ready=false;
    for(int i=0;i<5;i++)detector.Update(&source,{64,128},2,0,0);
    device.trace.ready=true;
    for(int i=0;i<30;i++)detector.Update(&source,{64,128},2,0,0);
    check(!detector.IsProbing()&&device.trace.flushes==0,
        "GS actual detector retires delayed completions across full-slot skipped-field gaps and still finishes bounded round");
    detector.Reset();device.trace.ready=true;
    for(int i=0;i<15;i++)detector.Update(&source,{64,128},2,0,0);
    check(!detector.IsProbing()&&detector.WantsShift(),"GS actual detector rejects same-parity replay pairs and resolves finite probe budget to shift");
    detector.Reset();device.trace.map_ok=false;
    detector.Update(&source,{64,128},2,0,0);detector.Update(&source,{64,128},2,0,1);
    check(!detector.IsProbing()&&detector.WantsShift(),"GS actual detector mapping failure releases readbacks and defaults without a stall");
    detector.Reset();check(device.trace.destroyed>0&&device.trace.flushes==0,
        "GS actual detector destruction/reset retires readbacks without flushing GPU work");
}
#else
static void TestActualFieldShiftLifecycle(void(*check)(bool,const char*)) {
    check(false,"GS production field-shift detector lifecycle is available for consumer validation");
}
#endif
