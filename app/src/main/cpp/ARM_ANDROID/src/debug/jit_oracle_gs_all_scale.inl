// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetGSAllScaleKernel(bool enabled);
extern "C" void EmuCoreXOracleGSAllScaleKernelStats(u64* values);
void GSAllScalePackedTests()
{
    using namespace GSCarryingLayoutOracle;
    const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSAllScaleKernel(true);
        EmuCoreXOracleSetGSCarryingLayouts(true,false);EmuCoreXOracleSetGSPackedBatch(true,false);});
    auto state=std::make_unique<GSPackedBatchOracle>();
    bool snapshots=true,draws=true,trace=true,coverage=true,phases=true,aaGate=true;
    u64 cases=0,chunks=0,vertices=0,falseChunks=0,liveRuns=0;
    for(float scale:{1.0f,1.5f,2.0f,4.0f,8.0f})
    for(auto hpo:{GSHalfPixelOffset::Normal,GSHalfPixelOffset::Native,GSHalfPixelOffset::NativeWTexOffset})
    for(u32 family=0;family<7;++family)
    for(u32 prim:{GS_TRIANGLESTRIP,GS_TRIANGLELIST,GS_SPRITE})
    for(u32 mode=0;mode<3;++mode)
    {
        GSConfig.UpscaleMultiplier=scale;GSConfig.UserHacks_HalfPixelOffset=hpo;
        GSConfig.UserHacks_AutoFlush=mode?GSHWAutoFlushLevel::Enabled:GSHWAutoFlushLevel::Disabled;
        GSConfig.AutoFlushSW=false;GSConfig.UserHacks_ReadTCOnClose=false;
        GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
        const bool aa=mode==2;
        std::vector<u8> descriptors=family<5?Descriptors(family,family==0?2:1):
            std::vector<u8>{GIF_REG_STQ,GIF_REG_RGBA,static_cast<u8>(family==5?GIF_REG_XYZ2:GIF_REG_XYZF2)};
        const u32 count=263;
        std::vector<GIFPackedReg> packet;
        if(family<5)packet=Packet(family,family==0?2:1,count,0);
        else
        {
            packet=Packet(0,2,count,0);
            std::vector<GIFPackedReg> triple(1+3*count);triple[0]=Tag(descriptors,count);
            for(u32 i=0;i<count;++i)
            {
                triple[1+3*i]=packet[1+4*i];triple[2+3*i]=packet[2+4*i];
                triple[3+3*i]=packet[3+4*i];
                if(family==5)triple[3+3*i].XYZ2.Z=0xf1234567u+i;
            }
            packet=std::move(triple);
        }
        // Give every phase both accepted geometry and ADC/rejected runs; use
        // exact parser outputs including exceptional Q and carried fog bits.
        for(u32 i=0;i<count;++i)
        {
            const u32 xyz=static_cast<u32>(std::find_if(descriptors.begin(),descriptors.end(),[](u8 d){return d==GIF_REG_XYZ2||d==GIF_REG_XYZF2;})-descriptors.begin());
            packet[1+i*descriptors.size()+xyz].U32[3]&=~0x8000u;
            if(i%11<2)packet[1+i*descriptors.size()+xyz].U32[3]|=0x8000u;
        }
        std::vector<u8> expected,expectedDraws,expectedTrace;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSAllScaleKernel(candidate);
            EmuCoreXOracleSetGSCarryingLayouts(true,false);EmuCoreXOracleSetGSPackedBatch(true,false);
            state->Prepare(prim,scale==1.0f,false,aa,true);
            state->SetTraceFlags(true,family==2||family==4,mode==1,false);
            state->SeedCarryingLayout();state->SetAllScalePolicy();state->SeedAllScaleFeedback(false);
            // Partial descriptor delivery and growth both occur in the actual
            // Transfer walker; chunks remain long enough to exercise RunChunk.
            state->SubmitCarrying(packet,mode==1?137u:0u,3);
            const auto actual=state->CarryingSnapshot();const auto actualTrace=state->TraceNow();
            if(!candidate){expected=actual;expectedDraws=state->draws;expectedTrace=actualTrace;}
            else
            {
                u64 stats[4]{};EmuCoreXOracleGSAllScaleKernelStats(stats);
                const bool equal=actual==expected&&state->draws==expectedDraws&&actualTrace==expectedTrace;
                if(!equal)std::printf("GS_ALL_SCALE_DIFF scale=%g hpo=%u family=%u prim=%u mode=%u state=%zu/%zu chunks=%llu\n",scale,static_cast<u32>(hpo),family,prim,mode,actual.size(),expected.size(),static_cast<unsigned long long>(stats[0]));
                snapshots&=actual==expected;draws&=state->draws==expectedDraws;trace&=actualTrace==expectedTrace;
                if(scale==2.0f||scale==4.0f||scale==8.0f)
                {
                    if(prim!=GS_SPRITE&&!aa&&hpo!=GSHalfPixelOffset::Normal&&
                        (family>=5||(prim==GS_TRIANGLESTRIP&&(family==0||family==3||family==4))))phases&=stats[0]>0;
                    if(aa)aaGate&=stats[0]==0;
                    if(prim!=GS_SPRITE&&state->ParserLoops(3)==0)coverage&=state->FusedCoverageComplete();
                }
                chunks+=stats[0];vertices+=stats[1];falseChunks+=stats[2];liveRuns+=stats[3];++cases;
            }
        }
    }
    Check(snapshots,"all-scale actual PACKED Transfer matches previous scalar/direct consumer state across seven layouts and ADC/Q/fog/chunk seams");
    Check(draws&&trace,"all-scale actual draw snapshots and trace match across upscale phase and AA1 fallback");
    Check(phases,"actual known-phase 2x/4x/8x triangles enter two-pass chunks for disabled and invariant-false autoFlush");
    Check(aaGate,"actual AA1 coverage expansion retains scalar kick without two-pass cull");
    Check(coverage,"all-scale chunk and scalar seams retain complete fused-trace coverage");
    Check(chunks>0&&falseChunks>0,"autoFlush invariant-false packed runs enter the real chunk consumer");
    std::printf("GS_ALL_SCALE cases=%llu chunks=%llu vertices=%llu falseChunks=%llu liveRuns=%llu\n",
        static_cast<unsigned long long>(cases),static_cast<unsigned long long>(chunks),static_cast<unsigned long long>(vertices),
        static_cast<unsigned long long>(falseChunks),static_cast<unsigned long long>(liveRuns));
    bool afExact=true,afLive=true,engineLevels=true,aaUnsupported=true;
    for(u32 prim:{GS_TRIANGLELIST,GS_TRIANGLESTRIP,GS_SPRITE})for(bool live:{false,true})
    {
        GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
        GSConfig.UpscaleMultiplier=prim==GS_SPRITE?1.0f:2.0f;
        GSConfig.UserHacks_HalfPixelOffset=GSHalfPixelOffset::Native;
        // Exercise the exact hot pairs which own fused handlers. Padding=1
        // is STQ,NOP,RGBA,XYZF2; padding=2 above appends its NOP instead.
        const u32 family=prim==GS_SPRITE?2u:0u;
        auto packet=Packet(family,1,257,0);
        auto regs=Descriptors(family,1);
        if(prim==GS_TRIANGLELIST)
        {
            std::vector<GIFPackedReg> triple(1+3*257);triple[0]=Tag({GIF_REG_STQ,GIF_REG_RGBA,GIF_REG_XYZF2},257);
            for(u32 i=0;i<257;++i)
            {triple[1+3*i]=packet[1+4*i];triple[2+3*i]=packet[3+4*i];triple[3+3*i]=packet[4+4*i];}
            packet=std::move(triple);regs={GIF_REG_STQ,GIF_REG_RGBA,GIF_REG_XYZF2};
        }
        if(prim==GS_SPRITE)for(u32 i=0;i<257;++i)
        {
            packet[1+i*regs.size()].U32[0]=160;packet[1+i*regs.size()].U32[1]=208;
            auto& xyz=packet[3+i*regs.size()];
            xyz.XYZ2.X=xyz.XYZ2.Y=(i&1)?400:100;
        }
        std::vector<GIFPackedReg> texflush(2);texflush[0]=Tag({GIF_REG_A_D},1);
        texflush[1].A_D.ADDR=GIF_A_D_REG_TEXFLUSH;
        std::vector<u8> expected,expectedDraws;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSAllScaleKernel(candidate);
            state->Prepare(prim,prim==GS_SPRITE,false,false,true);
            state->SetTraceFlags(true,true,false,false);state->SeedCarryingLayout();state->SetAllScalePolicy();
            state->SeedAllScaleFeedback(live);state->ForceEngineAutoFlush(GSHWAutoFlushLevel::Enabled);
            state->Submit(packet,0);state->Submit(texflush,0);state->Submit(packet,0);
            const auto actual=state->CarryingSnapshot();
            if(!candidate){expected=actual;expectedDraws=state->draws;}
            else
            {
                u64 stats[4]{};EmuCoreXOracleGSAllScaleKernelStats(stats);
                afExact&=actual==expected&&state->draws==expectedDraws;
                engineLevels&=state->engineLevelQueries>0;
                const bool routed=live?(stats[3]>0&&state->autoFlushes>0):(stats[2]>0&&stats[0]>0);
                if(!routed)std::printf("GS_ALL_SCALE_AF prim=%u live=%u chunks=%llu false=%llu staged=%llu flushes=%u\n",
                    prim,live,static_cast<unsigned long long>(stats[0]),static_cast<unsigned long long>(stats[2]),
                    static_cast<unsigned long long>(stats[3]),state->autoFlushes);
                afLive&=routed;
            }
        }
    }
    state->forceEngineLevel=false;state->coverageAlpha=false;
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.UpscaleMultiplier=2.0f;
    GSConfig.UserHacks_HalfPixelOffset=GSHalfPixelOffset::Native;
    EmuCoreXOracleSetGSAllScaleKernel(true);
    state->Prepare(GS_TRIANGLESTRIP,false,false,true,true);state->SetTraceFlags(false,false,false,false);
    state->SeedCarryingLayout();state->SetAllScalePolicy();state->Submit(Packet(0,1,257,0),0);
    u64 aaStats[4]{};EmuCoreXOracleGSAllScaleKernelStats(aaStats);aaUnsupported&=aaStats[0]>0;
    Check(afExact,"autoFlush live and invariant-false actual Transfer routes retain exact current vertex/Q and ordered draw snapshots");
    Check(afLive,"live feedback stays staged and actually flushes while invariant-false autoFlush uses chunks including native sprites");
    Check(engineLevels,"ResetHandlers uses the consuming engine virtual autoFlush level instead of the process hardware setting");
    Check(aaUnsupported,"AA1 flag without engine coverage expansion remains eligible for actual two-pass chunks");

    bool boundaryExact=true,boundaryFlush=true,boundaryChunk=true;
    for(u32 prim:{GS_TRIANGLESTRIP,GS_TRIANGLELIST})
    {
        GSConfig.UpscaleMultiplier=4.0f;GSConfig.UserHacks_HalfPixelOffset=GSHalfPixelOffset::Native;
        GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
        auto large=Packet(0,2,32700,0),tail=Packet(0,2,257,0);
        const auto triples=[](std::vector<GIFPackedReg> source) {
            const u32 n=(source.size()-1)/4;
            std::vector<GIFPackedReg> out(1+3*n);out[0]=Tag({GIF_REG_STQ,GIF_REG_RGBA,GIF_REG_XYZF2},n);
            for(u32 i=0;i<n;++i){out[1+3*i]=source[1+4*i];out[2+3*i]=source[2+4*i];out[3+3*i]=source[3+4*i];}
            return out;
        };
        if(prim==GS_TRIANGLELIST){large=triples(std::move(large));tail=triples(std::move(tail));}
        std::vector<u8> expected,expectedDraws;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSAllScaleKernel(candidate);state->forceEngineLevel=false;state->coverageAlpha=true;
            state->Prepare(prim,false,false,false,true);state->SetTraceFlags(true,false,true,false);
            state->SeedCarryingLayout();state->SetAllScalePolicy();state->captureTrace=true;
            state->Submit(large,0);state->Submit(large,0);state->Submit(tail,137);
            const auto actual=state->CarryingSnapshot();
            if(!candidate){expected=actual;expectedDraws=state->draws;}
            else
            {
                u64 stats[4]{};EmuCoreXOracleGSAllScaleKernelStats(stats);
                boundaryExact&=actual==expected&&state->draws==expectedDraws;
                boundaryFlush&=state->vertexFlushes>0;boundaryChunk&=stats[0]>0&&stats[1]>60000;
            }
            state->captureTrace=false;
        }
    }
    Check(boundaryExact&&boundaryFlush,"all-scale actual triangle vertex-count flush preserves current carry, FMM draw trace, and resumed partial descriptors");
    Check(boundaryChunk,"actual all-scale triangle chunks stop at the scalar vertex-count flush seam and re-enter afterward");

}
