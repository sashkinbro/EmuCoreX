// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Uses the actual GIF parser, accepted-index emission and GSVertexTrace::Update.
extern "C" void EmuCoreXOracleSetGSFusedTrace(bool enabled, bool observe, u32 mutation);
extern "C" void EmuCoreXOracleGSFusedTraceStats(u64* values);

namespace GSFusedTraceOracle
{
    void Values(std::vector<GIFPackedReg>& packet, u32 variant)
    {
        GSStripKernelOracle::Regular(packet, variant == 1 ? 2 : 0);
        constexpr u32 edge[] = {0,0x80000000,0x3f800000,0xbf800000,0x40000000,
            0xc0000000,0x7f800000,0xff800000,0x7fc00001,0xffc12345,
            1,0x80000001,0x00800000,0x7f7fffff,0x00800001,0x3f000000};
        for (u32 i=0;i<(packet.size()-1)/3;++i)
        {
            auto* r=packet.data()+1+i*3;
            if (variant >= 2)
            {
                r[0].U32[0]=edge[(i+variant)%std::size(edge)];
                r[0].U32[1]=edge[(i*3+variant)%std::size(edge)];
                r[0].U32[2]=edge[(i*5+variant)%std::size(edge)];
            }
            else r[0].U32[2]=(i%3==0)?0x3f800000:(i%3==1?0xc0000000:0x3f000000);
            // A stable last Q avoids the existing bulk/raw-vs-scalar carry policy
            // differing between parser modes; intermediate special Q stays tested.
            if (i+1==(packet.size()-1)/3)r[0].U32[2]=0x3f800000;
            r[2].XYZF2.Z=variant==5?0x12345679u:0x12345678u;
        }
    }
}

