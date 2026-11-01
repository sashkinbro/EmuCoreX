// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Adapted from GSVertexKickKernel.h at cb78946d6ed0678b82fa3e0409c10124265ec18d.
// Local native-grid strip kernel. Buffer ownership, flushing and FMM stay in GSState.
#pragma once

#include "GS/Renderers/Common/GSVertex.h"
#include "Config.h"
#include "GS/GSFusedVertexTrace.h"

namespace GSStripKickKernel
{
    constexpr u32 kChunkVertices=128,kMinVertices=6;
    constexpr u64 kBandX=0x0fffffffull,kBandY=kBandX<<28,kOutcode=0xfull<<56,kAdc=1ull<<60;
    struct Bounds {s32 l,t,r,b;};
    struct Entry {u64 xy,meta;};

    __forceinline_odr bool InDomain(s32 x,s32 y)
    {
        return x>=-65535&&x<=65535&&y>=-65535&&y<=65535;
    }
    __forceinline_odr Bounds BandedBounds(const GSVector4i& cull)
    {
        return {(cull.x+14)>>4,(cull.y+14)>>4,(cull.z-1)>>4,(cull.w-1)>>4};
    }
    __forceinline_odr bool ScissorInDomain(const GSVector4i& cull)
    {
        return cull.x>=-0x100000&&cull.x<=0x100000&&cull.y>=-0x100000&&cull.y<=0x100000&&
            cull.z>=-0x100000&&cull.z<=0x100000&&cull.w>=-0x100000&&cull.w<=0x100000;
    }
    __forceinline_odr Entry MakeEntry(s32 x,s32 y,const Bounds& bounds)
    {
        // Callers prove the coordinate domain first. Subtracting one and
        // packing the signed bands therefore never overflows a signed word.
        const s32 bx=(x-1)>>4,by=(y-1)>>4;
        const u32 oc=static_cast<u32>(bx<bounds.l)|(static_cast<u32>(bx>=bounds.r)<<1)|
            (static_cast<u32>(by<bounds.t)<<2)|(static_cast<u32>(by>=bounds.b)<<3);
        return {static_cast<u32>(x)|(static_cast<u64>(static_cast<u32>(y))<<32),
            (static_cast<u64>(static_cast<u32>(bx))&kBandX)|
            ((static_cast<u64>(static_cast<u32>(by))<<28)&kBandY)|(static_cast<u64>(oc)<<56)};
    }
    __forceinline_odr u32 Reject(u64 xy0,u64 meta0,u64 xy1,u64 meta1,u64 xy2,u64 meta2)
    {
        const u64 difference=(meta0^meta1)|(meta0^meta2);
        return ((meta0&meta1&meta2&kOutcode)!=0)||
            ((difference&kBandX)==0)||((difference&kBandY)==0)||
            xy0==xy1||xy0==xy2||xy1==xy2;
    }
    __forceinline_odr GSVector4i XY(u64 packed)
    {
#if defined(ARCH_ARM64)
        return GSVector4i(vreinterpretq_s32_u64(vdupq_n_u64(packed)));
#else
        return GSVector4i::loadl(&packed).xyxy();
#endif
    }
    __forceinline_odr GSVector4i NativeBBox(u64 a,u64 b,u64 c)
    {
        const GSVector4i raw=XY(a).runion(XY(b)).runion(XY(c));
        return ((raw+GSVector4i(15,15,-1,-1))&GSVector4i(~15))+GSVector4i(0,0,1,1);
    }

#if defined(ARCH_ARM64)
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
    inline thread_local bool MirrorObserve=false;
    inline thread_local u64 MirrorStats[3]={}; // actual quad calls, scalar entries, overlap-tail quads
#endif
    // Native-grid specialization of GSVertexKickKernel::BuildMirrorQuad.
    // Only provisional side arrays are written; last-register and primitive
    // state are not touched by an idempotent overlap-tail rebuild.
    struct MirrorBounds {int32x4_t ofx,ofy,l,t,r,b;};
    __forceinline_odr MirrorBounds MakeMirrorBounds(const Bounds& bounds,s32 ofx,s32 ofy)
    {
        return {vdupq_n_s32(ofx),vdupq_n_s32(ofy),vdupq_n_s32(bounds.l),
            vdupq_n_s32(bounds.t),vdupq_n_s32(bounds.r),vdupq_n_s32(bounds.b)};
    }
    template<bool observe=false>
    __forceinline_odr void BuildMirrorQuad(uint32x4_t v0,uint32x4_t v1,uint32x4_t v2,uint32x4_t v3,
        const MirrorBounds& k,u64* RESTRICT sideXY,u64* RESTRICT sideMeta)
    {
        const uint64x2_t a01=vreinterpretq_u64_u32(vzip1q_u32(v0,v1));
        const uint64x2_t a23=vreinterpretq_u64_u32(vzip1q_u32(v2,v3));
        const uint64x2_t b01=vreinterpretq_u64_u32(vzip2q_u32(v0,v1));
        const uint64x2_t b23=vreinterpretq_u64_u32(vzip2q_u32(v2,v3));
        const uint32x4_t X=vreinterpretq_u32_u64(vzip1q_u64(a01,a23));
        const uint32x4_t Y=vreinterpretq_u32_u64(vzip2q_u64(a01,a23));
        const uint32x4_t F=vreinterpretq_u32_u64(vzip2q_u64(b01,b23));
        const uint32x4_t mask16=vdupq_n_u32(65535);
        const int32x4_t wx=vsubq_s32(vreinterpretq_s32_u32(vandq_u32(X,mask16)),k.ofx);
        const int32x4_t wy=vsubq_s32(vreinterpretq_s32_u32(vandq_u32(Y,mask16)),k.ofy);
        const int32x4_t bx=vshrq_n_s32(vsubq_s32(wx,vdupq_n_s32(1)),4);
        const int32x4_t by=vshrq_n_s32(vsubq_s32(wy,vdupq_n_s32(1)),4);
        uint32x4_t oc=vandq_u32(vcltq_s32(bx,k.l),vdupq_n_u32(1));
        oc=vorrq_u32(oc,vandq_u32(vcgeq_s32(bx,k.r),vdupq_n_u32(2)));
        oc=vorrq_u32(oc,vandq_u32(vcltq_s32(by,k.t),vdupq_n_u32(4)));
        oc=vorrq_u32(oc,vandq_u32(vcgeq_s32(by,k.b),vdupq_n_u32(8)));
        oc=vorrq_u32(oc,vshrq_n_u32(vandq_u32(F,vdupq_n_u32(0x8000)),11));
        const uint32x4_t lo=vorrq_u32(vandq_u32(vreinterpretq_u32_s32(bx),vdupq_n_u32(0x0fffffff)),
            vshlq_n_u32(vreinterpretq_u32_s32(by),28));
        const uint32x4_t hi=vorrq_u32(vandq_u32(vshrq_n_u32(vreinterpretq_u32_s32(by),4),vdupq_n_u32(0x00ffffff)),
            vshlq_n_u32(oc,24));
        vst1q_u64(sideXY,vreinterpretq_u64_u32(vzip1q_u32(vreinterpretq_u32_s32(wx),vreinterpretq_u32_s32(wy))));
        vst1q_u64(sideXY+2,vreinterpretq_u64_u32(vzip2q_u32(vreinterpretq_u32_s32(wx),vreinterpretq_u32_s32(wy))));
        vst1q_u64(sideMeta,vreinterpretq_u64_u32(vzip1q_u32(lo,hi)));
        vst1q_u64(sideMeta+2,vreinterpretq_u64_u32(vzip2q_u32(lo,hi)));
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
        if constexpr(observe)++MirrorStats[0];
#endif
    }
    struct ParseConstants
    {
        uint8x16_t colourIndices={0,1,2,3,4,5,6,7,16,20,24,28,8,9,10,11};
        uint8x16_t fogIndices={0,1,4,5,24,25,26,255,255,255,255,255,28,255,255,255};
        uint32x4_t qMin={0,0,0x00800000,0};
    };
    template<bool fog,GSLimit24BitDepth depth>
    __forceinline_odr void Decode(const GIFPackedReg* RESTRICT packet,GSVertex& output,
        u64 uvfog,const ParseConstants& k)
    {
        uint32x4_t stq=vld1q_u32(packet[0].U32);
        stq=vorrq_u32(stq,vandq_u32(vceqq_u32(stq,vdupq_n_u32(0)),k.qMin));
        const uint8x16x2_t colours={{vreinterpretq_u8_u32(stq),vreinterpretq_u8_u32(vld1q_u32(packet[1].U32))}};
        const GSVector4i m0(vreinterpretq_s32_u8(vqtbl2q_u8(colours,k.colourIndices)));
        GSVector4i m1;
        if constexpr(fog)
        {
            const uint8x16_t raw=vld1q_u8(reinterpret_cast<const u8*>(&packet[2]));
            const uint8x16x2_t table={{raw,vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(raw),4))}};
            m1=GSVector4i(vreinterpretq_s32_u32(vsetq_lane_u32(static_cast<u32>(uvfog),
                vreinterpretq_u32_u8(vqtbl2q_u8(table,k.fogIndices)),2)));
        }
        else
        {
            const GSVector4i xy=GSVector4i::loadl(&packet[2].U64[0]);
            const GSVector4i z=GSVector4i::loadl(&packet[2].U64[1]);
            m1=xy.upl16(xy.srl<4>()).upl32(z).upl64(GSVector4i::loadl(&uvfog));
        }
        if constexpr(depth==GSLimit24BitDepth::PrioritizeUpper)
            m1.U32[1]=((m1.U32[1]>>8)&~0xffu)|(m1.U32[1]&0xffu);
        else if constexpr(depth==GSLimit24BitDepth::PrioritizeLower)
            m1.U32[1]&=0x00ffffff;
        output.m[0]=m0;output.m[1]=m1;
    }
    // Parse each vertex once, but build its signed mirror in four-wide groups.
    template<bool fog,GSLimit24BitDepth depth,bool observe=false>
    __forceinline_odr void ParseRows(const GIFPackedReg* RESTRICT packet,u32 count,
        GSVertex* RESTRICT provisional,u64* RESTRICT sideXY,u64* RESTRICT sideMeta,
        const Bounds& bounds,s32 ofx,s32 ofy,u64 uvfog)
    {
        const ParseConstants constants;
        const MirrorBounds mirror=MakeMirrorBounds(bounds,ofx,ofy);
        u32 i=0;
        for(const u32 quads=count&~3u;i<quads;i+=4)
        {
            for(u32 j=0;j<4;++j)Decode<fog,depth>(packet+(i+j)*3,provisional[i+j],uvfog,constants);
            BuildMirrorQuad<observe>(vld1q_u32(packet[(i+0)*3+2].U32),
                vld1q_u32(packet[(i+1)*3+2].U32),vld1q_u32(packet[(i+2)*3+2].U32),
                vld1q_u32(packet[(i+3)*3+2].U32),mirror,sideXY+i,sideMeta+i);
        }
        if(i<count&&count>=4)
        {
            // The final four real inputs cover the remainder without any
            // scalar metadata tail or read before/after this bounded packet.
            const u32 back=count-4;
            BuildMirrorQuad<observe>(vld1q_u32(packet[(back+0)*3+2].U32),
                vld1q_u32(packet[(back+1)*3+2].U32),vld1q_u32(packet[(back+2)*3+2].U32),
                vld1q_u32(packet[(back+3)*3+2].U32),mirror,sideXY+back,sideMeta+back);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
            if constexpr(observe)++MirrorStats[2];
#endif
            for(;i<count;++i)Decode<fog,depth>(packet+i*3,provisional[i],uvfog,constants);
            return;
        }
        for(;i<count;++i)
        {
            const auto* row=packet+i*3;
            Decode<fog,depth>(row,provisional[i],uvfog,constants);
            const Entry entry=MakeEntry(static_cast<s32>(row[2].XYZ2.X)-ofx,
                static_cast<s32>(row[2].XYZ2.Y)-ofy,bounds);
            sideXY[i]=entry.xy;sideMeta[i]=entry.meta|(static_cast<u64>(row[2].U32[3]&0x8000)<<45);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
            if constexpr(observe)++MirrorStats[1];
#endif
        }
    }
    template<bool fog,GSLimit24BitDepth depth>
    __forceinline_odr void Parse(const GIFPackedReg* RESTRICT packet,u32 count,
        GSVertex* RESTRICT provisional,u64* RESTRICT sideXY,u64* RESTRICT sideMeta,
        const Bounds& bounds,s32 ofx,s32 ofy,u64 uvfog)
    {
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
        if(MirrorObserve)
        {
            ParseRows<fog,depth,true>(packet,count,provisional,sideXY,sideMeta,bounds,ofx,ofy,uvfog);
            return;
        }
#endif
        ParseRows<fog,depth,false>(packet,count,provisional,sideXY,sideMeta,bounds,ofx,ofy,uvfog);
    }
