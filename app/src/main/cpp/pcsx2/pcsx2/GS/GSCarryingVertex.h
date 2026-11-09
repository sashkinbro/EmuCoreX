// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "GS/GSRegs.h"
#include "GS/GSVertexKick.h"
#include "GS/GSStripKickKernel.h"

// The five scalar-equivalent packed layouts. The two inherited contiguous
// triples retain their own raw-Q policy and never enter these parsers.
namespace GSCarryingVertex
{
    enum class Layout : u32 { NopTriple, STQPair, UVPair, RGBAPair, UVTriple };
    constexpr GSVertexKernels::PackedLayout KernelLayout(Layout l)
    {
        using K=GSVertexKernels::PackedLayout;
        switch(l)
        {
            case Layout::NopTriple:return K::NopTripleXYZF2;
            case Layout::STQPair:return K::PairSTQXYZ2;
            case Layout::UVPair:return K::PairUVXYZ2;
            case Layout::RGBAPair:return K::PairRGBAQXYZ2;
            case Layout::UVTriple:return K::UvTripleXYZF2;
        }
        return K::NopTripleXYZF2;
    }
    constexpr bool HasUV(Layout l) { return l == Layout::UVPair || l == Layout::UVTriple; }
    constexpr bool HasRGBA(Layout l) { return l == Layout::NopTriple || l == Layout::UVTriple; }
    constexpr bool XYZF(Layout l) { return l == Layout::NopTriple || l == Layout::UVTriple; }
    constexpr bool LatchesQ(Layout l) { return l == Layout::NopTriple || l == Layout::STQPair; }
    constexpr bool ColourOverCarry(Layout l) { return l == Layout::RGBAPair || l == Layout::UVTriple; }

    __forceinline_odr u32 NormalizeQ(u32 q)
    {
        return q == 0 ? 0x00800000u : (q & 0x7fffffffu) > 0x7f800000u ? 0x7f7fffffu : q;
    }
    __forceinline_odr GSVector4i MakeCarry(Layout l, const GSVector4i& m0, u32 latch)
    {
        GSVector4i carry = m0;
        if (ColourOverCarry(l)) carry.U32[2] = latch;
        return carry;
    }
    struct Consts
    {
#if defined(ARCH_ARM64)
        uint8x16_t colour = {0,1,2,3,4,5,6,7,16,20,24,28,8,9,10,11};
        uint8x16_t fog = {0,1,4,5,24,25,26,255,255,255,255,255,28,255,255,255};
        uint8x16_t xyz = {0,1,4,5,8,9,10,11,255,255,255,255,255,255,255,255};
#endif
    };
    template<Layout L>
    __forceinline_odr void Parse(const GIFPackedReg* r, const GIFPackedLayout& off, u64 uvfog,
        const GSVector4i& carry, const Consts& k, GSVector4i& m0, GSVector4i& m1)
    {
        if constexpr (HasUV(L))
            uvfog = (uvfog & 0xffffffff00000000ull) | (r[off.off_a].U32[0] & 0x3fff) |
                (static_cast<u64>(r[off.off_a].U32[1] & 0x3fff) << 16);
        if constexpr (L == Layout::UVPair)
            m0 = carry;
        else if constexpr (L == Layout::STQPair)
            m0 = GSVector4i::loadl(&r[off.off_a].U64[0]).upl64(carry.zwzw());
        else
        {
            const u32 rgba = L == Layout::RGBAPair ? off.off_a : off.off_rgba;
#if defined(ARCH_ARM64)
            const uint8x16_t a = L == Layout::NopTriple ?
                vld1q_u8(reinterpret_cast<const u8*>(r + off.off_a)) : vreinterpretq_u8_s32(carry.v4s);
            const uint8x16x2_t table = {{a, vld1q_u8(reinterpret_cast<const u8*>(r + rgba))}};
            m0 = GSVector4i(vreinterpretq_s32_u8(vqtbl2q_u8(table, k.colour)));
#else
            m0 = carry;
            if constexpr (L == Layout::NopTriple) m0.U64[0] = r[off.off_a].U64[0];
            m0.U32[2] = (r[rgba].U32[0]&255) | ((r[rgba].U32[1]&255)<<8) |
                ((r[rgba].U32[2]&255)<<16) | ((r[rgba].U32[3]&255)<<24);
            m0.U32[3] = L == Layout::NopTriple ? r[off.off_a].U32[2] : carry.U32[2];
#endif
            if constexpr (L == Layout::NopTriple) m0.U32[3] = NormalizeQ(m0.U32[3]);
        }
#if defined(ARCH_ARM64)
        const uint8x16_t raw = vld1q_u8(reinterpret_cast<const u8*>(r + off.off_xyz));
        if constexpr (XYZF(L))
        {
            const uint8x16x2_t table = {{raw, vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(raw),4))}};
            m1 = GSVector4i(vreinterpretq_s32_u32(vsetq_lane_u32(static_cast<u32>(uvfog),
                vreinterpretq_u32_u8(vqtbl2q_u8(table,k.fog)),2)));
        }
        else
            m1 = GSVector4i(vreinterpretq_s32_u64(vsetq_lane_u64(uvfog,
                vreinterpretq_u64_u8(vqtbl1q_u8(raw,k.xyz)),1)));
