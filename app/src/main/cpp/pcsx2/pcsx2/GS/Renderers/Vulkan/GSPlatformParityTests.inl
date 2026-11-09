// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Actual driver finalizer and readback-allocation consumer tests; no mirrored resolver.
#include "common/ScopedGuard.h"
namespace GSPlatformParityOracle {
template<class T> static bool BrokenMAD(const T& f) {
    if constexpr(requires {f.broken_mad_deinterlace;}) return f.broken_mad_deinterlace;
    return false;
}
}
void GSDeviceVK::TestPlatformParity(void (*check)(bool,const char*))
{
    const auto props=m_device_properties;const auto drivers=m_device_driver_properties;
    const auto features=m_features;const auto facts=m_road_facts;const auto optional=m_optional_extensions;const auto depth_format=m_depth_format;
    const bool timing=m_gpu_timing_supported,spin=m_spinning_supported;
    ScopedGuard restore=[&](){m_device_properties=props;m_device_driver_properties=drivers;
        FinalizeDriverCapabilities(false);m_features=features;m_road_facts=facts;m_optional_extensions=optional;m_depth_format=depth_format;m_gpu_timing_supported=timing;m_spinning_supported=spin;};
    struct Case {u32 vendor,device,version;VkDriverId driver;const char* name;bool timestamps,mad,coherent;};
    constexpr auto arm=VK_DRIVER_ID_ARM_PROPRIETARY;
    const Case cases[]={
        {0x13b5,0,VK_MAKE_VERSION(44,1,0),arm,"Mali-G615",false,false,false},
        {0x13b5,0,VK_MAKE_VERSION(44,0,0),arm,"Mali-G615",false,false,true},
        {0x13b5,0,VK_MAKE_VERSION(44,2,0),arm,"Mali-G615",false,false,true},
        {0x13b5,0,0,arm,"Mali-G57",true,true,true},
        {0x13b5,0,VK_MAKE_VERSION(44,1,0),arm,"Mali-G57",true,true,false},
        {0x13b5,0,VK_MAKE_VERSION(44,1,1),arm,"Mali-G57",true,true,true},
        {0x13b5,0,0xaa9c4b29u,arm,"Mali-T880",true,false,true},
        {0x13b5,0,VK_MAKE_VERSION(44,0,0),VK_DRIVER_ID_MESA_PANVK,"Mali-G57",true,true,false},
        {0x5143,0x07040000u,VK_MAKE_VERSION(512,900,0),VK_DRIVER_ID_QUALCOMM_PROPRIETARY,"Adreno (TM) 740",true,false,false},
        {0x5143,0x07040000u,VK_MAKE_VERSION(26,3,0),VK_DRIVER_ID_MESA_TURNIP,"Adreno (TM) 740",true,false,false},
        {0x1010,0,VK_MAKE_VERSION(44,0,0),VK_DRIVER_ID_IMAGINATION_PROPRIETARY,"PowerVR",true,false,false},
        {0x5143,0,0,VK_DRIVER_ID_QUALCOMM_PROPRIETARY,"Mali-G615",true,false,false},
        {0x13b5,0,0,arm,"mali-g615",true,false,true},
    };
    m_optional_extensions.vk_khr_driver_properties=true;
    bool timestamps_ok=true,mad_ok=true,memory_ok=true;
    for(const auto& in:cases) {
        m_device_properties={};m_device_properties.vendorID=in.vendor;m_device_properties.deviceID=in.device;
        m_device_properties.driverVersion=in.version;std::strncpy(m_device_properties.deviceName,in.name,VK_MAX_PHYSICAL_DEVICE_NAME_SIZE-1);
        m_device_driver_properties={};m_device_driver_properties.driverID=in.driver;
        m_road_facts=GSVulkanRoad::ResolveFacts(GetRoadIdentity(),false,0);
        m_gpu_timing_supported=m_spinning_supported=true;
        FinalizeDriverCapabilities(true);
        const bool ts=m_gpu_timing_supported&&m_spinning_supported;
        const bool mad=GSPlatformParityOracle::BrokenMAD(m_features);
        const bool coherent=GSDownloadTextureVK::GetReadbackMemoryPreference(*this)==VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        timestamps_ok&=ts==in.timestamps;mad_ok&=mad==in.mad;memory_ok&=coherent==in.coherent;
        if(ts!=in.timestamps||mad!=in.mad||coherent!=in.coherent)
            std::printf("GS_PLATFORM name=%s vendor=%x version=%x timestamps=%u/%u mad=%u/%u coherent=%u/%u\n",
                in.name,in.vendor,in.version,unsigned(ts),unsigned(in.timestamps),unsigned(mad),unsigned(in.mad),unsigned(coherent),unsigned(in.coherent));
    }
    check(timestamps_ok,"GS actual device finalizer disables timing and spin timestamps only for ARM Mali-G615");
    check(mad_ok,"GS actual device finalizer publishes exact ARM Mali-G57 FastMAD defect");
    check(memory_ok,"GS actual download allocation preference uses cached except Arm blob revisions outside exactly r44p1");
    // Capture the real TFX blend builder. The fake handle never reaches a cache or driver.
    const auto create=vkCreateGraphicsPipelines;
    VkPipelineColorBlendAttachmentState captured{};
    GSBlendConstantOracle::captured=&captured;vkCreateGraphicsPipelines=GSBlendConstantOracle::CaptureBlendPipeline;
    ScopedGuard restore_pipeline=[&](){vkCreateGraphicsPipelines=create;GSBlendConstantOracle::captured=nullptr;};
    bool mask_ok=true,negative_ok=true;
    struct MaskCase {u32 vendor,device,version;VkDriverId driver;bool affected;};
    const MaskCase masks[]={
        {0x5143,0x05090000u,0xffffffffu,VK_DRIVER_ID_QUALCOMM_PROPRIETARY,true},
        {0x5143,0x06000000u,0x801e9fffu,VK_DRIVER_ID_QUALCOMM_PROPRIETARY,true},
        {0x5143,0x06000000u,0x801ea000u,VK_DRIVER_ID_QUALCOMM_PROPRIETARY,false},
        {0x5143,0x05090000u,0,VK_DRIVER_ID_MESA_TURNIP,false},
        {0x13b5,0x05090000u,0,arm,false},
    };
    for(const auto& in:masks)for(u8 mask:{u8(0),u8(8),u8(1),u8(7),u8(15)})
    for(bool depth:{false,true})for(bool blend:{false,true}) {
        m_device_properties.vendorID=in.vendor;m_device_properties.deviceID=in.device;m_device_properties.driverVersion=in.version;
        m_device_driver_properties.driverID=in.driver;FinalizeDriverCapabilities(true);
        PipelineSelector pipe{};pipe.cms.wrgba=mask;pipe.dss.ztst=ZTST_ALWAYS;pipe.dss.zwe=depth;
        auto bs=GSHWDrawConfig::BlendState();bs.enable=blend;
        Vulkan::GraphicsPipelineBuilder builder;ConfigureTFXBlendPipeline(builder,pipe,bs);
        const bool built=builder.Create(m_device)!=VK_NULL_HANDLE;
        const bool workaround=in.affected&&!(mask&7)&&depth&&!blend;
        bool ok=built;
        if(workaround) {
            const bool alpha=(mask&8)!=0;
            ok&=captured.blendEnable&&captured.colorWriteMask==15&&captured.srcColorBlendFactor==VK_BLEND_FACTOR_ZERO&&captured.dstColorBlendFactor==VK_BLEND_FACTOR_ONE&&
                captured.srcAlphaBlendFactor==(alpha?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_ZERO)&&captured.dstAlphaBlendFactor==(alpha?VK_BLEND_FACTOR_ZERO:VK_BLEND_FACTOR_ONE);
            mask_ok&=ok;
        } else {
            ok&=captured.colorWriteMask==mask&&bool(captured.blendEnable)==blend;negative_ok&=ok;
        }
    }
    check(mask_ok,"GS actual old Qualcomm masked-RGB depth pipeline preserves RGB through blend and independently gates alpha");
    check(negative_ok,"GS actual mask workaround respects Turnip foreign vendor exact device/version boundary arbitrary RGB masks no-depth and existing blending controls");

}
