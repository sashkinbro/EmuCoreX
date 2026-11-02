// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unreferenced RED matrix. The future native-only device adapter must invoke
// the actual startup finalizer and source/render-pass builders, never a model.
#pragma once
namespace GSDriverSafetyOracle
{
    struct Input
    {
        u32 vendor=0x5143,version=0;
        GSVulkanRoad::Driver raw_driver=GSVulkanRoad::Driver::Unknown;
        std::string_view name="Adreno (TM) 740",driver_name,info;
        bool provoking_extension=true;
        VkFormat depth_format=VK_FORMAT_D32_SFLOAT_S8_UINT;
        bool stencil=true;
    };
    struct Observation
    {
        bool provoking_extension=false,provoking_feature=false;
        VkFormat depth_format=VK_FORMAT_UNDEFINED;
        bool stencil=false;
        // Actual complete source builder and captured actual render-pass
        // builder observations, not values copied from the expected policy.
        bool source_captured=false,source_provoking_last=false;
        bool depth_schema_captured=false;
        VkFormat depth_schema=VK_FORMAT_UNDEFINED;
        bool pipeline_captured=false,pipeline_provoking_last=false,pipeline_stencil=false;
        VkFormat pipeline_depth=VK_FORMAT_UNDEFINED;
        bool date_captured=false,date_stencil=false;VkFormat date_depth=VK_FORMAT_UNDEFINED;
    };
    struct Case
    {
        Input input;
        bool deny_provoking=false,deny_stencil=false;
        const char* label="";
    };
    constexpr u32 Version(u32 major,u32 minor,u32 patch=0)
    {
        return (major<<22)|(minor<<12)|patch;
    }
    template<typename ActualAdapter>
    void Run(ActualAdapter&& actual,void(*check)(bool,const char*))
    {
        using Driver=GSVulkanRoad::Driver;
        std::vector<Case> cases;
        // Literal identity positives and negatives. Provoking distrust follows
        // the RAW ID rule; old Turnip safety follows the resolved-driver rule.
        for(std::string_view name:{"Adreno (TM) 610","Adreno (TM) 740","Adreno (TM) 830"})
        {
            Input q;q.raw_driver=Driver::Qualcomm;q.name=name;q.version=Version(512,900);
            cases.push_back({q,true,false,"raw Qualcomm extension distrust"});
        }
        for(const auto& id:std::array<Input,5>{{
            {0x5143,Version(26,3),Driver::Turnip,"Adreno (TM) 740","Turnip",""},
            {0x13b5,Version(44,1),Driver::Arm,"Mali-G57","ARM","malisx2"},
            {0x1010,Version(1,2),Driver::Imagination,"PowerVR CXT","Imagination",""},
            {0x5143,Version(512,900),Driver::Unknown,"Adreno (TM) 740","Qualcomm proprietary",""},
            {0x13b5,Version(512,900),Driver::Qualcomm,"Mali-G57","Qualcomm",""}}})
            cases.push_back({id,false,false,"nonmatching raw identity keeps provoking"});
        Input absent;absent.raw_driver=Driver::Qualcomm;absent.version=Version(512,900);absent.provoking_extension=false;
        cases.push_back({absent,true,false,"extension absent remains absent"});
        for(const auto& item:std::array<std::pair<u32,bool>,7>{{
            {0,true},{Version(25,0),true},{Version(26,1,999),true},{Version(26,2),false},
            {Version(26,2,1),false},{Version(26,3),false},{Version(27,0),false}}})
        {
            Input tu;tu.raw_driver=Driver::Turnip;tu.driver_name="Turnip";tu.version=item.first;
            cases.push_back({tu,false,item.second,"Turnip exact26.2 boundary and unknown"});
        }
        Input hints;hints.version=Version(26,1);hints.driver_name="Mesa Turnip";hints.raw_driver=Driver::Unknown;
        cases.push_back({hints,false,true,"resolved unknown-ID Turnip hint old version"});
        hints.version=Version(26,2);cases.push_back({hints,false,false,"resolved unknown-ID Turnip hint fixed version"});
        Input qold;qold.raw_driver=Driver::Qualcomm;qold.version=Version(26,1);
        cases.push_back({qold,true,false,"Qualcomm old numeric version is not Turnip"});
        Input unknown;unknown.version=0;unknown.driver_name="Qualcomm";
        cases.push_back({unknown,false,false,"unknown rawID Qualcomm hint does not deny provoking"});
        Input foreign;foreign.vendor=0x13b5;foreign.name="Mali-G57";foreign.raw_driver=Driver::Turnip;
        foreign.driver_name="Turnip";foreign.version=Version(26,1);
        cases.push_back({foreign,false,false,"nonAdreno Turnip ID never disables stencil"});

        bool provoking=true,stencil=true,negative=true,schema=true,precision=true;
        u64 observed=0;
        for(const auto& item:cases)for(VkFormat format:{VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT})
        {
            auto in=item.input;in.depth_format=format;in.stencil=format!=VK_FORMAT_D32_SFLOAT;
            const auto out=actual(in);++observed;
            const bool expected_last=in.provoking_extension&&!item.deny_provoking;
            const VkFormat expected_depth=item.deny_stencil?VK_FORMAT_D32_SFLOAT:in.depth_format;
            const bool expected_stencil=in.stencil&&!item.deny_stencil;
            const bool last_ok=out.provoking_extension==expected_last&&out.provoking_feature==expected_last;
            const bool depth_ok=out.depth_format==expected_depth&&out.stencil==expected_stencil;
            if(item.deny_provoking)provoking&=last_ok;else negative&=last_ok;
            if(item.deny_stencil)stencil&=depth_ok;else{negative&=depth_ok;precision&=out.depth_format==in.depth_format&&out.stencil==in.stencil;}
            schema&=out.source_captured&&out.source_provoking_last==out.provoking_feature&&
                out.depth_schema_captured&&out.depth_schema==out.depth_format&&
                out.pipeline_captured&&out.pipeline_provoking_last==out.provoking_feature&&out.pipeline_depth==out.depth_format&&out.pipeline_stencil==out.stencil&&
                out.date_captured&&out.date_depth==out.depth_format&&out.date_stencil==out.stencil;
            if(!last_ok||!depth_ok)
                std::printf("GS_DRIVER_SAFETY case=%s vendor=%x driver=%u version=%x inFormat=%u gotLast=%u/%u expectedLast=%u gotDepth=%u/%u expectedDepth=%u/%u\n",
                    item.label,in.vendor,static_cast<unsigned>(in.raw_driver),in.version,static_cast<unsigned>(in.depth_format),
                    unsigned(out.provoking_extension),unsigned(out.provoking_feature),unsigned(expected_last),
                    static_cast<unsigned>(out.depth_format),unsigned(out.stencil),static_cast<unsigned>(expected_depth),unsigned(expected_stencil));
        }
        check(provoking,"GS actual Qualcomm raw-ID capability finalizer distrusts provoking LAST only on the affected driver");
        check(stencil,"GS actual old or unknown-version Turnip uses32bit nonstencil depth before DATE discard pipelines");
        check(negative,"GS driver safety identity hint vendor and exact26.2 boundary negative controls retain existing capabilities");
        check(schema,"GS finalized extension feature complete shader source and actual render-pass depth schema agree");
        check(precision,"GS unaffected drivers preserve existingD32S8 D24S8 andD32 format fallbacks without precision downgrade");
        std::printf("GS_DRIVER_SAFETY cases=%llu\n",static_cast<unsigned long long>(observed));
    }
}


