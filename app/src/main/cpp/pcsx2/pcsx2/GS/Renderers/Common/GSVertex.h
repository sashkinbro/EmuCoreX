// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/GSRegs.h"
#include "GS/GSVector.h"
#include "GS/Renderers/HW/GSVertexHW.h"
#include "GS/Renderers/SW/GSVertexSW.h"

struct alignas(32) GSVertex
{
	union
	{
		struct
		{
			GIFRegST ST;       // S:0, T:4
			GIFRegRGBAQ RGBAQ; // RGBA:8, Q:12
			GIFRegXYZ XYZ;     // XY:16, Z:20
			union { u32 UV; struct { u16 U, V; }; }; // UV:24
			u32 FOG;        // FOG:28
		};

#if defined(ARCH_X86)
#if _M_SSE >= 0x500
		__m256i mx;
#endif
		__m128i m[2];
#elif defined(ARCH_ARM64)
		int32x4_t m[2];
#endif
	};
};

static_assert(sizeof(GSVertex) == 32);

struct alignas(32) GSVertexPT1
{
	GSVector4 p;
	GSVector2 t;
	char pad[4];
	union { u32 c; struct { u8 r, g, b, a; }; };
};

static_assert(sizeof(GSVertexPT1) == sizeof(GSVertex));

__forceinline_odr GSVector4i GetVertexXY(const GSVertex& v)
{
	return GSVector4i(v.m[1]).upl16().xyxy();
}

// v0/v1/v2 are cached GetVertexXY results: both 64-bit halves contain X/Y.
template <GS_PRIM_CLASS primclass>
__forceinline_odr bool GSVertexIsRejected(const GSVector4i& bbox, const GSVector4i& scissor,
	const GSVector4i& v0, const GSVector4i& v1, const GSVector4i& v2)
{
	const GSVector4i bbox_ex = bbox + GSVector4i(0, 0, 1, 1);
#if defined(ARCH_ARM64)
	// Combine clipping and empty bounds before reducing the SIMD result. Cached
	// XY pairs need only 64-bit equality, rather than three 128-bit reductions.
	const int32x2_t lo = vmax_s32(vget_low_s32(bbox_ex.v4s), vget_low_s32(scissor.v4s));
	const int32x2_t hi = vmin_s32(vget_high_s32(bbox_ex.v4s), vget_high_s32(scissor.v4s));
	uint32x2_t valid = vcgt_s32(hi, lo);
	if constexpr (primclass == GS_TRIANGLE_CLASS || primclass == GS_SPRITE_CLASS)
		valid = vand_u32(valid, vcgt_s32(vget_high_s32(bbox.v4s), vget_low_s32(bbox.v4s)));
	if constexpr (primclass == GS_TRIANGLE_CLASS)
	{
		const uint64x1_t a = vreinterpret_u64_s32(vget_low_s32(v0.v4s));
		const uint64x1_t b = vreinterpret_u64_s32(vget_low_s32(v1.v4s));
		const uint64x1_t c = vreinterpret_u64_s32(vget_low_s32(v2.v4s));
		const uint64x1_t equal = vorr_u64(vorr_u64(vceq_u64(a, b), vceq_u64(b, c)), vceq_u64(a, c));
		valid = vbic_u32(valid, vreinterpret_u32_u64(equal));
	}
	return vminv_u32(valid) == 0;
#else
	bool rejected = !bbox_ex.rintersects(scissor);
	if constexpr (primclass == GS_TRIANGLE_CLASS || primclass == GS_SPRITE_CLASS)
		rejected |= bbox.rempty();
	if constexpr (primclass == GS_TRIANGLE_CLASS)
		rejected |= v0.eq(v1) | v1.eq(v2) | v0.eq(v2);
	return rejected;
#endif
}

__forceinline_odr GSVector4i GetVertexZ(const GSVertex& v)
{
	return GSVector4i(v.m[1]).yyyy();
}

__forceinline_odr GSVector4i GetVertexUV(const GSVertex& v)
{
	return GSVector4i(v.m[1]).uph16().xyxy();
}

__forceinline_odr GSVector4 GetVertexST(const GSVertex& v)
{
	return GSVector4::cast(GSVector4i(v.m[0])).xyxy();
}

__forceinline_odr GSVector4i GetVertexRGBA(const GSVertex& v)
{
	return GSVector4i(v.m[0]).uph8().upl16();
}

__forceinline_odr GSVector4 GetVertexQ(const GSVertex& v)
{
	return GSVector4::cast(GSVector4i(v.m[0])).wwww();
}

__forceinline_odr GSVector4i GetVertexFOG(const GSVertex& v)
{
	return GSVector4i(v.m[1]).wwww();
}

