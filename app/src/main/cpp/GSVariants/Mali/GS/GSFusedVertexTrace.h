// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "GS/GSRegs.h"
#include "GS/GSVector.h"
#include "GS/Renderers/Common/GSVertex.h"
#include <algorithm>
#include <cfloat>

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
extern thread_local bool g_gsFusedTraceEnabled;
extern thread_local bool g_gsFusedTraceObserve;
extern thread_local u32 g_gsFusedTraceMutation;
extern thread_local u64 g_gsFusedTraceStats[4];
#endif

// Adapted from the ARM64 GSVertexKick fused-FMM family. Integer bounds and
// masked IEEE quotients retain the legacy FMM's operations; no Q approximation.
namespace GSFusedVertexTrace
{
    struct Accumulator
    {
        GSVector4i pmin, pmax, tmin, tmax, tnan, cmin, cmax;

        __forceinline_odr void Reset(bool tme, bool fst)
        {
            pmin = cmin = GSVector4i::xffffffff();
            pmax = cmax = tnan = GSVector4i::zero();
            tmin = tme && !fst ? GSVector4i::cast(GSVector4(FLT_MAX)) : GSVector4i::xffffffff();
            tmax = tme && !fst ? GSVector4i::cast(GSVector4(-FLT_MAX)) : GSVector4i::zero();
        }

        __forceinline_odr void Color(const GSVector4i& m0)
        {
            cmin = cmin.min_u8(m0);
            cmax = cmax.max_u8(m0);
        }

        __forceinline_odr void Vertex(const GSVertex& vertex, bool tme, bool fst, bool color)
        {
            const GSVector4i m0(vertex.m[0]), m1(vertex.m[1]);
            const GSVector4i p = m1.upl16().blend32<0xc>(m1.ywyw());
            pmin = pmin.min_u32(p); pmax = pmax.max_u32(p);
            if (tme)
            {
                if (fst)
                {
                    tmin = tmin.min_u16(m1); tmax = tmax.max_u16(m1);
                }
                else
                {
                    const GSVector4 raw = GSVector4::cast(m0);
                    // Duplicate only S/T for division, like one legacy pair
                    // containing this vertex twice. Never divide RGBA padding.
                    const GSVector4 stq = (raw.xyxy() / raw.wwww()).xyww(raw);
                    const GSVector4i nan = GSVector4i::cast(stq != stq);
                    const GSVector4 keep = GSVector4::cast(~nan);
                    const GSVector4 lo = GSVector4::cast(tmin), hi = GSVector4::cast(tmax);
                    tmin = GSVector4i::cast(lo.blend32(lo.min(stq), keep));
                    tmax = GSVector4i::cast(hi.blend32(hi.max(stq), keep));
                    tnan |= nan;
                }
            }
            if (color) Color(m0);
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
            if (g_gsFusedTraceObserve) ++g_gsFusedTraceStats[3];
#endif
        }
    };

    __forceinline_odr u32 Key(const GIFRegPRIM& prim)
    {
        return prim.TME | (prim.FST << 1) | (prim.IIP << 2);
    }

    struct State
    {
        Accumulator acc;
        u32 watermark = 0, indices = 0, vertexEnd = 0, key = 0;
        bool valid = false;

        __forceinline_odr void Invalidate() { valid = false; }
        __forceinline_odr void Rewind(u32 first) { watermark = std::min(watermark, first); }

        __forceinline_odr void Triangle(const GSVertex* vertices, u32 first, u32 indexEnd, u32 drawKey)
        {
            const bool tme = (drawKey & 1) != 0, fst = (drawKey & 2) != 0;
            if (indexEnd == 3)
            {
                acc.Reset(tme, fst); watermark = first; vertexEnd = 0;
                key = drawKey; indices = 0; valid = true;
            }
            if (!valid || indices + 3 != indexEnd || key != drawKey)
            {
                valid = false;
                return;
            }
            const u32 last = first + 2;
            for (u32 at = std::max(first, watermark); at <= last; ++at)
                acc.Vertex(vertices[at], tme, fst, (drawKey & 4) != 0);
            // Flat color belongs to every provoking index, including an index
            // whose position/texture has already been included by a prior prim.
            if (!(drawKey & 4)) acc.Color(GSVector4i(vertices[last].m[0]));
            watermark = last + 1; vertexEnd = std::max(vertexEnd, watermark); indices = indexEnd;
        }
    };

    template<class Trace>
    __forceinline_odr void Finish(const Accumulator& a, bool tme, bool fst, bool color,
        const GIFRegXYOFFSET& offset, u32 tw, u32 th, Trace& trace)
    {
        const GSVector4 o(offset), scale(1.0f / 16, 1.0f / 16, 2.0f, 1.0f);
        trace.m_min.p = (GSVector4(a.pmin) - o) * scale;
        trace.m_max.p = (GSVector4(a.pmax) - o) * scale;
        trace.m_min.p = trace.m_min.p.template insert32<0, 2>(GSVector4::load(static_cast<float>(static_cast<u32>(a.pmin.template extract32<2>()))));
        trace.m_max.p = trace.m_max.p.template insert32<0, 2>(GSVector4::load(static_cast<float>(static_cast<u32>(a.pmax.template extract32<2>()))));
        if (tme)
        {
            if (fst)
            {
                const GSVector4i lo(a.tmin.U16[4], a.tmin.U16[5], a.tmin.U16[4], a.tmin.U16[5]);
                const GSVector4i hi(a.tmax.U16[4], a.tmax.U16[5], a.tmax.U16[4], a.tmax.U16[5]);
                const GSVector4 s = GSVector4(1.0f / 16, 1.0f).xxyy();
                trace.m_min.t = GSVector4(lo) * s; trace.m_max.t = GSVector4(hi) * s;
                // The legacy FST kernel deliberately leaves nan untouched.
            }
            else
            {
                const GSVector4 s(1 << tw, 1 << th, 1, 1);
                trace.m_min.t = GSVector4::cast(a.tmin) * s; trace.m_max.t = GSVector4::cast(a.tmax) * s;
                trace.nan.value = a.tnan.mask() & ~4u;
            }
        }
        else
        {
            trace.m_min.t = trace.m_max.t = GSVector4::zero(); trace.nan.value = 0;
        }
        trace.m_min.c = color ? a.cmin.zzzz().u8to32() : GSVector4i::zero();
        trace.m_max.c = color ? a.cmax.zzzz().u8to32() : GSVector4i::zero();
    }
}
