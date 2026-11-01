// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Included only under SELFTEST. No worker reads live Program/owner state.
struct CompileKnownNode { u32 pc=0; PipeState key; u8 kind=0; };
struct CompileObservation
{
    vuaot::observe::Snapshot result;
    std::vector<CompileKnownNode> known;
    u64 epoch=0,id=0;
    u32 rootPC=0,owner=0,codeBytes=0;
    const u8* base=nullptr;
    bool worker=false,inlineBody=false,retier=false,flagHack=false,accepted=false,finished=false,merged=false,incomplete=false;
};
struct CompileProbeState
{
    bool enabled=false,manual=false;
    u64 epoch=1,nextId=0;
    vuaot::observe::Snapshot result;
};
CompileProbeState compileProbe; // owner-only, worker results use completed-job handoff
std::atomic<u64> compileProbeLogEpoch{1};
constexpr size_t kCompileKnownLimit=512,kCompileScopeSamples=128,kCompileSamples=4096;
void MergeCompileObservation(CompileObservation&);

void NoteCompileBody(void* opaque,u32 pc,const u8* raw,const u8* at,u32 bytes)
{
    auto& r=*static_cast<CompileObservation*>(opaque);
    const uintptr_t base=reinterpret_cast<uintptr_t>(r.base),start=reinterpret_cast<uintptr_t>(at);
    if(!bytes||!base||start<base||u64(start-base)+bytes>bg::CodeSink::kCapacity)
    {++r.result.stats.droppedSamples;return;}
    const auto identity=CanonicalRootIdentity(pc,raw);
    u8 overlap=0;
    const auto first=std::lower_bound(r.known.begin(),r.known.end(),pc,
        [](const CompileKnownNode& a,u32 b){return a.pc<b;});
    for(auto found=first;found!=r.known.end()&&found->pc==pc;++found)
        if(SameKey(found->key,identity.second.data(),r.flagHack))overlap|=found->kind;
    auto& s=r.result.stats;++s.emittedNodes;s.bodyBytes+=bytes;
    if(overlap&1){++s.publishedOverlapNodes;s.publishedOverlapBytes+=bytes;}
    if(overlap&2){++s.readyOverlapNodes;s.readyOverlapBytes+=bytes;}
    if(r.incomplete){overlap|=4;++s.unknownOverlapNodes;}
    if(r.result.nodes.size()>=kCompileScopeSamples){++s.droppedSamples;return;}
    vuaot::observe::NodeSample sample;
    sample.generation=s.generation;sample.owner=r.owner;sample.rootPC=r.rootPC;sample.pc=pc;
    sample.offset=static_cast<u32>(start-base);sample.bytes=bytes;sample.key=identity.second;
    sample.overlap=overlap;sample.worker=r.worker;r.result.nodes.push_back(sample);
}
void NoteCompileAllocation(void* opaque,u64 bytes,u64 cpu)
{
    auto& s=static_cast<CompileObservation*>(opaque)->result.stats;
    ++s.sinkAllocations;s.sinkRequestedBytes+=bytes;s.sinkCpuNs+=cpu;
}
void ClearCompileBodies(CompileObservation* r)
{
    if(!r)return;
    auto& s=r->result.stats;s.emittedNodes=s.bodyBytes=0;
    s.publishedOverlapNodes=s.publishedOverlapBytes=s.readyOverlapNodes=s.readyOverlapBytes=0;
    s.unknownOverlapNodes=s.droppedSamples=0;r->result.nodes.clear();
}
void FinishCompileObservation(CompileObservation& r,u64 begin)
{
    const u64 end=vuaot::observe::ThreadCpuNs();const u64 cpu=begin&&end>=begin?end-begin:0;
    auto& s=r.result.stats;s.scopes=1;s.acceptedScopes=r.accepted?1:0;
    if(r.inlineBody)s.inlineBodies=1;else if(r.worker)s.workerJobs=1;else s.ownerJobs=1;
    if(r.worker)s.workerCpuNs=cpu;else s.ownerCpuNs=cpu;
    if(!r.accepted)ClearCompileBodies(&r);
    for(auto& sample:r.result.nodes)sample.codeBytes=r.codeBytes;
    r.finished=true;
#if defined(__ANDROID__) && !defined(NDEBUG)
    if(r.epoch==compileProbeLogEpoch.load(std::memory_order_relaxed)&&r.id<=4096)
    {
        __android_log_print(ANDROID_LOG_INFO,"EmuCoreX",
            "AOT_OBSERVE scope=%llu epoch=%llu gen=%llu owner=%u pc=%04x kind=%c retier=%u accepted=%u nodes=%llu bodyBytes=%llu published=%llu/%llu ready=%llu/%llu cpuNs=%llu sinks=%llu requestedBytes=%llu mallocCpuNs=%llu unknown=%llu dropped=%llu",
            (unsigned long long)r.id,(unsigned long long)r.epoch,(unsigned long long)s.generation,r.owner,r.rootPC,
            r.inlineBody?'I':r.worker?'W':'O',unsigned(r.retier),unsigned(r.accepted),(unsigned long long)s.emittedNodes,(unsigned long long)s.bodyBytes,
            (unsigned long long)s.publishedOverlapNodes,(unsigned long long)s.publishedOverlapBytes,
            (unsigned long long)s.readyOverlapNodes,(unsigned long long)s.readyOverlapBytes,(unsigned long long)cpu,
            (unsigned long long)s.sinkAllocations,(unsigned long long)s.sinkRequestedBytes,(unsigned long long)s.sinkCpuNs,
            (unsigned long long)s.unknownOverlapNodes,(unsigned long long)s.droppedSamples);
        u32 shown=0;for(const auto& sample:r.result.nodes)if(sample.overlap&&shown++<2)
            __android_log_print(ANDROID_LOG_INFO,"EmuCoreX","AOT_OBSERVE_NODE scope=%llu gen=%llu pc=%04x key=%016llx offset=%u bytes=%u codeBytes=%u overlap=%u",
                (unsigned long long)r.id,(unsigned long long)sample.generation,sample.pc,
                (unsigned long long)XXH3_64bits(sample.key.data(),sample.key.size()),sample.offset,sample.bytes,sample.codeBytes,sample.overlap);
    }
    else if(r.epoch==compileProbeLogEpoch.load(std::memory_order_relaxed)&&r.id==4097)
        __android_log_print(ANDROID_LOG_INFO,"EmuCoreX","AOT_OBSERVE logCap=4096 furtherScopesUnknown=1");
#endif
    if(!r.worker)MergeCompileObservation(r);
}
class CompileObservationScope
{
    CompileObservation* record;vuaot::observe::Hooks hooks;
    vuaot::observe::Hooks* previous;u64 begin=0;
public:
    explicit CompileObservationScope(CompileObservation* r):record(r),previous(vuaot::observe::active)
    {
        hooks={r,NoteCompileBody,NoteCompileAllocation};vuaot::observe::active=r?&hooks:nullptr;
        if(r)begin=vuaot::observe::ThreadCpuNs();
    }
    ~CompileObservationScope()
    {
        vuaot::observe::active=previous;
        if(record)FinishCompileObservation(*record,begin);
    }
};
std::shared_ptr<CompileObservation> MakeCompileObservation(microVU& mvu,u32 pc,bool worker,bool inlineBody)
{
    if(!compileProbe.enabled)return {};
    auto r=std::make_shared<CompileObservation>();r->epoch=compileProbe.epoch;r->id=++compileProbe.nextId;
    r->rootPC=pc;r->owner=mvu.prog.cur?mvu.prog.cur->idx:0;r->worker=worker;r->inlineBody=inlineBody;
    r->incomplete=inlineBody; // inline scopes measure CPU/bytes without a graph snapshot
    r->result.stats.generation=runtime.generation;r->result.nodes.reserve(kCompileScopeSamples);return r;
}
void PrepareCompileObservation(microVU& mvu,Program& program,const std::shared_ptr<BuildJob>& job,bool worker)
{
    if(!compileProbe.enabled)return;
    auto r=MakeCompileObservation(mvu,job->trace.startPC,worker,false);r->flagHack=job->env.flagHack;r->retier=job->retier;r->known.reserve(kCompileKnownLimit);
    const auto add=[&](const std::shared_ptr<BuildJob>& peer,u8 kind){
        if(!peer->accepted||peer->program!=mvu.prog.cur||peer->generation!=runtime.generation||
            (peer->dropped&&sticky::state.tripped.load(std::memory_order_acquire))||
            !(peer->cpu==EmuConfig.Cpu)||!(peer->fixes==EmuConfig.Gamefixes)||!(peer->speed==EmuConfig.Speedhacks)||
            !OwnerMatches(*peer)||!ImageMatches(*peer,mvu)||!OwnerImageMatches(*peer,mvu))return;
        for(const auto& node:peer->nodes)
        {
            // Retained old code remains alive for private links, but only the
            // currently installed published entry describes the live graph.
            if(kind==1&&(!node->baselineBlock||node->baselineBlock->x86ptrStart!=node->entry))continue;
            if(r->known.size()>=kCompileKnownLimit){r->incomplete=true;return;}
            const auto identity=CanonicalRootIdentity(node->pc,node->key.bytes);
            CompileKnownNode n;n.pc=node->pc;std::memcpy(n.key.bytes,identity.second.data(),96);n.kind=kind;r->known.push_back(n);
        }
    };
    size_t peers=0;
    for(const auto& peer:program.ready){if(r->incomplete)break;add(peer,2);}
    for(auto peer=program.jobs.rbegin();peer!=program.jobs.rend();++peer)
    {if(r->incomplete)break;if(++peers>kCompileKnownLimit){r->incomplete=true;break;}add(*peer,1);}
    if(r->incomplete)r->result.stats.truncatedSnapshots=1;
    std::sort(r->known.begin(),r->known.end(),[](const auto& a,const auto& b){return a.pc<b.pc;});
    job->observation=std::move(r);
}
void MergeCompileObservation(CompileObservation& r)
{
    if(r.merged||!r.finished)return;r.merged=true;
    // This owner-side handoff occurs only after the worker finished. Do not
    // retain up to512 keys/128 rows on every live native BuildJob.
    const ScopedGuard releasePayload([&]{
        std::vector<CompileKnownNode>().swap(r.known);
        std::vector<vuaot::observe::NodeSample>().swap(r.result.nodes);
    });
    if(!compileProbe.enabled||r.epoch!=compileProbe.epoch)return;
    const auto& s=r.result.stats;auto& total=compileProbe.result.stats;total.generation=runtime.generation;
#define OBS_ADD(field) total.field+=s.field
    OBS_ADD(scopes);OBS_ADD(acceptedScopes);OBS_ADD(workerJobs);OBS_ADD(ownerJobs);OBS_ADD(inlineBodies);
    OBS_ADD(emittedNodes);OBS_ADD(bodyBytes);OBS_ADD(publishedOverlapNodes);OBS_ADD(publishedOverlapBytes);
    OBS_ADD(readyOverlapNodes);OBS_ADD(readyOverlapBytes);OBS_ADD(workerCpuNs);OBS_ADD(ownerCpuNs);
    OBS_ADD(sinkAllocations);OBS_ADD(sinkRequestedBytes);OBS_ADD(sinkCpuNs);OBS_ADD(droppedSamples);
    OBS_ADD(truncatedSnapshots);OBS_ADD(unknownOverlapNodes);
#undef OBS_ADD
    for(const auto& sample:r.result.nodes)
        if(compileProbe.result.nodes.size()<kCompileSamples)compileProbe.result.nodes.push_back(sample);
        else ++total.droppedSamples;
}
void ClearCompileObservation(bool enabled)
{
    compileProbe.enabled=enabled;++compileProbe.epoch;compileProbeLogEpoch.store(compileProbe.epoch,std::memory_order_relaxed);
    compileProbe.nextId=0;compileProbe.result={};if(enabled)compileProbe.result.stats.generation=runtime.generation;
#if defined(__ANDROID__) && !defined(NDEBUG)
    if(enabled)__android_log_print(ANDROID_LOG_INFO,"EmuCoreX","AOT_OBSERVE_START epoch=%llu gen=%llu aot=%u scopes=0 nodes=0 sinks=0 knownCap=512 sampleCap=4096",
        (unsigned long long)compileProbe.epoch,(unsigned long long)runtime.generation,unsigned(EmuConfig.Cpu.Recompiler.EnableVUAot));
#endif
}
void ResetCompileObservation()
{
    bool enabled=compileProbe.enabled;
#if defined(__ANDROID__) && !defined(NDEBUG)
    if(!compileProbe.manual){char value[PROP_VALUE_MAX]{};enabled=__system_property_get("debug.emucorex.vu_aot_observe",value)>0&&value[0]=='1';}
    if(compileProbe.enabled)__android_log_print(ANDROID_LOG_INFO,"EmuCoreX","AOT_OBSERVE_STOP epoch=%llu gen=%llu scopes=%llu nodes=%llu sinks=%llu",
        (unsigned long long)compileProbe.epoch,(unsigned long long)compileProbe.result.stats.generation,
        (unsigned long long)compileProbe.result.stats.scopes,(unsigned long long)compileProbe.result.stats.emittedNodes,
        (unsigned long long)compileProbe.result.stats.sinkAllocations);
#endif
    ClearCompileObservation(enabled);
}
