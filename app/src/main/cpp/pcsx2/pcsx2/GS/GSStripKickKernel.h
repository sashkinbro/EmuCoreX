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
    // Pass one touches only the packet, provisional vertex slots and side
    // arrays. Constants and the depth-clamp choice stay outside the row loop.
    template<bool fog,GSLimit24BitDepth depth>
    __forceinline_odr void Parse(const GIFPackedReg* RESTRICT packet,u32 count,
        GSVertex* RESTRICT provisional,u64* RESTRICT sideXY,u64* RESTRICT sideMeta,
        const Bounds& bounds,s32 ofx,s32 ofy,u64 uvfog)
    {
        const uint8x16_t colourIndices={0,1,2,3,4,5,6,7,16,20,24,28,8,9,10,11};
        const uint8x16_t fogIndices={0,1,4,5,24,25,26,255,255,255,255,255,28,255,255,255};
        const uint32x4_t qMin={0,0,0x00800000,0};
        for(u32 i=0;i<count;++i,packet+=3)
        {
            uint32x4_t stq=vld1q_u32(packet[0].U32);
            stq=vorrq_u32(stq,vandq_u32(vceqq_u32(stq,vdupq_n_u32(0)),qMin));
            const uint8x16x2_t colours={{vreinterpretq_u8_u32(stq),vreinterpretq_u8_u32(vld1q_u32(packet[1].U32))}};
            const GSVector4i m0(vreinterpretq_s32_u8(vqtbl2q_u8(colours,colourIndices)));
            GSVector4i m1;
            if constexpr(fog)
            {
                const uint8x16_t raw=vld1q_u8(reinterpret_cast<const u8*>(&packet[2]));
                const uint8x16x2_t table={{raw,vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(raw),4))}};
                m1=GSVector4i(vreinterpretq_s32_u32(vsetq_lane_u32(static_cast<u32>(uvfog),
                    vreinterpretq_u32_u8(vqtbl2q_u8(table,fogIndices)),2)));
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
            provisional[i].m[0]=m0;provisional[i].m[1]=m1;
            const Entry entry=MakeEntry(static_cast<s32>(packet[2].XYZ2.X)-ofx,
                static_cast<s32>(packet[2].XYZ2.Y)-ofy,bounds);
            sideXY[i]=entry.xy;
            sideMeta[i]=entry.meta|((static_cast<u64>(packet[2].U32[3]&0x8000))<<45);
        }
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
}
