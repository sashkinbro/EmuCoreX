// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "common/ScopedGuard.h"
#include "common/Path.h"
#if __has_include("GS/Renderers/Vulkan/FrameGen/FrameGen.h") && defined(EMUCOREX_HAS_LSFG)
#include "GS/Renderers/Vulkan/FrameGen/LosslessDll.h"
#include "GS/Renderers/Vulkan/FrameGen/LsfgTranslate.h"
#include "GS/Renderers/Vulkan/FrameGen/FrameGenPacer.h"
#include <thread>
#endif
#if __has_include("GS/Renderers/Vulkan/FrameGen/FrameGen.h") && defined(EMUCOREX_HAS_LSFG)
namespace FrameGenCpuFixture
{
// Synthetic PE resources exercise only the reader/translator/cache. These tiny
// modules are never handed to Vulkan and contain no proprietary shader code.
static std::vector<u8> NativePe()
{
    std::vector<u32> ids{255,256};for(u32 id=280;id<=302;id++)ids.push_back(id);
    const u32 count=static_cast<u32>(ids.size()*3), base=0x200;
    const u32 languages=0x30+count*8, leaves=languages+count*24, blobs=leaves+count*16;
    std::vector<u8> image(base+blobs+count*20,0);
    const auto put=[&](u32 at,u32 value,u32 size=4){std::memcpy(image.data()+at,&value,size);};
    put(0,0x5a4d,2);put(0x3c,0x80);put(0x80,0x4550);put(0x86,1,2);put(0x94,0xe0,2);
    put(0x98,0x10b,2);put(0x98+96+16,0x1000);put(0x98+96+20,static_cast<u32>(image.size()-base));
    const u32 section=0x98+0xe0;
    put(section+8,static_cast<u32>(image.size()-base));put(section+12,0x1000);
    put(section+16,static_cast<u32>(image.size()-base));put(section+20,base);
    put(base+14,1,2);put(base+16,10);put(base+20,0x80000020);
    put(base+0x20+14,count,2);
    for(u32 i=0;i<count;i++)
    {
        const u32 family=i/static_cast<u32>(ids.size());
        const u32 offset=family==1?49:family==2?98:0;
        put(base+0x30+i*8,ids[i%ids.size()]+offset);put(base+0x34+i*8,0x80000000+languages+i*24);
        put(base+languages+i*24+14,1,2);put(base+languages+i*24+16,0x409);
        put(base+languages+i*24+20,leaves+i*16);
        put(base+leaves+i*16,0x1000+blobs+i*20);put(base+leaves+i*16+4,20);
        put(base+blobs+i*20,family?0x07230203:0x43425844); // SPIR-V or DXBC marker.
        put(base+blobs+i*20+4,0x00010000);put(base+blobs+i*20+8,family==1?16:32);
        put(base+blobs+i*20+12,1);
    }
    return image;
}
}
#endif
void EmuCoreXOracleFrameGen(void(*check)(bool,const char*),const char* filename)
{
#if !__has_include("GS/Renderers/Vulkan/FrameGen/FrameGen.h")
    check(false,"GS production native-SPIRV FrameGen chain and DLL reader are available");
#elif !defined(EMUCOREX_HAS_LSFG)
    check(false,"GS FrameGen native oracle requires the existing non-Play LSFG build");
#else
    const auto settings=GSConfig;const std::string old_path=GSLsfg::GetDllPath();
    const ScopedGuard restore([&](){GSConfig=settings;GSLsfg::SetDllPath(old_path);FileSystem::DeleteFilePath(filename);});
    std::vector<u8> bytes(0x134,0);bytes[0]='M';bytes[1]='Z';const u32 offset=0x130;
    std::memcpy(bytes.data()+0x3c,&offset,4);std::memcpy(bytes.data()+offset,"PE\0\0",4);
    check(FileSystem::WriteBinaryFile(filename,bytes.data(),bytes.size()),"LSFG test PE fixture can be written");
    check(GSLsfg::LooksLikeLosslessDll(filename),"LSFG actual importer accepts a structurally valid PE");
    VideoCore::FrameGen::ShaderResources resources;
    check(VideoCore::FrameGen::ReadShaderResources(filename,resources)!=VideoCore::FrameGen::LosslessStatus::Ok,
        "LSFG native loader rejects a structural PE without a shader resource directory");
    GSLsfg::NoteRendererCapability(true,0);
    check(!GSLsfg::HasSupportedRenderer(),"LSFG authoritative capability rejects an unsupported/stock Adreno6 profile");
    GSLsfg::NoteRendererCapability(true,6);
    check(GSLsfg::HasSupportedRenderer(),"LSFG authoritative capability accepts the resolved Turnip Adreno6 profile");
    GSLsfg::NoteRendererCapability(false,7);
    check(!GSLsfg::HasSupportedRenderer(),"LSFG authoritative capability rejects the GLES renderer");
    GSLsfg::SetDllPath(filename);GSLsfg::NoteRendererCapability(true,7);
    check(GSLsfg::GetUnavailableReason()==GSLsfg::Unavailable::Available,"LSFG caches the first actual structural verdict");
    bytes[0]=0;FileSystem::WriteBinaryFile(filename,bytes.data(),bytes.size());GSLsfg::InvalidateDllVerdict();
    check(GSLsfg::GetUnavailableReason()==GSLsfg::Unavailable::DllUnreadable,"LSFG same-path DLL replacement invalidates the actual cached verdict");
    const std::string a(300,'a'),b(400,'b');std::atomic<bool> coherent{true};GSLsfg::SetDllPath(a);
    std::thread writer([&](){for(u32 i=0;i<2000;i++)GSLsfg::SetDllPath(i&1?a:b);});
    for(u32 i=0;i<2000;i++){const std::string path=GSLsfg::GetDllPath();if(path!=a&&path!=b)coherent=false;}
    writer.join();check(coherent,"LSFG actual cross-thread DLL path API returns owned coherent strings");
    bytes=FrameGenCpuFixture::NativePe();
    if(!FileSystem::WriteBinaryFile(filename,bytes.data(),bytes.size()))
    {check(false,"LSFG native resource fixture can be written");return;}
    resources.clear();
    check(VideoCore::FrameGen::ReadShaderResources(filename,resources)==VideoCore::FrameGen::LosslessStatus::Ok&&resources.size()==75,
        "LSFG actual PE reader traverses all language leaves in the three synthetic resource families");
    const std::string old_cache=EmuFolders::Cache,cache_dir=std::string(filename)+"-cache";
    if(!FileSystem::CreateDirectoryPath(cache_dir.c_str(),true))
    {check(false,"LSFG cache fixture directory can be created");return;}
    EmuFolders::Cache=cache_dir;GSConfig.LsfgDllPath=filename;
    const ScopedGuard restore_cache([&](){VideoCore::FrameGen::ClearShaderCache();EmuFolders::Cache=old_cache;FileSystem::DeleteDirectory(cache_dir.c_str());});
    const std::string legacy_cache=Path::Combine(cache_dir,"lsfg_shaders.bin");
    const u8 obsolete_cache[]={1,2,3,4};
    if(!FileSystem::WriteBinaryFile(legacy_cache.c_str(),obsolete_cache,sizeof(obsolete_cache)))
    {check(false,"LSFG legacy cache fixture can be written");return;}
    GSLsfgLegacy::ClearShaderCache();
    check(!FileSystem::FileExists(legacy_cache.c_str()),"LSFG actual legacy cache invalidation drops same-path DLL translation cache");
    VideoCore::FrameGen::ShaderModules modules;
    const auto loaded32=VideoCore::FrameGen::LoadShaderModules(modules,false,false);
    check(loaded32==VideoCore::FrameGen::LosslessStatus::Ok&&modules.size()==25&&modules.at(255)[2]==32,
        "LSFG actual loader selects and translates the complete native fp32 family");
    const auto loaded16=VideoCore::FrameGen::LoadShaderModules(modules,true,true);
    check(loaded16==VideoCore::FrameGen::LosslessStatus::Ok&&modules.size()==25&&modules.at(255)[2]==16,
        "LSFG actual variant preference invalidates cached fp32 and selects native fp16");
    modules.clear();
    check(VideoCore::FrameGen::LoadShaderModules(modules,true,true)==VideoCore::FrameGen::LosslessStatus::Ok&&modules.size()==25&&modules.at(255)[2]==16,
        "LSFG actual framed shader cache round-trips the selected family");
    const std::array<u32,21> decorated={0x07230203,0x10000,0,4,0,
        (4u<<16)|71u,1,34,2,(4u<<16)|71u,1,33,7,
        (4u<<16)|71u,2,34,0,(4u<<16)|71u,2,33,9};
    const auto adopted=VideoCore::FrameGen::AdoptSpirvModule(std::span<const u8>(reinterpret_cast<const u8*>(decorated.data()),sizeof(decorated)));
    check(adopted.size()==21&&adopted[12]==1&&adopted[20]==0,
        "LSFG actual SPIRV translator renumbers bindings in descriptor-set order");
    GSConfig.LsfgEnabled=true;GSConfig.LsfgMultiplier=4;GSConfig.LsfgTargetRate=0;
    Vulkan::FrameGenPacer pacer;const auto cold=pacer.Plan(3);const auto no_capacity=pacer.Plan(0);const auto reset=pacer.Plan(3);
    check(cold.generations==0&&!cold.warm&&no_capacity.generations==0&&!no_capacity.warm&&reset.generations==0&&!reset.warm,
        "LSFG actual pacer waits for history and resets after losing swapchain capacity");
    GSConfig.LsfgEnabled=false;
    check(Vulkan::FrameGenSettings::MaxGenerations()==0&&pacer.Plan(3).generations==0,
        "LSFG actual pacer disables interpolation when the Android toggle is off");
    const u8 malformed[]={3,2,35,7,0};
    check(!VideoCore::FrameGen::IsSpirvModule(malformed),"LSFG actual SPIRV adoption rejects a truncated module");
#endif
}
