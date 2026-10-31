// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Real pixels from the actual production second-output expression, exported
// through one attachment. This does not claim unsupported dual-source hardware.
void GSDeviceVK::TestBlendConstantPixels(void (*check)(bool, const char*))
{
    const size_t begin_new=m_tfx_source.find("#if PS_AF_IN_SRC1");
    const size_t begin=begin_new!=std::string::npos?begin_new:m_tfx_source.find("#if SW_AD_TO_HW");
    const size_t end=m_tfx_source.find("// Correct the ALPHA value based on the output format",begin);
    check(begin!=std::string::npos&&end!=std::string::npos,"AF pixel fixture locates the actual production second-output body");
    if(begin==std::string::npos||end==std::string::npos)return;
    const auto expression=m_tfx_source.substr(begin,end-begin);
    const auto vs=g_vulkan_shader_cache->GetVertexShader(R"(#version 450
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.0-1.0,0,1);})");
    check(vs!=VK_NULL_HANDLE,"AF pixel fixture compiles a real full-screen vertex shader");
    if(!vs)return;
    ScopedGuard release_vs=[&](){vkDestroyShaderModule(m_device,vs,nullptr);};
    Vulkan::SamplerBuilder sampler_builder;sampler_builder.SetPointSampler();
    const auto sampler=sampler_builder.Create(m_device);
    if(!sampler){check(false,"AF pixel fixture creates a real sampler");return;}
    ScopedGuard release_sampler=[&](){vkDeviceWaitIdle(m_device);vkDestroySampler(m_device,sampler,nullptr);m_utility_sampler=VK_NULL_HANDLE;};
    const auto pass=GetRenderPass(LookupNativeFormat(GSTexture::Format::Color),VK_FORMAT_UNDEFINED);
    bool values=true,control=true;
    for(bool enabled:{false,true})for(u32 afix:{0u,1u,64u,128u})
    {
        GSHWDrawConfig::PSSelector selected;selected.af_in_src1=enabled;selected.no_color1=1;
        const std::string body=std::string(R"(
#define SW_AD_TO_HW 0
layout(push_constant) uniform Params{vec4 values;} params;
layout(location=0) out vec4 result;
void main(){float Af=params.values.x;vec4 C=vec4(0.0,0.0,0.0,16.0);
)")+expression+"result=alpha_blend;}\n";
        const auto fs=g_vulkan_shader_cache->GetFragmentShader(BuildTFXFragmentShaderSource(body,selected,{}));
        if(!fs){check(false,"AF production expression compiles without requiring physical dual-source support");return;}
        ScopedGuard release_fs=[&](){vkDestroyShaderModule(m_device,fs,nullptr);};
        Vulkan::GraphicsPipelineBuilder builder;
        builder.SetVertexShader(vs);builder.SetFragmentShader(fs);builder.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        builder.SetNoCullRasterizationState();builder.SetNoDepthTestState();builder.SetNoStencilState();builder.SetNoBlendingState();
        builder.SetDynamicViewportAndScissorState();builder.SetPipelineLayout(m_utility_pipeline_layout);builder.SetRenderPass(pass,0);
        const auto pipeline=builder.Create(m_device);
        if(!pipeline){check(false,"AF production expression creates a real supported one-output graphics pipeline");return;}
        ScopedGuard release_pipeline=[&](){EndRenderPass();vkDeviceWaitIdle(m_device);vkDestroyPipeline(m_device,pipeline,nullptr);m_current_pipeline=VK_NULL_HANDLE;};
        auto target=GSTextureVK::Create(GSTexture::RenderTarget,GSTexture::Format::Color,4,4,1);
        if(!target){check(false,"AF pixel fixture allocates its real output image");return;}
        const auto rect=target->GetRect();
        SetUtilityTexture(m_null_texture.get(),sampler);
        OMSetRenderTargets(target.get(),nullptr,rect);BeginRenderPass(pass,rect);SetPipeline(pipeline);
        SetViewport(VkViewport{0,0,4,4,0,1});SetScissor(rect);
        const float push[4]={float(afix)/128.0f,0,0,0};SetUtilityPushConstants(push,sizeof(push));
        if(!ApplyUtilityState()){check(false,"AF pixel fixture applies real descriptors and push constants");return;}
        vkCmdDraw(GetCurrentCommandBuffer(),3,1,0,0);EndRenderPass();
        auto download=GSDownloadTextureVK::Create(4,4,GSTexture::Format::Color);
        if(!download){check(false,"AF pixel fixture allocates readback");return;}
        download->CopyFromTexture(rect,target.get(),rect,0);download->Flush();
        if(!download->Map(rect)){check(false,"AF pixel fixture maps its actual rendered image");return;}
        const u32 expected=enabled?(afix*255+64)/128:32;
        bool same=true;
        for(int y=0;y<4;++y)for(int x=0;x<16;++x)
        {
            const int actual=download->GetMapPointer()[y*download->GetMapPitch()+x];
            same&=std::abs(actual-int(expected))<=1;
        }
        download->Unmap();
        if(enabled)values&=same;else control&=same;
    }
    check(values,"AF actual production shader body exports AFIX0/1/64/128 through its second-output expression");
    check(control,"AF disabled selector preserves the source-alpha expression on actual GPU pixels");
}
