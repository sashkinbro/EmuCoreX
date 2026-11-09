// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Explicit 8x4 physical-GPU cases. No random generator, feature override,
// synthetic shader or fake Vulkan object participates in these draws.
namespace GSDateStencilGPU
{
static thread_local PFN_vkCmdBeginRenderPass real_begin=nullptr;
static thread_local VkRenderPass setup_pass=VK_NULL_HANDLE;
static thread_local u32 setup_count=0;
static thread_local bool full_setup=true;
static VKAPI_ATTR void VKAPI_CALL Begin(VkCommandBuffer cb,const VkRenderPassBeginInfo* info,VkSubpassContents contents)
{
    if(info->renderPass==setup_pass)
    {
        ++setup_count;
        full_setup&=info->renderArea.offset.x==0&&info->renderArea.offset.y==0&&
            info->renderArea.extent.width==8&&info->renderArea.extent.height==4;
    }
    real_begin(cb,info,contents);
}
}

void GSDeviceVK::TestDateStencilPixels(void(*check)(bool,const char*))
{
    using namespace GSDateStencilGPU;
    {
        GSHWDrawConfig config{};config.colormask.wrgba=15;
        check(GSCanMirrorDateStencilAlpha(config),"GS simple replacement-alpha output qualifies for DATE mirroring");
        config.ps.replacement_alpha_snap=1;
        check(!GSCanMirrorDateStencilAlpha(config),"GS replacement alpha snap cannot use an unsnapped DATE alpha range");
        config.ps.replacement_alpha_snap=0;config.blend.enable=1;
        config.blend.src_factor_alpha=GSDevice::CONST_ZERO;config.blend.dst_factor_alpha=GSDevice::CONST_ONE;
        check(!GSCanMirrorDateStencilAlpha(config),"GS alpha-preserving blend cannot mirror the source alpha bit");
    }
    check(m_tfx_preparation_device&&!m_allocator&&!m_global_descriptor_pool,
        "GS DATE GPU fixture starts on isolated preparation-device resources");
    if(!m_tfx_preparation_device||m_allocator||m_global_descriptor_pool)return;
    if(!m_features.stencil_buffer||m_features.texture_barrier)
    {
        std::fprintf(stderr,"UNAVAILABLE GS DATE stencil-copy GPU case: physical stencil=%u textureBarrier=%u\n",
            m_features.stencil_buffer,m_features.texture_barrier);
        return;
    }

    // The preparation device owns TFX modules/layouts/pipelines/render passes.
    // This scope owns only the small draw/upload/utility resources it adds.
    const auto saved_perf=g_perfmon;
    ScopedGuard restore_perf([&](){g_perfmon=saved_perf;});
    ScopedGuard cleanup([&](){
        EndRenderPass();
        if(m_allocator&&m_current_command_buffer)ExecuteCommandBuffer(true);
        vkDeviceWaitIdle(m_device);
        m_date_copy.valid=false;m_null_texture.reset();
        if(m_tfx_ubo_descriptor_set)FreePersistentDescriptorSet(m_tfx_ubo_descriptor_set);
        m_tfx_ubo_descriptor_set=VK_NULL_HANDLE;
        m_vertex_stream_buffer.Destroy(false);m_index_stream_buffer.Destroy(false);
        m_expand_index_stream_buffer.Destroy(false);m_texture_stream_buffer.Destroy(false);
        m_vertex_uniform_stream_buffer.Destroy(false);m_fragment_uniform_stream_buffer.Destroy(false);
        for(auto& frame:m_frame_resources)
        {
            for(auto& callback:frame.cleanup_resources)callback();frame.cleanup_resources.clear();
            if(frame.fence)vkDestroyFence(m_device,frame.fence,nullptr);
            if(frame.command_pool)vkDestroyCommandPool(m_device,frame.command_pool,nullptr);
            if(frame.descriptor_pool)vkDestroyDescriptorPool(m_device,frame.descriptor_pool,nullptr);
            for(auto pool:frame.extra_descriptor_pools)vkDestroyDescriptorPool(m_device,pool,nullptr);
            frame=FrameResources{};
        }
        for(auto pipeline:m_convert)if(pipeline)vkDestroyPipeline(m_device,pipeline,nullptr);m_convert.clear();
        for(const auto& sampler:m_samplers)if(sampler.second)vkDestroySampler(m_device,sampler.second,nullptr);
        m_samplers.clear();m_point_sampler=m_linear_sampler=m_tfx_sampler=VK_NULL_HANDLE;
        if(m_timestamp_query_pool)vkDestroyQueryPool(m_device,m_timestamp_query_pool,nullptr);m_timestamp_query_pool=VK_NULL_HANDLE;
        if(m_pipeline_statistics_query_pool)vkDestroyQueryPool(m_device,m_pipeline_statistics_query_pool,nullptr);m_pipeline_statistics_query_pool=VK_NULL_HANDLE;
        if(m_global_descriptor_pool)vkDestroyDescriptorPool(m_device,m_global_descriptor_pool,nullptr);m_global_descriptor_pool=VK_NULL_HANDLE;
        if(m_utility_pipeline_layout)vkDestroyPipelineLayout(m_device,m_utility_pipeline_layout,nullptr);m_utility_pipeline_layout=VK_NULL_HANDLE;
        if(m_utility_ds_layout)vkDestroyDescriptorSetLayout(m_device,m_utility_ds_layout,nullptr);m_utility_ds_layout=VK_NULL_HANDLE;
        if(m_allocator)vmaDestroyAllocator(m_allocator);m_allocator=VK_NULL_HANDLE;
        m_current_render_pass=VK_NULL_HANDLE;m_current_command_buffer=VK_NULL_HANDLE;
        m_current_framebuffer=VK_NULL_HANDLE;m_current_render_target=m_current_depth_target=nullptr;
        m_current_pipeline=VK_NULL_HANDLE;m_index_buffer=VK_NULL_HANDLE;m_utility_texture=nullptr;
        m_tfx_textures.fill(nullptr);m_current_frame=0;m_next_fence_counter=1;m_completed_fence_counter=0;
    });
    const bool allocated=CreateAllocator()&&CreateCommandBuffers()&&
        m_texture_stream_buffer.Create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,65536,"date-proof-upload")&&
        m_index_stream_buffer.Create(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,4096,"date-proof-index")&&
        m_vertex_stream_buffer.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|(m_features.vs_expand?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:0),65536,"date-proof-vertex")&&
        // Production binds INDEX_BUFFER_SIZE at AA1 storage binding3 even when
        // this draw does not expand; keep that physical descriptor range valid.
        m_expand_index_stream_buffer.Create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,m_features.aa1?INDEX_BUFFER_SIZE:64,"date-proof-expand")&&
        m_vertex_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,65536,"date-proof-vs-ubo")&&
        m_fragment_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,65536,"date-proof-ps-ubo")&&
        CreateGlobalDescriptorPool()&&CreateNullTexture()&&CreatePersistentDescriptorSets()&&CreateRenderPasses();
    check(allocated,"GS DATE GPU fixture creates bounded production TFX streams descriptors attachments and passes");
    if(!allocated)return;
    m_index_buffer=m_index_stream_buffer.GetBuffer();
    m_point_sampler=GetSampler(GSHWDrawConfig::SamplerSelector::Point());

    // Build only the four canonical DATE conversion variants. This is the
    // production utility shader source and production selector/macro/layout
    // schema, without compiling the unrelated conversion/presentation catalog.
    Vulkan::DescriptorSetLayoutBuilder dsl;if(m_use_push_descriptors)dsl.SetPushFlag();
    dsl.AddBinding(0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,NUM_UTILITY_SAMPLERS,VK_SHADER_STAGE_FRAGMENT_BIT);
    m_utility_ds_layout=dsl.Create(m_device);
    Vulkan::PipelineLayoutBuilder layout;layout.AddDescriptorSet(m_utility_ds_layout);
    layout.AddPushConstants(VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,CONVERT_PUSH_CONSTANTS_SIZE);
    m_utility_pipeline_layout=layout.Create(m_device);
    const auto convert=ReadShaderSource("shaders/vulkan/convert.glsl");
    check(convert&&m_utility_ds_layout&&m_utility_pipeline_layout&&m_point_sampler,
        "GS DATE GPU fixture reads canonical conversion shaders and builds actual utility bindings");
    if(!convert||!m_utility_ds_layout||!m_utility_pipeline_layout||!m_point_sampler)return;
    VkShaderModule vertex=GetUtilityVertexShader(*convert);
    ScopedGuard destroy_vertex([&](){if(vertex)vkDestroyShaderModule(m_device,vertex,nullptr);});
    m_convert.resize(ShaderConvertSelector::NUM_TOTAL_SHADERS);
    bool conversions=vertex!=VK_NULL_HANDLE;
    for(SetDATM mode:{SetDATM::DATM0,SetDATM::DATM1,SetDATM::DATM0_RTA_CORRECTION,SetDATM::DATM1_RTA_CORRECTION})
    {
        if(!conversions)break;
        const ShaderConvertSelector shader(SetDATMShader(mode));
        std::string source=fmt::format("#define HAS_BILN {}\n#define HAS_STENCIL_OUTPUT {}\n#define HAS_INTEGER_OUTPUT {}\n#define HAS_DEPTH_OUTPUT {}\n#define HAS_FLOAT32_INPUT {}\n#define HAS_FLOAT32_OUTPUT {}\n",
            int(shader.Biln()),int(shader.StencilOutput()),int(shader.IntegerOutputBpp()!=0),int(shader.DepthOutput()),int(shader.Float32Input()),int(shader.Float32Output()));
        source+=*convert;VkShaderModule fragment=GetUtilityFragmentShader(source,shader.EntryPoint());
        ScopedGuard destroy_fragment([&](){if(fragment)vkDestroyShaderModule(m_device,fragment,nullptr);});
        if(!fragment){conversions=false;break;}
        Vulkan::GraphicsPipelineBuilder pipeline;SetPipelineProvokingVertex(m_features,pipeline);AddUtilityVertexAttributes(pipeline);
        pipeline.SetPipelineLayout(m_utility_pipeline_layout);pipeline.SetDynamicViewportAndScissorState();
        pipeline.AddDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS);pipeline.AddDynamicState(VK_DYNAMIC_STATE_LINE_WIDTH);
        pipeline.SetNoCullRasterizationState();pipeline.SetNoBlendingState();pipeline.SetVertexShader(vertex);pipeline.SetFragmentShader(fragment);
        pipeline.SetDepthState(false,false,VK_COMPARE_OP_ALWAYS);
        const VkStencilOpState stencil{VK_STENCIL_OP_KEEP,VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_KEEP,VK_COMPARE_OP_ALWAYS,1u,1u,1u};
        pipeline.SetStencilState(true,stencil,stencil);pipeline.SetColorWriteMask(0,shader.Mask());pipeline.SetRenderPass(m_date_setup_render_pass,0);
        m_convert[shader.Index()]=pipeline.Create(m_device,g_vulkan_shader_cache->GetPipelineCache(true),false);
        conversions&=m_convert[shader.Index()]!=VK_NULL_HANDLE;
    }
    check(conversions,"GS DATE GPU fixture builds only four physical canonical DATM setup pipelines");
    if(!conversions)return;

    const auto download=[&](GSTextureVK& texture)
    {
        std::vector<u32> output;EndRenderPass();
        auto staging=GSDownloadTextureVK::Create(8,4,texture.GetFormat());if(!staging)return output;
        staging->CopyFromTexture(texture.GetRect(),&texture,texture.GetRect(),0);staging->Flush();
        if(!staging->Map(texture.GetRect()))return output;
        output.resize(32);for(u32 y=0;y<4;y++)std::memcpy(output.data()+y*8,staging->GetMapPointer()+y*staging->GetMapPitch(),32);
        staging->Unmap();return output;
    };
    u32 total_draws=0;
    for(SetDATM mode:{SetDATM::DATM1,SetDATM::DATM0,SetDATM::DATM1_RTA_CORRECTION})
    {
        EndRenderPass();InitializeState();
        const bool inverse=mode==SetDATM::DATM0,rta=mode==SetDATM::DATM1_RTA_CORRECTION;
        const u8 low=rta?64:32,high=rta?255:192;
        auto target=GSTextureVK::Create(GSTexture::RenderTarget,GSTexture::Format::Color,8,4,1);
        auto depth=GSTextureVK::Create(GSTexture::DepthStencil,GSTexture::Format::DepthStencil,8,4,1);
        auto other_depth=GSTextureVK::Create(GSTexture::DepthStencil,GSTexture::Format::DepthStencil,8,4,1);
        check(target&&depth&&other_depth,"GS DATE GPU case allocates three tiny physical color/depth images");
        if(!target||!depth||!other_depth)return;
        std::array<u32,32> expected;for(u32 y=0;y<4;y++)for(u32 x=0;x<8;x++)expected[y*8+x]=(u32(x&1?high:low)<<24)|0x003f2111u;
        if(!target->Update(target->GetRect(),expected.data(),32)){check(false,"GS DATE seed upload succeeds");return;}
        ClearDepth(depth.get(),0.5f);depth->CommitClear();ClearDepth(other_depth.get(),0.5f);other_depth->CommitClear();
        const GSVector4i a(0,0,2,4),b(4,0,8,4),top(4,0,8,2),bottom(4,2,8,4),z_reject(4,2,6,4),unknown(6,0,8,1),gap(2,0,4,4);
        alignas(32) GSVertex verts[6]{};const u16 indices[6]={0,1,2,1,3,2};
        // Four vertices are sufficient; the six-entry storage avoids alignment
        // edge effects in production upload code without growing a draw buffer.
        const auto draw=[&](const GSVector4i& box,GSTextureVK* ds,u32 rgba,bool date,GSDateStencilAlpha known,bool fba,bool discard,bool reject_z,bool write_z)
        {
            for(u32 i=0;i<4;i++)
            {
                auto& v=verts[i];v={};v.XYZ.X=(i&1?box.z:box.x)*16;v.XYZ.Y=(i&2?box.w:box.y)*16;
                v.XYZ.Z=reject_z?0x40000000u:0xc0000000u;
                v.RGBAQ.R=rgba;v.RGBAQ.G=rgba>>8;v.RGBAQ.B=rgba>>16;v.RGBAQ.A=rgba>>24;v.RGBAQ.Q=1.f;
            }
            GSHWDrawConfig config{};config.rt=target.get();config.ds=ds;config.verts=verts;config.indices=indices;
            config.nverts=4;config.nindices=6;config.indices_per_prim=3;config.topology=GSHWDrawConfig::Topology::Triangle;
            config.drawarea=config.samplearea=box;config.scissor=target->GetRect();
            config.ps.tfx=4;config.ps.no_color1=1;config.ps.rta_correction=rta;config.ps.fba=fba;
            config.colormask.wrgba=15;config.datm=mode;config.date_stencil_alpha=known;
            config.destination_alpha=date?GSHWDrawConfig::DestinationAlphaMode::StencilOne:GSHWDrawConfig::DestinationAlphaMode::Off;
            config.depth=GSHWDrawConfig::DepthStencilSelector::NoDepth();config.depth.zwe=write_z;
            config.depth.ztst=reject_z?ZTST_GREATER:ZTST_ALWAYS;config.depth.date=date;config.depth.date_one=date;
            if(discard){config.ps.atst=GSHWDrawConfig::PS_ATST::GEQUAL;config.ps.afail=GSHWDrawConfig::PS_AFAIL::KEEP;
                config.cb_ps.FogColor_AREF.a=64.f;config.alpha_test=GSHWDrawConfig::AlphaTestMode::KEEP;}
            config.cb_vs.vertex_scale=GSVector2(1.f/64.f,1.f/32.f);config.cb_vs.vertex_offset=GSVector2(1.f,1.f);config.cb_vs.max_depth=0xffffffffu;
            DoRenderHW(config);++total_draws;
        };
        const u8 opposite_raw=inverse?160:32;
        const GSDateStencilAlpha opposite=inverse?GSDateStencilAlpha::Set:GSDateStencilAlpha::Clear;
        const GSDateStencilAlpha passing=inverse?GSDateStencilAlpha::Clear:GSDateStencilAlpha::Set;
        const u32 first=(u32(opposite_raw)<<24)|0x00705030u;
        const u32 second=(u32(opposite_raw)<<24)|0x00584499u;
        const u32 repeated=(u32(opposite_raw)<<24)|0x0055aaeeu;
        real_begin=vkCmdBeginRenderPass;setup_pass=m_date_setup_render_pass;setup_count=0;full_setup=true;
        {
            ScopedGuard restore_begin([&](){vkCmdBeginRenderPass=real_begin;});vkCmdBeginRenderPass=&Begin;
            draw(a,depth.get(),first,true,opposite,false,false,false,false);
            // A successful Off draw changes B's upper half to the passing bit.
            // FBA1 produces 0xa0 from alpha32 (or saturated255 in RTA storage).
            draw(top,depth.get(),0x20335577u,false,passing,!inverse,false,false,true);
            // Neither rejected writer may modify alpha, depth, or its mirror.
            draw(bottom,depth.get(),0x20abcdefu,false,passing,!inverse,true,false,true);
            draw(z_reject,depth.get(),0x20fedcbau,false,passing,!inverse,false,true,true);
            draw(b,depth.get(),second,true,opposite,false,false,false,false);
            draw(b,depth.get(),repeated,true,opposite,false,false,false,false);
            check(setup_count==1,"GS physical DATE full-target copy is reused across known alpha writes and rejected fragments");
            check(full_setup,"GS physical DATE setup covers disjoint box B before first draw limited to box A");
            // An explicitly unknown writer must invalidate the copy even though
            // this fixture knows its shader's constant output independently.
            const u8 passing_raw=inverse?32:128;
            draw(unknown,depth.get(),(u32(passing_raw)<<24)|0x00a08060u,false,GSDateStencilAlpha::Unknown,false,false,false,false);
            draw(b,depth.get(),second,true,opposite,false,false,false,false);
            check(setup_count==2,"GS physical unknown alpha writer forces one DATE setup rebuild");
            EndRenderPass();draw(a,depth.get(),repeated,true,opposite,false,false,false,false);
            check(setup_count==3,"GS physical pass boundary invalidates DATE stencil-copy ownership");
            draw(gap,other_depth.get(),first,true,opposite,false,false,false,false);
            check(setup_count==4,"GS physical DS switch rebuilds DATE stencil copy for the new attachment");
        }
        EndRenderPass();
        // Final image literals, independently enumerated from the ten draw
        // descriptions above. No copy-validity or alpha-classification model
        // computes the oracle. Every channel of every one of32 pixels matters.
        static constexpr std::array<u32,32> normal_pixels={
            0x203f2111,0x20705030,0x203f2111,0x20705030,0x20584499,0x20584499,0x20584499,0x20584499,
            0x203f2111,0x20705030,0x203f2111,0x20705030,0x20584499,0x20584499,0x20584499,0x20584499,
            0x203f2111,0x20705030,0x203f2111,0x20705030,0x203f2111,0x20584499,0x203f2111,0x20584499,
            0x203f2111,0x20705030,0x203f2111,0x20705030,0x203f2111,0x20584499,0x203f2111,0x20584499};
        static constexpr std::array<u32,32> inverse_pixels={
            0xa0705030,0xc03f2111,0xa0705030,0xc03f2111,0xa0584499,0xa0584499,0xa0584499,0xa0584499,
            0xa0705030,0xc03f2111,0xa0705030,0xc03f2111,0xa0584499,0xa0584499,0xa0584499,0xa0584499,
            0xa0705030,0xc03f2111,0xa0705030,0xc03f2111,0xa0584499,0xc03f2111,0xa0584499,0xc03f2111,
            0xa0705030,0xc03f2111,0xa0705030,0xc03f2111,0xa0584499,0xc03f2111,0xa0584499,0xc03f2111};
        expected=inverse?inverse_pixels:normal_pixels;
        // Raw32 -> RTA UNORM storage: round((32 /128)*255) ==64.
        if(rta)for(auto& pixel:expected)pixel=(pixel&0xffffffu)|0x40000000u;
        const auto rgba=download(*target),z=download(*depth),other_z=download(*other_depth);
        bool exact=rgba.size()==expected.size()&&std::equal(rgba.begin(),rgba.end(),expected.begin());
        if(!exact&&rgba.size()==32)
            for(u32 i=0;i<32;i++)if(rgba[i]!=expected[i]){std::fprintf(stderr,"GS_DATE_PIXEL mode=%u at=%u got=%08x expected=%08x setups=%u\n",u32(mode),i,rgba[i],expected[i],setup_count);break;}
        check(exact,"GS physical DATE mirror preserves exact literal RGBA through KEEP discard depth reject and repeated DATE reject");
        bool depth_exact=z.size()==32&&other_z.size()==32;
        for(u32 y=0;y<4&&depth_exact;y++)for(u32 x=0;x<8;x++)
        {
            const bool top_write=x>=4&&y<2;const u32 at=y*8+x;
            if(m_depth_format==VK_FORMAT_D24_UNORM_S8_UINT)
            {
                const u32 value=z[at]&0xffffffu,other_value=other_z[at]&0xffffffu;
                const u32 expected_z=top_write?0xbfffffu:0x800000u;
                depth_exact&=value>=expected_z-1&&value<=expected_z+1&&other_value>=0x7fffffu&&other_value<=0x800001u;
            }
            else depth_exact&=z[at]==(top_write?0x3f400000u:0x3f000000u)&&other_z[at]==0x3f000000u;
        }
        check(depth_exact,"GS physical alpha-discard and Z-reject leave literal depth unchanged while accepted writer stores three-quarter depth");
        std::printf("GS_DATE_STENCIL mode=%u draws=10 setupPasses=%u fullTarget=%u rgba=%u depth=%u\n",u32(mode),setup_count,full_setup,exact,depth_exact);
    }
    check(total_draws==30,"GS DATE GPU suite executes exactly thirty explicit small draws across three DATM representations");
}