#else
        const GSVector4i xy = GSVector4i::loadl(&r[off.off_xyz].U64[0]);
        if constexpr (XYZF(L))
        {
            GSVector4i zf = GSVector4i::loadl(&r[off.off_xyz].U64[1]);
            zf = zf.srl32<4>() & GSVector4i::x00ffffff().upl32(GSVector4i::x000000ff());
            m1 = xy.upl16(xy.srl<4>()).upl32(GSVector4i::load(static_cast<int>(uvfog))).upl32(zf);
        }
        else
            m1 = xy.upl16(xy.srl<4>()).upl32(GSVector4i::loadl(&r[off.off_xyz].U64[1])).upl64(GSVector4i::loadl(&uvfog));
#endif
    }
    __forceinline_odr void ClampDepth(GSVector4i& m1, GSLimit24BitDepth mode)
    {
        if (mode == GSLimit24BitDepth::PrioritizeUpper) m1.U32[1] = ((m1.U32[1]>>8)&~0xffu)|(m1.U32[1]&0xffu);
        else if (mode == GSLimit24BitDepth::PrioritizeLower) m1.U32[1] &= 0x00ffffff;
    }
#if defined(ARCH_ARM64)
    template<Layout L> struct BatchParser
    {
        GIFPackedLayout off;
        u64 uvfog;
        GSVector4i carry;
        Consts constants;
        GSLimit24BitDepth depth;
        template<GSLimit24BitDepth D>
        __forceinline_odr void DecodeRow(const GIFPackedReg* r,GSVertex& output) const
        {
            GSVector4i m0,m1;Parse<L>(r,off,uvfog,carry,constants,m0,m1);
            ClampDepth(m1,D);output.m[0]=m0;output.m[1]=m1;
        }
        template<GSLimit24BitDepth D,bool observe=false>
        __forceinline_odr void Rows(const GIFPackedReg* r,u32 count,GSVertex* provisional,
            u64* sideXY,u64* sideMeta,const GSStripKickKernel::Bounds& bounds,s32 ofx,s32 ofy) const
        {
            u32 i=0;
            // Every classified Rows consumer shares the same bounded raw-XYZ
            // mirror; DecodeRow retains each layout's carried vertex fields.
            {
                const auto mirror=GSStripKickKernel::MakeMirrorBounds(bounds,ofx,ofy);
                for(const u32 quads=count&~3u;i<quads;i+=4)
                {
                    for(u32 j=0;j<4;++j)DecodeRow<D>(r+(i+j)*off.stride,provisional[i+j]);
                    GSStripKickKernel::BuildMirrorQuad<observe>(vld1q_u32(r[(i+0)*off.stride+off.off_xyz].U32),
                        vld1q_u32(r[(i+1)*off.stride+off.off_xyz].U32),vld1q_u32(r[(i+2)*off.stride+off.off_xyz].U32),
                        vld1q_u32(r[(i+3)*off.stride+off.off_xyz].U32),mirror,sideXY+i,sideMeta+i);
                }
                if(i<count&&count>=4)
                {
                    const u32 back=count-4;
                    GSStripKickKernel::BuildMirrorQuad<observe>(vld1q_u32(r[(back+0)*off.stride+off.off_xyz].U32),
                        vld1q_u32(r[(back+1)*off.stride+off.off_xyz].U32),vld1q_u32(r[(back+2)*off.stride+off.off_xyz].U32),
                        vld1q_u32(r[(back+3)*off.stride+off.off_xyz].U32),mirror,sideXY+back,sideMeta+back);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
                    if constexpr(observe)++GSStripKickKernel::MirrorStats[2];
#endif
                    for(;i<count;++i)DecodeRow<D>(r+i*off.stride,provisional[i]);
                    return;
                }
            }
            for(;i<count;++i)
            {
                const auto* row=r+i*off.stride;DecodeRow<D>(row,provisional[i]);
                const auto& xyz=row[off.off_xyz];
                const auto entry=GSStripKickKernel::MakeEntry(static_cast<s32>(xyz.XYZ2.X)-ofx,
                    static_cast<s32>(xyz.XYZ2.Y)-ofy,bounds);
                sideXY[i]=entry.xy;sideMeta[i]=entry.meta|(static_cast<u64>(xyz.U32[3]&0x8000)<<45);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
                if constexpr(observe)++GSStripKickKernel::MirrorStats[1];
#endif
            }
        }
        template<bool observe>
        __forceinline_odr void Dispatch(const GIFPackedReg* r,u32 count,GSVertex* provisional,
            u64* sideXY,u64* sideMeta,const GSStripKickKernel::Bounds& bounds,s32 ofx,s32 ofy) const
        {
            if(depth==GSLimit24BitDepth::PrioritizeUpper) Rows<GSLimit24BitDepth::PrioritizeUpper,observe>(r,count,provisional,sideXY,sideMeta,bounds,ofx,ofy);
            else if(depth==GSLimit24BitDepth::PrioritizeLower) Rows<GSLimit24BitDepth::PrioritizeLower,observe>(r,count,provisional,sideXY,sideMeta,bounds,ofx,ofy);
            else Rows<GSLimit24BitDepth::Disabled,observe>(r,count,provisional,sideXY,sideMeta,bounds,ofx,ofy);
        }
        __forceinline_odr void operator()(const GIFPackedReg* r,u32 count,GSVertex* provisional,
            u64* sideXY,u64* sideMeta,const GSStripKickKernel::Bounds& bounds,s32 ofx,s32 ofy) const
        {
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
            if(GSStripKickKernel::MirrorObserve)
            {
                Dispatch<true>(r,count,provisional,sideXY,sideMeta,bounds,ofx,ofy);
                return;
            }
#endif
            Dispatch<false>(r,count,provisional,sideXY,sideMeta,bounds,ofx,ofy);
        }
    };
    template<bool fog> struct TripleBatchParser
    {
        u64 uvfog;
        GSLimit24BitDepth depth;
        __forceinline_odr void operator()(const GIFPackedReg* r,u32 count,GSVertex* provisional,
            u64* sideXY,u64* sideMeta,const GSStripKickKernel::Bounds& bounds,s32 ofx,s32 ofy) const
        {
            if(depth==GSLimit24BitDepth::PrioritizeUpper) GSStripKickKernel::Parse<fog,GSLimit24BitDepth::PrioritizeUpper>(r,count,provisional,sideXY,sideMeta,bounds,ofx,ofy,uvfog);
            else if(depth==GSLimit24BitDepth::PrioritizeLower) GSStripKickKernel::Parse<fog,GSLimit24BitDepth::PrioritizeLower>(r,count,provisional,sideXY,sideMeta,bounds,ofx,ofy,uvfog);
            else GSStripKickKernel::Parse<fog,GSLimit24BitDepth::Disabled>(r,count,provisional,sideXY,sideMeta,bounds,ofx,ofy,uvfog);
        }
    };
#endif
}
