// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after the packed/strip fixtures. Exercises the real Parse consumer.
#include <sys/mman.h>
#include <unistd.h>
extern "C" void EmuCoreXOracleSetGSMirrorObserve(bool enabled);
extern "C" void EmuCoreXOracleGSMirrorStats(u64* values);
extern "C" void EmuCoreXOracleGSStripParse(const GIFPackedReg* packet,u32 count,GSVertex* vertices,
    u64* xy,u64* meta,const s32* bounds,s32 ofx,s32 ofy,u64 uvfog,bool fog,u32 depth);
namespace GSMirrorQuadOracle
{
    struct GuardedInput
    {
        void* mapping=MAP_FAILED;size_t size=0;GIFPackedReg* packet=nullptr;
        explicit GuardedInput(const std::vector<GIFPackedReg>& source)
        {
            const size_t page=static_cast<size_t>(sysconf(_SC_PAGESIZE));
            const size_t bytes=source.size()*sizeof(GIFPackedReg);
            const size_t payload=std::max(page,(bytes+page-1)/page*page);
            size=payload+2*page;mapping=mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(mapping==MAP_FAILED)return;
            auto* start=static_cast<u8*>(mapping)+page;
            if(mprotect(start,payload,PROT_READ|PROT_WRITE))return;
            packet=reinterpret_cast<GIFPackedReg*>(start+payload-bytes);
            if(bytes)std::memcpy(packet,source.data(),bytes);
            if(mprotect(start,payload,PROT_READ))packet=nullptr;
        }
        ~GuardedInput(){if(mapping!=MAP_FAILED)munmap(mapping,size);}
    };
    s32 Floor16(s32 v){return v>=0?v/16:-((15-v)/16);}
    void RawTests(u64* savedStats,u64& expectedSmall,u64& expectedQuads)
    {
        bool vertexExact=true,mirrorExact=true,canaries=true,guards=true;
        u64 cases=0,vertices=0;
        constexpr u32 counts[]={0,1,2,3,4,5,6,7,8,9,15,16,17,31,32,63,64,127,128};
        constexpr u16 edges[]={0,1,15,16,17,32767,32768,65534,65535};
        constexpr u32 qbits[]={0,0x80000000u,0x7fc00123u,0x7f800000u,0xff800000u,0x3f800000u};
        constexpr s32 offsets[][2]={{0,0},{1,1},{32768,32768},{65535,0},{0,65535},{65535,65535}};
        constexpr s32 bounds[][4]={{0,0,0,0},{-4096,-4096,4096,4096},{-1,-1,1,1},{0,0,4096,4096},{4095,4095,4096,4096},{-4096,0,0,4096}};
        constexpr u64 uvfog=0x89abcdef24681357ull,guard=0xbadc0ffee0ddf00dull;
        u32 rng=0x67d3a219;
        const auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
        EmuCoreXOracleSetGSMirrorObserve(true);
        for(bool fog:{false,true})for(u32 depth=0;depth<3;++depth)for(u32 count:counts)
            for(const auto& offset:offsets)for(const auto& bound:bounds)
        {
            std::vector<GIFPackedReg> packet(count*3);
            for(u32 i=0;i<count;++i)
            {
                auto* r=packet.data()+i*3;
                for(u32 j=0;j<3;++j)for(auto& w:r[j].U32)w=next();
                r[0].U32[2]=qbits[(i+cases)%std::size(qbits)];
                r[2].U32[0]=(r[2].U32[0]&0xffff0000u)|edges[(i+cases)%std::size(edges)];
                r[2].U32[1]=(r[2].U32[1]&0xffff0000u)|edges[(i*3+cases)%std::size(edges)];
                r[2].U32[3]=(r[2].U32[3]&~0x8000u)|((i+cases)%3?0x8000u:0u);
            }
            GuardedInput input(packet);guards&=input.packet!=nullptr;
            if(!input.packet)continue;
            std::vector<GSVertex> out(count+2);std::memset(out.data(),0xa5,out.size()*sizeof(GSVertex));
            const GSVertex before=out.front(),after=out.back();
            std::vector<u64> xy(count+2,guard),meta(count+2,guard);
            EmuCoreXOracleGSStripParse(input.packet,count,out.data()+1,xy.data()+1,meta.data()+1,
                bound,offset[0],offset[1],uvfog,fog,depth);
            canaries&=std::memcmp(&out.front(),&before,sizeof(GSVertex))==0&&std::memcmp(&out.back(),&after,sizeof(GSVertex))==0&&
                xy.front()==guard&&xy.back()==guard&&meta.front()==guard&&meta.back()==guard;
            for(u32 i=0;i<count;++i)
            {
                const auto* r=packet.data()+i*3;
                std::array<u32,8> v{r[0].U32[0],r[0].U32[1],0,
                    r[0].U32[2]?r[0].U32[2]:0x00800000u,
                    (r[2].U32[0]&65535)|((r[2].U32[1]&65535)<<16),
                    fog?(r[2].U32[2]>>4)&0xffffffu:r[2].U32[2],static_cast<u32>(uvfog),
                    fog?(r[2].U32[3]>>4)&255u:static_cast<u32>(uvfog>>32)};
                for(u32 lane=0;lane<4;++lane)v[2]|=(r[1].U32[lane]&255u)<<(lane*8);
                if(depth==static_cast<u32>(GSLimit24BitDepth::PrioritizeUpper))v[5]=((v[5]>>8)&~255u)|(v[5]&255u);
                if(depth==static_cast<u32>(GSLimit24BitDepth::PrioritizeLower))v[5]&=0xffffffu;
                vertexExact&=std::memcmp(&out[i+1],v.data(),sizeof(GSVertex))==0;
                const s32 x=static_cast<s32>(r[2].U32[0]&65535)-offset[0],y=static_cast<s32>(r[2].U32[1]&65535)-offset[1];
                const s32 bx=Floor16(x-1),by=Floor16(y-1);
                const u32 oc=(bx<bound[0]?1u:0u)|(bx>=bound[2]?2u:0u)|(by<bound[1]?4u:0u)|(by>=bound[3]?8u:0u);
                const u64 expectedXY=static_cast<u32>(x)|(static_cast<u64>(static_cast<u32>(y))<<32);
                const u64 expectedMeta=(static_cast<u64>(static_cast<u32>(bx))&0xfffffffull)|
                    ((static_cast<u64>(static_cast<u32>(by))<<28)&0x00fffffff0000000ull)|
                    (static_cast<u64>(oc)<<56)|(r[2].U32[3]&0x8000u?1ull<<60:0ull);
                mirrorExact&=xy[i+1]==expectedXY&&meta[i+1]==expectedMeta;
            }
            expectedSmall+=count<4?count:0;
            expectedQuads+=count>=4?(count/4+(count%4!=0)):0;
            ++cases;vertices+=count;
        }
        EmuCoreXOracleGSMirrorStats(savedStats);
        Check(vertexExact,"quad actual Parse preserves raw-Q RGBA UV FOG XYZF/XYZ and all depth policies bitwise");
        Check(mirrorExact,"quad actual Parse matches independent signed full16-bit XY band outcode ADC and reserved-bit goldens");
        Check(guards&&canaries,"quad count0..3 and remainder inputs end at a real inaccessible page with exact bounded stores");
        std::printf("GS_MIRROR_RAW cases=%llu vertices=%llu expectedQuads=%llu expectedSmall=%llu\n",
            static_cast<unsigned long long>(cases),static_cast<unsigned long long>(vertices),
            static_cast<unsigned long long>(expectedQuads),static_cast<unsigned long long>(expectedSmall));
    }
}
void GSMirrorQuadTests()
{
    using namespace GSMirrorQuadOracle;
    const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSMirrorObserve(false);EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);});
    u64 rawStats[3]{},expectedSmall=0,expectedQuads=0;
    RawTests(rawStats,expectedSmall,expectedQuads);
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
    GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    auto state=std::make_unique<GSPackedBatchOracle>();state->captureTrace=true;
    bool parity=true,flush=true;u64 productionQuads=0,productionScalar=0,kernelVertices=0;
    constexpr u32 counts[]={4,5,6,7,8,9,31,32,127,128,129,255,257};
    for(bool fog:{false,true})for(bool offset:{false,true})for(u32 pending:{0u,1u,2u})for(u32 count:counts)
    {
        auto prefix=GSVertexPacket(pending,fog,false);GSStripKernelOracle::Regular(prefix,0);
        auto packet=GSVertexPacket(count,fog,true);GSStripKernelOracle::Regular(packet,2);
        if(offset)for(u32 i=0;i<count;++i){packet[1+i*3+2].U32[0]+=0x8000;packet[1+i*3+2].U32[1]+=0x8000;}
        std::vector<u8> expected,draws,trace;
        for(u32 mode=0;mode<2;++mode)
        {
            EmuCoreXOracleSetGSPackedBatch(mode!=0,false);EmuCoreXOracleSetGSStripKernel(mode!=0,true,0);EmuCoreXOracleSetGSMirrorObserve(true);
            state->Prepare(GS_TRIANGLESTRIP,true,offset,false,true);state->SetTraceFlags(false,false,true,false);state->Submit(prefix,0);state->Submit(packet,0);
            if(!mode){expected=state->Snapshot();draws=state->draws;trace=state->TraceSnapshot();}
            else
            {
                parity&=state->Snapshot()==expected&&state->draws==draws&&state->TraceSnapshot()==trace;
                u64 stats[3]{},strip[4]{};EmuCoreXOracleGSMirrorStats(stats);EmuCoreXOracleGSStripKernelStats(strip);
                productionQuads+=stats[0];productionScalar+=stats[1];kernelVertices+=strip[1];
            }
        }
    }
    Check(parity,"quad production packed strips preserve full vertices indices rings current state FMM and real grow seams");
    auto large=GSVertexPacket(22000,true,false);GSStripKernelOracle::Regular(large,0);
    std::vector<u8> expected,draws,trace;
    for(u32 mode=0;mode<2;++mode)
    {
        EmuCoreXOracleSetGSPackedBatch(mode!=0,false);EmuCoreXOracleSetGSStripKernel(mode!=0,true,0);EmuCoreXOracleSetGSMirrorObserve(true);
        state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);state->SetTraceFlags(false,false,true,false);for(u32 i=0;i<3;++i)state->Submit(large,0);
        flush&=state->vertexFlushes>0;
        if(!mode){expected=state->Snapshot();draws=state->draws;trace=state->TraceSnapshot();}
        else flush&=state->Snapshot()==expected&&state->draws==draws&&state->TraceSnapshot()==trace;
    }
    Check(flush,"quad real vertex-count Flush preserves pending strip carry current vertex and full draw/FMM snapshots");
    Check(rawStats[0]==expectedQuads&&productionQuads>0&&kernelVertices>0,"quad mirror construction executes on actual Parse and real production packed strips");
    Check(rawStats[1]==expectedSmall&&productionScalar==0,"quad mirror construction eliminates scalar per-vertex metadata except bounded count0..3 calls");
    std::printf("GS_MIRROR_PRODUCTION quads=%llu scalar=%llu kernelVertices=%llu\n",static_cast<unsigned long long>(productionQuads),
        static_cast<unsigned long long>(productionScalar),static_cast<unsigned long long>(kernelVertices));
}
