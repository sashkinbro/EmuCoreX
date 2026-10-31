// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after jit_oracle_gs_packed_batch.inl; retains staged and direct controls.
extern "C" void EmuCoreXOracleSetGSStripKernel(bool enabled,bool observe,u32 mutation);
extern "C" void EmuCoreXOracleGSStripKernelStats(u64* values);
extern "C" u32 EmuCoreXOracleGSStripCull(const s32* positions,const s32* scissor,bool candidate);

namespace GSStripKernelOracle
{
    void Regular(std::vector<GIFPackedReg>& packet,u32 adc)
    {
        constexpr u16 x[]={100,400,100,400},y[]={100,100,400,400};
        for(u32 i=0;i<(packet.size()-1)/3;++i)
        {
            auto* r=packet.data()+1+i*3;
            r[2].U32[0]=(r[2].U32[0]&0xffff0000)|x[i&3];
            r[2].U32[1]=(r[2].U32[1]&0xffff0000)|y[i&3];
            r[2].U32[3]=(r[2].U32[3]&~0x8000u)|
                ((adc==1 || (adc==2&&i%5<2))?0x8000u:0u);
        }
    }
    void CullTests()
    {
        bool exact=true;u64 cases=0;
        const auto compare=[&](const std::array<s32,6>& xy,const std::array<s32,4>& scissor){
            const GSVector4i a(xy[0],xy[1],xy[0],xy[1]),b(xy[2],xy[3],xy[2],xy[3]),c(xy[4],xy[5],xy[4],xy[5]);
            const GSVector4i raw=a.runion(b).runion(c);
            const GSVector4i rounded=((raw+GSVector4i(15,15,-1,-1))&GSVector4i(~15))+GSVector4i(0,0,1,1);
            const u32 expected=GSVertexIsRejected<GS_TRIANGLE_CLASS>(rounded,GSVector4i::load<false>(scissor.data()),a,b,c);
            exact&=EmuCoreXOracleGSStripCull(xy.data(),scissor.data(),true)==expected;
            ++cases;
        };
        constexpr std::array<s32,18> edges{-65535,-32768,-33,-17,-16,-15,-1,0,1,7,8,15,16,17,31,32,32767,65535};
        for(s32 x:edges)for(s32 y:edges)for(s32 d:{-17,-1,0,1,17})
            for(s32 offset:{0,1,1023})
            {
                const std::array<s32,4> scissor{offset*16-8,-8,(offset+4)*16+8,16392};
                compare({x,y,x+d,y+16,x+16,y+d},scissor);
            }
        u32 rng=0x72ac3541;
        const auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
        for(u32 i=0;i<24000;++i)
        {
            std::array<s32,6> xy;for(auto& v:xy)v=static_cast<s32>(next()&0x1ffff)-65535;
            if(i%7==0){xy[2]=xy[0];xy[3]=xy[1];}
            const s32 left=next()%1024,top=next()%1024;
            const std::array<s32,4> scissor{left*16-8,top*16-8,
                (left+1+static_cast<s32>(next()%1024))*16+8,(top+1+static_cast<s32>(next()%1024))*16+8};
            compare(xy,scissor);
        }
        Check(exact,"strip scalar cull matches local native bbox scissor and duplicate equations at dense edges");
        std::printf("GS_STRIP_CULL cases=%llu\n",static_cast<unsigned long long>(cases));
    }
}

