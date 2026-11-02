// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Device and Apply adapters are actual production seams, initially false/no-op.
#include "GS/Renderers/Common/GSBlendConstantPolicy.h"

namespace GSBlendConstantOracle
{
static thread_local VkPipelineColorBlendAttachmentState* captured = nullptr;
static VKAPI_ATTR VkResult VKAPI_CALL CaptureBlendPipeline(VkDevice, VkPipelineCache, u32 count,
    const VkGraphicsPipelineCreateInfo* infos, const VkAllocationCallbacks*, VkPipeline* result)
{
    if(count!=1 || !infos->pColorBlendState || infos->pColorBlendState->attachmentCount!=1)
        return VK_ERROR_UNKNOWN;
    *captured=infos->pColorBlendState->pAttachments[0];
    *result=reinterpret_cast<VkPipeline>(uintptr_t(0x76124000));
    return VK_SUCCESS;
}
}

void GSDeviceVK::TestBlendConstant(void (*check)(bool, const char*))
{
    using namespace GSBlendConstantPolicy;
    bool devices = true;
    for (bool vk : {false,true}) for (bool adreno : {false,true}) for (bool turnip : {false,true})
        devices &= DeviceDefect(vk,adreno,turnip) == (vk && adreno && turnip);
    check(devices, "AF route device defect is exact Vulkan Adreno Turnip only");
    const auto make = [] {
        GSHWDrawConfig c{};
        c.blend = GSHWDrawConfig::BlendState(true, GSDevice::CONST_COLOR, GSDevice::INV_CONST_COLOR,
            GSDevice::OP_ADD, GSDevice::CONST_ONE, GSDevice::CONST_ZERO, true, 64);
        c.ps.blend_c = 2; c.ps.no_color1 = true; c.cb_ps.TA_MaxDepth_Af.a = -99.0f;
        return c;
    };
    const auto bytes = [](const GSHWDrawConfig& c) {
        std::array<u8,sizeof(c)> out; std::memcpy(out.data(), &c, out.size()); return out;
    };
    GSDevice::FeatureSupport features; features.broken_blend_constant = true; features.dual_source_blend = true;
    bool eligible = true, forbidden = true, untouched = true;
    u64 positive = 0, negative = 0;
    const auto exercise = [&](GSHWDrawConfig c, GSDevice::FeatureSupport f, bool expected) {
        const auto before = bytes(c);
        const auto old = c;
        const bool applied = ApplyFixedFactor(c,f);
        if (!expected)
        {
            forbidden &= !applied; untouched &= bytes(c) == before; ++negative;
            return;
        }
        ++positive;
        const auto factor = [](u8 value) -> u8 {
            if (value == GSDevice::CONST_COLOR) return GSDevice::SRC1_COLOR;
            if (value == GSDevice::INV_CONST_COLOR) return GSDevice::INV_SRC1_COLOR;
            return value;
        };
        eligible &= applied && c.blend.enable == old.blend.enable && c.blend.op == old.blend.op &&
            c.blend.src_factor == factor(old.blend.src_factor) && c.blend.dst_factor == factor(old.blend.dst_factor) &&
            c.blend.src_factor_alpha == factor(old.blend.src_factor_alpha) &&
            c.blend.dst_factor_alpha == factor(old.blend.dst_factor_alpha) &&
            !c.blend.constant_enable && c.blend.constant == 0 && c.ps.af_in_src1 && !c.ps.no_color1 &&
            c.cb_ps.TA_MaxDepth_Af.a == float(old.blend.constant)/128.0f;
        // Restore only the approved outputs; every other config byte must be unchanged.
        c.blend = old.blend; c.ps.af_in_src1 = old.ps.af_in_src1; c.ps.no_color1 = old.ps.no_color1;
        c.cb_ps.TA_MaxDepth_Af.a = old.cb_ps.TA_MaxDepth_Af.a;
        untouched &= bytes(c) == before;
    };
    for (u32 afix=0; afix<256; ++afix) for (u8 op : {GSDevice::OP_ADD,GSDevice::OP_SUBTRACT,GSDevice::OP_REV_SUBTRACT})
    {
        auto c=make(); c.blend.constant=afix; c.blend.op=op;
        exercise(c,features,afix<=128);
    }
    for (u32 slot=0;slot<4;++slot) for (u8 value=0;value<16;++value)
    {
        auto c=make(); c.blend.src_factor=c.blend.src_factor_alpha=GSDevice::CONST_ONE;
        c.blend.dst_factor=c.blend.dst_factor_alpha=GSDevice::CONST_ZERO;
        if(slot==0)c.blend.src_factor=value;if(slot==1)c.blend.dst_factor=value;
        if(slot==2)c.blend.src_factor_alpha=value;if(slot==3)c.blend.dst_factor_alpha=value;
        exercise(c,features,value==GSDevice::CONST_COLOR||value==GSDevice::INV_CONST_COLOR);
    }
    for(u32 guard=0;guard<9;++guard)
    {
        auto c=make(); auto f=features;
        if(guard==0)f.broken_blend_constant=false;
        if(guard==1)f.dual_source_blend=false;
        if(guard==2)c.ps.pabe=true;
        if(guard==3)c.ps.blend_c=0;
        if(guard==4)c.blend.enable=false;
        if(guard==5)c.blend.constant_enable=false;
        if(guard==6)c.ps.blend_c=1;
        if(guard==7){c.blend_multi_pass.enable=true;c.blend_multi_pass.blend.src_factor=GSDevice::SRC1_COLOR;}
        if(guard==8){c.blend.src_factor=GSDevice::CONST_ONE;c.blend.dst_factor=GSDevice::CONST_ZERO;}
        exercise(c,f,false);
    }
    for(u32 slot=0;slot<4;++slot)for(u8 factor:{GSDevice::SRC1_COLOR,GSDevice::INV_SRC1_COLOR,GSDevice::SRC1_ALPHA,GSDevice::INV_SRC1_ALPHA})
    {
        auto c=make();if(slot==0)c.blend.src_factor=factor;if(slot==1)c.blend.dst_factor=factor;
        if(slot==2)c.blend.src_factor_alpha=factor;if(slot==3)c.blend.dst_factor_alpha=factor;
        exercise(c,features,false);
    }
    for(u32 slot=0;slot<4;++slot)for(u8 factor:{GSDevice::SRC1_COLOR,GSDevice::INV_SRC1_COLOR,GSDevice::SRC1_ALPHA,GSDevice::INV_SRC1_ALPHA})
    {
        auto c=make();c.blend_multi_pass.enable=true;
        c.blend_multi_pass.blend=GSHWDrawConfig::BlendState(true,GSDevice::CONST_ONE,GSDevice::CONST_ZERO,GSDevice::OP_ADD,GSDevice::CONST_ONE,GSDevice::CONST_ZERO,false,0);
        if(slot==0)c.blend_multi_pass.blend.src_factor=factor;if(slot==1)c.blend_multi_pass.blend.dst_factor=factor;
        if(slot==2)c.blend_multi_pass.blend.src_factor_alpha=factor;if(slot==3)c.blend_multi_pass.blend.dst_factor_alpha=factor;
        exercise(c,features,false);
    }
    check(eligible && positive>0, "AF actual adapter maps eligible fixed factors outputs uniform and required source1 selector exactly");
    check(forbidden && negative>0, "AF actual adapter rejects absent dual source PABE output ownership limits and irrelevant blends");
    check(untouched, "AF route changes no config bytes on every Mali default or other rejected case");
    DrawInputs input; input.broken_blend_constant=true; input.blend_c=2;
    input.blend_factor_in_alpha=true;
    check(!CanRouteFixedFactorToSecondOutput(make().blend,input), "AF pure policy rejects upstream alpha-output substitution even though local core does not implement it");
    GSHWDrawConfig::PSSelector selected; selected.af_in_src1=1;
    const auto generated=BuildTFXFragmentShaderSource(m_tfx_source,selected,{});
    check(generated.find("#define PS_AF_IN_SRC1 1")!=std::string::npos,
        "AF actual fragment source builder exports the new selector macro");
    check(m_tfx_source.find("#if PS_AF_IN_SRC1")!=std::string::npos &&
        m_tfx_source.find("vec4 alpha_blend = vec4(Af)")!=std::string::npos,
        "AF actual production shader contains the fixed-factor second-output branch");
    GSHWDrawConfig::PSSelector previous; previous.sprite_edge_clamp=1; previous.fst=1; previous.pabe=1;
    auto next=previous;next.af_in_src1=1;
    auto restored=next;restored.af_in_src1=0;
    check(sizeof(next)==16 && restored.key_lo==previous.key_lo && restored.key_hi==previous.key_hi &&
        next.key_lo==previous.key_lo && next.key_hi!=previous.key_hi,
        "AF selector appends a distinct shader-cache key without moving previous serialized fields");
    // Actual pipeline builder capture never forwards synthetic dual-source
    // support to the real driver. Fake handles are never cached or submitted.
    bool mapping=true;
    const auto saved_create=vkCreateGraphicsPipelines;
    VkPipelineColorBlendAttachmentState captured{};
    GSBlendConstantOracle::captured=&captured;
    vkCreateGraphicsPipelines=GSBlendConstantOracle::CaptureBlendPipeline;
    {
        ScopedGuard restore_builder=[&](){vkCreateGraphicsPipelines=saved_create;GSBlendConstantOracle::captured=nullptr;};
        for(u32 slot=0;slot<4;++slot)for(bool inverse:{false,true})
        {
            auto c=make();c.blend.src_factor=c.blend.src_factor_alpha=GSDevice::CONST_ONE;
            c.blend.dst_factor=c.blend.dst_factor_alpha=GSDevice::CONST_ZERO;
            const u8 factor=inverse?GSDevice::INV_CONST_COLOR:GSDevice::CONST_COLOR;
            if(slot==0)c.blend.src_factor=factor;if(slot==1)c.blend.dst_factor=factor;
            if(slot==2)c.blend.src_factor_alpha=factor;if(slot==3)c.blend.dst_factor_alpha=factor;
            mapping&=ApplyFixedFactor(c,features);
            PipelineSelector selected{};selected.bs=c.blend;selected.ps=c.ps;selected.cms.wrgba=15;
            Vulkan::GraphicsPipelineBuilder builder;
            ConfigureTFXBlendPipeline(builder,selected,c.blend);
            mapping&=builder.Create(m_device)!=VK_NULL_HANDLE && captured.blendEnable;
            const VkBlendFactor expected=inverse?VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:VK_BLEND_FACTOR_SRC1_COLOR;
            if(slot==0)mapping&=captured.srcColorBlendFactor==expected;
            if(slot==1)mapping&=captured.dstColorBlendFactor==expected;
            if(slot==2)mapping&=captured.srcAlphaBlendFactor==expected;
            if(slot==3)mapping&=captured.dstAlphaBlendFactor==expected;
        }
    }
    check(mapping,"AF actual TFX pipeline builder maps source1 and inverse source1 in all four factor positions");
    bool compiled=true;
    for(bool active:{false,true})
    {
        GSHWDrawConfig::PSSelector selector;selector.af_in_src1=active;selector.no_color1=!active;
        const auto shader=g_vulkan_shader_cache->GetFragmentShader(BuildTFXFragmentShaderSource(m_tfx_source,selector,{}));
        compiled&=shader!=VK_NULL_HANDLE;
        if(shader)vkDestroyShaderModule(m_device,shader,nullptr);
    }
    check(compiled,"AF both actual full TFX fragment interfaces compile without claiming unsupported blend hardware");
    std::printf("GS_AF_ROUTE positives=%llu negatives=%llu\n", static_cast<unsigned long long>(positive), static_cast<unsigned long long>(negative));
}
