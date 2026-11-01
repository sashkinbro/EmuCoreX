// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native local-FMM RED scaffold; include after list/outline helpers.
#pragma once
extern "C" void EmuCoreXOracleSetGSLocalFmm(bool enabled,bool observe);
extern "C" void EmuCoreXOracleGSLocalFmmStats(u64* values); // local chunks, initialized acc copies, touched acc commits
extern "C" u64 EmuCoreXOracleGSMemberFmmChunks();
extern "C" void EmuCoreXOracleSetGSLocalFmmMutation(u32 value);
namespace GSLocalFmmOracle
{
    using State=GSFusedVertexTrace::State;
    static_assert(offsetof(State,watermark)==112);
    struct GuardedOwner
    {
        void* mapping=MAP_FAILED;size_t page=0;State* owner=nullptr;
        GuardedOwner()
        {
            page=static_cast<size_t>(sysconf(_SC_PAGESIZE));if(page<sizeof(State))return;
            mapping=mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(mapping==MAP_FAILED)return;
            owner=new(static_cast<u8*>(mapping)+page-112) State;
            std::memset(owner,0xa5,sizeof(State));
            owner->watermark=111;owner->indices=3;owner->vertexEnd=222;owner->key=5;owner->valid=false;
        }
        bool Protect(int flags){return owner&&mprotect(mapping,page,flags)==0;}
        ~GuardedOwner(){if(mapping!=MAP_FAILED){mprotect(mapping,page,PROT_READ|PROT_WRITE);owner->~State();munmap(mapping,page*2);}}
    };
    // Access only the protected cursor TYPE, never construct GSState or replace
    // its emitter. This calls the exact production RunChunk specialization.
    class Access:public GSState
    {
    public:
        static bool Run(State& trace,u32 scenario)
        {
            const bool fresh=scenario==5;
            const u32 prefix=fresh?2:scenario<2?5:4;
            std::array<GSVertex,64> vertices{};std::array<u16,192> indices{};
            std::array<GSVector4i,4> ring{};std::array<GSStripKickKernel::Entry,4> seed{};
            std::array<u64,128> sideXY{},sideMeta{};
            constexpr u32 xs[]={100,400,100,400},ys[]={100,100,400,400};
            const GSStripKickKernel::Bounds bounds{0,0,1024,1024};
            for(u32 i=0;i<prefix;++i)
            {
                const u32 x=i>=3&&scenario<2?100:xs[i&3],y=i>=3&&scenario<2?100:ys[i&3];
                const std::array<u32,8> raw{0x3fa00000,0xc0200000,0x44332211,0x40000000,x|(y<<16),0x12345678,0x24681357,31};
                std::memcpy(&vertices[i],raw.data(),32);ring[i&3]=GSVector4i(x,y,x,y);
            }
            for(u32 i=0;i<4;++i)seed[i]=GSStripKickKernel::MakeEntry(ring[i].x,ring[i].y,bounds);
            auto packet=GSVertexPacket(6,false,false);
            for(u32 i=0;i<6;++i)
            {
                auto* r=packet.data()+1+i*3;r[0].U32[0]=0x3fa00000;r[0].U32[1]=0xc0200000;r[0].U32[2]=0x40000000;
                for(u32 lane=0;lane<4;++lane)r[1].U32[lane]=(0x44332211u>>(lane*8))&255u;
                r[2].XYZ2.X=scenario==1?20100:xs[(i+prefix)&3];r[2].XYZ2.Y=scenario==1?20100:ys[(i+prefix)&3];
                r[2].U32[2]=0x12345678;r[2].U32[3]=(r[2].U32[3]&~0x8000u)|(scenario==0?0x8000u:0u);
            }
            GSMirrorQuadOracle::GuardedInput guarded(packet);if(!guarded.packet)return false;
            VertexKickCursor cursor{vertices.data(),indices.data(),fresh?0u:scenario<2?3u:2u,prefix,
                fresh?0u:3u,prefix,64,fresh?0u:3u};
            using Parser=GSCarryingVertex::TripleBatchParser<false>;
            const Parser parser{(31ull<<32)|0x24681357,GSLimit24BitDepth::Disabled};
            const GSStripKickKernel::ChunkInvariants<Parser> inv{parser,bounds,0,0,seed.data(),ring.data(),sideXY.data(),sideMeta.data(),&trace,5};
            GSStripKickKernel::ChunkOutput output;
            GSStripKickKernel::RunChunk<GS_TRIANGLESTRIP,true,false>(guarded.packet+1,6,cursor,inv,output);
            return true;
        }
    };
    bool Guard(bool mismatch)
    {
        bool exact=true;
        for(u32 scenario=mismatch?3u:0u;scenario<(mismatch?5u:3u);++scenario)for(bool local:{false,true})
        {
            GuardedOwner guard;if(!guard.owner){exact=false;continue;}
            auto& owner=*guard.owner;
            if(mismatch){owner.valid=true;if(scenario==3)owner.key=0;else owner.indices=99;}
            const u32 oldWatermark=owner.watermark,oldIndices=owner.indices,oldEnd=owner.vertexEnd,oldKey=owner.key;
            std::array<u8,112> before{};std::memcpy(before.data(),&owner.acc,before.size());
            if(!guard.Protect(mismatch?PROT_READ:PROT_NONE)){exact=false;continue;}
            EmuCoreXOracleSetGSLocalFmm(local,true);exact&=Access::Run(owner,scenario);
            exact&=!owner.valid&&owner.watermark==oldWatermark&&owner.indices==oldIndices&&owner.vertexEnd==oldEnd&&owner.key==oldKey;
            exact&=guard.Protect(PROT_READ|PROT_WRITE)&&std::memcmp(&owner.acc,before.data(),before.size())==0;
        }
        return exact;
    }
    bool Golden()
    {
        bool exact=true;
        for(bool local:{false,true})
        {
            GuardedOwner guard;if(!guard.owner){exact=false;continue;}
            EmuCoreXOracleSetGSLocalFmm(local,true);exact&=Access::Run(*guard.owner,5);const auto& s=*guard.owner;
            const GSVector4i pmin(100,100,0x12345678,31),pmax(400,400,0x12345678,31);
            GSVector4i color;color.U32[0]=0x3fa00000;color.U32[1]=0xc0200000;color.U32[2]=0x44332211;color.U32[3]=0x40000000;
            const GSVector4 texture(0.625f,-1.25f,2.f,2.f);const GSVector4i zero=GSVector4i::zero();
            exact&=s.valid&&s.key==5&&s.indices==18&&s.watermark==8&&s.vertexEnd==8&&
                std::memcmp(&s.acc.pmin,&pmin,16)==0&&std::memcmp(&s.acc.pmax,&pmax,16)==0&&
                std::memcmp(&s.acc.cmin,&color,16)==0&&std::memcmp(&s.acc.cmax,&color,16)==0&&
                std::memcmp(&s.acc.tmin,&texture,16)==0&&std::memcmp(&s.acc.tmax,&texture,16)==0&&std::memcmp(&s.acc.tnan,&zero,16)==0;
        }
        return exact;
    }
    bool DetectCommitMutation()
    {
        GuardedOwner expected,mutant;if(!expected.owner||!mutant.owner)return false;
        EmuCoreXOracleSetGSLocalFmm(true,true);if(!Access::Run(*expected.owner,5))return false;
        u64 good[3]{};EmuCoreXOracleGSLocalFmmStats(good);
        EmuCoreXOracleSetGSLocalFmm(true,true);EmuCoreXOracleSetGSLocalFmmMutation(1);
        const bool ran=Access::Run(*mutant.owner,5);u64 bad[3]{};EmuCoreXOracleGSLocalFmmStats(bad);
        EmuCoreXOracleSetGSLocalFmmMutation(0);if(!ran)return false;
        const auto& a=*expected.owner;const auto& b=*mutant.owner;
        GSVertexTrace lhs(nullptr),rhs(nullptr);GIFRegXYOFFSET offset{};
        GSFusedVertexTrace::Finish(a.acc,true,false,true,offset,5,6,lhs);
        GSFusedVertexTrace::Finish(b.acc,true,false,true,offset,5,6,rhs);
        return a.valid&&b.valid&&a.indices==18&&b.indices==18&&a.key==b.key&&a.watermark==b.watermark&&
            good[2]==1&&bad[2]==0&&std::memcmp(&a.acc,&b.acc,sizeof(a.acc))!=0&&
            (std::memcmp(&lhs.m_min,&rhs.m_min,sizeof(lhs.m_min))!=0||std::memcmp(&lhs.m_max,&rhs.m_max,sizeof(lhs.m_max))!=0);
    }
    struct Work {u64 kernel=0,local=0,copies=0,commits=0,member=0;};
    void Configure(bool local)
    {
        EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSCarryingLayouts(true,false);
        EmuCoreXOracleSetGSStripKernel(true,true,0);EmuCoreXOracleSetGSNativeMirror(true,false,0);
        EmuCoreXOracleSetGSStripOutline(true,false);EmuCoreXOracleSetGSLocalFmm(local,true);
    }
    bool Streams(std::array<Work,7>& work)
    {
        bool exact=true;
        for(u32 family=0;family<7;++family)for(u32 count:{6u,7u,8u,127u,128u,129u,257u})
        for(u32 pending:{0u,1u,2u})for(u32 key=0;key<8;++key)
        {
            auto state=std::make_unique<GSPackedBatchOracle>();const u32 prim=family<2||family>=4?GS_TRIANGLESTRIP:GS_TRIANGLELIST;
            const u32 carrying=family==4?0:family==5?3:4;
            auto prefix=GSVertexPacket(pending,true,false);GSStripKernelOracle::Regular(prefix,0);
            auto packet=family<4?GSVertexPacket(count,(family&1)==0,false):GSCarryingQuadOracle::ProductionPacket(carrying,1,count,2);
            if(family<4)GSStripKernelOracle::Regular(packet,2);
            std::vector<u8> expected;
            for(bool local:{false,true})
            {
                GSNativeMirrorOracle::Prepare(*state,prim);state->SetTraceFlags(key&1,key&2,key&4,false);Configure(local);
                state->Submit(prefix,0);if(family<4)state->Submit(packet,0);else state->SubmitCarrying(packet,0,3);
                const auto actual=GSNativeMirrorOracle::Capture(*state);
                if(!local)expected=actual;else
                {
                    exact&=actual==expected;u64 k[4]{},l[3]{};EmuCoreXOracleGSStripKernelStats(k);EmuCoreXOracleGSLocalFmmStats(l);
                    work[family].kernel+=k[0];work[family].local+=l[0];work[family].copies+=l[1];work[family].commits+=l[2];work[family].member+=EmuCoreXOracleGSMemberFmmChunks();
                }
            }
        }
        return exact;
    }
    bool Flush()
    {
        bool exact=true;
        for(u32 prim:{GS_TRIANGLESTRIP,GS_TRIANGLELIST})for(bool fog:{false,true})
        {
            auto state=std::make_unique<GSPackedBatchOracle>();auto packet=GSVertexPacket(22000,fog,false);GSStripKernelOracle::Regular(packet,0);
            std::vector<u8> expected;
            for(bool local:{false,true})
            {
                GSNativeMirrorOracle::Prepare(*state,prim);Configure(local);for(u32 i=0;i<3;++i)state->Submit(packet,0);
                const auto actual=GSNativeMirrorOracle::Capture(*state);exact&=state->vertexFlushes>0;
                if(!local)expected=actual;else exact&=actual==expected;
            }
        }
        return exact;
    }
}
void GSLocalFmmTests()
{
    using namespace GSLocalFmmOracle;const auto settings=GSConfig;
    const ScopedGuard restore([&](){GSConfig=settings;EmuCoreXOracleSetGSLocalFmm(true,false);
        EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSCarryingLayouts(true,false);
        EmuCoreXOracleSetGSStripKernel(true,false,0);EmuCoreXOracleSetGSNativeMirror(true,false,0);EmuCoreXOracleSetGSStripOutline(true,false);});
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
    GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    Check(Guard(false),"actual pure chunk never reads or writes protected invalid accumulator for ADC rejection and existing-draw acceptance");
    Check(Guard(true),"actual key and index mismatches invalidate metadata without writing unchanged read-only accumulator");
    Check(Golden(),"actual first emission resets poison and matches independent finite seven-vector FMM and metadata golden");
    std::array<Work,7> work{};Check(Streams(work),"local and member FMM preserve complete seven-parser shading pending compaction raw state and trace snapshots");
    Check(Flush(),"local and member FMM preserve actual growth vertex-count Flush current registers and complete draws");
    bool all=true;u64 copies=0,commits=0;
    for(u32 family=0;family<work.size();++family)
    {
        const auto& w=work[family];all&=w.kernel>0&&w.local==w.kernel&&w.member==0;copies+=w.copies;commits+=w.commits;
        std::printf("GS_LOCAL_FMM family=%u kernel=%llu local=%llu copies=%llu commits=%llu member=%llu\n",family,
            static_cast<unsigned long long>(w.kernel),static_cast<unsigned long long>(w.local),static_cast<unsigned long long>(w.copies),static_cast<unsigned long long>(w.commits),static_cast<unsigned long long>(w.member));
    }
    Check(all,"all seven actual production parser kernels execute local fused-FMM ownership once per chunk");
    Check(copies>0&&commits>0,"actual local FMM reads initialized cache and commits touched accumulation at chunk exit");
    if(all)Check(DetectCommitMutation(),"actual skipped accumulator commit corrupts the production FMM consumer despite matching valid metadata");
}
