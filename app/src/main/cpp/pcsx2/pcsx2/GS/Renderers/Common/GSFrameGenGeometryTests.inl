// SPDX-License-Identifier: GPL-3.0+
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
void GSRenderer::TestFrameGenGeometry(void(*check)(bool,const char*))
{
#ifdef GS_FRAMEGEN_GEOMETRY_PUBLISH_SUPPORTED
    const bool isolated=!g_gs_renderer&&!g_gs_device;check(isolated,"GS frame geometry fixture requires isolated globals");if(!isolated)return;
    class Device final:public PresentationDevice{public:bool Plain()const{return m_present_geometry.plain;}bool Linear()const{return m_present_geometry.linear;}GSVector4i Source()const{return m_present_geometry.src_rect;}GSVector4 Destination()const{return m_present_geometry.draw_rect;}};
    const auto settings=GSConfig;GSConfig=Pcsx2Config::GSOptions();g_gs_device=std::make_unique<Device>();auto& d=*static_cast<Device*>(g_gs_device.get());
    ScopedGuard restore=[&](){g_gs_device->Destroy();g_gs_device.reset();GSConfig=settings;};
    GSConfig.Upscaler=GSUpscaler::Off;GSConfig.SGSRMode=GSSGSRMode::Disabled;GSConfig.CASMode=GSCASMode::Disabled;GSConfig.TVShader=0;GSConfig.LocalMultiplayerMode=0;GSConfig.LinearPresent=GSPostBilinearMode::BilinearSmooth;
    const GSVector4i src(2,4,62,60);const GSVector4 dst(8,12,632,468);
    auto publish=[&](){NotePresentGeometryForCurrentFrame(src,dst);return d.Plain();};
    check(publish()&&(d.Source()==src).alltrue()&&(d.Destination()==dst).alltrue()&&d.Linear(),"GS actual VSync geometry publisher retains crop, destination and linear plain-blit contract");
    for(auto upscaler:{GSUpscaler::MetalFXSpatial,GSUpscaler::FSR1,GSUpscaler::SGSR,GSUpscaler::SGSREdge}){GSConfig.Upscaler=upscaler;check(!publish(),"GS actual explicit upscaler output uses finished-screen frame-generation input");}GSConfig.Upscaler=GSUpscaler::Off;
    GSConfig.SGSRMode=static_cast<GSSGSRMode>(1);check(!publish(),"GS actual legacy fragment SGSR remains excluded from plain game-image capture");GSConfig.SGSRMode=GSSGSRMode::Disabled;
    GSConfig.CASMode=GSCASMode::SharpenOnly;check(!publish(),"GS actual CAS excludes game-image bypass");GSConfig.CASMode=GSCASMode::Disabled;
    GSConfig.TVShader=1;check(!publish(),"GS actual TV shader excludes game-image bypass");GSConfig.TVShader=0;
    GSConfig.LocalMultiplayerMode=1;check(!publish(),"GS actual local multiplayer split/duplicate excludes single-blit frame generation");GSConfig.LocalMultiplayerMode=0;
    GSConfig.LinearPresent=GSPostBilinearMode::BilinearSharp;check(!publish(),"GS actual sharp resize excludes plain game-image capture");GSConfig.LinearPresent=GSPostBilinearMode::Off;
    check(publish()&&!d.Linear(),"GS actual nearest plain-blit records nearest sampling");
#else
    check(false,"GS actual VSync frame-generation geometry publisher is available");
#endif
}
extern "C" void EmuCoreXOracleFrameGenGeometry(void(*check)(bool,const char*)){GSRenderer::TestFrameGenGeometry(check);}
