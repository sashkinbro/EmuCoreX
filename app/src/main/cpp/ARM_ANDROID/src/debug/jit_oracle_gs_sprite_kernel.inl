// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unreferenced native SPRITE RED fixture. Include after local-FMM/list helpers.
#pragma once
namespace GSSpriteKernelOracle
{
    // Legal pointer-to-member exposure of protected GSState data in test-only
    // code: no cast to a fictitious dynamic type and no production API change.
    class Access:public GSState
    {
    public:
        static auto& Vertices(GSState& s){return s.*(&Access::m_vertex);}
        static auto& Indices(GSState& s){return s.*(&Access::m_index);}
        static auto& Current(GSState& s){return s.*(&Access::m_v);}
        static auto& Latch(GSState& s){return s.*(&Access::m_q);}
        static auto& Cache(GSState& s){return s.*(&Access::m_fused_trace);}
        static void Seed(GSState& s)
        {
            auto& v=Current(s);v.ST.S=1.25f;v.ST.T=-2.5f;v.RGBAQ.U32[0]=0x44332211;v.RGBAQ.Q=3.f;
            v.UV=0x81238023;v.FOG=0x89abcdef;Latch(s)=2.f;
        }
    };
    void Configure(u32 mode)
    {
        EmuCoreXOracleSetGSPackedBatch(mode!=0,false);EmuCoreXOracleSetGSCarryingLayouts(mode!=0,true);
        EmuCoreXOracleSetGSStripKernel(mode==2,true,0);EmuCoreXOracleSetGSStripOutline(true,true);
        EmuCoreXOracleSetGSNativeMirror(true,false,0);EmuCoreXOracleSetGSLocalFmm(true,false);
    }
    std::vector<u8> Registers(u32 family,u32 padding)
    {
        if(family<2)return {GIF_REG_STQ,GIF_REG_RGBA,static_cast<u8>(family==0?GIF_REG_XYZF2:GIF_REG_XYZ2)};
        return GSCarryingLayoutOracle::Descriptors(family==2?1:2,padding);
    }
    std::vector<GIFPackedReg> Packet(u32 family,u32 loops,u32 shape=0,u32 base=0,u32 padding=0)
    {
        const auto regs=Registers(family,padding);
        std::vector<GIFPackedReg> out(1+loops*regs.size());out[0]=GSCarryingLayoutOracle::Tag(regs,loops);
        constexpr u32 qs[]={0,0x80000000u,0x7fc12345u,0x7f800000u,0xff800000u,0xbf800000u,1,0x3f800000u};
        u32 vertex=0;
        for(u32 n=0;n<loops;++n)for(u32 at=0;at<regs.size();++at)
        {
            auto& r=out[1+n*regs.size()+at];for(u32 lane=0;lane<4;++lane)r.U32[lane]=0xf1e2d3c4u^(vertex*113+lane*71+at*19);
            if(regs[at]==GIF_REG_STQ){r.U32[0]=0x3f000000|(vertex*71&0x7fffff);r.U32[1]=0x3f400000|(vertex*97&0x3fffff);r.U32[2]=qs[vertex%8];}
            if(regs[at]==GIF_REG_RGBA)for(u32 lane=0;lane<4;++lane)r.U32[lane]=0xcafe0000u|((vertex*47+lane*31)&255);
            if(regs[at]==GIF_REG_UV){r.U32[0]=0xfeedf123u+vertex*13;r.U32[1]=0xabcde456u+vertex*17;}
            if(regs[at]==GIF_REG_XYZ2||regs[at]==GIF_REG_XYZF2)
            {
                u32 xy=(vertex&1)?400:100;if(shape==3)xy+=20000;xy=(xy+base)&65535;
                if(family==0){r.XYZF2.X=xy;r.XYZF2.Y=xy;r.XYZF2.Z=0x123456+vertex;r.XYZF2.F=vertex*7;}
                else{r.XYZ2.X=xy;r.XYZ2.Y=xy;r.XYZ2.Z=0x12345678+vertex;}
                r.U32[3]=(r.U32[3]&~0x8000u)|((shape==1||(shape==2&&vertex%5<2))?0x8000u:0u);++vertex;
            }
        }
        return out;
    }
    void Submit(GSPackedBatchOracle& s,const std::vector<GIFPackedReg>& packet,u32 path=3)
    {
        GSMirrorQuadOracle::GuardedInput guarded(packet);
        if(!guarded.packet){Check(false,"sprite protected packet allocation succeeds");return;}
        if(path==2)s.Transfer<2>(reinterpret_cast<const u8*>(guarded.packet),packet.size());
        else s.Transfer<3>(reinterpret_cast<const u8*>(guarded.packet),packet.size());
    }
    std::vector<GIFPackedReg> Modes(u32 variant)
    {
        std::vector<GIFPackedReg> out(4);out[0]=GSCarryingLayoutOracle::Tag({GIF_REG_A_D},3);
        out[1].A_D.ADDR=GIF_A_D_REG_PRMODECONT;out[1].r.PRMODECONT.AC=variant<2;
        out[2].A_D.ADDR=GIF_A_D_REG_PRIM;out[2].r.PRIM.PRIM=GS_SPRITE;out[2].r.PRIM.FST=variant&1;
        out[3].A_D.ADDR=GIF_A_D_REG_PRMODE;out[3].r.PRMODE.FST=!(variant&1);
        return out;
    }
    u32 Normalized(u32 q){return q==0?0x00800000u:(q&0x7fffffffu)>0x7f800000u?0x7f7fffffu:q;}
    bool Literal()
    {
        bool exact=true;u32 diagnostics=0;
        const auto mismatch=[&](u32 family,u32 mode,u32 depth,const char* field,u32 index,u32 got,u32 want){
            if(diagnostics++<40)std::printf("GS_SPRITE_LITERAL family=%u mode=%u depth=%u field=%s index=%u got=%08x expected=%08x\n",family,mode,depth,field,index,got,want);
        };
        for(u32 family=0;family<4;++family)for(u32 mode=0;mode<4;++mode)for(u32 depth=0;depth<3;++depth)
        {
            GSConfig.UserHacks_Limit24BitDepth=static_cast<GSLimit24BitDepth>(depth);Configure(2);
            auto s=std::make_unique<GSVertexPacketOracle>();s->Reset(true);s->Prepare(GS_SPRITE,true);Access::Seed(*s);
            const auto modes=Modes(mode);s->Transfer<3>(reinterpret_cast<const u8*>(modes.data()),modes.size());
            const bool fst=mode<2?bool(mode&1):!bool(mode&1);exact&=s->m_env.PRIM.FST==fst;
            if(s->m_env.PRIM.FST!=fst)mismatch(family,mode,depth,"FST",0,s->m_env.PRIM.FST,fst);
            const auto packet=Packet(family,8);GSMirrorQuadOracle::GuardedInput guarded(packet);if(!guarded.packet){exact=false;continue;}
            s->Transfer<3>(reinterpret_cast<const u8*>(guarded.packet),packet.size());
            const auto& v=Access::Vertices(*s);const auto& ix=Access::Indices(*s);
            exact&=v.head==8&&v.tail==8&&v.next==8&&v.xy_tail==8&&ix.tail==8;
            if(v.head!=8||v.tail!=8||v.next!=8||v.xy_tail!=8||ix.tail!=8){
                mismatch(family,mode,depth,"head",0,v.head,8);mismatch(family,mode,depth,"tail",0,v.tail,8);
                mismatch(family,mode,depth,"next",0,v.next,8);mismatch(family,mode,depth,"xy_tail",0,v.xy_tail,8);mismatch(family,mode,depth,"index_tail",0,ix.tail,8);
            }
            const u32 stride=family<2?3:2;const bool fog=family==0;
            std::array<u32,8> last{};
            for(u32 i=0;i<8;++i)
            {
                const auto* r=packet.data()+1+i*stride;const auto& xyz=r[stride-1];
                u32 rgba=0x44332211;if(family<2){rgba=0;for(u32 lane=0;lane<4;++lane)rgba|=(r[1].U32[lane]&255)<<(lane*8);}
                u32 q=family<2?(r[0].U32[2]?r[0].U32[2]:0x00800000u):0x40400000u;
                if(!fst&&!(i&1)&&family<2){const u32 second=r[stride].U32[2];q=second?second:0x00800000u;}
                u32 z=fog?(xyz.U32[2]>>4)&0xffffffu:xyz.U32[2];
                if(depth==1)z=((z>>8)&~255u)|(z&255u);if(depth==2)z&=0xffffffu;
                const u32 uv=family==3?(r[0].U32[0]&0x3fff)|((r[0].U32[1]&0x3fff)<<16):0x81238023u;
                const std::array<u32,8> raw{family==3?0x3fa00000u:r[0].U32[0],family==3?0xc0200000u:r[0].U32[1],rgba,q,
                    (xyz.U32[0]&65535u)|((xyz.U32[1]&65535u)<<16),z,uv,fog?(xyz.U32[3]>>4)&255u:0x89abcdefu};
                if(v.tail==8){
                    exact&=std::memcmp(&v.buff[i],raw.data(),32)==0;
                    std::array<u32,8> actual;std::memcpy(actual.data(),&v.buff[i],32);
                    for(u32 lane=0;lane<8;++lane)if(actual[lane]!=raw[lane])mismatch(family,mode,depth,"vertexWord",i*8+lane,actual[lane],raw[lane]);
                }
                exact&=ix.tail==8&&ix.buff[i]==i;if(ix.tail==8&&ix.buff[i]!=i)mismatch(family,mode,depth,"index",i,ix.buff[i],i);last=raw;
            }
            exact&=std::memcmp(&Access::Current(*s),last.data(),32)==0;
            std::array<u32,8> actualCurrent;std::memcpy(actualCurrent.data(),&Access::Current(*s),32);
            for(u32 lane=0;lane<8;++lane)if(actualCurrent[lane]!=last[lane])mismatch(family,mode,depth,"currentWord",lane,actualCurrent[lane],last[lane]);
            // Every nonempty new GIF tag resets the temporary latch to1 in
            // Transfer before dispatch. UVPair carries current vertexQ3 but
            // writes no STQ, so its latch retains the tag's1, not Seed's2.
            const u32 expectedLatch=family==3?0x3f800000u:family==2?Normalized(packet[1+7*2].U32[2]):packet[1+7*3].U32[2];
            u32 latch;std::memcpy(&latch,&Access::Latch(*s),4);exact&=latch==expectedLatch;
            if(latch!=expectedLatch)mismatch(family,mode,depth,"latch",0,latch,expectedLatch);
            const auto probe=GSCarryingLayoutOracle::LatchProbe();s->Transfer<3>(reinterpret_cast<const u8*>(probe.data()),probe.size());
            const u32 probeQ=GSVector4i(Access::Current(*s).m[0]).U32[3];exact&=probeQ==expectedLatch;
            if(probeQ!=expectedLatch)mismatch(family,mode,depth,"rgbaProbeQ",0,probeQ,expectedLatch);
        }
        return exact;
    }
    std::vector<u8> CacheBytes(GSState& s)
    {
        const auto& c=Access::Cache(s);const auto* raw=reinterpret_cast<const u8*>(&c.acc);std::vector<u8> out(raw,raw+sizeof(c.acc));
        GSPackedBatchOracle::Append(out,c.watermark);GSPackedBatchOracle::Append(out,c.indices);GSPackedBatchOracle::Append(out,c.vertexEnd);
        GSPackedBatchOracle::Append(out,c.key);GSPackedBatchOracle::Append(out,c.valid);return out;
    }
    bool FmmInvalidation()
    {
        bool exact=true;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
        for(u32 family=0;family<4;++family)for(u32 shape=0;shape<4;++shape)for(u32 mode=0;mode<3;++mode)
        {
            auto s=std::make_unique<GSPackedBatchOracle>();GSNativeMirrorOracle::Prepare(*s,GS_SPRITE);Configure(mode);
            auto& c=Access::Cache(*s);std::memset(&c.acc,0xa5,sizeof(c.acc));c.watermark=111;c.indices=123;c.vertexEnd=222;c.key=7;c.valid=true;
            auto expected=CacheBytes(*s);const bool accepted=shape==0||shape==2;if(accepted)expected.back()=0;
            Submit(*s,Packet(family,8,shape));exact&=CacheBytes(*s)==expected;
            exact&=Access::Indices(*s).tail==(shape==0?8u:shape==2?4u:0u);
        }
        return exact;
    }
    s32 Floor16(s32 x){return x>=0?x/16:-static_cast<s32>((static_cast<u32>(-x)+15)/16);}
    bool NativeRectangleModel()
    {
        bool exact=true;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
        constexpr u16 edges[]={0,1,15,16,17,31,32,32767,32768,65534,65535};
        auto s=std::make_unique<GSPackedBatchOracle>();
        for(u16 x:edges)for(u16 y:edges)for(s32 dx:{0,1,15,16,17,-1})for(u32 base:{0u,32768u})
        {
            GSNativeMirrorOracle::Prepare(*s,GS_SPRITE,true,base!=0);Configure(2);auto packet=Packet(1,6);
            const u16 x1=static_cast<u16>(x+dx),y1=static_cast<u16>(y+17);
            for(u32 i=0;i<6;++i){auto& xyz=packet[1+i*3+2];xyz.XYZ2.X=(i&1)?x1:x;xyz.XYZ2.Y=(i&1)?y1:y;}
            const s32 a=s32(x)-base,b=s32(x1)-base,c=s32(y)-base,d=s32(y1)-base;
            const s32 l=Floor16(std::min(a,b)+15)*16,t=Floor16(std::min(c,d)+15)*16;
            const s32 r=Floor16(std::max(a,b)-1)*16+1,bot=Floor16(std::max(c,d)-1)*16+1;
            const auto clip=s->m_context->scissor.cull;
            const bool visible=r>l&&bot>t&&std::min(r+1,clip.z)>std::max(l,clip.x)&&std::min(bot+1,clip.w)>std::max(t,clip.y);
            Submit(*s,packet);exact&=Access::Indices(*s).tail==(visible?6u:0u);
        }
        return exact;
    }
    struct Work{u64 chunks=0,vertices=0,outlined=0,outlinedVertices=0,inlineChunks=0;};
    bool Matrix(std::array<Work,4>& work,u64& cases)
    {
        bool exact=true;auto s=std::make_unique<GSPackedBatchOracle>();
        for(u32 family=0;family<4;++family)for(u32 padding=0;padding<(family<2?1u:5u);++padding)
        for(u32 count:{0u,1u,2u,3u,4u,5u,6u,7u,8u,127u,128u,129u,257u})for(u32 pending:{0u,1u})
        for(u32 depth=0;depth<3;++depth)for(u32 shape:{0u,1u,2u,3u})for(u32 base:{0u,32768u})for(bool fst:{false,true})
        {
            GSConfig.UserHacks_Limit24BitDepth=static_cast<GSLimit24BitDepth>(depth);
            const auto packet=Packet(family,count,shape,base,padding);const auto prefix=GSNativeMirrorOracle::Singles(pending,false,false,0,false,false,base);
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                GSNativeMirrorOracle::Prepare(*s,GS_SPRITE,true,base!=0);s->SetTraceFlags(true,fst,true,false);Configure(mode);
                s->Submit(prefix,0);Submit(*s,packet,(padding&1)?2:3);const auto actual=GSNativeMirrorOracle::Capture(*s);
                if(!mode)expected=actual;else exact&=actual==expected;
                if(mode==2){u64 k[4]{},o[3]{};EmuCoreXOracleGSStripKernelStats(k);EmuCoreXOracleGSStripOutlineStats(o);
                    auto& w=work[family];w.chunks+=k[0];w.vertices+=k[1];w.outlined+=o[0];w.inlineChunks+=o[1];w.outlinedVertices+=o[2];}
            }
            ++cases;
        }
        return exact;
    }
    bool Flush()
    {
        bool exact=true;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
        for(u32 family=0;family<4;++family)
        {
            auto s=std::make_unique<GSPackedBatchOracle>();const auto packet=Packet(family,18000,0,0,family<2?0:1);std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                GSNativeMirrorOracle::Prepare(*s,GS_SPRITE);Configure(mode);s->Submit(packet,0);s->Submit(packet,0);
                const auto actual=GSNativeMirrorOracle::Capture(*s);exact&=s->vertexFlushes>0;
                if(!mode)expected=actual;else exact&=actual==expected;
            }
        }
        return exact;
    }
    bool SaveAndMixed()
    {
        bool exact=true;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
        for(u32 family=0;family<4;++family)for(u32 save:{0u,1u,2u})
        {
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                auto s=std::make_unique<GSPackedBatchOracle>();GSNativeMirrorOracle::Prepare(*s,GS_SPRITE);Configure(mode);
                const auto packet=Packet(family,129,0,0,family<2?0:1);const u32 cut=save==2?1+(family<2?3u:3u):2;
                std::vector<GIFPackedReg> before(packet.begin(),packet.begin()+cut),after(packet.begin()+cut,packet.end());
                s->Submit(before,0);
                if(save&& !GSCarryingLayoutOracle::SaveRoundtrip(*s,save==1))exact=false;
                s->Submit(after,0);auto out=GSNativeMirrorOracle::Capture(*s);
                // Real mixed tag class/offset transitions remain observable.
                std::vector<GIFPackedReg> prim(2);prim[0]=GSCarryingLayoutOracle::Tag({GIF_REG_A_D},1);prim[1].A_D.ADDR=GIF_A_D_REG_PRIM;prim[1].r.PRIM.PRIM=GS_TRIANGLESTRIP;
                s->Submit(prim,0);s->Submit(GSListKernelOracle::Packet(17,true),0);
                prim[1].r.PRIM.PRIM=GS_SPRITE;s->Submit(prim,0);s->Submit(GSNativeMirrorOracle::Offset(32768,32768),0);
                s->Submit(Packet(family,129,0,32768,family<2?0:1),0);const auto end=GSNativeMirrorOracle::Capture(*s);out.insert(out.end(),end.begin(),end.end());
                if(!mode)expected=out;else exact&=out==expected;
            }
        }
        return exact;
    }
    bool DetectMutation(u32 mutation)
    {
        std::vector<u8> control;
        for(u32 value:{0u,mutation})
        {
            auto s=std::make_unique<GSPackedBatchOracle>();GSNativeMirrorOracle::Prepare(*s,GS_SPRITE);Configure(2);
            EmuCoreXOracleSetGSStripKernel(true,true,value);
            const auto first=Packet(1,mutation==1?7u:8u,mutation==3?1u:0u);
            s->Submit(first,0);auto actual=GSNativeMirrorOracle::Capture(*s);
            if(mutation==1)
            {
                // Both tags enter the kernel without a scalar tail repairing
                // a missing XY-ring write. Seven leaves a real pending corner.
                s->Submit(Packet(1,8),0);const auto after=GSNativeMirrorOracle::Capture(*s);
                actual.insert(actual.end(),after.begin(),after.end());
            }
            u64 work[4]{};EmuCoreXOracleGSStripKernelStats(work);
            if(!work[0]||!work[1]||(mutation==1&&(work[0]!=2||work[1]!=15)))return false;
            if(!value)control=actual;else return actual!=control;
        }
        return false;
    }
    bool Fallback()
    {
        bool exact=true;
        for(u32 family=0;family<4;++family)for(u32 variant=0;variant<5;++variant)
        {
            std::vector<u8> expected;
            for(u32 mode=0;mode<3;++mode)
            {
                GSConfig.UserHacks_AutoFlush=variant==4?GSHWAutoFlushLevel::Enabled:GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=variant==4;
                auto s=std::make_unique<GSPackedBatchOracle>();GSNativeMirrorOracle::Prepare(*s,GS_SPRITE,variant!=0,false,variant==1,variant==2);Configure(mode);
                EmuCoreXOracleSetGSPackedBatch(mode!=0,true);
                if(variant==3)s->SetRawXYOffsetForStrip(0x0001800000018000ull);
                Submit(*s,Packet(family,129));const auto actual=GSNativeMirrorOracle::Capture(*s);
                if(!mode)expected=actual;else exact&=actual==expected;
                if(mode==2)
                {
                    u64 k[4]{},packed[3]{};EmuCoreXOracleGSStripKernelStats(k);EmuCoreXOracleGSPackedBatchStats(packed);exact&=k[0]==0;
                    if(variant==4)exact&=family<2?packed[1]==0:EmuCoreXOracleGSCarryingStagedVertices()>0;
                }
            }
        }
        GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;return exact;
    }
}
void GSSpriteKernelTests()
{
    using namespace GSSpriteKernelOracle;const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;EmuCoreXOracleSetGSPackedBatch(true,false);EmuCoreXOracleSetGSCarryingLayouts(true,false);
        EmuCoreXOracleSetGSStripKernel(true,false,0);EmuCoreXOracleSetGSStripOutline(true,false);EmuCoreXOracleSetGSNativeMirror(true,false,0);EmuCoreXOracleSetGSLocalFmm(true,false);});
    GSConfig.UserHacks_AutoFlush=GSHWAutoFlushLevel::Disabled;GSConfig.AutoFlushSW=false;GSConfig.UserHacks_ForceEvenSpritePosition=false;
    GSConfig.UserHacks_ReadTCOnClose=false;GSConfig.UserHacks_Limit24BitDepth=GSLimit24BitDepth::Disabled;
    Check(Literal(),"native sprite four-layout literal bytes Q propagation actual PRMODE writes owned latch depth fog UV and pair indices match");
    Check(FmmInvalidation(),"native sprite only accepted pairs invalidate FMM and preserve all accumulator and scalar bytes");
    Check(NativeRectangleModel(),"native sprite exact two-point integer rectangle and exclusive clipping model covers dense signed native edges");
    std::array<Work,4> work{};u64 cases=0;Check(Matrix(work,cases),"native sprite protected four-family padding repeated-pair pending depth Q ADC raw-offset complete states match staged/direct controls");
    Check(Flush(),"native sprite actual32765vertex limit Flush growth current Q and retained odd window match staged/direct draws");
    Check(SaveAndMixed(),"native sprite descriptor Defrost odd-pending Freeze write mixed tag class and offset continuation preserve complete state");
    Check(Fallback(),"native sprite unsupported grid AA1 clip offsets retain exact fallback and autoflush stays actually staged");
    bool all=true,one=true;
    for(u32 family=0;family<4;++family)
    {
        const auto& w=work[family];all&=w.chunks>0&&w.vertices>0;one&=w.chunks>0&&w.outlined==w.chunks&&w.outlinedVertices==w.vertices&&w.inlineChunks==0;
        std::printf("GS_SPRITE_KERNEL family=%u chunks=%llu vertices=%llu outlined=%llu inline=%llu outlined_vertices=%llu\n",family,
            static_cast<unsigned long long>(w.chunks),static_cast<unsigned long long>(w.vertices),static_cast<unsigned long long>(w.outlined),
            static_cast<unsigned long long>(w.inlineChunks),static_cast<unsigned long long>(w.outlinedVertices));
    }
    Check(all,"all four real contiguous and carrying-pair sprite handlers execute actual two-pass kernel work");
    Check(one,"each eligible sprite chunk executes exactly one actual outlined entry with no duplicate inline work");
    if(all)
    {
        Check(DetectMutation(1),"native sprite oracle detects missing actual XY ring publication across an odd pending pair");
        Check(DetectMutation(3),"native sprite oracle detects wrong strip head advance instead of rejected pair rewind");
        Check(DetectMutation(4),"native sprite oracle detects omitted accepted-pair second-corner Q propagation");
    }
    std::printf("GS_SPRITE_MATRIX cases=%llu\n",static_cast<unsigned long long>(cases));
}
