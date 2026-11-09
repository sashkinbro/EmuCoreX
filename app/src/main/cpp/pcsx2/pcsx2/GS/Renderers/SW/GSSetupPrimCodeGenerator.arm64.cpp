// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#include "GS/Renderers/SW/GSSetupPrimCodeGenerator.arm64.h"
#include "GS/Renderers/SW/GSBlockWalk.h"
#include "GS/Renderers/SW/GSCoordinateWalk.h"
#include "GS/Renderers/SW/GSVertexSW.h"

#include "common/StringUtil.h"
#include "common/Perf.h"

#include <cstdint>

MULTI_ISA_UNSHARED_IMPL;

using namespace oak::util;

static const auto& _vertex = X0;
static const auto& _index = X1;
static const auto& _dscan = X2;
static const auto& _locals = X3;
static const auto& _scratchaddr = X7;
static const auto& _vscratch = Q31;

static constexpr const GSScanlineConstantData128B& g_const = g_const_128b;

// Yay, you can't offsetof with non-constant array indices in GCC
#define OFFSETOF(base, field) (reinterpret_cast<uptr>(&reinterpret_cast<base*>(0)->field))
#define _local(field) OakMemOperand{_locals, static_cast<s64>(OFFSETOF(GSScanlineLocalData, field))}
#include "GSOakScanlineEmitter.h"
static thread_local GSDrawOakEmitter* gsAsm;

GSSetupPrimCodeGenerator::GSSetupPrimCodeGenerator(u64 key, void* code, size_t maxsize)
	: m_code(static_cast<u8*>(code))
	, m_capacity(maxsize)
	, m_sel(key)
{
	m_en.z = m_sel.zb ? 1 : 0;
	m_en.f = m_sel.fb && m_sel.fge ? 1 : 0;
	m_en.t = m_sel.fb && m_sel.tfx != TFX_NONE ? 1 : 0;
	m_en.c = m_sel.fb && !(m_sel.tfx == TFX_DECAL && m_sel.tcc) ? 1 : 0;
}

void GSSetupPrimCodeGenerator::Generate()
{
	pxAssert(!oakAsm);
	oak::CodeGenerator emitter(reinterpret_cast<u32*>(m_code));
	GSDrawOakEmitter gs_emitter(emitter);
	oakAsm = &emitter;
	gsAsm = &gs_emitter;
	// Colour and fog lane offsets and steps are built by
	// GSDrawScanline::SetupColourWalkTables, which the rasterizer calls after this
	// code runs. Depth and the texture coordinate walk one vector at a time and
	// still need the shift tables.
	const bool needs_shift = (m_en.z && m_sel.prim != GS_SPRITE_CLASS) || m_en.t;
	if (needs_shift)
	{
		gsAsm->MOV(X4, reinterpret_cast<intptr_t>(g_const.m_shift));
		for (int i = 0; i < (m_sel.notest ? 2 : 5); i++)
		{
			gsAsm->LDR(oak::QReg(3 + i), OakMemOperand{X4, static_cast<s64>(i * sizeof(g_const.m_shift[0]))});
		}
	}

	Depth();

	Texture();

	Color();

	gsAsm->RET();

	m_size = static_cast<size_t>(emitter.offset());
	pxAssert(m_size < m_capacity);
	gsAsm = nullptr;
	oakAsm = nullptr;

	Perf::any.RegisterKey(GetCode(), GetSize(), "GSSetupPrim_", m_sel.key);
}

