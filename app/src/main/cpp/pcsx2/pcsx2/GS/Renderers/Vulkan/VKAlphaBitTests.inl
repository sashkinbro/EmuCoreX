// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// The actual pipeline producer and builder reach a captured Vulkan API boundary.
// Dummy cached shader modules avoid pretending to compile/submit on a fake GPU.
#if __has_include("GS/Renderers/Common/GSAlphaBitLogicOp.h")
#include "GS/Renderers/Common/GSAlphaBitLogicOp.h"
#define ORACLE_HAS_ALPHA_LOGIC 1
#else
#define ORACLE_HAS_ALPHA_LOGIC 0
#endif
namespace AlphaBitPipelineOracle
{
    struct Capture{VkBool32 logic=0,blend=0,stencil=0,depth=0,depthWrite=0;VkLogicOp op=VK_LOGIC_OP_COPY;
        VkColorComponentFlags mask=0;VkStencilOpState front{};};
    static Capture captured;
    static u32 ended=0;
    static VKAPI_ATTR void VKAPI_CALL End(VkCommandBuffer){ended++;}
    static VKAPI_ATTR void VKAPI_CALL Features(VkPhysicalDevice,VkPhysicalDeviceFeatures* f)
    {*f={};f->logicOp=VK_TRUE;}
    static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice,VkPipelineCache,u32 count,const VkGraphicsPipelineCreateInfo* p,
        const VkAllocationCallbacks*,VkPipeline* pipeline)
    {
        captured={};if(count!=1)return VK_ERROR_INITIALIZATION_FAILED;
        if(p->pColorBlendState){const auto& b=*p->pColorBlendState;captured.logic=b.logicOpEnable;captured.op=b.logicOp;
            if(b.attachmentCount){captured.mask=b.pAttachments[0].colorWriteMask;captured.blend=b.pAttachments[0].blendEnable;}}
        if(p->pDepthStencilState){const auto& d=*p->pDepthStencilState;captured.stencil=d.stencilTestEnable;
            captured.front=d.front;captured.depth=d.depthTestEnable;captured.depthWrite=d.depthWriteEnable;}
        *pipeline=reinterpret_cast<VkPipeline>(static_cast<uintptr_t>(1));return VK_SUCCESS;
    }
}
void GSDeviceVK::TestAlphaBitPipelines(void (*check)(bool,const char*))
{
    check(!g_gs_device,"VK alpha-bit CPU pipeline capture owns isolated uncreated device");if(g_gs_device)return;
    GSDeviceVK device;
    const auto oldFeatures=vkGetPhysicalDeviceFeatures;const auto oldCreate=vkCreateGraphicsPipelines;const auto oldEnd=vkCmdEndRenderPass;
    const ScopedGuard restore([&](){vkGetPhysicalDeviceFeatures=oldFeatures;vkCreateGraphicsPipelines=oldCreate;vkCmdEndRenderPass=oldEnd;});
    vkGetPhysicalDeviceFeatures=AlphaBitPipelineOracle::Features;vkCreateGraphicsPipelines=AlphaBitPipelineOracle::Create;
    device.SelectDeviceFeatures();
    check(device.m_device_features.logicOp==VK_TRUE,"VK logical-device creation enables queried physical logicOp capability");
    bool set=true,clear=true,stencil=true,keys=true;
    for(u8 op:{u8(1),u8(2)})for(u8 copy:{u8(0),u8(1),u8(2)})
    {
        PipelineSelector p{};p.rt=p.ds=true;p.topology=static_cast<u8>(GSHWDrawConfig::Topology::Triangle);
        p.ps.tfx=4;p.ps.no_color1=true;
        // Stable raw byte encodings let this compile against the pre-feature
        // baseline too: it ignored exactly these reserved cms/dss bits.
        p.cms.key=0x08|(op<<4);p.dss.key=ZTST_GEQUAL|4|(copy<<5);
        device.m_tfx_vertex_shaders[p.vs.key]=reinterpret_cast<VkShaderModule>(static_cast<uintptr_t>(1));
        device.m_tfx_fragment_shaders[p.ps]=reinterpret_cast<VkShaderModule>(static_cast<uintptr_t>(2));
        const VkPipeline result=device.CreateTFXPipelineWithCache(p,VK_NULL_HANDLE,false);
        const auto& c=AlphaBitPipelineOracle::captured;
        const bool correct=result!=VK_NULL_HANDLE&&c.logic==VK_TRUE&&!c.blend&&c.mask==VK_COLOR_COMPONENT_A_BIT&&
            c.op==(op==1?VK_LOGIC_OP_OR:VK_LOGIC_OP_AND_INVERTED)&&c.depth&&c.depthWrite;
        if(op==1)set&=correct;else clear&=correct;
        stencil&=copy?(c.stencil&&c.front.passOp==VK_STENCIL_OP_REPLACE&&c.front.reference==(copy==2?1u:0u)&&
            c.front.compareOp==VK_COMPARE_OP_ALWAYS&&c.front.writeMask==1u):!c.stencil;
        PipelineSelector alternate=p;alternate.cms.key^=0x10;
        TFXPipelineCache cache;cache.Insert(p,reinterpret_cast<VkPipeline>(static_cast<uintptr_t>(3)));
        keys&=cache.Lookup(alternate)==VK_NULL_HANDLE&&cache.Lookup(p)!=VK_NULL_HANDLE;
        alternate=p;alternate.dss.key^=0x20;keys&=cache.Lookup(alternate)==VK_NULL_HANDLE;
        if(!correct)std::fprintf(stderr,"VK_ALPHA_BIT_PIPELINE op=%u stencil=%u logic=%u blend=%u mask=%x actualOp=%u\n",
            op,copy,c.logic,c.blend,c.mask,c.op);
    }
    check(set,"VK actual set-alpha-bit pipeline writes alpha through OR with blending disabled");
    check(clear,"VK actual clear-alpha-bit pipeline writes alpha through AND_INVERTED with blending disabled");
    check(stencil,"VK actual shared DATE mark pipeline replaces only stencil bit0 at the passing value");
    check(keys,"VK pipeline cache separates logic-operation and shared-DATE-stencil states");
#if ORACLE_HAS_ALPHA_LOGIC
    bool runs=true,qualification=true,date=true;
    GSVertex verts[9]{};u16 indices[9]={0,1,2,3,4,5,6,7,8};
    const auto classify=[&](std::initializer_list<u8> alpha,bool iip,bool fba){
        size_t n=0;for(u8 a:alpha)verts[n++].RGBAQ.A=a;
        return GSAlphaBitLogicOp::ClassifyTriangles(verts,indices,static_cast<u32>(n),iip,fba);};
    auto run=classify({0,0,0,128,128,128},true,false);runs&=run.first_op==2&&run.first_indices==3;
    run=classify({128,128,128,0,0,0},true,false);runs&=run.first_op==1&&run.first_indices==3;
    run=classify({0,0,0,128,128,128,0,0,0},true,false);runs&=run.first_op==0;
    run=classify({0,128,0},true,false);runs&=run.first_op==0;
    run=classify({1,1,128},false,false);runs&=run.first_op==1&&run.first_indices==0;
    run=classify({0,0,1},false,false);runs&=run.first_op==0;
    run=classify({0,0,0,128,128,128,0,0,0},true,true);runs&=run.first_op==1&&run.first_indices==0;
    runs&=GSAlphaBitLogicOp::ClassifyTriangles(verts,indices,0,false,false).first_op==0&&
        GSAlphaBitLogicOp::ClassifyTriangles(verts,indices,4,false,false).first_op==0;
    for(bool feature:{false,true})for(bool costs:{false,true})for(bool broken:{false,true})
        qualification&=GSAlphaBitLogicOp::DeviceQualifies(feature,costs,broken)==(feature&&costs&&!broken);
    for(bool datm:{false,true})for(u8 op:{u8(1),u8(2)})for(u32 oldAlpha=0;oldAlpha<256;oldAlpha++)
    {
        const u32 written=op==1?(oldAlpha|128u):(oldAlpha&127u);
        const bool passes=((written&128u)!=0)==datm;
        date&=GSAlphaBitLogicOp::StencilWriteFor(op,datm)==(passes?2:1);
    }
    check(runs,"VK alpha-bit triangle classification preserves provoking alpha two-run order and rejects varying or third runs");
    check(qualification,"VK alpha-bit device qualification requires real logicOp per-draw-read cost and working depth mask");
    check(date,"VK shared DATE mark stencil value agrees with independent stored-alpha bit oracle");
#else
    check(false,"VK alpha-bit triangle classification preserves provoking alpha two-run order and rejects varying or third runs");
    check(false,"VK alpha-bit device qualification requires real logicOp per-draw-read cost and working depth mask");
    check(false,"VK shared DATE mark stencil value agrees with independent stored-alpha bit oracle");
#endif
    const auto lifetime=[&]<typename Device>(Device& d)
    {
        if constexpr(requires{d.m_date_copy;d.m_render_pass_serial;d.DateCopyLive(GSHWDrawConfig{});})
        {
            GSHWDrawConfig c{};
            c.rt=reinterpret_cast<GSTexture*>(static_cast<uintptr_t>(0x100));
            c.ds=reinterpret_cast<GSTexture*>(static_cast<uintptr_t>(0x200));
            d.m_render_pass_serial=7;d.m_current_render_pass=reinterpret_cast<VkRenderPass>(static_cast<uintptr_t>(1));
            d.m_date_copy={true,c.rt,c.ds,SetDATM::DATM1,7};
            bool scoped=d.DateCopyLive(c);
            auto other=c;other.rt=reinterpret_cast<GSTexture*>(static_cast<uintptr_t>(0x300));scoped&=!d.DateCopyLive(other);
            other=c;other.ds=reinterpret_cast<GSTexture*>(static_cast<uintptr_t>(0x400));scoped&=!d.DateCopyLive(other);
            d.m_date_copy.pass_serial=6;scoped&=!d.DateCopyLive(c);d.m_date_copy.pass_serial=7;
            vkCmdEndRenderPass=AlphaBitPipelineOracle::End;AlphaBitPipelineOracle::ended=0;d.EndRenderPass();
            scoped&=!d.DateCopyLive(c)&&d.m_render_pass_serial==8&&AlphaBitPipelineOracle::ended==1;
            check(scoped,"VK actual shared DATE copy rejects target depth pass changes and expires at real render-pass end");
        }
        else check(false,"VK actual shared DATE copy rejects target depth pass changes and expires at real render-pass end");
    };
    lifetime(device);

}
