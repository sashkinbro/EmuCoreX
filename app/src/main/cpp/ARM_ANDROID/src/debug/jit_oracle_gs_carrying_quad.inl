// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after mirror_quad and carrying_layouts; uses the actual BatchParser.
extern "C" void EmuCoreXOracleGSCarryingParse(u32 layout,const GIFPackedReg* packet,u32 count,
    GSVertex* vertices,u64* xy,u64* meta,const GIFPackedLayout* offsets,u64 uvfog,
    const GSVector4i* current,u32 latch,const s32* bounds,s32 ofx,s32 ofy,u32 depth);
namespace GSCarryingQuadOracle
{
    struct Work {u64 quads=0,scalar=0,tail=0,expectedQuads=0,expectedSmall=0,expectedTail=0,cases=0,vertices=0;};
    bool Raw(u32 family,Work& work)
    {
        using namespace GSCarryingLayoutOracle;
        bool exact=true;
        constexpr u32 counts[]={0,1,2,3,4,5,6,7,9,31,32,127,128};
        constexpr u16 edges[]={0,1,15,16,17,32767,32768,65534,65535};
        constexpr u32 qbits[]={0,0x80000000u,0x7fc12345u,0x7f800000u,0xff800000u,0xbf800000u,1,0x3f800000u};
        constexpr u32 normalized[]={0x00800000u,0x80000000u,0x7f7fffffu,0x7f800000u,0xff800000u,0xbf800000u,1,0x3f800000u};
        constexpr u32 latches[]={0x40000000u,0u,0x80000000u,0x7fcabcdeu,0xbf800000u,0x7f800000u};
        constexpr s32 offsets[][2]={{0,0},{32768,32768},{65535,0},{0,65535}};
        constexpr s32 bounds[][4]={{0,0,4096,4096},{-4096,-4096,4096,4096},{-1,-1,1,1},{4095,4095,4096,4096}};
        constexpr u64 uvfog=0x89abcdef81238023ull,guard=0xbadc0ffee0ddf00dull;
        GSVector4i current;current.U32[0]=0x3fa00000;current.U32[1]=0xc0200000;current.U32[2]=0x44332211;current.U32[3]=0x40400000;
        u32 rng=0x6294eb31^family;
        const auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
        EmuCoreXOracleSetGSCarryingLayouts(true,false);EmuCoreXOracleSetGSMirrorObserve(true);
        for(u32 padding=0;padding<(family>=1&&family<=3?5u:4u);++padding)
        {
            const auto descriptors=Descriptors(family,padding);const auto tag=Tag(descriptors,7);
            u32 info[8]{};const bool recognized=EmuCoreXOracleGSClassifyCarryingLayout(&tag,info);
            exact&=recognized&&info[0]==family+4;if(!recognized)continue;
            const GIFPackedLayout off{info[4],info[5],info[6],info[7]};
            for(u32 count:counts)for(u32 depth=0;depth<3;++depth)for(u32 latch:latches)for(const auto& offset:offsets)
            {
                std::vector<GIFPackedReg> packet(count*off.stride);
                for(u32 i=0;i<count;++i)
                {
                    auto* r=packet.data()+i*off.stride;
                    for(u32 j=0;j<off.stride;++j)for(auto& w:r[j].U32)w=next();
                    if(family==0||family==1)
                    {r[off.off_a].U32[0]=0x3f000000|(i*71&0x7fffff);r[off.off_a].U32[1]=0x3f400000|(i*97&0x3fffff);r[off.off_a].U32[2]=qbits[(i+work.cases)%std::size(qbits)];}
                    if(family==2||family==4){r[off.off_a].U32[0]=0xfffff123u+i*19;r[off.off_a].U32[1]=0xabcde456u+i*31;}
                    if(family==0||family==3||family==4)
                    {const u32 rgba=family==3?off.off_a:off.off_rgba;for(u32 lane=0;lane<4;++lane)r[rgba].U32[lane]=0xcafe0000u|((i*47+lane*31)&255);}
                    auto& xyz=r[off.off_xyz];xyz.U32[0]=(xyz.U32[0]&0xffff0000u)|edges[(i+work.cases)%std::size(edges)];
                    xyz.U32[1]=(xyz.U32[1]&0xffff0000u)|edges[(i*3+work.cases)%std::size(edges)];
                    xyz.U32[3]=(xyz.U32[3]&~0x8000u)|((i+work.cases)%3?0x8000u:0u);
                }
                GSMirrorQuadOracle::GuardedInput input(packet);exact&=input.packet!=nullptr;if(!input.packet)continue;
                std::vector<GSVertex> out(count+2);std::memset(out.data(),0xa5,out.size()*sizeof(GSVertex));
                const GSVertex before=out.front(),after=out.back();std::vector<u64> xy(count+2,guard),meta(count+2,guard);
                const auto& bound=bounds[work.cases%std::size(bounds)];
                EmuCoreXOracleGSCarryingParse(family,input.packet,count,out.data()+1,xy.data()+1,meta.data()+1,&off,uvfog,&current,latch,bound,offset[0],offset[1],depth);
                exact&=std::memcmp(&out.front(),&before,sizeof(GSVertex))==0&&std::memcmp(&out.back(),&after,sizeof(GSVertex))==0&&xy.front()==guard&&xy.back()==guard&&meta.front()==guard&&meta.back()==guard;
                for(u32 i=0;i<count;++i)
                {
                    const auto* r=packet.data()+i*off.stride;const auto& xyz=r[off.off_xyz];
                    const bool fog=family==0||family==4;
                    std::array<u32,8> v{current.U32[0],current.U32[1],current.U32[2],current.U32[3],
                        (xyz.U32[0]&65535)|((xyz.U32[1]&65535)<<16),fog?(xyz.U32[2]>>4)&0xffffffu:xyz.U32[2],static_cast<u32>(uvfog),
                        fog?(xyz.U32[3]>>4)&255u:static_cast<u32>(uvfog>>32)};
                    if(family==0||family==1){v[0]=r[off.off_a].U32[0];v[1]=r[off.off_a].U32[1];}
                    if(family==0)v[3]=normalized[(i+work.cases)%std::size(normalized)];
                    if(family==3||family==4)v[3]=latch;
                    if(family==0||family==3||family==4)
                    {v[2]=0;const u32 rgba=family==3?off.off_a:off.off_rgba;for(u32 lane=0;lane<4;++lane)v[2]|=(r[rgba].U32[lane]&255)<<(lane*8);}
                    if(family==2||family==4)v[6]=(r[off.off_a].U32[0]&0x3fff)|((r[off.off_a].U32[1]&0x3fff)<<16);
                    if(depth==static_cast<u32>(GSLimit24BitDepth::PrioritizeUpper))v[5]=((v[5]>>8)&~255u)|(v[5]&255u);
                    if(depth==static_cast<u32>(GSLimit24BitDepth::PrioritizeLower))v[5]&=0xffffffu;
                    exact&=std::memcmp(&out[i+1],v.data(),sizeof(GSVertex))==0;
                    const s32 x=static_cast<s32>(xyz.U32[0]&65535)-offset[0],y=static_cast<s32>(xyz.U32[1]&65535)-offset[1];
                    const s32 bx=GSMirrorQuadOracle::Floor16(x-1),by=GSMirrorQuadOracle::Floor16(y-1);
                    const u32 oc=(bx<bound[0]?1u:0u)|(bx>=bound[2]?2u:0u)|(by<bound[1]?4u:0u)|(by>=bound[3]?8u:0u);
                    const u64 expectedXY=static_cast<u32>(x)|(static_cast<u64>(static_cast<u32>(y))<<32);
                    const u64 expectedMeta=(static_cast<u64>(static_cast<u32>(bx))&0xfffffffull)|((static_cast<u64>(static_cast<u32>(by))<<28)&0x00fffffff0000000ull)|
                        (static_cast<u64>(oc)<<56)|(xyz.U32[3]&0x8000u?1ull<<60:0ull);
                    exact&=xy[i+1]==expectedXY&&meta[i+1]==expectedMeta;
                }
                const bool rowsConsumer=family==0||family==3||family==4;
                work.expectedSmall+=rowsConsumer?(count<4?count:0):count;
                work.expectedQuads+=rowsConsumer&&count>=4?count/4+(count%4!=0):0;
                work.expectedTail+=rowsConsumer&&count>=4&&count%4!=0;++work.cases;work.vertices+=count;
            }
        }
        u64 stats[3]{};EmuCoreXOracleGSMirrorStats(stats);work.quads=stats[0];work.scalar=stats[1];work.tail=stats[2];return exact;
    }
    std::vector<GIFPackedReg> ProductionPacket(u32 family,u32 padding,u32 count,u32 variant)
    {
        auto packet=GSCarryingLayoutOracle::Packet(family,padding,count,variant);
        if(family==1||family==2)
        {
            const auto regs=GSCarryingLayoutOracle::Descriptors(family,padding);u32 vertex=0;
            for(u32 i=0;i<count;++i)for(u32 at=0;at<regs.size();++at)
                if(regs[at]==GIF_REG_XYZ2)
                {
                    auto& xyz=packet[1+i*regs.size()+at];const u32 value=(vertex++&1)?400:100;
                    xyz.XYZ2.X=value;xyz.XYZ2.Y=value;
                }
        }
        return packet;
    }
    bool State(u32 family,u64& quads,u64& scalar)
    {
        using namespace GSCarryingLayoutOracle;bool exact=true;
        const bool rowsConsumer=family==0||family==3||family==4;
        const u32 prim=rowsConsumer?GS_TRIANGLESTRIP:GS_SPRITE;
        u64 handlers=0,kernelVertices=0;
        auto state=std::make_unique<GSPackedBatchOracle>();state->captureTrace=true;
        for(u32 padding=0;padding<(family>=1&&family<=3?5u:4u);++padding)
        for(u32 count:{0u,1u,2u,5u,6u,7u,127u,128u,129u,257u})for(u32 path:{2u,3u})for(u32 pending:{0u,1u,2u})
        {
            const auto packet=ProductionPacket(family,padding,count,2);auto prefix=GSVertexPacket(pending,true,false);GSStripKernelOracle::Regular(prefix,0);
            std::vector<u8> reference,draws;
            for(bool candidate:{false,true})
            {
                EmuCoreXOracleSetGSCarryingLayouts(candidate,true);EmuCoreXOracleSetGSStripKernel(true,true,0);
                state->Prepare(prim,true,false,false,true);state->SetTraceFlags(true,false,true,false);state->SeedCarryingLayout(0x7fcabcde);
                state->Submit(prefix,0);EmuCoreXOracleSetGSMirrorObserve(true);state->SubmitCarrying(packet,0,path);
                auto actual=state->CarryingSnapshot();const auto trace=state->TraceNow();actual.insert(actual.end(),trace.begin(),trace.end());
                state->SubmitCarrying(LatchProbe(),0,path);const auto latch=state->CarryingSnapshot();actual.insert(actual.end(),latch.begin(),latch.end());
                if(!candidate){reference=actual;draws=state->draws;}else
                {exact&=actual==reference&&state->draws==draws;u64 stats[3]{};EmuCoreXOracleGSMirrorStats(stats);quads+=stats[0];scalar+=stats[1];
                    u64 carrying[4]{};EmuCoreXOracleGSCarryingLayoutsStats(carrying);handlers+=carrying[0];kernelVertices+=carrying[2];}
            }
        }
        const auto large=ProductionPacket(family,1,22000,0);std::vector<u8> reference,draws;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSCarryingLayouts(candidate,true);state->Prepare(prim,true,false,false,true);state->SetTraceFlags(true,false,true,false);state->SeedCarryingLayout();
            EmuCoreXOracleSetGSMirrorObserve(true);for(u32 i=0;i<3;++i)state->SubmitCarrying(large,0,3);
            auto actual=state->CarryingSnapshot();const auto trace=state->TraceNow();actual.insert(actual.end(),trace.begin(),trace.end());exact&=state->vertexFlushes>0;
            if(!candidate){reference=actual;draws=state->draws;}else{exact&=actual==reference&&state->draws==draws;u64 stats[3]{};EmuCoreXOracleGSMirrorStats(stats);quads+=stats[0];scalar+=stats[1];
                    u64 carrying[4]{};EmuCoreXOracleGSCarryingLayoutsStats(carrying);handlers+=carrying[0];kernelVertices+=carrying[2];}
        }
        // AutoFlush and a pre-first-XYZ descriptor prefix must never use Rows.
        GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Enabled;GSConfig.AutoFlushSW=true;
        EmuCoreXOracleSetGSCarryingLayouts(true,true);state->Prepare(prim,true,false,false,true);state->SetTraceFlags(true,false,true,false);state->SeedCarryingLayout();
        EmuCoreXOracleSetGSStripKernel(true,true,0);EmuCoreXOracleSetGSMirrorObserve(true);state->SubmitCarrying(ProductionPacket(family,1,129,0),0,3);
        u64 excluded[3]{},excludedKernel[4]{};EmuCoreXOracleGSMirrorStats(excluded);EmuCoreXOracleGSStripKernelStats(excludedKernel);
        exact&=excluded[0]==0&&excluded[1]==0&&excludedKernel[0]==0&&EmuCoreXOracleGSCarryingStagedVertices()>0;
        GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
        const auto packet=ProductionPacket(family,1,17,0);const auto regs=Descriptors(family,1);const auto firstXYZ=std::find_if(regs.begin(),regs.end(),[](u8 x){return x==GIF_REG_XYZ2||x==GIF_REG_XYZF2;});
        const u32 xyz=static_cast<u32>(firstXYZ-regs.begin());std::vector<GIFPackedReg> prefix(packet.begin(),packet.begin()+1+xyz);
        state->Prepare(prim,true,false,false,true);state->SeedCarryingLayout();EmuCoreXOracleSetGSStripKernel(true,true,0);
        EmuCoreXOracleSetGSMirrorObserve(true);state->SubmitCarrying(prefix,0,3);
        EmuCoreXOracleGSMirrorStats(excluded);EmuCoreXOracleGSStripKernelStats(excludedKernel);
        exact&=excluded[0]==0&&excluded[1]==0&&excludedKernel[0]==0&&state->ParserReg(3)==xyz;
        exact&=handlers>0&&kernelVertices>0;
        std::printf("GS_CARRY_CLASSIFIED family=%u prim=%u handlers=%llu kernelVertices=%llu\n",family,prim,
            static_cast<unsigned long long>(handlers),static_cast<unsigned long long>(kernelVertices));
        return exact;
    }
}
void GSCarryingQuadTests()
{
    const auto saved=GSConfig;const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSMirrorObserve(false);EmuCoreXOracleSetGSCarryingLayouts(true,false);EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);});
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;GSConfig.UserHacks_ReadTCOnClose=false;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    constexpr const char* controls[]={"carrying quad NOP triple raw Q normalized vertex/meta guarded input and production state are exact",
        "carrying quad STQ pair keeps own Q distinct from latch and exact bounded vertices/meta/state",
        "carrying quad UV pair keeps complete carried m0 and exact UV14 fog bounded vertices/meta/state",
        "carrying quad RGBA pair uses latched Q with exact bounded vertices/meta/state",
        "carrying quad UV triple carries ST and uses latched Q with exact colour fog bounded vertices/meta/state"};
    constexpr const char* workNames[]={"carrying quad NOP triple actually vectorizes mirror work and eliminates scalar Rows metadata",
        "carrying STQ sprite pair executes classified kernel with scalar mirror metadata and zero quad work",
        "carrying UV sprite pair executes classified kernel with scalar mirror metadata and zero quad work",
        "carrying quad RGBA pair actually vectorizes mirror work and eliminates scalar Rows metadata",
        "carrying quad UV triple actually vectorizes mirror work and eliminates scalar Rows metadata"};
    for(u32 family=0;family<5;++family)
    {
        GSCarryingQuadOracle::Work raw;const bool byteExact=GSCarryingQuadOracle::Raw(family,raw);u64 productionQuads=0,productionScalar=0;
        const bool stateExact=GSCarryingQuadOracle::State(family,productionQuads,productionScalar);
        Check(byteExact&&stateExact,controls[family]);
        const bool rowsConsumer=family==0||family==3||family==4;
        Check(raw.quads==raw.expectedQuads&&raw.scalar==raw.expectedSmall&&raw.tail==raw.expectedTail&&
            (rowsConsumer?productionQuads>0:productionQuads==0)&&(rowsConsumer?productionScalar==0:productionScalar>0),workNames[family]);
        std::printf("GS_CARRY_CONTROL family=%u raw=%u classified=%u\n",family,byteExact,stateExact);
        std::printf("GS_CARRY_QUAD family=%u cases=%llu vertices=%llu rawQuads=%llu/%llu scalar=%llu/%llu tail=%llu/%llu productionQuads=%llu productionScalar=%llu\n",family,
            static_cast<unsigned long long>(raw.cases),static_cast<unsigned long long>(raw.vertices),static_cast<unsigned long long>(raw.quads),static_cast<unsigned long long>(raw.expectedQuads),
            static_cast<unsigned long long>(raw.scalar),static_cast<unsigned long long>(raw.expectedSmall),static_cast<unsigned long long>(raw.tail),static_cast<unsigned long long>(raw.expectedTail),
            static_cast<unsigned long long>(productionQuads),static_cast<unsigned long long>(productionScalar));
    }
}
