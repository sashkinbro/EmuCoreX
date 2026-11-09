// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Actual renderer classifier/converter/mask consumer. Literal rectangles and
// channel expectations are independent of the production heuristics.
#include "GSCoherenceTestDevice.h"
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
void GSRendererHW::TestTextureShuffleParity(void (*check)(bool,const char*))
{
    cpuinfo_initialize();
    const bool isolated=!g_gs_renderer&&!g_texture_cache&&!g_gs_device;
    check(isolated,"GS texture shuffle parity owns isolated CPU renderer");if(!isolated)return;
    const auto saved=GSConfig;GSConfig=Pcsx2Config::GSOptions();
    GSConfig.AccurateBlendingUnit=AccBlendLevel::Basic;
    GSConfig.LoadTextureReplacements=GSConfig.DumpReplaceableTextures=false;
    g_gs_device=std::make_unique<CoherenceDevice>();g_gs_renderer=std::make_unique<GSRendererHW>();
    auto& r=*static_cast<GSRendererHW*>(g_gs_renderer.get());
    const ScopedGuard restore([&](){g_gs_renderer.reset();g_gs_device.reset();GSConfig=saved;});
    CoherenceTexture image(GSTexture::RenderTarget,256,128,1,GSTexture::Format::Color);
    GIFRegTEX0 target_tex{};target_tex.TBP0=0x1000;target_tex.TBW=4;target_tex.PSM=PSMCT32;
    GSTextureCache::Target target(target_tex,GSTextureCache::RenderTarget,GSVector2i(256,128),1.f,&image);
    target.m_valid=GSVector4i(0,0,256,128);target.m_32_bits_fmt=true;
    GIFRegTEX0 source_tex=target_tex;source_tex.TBP0=0x800;source_tex.TBW=2;source_tex.PSM=PSMCT16;
    GSTextureCache::Source source(source_tex,GIFRegTEXA{});
    source.m_texture=&image;source.m_shared_texture=true;source.m_32_bits_fmt=true;
    source.m_unscaled_size=GSVector2i(128,64);source.m_valid_rect=GSVector4i(0,0,128,64);
    const ScopedGuard detach([&](){target.m_texture=nullptr;source.m_texture=nullptr;});
    const auto prepare=[&](bool triangle=false,bool fst=true){
        r.ResetStates();r.m_env={};r.m_cached_ctx={};r.PRIM=&r.m_env.PRIM;r.m_draw_env=&r.m_env;r.m_context=&r.m_env.CTXT[0];
        r.m_env.PRIM.PRIM=triangle?GS_TRIANGLELIST:GS_SPRITE;r.m_env.PRIM.TME=1;r.m_env.PRIM.FST=fst;
        r.m_context->XYOFFSET.OFX=0x100;r.m_context->XYOFFSET.OFY=0x200;r.m_context->UpdateScissor();
        r.m_context->scissor.in=GSVector4i(0,0,256,128);
        r.m_context->FRAME.FBP=0x1000>>5;r.m_context->FRAME.FBW=4;r.m_context->FRAME.PSM=PSMCT16;
        r.m_context->TEX0=source_tex;r.m_context->TEX0.TW=8;r.m_context->TEX0.TH=7;
        r.m_cached_ctx.FRAME=r.m_context->FRAME;r.m_cached_ctx.TEX0=r.m_context->TEX0;
        r.m_vt.m_primclass=triangle?GS_TRIANGLE_CLASS:GS_SPRITE_CLASS;r.m_vt.m_filter={};
        r.m_vt.m_min.p=r.m_vt.m_min.t=GSVector4(0.f,0.f,1.f,1.f);
        r.m_vt.m_max.p=r.m_vt.m_max.t=GSVector4(16.f,16.f,1.f,1.f);
        r.m_quad_check_valid=r.m_quad_check_valid_shuffle=false;
        r.m_vertex.head=0;r.m_vertex.tail=r.m_vertex.next=0;r.m_index.tail=0;
        r.m_process_texture=true;r.m_texture_shuffle=false;r.m_copy_16bit_to_target_shuffle=false;r.m_same_group_texture_shuffle=false;
        r.m_texture_shuffle_info={};r.m_split_texture_shuffle_pages=0;
        for(u32 i=0;i<20;i++){r.m_vertex.buff[i]={};r.m_index.buff[i]=i;}
    };
    const auto vertex=[&](u32 at,int x,int y,float u,float v,float q=1.f){
        auto& out=r.m_vertex.buff[at];out={};out.XYZ.X=0x100+x*16;out.XYZ.Y=0x200+y*16;
        out.XYZ.Z=9;out.RGBAQ.R=51;out.RGBAQ.G=73;out.RGBAQ.B=91;out.RGBAQ.A=128;out.RGBAQ.Q=q;out.FOG=117u<<24;
        out.U=static_cast<u16>(u*16);out.V=static_cast<u16>(v*16);out.ST.S=u/256.f*q;out.ST.T=v/128.f*q;
    };
    const auto quad=[&](u32 at,GSVector4i xy,GSVector4i uv){
        vertex(at*2,xy.x,xy.y,float(uv.x),float(uv.y));vertex(at*2+1,xy.z,xy.w,float(uv.z),float(uv.w));
        r.m_vertex.tail=r.m_vertex.next=(at+1)*2;r.m_index.tail=(at+1)*2;
    };
    // A consumer-only literal test also executes on the legacy baseline, so RED
    // proves more than the absence of a new declaration.
    prepare();r.m_texture_shuffle=true;r.m_cached_ctx.FRAME.FBMSK=8;
    check(r.GetEffectiveTextureShuffleFbmsk()==0x00010001u,
        "GS actual texture shuffle FBMSK replicates the literal packed 16-bit mask into both channel pairs");
    prepare();quad(0,GSVector4i(0,0,8,8),GSVector4i(0,0,8,8));
    r.m_texture_shuffle=true;r.m_texture_shuffle_info.type=TextureShuffleType::GappedSwizzle;
    r.m_texture_shuffle_info.channels=TextureShuffleChannels_BlueToRed;
    r.m_vt.m_max.p=GSVector4(8.f,32.f,1.f,1.f);r.m_vt.m_max.t=GSVector4(8.f,32.f,1.f,1.f);
    r.EmulateTextureShuffleAndFbmask(&target,&source);
    check(r.m_r.eq(GSVector4i(0,0,256,128))&&r.m_vertex.tail==2&&r.m_index.tail==2&&
        r.m_vertex.buff[1].XYZ.X==0x100+256*16&&r.m_vertex.buff[1].XYZ.Y==0x200+128*16&&
        r.m_vertex.buff[1].U==128*16&&r.m_vertex.buff[1].V==64*16,
        "GS actual GappedSwizzle converter rebuilds full target and source coverage instead of legacy partial geometry");
    check(r.m_conf.ps.process_ba==SHUFFLE_READ&&r.m_conf.ps.process_rg==SHUFFLE_WRITE&&r.m_conf.colormask.wrgba==1,
        "GS actual typed texture shuffle consumer writes exactly the selected blue-to-red channel");
    prepare();quad(0,GSVector4i(0,0,8,8),GSVector4i(0,0,8,8));r.m_r=GSVector4i(0,0,64,32);
    r.m_cached_ctx.FRAME.PSM=PSMCT16S;r.m_cached_ctx.FRAME.FBW=10;r.m_cached_ctx.FRAME.FBMSK=8;
    r.m_cached_ctx.TEX0.TBW=1;r.m_cached_ctx.TEX0.TBP0=0x2800;r.m_cached_ctx.TEX0.PSM=PSMZ16S;
    const GSVertex nfs_vertices[2]={r.m_vertex.buff[0],r.m_vertex.buff[1]};int skip=0;
    const bool nfs_return=GSHwHack::GSC_NFSUndercover(r,skip);
    check(!nfs_return&&skip==79&&std::memcmp(nfs_vertices,r.m_vertex.buff,sizeof(nfs_vertices))==0,
        "GS actual NFS GappedSwizzle CRC keeps skip79 without rewriting geometry owned by the converter");
    [&]<class Renderer>(Renderer& state){
        if constexpr(requires{state.SyncTextureShuffleCompatibility();})
        {
            const auto detect=[&](TextureShuffleType expected,const char* name){
                state.DetectTextureShuffle();
                const bool ok=state.m_texture_shuffle_info.type==expected&&state.m_texture_shuffle==bool(state.m_texture_shuffle_info);
                if(!ok)std::fprintf(stderr,"GS_TEXTURE_SHUFFLE case=%s actual=%u expected=%u channels=%x\n",name,u32(state.m_texture_shuffle_info.type),u32(expected),u32(state.m_texture_shuffle_info.channels));
                check(ok,name);
            };
            prepare();quad(0,GSVector4i(0,0,16,16),GSVector4i(8,0,24,16));detect(TextureShuffleType::Offset,"GS actual classifier recognizes an 8-pixel column offset");
            check(state.m_texture_shuffle_info.channels==(TextureShuffleChannels_BlueToRed|TextureShuffleChannels_AlphaToGreen),
                "GS actual classifier excludes the clobbered BA pair for a nonrecursive wide offset");
            prepare();quad(0,GSVector4i(0,0,16,16),GSVector4i(0,0,16,16));state.m_context->FRAME.FBMSK=state.m_cached_ctx.FRAME.FBMSK=0xffff;
            detect(TextureShuffleType::Copy,"GS actual classifier recognizes a masked same-channel copy");
            prepare();quad(0,GSVector4i(0,0,16,16),GSVector4i(0,0,16,16));state.m_context->CLAMP.WMS=CLAMP_REGION_REPEAT;state.m_context->CLAMP.MINU=7;
            state.m_cached_ctx.CLAMP=state.m_context->CLAMP;detect(TextureShuffleType::RegionRepeat8,"GS actual classifier recognizes region-repeat bit3 selection");
            prepare();quad(0,GSVector4i(0,0,16,16),GSVector4i(0,0,16,16));state.m_context->CLAMP.WMS=CLAMP_REGION_REPEAT;state.m_context->CLAMP.MINU=15;
            state.m_cached_ctx.CLAMP=state.m_context->CLAMP;detect(TextureShuffleType::RegionRepeat16,"GS actual classifier recognizes region-repeat bit4 selection");
            prepare();quad(0,GSVector4i(0,0,2,2),GSVector4i(0,0,1,1));detect(TextureShuffleType::TwoPixel,"GS actual classifier recognizes Powerdrome two-pixel replication");
            prepare();quad(0,GSVector4i(16,0,32,8),GSVector4i(31,0,15,8));detect(TextureShuffleType::Reverse,"GS actual classifier recognizes reversed 16-pixel columns");
            prepare();quad(0,GSVector4i(0,0,8,8),GSVector4i(0,0,8,8));quad(1,GSVector4i(8,0,16,8),GSVector4i(0,8,8,16));
            detect(TextureShuffleType::Swizzle,"GS actual classifier recognizes opposite-axis swizzle strips");
            quad(2,GSVector4i(8,24,16,32),GSVector4i(8,8,16,16));detect(TextureShuffleType::GappedSwizzle,"GS actual classifier recognizes the third-quad 16-row gap");
            prepare();quad(0,GSVector4i(0,0,8,8),GSVector4i(0,0,8,8));quad(1,GSVector4i(8,0,16,8),GSVector4i(0,8,8,16));
            state.m_vertex.tail=state.m_vertex.next=2;state.m_index.tail=2;
            detect(TextureShuffleType::None,"GS actual classifier does not read the poisoned next quad beyond a one-quad tail");
            prepare();quad(0,GSVector4i(8,0,16,8),GSVector4i(15,0,7,8));
            detect(TextureShuffleType::None,"GS actual classifier bounds the next quad after a skipped 8-pixel reversal");
            prepare();quad(0,GSVector4i(0,0,8,8),GSVector4i(0,0,8,8));quad(1,GSVector4i(8,0,16,8),GSVector4i(0,8,8,16));
            state.m_context->TEX0.PSM=state.m_cached_ctx.TEX0.PSM=PSMCT32;state.m_context->FRAME.FBMSK=state.m_cached_ctx.FRAME.FBMSK=0xffff;
            detect(TextureShuffleType::SwizzleTex32,"GS actual classifier recognizes C32-blue to C16-alpha swizzle");
            prepare();quad(0,GSVector4i(0,0,16,16),GSVector4i(8,0,24,16));state.DetectTextureShuffle();
            state.m_context->FRAME.FBMSK=state.m_cached_ctx.FRAME.FBMSK=0x38f8;source.m_32_bits_fmt=false;
            state.DetectTextureShuffleSecondPass(&target,&source);
            check(state.m_texture_shuffle_info.real_16_bit_source&&state.m_texture_shuffle,
                "GS actual real16 Offset second pass accepts Splashdown non-lower-ones FBMSK");
            source.m_32_bits_fmt=true;
            for(bool triangle:{false,true})for(bool fst:{false,true})
            {
                prepare(triangle,fst);
                vertex(0,0,0,8,0);vertex(1,16,0,24,0);vertex(2,0,16,8,16);vertex(3,16,16,24,16);
                if(triangle){const u16 q[]={0,1,2,1,2,3};std::copy(std::begin(q),std::end(q),state.m_index.buff);state.m_vertex.tail=state.m_vertex.next=4;state.m_index.tail=6;}
                else{vertex(1,16,16,24,16);state.m_vertex.tail=state.m_vertex.next=2;state.m_index.tail=2;}
                state.DetectTextureShuffle();state.DetectTextureShuffleSecondPass(&target,&source);
                check(state.m_texture_shuffle_info.type==TextureShuffleType::Offset,"GS actual sprite and triangle FST/STQ layouts reach the same typed Offset classification");
                state.m_texture_shuffle_info.real_16_bit_source=true;state.SyncTextureShuffleCompatibility();
                state.m_vt.m_min.p=GSVector4(0,0,1,1);state.m_vt.m_max.p=GSVector4(16,16,1,1);
                state.m_vt.m_min.t=GSVector4(8,0,1,1);state.m_vt.m_max.t=GSVector4(24,16,1,1);
                state.EmulateTextureShuffleAndFbmask(&target,&source);
                check(state.m_r.eq(GSVector4i(0,0,8,16))&&state.m_vertex.tail==(triangle?4u:2u)&&state.m_index.tail==(triangle?6u:2u),
                    "GS actual real16 Offset converter folds X only and preserves sprite/triangle topology");
                check(state.m_vertex.buff[0].RGBAQ.A==128&&state.m_vertex.buff[0].XYZ.Z==9&&state.m_vertex.buff[0].FOG==(117u<<24),
                    "GS actual shuffle converter retains provoking color depth fog payload");
            }
        }
        else check(false,"GS actual typed classifier and compatibility producer are integrated");
    }(r);
    check(static_cast<CoherenceDevice*>(g_gs_device.get())->unexpected_calls==0,"GS texture shuffle fixtures perform no GPU readback or submit");
}
extern "C" void EmuCoreXOracleTextureShuffleParity(void (*check)(bool,const char*))
{GSRendererHW::TestTextureShuffleParity(check);}
