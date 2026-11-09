// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// The real VK cache SetGame/GetShaderSPV and background writer are the consumers.
#if __has_include("GS/GSCacheFile.h")
#include "GS/GSCacheFile.h"
void VKShaderCache::TestGamePackStorage(void(*check)(bool,const char*),const char* directory)
{
    const std::string fresh=Path::Combine(directory,fmt::format("fresh_{}",GSCacheFile::Clock::now().time_since_epoch().count()));
    directory=fresh.c_str();
    check(FileSystem::EnsureDirectoryExists(directory,true),"GS pack fixture creates its unique isolated cache root");
    const std::string previous=EmuFolders::Cache;EmuFolders::Cache=directory;
    ScopedGuard restore=[&](){EmuFolders::Cache=previous;};
    VKShaderCache cache;
    cache.m_spirv_stamp.Add("oracle",1);cache.m_spirv_stem="vk_spirv_oracle";
    cache.m_writer.Start(directory);
    cache.m_device_spirv=cache.OpenGamePack("",0,"vk_spirv",cache.m_spirv_stem,GSCacheFile::KIND_VK_SPIRV,cache.m_spirv_stamp);
    const auto put=[&](GSCacheFile::Pack& pack,std::string_view source,u32 stage,u32 tag){
        u8 key[24]{};const auto digest=GSCacheFile::Hash128(source.data(),source.size());const u32 length=static_cast<u32>(source.size());
        std::memcpy(key,digest.bytes,16);std::memcpy(key+16,&length,4);std::memcpy(key+20,&stage,4);
        const u32 payload[]={0x07230203u,0x00010000u,tag,1,0};
        pack.Put(1,key,sizeof(key),payload,sizeof(payload));
    };
    put(*cache.m_device_spirv,"utility",0,30);
    cache.SetGame("PACK-A",0xaaa);put(*cache.m_game_spirv,"shared-source",0,10);put(*cache.m_game_spirv,"shared-source",1,11);
    // Startup cleanup may overlap an opened RAM pack before its first write.
    // Exercise the real cleanup synchronously so timing cannot hide this seam.
    GSCacheFile::CleanLeftovers(directory);
    const std::string pack_directory(Path::GetDirectory(cache.m_game_spirv->GetPath()));
    check(FileSystem::DirectoryExists(pack_directory.c_str()),"GS actual cleanup retains the directory backing an open unwritten game pack");
    auto read=cache.GetShaderSPV(0,"shared-source");auto fragment=cache.GetShaderSPV(1,"shared-source");
    check(read&&(*read)[2]==10&&fragment&&(*fragment)[2]==11,"GS real SPIR-V consumer routes game pack entries with stage-qualified complete-source keys");
    const auto a=cache.m_game_spirv;cache.SetGame("PACK-B",0xbbb);put(*cache.m_game_spirv,"shared-source",0,20);
    read=cache.GetShaderSPV(0,"shared-source");auto utility=cache.GetShaderSPV(0,"utility");
    check(read&&(*read)[2]==20&&utility&&(*utility)[2]==30,"GS real game switch separates matching shader sources and retains device-pack fallback");
    cache.m_writer.Flush(true);cache.SetGame("PACK-A",0xaaa);read=cache.GetShaderSPV(0,"shared-source");
    if(!read||(*read)[2]!=10) {
        const auto info=cache.m_game_spirv->GetLoadInfo();
        std::printf("GS_PACK_REOPEN path=%s load=%s records=%u torn=%llu file=%llu ram=%llu entries=%zu oldEntries=%zu oldPending=%u readWords=%zu tag=%u\n",
            cache.m_game_spirv->GetPath().c_str(),GSCacheFile::ReadResultString(info.result),info.records,
            static_cast<unsigned long long>(info.torn_bytes),static_cast<unsigned long long>(cache.m_game_spirv->GetFileBytes()),
            static_cast<unsigned long long>(cache.m_game_spirv->GetRamBytes()),cache.m_game_spirv->GetEntryCount(),a->GetEntryCount(),unsigned(a->HasPendingWrites()),read?read->size():0,read&&read->size()>2?(*read)[2]:0);
    }
    check(read&&(*read)[2]==10,"GS real game pack reopen restores persisted first-game shader bytes");
    const std::string framed=Path::Combine(directory,"vulkan_pipelines_oracle.bin");
    GSCacheFile::Stamp stamp;stamp.Add("driver",100);const u8 bytes[]={1,2,3,4};
    check(GSCacheFile::WriteFramedFile(framed,GSCacheFile::KIND_VK_PIPELINES,stamp,bytes,sizeof(bytes)),"GS actual framed cache writes atomic checked payload");
    auto damaged=FileSystem::ReadBinaryFile(framed.c_str());if(damaged&&damaged->size()>56){damaged->back()^=0x80;FileSystem::WriteBinaryFile(framed.c_str(),damaged->data(),damaged->size());}
    std::vector<u8> payload;
    check(GSCacheFile::ReadFramedFile(framed,GSCacheFile::KIND_VK_PIPELINES,stamp,&payload)==GSCacheFile::ReadResult::ChecksumMismatch,
        "GS actual opaque pipeline frame rejects payload corruption before a driver sees bytes");
    GSCacheFile::DeleteAll(directory);cache.m_writer.Flush(true);
    check(cache.m_game_spirv->GetEntryCount()==0&&cache.m_device_spirv->GetEntryCount()==0&&a->GetEntryCount()==0,
        "GS actual cache clear empties every open game/device pack and writer cannot resurrect stale entries");
    cache.m_writer.Stop(GSCacheFile::Clock::now()+GSCacheFile::PackWriter::CLOSE_DEADLINE);
    check(!cache.m_writer.IsRunning(),"GS actual cache writer shutdown joins within cooperative write deadline lifecycle");
}
#else
void VKShaderCache::TestGamePackStorage(void(*check)(bool,const char*),const char*)
{
    check(false,"GS production per-game SPIR-V pack and writer consumers are available");
}
#endif
extern "C" void EmuCoreXOracleGamePackStorage(void(*check)(bool,const char*),const char* directory)
{
    VKShaderCache::TestGamePackStorage(check,directory);
}