#endif

    // The only state-machine pass. No GSState helper can observe provisional
    // writes, grow a buffer or flush in this bounded chunk.
    template<bool writeRing=true,bool observe=false,class Cursor>
    __forceinline_odr GSVector4i Emit(Cursor& cursor,GSVector4i* RESTRICT ring,
        const Entry* RESTRICT seed,const u64* RESTRICT sideXY,const u64* RESTRICT sideMeta,
        u32 count,u32& accState,u32& scalarCulls,GSFusedVertexTrace::State& trace,u32 traceKey)
    {
        GSVertex* RESTRICT vertices=cursor.vb;
        u32 head=cursor.head,tail=cursor.tail,next=cursor.next,itail=cursor.itail;
        const u32 xyTail=cursor.xy_tail;
        const GSVertex* provisional=vertices+tail;
        u16* RESTRICT indices=cursor.ib+itail;
        u64 xy0=seed[(xyTail-1)&3].xy,meta0=seed[(xyTail-1)&3].meta;
        u64 xy1=seed[(xyTail-2)&3].xy,meta1=seed[(xyTail-2)&3].meta;
        u64 xy2=0,meta2=0;
        GSVector4i rect=GSVector4i::zero();accState=0;
        for(u32 i=0;i<count;++i)
        {
            xy2=xy1;meta2=meta1;xy1=xy0;meta1=meta0;xy0=sideXY[i];meta0=sideMeta[i];
            vertices[tail]=*provisional++;
            if(++tail-head<3)continue;
            if(meta0&kAdc){++head;continue;}
            if constexpr(observe)++scalarCulls;
            if(Reject(xy0,meta0,xy1,meta1,xy2,meta2)) {++head;continue;}
            if(next<head)
            {
                vertices[next]=vertices[head];vertices[next+1]=vertices[head+1];vertices[next+2]=vertices[head+2];
                trace.Rewind(next);
                head=next;
            }
            indices[0]=static_cast<u16>(head);indices[1]=static_cast<u16>(head+1);indices[2]=static_cast<u16>(head+2);
            indices+=3;itail+=3;tail=head+3;next=tail;++head;
            trace.Triangle(vertices,tail-3,itail,traceKey);
            const GSVector4i draw=NativeBBox(xy0,xy1,xy2).sra32<4>()+GSVector4i(0,0,1,1);
            if(accState)rect=rect.runion(draw);
            else {rect=draw;accState=(itail==3)?2:1;}
        }
        if constexpr(writeRing)
            for(u32 i=count-4;i<count;++i)ring[(xyTail+i)&3]=XY(sideXY[i]);
        cursor.head=head;cursor.tail=tail;cursor.next=next;cursor.itail=itail;cursor.xy_tail=xyTail+count;
        return rect;
    }

    // Fixed per-chunk inputs. No GSState owner or callback can enter the pure
    // two-pass run; its caller owns every grow/flush/snapshot/publication seam.
    template<class Parser> struct ChunkInvariants
    {
        Parser parser;
        Bounds bounds;
        s32 ofx,ofy;
        const Entry* seed;
        GSVector4i* ring;
        u64* sideXY;
        u64* sideMeta;
        GSFusedVertexTrace::State* trace;
        u32 traceKey;
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
        u64* outlinedWork = nullptr;
#endif
    };
    struct ChunkOutput
    {
        GSVector4i last0,last1,rect;
        u32 accState,scalarCulls;
    };
    // Keep both passes together, but prevent LTO from fusing them into every
    // handler and its legacy seam kick. Quad and parse helpers stay inline here.
    template<bool writeRing=true,bool observe=false,class Parser,class Cursor>
    __noinline void RunChunk(const GIFPackedReg* RESTRICT packet,u32 count,
        Cursor& cursor,const ChunkInvariants<Parser>& inv,ChunkOutput& output)
    {
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
        if(inv.outlinedWork)
        {
            ++inv.outlinedWork[0];
            inv.outlinedWork[2]+=count;
        }
#endif
        GSVertex* const provisional=cursor.vb+cursor.tail;
        inv.parser(packet,count,provisional,inv.sideXY,inv.sideMeta,inv.bounds,inv.ofx,inv.ofy);
        // Capture the last original parse before compaction can overwrite it.
        output.last0=GSVector4i(provisional[count-1].m[0]);
        output.last1=GSVector4i(provisional[count-1].m[1]);
        output.accState=output.scalarCulls=0;
        output.rect=Emit<writeRing,observe>(cursor,inv.ring,inv.seed,inv.sideXY,
            inv.sideMeta,count,output.accState,output.scalarCulls,*inv.trace,inv.traceKey);
    }
}
