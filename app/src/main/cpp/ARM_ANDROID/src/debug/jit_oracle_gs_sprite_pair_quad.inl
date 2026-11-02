// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unreferenced next RED. Include after carrying_quad and sprite_kernel.
#pragma once
namespace GSSpritePairQuadOracle
{
    constexpr u32 QBits[]={0,0x80000000u,0x7fc12345u,0x7f800000u,0xff800000u,0xbf800000u,1u,0x3f800000u};
    bool CarriedRaw(u32 family,u64& cases)
    {
        bool exact=true;
        constexpr u16 edges[]={0,1,15,16,17,32767,32768,65534,65535};
        constexpr u64 uvfog=0x89abcdef81238023ull,guard=0xbadc0ffee0ddf00dull;
        constexpr s32 offsets[][2]={{0,0},{32768,32768},{65535,0},{0,65535}};
        const s32 bounds[4]={-1,-2,4096,4095};
        EmuCoreXOracleSetGSCarryingLayouts(true,false);
        for(u32 padding=0;padding<5;++padding)
        {
            const auto descriptors=GSCarryingLayoutOracle::Descriptors(family,padding);const auto tag=GSCarryingLayoutOracle::Tag(descriptors,7);
            u32 info[8]{};const bool recognized=EmuCoreXOracleGSClassifyCarryingLayout(&tag,info);
            exact&=recognized&&info[0]==family+4;if(!recognized)continue;
            const GIFPackedLayout off{info[4],info[5],info[6],info[7]};
            for(u32 count:{0u,1u,2u,3u,4u,5u,7u,31u,128u})for(u32 depth=0;depth<3;++depth)
            for(u32 q:QBits)for(const auto& offset:offsets)
            {
                GSVector4i current;current.U32[0]=0x3fa00000;current.U32[1]=0xc0200000;
                current.U32[2]=0x44332211;current.U32[3]=q;const auto beforeCurrent=current;
                const u32 latch=QBits[(cases+3)%std::size(QBits)];
                std::vector<GIFPackedReg> packet(count*off.stride);
                for(u32 i=0;i<count;++i)
                {
                    auto* r=packet.data()+i*off.stride;
                    for(u32 at=0;at<off.stride;++at)for(u32 lane=0;lane<4;++lane)r[at].U32[lane]=0xf1e2d3c4u^(i*113+at*19+lane*71);
                    r[off.off_a].U32[0]=family==1?0x3f000000u+i*71:0xfffff123u+i*19;
                    r[off.off_a].U32[1]=family==1?0x3f400000u+i*97:0xabcde456u+i*31;
                    r[off.off_a].U32[2]=QBits[(i+5)%std::size(QBits)];
                    auto& xyz=r[off.off_xyz];xyz.U32[0]=(xyz.U32[0]&0xffff0000u)|edges[(i+cases)%std::size(edges)];
                    xyz.U32[1]=(xyz.U32[1]&0xffff0000u)|edges[(i*3+cases)%std::size(edges)];
                    xyz.U32[3]=(xyz.U32[3]&~0x8000u)|((i+cases)%3?0x8000u:0u);
                }
                GSMirrorQuadOracle::GuardedInput input(packet);exact&=input.packet!=nullptr;if(!input.packet)continue;
                std::vector<GSVertex> output(count+2);std::memset(output.data(),0xa5,output.size()*sizeof(GSVertex));
                const auto before=output.front(),after=output.back();std::vector<u64> xy(count+2,guard),meta(count+2,guard);
                EmuCoreXOracleGSCarryingParse(family,input.packet,count,output.data()+1,xy.data()+1,meta.data()+1,&off,uvfog,&current,latch,bounds,offset[0],offset[1],depth);
                exact&=std::memcmp(&current,&beforeCurrent,sizeof(current))==0&&std::memcmp(&output.front(),&before,sizeof(before))==0&&
                    std::memcmp(&output.back(),&after,sizeof(after))==0&&xy.front()==guard&&xy.back()==guard&&meta.front()==guard&&meta.back()==guard;
                for(u32 i=0;i<count;++i)
                {
                    const auto* r=packet.data()+i*off.stride;const auto& xyz=r[off.off_xyz];
                    u32 z=xyz.U32[2];if(depth==1)z=((z>>8)&~255u)|(z&255u);if(depth==2)z&=0xffffffu;
                    const u32 uv=family==2?(r[off.off_a].U32[0]&0x3fff)|((r[off.off_a].U32[1]&0x3fff)<<16):static_cast<u32>(uvfog);
                    const std::array<u32,8> expected{family==1?r[off.off_a].U32[0]:current.U32[0],family==1?r[off.off_a].U32[1]:current.U32[1],
                        current.U32[2],q,(xyz.U32[0]&65535u)|((xyz.U32[1]&65535u)<<16),z,uv,static_cast<u32>(uvfog>>32)};
                    exact&=std::memcmp(&output[i+1],expected.data(),sizeof(GSVertex))==0;
                    const s32 x=static_cast<s32>(xyz.U32[0]&65535)-offset[0],y=static_cast<s32>(xyz.U32[1]&65535)-offset[1];
                    const s32 bx=GSMirrorQuadOracle::Floor16(x-1),by=GSMirrorQuadOracle::Floor16(y-1);
                    const u32 oc=(bx<bounds[0]?1u:0u)|(bx>=bounds[2]?2u:0u)|(by<bounds[1]?4u:0u)|(by>=bounds[3]?8u:0u);
                    const u64 expectedXY=static_cast<u32>(x)|(static_cast<u64>(static_cast<u32>(y))<<32);
                    const u64 expectedMeta=(static_cast<u64>(static_cast<u32>(bx))&0xfffffffull)|((static_cast<u64>(static_cast<u32>(by))<<28)&0x00fffffff0000000ull)|
                        (static_cast<u64>(oc)<<56)|(xyz.U32[3]&0x8000u?1ull<<60:0ull);
                    exact&=xy[i+1]==expectedXY&&meta[i+1]==expectedMeta;
                }
                ++cases;
            }
        }
        return exact&&cases==4320;
    }
    bool CarriedState(u32 family)
    {
        bool exact=true;
        for(u32 q:QBits)for(bool fst:{false,true})for(u32 pending:{0u,1u})for(u32 shape:{0u,2u})for(u32 path:{2u,3u})
        {
            const auto packet=GSSpriteKernelOracle::Packet(family+1,129,shape);
            const auto prefix=GSNativeMirrorOracle::Singles(pending);
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                auto s=std::make_unique<GSPackedBatchOracle>();GSNativeMirrorOracle::Prepare(*s,GS_SPRITE);s->SetTraceFlags(true,fst,true,false);
                GSSpriteKernelOracle::Access::Current(*s).m[0]=GSVector4i(0x3fa00000,0xc0200000,0x44332211,q);
                GSSpriteKernelOracle::Configure(mode);s->Submit(prefix,0);GSSpriteKernelOracle::Submit(*s,packet,path);
                const u32 currentQ=GSVector4i(GSSpriteKernelOracle::Access::Current(*s).m[0]).U32[3];
                u32 latchedQ;std::memcpy(&latchedQ,&GSSpriteKernelOracle::Access::Latch(*s),4);
                //129 vertices end on descriptor positive zero: STQ normalizes
                //its own latch to FLT_MIN; UV retains the tag's1. Both carry
                //the original vertexQ word independently of that latch.
                exact&=currentQ==q&&latchedQ==(family==1?0x00800000u:0x3f800000u);
                auto actual=GSNativeMirrorOracle::Capture(*s);
                // Probe through a real new tag; temporaryQ resets1 independently
                // of both current vertexQ and any STQ descriptor in this packet.
                s->SubmitCarrying(GSCarryingLayoutOracle::LatchProbe(),0,path);
                exact&=GSVector4i(GSSpriteKernelOracle::Access::Current(*s).m[0]).U32[3]==0x3f800000u;
                const auto probe=GSNativeMirrorOracle::Capture(*s);actual.insert(actual.end(),probe.begin(),probe.end());
                if(!mode)expected=actual;else exact&=actual==expected;
            }
        }
        return exact;
    }
}
void GSSpritePairQuadTests()
{
    const auto saved=GSConfig;const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSMirrorObserve(false);
        EmuCoreXOracleSetGSCarryingLayouts(true,false);EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);
        EmuCoreXOracleSetGSStripOutline(true,false);EmuCoreXOracleSetGSNativeMirror(true,false,0);EmuCoreXOracleSetGSLocalFmm(true,false);});
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;GSConfig.UserHacks_ReadTCOnClose=false;
    GSConfig.UserHacks_ForceEvenSpritePosition=false;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    for(u32 family:{1u,2u})
    {
        GSCarryingQuadOracle::Work raw;const bool oldRaw=GSCarryingQuadOracle::Raw(family,raw);
        Check(oldRaw,family==1?"STQ sprite pair independent raw vertices/meta reservedXYZ ADC depth and protected padding/repeated pairs remain exact":
            "UV sprite pair independent raw vertices/meta reservedXYZ ADC depth and protected padding/repeated pairs remain exact");
        u64 currentCases=0;const bool carry=GSSpritePairQuadOracle::CarriedRaw(family,currentCases);
        Check(carry,family==1?"STQ pair carries exact currentQ raw zero NaN infinity subnormal separately from descriptorQ and latch":
            "UV pair carries complete raw currentm0 Q and UV14/Fog through bounded overlap tails without normalization");
        u64 quads=0,scalar=0;const bool state=GSCarryingQuadOracle::State(family,quads,scalar)&&GSSpritePairQuadOracle::CarriedState(family);
        Check(state,family==1?"STQ pair full production state latch probe odd pending both paths actualFlush and staged exclusions match scalar":
            "UV pair full production state latch probe odd pending both paths actualFlush and staged exclusions match scalar");
        // Independent count table behind Raw:5 descriptor forms,3 depth modes,
        //6 distinct latches and4 offsets for each count. Every count>=4 uses
        //ceil(count/4) real quads and no scalar mirror entries.
        constexpr u32 counts[]={0,1,2,3,4,5,6,7,9,31,32,127,128};
        u64 expectedQuads=0,expectedSmall=0,expectedTail=0;
        for(u32 count:counts){expectedQuads+=count>=4?(count+3)/4:0;expectedSmall+=count<4?count:0;expectedTail+=count>=4&&count%4!=0;}
        constexpr u32 repeats=5*3*6*4;expectedQuads*=repeats;expectedSmall*=repeats;expectedTail*=repeats;
        Check(raw.cases==4680&&raw.quads==expectedQuads&&raw.scalar==expectedSmall&&raw.tail==expectedTail&&quads>0&&scalar==0,
            family==1?"STQ sprite pair actually uses fourwide production mirror groups with bounded overlapping tails and no scalar metadata":
            "UV sprite pair actually uses fourwide production mirror groups with bounded overlapping tails and no scalar metadata");
        std::printf("GS_SPRITE_PAIR_QUAD family=%u rawCases=%llu currentQCases=%llu rawQuads=%llu/%llu scalar=%llu/%llu tail=%llu/%llu productionQuads=%llu productionScalar=%llu\n",family,
            static_cast<unsigned long long>(raw.cases),static_cast<unsigned long long>(currentCases),static_cast<unsigned long long>(raw.quads),static_cast<unsigned long long>(expectedQuads),
            static_cast<unsigned long long>(raw.scalar),static_cast<unsigned long long>(expectedSmall),static_cast<unsigned long long>(raw.tail),static_cast<unsigned long long>(expectedTail),
            static_cast<unsigned long long>(quads),static_cast<unsigned long long>(scalar));
    }
}
