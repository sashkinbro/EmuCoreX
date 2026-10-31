// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
extern "C" void EmuCoreXOracleSetGSCarryingLayouts(bool enabled, bool observe);
extern "C" void EmuCoreXOracleGSCarryingLayoutsStats(u64* values);
extern "C" u64 EmuCoreXOracleGSCarryingStagedVertices();
extern "C" bool EmuCoreXOracleGSClassifyCarryingLayout(const GIFPackedReg* tag, u32* info);

namespace GSCarryingLayoutOracle
{
    std::vector<u8> Descriptors(u32 family,u32 padding)
    {
        std::vector<u8> regs;
        if(family==0)regs={GIF_REG_STQ,GIF_REG_RGBA,GIF_REG_XYZF2};
        if(family==1)regs={GIF_REG_STQ,GIF_REG_XYZ2};
        if(family==2)regs={GIF_REG_UV,GIF_REG_XYZ2};
        if(family==3)regs={GIF_REG_RGBA,GIF_REG_XYZ2};
        if(family==4)regs={GIF_REG_UV,GIF_REG_RGBA,GIF_REG_XYZF2};
        if(padding==4&&family>=1&&family<=3){const auto repeat=regs;regs.insert(regs.end(),repeat.begin(),repeat.end());return regs;}
        if(family==0||padding)
        {
            const u32 where=padding==0?0:padding==1?1:padding==2?static_cast<u32>(regs.size()):1;
            regs.insert(regs.begin()+where,GIF_REG_NOP);
            if(padding==3){regs.insert(regs.begin(),GIF_REG_NOP);regs.push_back(GIF_REG_NOP);}
        }
        return regs;
    }

    GIFPackedReg Tag(const std::vector<u8>& regs,u32 loops)
    {
        GIFPackedReg tag{};
        tag.U64[0]=loops|(1ull<<15)|(u64(regs.size()&15)<<60);
        for(u32 i=0;i<regs.size();++i)tag.U64[1]|=u64(regs[i])<<(i*4);
        return tag;
    }

    std::vector<GIFPackedReg> Packet(u32 family,u32 padding,u32 loops,u32 variant)
    {
        const auto regs=Descriptors(family,padding);
        std::vector<GIFPackedReg> packet(1+regs.size()*loops);packet[0]=Tag(regs,loops);
        constexpr u32 q[]={0,0x80000000,0x7fc12345,0x7f800000,0xbf800000,0x3f000000,1,0x3f800000};
        constexpr u16 x[]={100,400,100,400},y[]={100,100,400,400};
        u32 vertex=0;
        for(u32 i=0;i<loops;++i)for(u32 at=0;at<regs.size();++at)
        {
            auto& r=packet[1+i*regs.size()+at];
            for(u32 lane=0;lane<4;++lane)r.U32[lane]=0xf1e2d3c4u^(i*113+at*19+lane);
            if(regs[at]==GIF_REG_STQ)
            {
                r.U32[0]=0x3f000000|(vertex*71&0x7fffff);r.U32[1]=0x3f400000|(vertex*97&0x3fffff);
                r.U32[2]=q[(vertex+variant)%std::size(q)];
            }
            if(regs[at]==GIF_REG_UV){r.U32[0]=0xf123+vertex*13;r.U32[1]=0xe456+vertex*17;}
            if(regs[at]==GIF_REG_RGBA)
                for(u32 lane=0;lane<4;++lane)r.U32[lane]=0xcafe0000u|((vertex*47+lane*31)&255);
            if(regs[at]==GIF_REG_XYZ2||regs[at]==GIF_REG_XYZF2)
            {
                if(regs[at]==GIF_REG_XYZF2)
                {r.XYZF2.X=x[vertex&3];r.XYZF2.Y=y[vertex&3];r.XYZF2.Z=0x123456+(vertex&1);r.XYZF2.F=vertex*7;}
                else {r.XYZ2.X=x[vertex&3];r.XYZ2.Y=y[vertex&3];r.XYZ2.Z=0x89abcde0+(vertex&1);}
                r.U32[3]=(r.U32[3]&~0x8000u)|((variant==1||(variant==2&&vertex%5<2))?0x8000u:0u);
                ++vertex;
            }
        }
        return packet;
    }

