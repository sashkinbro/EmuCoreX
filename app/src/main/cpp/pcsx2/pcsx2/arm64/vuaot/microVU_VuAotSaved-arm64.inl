// SPDX-License-Identifier: GPL-3.0+
// Included in the runtime's private namespace after Environment and Publish.
namespace saved
{
    namespace wire = vuaot::persist;
    wire::Identity Identity(microVU& mvu, const microProgram& program)
    {
        wire::Identity id;
        const auto hash = mVUProgCache::ProgramKey(program.contentKey);
        const auto options = mVUPersist::OptionsSentinel(mvu);
        id.contentHashLo=hash.low64; id.contentHashHi=hash.high64;
        id.sentinelLo=options.low64; id.sentinelHi=options.high64;
        mVUPersist::GetBuildId(id.buildLo,id.buildHi);
        id.mvuAbi=kMvuCompilerAbiVersion;
        const u64 layout[]={sizeof(microRegInfo),sizeof(Node),sizeof(ProgramCounters),
            offsetof(microVU,macFlag),offsetof(microVU,aotBackup),offsetof(microVU,cycles),
            offsetof(VURegs,Mem),sizeof(vuaot::MvuEmitEnv),wire::kVuAotAbiVersion};
        id.layoutHash=static_cast<u32>(XXH3_64bits(layout,sizeof(layout)));
        return id;
    }
    bool Code(const BuildJob& job, wire::RegionCode& out)
    {
        out.code=job.code;
        const auto nodeIndex=[&](const Node* n) {
            for(u32 i=0;i<job.nodes.size();++i) if(job.nodes[i].get()==n) return i;
            return UINT32_MAX;
        };
        for(const auto& n:job.nodes)
            out.nodes.push_back({n->pc,out.KeyIndex(n->key.bytes),n->codeOffset});
        for(const auto& site:job.sites)
        {
            wire::Placeholder ph;
            if(site.end.valid) { ph.kind=wire::kPhEnd; ph.index=out.ends.size(); out.ends.push_back({site.end}); }
            else if(site.jump)
            {
                ph.kind=wire::kPhTail; ph.index=out.tails.size();
                const u32 n=nodeIndex(site.jump); if(n==UINT32_MAX) return false;
                out.tails.push_back({n,out.KeyIndex(site.jump->jumpEnd.bytes)});
            }
            else { ph.kind=wire::kPhExit; ph.index=out.exits.size(); out.exits.push_back({site.pc,out.KeyIndex(site.key.bytes)}); }
            out.ph.push_back(ph);
        }
        for(const auto& f:job.fixups)
        {
            wire::Fixup w; w.offset=f.offset;
            switch(f.kind)
            {
                case bg::Fixup::IntraAbs: w.form=wire::kFormMovRun; w.width=4; w.target=wire::kTargetIntra; w.value=f.value; break;
                case bg::Fixup::SiteAbs: w.form=wire::kFormMovRun; w.width=4; w.target=wire::kTargetPlaceholder; w.index=f.id; break;
                case bg::Fixup::Site26: w.form=wire::kFormRel26; w.target=wire::kTargetPlaceholder; w.index=f.id; break;
                default:
                {
                    if(f.kind==bg::Fixup::OutsideAbs) { w.form=wire::kFormMovRun; w.width=f.id; }
                    else if(f.kind==bg::Fixup::Data64) w.form=wire::kFormData64;
                    else if(f.kind==bg::Fixup::External26) w.form=wire::kFormRel26;
                    else return false;
                    const u64 counters=reinterpret_cast<u64>(&job.counters);
                    if(f.value>=counters && f.value-counters<sizeof(job.counters))
                    { w.target=wire::kTargetProgram; w.value=f.value-counters; }
                    else if(const u32 n=nodeIndex(reinterpret_cast<Node*>(f.value)); n!=UINT32_MAX)
                    { w.target=wire::kTargetNodeHandle; w.index=n; }
                    else
                    {
                        mVUPersist::AddressRegion region; s64 offset;
                        if(!mVUPersist::ClassifyAddressRegion(reinterpret_cast<const void*>(f.value),region,offset)) return false;
                        w.target=region==mVUPersist::AddressRegion::Image ? wire::kTargetImage : wire::kTargetData;
                        w.value=offset;
                    }
                    break;
                }
            }
            out.fixups.push_back(w);
        }
        out.regionNodes=out.nodes.size(); out.regionBytes=out.code.size(); out.regions=1;
        return true;
    }
    void Save(microVU& mvu)
    {
        if(!mVUProgCache::SidecarsEnabled(1)) return;
        for(const auto& [program,state]:runtime.programs)
        {
            if((state.jobs.empty() && state.declined.empty()) || !program->contentKeyValid) continue;
            wire::File file; file.id=Identity(mvu,*program);
            file.tiers=state.tiers; file.settled=state.settled; file.jumpTargets=state.jumpTargets;
            file.declinedRoots=state.declined;
            for(const auto& job:state.jobs)
            {
                wire::Record record; record.pc=job->trace.startPC;
                record.kind=file.records.empty()||job->retier ? wire::kGeneration : wire::kRoot;
                record.flags=wire::kAccepted|(job->viaJump ? wire::kViaJump : 0)|(job->dropped ? wire::kStickyDropped : 0)|(job->dropAllowed ? wire::kDropSticky : 0);
                std::memcpy(record.key,job->trace.entryState,96);
                record.SetReadSet(job->read.data(),job->micro.data());
                if(Code(*job,record.code)) record.flags|=wire::kHasCode;
                else record.code={};
                // A retier replaces its generation; old jobs remain reachable
                // in memory but must not be replayed before the newer graph.
                if(job->retier) file.records.clear();
                file.records.push_back(std::move(record));
            }
            auto snapshot=std::make_shared<wire::File>(std::move(file));
            const auto hash=mVUProgCache::ProgramKey(program->contentKey);
            mVUProgCache::QueueSidecar(1,hash,[snapshot] { return wire::Serialize(*snapshot); });
        }
    }
    std::shared_ptr<BuildJob> Plan(microVU& mvu, const wire::Record& record, const Program& state)
    {
        if(!record.MatchesMicro(mvu.regs().Micro)) return {};
        auto job=std::make_shared<BuildJob>();
        job->program=mvu.prog.cur; job->generation=runtime.generation;
        job->dropAllowed=sticky::Allowed();
        job->cpu=EmuConfig.Cpu; job->fixes=EmuConfig.Gamefixes; job->speed=EmuConfig.Speedhacks;
        job->env=Environment(mvu); job->jumpTargets=state.jumpTargets;
        job->viaJump=(record.flags&wire::kViaJump)!=0;
        std::memcpy(job->micro.data(),mvu.regs().Micro,0x4000);
        PipeState key; std::memcpy(key.bytes,record.key,96);
        PipeConfig cfg; cfg.vuIndex=1; cfg.flagHack=job->env.flagHack; cfg.ibitHack=job->env.ibitHack; cfg.xgkickHack=job->env.xgkickHack;
        PipeAnalyzer(job->micro.data(),cfg).Analyze(record.pc,key).ToTrace(job->trace,1);
        for(u32 i=0;i<0x1000;++i) job->read[i]=(record.readBits[i/8]>>(i%8))&1;
        return job;
    }
    bool Install(microVU& mvu, const wire::Record& record, const std::shared_ptr<BuildJob>& job)
    {
        if(!record.HasCode()) return false;
        job->dropped=(record.flags&wire::kStickyDropped)!=0;
        if(job->dropped && !sticky::Allowed()) return false;
        const auto& code=record.code;
        // These are the only placeholder forms the local exporter creates.
        if(!code.pending.empty() || !code.blockRefs.empty()) return false;
        job->code=code.code;
        for(const auto& n:code.nodes)
        {
            auto node=std::make_unique<Node>(); node->pc=n.pc; node->codeOffset=n.entryOff; node->job=job.get();
            std::memcpy(node->key.bytes,code.keys[n.key].data(),96); job->nodes.push_back(std::move(node));
        }
        for(const auto& ph:code.ph)
        {
            Site site;
            if(ph.kind==wire::kPhEnd) site.end=code.ends[ph.index].end;
            else if(ph.kind==wire::kPhExit)
            { const auto& exit=code.exits[ph.index]; site.pc=exit.pc; std::memcpy(site.key.bytes,code.keys[exit.key].data(),96); }
            else if(ph.kind==wire::kPhTail)
            { const auto& tail=code.tails[ph.index]; site.jump=job->nodes[tail.node].get(); std::memcpy(site.jump->jumpEnd.bytes,code.keys[tail.key].data(),96); }
            else return false;
            job->sites.push_back(site);
        }
        for(const auto& f:code.fixups)
        {
            bg::Fixup live{}; live.offset=f.offset;
            if(f.target==wire::kTargetIntra)
            { live.kind=bg::Fixup::IntraAbs; live.value=f.value; }
            else if(f.target==wire::kTargetPlaceholder)
            { live.kind=f.form==wire::kFormRel26 ? bg::Fixup::Site26 : bg::Fixup::SiteAbs; live.id=f.index; }
            else
            {
                const void* address=nullptr;
                if(f.target==wire::kTargetProgram)
                { if(f.value<0 || f.value>=sizeof(job->counters)) return false; address=reinterpret_cast<const u8*>(&job->counters)+f.value; }
                else if(f.target==wire::kTargetNodeHandle) address=job->nodes[f.index].get();
                else if(f.target==wire::kTargetImage || f.target==wire::kTargetData)
                { if(!mVUPersist::ResolveAddressRegion(f.target==wire::kTargetImage ? mVUPersist::AddressRegion::Image : mVUPersist::AddressRegion::Data,f.value,address)) return false; }
                else return false;
                live.value=reinterpret_cast<u64>(address);
                if(f.form==wire::kFormData64)
                {
                    live.kind=bg::Fixup::Data64;
                    if(f.target==wire::kTargetNodeHandle && (f.offset>job->code.size() || job->code.size()-f.offset<16)) return false;
                    std::memcpy(job->code.data()+f.offset,&live.value,8);
                }
                else if(f.form==wire::kFormRel26) live.kind=bg::Fixup::External26;
                else
                {
                    live.kind=bg::Fixup::OutsideAbs; live.id=f.width;
                    // Local CodeSink emits full pointer runs. A shorter future
                    // producer must go through the plan fallback, never truncate.
                    if(f.width!=4 || !bg::PatchAbsMov(reinterpret_cast<u32*>(job->code.data()+f.offset),live.value)) return false;
                }
            }
            job->fixups.push_back(live);
        }
        job->accepted=true;
        return Publish(mvu,job);
    }
    bool Load(microVU& mvu, Program& state)
    {
        if(state.sidecarTried) return false;
        state.sidecarTried=true;
        if(!mVUProgCache::SidecarsEnabled(1) || !mvu.prog.cur->contentKeyValid) return false;
        std::vector<u8> bytes;
        if(!mVUProgCache::ReadSidecar(1,mVUProgCache::ProgramKey(mvu.prog.cur->contentKey),bytes)) return false;
        wire::File file;
        if(wire::Parse(bytes.data(),bytes.size(),Identity(mvu,*mvu.prog.cur),file)!=wire::ParseResult::Ok) return false;
        state.declined=file.declinedRoots;
        state.jumpTargets=file.jumpTargets;
        for(const auto& entry:state.jumpTargets) state.targetCount+=entry.second.size();
        state.settled=file.settled;
        bool installed=false;
        for(const auto& record:file.records)
        {
            auto job=Plan(mvu,record,state); if(!job) continue;
            if(Install(mvu,record,job))
            {
                installed=true;
                // Hydrated baseline blocks do not call Remember. Retiering must
                // still have a root trace when a new indirect target is found.
                state.traces.push_back({job->trace,job->nodes.front()->baselineBlock});
                ++runtime.stats.savedRecords;
            }
            else state.savedPlans.push_back(record);
        }
        state.tiers=std::max(state.tiers,file.tiers);
        return installed;
    }
    void StartPlan(microVU& mvu, Program& state)
    {
        while(!state.pending && !state.savedPlans.empty())
        {
            auto record=std::move(state.savedPlans.front()); state.savedPlans.pop_front();
            auto job=Plan(mvu,record,state); if(!job) continue;
            runtime.worker.Start();
            if(runtime.worker.Submit(job)) { state.pending=job; ++runtime.stats.savedPlanJobs; }
        }
    }
}
