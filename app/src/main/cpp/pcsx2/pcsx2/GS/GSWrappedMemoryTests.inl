// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Bounded allocator regression: two pairs, at most128KiB touched physical memory/pair.
#ifndef _WIN32
#include <dirent.h>
#endif
namespace {
long WrappedHandleCount()
{
#ifdef _WIN32
    DWORD count=0;return GetProcessHandleCount(GetCurrentProcess(),&count)?static_cast<long>(count):-1;
#else
    DIR* d=opendir("/proc/self/fd");if(!d)return -1;long count=0;
    while(const dirent* e=readdir(d))if(e->d_name[0]!='.')++count;
    closedir(d);return count;
#endif
}
#ifndef _WIN32
long WrappedMappingCount()
{
    FILE* f=fopen("/proc/self/maps","r");if(!f)return -1;long count=0;char line[2048];
    while(fgets(line,sizeof(line),f))if(strstr(line,"GS.mem"))++count;
    fclose(f);return count;
}
#endif
}
extern "C" void EmuCoreXOracleGSWrappedMemory(void(*check)(bool,const char*))
{
    const bool isolated=!g_gs_renderer&&!g_gs_device;
    check(isolated,"GS wrapped-memory fixture requires isolated globals");if(!isolated)return;
    constexpr size_t size=65536,repeat=4;
    // Warm libc/proc parsing before measuring. No large renderer or rasterizer is constructed.
    WrappedHandleCount();
#ifndef _WIN32
    WrappedMappingCount();
#endif
    for(int order=0;order<2;order++)
    {
        const long handles=WrappedHandleCount();
#ifndef _WIN32
        const long mappings=WrappedMappingCount();
#endif
        u8* first=static_cast<u8*>(GSAllocateWrappedMemory(size,repeat));
        u8* second=static_cast<u8*>(GSAllocateWrappedMemory(size,repeat));
        const bool valid=first&&second&&first!=reinterpret_cast<void*>(static_cast<size_t>(-1))&&second!=reinterpret_cast<void*>(static_cast<size_t>(-1));
        check(valid,"GS two simultaneous wrapped allocations return real views");
        if(valid)
        {
            first[0]=0x12;second[0]=0x34;first[size+1]=0x56;second[2*size+1]=0x78;
            bool aliases=true;for(size_t i=0;i<repeat;i++)aliases&=first[i*size]==0x12&&first[i*size+1]==0x56&&second[i*size]==0x34&&second[i*size+1]==0x78;
            check(aliases,"GS each wrapped allocation aliases its own four views without sharing the other allocation");
        }
        if(order==0){if(second)GSFreeWrappedMemory(second,size,repeat);if(first)GSFreeWrappedMemory(first,size,repeat);}
        else{if(first)GSFreeWrappedMemory(first,size,repeat);if(second)GSFreeWrappedMemory(second,size,repeat);}
        const long after_handles=WrappedHandleCount();check(handles>=0&&after_handles==handles,"GS freeing two wrapped allocations in either order closes every descriptor/section handle");
#ifndef _WIN32
        const long after_mappings=WrappedMappingCount();check(mappings>=0&&after_mappings==mappings,"GS freeing both wrapped allocations removes every named GS mapping");
#endif
    }
}