extern "C" void EmuCoreXOracleProvokingFallback(void (*check)(bool,const char*));
namespace GSDriverSafetyOracle
{
    struct PassFacts{VkFormat depth=VK_FORMAT_UNDEFINED;bool stencil=false;};
    static thread_local std::unordered_map<VkRenderPass,PassFacts> capturedPasses;
    static thread_local uintptr_t passSerial=0x63150000;
    static thread_local bool pipelineCaptured=false,pipelineLast=false,pipelineStencil=false;
    static thread_local VkRenderPass pipelinePass=VK_NULL_HANDLE;
    static constexpr uintptr_t FakeVS=0x63160000,FakeFS=0x63170000;
    static VKAPI_ATTR VkResult VKAPI_CALL CapturePass(VkDevice,const VkRenderPassCreateInfo* info,
        const VkAllocationCallbacks*,VkRenderPass* result)
    {
        PassFacts facts;
        if(info->subpassCount==1&&info->pSubpasses[0].pDepthStencilAttachment)
        {
            const u32 at=info->pSubpasses[0].pDepthStencilAttachment->attachment;
            if(at<info->attachmentCount)
            {
                const auto& depth=info->pAttachments[at];facts.depth=depth.format;
                facts.stencil=depth.stencilLoadOp!=VK_ATTACHMENT_LOAD_OP_DONT_CARE||depth.stencilStoreOp!=VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }
        }
        passSerial+=16;*result=reinterpret_cast<VkRenderPass>(passSerial);capturedPasses.emplace(*result,facts);return VK_SUCCESS;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL CapturePipeline(VkDevice,VkPipelineCache,u32 count,
        const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* result)
    {
        pipelineCaptured=count==1&&info->stageCount==2;pipelinePass=info->renderPass;
        bool vs=false,fs=false;
        for(u32 n=0;n<info->stageCount;++n)
        {
            vs|=info->pStages[n].stage==VK_SHADER_STAGE_VERTEX_BIT&&info->pStages[n].module==reinterpret_cast<VkShaderModule>(FakeVS);
            fs|=info->pStages[n].stage==VK_SHADER_STAGE_FRAGMENT_BIT&&info->pStages[n].module==reinterpret_cast<VkShaderModule>(FakeFS);
        }
        pipelineCaptured&=vs&&fs;pipelineLast=false;
        if(info->pRasterizationState)for(auto p=static_cast<const VkBaseInStructure*>(info->pRasterizationState->pNext);p;p=p->pNext)
            if(p->sType==VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT)
                pipelineLast=reinterpret_cast<const VkPipelineRasterizationProvokingVertexStateCreateInfoEXT*>(p)->provokingVertexMode==VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;
        pipelineStencil=info->pDepthStencilState&&info->pDepthStencilState->stencilTestEnable;
        // Structural capture ends here: no fake shader/pass object reaches the
        // driver, object naming, command recording or destruction.
        *result=VK_NULL_HANDLE;return VK_SUCCESS;
    }
}
void GSDeviceVK::TestDriverSafety(void (*check)(bool,const char*))
{
    using namespace GSDriverSafetyOracle;
    const auto properties=m_device_properties;const auto driverProperties=m_device_driver_properties;
    const auto optional=m_optional_extensions;const auto features=m_features;const auto facts=m_road_facts;
    const auto depth=m_depth_format;const auto create=vkCreateRenderPass;const auto createPipeline=vkCreateGraphicsPipelines;
    auto passes=std::move(m_render_pass_cache);m_render_pass_cache.clear();
    auto vertexShaders=std::move(m_tfx_vertex_shaders);m_tfx_vertex_shaders.clear();
    auto fragmentShaders=std::move(m_tfx_fragment_shaders);m_tfx_fragment_shaders.clear();
    std::array<u8,sizeof(m_tfx_render_pass)> tfxPasses;std::memcpy(tfxPasses.data(),&m_tfx_render_pass,tfxPasses.size());
    const std::array<VkRenderPass,7> utility={m_utility_color_render_pass_load,m_utility_color_render_pass_clear,m_utility_color_render_pass_discard,
        m_utility_depth_render_pass_load,m_utility_depth_render_pass_clear,m_utility_depth_render_pass_discard,m_date_setup_render_pass};
    const auto clearModules=[&](){
        for(const auto& entry:m_tfx_vertex_shaders)if(entry.second&&entry.second!=reinterpret_cast<VkShaderModule>(FakeVS))vkDestroyShaderModule(m_device,entry.second,nullptr);
        for(const auto& entry:m_tfx_fragment_shaders)if(entry.second&&entry.second!=reinterpret_cast<VkShaderModule>(FakeFS))vkDestroyShaderModule(m_device,entry.second,nullptr);
        m_tfx_vertex_shaders.clear();m_tfx_fragment_shaders.clear();
    };
    ScopedGuard restore=[&](){
        vkCreateRenderPass=create;vkCreateGraphicsPipelines=createPipeline;m_render_pass_cache=std::move(passes);
        clearModules();m_tfx_vertex_shaders=std::move(vertexShaders);m_tfx_fragment_shaders=std::move(fragmentShaders);
        std::memcpy(&m_tfx_render_pass,tfxPasses.data(),tfxPasses.size());
        m_utility_color_render_pass_load=utility[0];m_utility_color_render_pass_clear=utility[1];m_utility_color_render_pass_discard=utility[2];
        m_utility_depth_render_pass_load=utility[3];m_utility_depth_render_pass_clear=utility[4];m_utility_depth_render_pass_discard=utility[5];m_date_setup_render_pass=utility[6];
        m_device_properties=properties;m_device_driver_properties=driverProperties;
        m_optional_extensions=optional;m_features=features;m_road_facts=facts;m_depth_format=depth;capturedPasses.clear();
    };
    bool flatChecked[2]{};
    Run([&](const Input& in){
        m_device_properties=properties;m_device_driver_properties={};m_optional_extensions=optional;m_features=features;
        m_device_properties.vendorID=in.vendor;m_device_properties.driverVersion=in.version;
        const auto copy=[](auto& destination,std::string_view source){
            const size_t size=std::min(source.size(),sizeof(destination)-1);if(size)std::memcpy(destination,source.data(),size);destination[size]=0;
        };
        copy(m_device_properties.deviceName,in.name);copy(m_device_driver_properties.driverName,in.driver_name);copy(m_device_driver_properties.driverInfo,in.info);
        m_optional_extensions.vk_khr_driver_properties=true;
        switch(in.raw_driver)
        {
            case GSVulkanRoad::Driver::Qualcomm:m_device_driver_properties.driverID=VK_DRIVER_ID_QUALCOMM_PROPRIETARY;break;
            case GSVulkanRoad::Driver::Turnip:m_device_driver_properties.driverID=VK_DRIVER_ID_MESA_TURNIP;break;
            case GSVulkanRoad::Driver::Arm:m_device_driver_properties.driverID=VK_DRIVER_ID_ARM_PROPRIETARY;break;
            case GSVulkanRoad::Driver::Imagination:m_device_driver_properties.driverID=VK_DRIVER_ID_IMAGINATION_PROPRIETARY;break;
            default:m_device_driver_properties.driverID=static_cast<VkDriverId>(0);break;
        }
        m_road_facts=GSVulkanRoad::ResolveFacts(GetRoadIdentity(),false,0);
        m_optional_extensions.vk_ext_provoking_vertex=in.provoking_extension;
        FinalizeDriverCapabilities(false);
        m_features.provoking_vertex_last=m_optional_extensions.vk_ext_provoking_vertex;
        m_depth_format=in.depth_format;m_features.stencil_buffer=in.stencil;FinalizeDriverCapabilities(true);
        Observation out;out.provoking_extension=m_optional_extensions.vk_ext_provoking_vertex;
        out.provoking_feature=m_features.provoking_vertex_last;out.depth_format=m_depth_format;out.stencil=m_features.stencil_buffer;
        const TFXShaderSourceCapabilities caps={m_features.rov,m_features.texture_barrier,
            m_features.texture_barrier&&UseFeedbackLoopLayout(),HasMobileGPUProfile(),m_features.provoking_vertex_last};
        const auto source=BuildTFXVertexShaderSource(m_tfx_source,{},caps);
        out.source_captured=source.find("#define VS_PROVOKING_VERTEX_LAST ")!=std::string::npos&&source.find("void main()")!=std::string::npos;
        out.source_provoking_last=source.find("#define VS_PROVOKING_VERTEX_LAST 1\n")!=std::string::npos;
        capturedPasses.clear();vkCreateRenderPass=&CapturePass;m_render_pass_cache.clear();
        const bool passOK=CreateRenderPasses();
        const auto date=capturedPasses.find(m_date_setup_render_pass);
        out.date_captured=passOK&&date!=capturedPasses.end();
        if(date!=capturedPasses.end()){out.date_depth=date->second.depth;out.date_stencil=date->second.stencil;}
        PipelineSelector selector{};selector.ds=true;selector.topology=static_cast<u32>(GSHWDrawConfig::Topology::Triangle);
        selector.ps.no_color=selector.ps.no_color1=true;selector.dss=GSHWDrawConfig::DepthStencilSelector::NoDepth();selector.dss.date=m_features.stencil_buffer;
        clearModules();m_tfx_vertex_shaders[selector.vs.key]=reinterpret_cast<VkShaderModule>(FakeVS);
        m_tfx_fragment_shaders[selector.ps]=reinterpret_cast<VkShaderModule>(FakeFS);
        pipelineCaptured=pipelineLast=pipelineStencil=false;pipelinePass=VK_NULL_HANDLE;vkCreateGraphicsPipelines=&CapturePipeline;
        CreateTFXPipelineWithCache(selector,VK_NULL_HANDLE,false);
        out.pipeline_captured=pipelineCaptured;out.pipeline_provoking_last=pipelineLast;out.pipeline_stencil=pipelineStencil;
        const auto used=capturedPasses.find(pipelinePass);
        if(used!=capturedPasses.end()){out.pipeline_depth=used->second.depth;out.depth_schema=used->second.depth;out.depth_schema_captured=true;}
        vkCreateRenderPass=create;vkCreateGraphicsPipelines=createPipeline;m_render_pass_cache.clear();clearModules();
        if(!flatChecked[out.provoking_feature])
        {flatChecked[out.provoking_feature]=true;EmuCoreXOracleProvokingFallback(check);}
        return out;
    },check);
}
