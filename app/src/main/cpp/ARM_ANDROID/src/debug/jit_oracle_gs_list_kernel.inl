// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after the packed/strip/mirror/carrying/outline helpers. Native LIST RED.
namespace GSListKernelOracle
{
    struct Work {u64 chunks=0,vertices=0,outlined=0,inlineChunks=0,outlinedVertices=0;};
    void Configure(u32 mode)
    {
        EmuCoreXOracleSetGSPackedBatch(mode!=0,false);
        EmuCoreXOracleSetGSStripKernel(mode==2,true,0);
        EmuCoreXOracleSetGSNativeMirror(mode!=0,false,0);
        EmuCoreXOracleSetGSStripOutline(true,true);
    }
    void Collect(Work& w)
    {
        u64 kernel[4]{},outline[3]{};EmuCoreXOracleGSStripKernelStats(kernel);EmuCoreXOracleGSStripOutlineStats(outline);
        w.chunks+=kernel[0];w.vertices+=kernel[1];w.outlined+=outline[0];w.inlineChunks+=outline[1];w.outlinedVertices+=outline[2];
    }
    std::vector<GIFPackedReg> Packet(u32 count,bool fog,u32 shape=0,u32 base=0)
    {
        auto packet=GSVertexPacket(count,fog,false);GSStripKernelOracle::Regular(packet,0);
        constexpr u32 qs[]={0u,0x80000000u,0x7fc12345u,0x7f800000u,0xff800000u,0xbf800000u,1u,0x3f800000u};
        for(u32 i=0;i<count;++i)
        {
            auto* r=packet.data()+1+i*3;r[0].U32[2]=qs[i%std::size(qs)];
            auto& xyz=r[2];
            if(i/3==1)
            {
                if(shape==1&&i%3==2)xyz.U32[3]|=0x8000u;
                if(shape==2){xyz.XYZ2.X=100;xyz.XYZ2.Y=100;}
                if(shape==3){xyz.XYZ2.X=101+i%3;xyz.XYZ2.Y=101+(i+1)%3;}
                if(shape==4){xyz.XYZ2.X+=20000;xyz.XYZ2.Y+=20000;}
            }
            if(shape==5&&i%3==2)xyz.U32[3]|=0x8000u;
            if(base){xyz.XYZ2.X+=base;xyz.XYZ2.Y+=base;}
        }
        return packet;
    }
    std::array<u32,8> RawVertex(const GIFPackedReg* r,bool fog,u32 depth)
    {
        u32 color=0;for(u32 lane=0;lane<4;++lane)color|=(r[1].U32[lane]&255u)<<(lane*8);
        u32 z=fog?(r[2].U32[2]>>4)&0xffffffu:r[2].U32[2];
        if(depth==static_cast<u32>(GSLimit24BitDepth::PrioritizeUpper))z=((z>>8)&~255u)|(z&255u);
        if(depth==static_cast<u32>(GSLimit24BitDepth::PrioritizeLower))z&=0xffffffu;
        return {r[0].U32[0],r[0].U32[1],color,r[0].U32[2]?r[0].U32[2]:0x00800000u,
            (r[2].U32[0]&65535u)|((r[2].U32[1]&65535u)<<16),z,0x24681357u,
            fog?(r[2].U32[3]>>4)&255u:0x89abcdefu};
    }
    bool Literal(bool& rewind)
    {
        bool bytes=true;rewind=true;
        auto state=std::make_unique<GSVertexPacketOracle>();
        for(bool fog:{false,true})for(u32 depth=0;depth<3;++depth)for(u32 shape=0;shape<6;++shape)
        {
            GSConfig.UserHacks_Limit24BitDepth=static_cast<GSLimit24BitDepth>(depth);Configure(2);
            state->Reset(true);state->Prepare(GS_TRIANGLELIST,true);const auto packet=Packet(9,fog,shape);
            GSMirrorQuadOracle::GuardedInput guarded(packet);if(!guarded.packet){bytes=rewind=false;continue;}
            state->Transfer<3>(reinterpret_cast<const u8*>(guarded.packet),packet.size());
            const auto out=state->Snapshot();if(out.size()<152){bytes=rewind=false;continue;}
            u32 head,tail,next,xyTail,itail;
            std::memcpy(&head,out.data()+36,4);std::memcpy(&tail,out.data()+40,4);std::memcpy(&next,out.data()+44,4);
            std::memcpy(&xyTail,out.data()+48,4);std::memcpy(&itail,out.data()+132,4);
            std::vector<u32> accepted;
            for(u32 i=0;i<9;++i)if(shape==0||(shape!=5&&i/3!=1))accepted.push_back(i);
            const u32 expected=accepted.size();rewind&=head==expected&&tail==expected&&next==expected&&itail==expected&&xyTail==9;
            if(out.size()!=136+expected*32+expected*2+16){bytes=false;continue;}
            for(u32 i=0;i<expected;++i)
            {
                const auto raw=RawVertex(packet.data()+1+accepted[i]*3,fog,depth);
                bytes&=std::memcmp(out.data()+136+i*32,raw.data(),32)==0;
                u16 index;std::memcpy(&index,out.data()+136+expected*32+i*2,2);bytes&=index==i;
            }
            const auto current=RawVertex(packet.data()+1+8*3,fog,depth);
            bytes&=std::memcmp(out.data(),current.data(),32)==0;
            u32 latch;std::memcpy(&latch,out.data()+32,4);bytes&=latch==packet[1+8*3].U32[2];
            // xy_tail follows every raw kick, including rejected list groups.
            for(u32 i=5;i<9;++i)
            {
                const auto& xyz=packet[1+i*3+2];const std::array<u32,4> xy{xyz.U32[0]&65535u,xyz.U32[1]&65535u,xyz.U32[0]&65535u,xyz.U32[1]&65535u};
                rewind&=std::memcmp(out.data()+52+(i&3)*16,xy.data(),16)==0;
            }
        }
        return bytes;
    }
    bool Matrix(Work (&work)[2],u64& cases)
    {
        auto state=std::make_unique<GSPackedBatchOracle>();bool exact=true;
        constexpr u32 counts[]={0,1,2,3,4,5,6,7,8,127,128,129,257};
        for(bool fog:{false,true})for(u32 count:counts)for(u32 pending:{0u,1u,2u})
        for(u32 depth=0;depth<3;++depth)for(u32 shape:{0u,1u,2u,3u,4u,5u})for(u32 base:{0u,32768u})
        {
            GSConfig.UserHacks_Limit24BitDepth=static_cast<GSLimit24BitDepth>(depth);
            const auto prefix=Packet(pending,fog,0,base),packet=Packet(count,fog,shape,base);
            GSMirrorQuadOracle::GuardedInput guarded(packet);if(!guarded.packet){exact=false;continue;}
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                GSNativeMirrorOracle::Prepare(*state,GS_TRIANGLELIST,true,base!=0);Configure(mode);
                state->Submit(prefix,0);state->Transfer<3>(reinterpret_cast<const u8*>(guarded.packet),packet.size());
                const auto actual=GSNativeMirrorOracle::Capture(*state);
                if(!mode)expected=actual;else exact&=actual==expected;
                if(mode==2)Collect(work[fog?0:1]);
            }
            ++cases;
        }
        return exact;
    }
    bool Flush()
    {
        bool exact=true;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
        for(bool fog:{false,true})for(u32 shape:{0u,1u})
        {
            auto state=std::make_unique<GSPackedBatchOracle>();const auto packet=Packet(22000,fog,shape);std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                GSNativeMirrorOracle::Prepare(*state,GS_TRIANGLELIST);Configure(mode);
                for(u32 i=0;i<3;++i)state->Submit(packet,0);
                const auto actual=GSNativeMirrorOracle::Capture(*state);exact&=state->vertexFlushes>0;
                if(!mode)expected=actual;else exact&=actual==expected;
            }
        }
        return exact;
    }
    bool Fallback()
    {
        bool exact=true;const auto packet=Packet(257,true);
        for(u32 variant=0;variant<7;++variant)
        {
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                auto state=std::make_unique<GSPackedBatchOracle>();
                const u32 prim=variant==5?GS_TRIANGLEFAN:variant==6?GS_SPRITE:GS_TRIANGLELIST;
                GSNativeMirrorOracle::Prepare(*state,prim,variant!=0,false,variant==1,variant==2);
                if(variant==3)state->SetRawXYOffsetForStrip(0x0001800000018000ull);
                if(variant==4)
                {
                    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Enabled;GSConfig.AutoFlushSW=true;
                    state->Prepare(prim,true,false,false,true);state->SetTraceFlags(true,false,true,false);state->SeedCarryingLayout();
                }
                Configure(mode);state->Submit(packet,0);const auto actual=GSNativeMirrorOracle::Capture(*state);
                if(!mode)expected=actual;else exact&=actual==expected;
                if(mode==2){u64 stats[4]{};EmuCoreXOracleGSStripKernelStats(stats);exact&=stats[0]==0;}
                GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
            }
        }
        return exact;
    }
    bool DetectMutation(u32 mutation)
    {
        const auto packet=Packet(257,true,1);std::vector<u8> reference;
        for(u32 value:{0u,mutation})
        {
            auto state=std::make_unique<GSPackedBatchOracle>();GSNativeMirrorOracle::Prepare(*state,GS_TRIANGLELIST);Configure(2);
            EmuCoreXOracleSetGSStripKernel(true,true,value);
            std::vector<u8> actual;
            if(mutation==1)
            {
                // Both tags fit the real16-vertex logical capacity and each
                // enters the kernel once. Observe before a scalar tail can
                // repair the omitted ring; seven inputs leave one pending.
                state->Submit(Packet(7,true,1),0);actual=GSNativeMirrorOracle::Capture(*state);
                state->Submit(Packet(8,true),0);const auto after=GSNativeMirrorOracle::Capture(*state);
                actual.insert(actual.end(),after.begin(),after.end());
                u64 work[4]{};EmuCoreXOracleGSStripKernelStats(work);
                if(work[0]!=2||work[1]!=15)return false;
            }
            else {state->Submit(packet,0);actual=GSNativeMirrorOracle::Capture(*state);}
            if(!value)reference=actual;else return actual!=reference;
        }
        return false;
    }
    bool Seams()
    {
        bool exact=true;std::vector<std::vector<u8>> expected;
        for(u32 mode=0;mode<3;++mode)
        {
            auto state=std::make_unique<GSPackedBatchOracle>();GSNativeMirrorOracle::Prepare(*state,GS_TRIANGLELIST);Configure(mode);
            std::vector<std::vector<u8>> stages;
            const auto submit=[&](const std::vector<GIFPackedReg>& packet){state->Submit(packet,0);stages.push_back(GSNativeMirrorOracle::Capture(*state));};
            submit(Packet(2,false));submit(GSNativeMirrorOracle::Scissor(600,600,800,800));submit(Packet(7,true));
            submit(GSNativeMirrorOracle::Scissor(0,0,1023,1023));submit(Packet(129,true));
            std::vector<GIFPackedReg> prim(2);prim[0]=GSCarryingLayoutOracle::Tag({GIF_REG_A_D},1);prim[1].A_D.ADDR=GIF_A_D_REG_PRIM;
            prim[1].r.PRIM.PRIM=GS_TRIANGLESTRIP;submit(prim);submit(Packet(257,false));submit(GSNativeMirrorOracle::Singles(7));
            prim[1].r.PRIM.PRIM=GS_TRIANGLELIST;submit(prim);submit(GSNativeMirrorOracle::Offset(32768,32768));submit(Packet(129,false,0,32768));
            prim[1].r.PRIM.CTXT=1;submit(GSNativeMirrorOracle::Scissor(1,1,100,100,1));submit(prim);submit(Packet(129,true));
            if(!mode)expected=stages;else exact&=stages==expected;
        }
        return exact;
    }
}
void GSListKernelTests()
{
    using namespace GSListKernelOracle;const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSPackedBatch(true,false);
        EmuCoreXOracleSetGSStripKernel(true,false,0);EmuCoreXOracleSetGSNativeMirror(true,false,0);EmuCoreXOracleSetGSStripOutline(true,false);});
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
    GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    bool rewind=true;Check(Literal(rewind),"native list raw vertices sequential indices Q current depth and fog match independent literal bytes");
    Check(rewind,"native list ADC duplicate narrow and outside groups rewind head tail next while raw XY ring keeps every kick");
    Check(GSNativeMirrorOracle::Raster(),"native list triangle cull retains independently proved strict-interior native samples");
    Work work[2]{};u64 cases=0;Check(Matrix(work,cases),"native list staged direct and candidate paths preserve complete protected-packet state and FMM at every boundary");
    Check(Flush(),"native list preserves actual65532vertex Flush growth retained state current registers FMM and draws");
    Check(Fallback(),"native list unsupported scale AA1 clip offset autoflush fan and sprite stay exact without list kernel work");
    Check(Seams(),"native list mixed strip context scissor and XYOFFSET transitions preserve every complete intermediate state");
    bool both=true,one=true;
    for(u32 i=0;i<2;++i)
    {
        const auto& w=work[i];both&=w.chunks>0&&w.vertices>0;
        one&=w.chunks>0&&w.outlined==w.chunks&&w.outlinedVertices==w.vertices&&w.inlineChunks==0;
        std::printf("GS_LIST_KERNEL family=%u chunks=%llu vertices=%llu outlined=%llu inline=%llu outlined_vertices=%llu\n",i,
            static_cast<unsigned long long>(w.chunks),static_cast<unsigned long long>(w.vertices),static_cast<unsigned long long>(w.outlined),
            static_cast<unsigned long long>(w.inlineChunks),static_cast<unsigned long long>(w.outlinedVertices));
    }
    Check(both,"both real contiguous triple list handlers execute actual two-pass kernel work");
    Check(one,"one actual outlined entry owns each eligible list chunk without duplicate inline work");
    if(both)
    {
        Check(DetectMutation(1),"native list oracle detects omitted actual XY ring writeback before scalar repair and across a pending group");
        Check(DetectMutation(3),"native list oracle detects wrong strip-style head advance instead of rejected-list tail rewind");
    }
    std::printf("GS_LIST_MATRIX cases=%llu\n",static_cast<unsigned long long>(cases));
}
