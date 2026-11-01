// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after packed/strip/carrying/native-mirror helpers.
// Counters must be incremented inside the actual pure outlined chunk, never at its caller.
extern "C" void EmuCoreXOracleSetGSStripOutline(bool enabled,bool observe);
extern "C" void EmuCoreXOracleGSStripOutlineStats(u64* values); // outlined chunks, inline chunks, outlined vertices
namespace GSStripOutlineOracle
{
    struct Work {u64 chunks=0,vertices=0,outlined=0,inlineChunks=0,outlinedVertices=0;};
    void Collect(Work& work)
    {
        u64 kernel[4]{},outline[3]{};EmuCoreXOracleGSStripKernelStats(kernel);EmuCoreXOracleGSStripOutlineStats(outline);
        work.chunks+=kernel[0];work.vertices+=kernel[1];work.outlined+=outline[0];work.inlineChunks+=outline[1];work.outlinedVertices+=outline[2];
    }
    void Configure(u32 mode)
    {
        EmuCoreXOracleSetGSPackedBatch(mode!=0,false);EmuCoreXOracleSetGSCarryingLayouts(mode!=0,false);
        EmuCoreXOracleSetGSStripKernel(mode!=0,true,0);EmuCoreXOracleSetGSNativeMirror(true,false,0);
        EmuCoreXOracleSetGSStripOutline(mode==2,true);
    }
    bool TripleMatrix(std::array<Work,5>& work,bool& golden)
    {
        auto state=std::make_unique<GSPackedBatchOracle>();bool exact=true;
        constexpr u32 counts[]={0,1,2,3,4,5,6,7,8,127,128,129,257};
        for(bool fog:{false,true})for(u32 pending:{0u,1u,2u})for(u32 count:counts)
        for(u32 adc:{0u,1u,2u})for(u32 depth=0;depth<3;++depth)
        {
            GSConfig.UserHacks_Limit24BitDepth=static_cast<GSLimit24BitDepth>(depth);
            auto prefix=GSVertexPacket(pending,fog,false);GSStripKernelOracle::Regular(prefix,0);
            auto packet=GSVertexPacket(count,fog,false);GSStripKernelOracle::Regular(packet,adc);
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                // This raw golden uses the original packed fixture's UV literal,
                // not SeedCarryingLayout's deliberately different current UV.
                state->Reset(true);state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);
                state->SetTraceFlags(true,false,true,false);Configure(mode);
                state->Submit(prefix,0);state->Submit(packet,0);const auto actual=GSNativeMirrorOracle::Capture(*state);
                if(!mode)expected=actual;else exact&=actual==expected;
                if(mode==2)
                {
                    Collect(work[fog?0:1]);
                    if(pending==0&&adc==0&&depth==0)golden&=state->StripGoldenMatches(packet,fog);
                }
            }
        }
        return exact;
    }
    bool CarryMatrix(std::array<Work,5>& work)
    {
        auto state=std::make_unique<GSPackedBatchOracle>();bool exact=true;
        for(u32 family:{0u,3u,4u})for(u32 count:{0u,1u,2u,3u,5u,6u,7u,8u,127u,128u,129u,257u})
        for(u32 path:{2u,3u})for(u32 pending:{0u,1u,2u})for(u32 depth=0;depth<3;++depth)
        {
            GSConfig.UserHacks_Limit24BitDepth=static_cast<GSLimit24BitDepth>(depth);
            auto prefix=GSVertexPacket(pending,true,false);GSStripKernelOracle::Regular(prefix,0);
            const auto packet=GSCarryingQuadOracle::ProductionPacket(family,1,count,2);
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                GSNativeMirrorOracle::Prepare(*state,GS_TRIANGLESTRIP);Configure(mode);
                state->Submit(prefix,0);state->SubmitCarrying(packet,0,path);
                const auto actual=GSNativeMirrorOracle::Capture(*state);
                if(!mode)expected=actual;else exact&=actual==expected;
                if(mode==2)Collect(work[family==0?2:family==3?3:4]);
            }
        }
        return exact;
    }
    bool Flush()
    {
        auto state=std::make_unique<GSPackedBatchOracle>();bool exact=true;
        GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
        for(u32 family=0;family<5;++family)
        {
            const bool triple=family<2;const u32 carrying=family==2?0:family==3?3:4;
            auto packet=triple?GSVertexPacket(22000,family==0,false):GSCarryingQuadOracle::ProductionPacket(carrying,1,22000,0);
            if(triple)GSStripKernelOracle::Regular(packet,0);
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                GSNativeMirrorOracle::Prepare(*state,GS_TRIANGLESTRIP);Configure(mode);
                for(u32 i=0;i<3;++i){if(triple)state->Submit(packet,0);else state->SubmitCarrying(packet,0,3);}
                const auto actual=GSNativeMirrorOracle::Capture(*state);exact&=state->vertexFlushes>0;
                if(!mode)expected=actual;else exact&=actual==expected;
            }
        }
        return exact;
    }
    bool Fallback()
    {
        auto state=std::make_unique<GSPackedBatchOracle>();bool exact=true;
        const auto packet=[](){auto p=GSVertexPacket(257,true,false);GSStripKernelOracle::Regular(p,0);return p;}();
        for(u32 variant=0;variant<6;++variant)
        {
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                GSNativeMirrorOracle::Prepare(*state,variant==5?GS_TRIANGLEFAN:GS_TRIANGLESTRIP,
                    variant!=0,false,variant==1,variant==2);Configure(mode);
                if(variant==3)state->SetRawXYOffsetForStrip(0x0001800000018000ull);
                if(variant==4){GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Enabled;GSConfig.AutoFlushSW=true;state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);}
                state->Submit(packet,0);const auto actual=GSNativeMirrorOracle::Capture(*state);
                if(!mode)expected=actual;else exact&=actual==expected;
                if(mode==2){u64 counts[3]{};EmuCoreXOracleGSStripOutlineStats(counts);exact&=counts[0]==0;}
                GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
            }
        }
        return exact;
    }
    bool Seams()
    {
        std::vector<std::vector<u8>> expected;
        for(u32 mode=0;mode<3;++mode)
        {
            // Pending-two snapshots have no indices: TraceNow intentionally
            // retains the previous m_primclass. Give each arm the same fresh
            // constructor state instead of leaking the prior arm's final draw.
            auto state=std::make_unique<GSPackedBatchOracle>();
            Configure(mode);std::vector<std::vector<u8>> actual;
            GSNativeMirrorOracle::OwnershipScript(*state,true,actual);
            if(!mode)expected=actual;else if(actual!=expected)
            {
                const size_t stages=std::min(actual.size(),expected.size());
                for(size_t stage=0;stage<stages;++stage)if(actual[stage]!=expected[stage])
                {
                    size_t at=0;while(at<std::min(actual[stage].size(),expected[stage].size())&&actual[stage][at]==expected[stage][at])++at;
                    std::printf("GS_STRIP_OUTLINE_SEAM mode=%u stage=%zu expected_size=%zu actual_size=%zu byte=%zu expected=%u actual=%u\n",mode,stage,
                        expected[stage].size(),actual[stage].size(),at,at<expected[stage].size()?expected[stage][at]:256u,at<actual[stage].size()?actual[stage][at]:256u);
                    break;
                }
                return false;
            }
        }
        return true;
    }
}
void GSStripOutlineTests()
{
    using namespace GSStripOutlineOracle;
    const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSPackedBatch(true,false);
        EmuCoreXOracleSetGSCarryingLayouts(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);
        EmuCoreXOracleSetGSNativeMirror(true,false,0);EmuCoreXOracleSetGSStripOutline(true,false);});
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
    GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    std::array<Work,5> work{};bool golden=true;
    Check(TripleMatrix(work,golden),"outlined and inline chunks match staged complete triple raw state carry indices and FMM at every boundary");
    Check(golden,"outlined triple chunks preserve independent literal vertex bytes and sequential triangle indices");
    Check(CarryMatrix(work),"outlined three actual carrying Rows preserve staged raw state Q UV fog depth indices and FMM");
    Check(Flush(),"outlined five parser families preserve actual growth vertex-count Flush draw snapshot and last current vertex");
    Check(Fallback(),"unsupported native grid AA1 offset scissor autoflush and topology stay exact with zero outlined work");
    Check(Seams(),"outlined chunk invariants are reread after scissor context XYOFFSET fan and mixed-batch seams");
    Check(GSNativeMirrorOracle::Raster(),"outlined native cull contract retains independently proved strict-interior samples");
    bool actual=true,one=true;
    for(u32 i=0;i<work.size();++i)
    {
        const auto& w=work[i];actual&=w.chunks>0&&w.outlined>0&&w.outlinedVertices>0;
        one&=w.outlined==w.chunks&&w.outlinedVertices==w.vertices&&w.inlineChunks==0;
        std::printf("GS_STRIP_OUTLINE family=%u kernel=%llu vertices=%llu outlined=%llu inline=%llu outlined_vertices=%llu\n",i,
            static_cast<unsigned long long>(w.chunks),static_cast<unsigned long long>(w.vertices),static_cast<unsigned long long>(w.outlined),
            static_cast<unsigned long long>(w.inlineChunks),static_cast<unsigned long long>(w.outlinedVertices));
    }
    Check(actual,"all five eligible production parsers execute the actual pure outlined chunk entry");
    Check(one,"one outlined call owns each bounded chunk with no duplicate inline execution");
}