void GSSetupPrimCodeGenerator::Depth()
{
	if (!m_en.z && !m_en.f)
	{
		return;
	}

	if (m_sel.prim != GS_SPRITE_CLASS)
	{
		// Fog's lane table and step come from the colour walk (GSColourWalk.h).

		if (m_en.z)
		{
			// VectorF dz = VectorF::broadcast64(&dscan.p.z)
			gsAsm->ADD(_scratchaddr, _dscan, offsetof(GSVertexSW, p.z));
			gsAsm->LD1R(_vscratch.D2(), OakMemOperand{_scratchaddr, static_cast<s64>(0)});

			// m_local.d4.z = dz.mul64(GSVector4::f32to64(shift));
			gsAsm->FCVTL(Q1.D2(), oak::VReg_2S(Q3.index()));
			gsAsm->FMUL(Q1.D2(), Q1.D2(), _vscratch.D2());
			gsAsm->STR(Q1.D2(), _local(d4.z));

			gsAsm->FCVTN(oak::VReg_2S(Q0.index()), _vscratch.D2());
			gsAsm->FCVTN2(Q0.S4(), _vscratch.D2());

			for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
			{
				// m_local.d[i].z0 = dz.mul64(VectorF::f32to64(half_shift[2 * i + 2]));
				// m_local.d[i].z1 = dz.mul64(VectorF::f32to64(half_shift[2 * i + 3]));

				gsAsm->FMUL(Q1.S4(), Q0.S4(), oak::QReg(4 + i).S4());
				gsAsm->STR(Q1.S4(), _local(d[i].z));
			}
		}
	}
	else
	{
		// GSVector4 p = vertex[index[1]].p;

		gsAsm->LDRH(W4, OakMemOperand{_index, static_cast<s64>(sizeof(u16))});
		gsAsm->LSL(W4, W4, 6); // * sizeof(GSVertexSW)
		gsAsm->ADD(X4, _vertex, X4);

		if (m_en.f)
		{
			// m_local.p.f = GSVector4i(p).zzzzh().zzzz();

			gsAsm->LDR(Q0, OakMemOperand{X4, static_cast<s64>(offsetof(GSVertexSW, p))});

			gsAsm->FCVTZS(Q1.S4(), Q0.S4());
			gsAsm->DUP(Q1.H8(), Q1.H8(), 6);

			gsAsm->STR(Q1, OakMemOperand{_locals, static_cast<s64>(offsetof(GSScanlineLocalData, p.f))});
		}

		if (m_en.z)
		{
			// uint32 z is bypassed in t.w

			gsAsm->ADD(_scratchaddr, X4, offsetof(GSVertexSW, t.w));
			gsAsm->LD1R(Q0.S4(), OakMemOperand{_scratchaddr, static_cast<s64>(0)});
			gsAsm->STR(Q0, OakMemOperand{_locals, static_cast<s64>(offsetof(GSScanlineLocalData, p.z))});
		}
	}
}

