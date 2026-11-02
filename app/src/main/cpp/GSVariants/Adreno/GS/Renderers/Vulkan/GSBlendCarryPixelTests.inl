// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "GS/Renderers/Common/GSVertex.h"
extern "C" bool EmuCoreXPrepareBlendCarryGPU(GSTexture*,const GSVertex*,const u16*,GSHWDrawConfig*,GSHWDrawConfig*,bool);
namespace GSBlendCarryGPU
{
static thread_local PFN_vkCmdBindPipeline bind=nullptr;
static thread_local PFN_vkCmdDrawIndexed draw=nullptr;
static thread_local u32 binds=0,draws=0;
static VKAPI_ATTR void VKAPI_CALL Bind(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipeline pipeline)
{
    if(point==VK_PIPELINE_BIND_POINT_GRAPHICS)++binds;
    bind(cb,point,pipeline);
}
static VKAPI_ATTR void VKAPI_CALL Draw(VkCommandBuffer cb,u32 count,u32 instances,u32 first,s32 offset,u32 firstInstance)
{
    ++draws;draw(cb,count,instances,first,offset,firstInstance);
}
}
void GSDeviceVK::TestBlendCarryPixels(void (*check)(bool,const char*))
{
    check(m_features.dual_source_blend==bool(m_device_features.dualSrcBlend),
        "GS blend GPU proof restores effective real enabled dual-source capability after all structural models");
    if(m_features.dual_source_blend!=bool(m_device_features.dualSrcBlend))return;
    // Never manufacture hardware support or submit SRC1 on a device without it.
    if(!m_features.dual_source_blend)
    {
        std::fprintf(stderr,"UNAVAILABLE GS canonical carried TFX GPU proof: physical effective dual-source disabled\n");
        return;
    }
    check(m_allocator&&m_null_texture&&!m_global_descriptor_pool,
        "GS blend GPU proof starts with real restart resources and no synthetic global descriptors");
    if(!m_allocator||!m_null_texture||m_global_descriptor_pool)return;
    EndRenderPass();
    VkPipeline oldDonorPipeline=VK_NULL_HANDLE;
    const auto oldTimestamp=m_timestamp_query_pool,oldStatistics=m_pipeline_statistics_query_pool;
    const auto oldPoint=m_point_sampler,oldLinear=m_linear_sampler,oldTFXSampler=m_tfx_sampler;
    const auto oldTFXSamplerKey=m_tfx_sampler_sel;
    std::vector<u32> samplerKeys;for(const auto& item:m_samplers)samplerKeys.push_back(item.first);
    ScopedGuard cleanup=[&](){
        EndRenderPass();ExecuteCommandBuffer(true);
        if(oldDonorPipeline)vkDestroyPipeline(m_device,oldDonorPipeline,nullptr);
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
        m_vertex_stream_buffer.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|(m_features.vs_expand?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:0),65536,"blend-proof-vertex")&&
        m_expand_index_stream_buffer.Create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,m_features.aa1?INDEX_BUFFER_SIZE:4,"blend-proof-expansion")&&
        m_vertex_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,65536,"blend-proof-vs-ubo")&&
        m_fragment_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,65536,"blend-proof-ps-ubo")&&
        CreatePersistentDescriptorSets();
    check(initialized,"GS blend GPU proof creates actual TFX vertex streams dynamic UBO descriptors and physical layout bindings");
    if(!initialized)return;
    InitializeState();
    const u16 indices[9]={0,1,2,3,4,5,6,7,8};
    bool prepared=true,pixels=true,literals=true,counted=true,oldDonorSame=true;
    u32 donorIdealMaxError=0;
    u32 legacyBinds=0,carriedBinds=0,totalDraws=0;
    // Independent channel values exercise zero, saturated endpoints and ordinary
    // colors. Full alpha128 overlap must produce exact source pixels in all lanes.
    const u8 palettes[3][4][3]={
        {{32,96,160},{64,128,192},{96,160,224},{32,96,160}},
        {{0,255,0},{255,0,255},{0,255,255},{255,0,0}},
        {{252,4,128},{0,252,4},{252,0,128},{4,252,0}}};
    for(const auto& colors:palettes)
    {
        alignas(32) GSVertex vertices[36]{};
        for(u32 d=0;d<4;++d)for(u32 v=0;v<9;++v)
        {
            auto& vertex=vertices[d*9+v];vertex.RGBAQ.R=colors[d][0];vertex.RGBAQ.G=colors[d][1];vertex.RGBAQ.B=colors[d][2];
            vertex.RGBAQ.A=(d==1||d==2)?128:(v<3?32:v<6?64:96);vertex.RGBAQ.Q=1.f;
            vertex.XYZ.X=(v%3==1?128:0)+(v>=6?2048:0);
            vertex.XYZ.Y=(v%3==2?128:0)+(v<3?2048:0);
        }
        auto target=GSTextureVK::Create(GSTexture::RenderTarget,GSTexture::Format::Color,4,4,1);
        check(bool(target),"GS blend GPU proof allocates a canonical UNORM32 render target");
        if(!target)return;
        std::array<GSHWDrawConfig,4> legacy,carried;
        const bool ready=EmuCoreXPrepareBlendCarryGPU(target.get(),vertices,indices,legacy.data(),carried.data(),false)&&carried[1].blend.enable&&carried[2].blend.enable;
        prepared&=ready;if(!ready)continue;
        // Compile the unchanged pre-fix donor expression through the actual
        // complete canonical pipeline builder. No handwritten substitute shader
        // or global device feature override participates in this control.
        PipelineSelector oldSelector{};UpdateHWPipelineSelector(legacy[0],oldSelector);
        if(!oldDonorPipeline)
        {
            const std::string expression="Color.rgb = C == 1.0f ? A : ((A - B) * C_clamped + D) - (124.0f/256.0f);";
            const auto offset=m_tfx_source.find(expression);
            if(offset==std::string::npos)oldDonorSame=false;
            else
            {
                std::string source=m_tfx_source;
                auto shaders=std::move(m_tfx_fragment_shaders);m_tfx_fragment_shaders.clear();
                ScopedGuard restoreSource=[&](){
                    for(const auto& shader:m_tfx_fragment_shaders)if(shader.second)vkDestroyShaderModule(m_device,shader.second,nullptr);
                    m_tfx_fragment_shaders=std::move(shaders);m_tfx_source=std::move(source);
                };
                m_tfx_source.replace(offset,expression.size(),"Color.rgb = ((A - B) * C_clamped + D) - (124.0f/256.0f);");
                oldDonorPipeline=CreateTFXPipelineWithCache(oldSelector,VK_NULL_HANDLE,false);
            }
        }
        std::vector<u8> oldDonorPixels;
        if(oldDonorPipeline)
        {
            const auto rect=target->GetRect();u32 clear[16];std::fill(std::begin(clear),std::end(clear),0x40403010u);
            if(target->Update(rect,clear,16))
            {
                InitializeState();
                const auto hot=m_hot_tfx_pipelines;
                {
                    ScopedGuard restoreHot=[&](){m_hot_tfx_pipelines=hot;};
                    m_hot_tfx_pipelines.Insert(oldSelector,oldDonorPipeline);DoRenderHW(legacy[0]);
                }
                EndRenderPass();auto download=GSDownloadTextureVK::Create(4,4,GSTexture::Format::Color);
                if(download)
                {
                    download->CopyFromTexture(rect,target.get(),rect,0);download->Flush();
                    if(download->Map(rect))
                    {
                        oldDonorPixels.resize(64);
                        for(u32 y=0;y<4;++y)std::memcpy(oldDonorPixels.data()+y*16,download->GetMapPointer()+y*download->GetMapPitch(),16);
                        download->Unmap();
                    }
                }
            }
        }
        oldDonorSame&=oldDonorPixels.size()==64;

        for(u32 prefix=1;prefix<=4;++prefix)
        {
            std::vector<u8> reference;
            for(bool carry:{false,true})
            {
                const u32 initial=0x40403010;u32 initialPixels[16];std::fill(std::begin(initialPixels),std::end(initialPixels),initial);
                const auto rect=target->GetRect();
                if(!target->Update(rect,initialPixels,16)){pixels=false;continue;}
                InitializeState();
                auto& selected=carry?carried:legacy;
                GSBlendCarryGPU::bind=vkCmdBindPipeline;GSBlendCarryGPU::draw=vkCmdDrawIndexed;
                GSBlendCarryGPU::binds=GSBlendCarryGPU::draws=0;
                {
                    ScopedGuard restore=[&](){vkCmdBindPipeline=GSBlendCarryGPU::bind;vkCmdDrawIndexed=GSBlendCarryGPU::draw;};
                    vkCmdBindPipeline=&GSBlendCarryGPU::Bind;vkCmdDrawIndexed=&GSBlendCarryGPU::Draw;
                    for(u32 d=0;d<prefix;++d)DoRenderHW(selected[d]);
                }
                const auto binds=GSBlendCarryGPU::binds,draws=GSBlendCarryGPU::draws;
                counted&=draws==prefix;totalDraws+=draws;
                if(prefix==4){if(carry)carriedBinds+=binds;else legacyBinds+=binds;counted&=binds==(carry?1u:3u);}
                EndRenderPass();
                auto download=GSDownloadTextureVK::Create(4,4,GSTexture::Format::Color);
                if(!download){pixels=false;continue;}
                download->CopyFromTexture(rect,target.get(),rect,0);download->Flush();
                if(!download->Map(rect)){pixels=false;continue;}
                std::vector<u8> output(64);
                for(u32 y=0;y<4;++y)std::memcpy(output.data()+y*16,download->GetMapPointer()+y*download->GetMapPitch(),16);
                download->Unmap();
                if(!carry)reference=output;else pixels&=output==reference;
                if(!carry&&prefix==1)oldDonorSame&=output==oldDonorPixels;
                u32 expected[3]={16,48,64};
                for(u32 d=0;d<prefix;++d)for(u32 c=0;c<3;++c)
                    expected[c]=(d==1||d==2)?colors[d][c]:(u32(colors[d][c])+expected[c])/2;
                const u8 expectedAlpha=(prefix==2||prefix==3)?128:64;
                if((prefix==2||prefix==3)&&(output[0]!=expected[0]||output[1]!=expected[1]||output[2]!=expected[2]||output[3]!=expectedAlpha))
                    std::fprintf(stderr,"GS_BLEND_GPU_PIXEL prefix=%u carried=%u rgba=%u,%u,%u,%u expected=%u,%u,%u,%u binds=%u draws=%u\n",prefix,unsigned(carry),output[0],output[1],output[2],output[3],expected[0],expected[1],expected[2],expectedAlpha,binds,draws);
                for(u32 at=0;at<64;at+=4)
                {
                    for(u32 c=0;c<3;++c)
                    {
                        if(prefix==2||prefix==3)literals&=output[at+c]==expected[c];
                        else if(!carry)donorIdealMaxError=std::max(donorIdealMaxError,u32(std::abs(int(output[at+c])-int(expected[c]))));
                    }
                    literals&=output[at+3]==expectedAlpha;
                }
            }
        }
    }
    // Exhaustive endpoint colors share one physical16x16 image and one actual
    // canonical indexed draw per stream. Only geometry/viewport constants vary
    // from the actual captured identities; shader and blend selectors do not.
    bool endpoints=true;
    auto endpointTarget=GSTextureVK::Create(GSTexture::RenderTarget,GSTexture::Format::Color,16,16,1);
    check(bool(endpointTarget),"GS blend GPU proof allocates exhaustive256-color canonical endpoint image");
    if(endpointTarget)
    {
        alignas(32) GSVertex seed[36]{};
        for(u32 d=0;d<4;++d)for(u32 v=0;v<9;++v)
        {
            auto& q=seed[d*9+v];q.RGBAQ.R=64;q.RGBAQ.G=128;q.RGBAQ.B=192;q.RGBAQ.Q=1.f;
            q.RGBAQ.A=(d==1||d==2)?128:(v<3?32:v<6?64:96);
            q.XYZ.X=(v%3==1?128:0)+(v>=6?2048:0);q.XYZ.Y=(v%3==2?128:0)+(v<3?2048:0);
        }
        std::array<GSHWDrawConfig,4> legacy,carried;
        const bool ready=EmuCoreXPrepareBlendCarryGPU(endpointTarget.get(),seed,indices,legacy.data(),carried.data(),false)&&carried[1].blend.enable&&carried[2].blend.enable;
        prepared&=ready;
        if(ready)
        {
            std::vector<GSVertex> tiles(256*6);std::vector<u16> tileIndices(tiles.size());
            const u16 dx[6]={0,16,0,16,16,0},dy[6]={0,0,16,0,16,16};
            for(u32 color=0;color<256;++color)for(u32 v=0;v<6;++v)
            {
                const u32 at=color*6+v;auto& vertex=tiles[at];
                vertex.RGBAQ.R=color;vertex.RGBAQ.G=255-color;vertex.RGBAQ.B=color^0x55;vertex.RGBAQ.A=128;vertex.RGBAQ.Q=1.f;
                vertex.XYZ.X=(color%16)*16+dx[v];vertex.XYZ.Y=(color/16)*16+dy[v];tileIndices[at]=u16(at);
            }
            std::vector<u8> reference;
            for(bool carry:{false,true})
            {
                u32 clear[256];std::fill(std::begin(clear),std::end(clear),0x47182a63u);
                const auto rect=endpointTarget->GetRect();
                if(!endpointTarget->Update(rect,clear,64)){endpoints=false;continue;}
                InitializeState();auto config=carry?carried[1]:legacy[1];
                config.verts=tiles.data();config.indices=tileIndices.data();config.nverts=config.nindices=u32(tiles.size());
                config.cb_vs.vertex_scale=GSVector2(1.f/128.f,1.f/128.f);
                DoRenderHW(config);EndRenderPass();
                auto download=GSDownloadTextureVK::Create(16,16,GSTexture::Format::Color);
                if(!download){endpoints=false;continue;}
                download->CopyFromTexture(rect,endpointTarget.get(),rect,0);download->Flush();
                if(!download->Map(rect)){endpoints=false;continue;}
                std::vector<u8> output(1024);
                for(u32 y=0;y<16;++y)std::memcpy(output.data()+y*64,download->GetMapPointer()+y*download->GetMapPitch(),64);
                download->Unmap();
                if(!carry)reference=output;else endpoints&=output==reference;
                for(u32 color=0;color<256;++color)
                {
                    const u8* actual=output.data()+color*4;
                    const bool exact=actual[0]==color&&actual[1]==255-color&&actual[2]==(color^0x55)&&actual[3]==128;
                    if(!exact)std::fprintf(stderr,"GS_BLEND_ENDPOINT color=%u carry=%u rgba=%u,%u,%u,%u\n",color,unsigned(carry),actual[0],actual[1],actual[2],actual[3]);
                    endpoints&=exact;
                }
            }
            // Smooth shading supplies fractional Cs while As stays exactly128.
            // The endpoint must preserve the same interpolated UNORM bytes, not
            // only the integer flat-color cases above.
            bool gradient=true;alignas(32) GSVertex smooth[3]{};
            const u8 rgb[3][3]={{0,10,26},{254,32,64},{128,248,192}};
            for(u32 v=0;v<3;++v)
            {
                smooth[v].RGBAQ.R=rgb[v][0];smooth[v].RGBAQ.G=rgb[v][1];smooth[v].RGBAQ.B=rgb[v][2];
                smooth[v].RGBAQ.A=128;smooth[v].RGBAQ.Q=1.f;smooth[v].XYZ.X=v==1?512:0;smooth[v].XYZ.Y=v==2?512:0;
            }
            std::array<GSHWDrawConfig,4> smoothLegacy,smoothCarried;
            const bool smoothReady=EmuCoreXPrepareBlendCarryGPU(endpointTarget.get(),seed,indices,smoothLegacy.data(),smoothCarried.data(),true);
            const bool declined=smoothReady&&smoothLegacy[1].ps.iip&&smoothLegacy[1].vs.iip&&smoothCarried[1].ps.iip&&smoothCarried[1].vs.iip&&
                !smoothLegacy[1].blend.enable&&!smoothCarried[1].blend.enable&&smoothLegacy[1].ps==smoothCarried[1].ps&&smoothLegacy[1].vs.key==smoothCarried[1].vs.key;
            check(declined,"GS actual IIP opaque stage and renderer submission preserve the unblended selector instead of adopting an unsafe fractional-color donor");
            gradient&=declined;
            std::vector<u8> smoothReference;
            const u16 smoothIndices[3]={0,1,2};
            for(u32 mode=0;smoothReady&&mode<3;++mode)
            {
                u32 clear[256];std::fill(std::begin(clear),std::end(clear),0x47182a63u);
                const auto rect=endpointTarget->GetRect();
                if(!endpointTarget->Update(rect,clear,64)){gradient=false;continue;}
                // Mode2 deliberately bypasses the policy only as a portable
                // counterexample witness. Its mismatch is not required: a
                // future GPU may implement exact fractional blend conversion.
                InitializeState();auto config=mode==0?smoothLegacy[1]:mode==1?smoothCarried[1]:carried[1];
                config.vs.iip=true;config.ps.iip=true;config.verts=smooth;config.indices=smoothIndices;config.nverts=config.nindices=3;
                config.cb_vs.vertex_scale=GSVector2(1.f/128.f,1.f/128.f);
                DoRenderHW(config);EndRenderPass();
                auto download=GSDownloadTextureVK::Create(16,16,GSTexture::Format::Color);
                if(!download){gradient=false;continue;}
                download->CopyFromTexture(rect,endpointTarget.get(),rect,0);download->Flush();
                if(!download->Map(rect)){gradient=false;continue;}
                std::vector<u8> output(1024);
                for(u32 y=0;y<16;++y)std::memcpy(output.data()+y*64,download->GetMapPointer()+y*download->GetMapPitch(),64);
                download->Unmap();
                if(mode==0)smoothReference=output;
                if(smoothReference.size()!=output.size()){gradient=false;continue;}
                if(mode!=0)
                {
                    if(mode==1)gradient&=output==smoothReference;
                    u32 differences=0;for(u32 pixel=0;pixel<256;++pixel)differences+=std::memcmp(output.data()+pixel*4,smoothReference.data()+pixel*4,4)!=0;
                    if(mode==2)std::printf("GS_BLEND_SMOOTH_UNSAFE policyBypassed=1 differingPixels=%u (diagnostic only)\n",differences);
                    for(u32 pixel=0;pixel<256;++pixel)if(std::memcmp(output.data()+pixel*4,smoothReference.data()+pixel*4,4))
                    {
                        const u8* a=output.data()+pixel*4;const u8* b=smoothReference.data()+pixel*4;
                        std::fprintf(stderr,"GS_BLEND_SMOOTH_DIFF mode=%u pixel=%u xy=%u,%u legacy=%u,%u,%u,%u actual=%u,%u,%u,%u\n",mode,pixel,pixel%16,pixel/16,b[0],b[1],b[2],b[3],a[0],a[1],a[2],a[3]);break;
                    }
                }
                bool varied=false;u32 alphaMin=255,alphaMax=0;
                for(u32 pixel=0;pixel<256;++pixel)
                {
                    if(mode<2)gradient&=output[pixel*4+3]==128;varied|=output[pixel*4]!=output[0];
                    alphaMin=std::min(alphaMin,u32(output[pixel*4+3]));alphaMax=std::max(alphaMax,u32(output[pixel*4+3]));
                }
                std::fprintf(stderr,"GS_BLEND_SMOOTH mode=%u first=%u,%u,%u,%u alphaMin=%u alphaMax=%u varied=%u vs=%x ps=%llx/%llx blend=%x verts=%u indices=%u scale=%f,%f\n",mode,output[0],output[1],output[2],output[3],alphaMin,alphaMax,unsigned(varied),config.vs.key,(unsigned long long)config.ps.key_hi,(unsigned long long)config.ps.key_lo,config.blend.key,config.nverts,config.nindices,config.cb_vs.vertex_scale.x,config.cb_vs.vertex_scale.y);
                if(mode<2)gradient&=varied;
            }
            check(gradient,"GS actual canonical smooth fractional-color alpha128 endpoint remains byte-exact to unblended legacy across the image");

        }
        else {endpoints=false;check(false,"GS smooth endpoint needs the actual captured renderer configurations");check(false,"GS IIP policy capture needs actual renderer configurations");}
    }
    else {endpoints=false;check(false,"GS smooth endpoint needs a real physical render target");check(false,"GS IIP policy capture needs a real physical render target");}
    check(oldDonorSame,"GS canonical nonendpoint donor pixels remain byte-exact to the frozen original rounding expression on the same physical GPU");
    check(endpoints,"GS actual canonical carried endpoint is byte-exact to unblended legacy and literal source for every RGB byte value0to255");
    // Existing biased hardware mix is an approximation: OLD legacy already
    // differs from ideal integer half-alpha by one on this physical driver.
    // It is never the identity golden; full legacy-vs-carried remains exact.
    check(donorIdealMaxError<=1,"GS existing legacy half-alpha hardware precision remains within its independently observed one-byte ideal bound");
    std::printf("GS_BLEND_LEGACY_PRECISION idealMaxError=%u identityTolerance=0\n",donorIdealMaxError);
    check(prepared,"GS blend GPU configs come from actual opaque blending stage and actual renderer carry submission with unchanged physical caps");
    check(pixels,"GS full canonical TFX DoRenderHW carried pixels equal legacy after each donor identity identity donor prefix");
    check(literals,"GS canonical TFX actual zero saturated and ordinary RGB pixels match literal alpha128 identities exactly");
    check(counted,"GS actual forwarded vkCmdBindPipeline calls drop three to one while all four canonical indexed draws remain per sequence");
    std::printf("GS_BLEND_GPU canonicalTFX=1 physicalDual=%u legacyBinds=%u carriedBinds=%u actualDraws=%u\n",
        unsigned(m_features.dual_source_blend),legacyBinds,carriedBinds,totalDraws);
}