void GSFusedTraceTests()
{
    using namespace GSFusedTraceOracle;
    const auto saved=GSConfig;
    const ScopedGuard restore([&](){
        GSConfig=saved;EmuCoreXOracleSetGSFusedTrace(true,false,0);
        EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);
    });
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
    GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    auto state=std::make_unique<GSPackedBatchOracle>();
    bool exact=true,allQualified=true,skipsWalk=true,dedup=true;
    u64 cases=0,legacyIndices=0,fusedCalls=0,accumulated=0;
    for (u32 prim:{GS_TRIANGLELIST,GS_TRIANGLESTRIP})
    for (bool tme:{false,true})for (bool fst:{false,true})for (bool iip:{false,true})
    for (bool decal:{false,true})for (bool fog:{false,true})for (u32 parser=0;parser<3;++parser)
    for (u32 variant=0;variant<6;++variant)
    {
        auto packet=GSVertexPacket(257,fog,false);Values(packet,variant);
        std::vector<u8> expected,snapshot;
        for (bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSFusedTrace(candidate,true,0);
            EmuCoreXOracleSetGSPackedBatch(parser!=0,false);
            EmuCoreXOracleSetGSStripKernel(parser==2,false,0);
            state->Prepare(prim,true,false,false,true);
            state->SetTraceFlags(tme,fst,iip,decal);
            state->Submit(packet,parser==0?1:0);
            const auto actual=state->TraceNow();
            u64 stats[4]{};EmuCoreXOracleGSFusedTraceStats(stats);
            if (!candidate) {expected=actual;snapshot=state->Snapshot();legacyIndices+=stats[1];}
            else
            {
                exact&=actual==expected&&snapshot==state->Snapshot();
                allQualified&=stats[2]==1;skipsWalk&=stats[0]==0&&stats[1]==0;
                fusedCalls+=stats[2];accumulated+=stats[3];
                if(prim==GS_TRIANGLESTRIP&&variant==0)dedup&=stats[3]==257;
            }
        }
        ++cases;
    }
    Check(exact,"fused trace is bitwise legacy-exact for triangle flags flat colors varying Q NaNs signed zero and all parser paths");
    Check(allQualified,"all fully tracked triangle list and strip draws consume emission-time FMM bounds");
    Check(skipsWalk,"qualified actual GSVertexTrace dispatch skips the legacy index walk");
    Check(dedup&&accumulated>0,"strip FMM watermark folds each newly referenced position and texture once");

    bool seam=true,rewind=true,fallback=true;
    for(u32 variant=0;variant<5;++variant)
    {
        auto packet=GSVertexPacket(129,true,false);Values(packet,variant);
        auto suffix=GSVertexPacket(128,false,false);Values(suffix,variant+1);
        std::vector<u8> expected,snapshot,draws;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSFusedTrace(candidate,true,0);
            EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);
            state->Prepare(variant==4?GS_TRIANGLEFAN:GS_TRIANGLESTRIP,true,false,false,true);
            state->SetTraceFlags(true,variant==3,variant==2,false);
            state->Submit(packet,0);
            if(variant==1)state->RewindForTrace(GS_TRIANGLESTRIP);
            if(variant==2)
            {
                std::vector<GIFPackedReg> reglist(2);
                reglist[0].U64[0]=1|(1ull<<15)|(1ull<<58)|(2ull<<60);
                reglist[0].U64[1]=GIF_REG_RGBA|(u64(GIF_REG_XYZ2)<<4);
                reglist[1].U64[0]=0x3f800000abcdef01ull;
                reglist[1].U64[1]=200ull|(300ull<<16)|(0x12345678ull<<32);
                state->Submit(reglist,1);
            }
            state->Submit(suffix,5);
            const auto actual=state->TraceNow();
            if(!candidate){expected=actual;snapshot=state->Snapshot();draws=state->draws;}
            else
            {
                const bool same=actual==expected&&state->Snapshot()==snapshot&&state->draws==draws;
                if(variant==1)rewind&=same;else seam&=same;
                if(variant==4){u64 stats[4]{};EmuCoreXOracleGSFusedTraceStats(stats);fallback&=stats[0]==1&&stats[2]==0;}
            }
        }
    }
    Check(seam&&rewind,"FMM coverage preserves compaction rewind reglist mixed layouts and partial packet seams");
    Check(fallback,"triangle fans retain the independent legacy FMM walk");

    auto large=GSVertexPacket(22000,true,false);Values(large,0);
    std::vector<u8> expected,draws;
    bool flush=true;
    state->captureTrace=true;
    for(bool candidate:{false,true})
    {
        EmuCoreXOracleSetGSFusedTrace(candidate,true,0);
        state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);state->SetTraceFlags(true,false,false,false);
        for(u32 i=0;i<3;++i)state->Submit(large,0);
        const auto actual=state->TraceNow();
        if(!candidate){expected=actual;draws=state->draws;}
        else flush&=actual==expected&&state->draws==draws&&state->vertexFlushes>0;
    }
    state->captureTrace=false;
    Check(flush,"FMM trace and downstream draw state remain bitwise exact across actual grow flush and retained strip windows");

    bool poisonExact=true,poisonFallback=true;
    for(u32 poison=0;poison<7;++poison)
    {
        auto packet=GSVertexPacket(257,true,false);Values(packet,2);
        std::vector<u8> reference;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSFusedTrace(candidate,true,0);
            state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);
            state->SetTraceFlags(true,false,false,false);
            state->Submit(packet,0);
            if(poison<5)state->PoisonTraceCoverage(poison);
            const auto actual=poison<5?state->TraceNow():state->TraceForeign(poison==5);
            if(!candidate)reference=actual;
            else
            {
                u64 stats[4]{};EmuCoreXOracleGSFusedTraceStats(stats);
                poisonExact&=actual==reference;poisonFallback&=stats[0]==1&&stats[2]==0;
            }
        }
    }
    Check(poisonExact&&poisonFallback,"partial count stale range watermark key invalid state and foreign buffers force exact legacy fallback");

    bool known=true;
    for(bool candidate:{false,true})
    {
        auto packet=GSVertexPacket(3,false,false);GSStripKernelOracle::Regular(packet,0);
        for(u32 i=0;i<3;++i)
        {
            auto* r=packet.data()+1+i*3;
            r[0].U32[0]=0x3f800000;r[0].U32[1]=0x40000000;r[0].U32[2]=0x40000000;
            r[2].XYZ2.Z=0x12345678;
            for(u32 lane=0;lane<4;++lane)r[1].U32[lane]=i==2?(lane+1)*17:0xf0+i;
        }
        EmuCoreXOracleSetGSFusedTrace(candidate,true,0);
        state->Prepare(GS_TRIANGLELIST,true,false,false,true);
        state->SetTraceFlags(true,false,false,false);state->SetKnownTraceCarry();
        state->Submit(packet,0);state->TraceNow();known&=state->KnownTraceMatches();
    }
    Check(known,"finite FMM matches hand-derived native position unsigned depth STQ scale and flat provoking RGBA bounds");

    bool postwork=true,postworkFused=true;
    for(bool native:{false,true})for(bool aa1:{false,true})
    for(bool tme:{false,true})for(bool fst:{false,true})
    {
        auto packet=GSVertexPacket(129,true,false);Values(packet,3);
        std::vector<u8> reference;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSFusedTrace(candidate,true,0);
            state->Prepare(GS_TRIANGLESTRIP,native,false,aa1,true);
            state->SetTraceFlags(tme,fst,true,false);
            state->Submit(packet,0);const auto actual=state->TraceNow();
            if(!candidate)reference=actual;
            else
            {
                u64 stats[4]{};EmuCoreXOracleGSFusedTraceStats(stats);
                postwork&=actual==reference;postworkFused&=stats[2]==1&&stats[0]==0;
            }
        }
    }
    Check(postwork&&postworkFused,"native upscale and AA1 coverage alpha retain exact trace equality filter LOD and depth postwork while fused");
    std::printf("GS_FUSED_TRACE cases=%llu legacyIndices=%llu fusedCalls=%llu uniqueVertices=%llu\n",
        static_cast<unsigned long long>(cases),static_cast<unsigned long long>(legacyIndices),
        static_cast<unsigned long long>(fusedCalls),static_cast<unsigned long long>(accumulated));
}