void GSSetupPrimCodeGenerator::Texture()
{
	if (!m_en.t)
	{
		return;
	}

	// Twice the triangle's area in 12.4 units squared, as an exact integer, and
	// from it the mask the lag is gated on: all-ones where the trail is taken, zero
	// where the setup inverts exactly (see GSCoordinateWalk.h).
	//
	// The position lane is the 12.4 word over sixteen, so FCVTZS at four fractional
	// bits recovers it and the cross product runs in integers. The C++ reference
	// forms the same integer the same way.
	//
	// Lines and points have no area and keep the lag.
	if (m_sel.prim != GS_SPRITE_CLASS)
	{
		if (m_sel.prim == GS_TRIANGLE_CLASS)
		{
			const oak::QReg vp[3] = {Q0, Q1, Q2};

			for (int k = 0; k < 3; k++)
			{
				gsAsm->LDRH(W4, OakMemOperand{_index, static_cast<s64>(sizeof(u16) * k)});
				gsAsm->LSL(W4, W4, 6); // * sizeof(GSVertexSW)
				gsAsm->ADD(X4, _vertex, X4);
				gsAsm->LDR(vp[k], OakMemOperand{X4, static_cast<s64>(offsetof(GSVertexSW, p))});
				gsAsm->FCVTZS(oak::VReg_2S(vp[k].index()), oak::VReg_2S(vp[k].index()), 4);
			}

			// D1 = p1 - p0, D2 = p2 - p0, then the cross as (D1.x*D2.y, D1.y*D2.x)
			// and the difference of the pair.
			gsAsm->SUB(oak::VReg_2S(Q1.index()), oak::VReg_2S(Q1.index()), oak::VReg_2S(Q0.index()));
			gsAsm->SUB(oak::VReg_2S(Q2.index()), oak::VReg_2S(Q2.index()), oak::VReg_2S(Q0.index()));
			gsAsm->REV64(oak::VReg_2S(Q2.index()), oak::VReg_2S(Q2.index()));
			gsAsm->SMULL(Q0.D2(), oak::VReg_2S(Q1.index()), oak::VReg_2S(Q2.index()));
			gsAsm->EXT(Q1.B16(), Q0.B16(), Q0.B16(), 8);
			gsAsm->SUB(Q0.D2(), Q0.D2(), Q1.D2());
			gsAsm->ABS(Q0.D2(), Q0.D2());
			gsAsm->FMOV(X4, D0);

			// A power of two clears every bit below its own, and zero is not one.
			gsAsm->SUB(X5, X4, 1);
			gsAsm->TST(X4, X5);
			gsAsm->CSET(W5, oak::Cond::EQ);
			gsAsm->CMP(X4, 0);
			gsAsm->CSET(W6, oak::Cond::NE);
			gsAsm->AND(W5, W5, W6);
			gsAsm->CMP(W5, 0);
			gsAsm->CSETM(W6, oak::Cond::EQ);
		}
		else
		{
			gsAsm->MOV(W6, -1);
		}
	}

	// GSVector4 t = dscan.t;

	gsAsm->LDR(Q0, OakMemOperand{_dscan, static_cast<s64>(offsetof(GSVertexSW, t))});

	// The coordinate a triangle samples at trails the exact plane in the direction
	// the walk is going, by less than a sixteenth of a texel (see CSetupPrim in
	// GSDrawScanline.cpp). Sprites take nothing.
	//
	// A compare against zero leaves all-ones (-1) in the lanes that walk forward,
	// so negating it gives the one unit the scanline subtracts, and zero on still
	// and backward axes. The compare is on the step the walk actually takes, which
	// on the affine route is the floored one: a gradient below a grid unit per
	// pixel walks nowhere and does not trail.
	//

	if (m_sel.stqplane)
	{
		// The console's S, T and Q planes (GSPerspectivePlane.h). dscan.t holds the steps as
		// integers in units of g/2^14, already what the accumulator adds per pixel, so nothing
		// is floored here. There is no coordinate lag on this route, and the lane offsets are
		// the step times each lane's distance from the span's anchor. Every lane wraps at 32
		// bits, as the accumulator does.
		//
		// Q0 holds dscan.t.
		gsAsm->MOVI(_vscratch.S4(), 0);
		gsAsm->STR(_vscratch, _local(tclag.u));
		gsAsm->STR(_vscratch, _local(tclag.v));

		gsAsm->SHL(Q2.S4(), Q0.S4(), 2);
		gsAsm->STR(Q2, OakMemOperand{_locals, static_cast<s64>(offsetof(GSScanlineLocalData, d4.stq))});

		gsAsm->MOV(_scratchaddr, reinterpret_cast<intptr_t>(g_const.m_lane));

		for (int j = 0; j < 3; j++)
		{
			gsAsm->DUP(Q2.S4(), Q0.S4(), j);

			for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
			{
				gsAsm->LDR(_vscratch, OakMemOperand{_scratchaddr, static_cast<s64>(i * sizeof(g_const.m_lane[0]))});
				gsAsm->MUL(Q1.S4(), Q2.S4(), _vscratch.S4());

				switch (j)
				{
					case 0: gsAsm->STR(Q1, _local(d[i].s)); break;
					case 1: gsAsm->STR(Q1, _local(d[i].t)); break;
					case 2: gsAsm->STR(Q1, _local(d[i].q)); break;
				}
			}
		}

		return;
	}

	if (m_sel.uvwalk)
	{
		// The texel accumulator is seed + n * floor(step) on a 12.15 grid
		// (GSCoordinateWalk.h), so the per-pixel step is floored once, here, and every
		// lane and vector offset below is an integer multiple of it. Truncating each
		// product instead gives floor(n * step), a different sequence that does not
		// match the console.
		//
		// FCVTMS rounds toward minus infinity, as dropping the bits below a fixed-point
		// register does; the BIC puts the result on the UV grid (GS_UV_GRID_MASK).

		// GSVector4i step = GSVector4i(t.floor()) & GS_UV_GRID_MASK;
		gsAsm->FCVTMS(Q1.S4(), Q0.S4());
		gsAsm->BIC(Q1.S4(), (1 << GS_UV_GRID_SHIFT) - 1, 0);

		if (m_sel.prim != GS_SPRITE_CLASS)
		{
			// Q0 held dscan.t and is spent: the step lives in Q1 from here on, and
			// W6 carries the area gate from the top of this function.
			gsAsm->DUP(Q0.S4(), W6);

			for (int j = 0; j < 2; j++)
			{
				gsAsm->DUP(_vscratch.S4(), Q1.S4(), j);
				gsAsm->CMGT(_vscratch.S4(), _vscratch.S4(), 0);
				gsAsm->AND(_vscratch.B16(), _vscratch.B16(), Q0.B16());
				gsAsm->NEG(_vscratch.S4(), _vscratch.S4());
				gsAsm->STR(_vscratch, j == 0 ? _local(tclag.u) : _local(tclag.v));
			}
		}

		// m_local.d4.stq = step * 4;
		gsAsm->SHL(Q2.S4(), Q1.S4(), 2);
		gsAsm->STR(Q2, OakMemOperand{_locals, static_cast<s64>(offsetof(GSScanlineLocalData, d4.stq))});

		gsAsm->MOV(_scratchaddr, reinterpret_cast<intptr_t>(g_const.m_lane));

		for (int j = 0; j < 2; j++)
		{
			// GSVector4i ds = step.xxxx();
			// GSVector4i dt = step.yyyy();

			gsAsm->DUP(Q2.S4(), Q1.S4(), j);

			for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
			{
				// m_local.d[i].s/t = ds/dt * m_lane[i];

				gsAsm->LDR(_vscratch, OakMemOperand{_scratchaddr, static_cast<s64>(i * sizeof(g_const.m_lane[0]))});
				gsAsm->MUL(Q0.S4(), Q2.S4(), _vscratch.S4());

				switch (j)
				{
					case 0: gsAsm->STR(Q0, _local(d[i].s)); break;
					case 1: gsAsm->STR(Q0, _local(d[i].t)); break;
				}
			}
		}

		return;
	}

	if (m_sel.prim != GS_SPRITE_CLASS)
	{
		// A constant-Q triangle's own ST plane and a live divide take the same rule:
		// a live divide at a power-of-two area does not trail either. Q0 keeps dscan.t
		// for the step below; Q1 carries the area gate from the top of this function.
		gsAsm->DUP(Q1.S4(), W6);

		for (int j = 0; j < 2; j++)
		{
			gsAsm->DUP(_vscratch.S4(), Q0.S4(), j);
			gsAsm->FCMGT(_vscratch.S4(), _vscratch.S4(), 0.0);
			gsAsm->AND(_vscratch.B16(), _vscratch.B16(), Q1.B16());
			gsAsm->NEG(_vscratch.S4(), _vscratch.S4());
			gsAsm->STR(_vscratch, j == 0 ? _local(tclag.u) : _local(tclag.v));
		}
	}

	// The multiply is by m_shift[0], four pixels: one vector, not one block.
	// Colour and fog take the eight-wide block (GSDrawScanline::SetupColourWalkTables);
	// the coordinate does not, see GSBlockWalk.h and TheCoordinateStepStaysOneVector.
	// Taking the block step here would advance the coordinate eight pixels every four.
	//
	// A constant-Q triangle's ST plane arrives as a 16.16 integer too and the
	// scanline reads it the same way, but it is not snapped to the accumulator's
	// grid, so this path keeps the float step.
	//
	// m_local.d4.stq = GSVector4i(t * 4.0f) or t * 4.0f;
	gsAsm->FMUL(Q1.S4(), Q0.S4(), Q3.S4());

	if (m_sel.fst)
		gsAsm->FCVTZS(Q1.S4(), Q1.S4());

	gsAsm->STR(Q1, OakMemOperand{_locals, static_cast<s64>(offsetof(GSScanlineLocalData, d4.stq))});

	for (int j = 0, k = m_sel.fst ? 2 : 3; j < k; j++)
	{
		// GSVector4 ds = t.xxxx();
		// GSVector4 dt = t.yyyy();
		// GSVector4 dq = t.zzzz();

		gsAsm->DUP(Q1.S4(), Q0.S4(), j);

		for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
		{
			// m_local.d[i].s/t/q = ds/dt/dq * m_shift[i];

			gsAsm->FMUL(Q2.S4(), Q1.S4(), oak::QReg(4 + i).S4());

			if (m_sel.fst)
				gsAsm->FCVTZS(Q2.S4(), Q2.S4());

			switch (j)
			{
				case 0: gsAsm->STR(Q2, _local(d[i].s)); break;
				case 1: gsAsm->STR(Q2, _local(d[i].t)); break;
				case 2: gsAsm->STR(Q2, _local(d[i].q)); break;
			}
		}
	}
}

