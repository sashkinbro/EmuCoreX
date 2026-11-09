// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "common/ScopedGuard.h"
#if __has_include("GS/Renderers/Vulkan/FrameGen/FrameGen.h")
#include "GSPresentationTestDevice.h"
#endif
void EmuCoreXOraclePresentUpscalers(void(*check)(bool,const char*))
{
#if !__has_include("GS/Renderers/Vulkan/FrameGen/FrameGen.h")
    check(false,"GS production SGSR/SGSREdge and FSR1 presentation compute consumers are available");
#else
    struct Device final:PresentationDevice
    {
        std::array<u32,NUM_SGSR_CONSTANTS> sgsr{};std::array<u32,NUM_FSR1_CONSTANTS> easu{},rcas{};
        GSTexture* first=nullptr;GSTexture* second=nullptr;u32 passes=0;bool fail=false,edge=false;
        bool DoSGSR(GSTexture*,GSTexture* dst,const std::array<u32,NUM_SGSR_CONSTANTS>& c,bool e)override
        {sgsr=c;first=dst;edge=e;++passes;return !fail;}
        bool DoFSR1EASU(GSTexture*,GSTexture* dst,const std::array<u32,NUM_FSR1_CONSTANTS>& c)override
        {easu=c;first=dst;++passes;return !fail;}
        bool DoFSR1RCAS(GSTexture* src,GSTexture* dst,const std::array<u32,NUM_FSR1_CONSTANTS>& c)override
        {rcas=c;second=dst;++passes;return src==first&&!fail;}
    } device;
    const auto settings=GSConfig;const ScopedGuard restore([&](){GSConfig=settings;device.Destroy();});
    GSConfig.SGSR_Sharpness=150;PresentationTexture input(GSTexture::RenderTarget,32,16,1,GSTexture::Format::Color);
    GSTexture* texture=&input;GSVector4i rect(4,2,20,10);GSVector4 uv(0.125f,0.125f,0.625f,0.625f);
    const GSVector4 destination(0,0,64,32);device.SGSRUpscale(texture,rect,uv,destination,true);
    const auto as_float=[](u32 v){return std::bit_cast<float>(v);};
    check(device.passes==1&&device.edge&&texture==device.first&&rect.eq(GSVector4i(0,0,64,32)),
        "GS SGSR actual consumer selects directional pass and rewrites the presented texture/region");
    check(device.sgsr[0]==64&&device.sgsr[1]==32&&as_float(device.sgsr[2])==0.125f&&
        as_float(device.sgsr[4])==0.5f&&as_float(device.sgsr[10])==1.5f,
        "GS SGSR actual consumer pushes cropped source coordinates and independent sharpness");
    const auto allocations=device.allocations;texture=&input;rect=GSVector4i(4,2,20,10);
    device.SGSRUpscale(texture,rect,uv,destination,false);
    check(device.allocations==allocations&&!device.edge,"GS SGSR retains its output allocation across the two compute modes");
    device.passes=0;texture=&input;rect=GSVector4i(4,2,20,10);device.FSR1Upscale(texture,rect,uv,destination);
    check(device.passes==2&&device.first!=device.second&&texture==device.second,
        "GS FSR1 actual consumer orders EASU and RCAS over separate texture owners");
    device.fail=true;texture=&input;rect=GSVector4i(4,2,20,10);device.SGSRUpscale(texture,rect,uv,destination,false);
    check(texture==&input&&rect.eq(GSVector4i(4,2,20,10)),"GS failed upscaler leaves the original presentation source intact");
#endif
}
