// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
void GSRenderer::TestMovieComb(void(*check)(bool,const char*))
{
#ifdef GS_MOVIE_COMB_SUPPORTED
    const bool isolated=!g_gs_renderer&&!g_gs_device;check(isolated,"GS movie comb requires isolated globals");if(!isolated)return;
    cpuinfo_initialize();const auto settings=GSConfig;const u64 draw=GSState::s_n;
    GSConfig=Pcsx2Config::GSOptions();GSConfig.PCRTCOffsets=false;GSConfig.ShadeBoost=GSConfig.FXAA=GSConfig.ShaderChainEnabled=false;
    g_gs_device=std::make_unique<PresentationDevice>();g_gs_renderer=std::make_unique<PresentationRenderer>();
    ScopedGuard restore=[&](){g_gs_renderer.reset();g_gs_device->Destroy();g_gs_device.reset();GSConfig=settings;GSState::s_n=draw;};
    auto& r=*static_cast<PresentationRenderer*>(g_gs_renderer.get());auto& d=*static_cast<PresentationDevice*>(g_gs_device.get());
    GSPrivRegSet regs{};r.SetRegsMem(reinterpret_cast<u8*>(&regs));regs.SMODE1.CMOD=2;regs.SYNCV.VFP=1;regs.SMODE2.INT=1;regs.SMODE2.FFMD=0;regs.PMODE.EN2=1;
    r.PCRTCDisplays.interlaced=1;r.PCRTCDisplays.videomode=0;auto& circuit=r.PCRTCDisplays.PCRTCDisplays[1];circuit.enabled=true;
    circuit.displayRect=GSVector4i(0,0,64,64);circuit.framebufferRect=GSVector4i(0,0,64,64);circuit.magnification=GSVector2i(1,1);
    GSConfig.InterlaceMode=GSInterlaceMode::Automatic;GSNoteVideoDecode();r.Merge(0);
    check(d.interlace_shaders.empty(),"GS actual movie comb defaults off even while IPU decode notifications arrive");
    GSConfig.MovieCombDeinterlace=true;GSNoteVideoDecode();d.interlace_shaders.clear();r.Merge(0);
    check(d.interlace_shaders.size()==1&&d.interlace_shaders[0]==ShaderInterlace::COMB&&d.field_pad.z==2.0f,
        "GS actual IPU notification plus automatic frame-mode merge selects native-line comb pass");
    bool active=true;for(int i=0;i<29;i++){d.interlace_shaders.clear();r.Merge(0);active&=d.interlace_shaders.size()==1&&d.interlace_shaders[0]==ShaderInterlace::COMB;}
    d.interlace_shaders.clear();r.Merge(0);check(active&&d.interlace_shaders.empty(),"GS actual comb stays active30merges after decode then expires to progressive passthrough");
    GSNoteVideoDecode();GSConfig.InterlaceMode=GSInterlaceMode::BobTFF;d.interlace_shaders.clear();r.Merge(0);
    check(d.interlace_shaders.size()==1&&d.interlace_shaders[0]==ShaderInterlace::BOB,"GS actual explicit interlace mode outranks movie comb");
    GSConfig.InterlaceMode=GSInterlaceMode::Automatic;regs.SMODE2.FFMD=1;d.interlace_shaders.clear();r.Merge(0);
    check(d.interlace_shaders.empty(),"GS actual integer-upscaled field-direct route outranks movie comb");
    r.Destroy();
#else
    check(false,"GS production IPU notification movie-comb merge lifecycle consumers are available");
#endif
}
extern "C" void EmuCoreXOracleMovieComb(void(*check)(bool,const char*)) { GSRenderer::TestMovieComb(check); }