    std::vector<GIFPackedReg> LatchProbe()
    {
        std::vector<GIFPackedReg> packet(2);packet[0]=Tag({GIF_REG_RGBA},1);
        packet[1].U32[0]=0x11;packet[1].U32[1]=0x22;packet[1].U32[2]=0x33;packet[1].U32[3]=0x44;
        return packet;
    }

    bool SaveRoundtrip(GSPackedBatchOracle& state,bool load)
    {
        freezeData data{};
        if(state.Freeze(&data,true))return false;
        std::vector<u8> bytes(data.size);data.data=bytes.data();
        if(state.Freeze(&data,false))return false;
        if(load&&state.Defrost(&data))return false;
        state.SetCarryingNativePolicy();
        return true;
    }
}

void GSCarryingLayoutTests()
{
    using namespace GSCarryingLayoutOracle;
    const auto saved=GSConfig;
    const ScopedGuard restore([&](){
        GSConfig=saved;EmuCoreXOracleSetGSCarryingLayouts(true,false);
        EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);
    });
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;
    GSConfig.UserHacks_ReadTCOnClose=false;
    GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    bool classified=true,repeatCounts=true,rejects=true;
    for(u32 family=0;family<5;++family)for(u32 padding=0;padding<(family>=1&&family<=3?5u:4u);++padding)
    {
        const auto regs=Descriptors(family,padding);const auto tag=Tag(regs,17);
        u32 info[8]{};const bool recognized=EmuCoreXOracleGSClassifyCarryingLayout(&tag,info);
        const bool repeated=padding==4;
        const u32 xyz=static_cast<u32>(std::find_if(regs.begin(),regs.end(),[](u8 d){return d==GIF_REG_XYZ2||d==GIF_REG_XYZF2;})-regs.begin());
        const u32 a=static_cast<u32>(std::find_if(regs.begin(),regs.end(),[](u8 d){return d!=GIF_REG_NOP;})-regs.begin());
        const auto rgba=std::find(regs.begin(),regs.end(),GIF_REG_RGBA);
        const u32 rgbaOffset=(family==0||family==4)?static_cast<u32>(rgba-regs.begin()):0;
        classified&=recognized&&info[0]==4+family&&info[4]==(repeated?2u:regs.size())&&
            info[5]==a&&info[6]==rgbaOffset&&info[7]==xyz;
        const auto large=Tag(regs,32767);EmuCoreXOracleGSClassifyCarryingLayout(&large,info);
        repeatCounts&=info[1]==regs.size()&&info[2]==32767;
    }
    for(const std::vector<u8>& regs:std::vector<std::vector<u8>>{
        {GIF_REG_XYZ2,GIF_REG_STQ},{GIF_REG_NOP,GIF_REG_RGBA,GIF_REG_STQ,GIF_REG_XYZF2},
        {GIF_REG_STQ,GIF_REG_STQ,GIF_REG_XYZ2},{GIF_REG_UV,GIF_REG_XYZF2},
        {GIF_REG_RGBA,GIF_REG_A_D,GIF_REG_XYZ2},{GIF_REG_NOP,GIF_REG_NOP}})
    {u32 info[8]{};const auto tag=Tag(regs,3);rejects&=!EmuCoreXOracleGSClassifyCarryingLayout(&tag,info);}
    Check(classified,"all five carrying layout families classify exact NOP offsets and repeated-record strides");
    Check(repeatCounts,"large repeated-pair tags preserve original NREG and 15-bit NLOOP for persistence");
    Check(rejects,"unsupported reordered duplicate and side-effect descriptors retain scalar replay");

    auto state=std::make_unique<GSPackedBatchOracle>();
    bool exact=true,flushExact=true;
    u64 cases=0,handlers=0,vertices=0,kernel=0,referenceDispatch=0,candidateDispatch=0;
    for(u32 family=0;family<5;++family)for(u32 prim:{GS_TRIANGLESTRIP,GS_TRIANGLELIST,GS_SPRITE,GS_TRIANGLEFAN})
    for(u32 padding=0;padding<(family>=1&&family<=3?5u:4u);++padding)
    for(u32 variant=0;variant<3;++variant)for(u32 count:{0u,1u,2u,5u,6u,127u,128u,129u,257u})
    {
        const auto packet=Packet(family,padding,count,variant);
        std::vector<u8> reference,draws;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSCarryingLayouts(candidate,true);
            state->Prepare(prim,true,false,false,true);state->SetTraceFlags(true,variant==2,variant==1,false);
            state->SeedCarryingLayout();state->SubmitCarrying(packet,0,3);
            auto actual=state->CarryingSnapshot();
            const auto trace=state->TraceNow();actual.insert(actual.end(),trace.begin(),trace.end());
            state->SubmitCarrying(LatchProbe(),0,3);
            const auto after=state->CarryingSnapshot();actual.insert(actual.end(),after.begin(),after.end());
            u64 stats[4]{};EmuCoreXOracleGSCarryingLayoutsStats(stats);
            if(!candidate){reference=actual;draws=state->draws;referenceDispatch+=stats[3];}
            else
            {
                exact&=actual==reference&&state->draws==draws;
                handlers+=stats[0];vertices+=stats[1];kernel+=stats[2];candidateDispatch+=stats[3];
            }
        }
        ++cases;
    }
    Check(exact,"carrying layouts preserve scalar raw vertices latch Q UV FOG FMM ADC and draw state across primitive and record boundaries");
    Check(handlers>0&&vertices>0,"recognized carrying layouts enter actual fused handlers and consume vertex records");
    Check(kernel>0,"eligible carrying triangle strips reach the bounded two-pass kernel");
    Check(candidateDispatch<referenceDispatch,"carrying handlers remove indirect per-descriptor dispatch on qualified runs");

    bool partial=true,uvHack=true;
    for(u32 family=0;family<5;++family)for(u32 path:{2u,3u})for(u32 chunk:{1u,2u,5u})
    {
        const auto packet=Packet(family,1,129,2);std::vector<u8> reference,draws;
        for(bool candidate:{false,true})
        {
            GSConfig.UserHacks_ForceEvenSpritePosition=true;
            EmuCoreXOracleSetGSCarryingLayouts(candidate,true);
            state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);state->SetTraceFlags(true,false,false,false);
            state->SeedCarryingLayout(0x7fc12345);
            auto prefix=GSVertexPacket(7,true,false);GSStripKernelOracle::Regular(prefix,0);state->Submit(prefix,0);
            state->Submit(GSPackedBatchChanges(GS_TRIANGLESTRIP),0);
            state->SubmitCarrying(packet,chunk,path);
            const auto actual=state->CarryingSnapshot();
            if(!candidate){reference=actual;draws=state->draws;}
            else {partial&=actual==reference&&draws==state->draws;uvHack&=actual==reference;}
        }
    }
    GSConfig.UserHacks_ForceEvenSpritePosition=false;
    Check(partial&&uvHack,"dirty descriptor prefixes partial PATH2 PATH3 resumes context changes and packed UV sticky state preserve scalar behavior");

    bool restored=true,continued=true;
    for(u32 family=0;family<5;++family)
    {
        const auto regs=Descriptors(family,1);const auto packet=Packet(family,1,17,0);
        const u32 xyz=static_cast<u32>(std::find_if(regs.begin(),regs.end(),[](u8 d){return d==GIF_REG_XYZ2||d==GIF_REG_XYZF2;})-regs.begin());
        std::vector<GIFPackedReg> prefix(packet.begin(),packet.begin()+1+xyz),suffix(packet.begin()+1+xyz,packet.end());
        std::vector<u8> reference;
        for(bool load:{false,true})
        {
            state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);state->SetTraceFlags(true,false,false,false);
            state->SeedCarryingLayout();state->SubmitCarrying(prefix,0,3);
            const u32 beforeReg=state->ParserReg(3);
            const bool saveOk=SaveRoundtrip(*state,load);
            const u32 afterReg=state->ParserReg(3);
            restored&=beforeReg==xyz&&saveOk&&afterReg==xyz;
            state->draws.clear();state->SubmitCarrying(suffix,0,3);
            const auto actual=state->CarryingSnapshot();if(!load)reference=actual;else
            {
                continued&=actual==reference;
                if(actual!=reference||beforeReg!=xyz||afterReg!=xyz||!saveOk)
                {
                    std::printf("GS_CARRY_SAVE family=%u beforeReg=%u afterReg=%u expectedReg=%u save=%u sizes=%zu/%zu\n",
                        family,beforeReg,afterReg,xyz,saveOk,reference.size(),actual.size());
                    u32 differences=0;
                    for(size_t at=0;at<std::min(actual.size(),reference.size())&&differences<20;++at)
                        if(actual[at]!=reference[at])
                        {
                            std::printf("GS_CARRY_DIFF byte=%zu expected=%02x actual=%02x\n",at,reference[at],actual[at]);
                            ++differences;
                        }
                }
            }
        }
    }
    Check(restored&&continued,"mid-tag savestate restores the exact descriptor cursor and continues carrying vertex semantics");

    for(u32 family:{0u,3u,4u})
    {
        const auto packet=Packet(family,0,22000,0);std::vector<u8> reference,draws;
        state->captureTrace=true;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSCarryingLayouts(candidate,true);
            state->Prepare(GS_TRIANGLESTRIP,true,false,false,true);state->SetTraceFlags(true,false,false,false);
            state->SeedCarryingLayout();for(u32 i=0;i<3;++i)state->SubmitCarrying(packet,0,3);
            const auto actual=state->CarryingSnapshot();
            if(!candidate){reference=actual;draws=state->draws;}
            else flushExact&=actual==reference&&draws==state->draws&&state->vertexFlushes>0;
        }
        state->captureTrace=false;
    }
    Check(flushExact,"carrying layouts retain exact real growth vertex-count flush current vertex and FMM draw traces");

    bool depthExact=true,depthKnown=true,depthKernel=true;
    for(auto mode:{GSLimit24BitDepth::PrioritizeUpper,GSLimit24BitDepth::PrioritizeLower})
    for(u32 family=0;family<5;++family)for(u32 count:{5u,129u})
    {
        GSConfig.UserHacks_Limit24BitDepth=mode;
        const u32 prim=(family==1||family==2)?GS_SPRITE:GS_TRIANGLESTRIP;
        const auto packet=Packet(family,0,count,0);std::vector<u8> reference,draws;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSCarryingLayouts(candidate,true);
            state->Prepare(prim,true,false,false,true);state->SetTraceFlags(true,false,false,false);
            state->SeedCarryingLayout();state->SubmitCarrying(packet,0,3);
            auto actual=state->CarryingSnapshot();const auto trace=state->TraceNow();actual.insert(actual.end(),trace.begin(),trace.end());
            const bool fog=family==0||family==4;
            const u32 expected=mode==GSLimit24BitDepth::PrioritizeUpper?(fog?0x1256u:0x89abe0u):(fog?0x123456u:0xabcde0u);
            depthKnown&=state->CarryingCurrentZ()==expected;
            if(!candidate){reference=actual;draws=state->draws;}
            else
            {
                u64 stats[4]{};EmuCoreXOracleGSCarryingLayoutsStats(stats);
                depthExact&=actual==reference&&state->draws==draws&&stats[0]>0;
                if(prim==GS_TRIANGLESTRIP&&count==129)depthKernel&=stats[2]>0;
            }
        }
    }
    GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    Check(depthExact&&depthKnown&&depthKernel,"active 24-bit depth policies match scalar traces and literal Z values through carrying direct and kernel paths");

    bool afExact=true,afRoute=true;
    u64 afFlushes=0,stagedVertices=0;
    for(auto level:{GSHWAutoFlushLevel::SpritesOnly,GSHWAutoFlushLevel::Enabled})
    for(u32 family=0;family<5;++family)
    {
        GSConfig.UserHacks_AutoFlush=level;GSConfig.AutoFlushSW=true;
        const u32 prim=(family==1||family==2)?GS_SPRITE:GS_TRIANGLESTRIP;
        auto packet=Packet(family,1,129,0);const auto regs=Descriptors(family,1);
        for(u32 i=0;i<129;++i)for(u32 at=0;at<regs.size();++at)
        {
            auto& r=packet[1+i*regs.size()+at];
            if(regs[at]==GIF_REG_STQ)
            {
                r.U32[0]=family==1?0x3f200000:0x3ea00000;
                r.U32[1]=family==1?0x3ed00000:0x3e500000;r.U32[2]=0x3f800000;
            }
            if(regs[at]==GIF_REG_UV){r.U32[0]=160;r.U32[1]=208;}
            if(prim==GS_SPRITE&&regs[at]==GIF_REG_XYZ2)
            {r.XYZ2.X=(i&1)?400:100;r.XYZ2.Y=(i&1)?400:100;}
        }
        std::vector<GIFPackedReg> setup(4);setup[0]=Tag({GIF_REG_A_D},3);
        setup[1].A_D.ADDR=GIF_A_D_REG_FRAME_1;setup[1].r.FRAME.FBW=1;
        setup[2].A_D.ADDR=GIF_A_D_REG_TEX0_1;
        setup[2].r.TEX0.TBW=1;setup[2].r.TEX0.TW=5;setup[2].r.TEX0.TH=6;
        // Fixed layer0: a negative Q-derived LOD must not suppress this
        // texture-cache overlap premise before the autoflush code is reached.
        setup[3].A_D.ADDR=GIF_A_D_REG_TEX1_1;
        std::vector<GIFPackedReg> texflush(2);texflush[0]=Tag({GIF_REG_A_D},1);
        texflush[1].A_D.ADDR=GIF_A_D_REG_TEXFLUSH;
        std::vector<u8> reference,draws;
        for(bool candidate:{false,true})
        {
            EmuCoreXOracleSetGSCarryingLayouts(candidate,true);
            state->Prepare(prim,true,false,false,true);state->SetTraceFlags(true,family==2||family==4,false,false);
            state->SeedCarryingLayout();state->SeedCarryingAutoFlush();state->Submit(setup,0);
            state->SubmitCarrying(packet,0,3);
            // Constant texels stay in one texture-cache page, so overlap alone
            // correctly does not flush. A real TEXFLUSH between overlapping
            // draws forces the production invalidation branch to execute.
            state->Submit(texflush,0);state->SubmitCarrying(packet,0,3);
            const auto actual=state->CarryingSnapshot();
            if(!candidate){reference=actual;draws=state->draws;}
            else
            {
                u64 stats[4]{};EmuCoreXOracleGSCarryingLayoutsStats(stats);
                const u64 staged=EmuCoreXOracleGSCarryingStagedVertices();
                const bool requireStaged=prim==GS_SPRITE||level==GSHWAutoFlushLevel::Enabled;
                if(actual!=reference||state->draws!=draws||!(stats[0]>0&&(requireStaged?(staged>0&&stats[2]==0):staged==0)))
                {
                    std::printf("GS_CARRY_AF_DIFF family=%u level=%u staged=%llu kernel=%llu af=%u state=%zu/%zu draws=%zu/%zu\n",
                        family,static_cast<u32>(level),static_cast<unsigned long long>(staged),static_cast<unsigned long long>(stats[2]),
                        state->autoFlushes,reference.size(),actual.size(),draws.size(),state->draws.size());
                    const auto differences=[](const char* which,const std::vector<u8>& expected,const std::vector<u8>& actual){
                        u32 count=0;for(size_t at=0;at<std::min(expected.size(),actual.size())&&count<12;++at)
                            if(expected[at]!=actual[at]){std::printf("GS_CARRY_AF_BYTE %s offset=%zu expected=%02x actual=%02x\n",which,at,expected[at],actual[at]);++count;}
                    };
                    differences("state",reference,actual);differences("draw",draws,state->draws);
                }
                afExact&=actual==reference&&state->draws==draws;
                afRoute&=stats[0]>0&&(requireStaged?(staged>0&&stats[2]==0):staged==0);
                afFlushes+=state->autoFlushes;stagedVertices+=staged;
            }
        }
    }
    Check(afExact&&afRoute,"enabled sprite and full autoflush retain actual staged carrying routes and exact scalar current Q draw state");
    Check(afFlushes>0,"carrying autoflush fixture triggers real texture-target overlap flushes");
    std::printf("GS_CARRY_AF flushes=%llu stagedVertices=%llu\n",static_cast<unsigned long long>(afFlushes),static_cast<unsigned long long>(stagedVertices));
    std::printf("GS_CARRYING_LAYOUT cases=%llu handlers=%llu vertices=%llu kernel=%llu scalarReference=%llu scalarCandidate=%llu\n",
        static_cast<unsigned long long>(cases),static_cast<unsigned long long>(handlers),static_cast<unsigned long long>(vertices),
        static_cast<unsigned long long>(kernel),static_cast<unsigned long long>(referenceDispatch),static_cast<unsigned long long>(candidateDispatch));
}