void GSSetupPrimCodeGenerator::Color()
{
	if (!m_en.c)
	{
		return;
	}

	if (m_sel.iip)
	{
		// A gouraud primitive's colour lane table and step are built by
		// GSDrawScanline::SetupColourWalkTables after this code runs. See GSColourWalk.h.
		return;
	}

	{
		// GSVector4i c = GSVector4i(vertex[index[last].c);

		int last = 0;

		switch (m_sel.prim)
		{
			case GS_POINT_CLASS:    last = 0; break;
			case GS_LINE_CLASS:     last = 1; break;
			case GS_TRIANGLE_CLASS: last = 2; break;
			case GS_SPRITE_CLASS:   last = 1; break;
		}

		if (!(m_sel.prim == GS_SPRITE_CLASS && (m_en.z || m_en.f))) // if this is a sprite, the last vertex was already loaded in Depth()
		{
			gsAsm->LDRH(W4, OakMemOperand{_index, static_cast<s64>(sizeof(u16) * last)});
			gsAsm->LSL(W4, W4, 6); // * sizeof(GSVertexSW)
			gsAsm->ADD(X4, _vertex, X4);
		}

		gsAsm->LDR(Q0, OakMemOperand{X4, static_cast<s64>(offsetof(GSVertexSW, c))});
		gsAsm->FCVTZS(Q0.S4(), Q0.S4());

		// c = c.upl16(c.zwxy());

		gsAsm->EXT(Q1.B16(), Q0.B16(), Q0.B16(), 8);
		gsAsm->ZIP1(Q0.H8(), Q0.H8(), Q1.H8());

		// if (!tme) c = c.srl16(7);

		if (m_sel.tfx == TFX_NONE)
			gsAsm->USHR(Q0.H8(), Q0.H8(), 7);

		// m_local.c.rb = c.xxxx();
		// m_local.c.ga = c.zzzz();

		gsAsm->DUP(Q1.S4(), Q0.S4(), 0);
		gsAsm->DUP(Q2.S4(), Q0.S4(), 2);

		gsAsm->STR(Q1, _local(c.rb));
		gsAsm->STR(Q2, _local(c.ga));
	}
}
