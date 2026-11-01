// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after the existing packed/strip/mirror/carrying helpers.
extern "C" void EmuCoreXOracleSetGSNativeMirror(bool enabled,bool observe,u32 mutation);
extern "C" void EmuCoreXOracleGSNativeMirrorStats(u64* values);
namespace GSNativeMirrorOracle
{
    std::vector<GIFPackedReg> Singles(u32 count,bool fog=false,bool reglist=false,u32 shape=0,bool rawADC=false,bool forcedADC=false,u32 base=0)
    {
        const u32 descriptor=fog?(forcedADC?GIF_REG_XYZF3:GIF_REG_XYZF2):(forcedADC?GIF_REG_XYZ3:GIF_REG_XYZ2);
        std::vector<GIFPackedReg> packet(1+(reglist?(count+1)/2:count));
        packet[0].U64[0]=count|(1ull<<15)|(reglist?1ull<<58:0)|(1ull<<60);packet[0].U64[1]=descriptor;
        constexpr u32 xs[]={100,400,100,400},ys[]={100,100,400,400};
        constexpr u32 edges[]={0,1,15,16,17,32767,32768,65534,65535};
        for(u32 i=0;i<count;++i)
        {
            u32 x=xs[i&3],y=ys[i&3];
            if(shape==1&&i%3==2){x=xs[(i-2)&3];y=ys[(i-2)&3];}
            if(shape==2){x+=20000;y+=20000;}
            if(shape==3){x=edges[i%std::size(edges)];y=edges[(i/3)%std::size(edges)];}
            x=(x+base)&65535;y=(y+base)&65535;
            if(reglist)
            {
                GIFReg value{};if(fog){value.XYZF.X=x;value.XYZF.Y=y;value.XYZF.Z=0x123456+i;value.XYZF.F=i*7;}
                else{value.XYZ.X=x;value.XYZ.Y=y;value.XYZ.Z=0x12345678+i;}
                std::memcpy(reinterpret_cast<u8*>(packet.data()+1)+i*8,&value,8);
            }
            else
            {
                auto& value=packet[1+i];for(u32& lane:value.U32)lane=0xf1e2d3c4u^(i*73);
                if(fog){value.XYZF2.X=x;value.XYZF2.Y=y;value.XYZF2.Z=0x123456+i;value.XYZF2.F=i*7;}
                else{value.XYZ2.X=x;value.XYZ2.Y=y;value.XYZ2.Z=0x12345678+i;}
                value.U32[3]=(value.U32[3]&~0x8000u)|((rawADC&&i%5<2)?0x8000u:0u);
            }
        }
        return packet;
    }
    std::vector<GIFPackedReg> Scissor(u32 left,u32 top,u32 right,u32 bottom,u32 context=0)
    {
        std::vector<GIFPackedReg> out(2);out[0]=GSCarryingLayoutOracle::Tag({GIF_REG_A_D},1);
        out[1].A_D.ADDR=context?GIF_A_D_REG_SCISSOR_2:GIF_A_D_REG_SCISSOR_1;
        out[1].r.SCISSOR.SCAX0=left;out[1].r.SCISSOR.SCAY0=top;out[1].r.SCISSOR.SCAX1=right;out[1].r.SCISSOR.SCAY1=bottom;return out;
    }
    std::vector<GIFPackedReg> Offset(u32 x,u32 y)
    {
        std::vector<GIFPackedReg> out(2);out[0]=GSCarryingLayoutOracle::Tag({GIF_REG_A_D},1);out[1].A_D.ADDR=GIF_A_D_REG_XYOFFSET_1;
        out[1].r.XYOFFSET.OFX=x;out[1].r.XYOFFSET.OFY=y;return out;
    }
    void Prepare(GSPackedBatchOracle& s,u32 prim,bool native=true,bool offset=false,bool aa1=false,bool invalid=false)
    {
        s.Reset(true);s.Prepare(prim,native,offset,aa1,true,invalid);s.SetTraceFlags(true,false,true,false);s.SeedCarryingLayout();
    }
    std::vector<u8> Capture(GSPackedBatchOracle& s)
    {
        auto out=s.CarryingSnapshot();const auto trace=s.TraceNow();out.insert(out.end(),trace.begin(),trace.end());
        GSPackedBatchOracle::Append(out,s.draws.size());out.insert(out.end(),s.draws.begin(),s.draws.end());return out;
    }
    bool LiteralGolden()
    {
        auto state=std::make_unique<GSVertexPacketOracle>();bool exact=true;
        for(u32 prim:{GS_TRIANGLESTRIP,GS_TRIANGLELIST})for(bool fog:{false,true})for(bool reglist:{false,true})
        {
            state->Reset(true);state->Prepare(prim,true);const auto packet=Singles(17,fog,reglist);state->Transfer<3>(reinterpret_cast<const u8*>(packet.data()),static_cast<u32>(packet.size()));
            const auto bytes=state->Snapshot();if(bytes.size()<136){exact=false;continue;}
            u32 tail=0,itail=0;std::memcpy(&tail,bytes.data()+40,4);std::memcpy(&itail,bytes.data()+132,4);
            exact&=tail==17&&itail==(prim==GS_TRIANGLESTRIP?45u:15u);
            constexpr u32 xs[]={100,400,100,400},ys[]={100,100,400,400};
            if(tail!=17||bytes.size()<136+tail*32+itail*2){exact=false;continue;}
            for(u32 i=0;i<tail;++i)
            {
                const std::array<u32,8> v{0,0,0,0,xs[i&3]|(ys[i&3]<<16),fog?0x123456+i:0x12345678+i,0x24681357,fog?(i*7)&255u:0x89abcdef};
                exact&=std::memcmp(bytes.data()+136+i*32,v.data(),32)==0;
            }
            for(u32 i=0;i<itail;++i){u16 actual;std::memcpy(&actual,bytes.data()+136+tail*32+i*2,2);exact&=actual==(prim==GS_TRIANGLESTRIP?i/3+i%3:i);}
        }
        return exact;
    }
    bool Raster()
    {
        bool exact=true;u32 rng=0x542ad933;u64 covered=0;
        const auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
        const std::array<s32,4> clip{-8,-8,16392,16392};
        for(u32 i=0;i<4000;++i)
        {
            std::array<s32,6> p;for(auto& v:p)v=static_cast<s32>(next()%97);
            const auto cross=[&](u32 a,u32 b,s64 x,s64 y){return (s64(p[b*2])-p[a*2])*(y-p[a*2+1])-(s64(p[b*2+1])-p[a*2+1])*(x-p[a*2]);};
            bool sample=false;for(s64 y=0;y<=96;y+=16)for(s64 x=0;x<=96;x+=16)
            {const s64 a=cross(0,1,x,y),b=cross(1,2,x,y),c=cross(2,0,x,y);sample|=(a>0&&b>0&&c>0)||(a<0&&b<0&&c<0);}
            if(sample){++covered;exact&=EmuCoreXOracleGSStripCull(p.data(),clip.data(),true)==0;}
        }
        return exact&&covered>0;
    }
    void OwnershipScript(GSPackedBatchOracle& state,bool candidate,std::vector<std::vector<u8>>& stages)
    {
        Prepare(state,GS_TRIANGLESTRIP);EmuCoreXOracleSetGSNativeMirror(candidate,true,0);
        const auto submit=[&](const std::vector<GIFPackedReg>& p){state.Submit(p,0);stages.push_back(Capture(state));};
        submit(Singles(2));submit(Scissor(600,600,800,800));submit(Singles(3));submit(Scissor(0,0,1023,1023));
        submit(Singles(17));auto batch=GSVertexPacket(257,true,false);GSStripKernelOracle::Regular(batch,0);submit(batch);submit(Singles(17,false,false,2));
        submit(GSCarryingLayoutOracle::Packet(3,1,129,2));submit(Singles(7,true,true));
        submit(Scissor(1,1,10,10,1));std::vector<GIFPackedReg> context(2);context[0]=GSCarryingLayoutOracle::Tag({GIF_REG_A_D},1);
        context[1].A_D.ADDR=GIF_A_D_REG_PRIM;context[1].r.PRIM.PRIM=GS_TRIANGLESTRIP;context[1].r.PRIM.CTXT=1;submit(context);submit(Singles(17));
        context[1].r.PRIM.CTXT=0;submit(context);submit(Offset(32768,32768));submit(Singles(17,true,false,0,false,false,32768));
        context[1].r.PRIM.PRIM=GS_TRIANGLEFAN;submit(context);submit(Singles(7));context[1].r.PRIM.PRIM=GS_TRIANGLESTRIP;submit(context);submit(Singles(7));
    }
    std::vector<u8> ScissorMutation(GSPackedBatchOracle& state,u32 mutation)
    {
        Prepare(state,GS_TRIANGLESTRIP);EmuCoreXOracleSetGSNativeMirror(true,true,mutation);
        state.Submit(Singles(2),0);state.Submit(Scissor(600,600,800,800),0);
        auto third=Singles(1);third[1].XYZ2.Y=400;state.Submit(third,0);return Capture(state);
    }
    std::vector<u8> BatchMutation(GSPackedBatchOracle& state,u32 mutation,u64& publications)
    {
        Prepare(state,GS_TRIANGLESTRIP);EmuCoreXOracleSetGSNativeMirror(true,true,mutation);state.Submit(Singles(17),0);
        auto packet=GSVertexPacket(257,true,false);GSStripKernelOracle::Regular(packet,0);
        for(u32 i=0;i<257;++i){packet[1+i*3+2].XYZF2.X+=20000;packet[1+i*3+2].XYZF2.Y+=20000;}
        state.Submit(packet,0);state.Submit(Singles(3,false,false,2),0);
        u64 stats[8]{};EmuCoreXOracleGSNativeMirrorStats(stats);publications=stats[5];return Capture(state);
    }
    bool Save(GSPackedBatchOracle& state,bool candidate,std::vector<u8>& payload,std::vector<u8>& after)
    {
        Prepare(state,GS_TRIANGLESTRIP);EmuCoreXOracleSetGSNativeMirror(candidate,true,0);const auto packet=Singles(17);
        std::vector<GIFPackedReg> prefix(packet.begin(),packet.begin()+3),suffix(packet.begin()+3,packet.end());state.Submit(prefix,0);
        freezeData data{};if(state.Freeze(&data,true))return false;
        const int expected=data.size;
        std::vector<u8> bytes(data.size+32,0xa5);data.data=bytes.data()+16;
        if(state.Freeze(&data,false))return false;
        u32 version=0;std::memcpy(&version,data.data,sizeof(version));
        bool guards=data.size==expected&&version==9;for(u32 i=0;i<16;++i)guards&=bytes[i]==0xa5&&bytes[16+data.size+i]==0xa5;
        payload.assign(data.data,data.data+data.size);if(state.Defrost(&data))return false;state.SetCarryingNativePolicy();state.draws.clear();state.Submit(suffix,0);after=Capture(state);return guards;
    }
}
void GSNativeMirrorTests()
{
    using namespace GSNativeMirrorOracle;const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSNativeMirror(true,false,0);EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSStripKernel(true,false,0);});
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;GSConfig.UserHacks_ReadTCOnClose=false;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    GSStripKernelOracle::CullTests();
    Check(Raster(),"native mirror reject never drops an independently proved strict-interior native integer pixel sample");
    Check(LiteralGolden(),"native single PACKED REGLIST XYZ2 XYZF2 vertices and strip/list indices match literal bytes");
    auto state=std::make_unique<GSPackedBatchOracle>();state->captureTrace=true;bool matrix=true;
    for(u32 prim:{GS_TRIANGLESTRIP,GS_TRIANGLELIST})for(bool fog:{false,true})for(bool reglist:{false,true})for(bool offset:{false,true})
    for(u32 shape=0;shape<4;++shape)for(u32 count:{0u,1u,2u,3u,4u,17u,257u})for(bool forced:{false,true})
    {
        const auto packet=Singles(count,fog,reglist,shape,!forced,forced,offset?32768:0);std::vector<u8> expected;
        for(bool candidate:{false,true})
        {Prepare(*state,prim,true,offset);EmuCoreXOracleSetGSNativeMirror(candidate,true,0);state->Submit(packet,reglist?0:1);const auto actual=Capture(*state);if(!candidate)expected=actual;else matrix&=actual==expected;}
    }
    Check(matrix,"native single packets preserve exact complete state Q UV fog ADC FMM and buffer growth across raw domains");
    std::vector<std::vector<u8>> expectedStages,actualStages;OwnershipScript(*state,false,expectedStages);OwnershipScript(*state,true,actualStages);
    Check(expectedStages==actualStages,"native mirror mixed single/batch/carry/scissor/context/offset/fan ownership preserves every intermediate state");
    bool offsetExact=true;std::vector<u8> expected;
    for(bool candidate:{false,true})
    {
        Prepare(*state,GS_TRIANGLESTRIP);EmuCoreXOracleSetGSNativeMirror(candidate,true,0);state->Submit(Singles(2),0);state->Submit(Offset(32768,32768),0);
        auto last=Singles(1,false,false,0,false,false,32768);state->Submit(last,0);const auto actual=Capture(*state);if(!candidate)expected=actual;else offsetExact&=actual==expected;
    }
    Check(offsetExact,"native mirror keeps previous window coordinates under a changed XYOFFSET instead of reprojecting old raw vertices");
    bool flushExact=true;const auto large=Singles(22000);
    for(bool candidate:{false,true})
    {
        Prepare(*state,GS_TRIANGLESTRIP);EmuCoreXOracleSetGSNativeMirror(candidate,true,0);for(u32 i=0;i<3;++i)state->Submit(large,0);
        flushExact&=state->vertexFlushes>0;const auto actual=Capture(*state);if(!candidate)expected=actual;else flushExact&=actual==expected;
    }
    Check(flushExact,"native mirror preserves real vertex-count Flush retained strip current vertex and complete draw trace");
    bool fallback=true;
    for(u32 variant=0;variant<7;++variant)
    {
        const u32 prim=variant==4?GS_TRIANGLEFAN:variant==5?GS_SPRITE:variant==6?GS_POINTLIST:GS_TRIANGLESTRIP;
        for(bool candidate:{false,true})
        {
            Prepare(*state,prim,variant!=0,false,variant==1,variant==2);if(variant==3)state->SetRawXYOffsetForStrip(0x0001800000018000ull);
            EmuCoreXOracleSetGSNativeMirror(candidate,true,0);state->Submit(Singles(17),0);const auto actual=Capture(*state);
            if(!candidate)expected=actual;else{fallback&=actual==expected;u64 stats[8]{};EmuCoreXOracleGSNativeMirrorStats(stats);fallback&=stats[0]==0;}
        }
    }
    for(bool candidate:{false,true})
    {
        Prepare(*state,GS_TRIANGLESTRIP);state->SetRawXYOffsetForStrip(0x0001800000018000ull);
        EmuCoreXOracleSetGSNativeMirror(candidate,true,0);state->Submit(Singles(2),0);
        state->Submit(Offset(0,0),0);state->Submit(Singles(1),0);const auto actual=Capture(*state);
        if(!candidate)expected=actual;else{fallback&=actual==expected;u64 stats[8]{};EmuCoreXOracleGSNativeMirrorStats(stats);fallback&=stats[0]==0;}
    }
    Check(fallback,"native mirror unsupported scale AA1 invalid clip reserved offset fan sprite point paths stay exact legacy with zero fast work");
    std::vector<u8> oldSave,newSave,oldAfter,newAfter;
    Check(Save(*state,false,oldSave,oldAfter)&&Save(*state,true,newSave,newAfter)&&oldSave==newSave&&oldAfter==newAfter,
        "native mirror actual guarded Freeze size and bytes exclude derived cache and mid-tag Defrost continuation stays exact");
    u64 oldStats[8]{},newStats[8]{};
    for(bool candidate:{false,true})
    {
        Prepare(*state,GS_TRIANGLESTRIP);EmuCoreXOracleSetGSNativeMirror(candidate,true,0);state->Submit(Singles(257,false,false,2),0);
        EmuCoreXOracleGSNativeMirrorStats(candidate?newStats:oldStats);
    }
    Check(newStats[0]>0&&newStats[1]>0,"native single XYZ handler actually executes persistent mirror attempts and rejects");
    Check(oldStats[2]>0&&newStats[2]<oldStats[2],"native single persistent mirror eliminates actual repeated legacy vector rejection calls");
    if(newStats[0]>0)
    {
        const auto scissor=ScissorMutation(*state,0);const auto stale=ScissorMutation(*state,1);
        Check(scissor!=stale,"native mirror oracle detects omitted cull-key refresh with a pending two-vertex window");
        u64 published=0,omitted=0;const auto mixed=BatchMutation(*state,0,published);const auto staleMixed=BatchMutation(*state,2,omitted);
        Check(published>0&&omitted==0&&mixed!=staleMixed,"native mirror oracle detects omitted actual mixed-batch last4 metadata publication");
    }
    std::printf("GS_NATIVE_MIRROR fast=%llu reject=%llu legacy=%llu/%llu build=%llu refresh=%llu batch=%llu invalidation=%llu fallback=%llu\n",
        static_cast<unsigned long long>(newStats[0]),static_cast<unsigned long long>(newStats[1]),static_cast<unsigned long long>(newStats[2]),static_cast<unsigned long long>(oldStats[2]),
        static_cast<unsigned long long>(newStats[3]),static_cast<unsigned long long>(newStats[4]),static_cast<unsigned long long>(newStats[5]),static_cast<unsigned long long>(newStats[6]),static_cast<unsigned long long>(newStats[7]));
}
