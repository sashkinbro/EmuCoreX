// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
namespace GSAlphaBitGPU
{
static thread_local PFN_vkCmdDrawIndexed realDraw=nullptr;
static thread_local u32 draws=0;
static VKAPI_ATTR void VKAPI_CALL Draw(VkCommandBuffer cb,u32 count,u32 instances,u32 first,s32 offset,u32 instance)
{draws++;realDraw(cb,count,instances,first,offset,instance);}
}
void GSDeviceVK::TestAlphaBitPixels(void (*check)(bool,const char*))
{
    std::fprintf(stderr,"VK_ALPHA_PHYSICAL device=%s driver=%s logicOpEnabled=%u profileRoad=%u stencil=%u\n",
        m_device_properties.deviceName,m_device_driver_properties.driverName,m_device_features.logicOp,
        m_features.alpha_bit_logic_op,m_features.stencil_buffer);
    if(!m_device_features.logicOp)
    {
        check(!m_features.alpha_bit_logic_op,"VK physical unsupported logicOp retains production framebuffer-mask fallback");
        std::fprintf(stderr,"UNAVAILABLE VK physical alpha-bit mark pixels: logical device has no physical logicOp support; CPU pipeline proofs are separate evidence\n");
        return;
    }
    check(m_allocator&&m_null_texture&&!m_global_descriptor_pool,
        "VK alpha-bit GPU proof starts with actual restart resources and no synthetic descriptors");
    if(!m_allocator||!m_null_texture||m_global_descriptor_pool)return;
    EndRenderPass();
    const auto oldTimestamp=m_timestamp_query_pool,oldStatistics=m_pipeline_statistics_query_pool;
    const auto oldPoint=m_point_sampler,oldLinear=m_linear_sampler,oldTFXSampler=m_tfx_sampler;
    const auto oldTFXSamplerKey=m_tfx_sampler_sel;
    std::vector<u32> samplerKeys;for(const auto& item:m_samplers)samplerKeys.push_back(item.first);
    ScopedGuard cleanup=[&](){
        EndRenderPass();ExecuteCommandBuffer(true);
        if(m_tfx_ubo_descriptor_set)FreePersistentDescriptorSet(m_tfx_ubo_descriptor_set);
        m_tfx_ubo_descriptor_set=VK_NULL_HANDLE;
        m_vertex_stream_buffer.Destroy(false);m_expand_index_stream_buffer.Destroy(false);
        m_vertex_uniform_stream_buffer.Destroy(false);m_fragment_uniform_stream_buffer.Destroy(false);
        if(m_global_descriptor_pool)vkDestroyDescriptorPool(m_device,m_global_descriptor_pool,nullptr);
        m_global_descriptor_pool=VK_NULL_HANDLE;
        if(m_timestamp_query_pool!=oldTimestamp&&m_timestamp_query_pool)vkDestroyQueryPool(m_device,m_timestamp_query_pool,nullptr);
        m_timestamp_query_pool=oldTimestamp;
        if(m_pipeline_statistics_query_pool!=oldStatistics&&m_pipeline_statistics_query_pool)
            vkDestroyQueryPool(m_device,m_pipeline_statistics_query_pool,nullptr);
        m_pipeline_statistics_query_pool=oldStatistics;
        for(auto it=m_samplers.begin();it!=m_samplers.end();)
        {
            if(std::find(samplerKeys.begin(),samplerKeys.end(),it->first)==samplerKeys.end())
            {if(it->second)vkDestroySampler(m_device,it->second,nullptr);it=m_samplers.erase(it);}
            else ++it;
        }
        m_point_sampler=oldPoint;m_linear_sampler=oldLinear;m_tfx_sampler=oldTFXSampler;m_tfx_sampler_sel=oldTFXSamplerKey;
        m_current_pipeline=VK_NULL_HANDLE;InvalidateCachedState();
    };
    const bool initialized=CreateGlobalDescriptorPool()&&
        m_vertex_stream_buffer.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|(m_features.vs_expand?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:0),65536,"alpha-proof-vertex")&&
        m_expand_index_stream_buffer.Create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,m_features.aa1?INDEX_BUFFER_SIZE:4,"alpha-proof-expansion")&&
        m_vertex_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,65536,"alpha-proof-vs-ubo")&&
        m_fragment_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,65536,"alpha-proof-ps-ubo")&&
        CreatePersistentDescriptorSets();
    check(initialized,"GS alpha-bit GPU proof creates actual TFX vertex streams dynamic UBO descriptors and physical layout bindings");
    if(!initialized)return;
    InitializeState();
    alignas(32) GSVertex vertices[9]{};u16 indices[9]={0,1,2,3,4,5,6,7,8};
    for(u32 v=0;v<9;v++)
    {
        auto& p=vertices[v];p.RGBAQ.R=0xa1;p.RGBAQ.G=0xb2;p.RGBAQ.B=0xc3;p.RGBAQ.A=v<6?128:0;p.RGBAQ.Q=1.f;
        p.XYZ.X=v%3==1?128:0;p.XYZ.Y=v%3==2?128:0;
    }
    auto target=GSTextureVK::Create(GSTexture::RenderTarget,GSTexture::Format::Color,4,4,1);
    check(bool(target),"VK alpha-bit GPU proof allocates actual UNORM32 render target");if(!target)return;
    GSHWDrawConfig config{};config.rt=target.get();config.verts=vertices;config.indices=indices;
    config.nverts=config.nindices=9;config.indices_per_prim=3;config.topology=GSHWDrawConfig::Topology::Triangle;
    config.drawarea=config.samplearea=config.scissor=target->GetRect();config.depth=GSHWDrawConfig::DepthStencilSelector::NoDepth();
    config.ps.tfx=4;config.ps.no_color1=true;config.ps.fba=true;config.colormask.wrgba=8;
    config.cb_vs.vertex_scale=GSVector2(1.f/32.f,1.f/32.f);config.cb_vs.vertex_offset=GSVector2(1.f,1.f);config.cb_vs.max_depth=0xffffffff;
    bool set=true,clear=true,split=true,counted=true;u32 cases=0;
    for(u8 alpha:{u8(0),u8(1),u8(127),u8(128),u8(129),u8(255)})
    for(u32 variant=0;variant<4;variant++)
    {
        std::array<u32,16> seed;seed.fill((static_cast<u32>(alpha)<<24)|0x00332211u);
        if(!target->Update(target->GetRect(),seed.data(),16)){set=clear=split=false;continue;}
        InitializeState();
        config.colormask.logic_op=(variant==0||variant==2)?GSAlphaBitLogicOp::SetBit:GSAlphaBitLogicOp::ClearBit;
        config.logic_op_split=variant>=2?6:0;
        GSAlphaBitGPU::realDraw=vkCmdDrawIndexed;GSAlphaBitGPU::draws=0;
        {const ScopedGuard restoreDraw([&](){vkCmdDrawIndexed=GSAlphaBitGPU::realDraw;});vkCmdDrawIndexed=GSAlphaBitGPU::Draw;DoRenderHW(config);}
        const u32 draws=GSAlphaBitGPU::draws;EndRenderPass();
        auto download=GSDownloadTextureVK::Create(4,4,GSTexture::Format::Color);
        bool exact=bool(download);std::array<u8,64> image{};
        if(download){download->CopyFromTexture(target->GetRect(),target.get(),target->GetRect(),0);download->Flush();
            exact&=download->Map(target->GetRect());if(exact){for(u32 y=0;y<4;y++)std::memcpy(image.data()+y*16,download->GetMapPointer()+y*download->GetMapPitch(),16);download->Unmap();}}
        const u8 expected=(variant==0||variant==3)?static_cast<u8>(alpha|128u):static_cast<u8>(alpha&127u);
        for(size_t i=0;i<image.size();i+=4)exact&=image[i]==0x11&&image[i+1]==0x22&&image[i+2]==0x33&&image[i+3]==expected;
        counted&=draws==(variant>=2?2u:1u);
        if(variant==0)set&=exact;else if(variant==1)clear&=exact;else split&=exact;
        if(!exact)std::fprintf(stderr,"VK_ALPHA_PIXEL alpha=%u variant=%u rgba=%u/%u/%u/%u expectedA=%u draws=%u\n",alpha,variant,image[0],image[1],image[2],image[3],expected,draws);
        cases++;
    }
    check(set,"VK actual supported logic OR preserves RGB and lower alpha bits for every tested alpha boundary");
    check(clear,"VK actual supported logic AND_INVERTED preserves RGB and lower alpha bits for every tested alpha boundary");
    check(split,"VK actual DoRenderHW split mark order preserves independent set-clear and clear-set alpha results");
    check(counted&&cases==24,"VK actual alpha-bit marks submit one or two real indexed runs as requested");
    std::printf("VK_ALPHA_PIXELS cases=%u stockOrderedRoad=%u\n",cases,m_features.alpha_bit_logic_op);
    TestStencilCounterPixels(check);
}
