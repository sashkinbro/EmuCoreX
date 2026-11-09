// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#if __has_include("GS/GSCacheFile.h")
void GLShaderCache::TestGamePackStorage(void(*check)(bool,const char*),const char* directory)
{
    const std::string previous=EmuFolders::Cache;EmuFolders::Cache=directory;
    const auto restore=ScopedGuard([&](){EmuFolders::Cache=previous;});
    GLShaderCache cache;cache.m_stamp.Add("oracle-gl",1);cache.m_stem="gl_oracle";
    cache.m_writer.Start(directory);
    cache.m_device_pack=GSCacheFile::OpenPack(GSCacheFile::GetPackDirectory(directory,"",0),"gl_",cache.m_stem,GSCacheFile::KIND_GL_PROGRAMS,cache.m_stamp);
    cache.m_writer.Add(cache.m_device_pack);
    const u8 binary[]={4,3,2,1};
    auto key=GetCacheKey("vertex","fragment",false);
    cache.SetGame("GL-A",0xaaa);cache.m_game_pack->Put(PROGRAM_TYPE,key.data(),KEY_SIZE,binary,sizeof(binary),123);
    check(cache.HasProgram("vertex","fragment")&&!cache.HasProgram("vertex","other"),"GS real GL warmup HasProgram uses current game's exact source-pair key");
    cache.SetGame("GL-B",0xbbb);
    check(!cache.HasProgram("vertex","fragment"),"GS real GL pack switch separates identical source pairs between games");
    cache.m_device_pack->Put(PROGRAM_TYPE,key.data(),KEY_SIZE,binary,sizeof(binary),123);
    check(cache.HasProgram("vertex","fragment"),"GS real GL warmup consumer retains device program fallback across game switches");
    cache.m_writer.Flush(true);GSCacheFile::DeleteAll(directory);cache.m_writer.Flush(true);
    check(!cache.HasProgram("vertex","fragment"),"GS real GL cache clear invalidates RAM lookup and cannot resurrect pending pack bytes");
}
#else
void GLShaderCache::TestGamePackStorage(void(*check)(bool,const char*),const char*)
{
    check(false,"GS production GL game pack warmup consumer is available");
}
#endif
extern "C" void EmuCoreXOracleGLGamePackStorage(void(*check)(bool,const char*),const char* directory)
{
    GLShaderCache::TestGamePackStorage(check,directory);
}
