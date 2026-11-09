// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "GS/Renderers/HW/GSTextureReplacements.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/SettingsWrapper.h"
#include <cstdio>
#include <vector>
#if __has_include("GS/Renderers/HW/GSTextureUpscaleSupport.h")
#include "GS/Renderers/HW/GSTextureUpscaleSupport.h"
#define EMUCOREX_HAS_UPSCALE_SUPPORT 1
#endif
#if __has_include("GS/Renderers/Common/GSReplacementAlphaSnap.h")
#include "GS/Renderers/Common/GSReplacementAlphaSnap.h"
#define EMUCOREX_HAS_REPLACEMENT_ALPHA_SNAP 1
#endif
extern "C" void EmuCoreXOracleTestReplacementWorker(void (*check)(bool,const char*));

static void TestTextureReplacementParity(CoherenceCache& cache,CoherenceDevice& device,GSRendererHW& renderer,
    void (*check)(bool,const char*))
{
    EmuCoreXOracleTestReplacementWorker(check);
    const auto ktx2=GSTextureReplacements::GetLoader("fixture.ktx2");
    const auto astc=GSTextureReplacements::GetLoader("fixture.astc");
    const auto ktx=GSTextureReplacements::GetLoader("fixture.ktx");
    check(ktx2!=nullptr,"GS existing Android KTX2 replacement loader remains registered");
    check(astc!=nullptr,"GS actual raw ASTC replacement loader is registered");
    check(ktx!=nullptr,"GS actual strict KTX1 replacement loader is registered");
    const bool saved_astc=device.Features().astc_textures;
    device.SetASTCCap(true);
    const auto load=[&](GSTextureReplacements::ReplacementTextureLoader loader,const std::vector<u8>& bytes,
        GSTextureReplacements::ReplacementTexture& out,bool only_base=true) {
        if(!loader)return false;
        auto* file=std::tmpfile();
        if(!file)return false;
        const ScopedGuard close([&](){std::fclose(file);});
        if(std::fwrite(bytes.data(),1,bytes.size(),file)!=bytes.size() || std::fflush(file))return false;
        const std::string path="/proc/self/fd/"+std::to_string(fileno(file));
        return loader(path,&out,only_base);
    };
    if(astc)
    {
        // Literal 13x11 image, 6x5 blocks: ceil(13/6)*ceil(11/5)*16 = 144 bytes.
        std::vector<u8> bytes(16+144,0x57);
        const u8 header[]={0x13,0xab,0xa1,0x5c,6,5,1,13,0,0,11,0,0,1,0,0};
        std::memcpy(bytes.data(),header,sizeof(header));
        GSTextureReplacements::ReplacementTexture out{};
        check(load(astc,bytes,out) && out.width==13 && out.height==11 && out.pitch==48 &&
            out.format==GSTexture::Format::ASTC6x5 && out.data==std::vector<u8>(144,0x57) && out.mips.empty(),
            "GS actual raw ASTC loader preserves literal non-square block geometry and payload");
        bytes.push_back(0);
        check(!load(astc,bytes,out),"GS actual raw ASTC loader rejects trailing payload bytes");
        bytes.pop_back();bytes.pop_back();
        check(!load(astc,bytes,out),"GS actual raw ASTC loader rejects truncated block data");
    }
    if(ktx)
    {
        std::vector<u8> bytes(64,0);
        const u8 magic[]={0xab,'K','T','X',' ', '1','1',0xbb,13,10,26,10};
        std::memcpy(bytes.data(),magic,sizeof(magic));
        const auto put=[&](size_t at,u32 word) {for(u32 i=0;i<4;++i)bytes[at+i]=static_cast<u8>(word>>(8*i));};
        put(12,0x04030201);put(20,1);put(28,0x93b3);put(32,0x1908);
        put(36,65);put(40,33);put(52,1);put(56,7);
        const char key[]="KTXorientation",value[]="S=r,T=d";
        const u32 entry=sizeof(key)+sizeof(value);
        const u32 kv=4+((entry+3)&~3u);put(60,kv);bytes.resize(64+kv,0);put(64,entry);
        std::memcpy(bytes.data()+68,key,sizeof(key));std::memcpy(bytes.data()+68+sizeof(key),value,sizeof(value));
        std::vector<size_t> sizes;
        for(u32 level=0;level<7;++level)
        {
            const u32 w=std::max(1u,65u>>level),h=std::max(1u,33u>>level);
            const u32 n=((w+5)/6)*((h+4)/5)*16;
            const size_t at=bytes.size();sizes.push_back(at);bytes.resize(at+4+n,static_cast<u8>(level+1));put(at,n);
        }
        GSTextureReplacements::ReplacementTexture out{};
        const bool loaded=load(ktx,bytes,out,true);
        bool exact=loaded&&out.width==65&&out.height==33&&out.pitch==176&&out.mips.size()==6;
        if(exact)for(u32 level=1;level<7;++level)
        {
            const auto& mip=out.mips[level-1];const u32 w=std::max(1u,65u>>level),h=std::max(1u,33u>>level);
            exact&=mip.width==w&&mip.height==h&&mip.pitch==((w+5)/6)*16&&
                mip.data==std::vector<u8>(((w+5)/6)*((h+4)/5)*16,static_cast<u8>(level+1));
        }
        check(exact,"GS actual KTX1 loader retains all seven explicit odd-sized ASTC mips even for base-only callers");
        GSTextureReplacements::ReplacementTexture untouched{};untouched.width=123;untouched.data={4,5,6};
        bytes[sizes.back()]=17;
        check(!load(ktx,bytes,untouched)&&untouched.width==123&&untouched.data==std::vector<u8>({4,5,6}),
            "GS actual KTX1 loader rejects a corrupt later mip without publishing a partial texture");
    }
    device.SetASTCCap(saved_astc);
    [&]<class Cache>(Cache& c) {
        GSTextureCache::HashCacheKey key{};
        GIFRegTEX0 tex{};tex.TBP0=0x2000;tex.TBW=1;tex.TW=tex.TH=4;tex.PSM=PSMCT32;
        key.TEX0=tex;key.TEX0Hash=0x45f0a91;
        if constexpr(requires {c.InjectHashCacheTexture(key,static_cast<GSTexture*>(nullptr),std::pair<u8,u8>{0,255},true);
            c.GetNativeTexture(static_cast<const GSTextureCache::Source*>(nullptr));})
        {
            c.RemoveAll(true,true,true);
            for(u32 y=0;y<16;++y)for(u32 x=0;x<16;++x)
                renderer.m_mem.WritePixel32(x,y,0x80000000u|(y<<8)|x,tex.TBP0,tex.TBW);
            auto* upscale=g_gs_device->CreateTexture(32,32,1,GSTexture::Format::Color);
            c.InjectHashCacheTexture(key,upscale,{0,128},true);
            auto* e=c.FixtureHashEntry(key);
            check(e&&e->generated&&e->is_replacement&&!e->native,"GS actual generated injection records owned upscale identity without eager native allocation");
            GSTextureCache::Source source(tex,{});source.m_from_hash_cache=e;
            auto* native=static_cast<CoherenceTexture*>(c.GetNativeTexture(&source));
            bool pixels=native&&native->GetWidth()==16&&native->GetHeight()==16;
            if(pixels)for(u32 y=0;y<16;++y)for(u32 x=0;x<16;++x)pixels&=native->pixels[y*16+x]==(0x80000000u|(y<<8)|x);
            check(pixels&&c.GetNativeTexture(&source)==native,"GS actual generated native fallback materializes literal VRAM once and reuses its owned texture");
            check(c.GetHashCacheMemoryUsage()==1024&&c.GetHashCacheReplacementMemoryUsage()==4096,
                "GS actual generated cache counts native and upscaled allocations in their exact ownership pools");
            auto* pack=g_gs_device->CreateTexture(32,32,1,GSTexture::Format::Color);
            c.InjectHashCacheTexture(key,pack,{0,128},false);
            check(!e->generated&&c.GetNativeTexture(&source)==nullptr,"GS actual pack injection wins over generated texture and disables native substitution");
            source.m_from_hash_cache=nullptr;
            c.RemoveAll(true,true,true);
            check(c.GetTotalHashCacheMemoryUsage()==0,"GS actual generated/native ownership releases both memory pools on cache teardown");
        }
        else check(false,"GS generated upscale injection and lazy native ownership consumer contract is present");
    }(cache);
    [&]<class Options>(Options options) {
        if constexpr(requires {options.TextureUpscale;})
        {
            using Mode=decltype(options.TextureUpscale);
            bool modes=true;
            for(int value:{0,1,2,3,256,257,-1})
            {
                MemorySettingsInterface si;si.SetIntValue("EmuCore/GS","TextureUpscale",value);
                SettingsLoadWrapper load(si);Options current;current.LoadSave(load);
                modes&=static_cast<int>(current.TextureUpscale)==(value>=0&&value<=2?value:0);
            }
            Options other=options;other.TextureUpscale=static_cast<Mode>(1);
            check(modes&&!options.OptionsAreEqual(other),"GS actual upscale setting accepts only 0/1/2 without narrow-cast wrap and participates in renderer reconfiguration");
        }
        else check(false,"GS typed texture upscale setting is connected to actual GSOptions load and equality");
    }(Pcsx2Config::GSOptions());
#ifdef EMUCOREX_HAS_UPSCALE_SUPPORT
    using namespace GSTextureUpscaleSupport;
    BoundedLifoQueue<int> q(2,10);std::vector<int> dropped;
    q.Push(1,4,&dropped);q.Push(2,4,&dropped);q.Push(3,4,&dropped);
    check(dropped==std::vector<int>({1})&&q.PopNewest()==3&&q.PopNewest()==2,
        "GS actual upscale pending queue is bounded and processes newest work first");
    const u8 src[]={20,30,40,0,80,90,100,128};u8 dst[32];
    GSTextureUpscaler::BilinearRGBA8x2(src,2,1,8,dst,16);
    KeepHardAlphaEdges2x(src,2,1,8,dst,16);
    bool mask=true;for(u32 y=0;y<2;++y)for(u32 x=0;x<4;++x)
        mask&=std::memcmp(dst+y*16+x*4,src+(x/2)*4,4)==0;
    check(mask,"GS actual upscale hard alpha pass retains exact source colour and binary mask texels");
    const std::string filters_dir=Path::Combine(EmuFolders::Resources,"upscale/raisr/ps2");
    std::string filter_error;
    const auto filters=GSTextureUpscaler::FilterSet::Load(filters_dir,&filter_error);
    check(filters!=nullptr,"GS actual Android upscaler loads the shipped PS2 trained filter resources");
    if(filters)
    {
        bool exact=GSTextureUpscaler::NeonAvailable();
        for(const auto [w,h]:{std::pair<u32,u32>{1,7},{13,17},{45,150}})
        {
            const u32 sp=w*4+12,dp=w*8+20;
            std::vector<u8> source(sp*h,0xa7),scalar(dp*h*2,0xb9),neon=scalar;
            u32 random=0x834c91u+w*17+h;
            for(u32 y=0;y<h;++y)for(u32 x=0;x<w*4;++x)
            {random=random*1664525u+1013904223u;source[y*sp+x]=random>>24;}
            GSTextureUpscaler::LumaPlanes a,b;
            GSTextureUpscaler::UpscaleRGBA8x2Planes(*filters,source.data(),w,h,sp,scalar.data(),dp,GSTextureUpscaler::Impl::Scalar,&a);
            GSTextureUpscaler::UpscaleRGBA8x2Planes(*filters,source.data(),w,h,sp,neon.data(),dp,GSTextureUpscaler::Impl::Neon,&b);
            const auto equal=[](const auto& x,const auto& y) {
                return x.size()==y.size()&&std::memcmp(x.data(),y.data(),x.size()*sizeof(x[0]))==0;
            };
            exact&=scalar==neon&&equal(a.cheap,b.cheap)&&equal(a.hash,b.hash)&&equal(a.raisr,b.raisr)&&equal(a.blended,b.blended);
            for(u32 y=0;y<h*2;++y)for(u32 x=w*8;x<dp;++x)exact&=scalar[y*dp+x]==0xb9;
        }
        check(exact,"GS actual ARM NEON upscale matches scalar pixels and all four float/hash planes across odd pitches, vector tails and row bands");
    }
#else
    check(false,"GS bounded upscale support and hard-alpha consumer kernels are present");
#endif
#ifdef EMUCOREX_HAS_REPLACEMENT_ALPHA_SNAP
    using GSReplacementAlphaSnap::Wanted;
    check(Wanted(true,true,true,ATST_EQUAL,128)&&Wanted(true,true,true,ATST_GEQUAL,120)&&
        !Wanted(false,true,true,ATST_EQUAL,128)&&!Wanted(true,true,true,ATST_ALWAYS,128)&&
        !Wanted(true,true,true,ATST_EQUAL,119)&&!Wanted(true,false,true,ATST_EQUAL,128),
        "GS replacement alpha snap preserves near-opaque tests and declines ordinary or alpha-unused textures");
#else
    check(false,"GS replacement alpha snap sampler contract is present");
#endif
}