void GSStripKernelTests()
{
    using namespace GSStripKernelOracle;
    const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);});
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
    GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    CullTests();
    auto state=std::make_unique<GSPackedBatchOracle>();
    bool matrix=true,golden=true,allRejected=true,fallback=true;
    u64 kernelCalls=0,kernelVertices=0,directCulls=0,candidateCulls=0;
    constexpr u32 counts[]={0,1,2,3,4,5,6,7,127,128,129,255,256,257};
    for(bool fog:{false,true})for(u32 pending:{0u,1u,2u})for(u32 count:counts)for(u32 adc:{0u,1u,2u})
    {
        auto prefix=GSVertexPacket(pending,fog,false);Regular(prefix,0);
        auto packet=GSVertexPacket(count,fog,false);Regular(packet,adc);
        std::vector<u8> expected,draws;
        for(u32 mode=0;mode<3;++mode)
        {
            EmuCoreXOracleSetGSPackedBatch(mode!=0,false);
            EmuCoreXOracleSetGSStripKernel(mode==2,true,0);
            state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);
            state->Submit(prefix,0);state->Submit(packet,0);
            const auto actual=state->Snapshot();
            if(mode==0){expected=actual;draws=state->draws;}
            else matrix&=actual==expected&&state->draws==draws;
            u64 stats[4]{};EmuCoreXOracleGSStripKernelStats(stats);
            if(mode==1)directCulls+=stats[2];
            if(mode==2){kernelCalls+=stats[0];kernelVertices+=stats[1];candidateCulls+=stats[2];}
            if(pending==0&&adc==0&&mode==2)golden&=state->StripGoldenMatches(packet,fog);
        }
    }
    Check(matrix,"staged direct and strip kernel preserve complete vertices indices carry rings draw state at chunk boundaries");
    Check(golden,"accepted strip output matches independent raw vertex bytes and sequential triangle indices");
    Check(kernelCalls>0&&kernelVertices>0,"eligible native strips enter the two-pass kernel and consume vertices");
    Check(candidateCulls<directCulls,"two-pass strip kernels eliminate repeated per-vertex vector rejection work");

    for(u32 variant=0;variant<6;++variant)
    {
        auto packet=GSVertexPacket(257,true,true);Regular(packet,variant==0?1:2);
        const auto changes=GSPackedBatchChanges(GS_TRIANGLESTRIP);
        std::vector<u8> expected,draws;
        for(u32 mode=0;mode<3;++mode)
        {
            EmuCoreXOracleSetGSPackedBatch(mode!=0,false);EmuCoreXOracleSetGSStripKernel(mode==2,true,0);
            state->Prepare(GS_TRIANGLESTRIP,variant!=1,variant==4,variant==2,true,variant==3);
            if(variant==5)state->SetRawXYOffsetForStrip(0x0001800000018000ull);
            if(variant==0||variant==4)state->Submit(changes,0);
            state->Submit(packet,variant==4?5:0);
            const auto actual=state->Snapshot();
            if(!mode){expected=actual;draws=state->draws;}
            else if(variant==0)allRejected&=actual==expected&&state->draws==draws;
            else fallback&=actual==expected&&state->draws==draws;
            if(mode==2&&(variant==1||variant==2||variant==3||variant==5))
            {
                u64 stats[4]{};EmuCoreXOracleGSStripKernelStats(stats);
                fallback&=stats[0]==0;
            }
        }
    }
    Check(allRejected,"all-ADC strip preserves pending environment and dirty state without accepted primitives");
    Check(fallback,"upscale AA1 invalid scissor offset context and partial packet seams retain exact fallback state");

    auto large=GSVertexPacket(22000,true,false);Regular(large,0);
    std::vector<u8> expected,draws;bool flushExact=true,flushed=true;
    for(u32 mode=0;mode<3;++mode)
    {
        EmuCoreXOracleSetGSPackedBatch(mode!=0,false);EmuCoreXOracleSetGSStripKernel(mode==2,true,0);
        state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);
        for(u32 i=0;i<3;++i)state->Submit(large,0);
        flushed&=state->vertexFlushes>0;
        if(!mode){expected=state->Snapshot();draws=state->draws;}
        else flushExact&=state->Snapshot()==expected&&state->draws==draws;
    }
    Check(flushExact&&flushed,"strip kernel preserves exact real growth vertex-count Flush current vertex and retained window");
    // Fault-injection controls only run once the candidate exists. They must
    // diverge from the staged snapshot, not be mistaken for passing behavior.
    if(kernelCalls)
    {
        bool detectRing=false,detectPublication=false;
        auto packet=GSVertexPacket(257,true,false);Regular(packet,0);
        for(u32 mutation:{0u,1u})
        {
            EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,true,mutation);
            state->Prepare(GS_TRIANGLESTRIP,true,false,false,false);state->Submit(packet,0);
            if(!mutation)expected=state->Snapshot();else detectRing=state->Snapshot()!=expected;
        }
        for(u32 mutation:{0u,2u})
        {
            EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,true,mutation);
            state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);
            for(u32 i=0;i<3;++i)state->Submit(large,0);
            if(!mutation)draws=state->draws;else detectPublication=state->draws!=draws;
        }
        Check(detectRing,"strip oracle detects omitted ring writeback");
        Check(detectPublication,"strip oracle detects missing current-vertex publication at a real Flush seam");
    }
    std::printf("GS_STRIP_KERNEL chunks=%llu vertices=%llu directVectorCull=%llu candidateVectorCull=%llu\n",
        static_cast<unsigned long long>(kernelCalls),static_cast<unsigned long long>(kernelVertices),
        static_cast<unsigned long long>(directCulls),static_cast<unsigned long long>(candidateCulls));
}
