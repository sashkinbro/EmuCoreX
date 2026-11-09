// SPDX-License-Identifier: GPL-3.0+
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// Bounded geometry/settings consumers: no GSState, renderer, GPU or wrapped allocations.
#include "common/MemorySettingsInterface.h"
#include "common/SettingsWrapper.h"
namespace {
class GeometryDevice final:public PresentationDevice
{
public:
    unsigned surface_rotation=0;
    std::vector<GSVector4> presented;
    GeometryDevice(){Size(640,480);}
    void Size(u32 w,u32 h){m_window_info.surface_width=w;m_window_info.surface_height=h;}
#ifdef GS_PRESENT_GEOMETRY_PARITY
    DisplayRotation GetSurfacePreRotation()const override{return static_cast<DisplayRotation>(surface_rotation);}
#endif
    void PresentRect(GSTexture*,const GSVector4&,GSTexture*,const GSVector4& d,PresentShader,float,Filter)override{presented.push_back(d);}
};
}
void GSRenderer::TestPresentationGeometry(void(*check)(bool,const char*))
{
    const bool isolated=!g_gs_renderer&&!g_gs_device;check(isolated,"GS geometry fixture requires isolated globals");if(!isolated)return;
    const auto gs=GSConfig;const auto aspect=EmuConfig.CurrentAspectRatio;const float custom=EmuConfig.CurrentCustomAspectRatio;
    GSConfig=Pcsx2Config::GSOptions();GSConfig.IntegerScaling=false;EmuConfig.CurrentAspectRatio=AspectRatioType::R4_3;EmuConfig.CurrentCustomAspectRatio=0;
    g_gs_device=std::make_unique<GeometryDevice>();auto& device=*static_cast<GeometryDevice*>(g_gs_device.get());
    ScopedGuard restore=[&](){g_gs_device->Destroy();g_gs_device.reset();GSConfig=gs;EmuConfig.CurrentAspectRatio=aspect;EmuConfig.CurrentCustomAspectRatio=custom;};
    const GSVector4i src(0,0,640,448);const GSVector2i size(640,448);
    const auto portrait=CalculateDrawDstRect(600,1200,src,size,GSDisplayAlignment::Center,false,false);
    check(portrait.y==0&&portrait.w==450,"GS actual default portrait composition aligns picture to top for touch-control room");
    GSConfig.AspectRatio=static_cast<AspectRatioType>(5);EmuConfig.CurrentAspectRatio=static_cast<AspectRatioType>(5);
    check(std::abs(GetCurrentAspectRatioFloat(false)-21.0f/9.0f)<0.00001f,"GS actual appended aspect5 selects21:9 without changing legacy aspect indices");
#ifdef GS_PRESENT_GEOMETRY_PARITY
    GSConfig.AspectRatio=AspectRatioType::R4_3;EmuConfig.CurrentAspectRatio=AspectRatioType::R4_3;
    GSConfig.PortraitRenderTopInset=36;
    auto rect=CalculateDrawDstRect(600,1200,src,size,GSDisplayAlignment::Center,false,false);
    check(rect.y==36&&rect.w==486,"GS actual portrait top inset is applied after aspect fit");
    GSConfig.PortraitRenderTopAlign=false;rect=CalculateDrawDstRect(600,1200,src,size,GSDisplayAlignment::Center,false,false);
    check(rect.y==375&&rect.w==825,"GS actual portrait center choice restores symmetric letterbox");
    GSConfig.LandscapeRenderTopAlign=true;GSConfig.StretchY=150;
    rect=CalculateDrawDstRect(1200,600,src,size,GSDisplayAlignment::Center,false,false);
    check(rect.y==0&&rect.w==900,"GS actual landscape top alignment anchors oversized vertical crop");
    GSConfig.LandscapeRenderTopAlign=false;GSConfig.StretchY=100;
    for(unsigned turn=0;turn<4;turn++)
    {
        GSConfig.Rotation=static_cast<DisplayRotation>(turn);device.surface_rotation=1;
        const auto logical=device.GetPresentationSize();
        check(logical.x==(turn%2?480:640)&&logical.y==(turn%2?640:480),"GS actual logical presentation swaps axes only for user quarter turns");
        check(static_cast<unsigned>(device.GetPresentRotation())==(turn+1)%4,"GS actual final present combines user and surface rotations exactly once");
    }
    GSConfig.Rotation=DisplayRotation::Rot0;device.surface_rotation=0;
    GSConfig.CustomAspectRatio=0;GSConfig.AspectRatio=AspectRatioType::Custom;
    check(GetCurrentAspectRatioFloat(false)==0.5f,"GS actual custom aspect clamps invalid zero before division");
    GSConfig.CustomAspectRatio=10;check(GetCurrentAspectRatioFloat(false)==5.0f,"GS actual custom aspect clamps excessive panel ratio");
    GSConfig.CustomAspectRatio=2.1f;check(std::abs(GetCurrentAspectRatioFloat(false)-2.1f)<0.00001f,"GS actual custom aspect retains valid value");
    GSConfig.AspectRatio=AspectRatioType::R4_3;EmuConfig.CurrentAspectRatio=AspectRatioType::R4_3;
    GSConfig.SideBySideVR=true;GSConfig.SideBySideVRSize=80;GSConfig.SideBySideVRDistance=0;GSConfig.SideBySideVRHeight=0;
    PresentationTexture texture(GSTexture::RenderTarget,640,448,1,GSTexture::Format::Color);
    device.presented.clear();PresentLocalMultiplayerFrame(&texture,src,GSVector4(0,0,1,1),GSVector4(0,0,640,480),false,PresentShader::COPY,0,true);
    check(device.presented.size()==2&&device.presented[0].x==32&&device.presented[0].z==288&&device.presented[1].x==352&&device.presented[1].z==608,"GS actual SBS present produces centered aspect-fitted boxes in each eye");
    GSConfig.SideBySideVRDistance=100;GSConfig.SideBySideVRHeight=-100;GSVector4 left,right;
    CalculateSideBySideRects(640,480,src,size,false,false,&left,&right);
    check(left.x==0&&right.z==640&&left.y==96,"GS actual SBS distance and height consume remaining eye-box room");
    GSConfig.LocalMultiplayerMode=1;device.presented.clear();
    PresentLocalMultiplayerFrame(&texture,src,GSVector4(0,0,1,1),GSVector4(0,0,640,480),false,PresentShader::COPY,0,true);
    check(device.presented.size()==2&&device.presented[0].z==320&&device.presented[0].w==480,"GS existing local multiplayer layout retains precedence and dimensions");
    // Existing NativeApp.setSetting/RuntimeSettings publishes these exact SettingsInterface keys.
    MemorySettingsInterface si;si.SetStringValue("EmuCore/GS","DisplayRotation","270");si.SetStringValue("EmuCore/GS","AspectRatio","20:9");si.SetFloatValue("EmuCore/GS","CustomAspectRatio",2.4f);
    si.SetBoolValue("EmuCore/GS","PortraitRenderTopAlign",false);si.SetIntValue("EmuCore/GS","PortraitRenderTopInset",44);si.SetBoolValue("EmuCore/GS","LandscapeRenderTopAlign",true);si.SetBoolValue("EmuCore/GS","SideBySideVR",true);si.SetIntValue("EmuCore/GS","SideBySideVRSize",75);
    Pcsx2Config::GSOptions loaded;SettingsLoadWrapper load(si);loaded.LoadSave(load);
    check(loaded.Rotation==DisplayRotation::Rot270&&loaded.AspectRatio==AspectRatioType::R20_9&&loaded.CustomAspectRatio==2.4f,"GS actual SettingsInterface enum and custom-aspect host publication loads into production config");
    check(!loaded.PortraitRenderTopAlign&&loaded.PortraitRenderTopInset==44&&loaded.LandscapeRenderTopAlign&&loaded.SideBySideVR&&loaded.SideBySideVRSize==75,"GS actual settings host adapter loads portrait/inset/landscape/SBS controls");
    MemorySettingsInterface saved;SettingsSaveWrapper save(saved);loaded.LoadSave(save);Pcsx2Config::GSOptions roundtrip;SettingsLoadWrapper again(saved);roundtrip.LoadSave(again);
    check(roundtrip==loaded,"GS actual presentation options survive settings save/load and equality");
    roundtrip.DisableSwapChainPreRotation=!loaded.DisableSwapChainPreRotation;check(!roundtrip.RestartOptionsAreEqual(loaded),"GS actual surface pre-rotation opt-out triggers required device restart");
#else
    check(false,"GS actual rotation, geometry controls and SettingsInterface consumers are available");
#endif
}
extern "C" void EmuCoreXOraclePresentationGeometry(void(*check)(bool,const char*)){GSRenderer::TestPresentationGeometry(check);}
