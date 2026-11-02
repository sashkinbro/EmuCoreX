// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native CPU-only Vulkan callback table; actual VMA and stream code execute.
// No synthetic handle or memory property is forwarded to a physical driver.
#include "GSStreamRingActivationTests.inl"
namespace GSStreamRingActivationOracle
{
struct Backend
{
    struct Buffer{VkDeviceSize size=0;u32 bits=0;VkDeviceMemory memory=VK_NULL_HANDLE;VkDeviceSize offset=0;};
    struct Memory{VkDeviceSize size=0;u32 type=0;size_t base=0;std::vector<u8> data;};
    static thread_local Backend* active;
    Backend* previous=nullptr;
    VkPhysicalDeviceProperties props{};VkPhysicalDeviceMemoryProperties types{};
    VmaAllocator allocator=VK_NULL_HANDLE;
    std::unordered_map<VkBuffer,std::unique_ptr<Buffer>> buffers;
    std::unordered_map<VkDeviceMemory,std::unique_ptr<Memory>> memories;
    std::vector<VkMappedMemoryRange> flushes;
    u32 bits=0,creates=0,allocationAttempts=0;bool healthy=true,failCached=false,failAll=false;
    Backend(const VkPhysicalDeviceProperties& properties,const u32* flags,u32 count):previous(active),props(properties)
    {
        active=this;props.apiVersion=VK_API_VERSION_1_1;props.limits.nonCoherentAtomSize=256;props.limits.minMemoryMapAlignment=64;
        props.limits.maxMemoryAllocationCount=4096;types.memoryTypeCount=count;types.memoryHeapCount=2;
        types.memoryHeaps[0]={512*1024,VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};types.memoryHeaps[1]={512*1024,0};
        for(u32 i=0;i<count;++i)types.memoryTypes[i]={flags[i],(flags[i]&1)?0u:1u};bits=(1u<<count)-1;
    }
    ~Backend(){Destroy();active=previous;}
    void Destroy(){if(allocator){vmaDestroyAllocator(allocator);allocator=VK_NULL_HANDLE;}}
    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL InstanceProc(VkInstance,const char*){return nullptr;}
    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DeviceProc(VkDevice,const char*){return nullptr;}
    static VKAPI_ATTR void VKAPI_CALL Properties(VkPhysicalDevice,VkPhysicalDeviceProperties* p){*p=active->props;}
    static VKAPI_ATTR void VKAPI_CALL MemoryProperties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* p){*p=active->types;}
    static VKAPI_ATTR void VKAPI_CALL MemoryProperties2(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties2* p){p->memoryProperties=active->types;}
    static VKAPI_ATTR VkResult VKAPI_CALL Allocate(VkDevice,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks*,VkDeviceMemory* result)
    {
        auto& b=*active;++b.allocationAttempts;
        if(info->memoryTypeIndex>=b.types.memoryTypeCount){b.healthy=false;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
        if(b.failAll||(b.failCached&&(b.types.memoryTypes[info->memoryTypeIndex].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)))return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        auto memory=std::make_unique<Memory>();memory->size=info->allocationSize;memory->type=info->memoryTypeIndex;
        memory->data.assign(size_t(memory->size)+512,0xcd);
        const uintptr_t raw=reinterpret_cast<uintptr_t>(memory->data.data());memory->base=size_t(((raw+255+64)&~uintptr_t(255))-raw);
        *result=reinterpret_cast<VkDeviceMemory>(memory.get());b.memories.emplace(*result,std::move(memory));return VK_SUCCESS;
    }
    static VKAPI_ATTR void VKAPI_CALL Free(VkDevice,VkDeviceMemory memory,const VkAllocationCallbacks*)
    {
        auto& b=*active;const auto at=b.memories.find(memory);if(at==b.memories.end()){b.healthy=false;return;}
        const auto& m=*at->second;
        b.healthy&=std::all_of(m.data.begin(),m.data.begin()+m.base,[](u8 x){return x==0xcd;})&&
            std::all_of(m.data.begin()+m.base+size_t(m.size),m.data.end(),[](u8 x){return x==0xcd;});b.memories.erase(at);
    }
    static VKAPI_ATTR VkResult VKAPI_CALL Map(VkDevice,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags,void** result)
    {
        auto& b=*active;const auto at=b.memories.find(memory);
        if(at==b.memories.end()||offset>at->second->size||(size!=VK_WHOLE_SIZE&&size>at->second->size-offset)){b.healthy=false;return VK_ERROR_MEMORY_MAP_FAILED;}
        *result=at->second->data.data()+at->second->base+size_t(offset);return VK_SUCCESS;
    }
    static VKAPI_ATTR void VKAPI_CALL Unmap(VkDevice,VkDeviceMemory){}
    static VKAPI_ATTR VkResult VKAPI_CALL Flush(VkDevice,u32 count,const VkMappedMemoryRange* ranges)
    {
        auto& b=*active;
        for(u32 i=0;i<count;++i)
        {
            const auto& r=ranges[i];const auto at=b.memories.find(r.memory);
            if(at==b.memories.end()){b.healthy=false;continue;}
            const auto size=at->second->size;
            b.healthy&=r.offset<=size&&r.offset%256==0&&
                (r.size==VK_WHOLE_SIZE||(r.size<=size-r.offset&&(r.size%256==0||r.offset+r.size==size)));
            b.flushes.push_back(r);
        }
        return VK_SUCCESS;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL Invalidate(VkDevice,u32,const VkMappedMemoryRange*){active->healthy=false;return VK_ERROR_FEATURE_NOT_PRESENT;}
    static VKAPI_ATTR VkResult VKAPI_CALL CreateBuffer(VkDevice,const VkBufferCreateInfo* info,const VkAllocationCallbacks*,VkBuffer* result)
    {
        auto& b=*active;++b.creates;auto buffer=std::make_unique<Buffer>();buffer->size=info->size;buffer->bits=b.bits;
        *result=reinterpret_cast<VkBuffer>(buffer.get());b.buffers.emplace(*result,std::move(buffer));return VK_SUCCESS;
    }
    static VKAPI_ATTR void VKAPI_CALL DestroyBuffer(VkDevice,VkBuffer buffer,const VkAllocationCallbacks*)
    {if(active->buffers.erase(buffer)!=1)active->healthy=false;}
    static VKAPI_ATTR void VKAPI_CALL Requirements(VkDevice,VkBuffer buffer,VkMemoryRequirements* result)
    {
        const auto at=active->buffers.find(buffer);if(at==active->buffers.end()){active->healthy=false;*result={};return;}
        *result={(at->second->size+255)&~VkDeviceSize(255),256,at->second->bits};
    }
    static VKAPI_ATTR void VKAPI_CALL Requirements2(VkDevice device,const VkBufferMemoryRequirementsInfo2* info,VkMemoryRequirements2* result)
    {
        Requirements(device,info->buffer,&result->memoryRequirements);
        for(auto p=static_cast<VkBaseOutStructure*>(result->pNext);p;p=p->pNext)if(p->sType==VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS)
        {auto* d=reinterpret_cast<VkMemoryDedicatedRequirements*>(p);d->requiresDedicatedAllocation=d->prefersDedicatedAllocation=VK_FALSE;}
    }
    static VKAPI_ATTR VkResult VKAPI_CALL Bind(VkDevice,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset)
    {
        auto& b=*active;const auto a=b.buffers.find(buffer);const auto m=b.memories.find(memory);
        if(a==b.buffers.end()||m==b.memories.end()||offset>m->second->size||a->second->size>m->second->size-offset){b.healthy=false;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
        a->second->memory=memory;a->second->offset=offset;return VK_SUCCESS;
    }
    static VKAPI_ATTR VkResult VKAPI_CALL Bind2(VkDevice device,u32 count,const VkBindBufferMemoryInfo* info)
    {for(u32 i=0;i<count;++i)if(Bind(device,info[i].buffer,info[i].memory,info[i].memoryOffset)!=VK_SUCCESS)return VK_ERROR_OUT_OF_DEVICE_MEMORY;return VK_SUCCESS;}
    static VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice,const VkImageCreateInfo*,const VkAllocationCallbacks*,VkImage*){active->healthy=false;return VK_ERROR_FEATURE_NOT_PRESENT;}
    static VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice,VkImage,const VkAllocationCallbacks*){active->healthy=false;}
    static VKAPI_ATTR void VKAPI_CALL ImageRequirements(VkDevice,VkImage,VkMemoryRequirements* out){active->healthy=false;*out={};}
    static VKAPI_ATTR void VKAPI_CALL ImageRequirements2(VkDevice,const VkImageMemoryRequirementsInfo2*,VkMemoryRequirements2* out){active->healthy=false;out->memoryRequirements={};}
    static VKAPI_ATTR VkResult VKAPI_CALL BindImage(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize){active->healthy=false;return VK_ERROR_FEATURE_NOT_PRESENT;}
    static VKAPI_ATTR VkResult VKAPI_CALL BindImage2(VkDevice,u32,const VkBindImageMemoryInfo*){active->healthy=false;return VK_ERROR_FEATURE_NOT_PRESENT;}
    static VKAPI_ATTR void VKAPI_CALL Copy(VkCommandBuffer,VkBuffer,VkBuffer,u32,const VkBufferCopy*){active->healthy=false;}
    bool Start(VkInstance instance,VkPhysicalDevice physical,VkDevice device)
    {
        VmaVulkanFunctions f{};f.vkGetInstanceProcAddr=InstanceProc;f.vkGetDeviceProcAddr=DeviceProc;
        f.vkGetPhysicalDeviceProperties=Properties;f.vkGetPhysicalDeviceMemoryProperties=MemoryProperties;f.vkGetPhysicalDeviceMemoryProperties2KHR=MemoryProperties2;
        f.vkAllocateMemory=Allocate;f.vkFreeMemory=Free;f.vkMapMemory=Map;f.vkUnmapMemory=Unmap;f.vkFlushMappedMemoryRanges=Flush;f.vkInvalidateMappedMemoryRanges=Invalidate;
        f.vkCreateBuffer=CreateBuffer;f.vkDestroyBuffer=DestroyBuffer;f.vkGetBufferMemoryRequirements=Requirements;f.vkGetBufferMemoryRequirements2KHR=Requirements2;f.vkBindBufferMemory=Bind;f.vkBindBufferMemory2KHR=Bind2;
        f.vkCreateImage=CreateImage;f.vkDestroyImage=DestroyImage;f.vkGetImageMemoryRequirements=ImageRequirements;f.vkGetImageMemoryRequirements2KHR=ImageRequirements2;f.vkBindImageMemory=BindImage;f.vkBindImageMemory2KHR=BindImage2;f.vkCmdCopyBuffer=Copy;
        VmaAllocatorCreateInfo ci{};ci.vulkanApiVersion=VK_API_VERSION_1_1;ci.flags=VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
        ci.instance=instance;ci.physicalDevice=physical;ci.device=device;ci.pVulkanFunctions=&f;ci.preferredLargeHeapBlockSize=65536;
        return vmaCreateAllocator(&ci,&allocator)==VK_SUCCESS;
    }
    u32 Type(VkBuffer buffer) const
    {const auto b=buffers.find(buffer);if(b==buffers.end())return GS_INVALID_MEMORY_TYPE;const auto m=memories.find(b->second->memory);return m==memories.end()?GS_INVALID_MEMORY_TYPE:m->second->type;}
    bool Empty()const{return buffers.empty()&&memories.empty()&&healthy;}
};
thread_local Backend* Backend::active=nullptr;
}
namespace GSStreamRingActivationOracle
{
struct Writes{bool deferred=false,atoms=false,coherent=false,zero=false,drain=false,bytes=false,guards=false;};
Writes Exercise(VKStreamBuffer& ring,Backend& b)
{
    Writes result;const u32 type=b.Type(ring.GetBuffer());if(type==GS_INVALID_MEMORY_TYPE)return result;
    const bool nc=(b.types.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)==0;
    const auto buffer=*b.buffers.at(ring.GetBuffer());u8* const data=ring.GetHostPointer();
    std::array<u8,16> before,after;std::memcpy(before.data(),data-16,16);std::memcpy(after.data(),data+1024,16);
    b.flushes.clear();
    if(!ring.ReserveMemory(13,1))return result;std::memset(ring.GetCurrentHostPointer(),0xa7,13);ring.CommitMemory(13);
    if(!ring.ReserveMemory(7,16)||ring.GetCurrentOffset()!=16)return result;std::memset(ring.GetCurrentHostPointer(),0xb8,7);ring.CommitMemory(7);
    result.deferred=b.flushes.empty();ring.CommitMemory(0);result.zero=b.flushes.empty();ring.FlushPendingWrites();
    result.atoms=b.flushes.size()==(nc?1u:0u);
    if(nc&&b.flushes.size()==1)result.atoms&=b.flushes[0].memory==buffer.memory&&b.flushes[0].offset==buffer.offset&&b.flushes[0].size==256;
    result.coherent=nc||b.flushes.empty();
    if(!ring.ReserveMemory(5,1))return result;std::memset(ring.GetCurrentHostPointer(),0xc9,5);ring.CommitMemory(5);
    result.bytes=true;for(u32 i=0;i<1024;++i){const u8 want=i<13?0xa7:i>=16&&i<23?0xb8:i>=23&&i<28?0xc9:0;result.bytes&=data[i]==want;}
    result.guards=std::memcmp(before.data(),data-16,16)==0&&std::memcmp(after.data(),data+1024,16)==0;
    const size_t flushed=b.flushes.size();ring.Destroy(false);result.drain=b.flushes.size()==flushed+(nc?1u:0u);
    return result;
}
}
void GSDeviceVK::TestStreamRingActivation(void (*check)(bool,const char*))
{
    using namespace GSStreamRingActivationOracle;
    if(!m_tfx_preparation_device||m_allocator){check(false,"GS stream activation owns an isolated pre-allocation preparation device");return;}
    const auto properties=m_device_properties;const auto facts=m_road_facts;const auto memory=m_stream_ring_memory;
    const u32 frame=m_current_frame;const auto counter=m_frame_resources[frame].fence_counter,completed=m_completed_fence_counter;
    ScopedGuard restore=[&](){m_allocator=VK_NULL_HANDLE;m_device_properties=properties;m_road_facts=facts;m_stream_ring_memory=memory;
        m_current_frame=frame;m_frame_resources[frame].fence_counter=counter;m_completed_fence_counter=completed;};
    const std::array<VkBufferUsageFlags,6> usage={static_cast<VkBufferUsageFlags>(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|(m_features.vs_expand?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:0)),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    constexpr const char* names[]={"activation-vertex","activation-index","activation-expand","activation-vs-ubo","activation-ps-ubo","activation-texture"};
    const auto with=[&](const u32* flags,u32 count,const GSStreamRingMemoryDecision& decision,auto&& body){
        Backend backend(m_device_properties,flags,count);if(!backend.Start(m_instance,m_physical_device,m_device))return false;
        m_allocator=backend.allocator;m_stream_ring_memory=decision;m_frame_resources[frame].fence_counter=1;m_completed_fence_counter=0;
        ScopedGuard clear([&](){m_allocator=VK_NULL_HANDLE;m_frame_resources[frame].fence_counter=counter;m_completed_fence_counter=completed;});
        const bool result=body(backend);m_allocator=VK_NULL_HANDLE;backend.Destroy();return result&&backend.Empty();
    };
    const auto policy=[&](const Identity& id,const u32* flags,u32 count){
        PolicyObservation result;
        m_device_properties=properties;m_device_properties.vendorID=id.vendor;m_device_properties.deviceID=id.device_id;
        const size_t length=std::min(id.name.size(),sizeof(m_device_properties.deviceName)-1);
        if(length)std::memcpy(m_device_properties.deviceName,id.name.data(),length);m_device_properties.deviceName[length]=0;
        m_road_facts=GSVulkanRoad::ResolveFacts({.vendor=id.vendor,.driver=id.driver,.name=id.name,.driver_name=id.driver_name,.info=id.info,.device_id=id.device_id},false,0);
        VkPhysicalDeviceMemoryProperties table{};table.memoryTypeCount=count;
        for(u32 i=0;i<count;++i)table.memoryTypes[i].propertyFlags=flags[i];
        SelectStreamRingMemory(table);result.actual_startup_called=true;result.preferred=PreferCachedStreamRings();result.decision=m_stream_ring_memory;
        const bool clean=with(flags,count,result.decision,[&](Backend& b){
            for(u32 role=0;role<usage.size();++role)
            {
                VKStreamBuffer ring;ScopedGuard destroy([&](){if(ring.IsValid())ring.Destroy(false);});
                auto& out=result.rings[role];const u32 creates=b.creates;
                out.created=ring.Create(usage[role],1024,names[role]);out.actual_create_calls=b.creates-creates;
                if(out.created){out.type_index=b.Type(ring.GetBuffer());const auto writes=Exercise(ring,b);out.bytes_intact=writes.bytes;out.canaries_intact=writes.guards;}
            }
            return true;
        });
        result.no_live_fake_resources=clean;return result;
    };
    const auto contracts=[&](){
        ContractObservation out;out.actual_vma_and_stream_methods=true;out.all_three_roads=out.all_six_roles=true;
        out.restricted_type_retry=out.allocation_failure_retry=out.total_failure_clean=out.replacement_preserves_old=true;
        out.commits_deferred=out.alignment_gap_coalesced=out.atom_ranges_exact=out.zero_commit_no_flush=out.destroy_drains_pending=true;
        out.coherent_no_flush=out.mapped_bytes_exact=out.canaries_intact=out.no_live_fake_resources=true;
        using Road=GSStreamRingMemoryRoad;
        constexpr u32 flags[]={1,7,11,15};
        const std::array<GSStreamRingMemoryDecision,3> choices={GSStreamRingMemoryDecision{Road::WriteCombined,1,0},
            {Road::CachedCoherent,3,15},{Road::CachedNonCoherent,2,11}};
        for(const auto& decision:choices)
        {
            const u32 count=decision.road==Road::CachedNonCoherent?3:4;
            const bool clean=with(flags,count,decision,[&](Backend& b){
                for(u32 role=0;role<usage.size();++role)
                {
                    VKStreamBuffer padding,ring;
                    ScopedGuard destroy([&](){if(ring.IsValid())ring.Destroy(false);if(padding.IsValid())padding.Destroy(false);});
                    const bool allocated=padding.Create(usage[role],512,"atom-offset-padding")&&ring.Create(usage[role],1024,names[role]);
                    out.all_six_roles&=allocated;if(!allocated)continue;
                    const auto at=b.buffers.at(ring.GetBuffer())->offset;
                    out.all_three_roads&=b.Type(ring.GetBuffer())==decision.type_index;
                    const auto writes=Exercise(ring,b);out.commits_deferred&=writes.deferred;out.zero_commit_no_flush&=writes.zero;
                    out.atom_ranges_exact&=writes.atoms&&(decision.road!=Road::CachedNonCoherent||at>0);
                    out.alignment_gap_coalesced&=writes.atoms;out.destroy_drains_pending&=writes.drain;
                    out.coherent_no_flush&=writes.coherent;out.mapped_bytes_exact&=writes.bytes;out.canaries_intact&=writes.guards;
                }
                return true;
            });
            out.no_live_fake_resources&=clean;
        }
        // Buffer-specific restrictions can change coherence in either direction;
        // actual returned memory properties, not the preferred-road label, win.
        for(bool coherentRequest:{false,true})
        {
            const auto decision=coherentRequest?choices[1]:choices[2];
            const bool clean=with(flags,4,decision,[&](Backend& b){
                b.bits=coherentRequest?1u<<2:1u<<1;
                VKStreamBuffer ring;ScopedGuard destroy([&](){if(ring.IsValid())ring.Destroy(false);});
                const u32 before=b.creates;const bool allocated=ring.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,1024,"restricted-cached-type");
                const u32 wanted=coherentRequest?2:1;
                out.restricted_type_retry&=allocated&&b.creates-before==2&&b.Type(ring.GetBuffer())==wanted;
                if(allocated){const auto writes=Exercise(ring,b);out.atom_ranges_exact&=writes.atoms;out.coherent_no_flush&=writes.coherent;out.mapped_bytes_exact&=writes.bytes;out.canaries_intact&=writes.guards;}
                return true;
            });out.no_live_fake_resources&=clean;
        }
        const bool fallbackClean=with(flags,3,choices[2],[&](Backend& b){
            b.failCached=true;VKStreamBuffer ring;ScopedGuard destroy([&](){if(ring.IsValid())ring.Destroy(false);});
            const u32 before=b.creates;const bool allocated=ring.Create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,1024,"cached-allocation-failure");
            out.allocation_failure_retry&=allocated&&b.creates-before==2&&b.Type(ring.GetBuffer())==1;
            if(allocated){const auto writes=Exercise(ring,b);out.mapped_bytes_exact&=writes.bytes;out.canaries_intact&=writes.guards;}
            return true;
        });out.no_live_fake_resources&=fallbackClean;
        const bool failClean=with(flags,3,choices[2],[&](Backend& b){
            b.failAll=true;VKStreamBuffer ring;ScopedGuard destroy([&](){if(ring.IsValid())ring.Destroy(false);});
            out.total_failure_clean&=!ring.Create(VK_BUFFER_USAGE_INDEX_BUFFER_BIT,1024,"all-memory-failure")&&!ring.IsValid()&&b.buffers.empty();return true;
        });out.no_live_fake_resources&=failClean;
        const bool replaceClean=with(flags,3,choices[2],[&](Backend& b){
            VKStreamBuffer ring;ScopedGuard destroy([&](){if(ring.IsValid())ring.Destroy(false);});
            const bool allocated=ring.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,1024,"replacement-old");
            if(!allocated){out.replacement_preserves_old=false;return true;}
            const auto buffer=ring.GetBuffer();u8* const data=ring.GetHostPointer();std::memset(data,0x5c,1024);b.failAll=true;
            out.replacement_preserves_old&=!ring.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,2048,"replacement-failure")&&ring.GetBuffer()==buffer&&ring.GetHostPointer()==data&&ring.GetCurrentSize()==1024;
            for(u32 i=0;i<1024;++i)out.replacement_preserves_old&=data[i]==0x5c;
            return true;
        });out.no_live_fake_resources&=replaceClean;
        std::printf("GS_CACHED_CONTRACT roads=%u roles=%u retry=%u/%u failure=%u replacement=%u deferred=%u atoms=%u coherent=%u bytes=%u guards=%u liveClean=%u\n",
            unsigned(out.all_three_roads),unsigned(out.all_six_roles),unsigned(out.restricted_type_retry),unsigned(out.allocation_failure_retry),unsigned(out.total_failure_clean),unsigned(out.replacement_preserves_old),
            unsigned(out.commits_deferred),unsigned(out.atom_ranges_exact),unsigned(out.coherent_no_flush),unsigned(out.mapped_bytes_exact),unsigned(out.canaries_intact),unsigned(out.no_live_fake_resources));
        return out;
    };
    Run(policy,contracts,check);
}
