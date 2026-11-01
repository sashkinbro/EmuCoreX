// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
#include "common/Pcsx2Defs.h"
#include <array>
#include <vector>
namespace vuaot::observe
{
    struct Stats
    {
        u64 generation=0, scopes=0, acceptedScopes=0;
        u64 workerJobs=0, ownerJobs=0, inlineBodies=0;
        u64 emittedNodes=0, bodyBytes=0;
        u64 publishedOverlapNodes=0, publishedOverlapBytes=0;
        u64 readyOverlapNodes=0, readyOverlapBytes=0;
        u64 workerCpuNs=0, ownerCpuNs=0;
        u64 sinkAllocations=0, sinkRequestedBytes=0, sinkCpuNs=0;
        u64 droppedSamples=0, truncatedSnapshots=0, unknownOverlapNodes=0;
    };
    struct NodeSample
    {
        u64 generation=0;
        u32 owner=0, rootPC=0, pc=0, offset=0, bytes=0, codeBytes=0;
        std::array<u8,96> key{};
        u8 overlap=0; // bit0 published; bit1 accepted ready; bit2 incomplete snapshot
        bool worker=false;
    };
    struct Snapshot { Stats stats; std::vector<NodeSample> nodes; };
    struct Hooks
    {
        void* user=nullptr;
        void (*body)(void*,u32,const u8*,const u8*,u32)=nullptr;
        void (*allocation)(void*,u64,u64)=nullptr;
    };
    extern thread_local Hooks* active;
    u64 ThreadCpuNs();
    u8* AllocateScratch(size_t bytes);
    inline void NoteBody(u32 pc,const u8* key,const u8* start,u32 bytes)
    {
        if(active&&active->body)active->body(active->user,pc,key,start,bytes);
    }

}
#endif
