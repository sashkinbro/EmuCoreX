// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Exercises the real GSRenderer::Merge and GSDevice::Interlace consumers.
#include "GSPresentationTestDevice.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
#include "GSFieldShiftLifecycleTests.inl"
namespace {
class PresentationRenderer final : public GSRenderer
{
public:
    PresentationTexture output{GSTexture::RenderTarget,128,192,1,GSTexture::Format::Color};
    float scale=2.0f;
    void Draw() override {}
    float GetUpscaleMultiplier() override { return scale; }
    GSTexture* GetOutput(int,float& out_scale,int& y_offset) override
    { out_scale=scale;y_offset=0;return &output; }
};
}
void GSRenderer::TestPresentationParity(void (*check)(bool,const char*))
{
    const bool isolated=!g_gs_renderer&&!g_gs_device;
    check(isolated,"GS presentation parity requires isolated globals");
    if(!isolated)return;
    cpuinfo_initialize();
    const auto settings=GSConfig;const u64 draw=GSState::s_n;
    GSConfig=Pcsx2Config::GSOptions();GSConfig.PCRTCOffsets=false;
    GSConfig.ShadeBoost=GSConfig.FXAA=GSConfig.ShaderChainEnabled=false;
    g_gs_device=std::make_unique<PresentationDevice>();
    g_gs_renderer=std::make_unique<PresentationRenderer>();
    ScopedGuard restore=[&](){g_gs_renderer.reset();g_gs_device->Destroy();g_gs_device.reset();GSConfig=settings;GSState::s_n=draw;};
    auto& renderer=*static_cast<PresentationRenderer*>(g_gs_renderer.get());
    auto& device=*static_cast<PresentationDevice*>(g_gs_device.get());
    GSPrivRegSet regs{};renderer.SetRegsMem(reinterpret_cast<u8*>(&regs));
    regs.SMODE1.CMOD=2;regs.SYNCV.VFP=1;regs.SMODE2.INT=1;regs.SMODE2.FFMD=1;
    regs.PMODE.EN2=1;regs.PMODE.ALP=255;
    renderer.PCRTCDisplays.interlaced=1;renderer.PCRTCDisplays.FFMD=1;
    renderer.PCRTCDisplays.videomode=static_cast<int>(GSVideoMode::NTSC);
    auto& circuit=renderer.PCRTCDisplays.PCRTCDisplays[1];circuit.enabled=true;
    circuit.displayRect=GSVector4i(0,0,64,64);circuit.framebufferRect=GSVector4i(0,0,64,64);
    circuit.magnification=GSVector2i(1,1);
    GSConfig.InterlaceMode=GSInterlaceMode::Automatic;
    renderer.Merge(1);
    check(device.interlace_shaders.empty(),"GS actual Merge presents integer 2x field-mode whole-picture directly without temporal history");
    check(device.merged_dst[1].y==0.0f&&device.merged_src[1].y<0.0f,
        "GS actual Merge shifts field-direct source with clamp fill and leaves destination top drawn");
    circuit.framebufferRect=GSVector4i(0,8,64,72);device.merged_band[1]={};renderer.Merge(1);
    const auto& band=device.merged_band[1];
    check(band.enabled&&band.dst.y==0.0f&&band.dst.w==2.0f&&band.src.y==band.src.w&&
        band.src.y==(16.5f/192.0f)&&device.merged_dst[1].y==band.dst.w,
        "GS actual Merge fills shifted top band from its own nonzero-DBY circuit texel row without double-blended overlap");
    circuit.framebufferRect=GSVector4i(0,0,64,64);
    // Fractional and native scales must retain actual deinterlacing/history.
    for(float scale:{1.0f,1.5f})
    { renderer.scale=scale;device.interlace_shaders.clear();renderer.Merge(1);
      check(!device.interlace_shaders.empty(),"GS actual Merge retains FastMAD at native and fractional scales");
      check(device.field_pad.x==0.0f&&device.field_pad.y==1.0f,
          "GS actual Merge passes the one whole undrawn row at native and fractional scales to Interlace CB"); }
    renderer.scale=2.0f;GSConfig.InterlaceMode=GSInterlaceMode::AdaptiveTFF;
    device.interlace_shaders.clear();device.BrokenMAD(true);renderer.Merge(1);
    check(device.interlace_shaders.size()==2&&device.interlace_shaders[0]==ShaderInterlace::WEAVE&&device.interlace_shaders[1]==ShaderInterlace::BLEND,
        "GS actual Mali-G57 FastMAD fallback executes weave plus blend");
    check(device.merged_dst[1].y==0.0f,"GS actual Mali-G57 FastMAD fallback suppresses FFMD merge offset");
    device.interlace_shaders.clear();device.BrokenMAD(false);renderer.Merge(1);
    check(device.interlace_shaders.size()==2&&device.interlace_shaders[0]==ShaderInterlace::MAD_BUFFER,
        "GS unaffected actual FastMAD retains bank history");
    renderer.Destroy();
    TestActualFieldShiftLifecycle(check);
}
