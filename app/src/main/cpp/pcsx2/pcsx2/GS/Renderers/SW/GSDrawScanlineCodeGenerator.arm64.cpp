// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#include "GS/Renderers/SW/GSDrawScanlineCodeGenerator.arm64.h"
#include "GS/Renderers/SW/GSBlockWalk.h"
#include "GS/Renderers/SW/GSCoordinateWalk.h"
#include "GS/Renderers/SW/GSDrawScanline.h"
#include "GS/Renderers/SW/GSVertexSW.h"
#include "GS/GSState.h"

#include "common/StringUtil.h"
#include "common/Perf.h"

#include <cstdint>

// warning : offset of on non-standard-layout type 'GSScanlineGlobalData' [-Winvalid-offsetof]
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif

MULTI_ISA_UNSHARED_IMPL;

using namespace oak::util;

static const auto& _steps = W0;
static const auto& _left = W1;
static const auto& _skip = W1;
static const auto& _top = W2;
static const auto& _v = X3;
static const auto& _locals = X4;

// X5-X9 used internally

static const auto& _globals = X10;
static const auto& _vm = X11;
static const auto& _vm_high = X12;
static const auto& _global_tex0 = X13;
static const auto& _global_clut = X14;

static const auto& _wscratch = W15;
static const auto& _xscratch = X15;
static const auto& _wscratch2 = W16;
static const auto& _xscratch2 = X16;
static const auto& _scratchaddr = X17;

static const auto& _global_dimx = X19;
static const auto& _local_aref = W20;
static const auto& _global_frb = W21;
static const auto& _global_fga = W22;
// The level-of-detail table's per-draw scalars and base (GSLevelOfDetail.h):
// 4 + TEX1.L, TEX1.K in sixteenths, and the integer ceiling. X28 is saved by
// the prologue and otherwise unused.
static const auto& _global_lodshift = W23;
static const auto& _global_lodk = W24;
static const auto& _global_mxl = W25;
static const auto& _global_lodtab = X28;

// The alternating block step's cursor. Callee-saved on purpose: X5-X9 all
// carry live values inside the loop body.
static const auto& _block_ptr = X26;
static const auto& _block_hop = X27;

static const auto& _vscratch = Q31;
static const auto& _vscratch2 = Q30;
static const auto& _vscratch3 = Q29;
static const auto& _temp_s = Q28;
static const auto& _temp_t = Q27;
static const auto& _temp_q = Q26;
static const auto& _temp_z0 = Q25;
static const auto& _temp_z1 = Q24;
static const auto& _temp_rb = Q23;
static const auto& _temp_ga = Q22;
static const auto& _temp_zs = Q21;
static const auto& _temp_zd = Q20;
static const auto& _temp_vf = Q19;
static const auto& _d4_z = Q18;
static const auto& _d4_stq = Q17;
static const auto& _d4_c = Q16;
static const auto& _global_tmin = Q15;
static const auto& _global_tmax = Q14;
static const auto& _global_tmask = Q13;
static const auto& _const_movemskw_mask = Q12;
static const auto& _temp_f = Q10;
static const auto& _d4_f = Q9;

static const auto& _test = Q8;
static const auto& _fd = Q2;

// Yay, you can't offsetof with non-constant array indices in GCC
#define OFFSETOF(base, field) (reinterpret_cast<uptr>(&reinterpret_cast<base*>(0)->field))
#define _local(field) OakMemOperand{_locals, static_cast<s64>(OFFSETOF(GSScanlineLocalData, field))}
#define _global(field) OakMemOperand{_globals, static_cast<s64>(OFFSETOF(GSScanlineGlobalData, field))}
#include "GSOakScanlineEmitter.h"
static thread_local GSDrawOakEmitter* gsAsm;

GSDrawScanlineCodeGenerator::GSDrawScanlineCodeGenerator(u64 key, void* code, size_t maxsize)
	: m_code(static_cast<u8*>(code))
	, m_capacity(maxsize)
	, m_sel(key)
{
	m_block_split = GSBlockWalkIsSplit(4);
}

void GSDrawScanlineCodeGenerator::Generate()
{
	pxAssert(!oakAsm);
	oak::CodeGenerator emitter(reinterpret_cast<u32*>(m_code));
	GSDrawOakEmitter gs_emitter(emitter);
	oakAsm = &emitter;
	gsAsm = &gs_emitter;
	if (m_sel.breakpoint)
		gsAsm->BRK(1);

	if (GSDrawScanline::ShouldUseCDrawScanline(m_sel.key))
	{
		gsAsm->MOV(X15, reinterpret_cast<uintptr_t>(
											static_cast<void (*)(int, int, int, const GSVertexSW&, GSScanlineLocalData&)>(
												&GSDrawScanline::CDrawScanline)));
		gsAsm->BR(X15);
		m_size = static_cast<size_t>(emitter.offset());
		pxAssert(m_size < m_capacity);
		gsAsm = nullptr;
		oakAsm = nullptr;
		return;
	}

	gsAsm->SUB(SP, SP, 144);
	gsAsm->STP(X19, X20, SP, oak::SOffset<10,3>(0));
	gsAsm->STP(X21, X22, SP, oak::SOffset<10,3>(16));
	gsAsm->STP(X23, X24, SP, oak::SOffset<10,3>(32));
	gsAsm->STP(X25, X26, SP, oak::SOffset<10,3>(48));
	gsAsm->STP(D8, D9, SP, oak::SOffset<10,3>(64));
	gsAsm->STP(D10, D11, SP, oak::SOffset<10,3>(80));
	gsAsm->STP(D12, D13, SP, oak::SOffset<10,3>(96));
	gsAsm->STP(D14, D15, SP, oak::SOffset<10,3>(112));
	gsAsm->STP(X27, X28, SP, oak::SOffset<10,3>(128));

	gsAsm->LDR(_globals, _local(gd));
	gsAsm->LDR(_vm, _global(vm));
	gsAsm->ADD(_vm_high, _vm, 8 * 2);

	Init();

	oak::Label loop;
	gsAsm->l(loop);

	bool tme = m_sel.tfx != TFX_NONE;

	TestZ(tme ? Q5 : Q2, tme ? Q6 : Q3);

	if (m_sel.mmin)
		SampleTextureLOD();
	else
		SampleTexture();

	AlphaTFX();

	ReadMask();

	TestAlpha();

	ColorTFX();

	Fog();

	ReadFrame();

	TestDestAlpha();

	WriteMask();

	WriteZBuf();

	AlphaBlend();

	WriteFrame();

	oak::Label exit;
	gsAsm->l(m_step_label);

	// if (steps <= 0) break;

	if (!m_sel.edge)
	{
		gsAsm->CMP(_steps, 0);
		gsAsm->B(oak::Cond::LE, exit);

		Step();

		gsAsm->B(loop);
	}

	gsAsm->l(exit);

	gsAsm->LDP(X27, X28, SP, oak::SOffset<10,3>(128));
	gsAsm->LDP(D14, D15, SP, oak::SOffset<10,3>(112));
	gsAsm->LDP(D12, D13, SP, oak::SOffset<10,3>(96));
	gsAsm->LDP(D10, D11, SP, oak::SOffset<10,3>(80));
	gsAsm->LDP(D8, D9, SP, oak::SOffset<10,3>(64));
	gsAsm->LDP(X25, X26, SP, oak::SOffset<10,3>(48));
	gsAsm->LDP(X23, X24, SP, oak::SOffset<10,3>(32));
	gsAsm->LDP(X21, X22, SP, oak::SOffset<10,3>(16));
	gsAsm->LDP(X19, X20, SP, oak::SOffset<10,3>(0));
	gsAsm->ADD(SP, SP, 144);

	gsAsm->RET();

	m_size = static_cast<size_t>(emitter.offset());
	pxAssert(m_size < m_capacity);
	gsAsm = nullptr;
	oakAsm = nullptr;

	Perf::any.RegisterKey(GetCode(), GetSize(), "GSDrawScanline_", m_sel.key);
}

void GSDrawScanlineCodeGenerator::Init()
{
	// Where this span starts inside its eight-pixel block indexes the colour and
	// fog lane tables, and on a four-lane host decides which of the two alternating
	// steps the walk begins on. Read off the true left edge, before vector
	// alignment rounds it down. See GSColourWalk.h.
	gsAsm->AND(W9, _left, 7);

	if (!m_sel.notest)
	{
		// int skip = left & 3;

		gsAsm->MOV(W6, _left);
		gsAsm->AND(_left, _left, 3);

		// int steps = pixels + skip - 4;

		gsAsm->ADD(_steps, _steps, _left);
		gsAsm->SUB(_steps, _steps, 4);

		// left -= skip;

		gsAsm->SUB(W6, W6, _left);

		// GSVector4i test = m_test[skip] | m_test[7 + (steps & (steps >> 31))];

		gsAsm->LSL(_left, _left, 4);

		gsAsm->ADD(_scratchaddr, _globals, offsetof(GSScanlineGlobalData, const_test_128b[0]));
		gsAsm->LDR(_test, _scratchaddr, X1);

		gsAsm->ASR(W5, _steps, 31);
		gsAsm->AND(W5, W5, _steps);
		gsAsm->LSL(W5, W5, 4);

		gsAsm->ADD(_scratchaddr, _globals, offsetof(GSScanlineGlobalData, const_test_128b[7]));
		gsAsm->LDR(_vscratch, _scratchaddr, W5, oak::IndexExt::SXTW);
		gsAsm->ORR(_test.B16(), _test.B16(), _vscratch.B16());
	}
	else
	{
		gsAsm->MOV(W6, _left); // left
		gsAsm->MOV(_skip, WZR); // skip
		gsAsm->SUB(_steps, _steps, 4); // steps
	}

	// GSVector2i* fza_base = &m_local.gd->fzbr[top];

	gsAsm->LDR(_scratchaddr, _global(fzbr));
	gsAsm->LSL(W7, _top, 3); // *8
	gsAsm->ADD(X7, _scratchaddr, X7);

	// GSVector2i* fza_offset = &m_local.gd->fzbc[left >> 2];
	gsAsm->LDR(_scratchaddr, _global(fzbc));
	gsAsm->LSL(W8, W6, 1); // *2
	gsAsm->ADD(X8, _scratchaddr, X8);

	// X26 = &m_local.d[left & 7]. The colour and fog inits below read their lane
	// offsets from there; at the end of Init it moves to &m_local.dw[left & 7][0],
	// where Step walks the alternating pair. X27 is the signed hop to the other
	// phase, so stepping is one add and one negate with no alignment assumption.
	//
	// The shift is the row stride of both tables, hard-coded. A fifth GSVector4i in
	// skip or blockstep, or a third phase, would silently index the wrong row.
	static_assert(sizeof(GSScanlineLocalData::skip) == 128);
	static_assert(sizeof(GSScanlineLocalData::blockstep) == 64);
	static_assert(sizeof(GSScanlineLocalData::dw[0]) == 128);
	static_assert(offsetof(GSScanlineLocalData, d) == 0);
	gsAsm->ADD(_block_ptr, _locals, X9, oak::AddSubShift::LSL, 7);

	if (m_block_split)
		gsAsm->MOV(_block_hop, sizeof(GSScanlineLocalData::blockstep));

	// W1 = &m_local.d[skip], for the lanes indexed by the span's position inside
	// the vector: depth and the texture coordinate. Colour and fog use _block_ptr.
	if ((m_sel.prim != GS_SPRITE_CLASS && m_sel.zb) || (m_sel.fb && m_sel.tfx != TFX_NONE))
	{
		gsAsm->LSL(W1, W1, 3); // *8
		gsAsm->ADD(X1, X1, _locals);
		static_assert(offsetof(GSScanlineLocalData, d) == 0);
	}

	if (m_sel.prim != GS_SPRITE_CLASS)
	{
		if ((m_sel.fwrite && m_sel.fge) || m_sel.zb)
		{
			gsAsm->LDR(_d4_z, _local(d4.z));

			if (m_sel.fwrite && m_sel.fge)
			{
				// f = GSVector4i(v.t).zzzzh().zzzz().add16(m_local.d[left & 7].f);
				gsAsm->LDR(oak::SReg(_temp_f.index()), OakMemOperand{_v, static_cast<s64>(offsetof(GSVertexSW, t.w))});
				gsAsm->LDR(_vscratch, OakMemOperand{_block_ptr, static_cast<s64>(offsetof(GSScanlineLocalData::skip, f))});
				if (!m_block_split)
					gsAsm->LDR(_d4_f, _local(d4.f));

				gsAsm->FCVTZS(oak::SReg(_temp_f.index()), oak::SReg(_temp_f.index()));
				gsAsm->DUP(_temp_f.H8(), _temp_f.H8(), 0);
				gsAsm->ADD(_temp_f.H8(), _temp_f.H8(), _vscratch.H8());
			}

			if (m_sel.zb && m_sel.zequal)
			{
				gsAsm->LDR(oak::DReg(_temp_z0.index()), OakMemOperand{_v, static_cast<s64>(offsetof(GSVertexSW, p.z))});
				gsAsm->FCVTZS(oak::DReg(_temp_z0.index()), oak::DReg(_temp_z0.index()));
				gsAsm->DUP(_temp_z0.S4(), _temp_z0.S4(), 0);
			}
			else if (m_sel.zb)
			{
				// z = vp.zzzz() + m_local.d[skip].z;
				gsAsm->ADD(_scratchaddr, _v, offsetof(GSVertexSW, p.z));
				gsAsm->LDR(_vscratch, OakMemOperand{X1, static_cast<s64>(offsetof(GSScanlineLocalData::skip, z))});
				gsAsm->LD1R(_vscratch2.D2(), OakMemOperand{_scratchaddr, static_cast<s64>(0)});

				// low
				gsAsm->FCVTL(_temp_z0.D2(), oak::VReg_2S(_vscratch.index()));
				gsAsm->FADD(_temp_z0.D2(), _temp_z0.D2(), _vscratch2.D2());

				// high
				gsAsm->FCVTL2(_temp_z1.D2(), _vscratch.S4());
				gsAsm->FADD(_temp_z1.D2(), _temp_z1.D2(), _vscratch2.D2());
			}
		}
	}
	else
	{
		if (m_sel.ztest || m_sel.zwrite)
		{
			gsAsm->LDR(_temp_z0, _local(p.z));
		}

		if (m_sel.fwrite && m_sel.fge)
		{
			gsAsm->LDR(_temp_f, _local(p.f));
		}
	}

	if (m_sel.fb)
	{
		if (m_sel.edge)
		{
			gsAsm->ADD(_scratchaddr, _v, offsetof(GSVertexSW, p.x));
			gsAsm->LD1R(Q3.H8(), OakMemOperand{_scratchaddr, static_cast<s64>(0)});
		}

		if (m_sel.tfx != TFX_NONE)
		{
			gsAsm->LDR(Q4, OakMemOperand{_v, static_cast<s64>(offsetof(GSVertexSW, t))});
		}

		if (m_sel.edge)
		{
			// m_local.temp.cov = GSVector8i::broadcast16(GSVector4i::cast(scan.p)).srl16(9);
			gsAsm->USHR(Q3.H8(), Q3.H8(), 9);
			gsAsm->STR(Q3, _local(temp.cov));
		}

		if (m_sel.tfx != TFX_NONE)
		{
			if (m_sel.fst)
			{
				// GSVector4i vti = GSVector4i(vt.floor()) & GS_UV_GRID_MASK;
				//
				// A span that walks the accumulator seeds on its 12.15 grid, floored below
				// the exact plane (GSCoordinateWalk.h). FCVTMS is the floor; the BIC is the
				// grid. A triangle's own ST plane keeps the truncating conversion.

				if (m_sel.uvwalk)
				{
					gsAsm->FCVTMS(Q6.S4(), Q4.S4());
					gsAsm->BIC(Q6.S4(), (1 << GS_UV_GRID_SHIFT) - 1, 0);
				}
				else
				{
					gsAsm->FCVTZS(Q6.S4(), Q4.S4());
				}

				// s = vti.xxxx() + m_local.d[skip].s;
				// t = vti.yyyy(); if (!sprite) t += m_local.d[skip].t;

				gsAsm->DUP(_temp_s.S4(), Q6.S4(), 0);
				gsAsm->DUP(_temp_t.S4(), Q6.S4(), 1);

				gsAsm->LDR(_vscratch, OakMemOperand{X1, static_cast<s64>(offsetof(GSScanlineLocalData::skip, s))});
				gsAsm->ADD(_temp_s.S4(), _temp_s.S4(), _vscratch.S4());

				if (m_sel.prim != GS_SPRITE_CLASS || m_sel.mmin)
				{
					gsAsm->LDR(_vscratch, OakMemOperand{X1, static_cast<s64>(offsetof(GSScanlineLocalData::skip, t))});
					gsAsm->ADD(_temp_t.S4(), _temp_t.S4(), _vscratch.S4());
				}
				else
				{
					if (m_sel.ltf)
					{
						// vf = GSTruncateCoordinate(v).xxzzlh().srl16(12);
						//
						// The sixteenth index truncates toward zero, so a negative coordinate takes
						// a sixteenth less one first. A sprite's V never steps, so the weight is
						// taken once here while the loop re-forms the index from t; both must read
						// the same sixteenth, so the bias goes to the weight's register and t keeps
						// the raw coordinate.
						gsAsm->SSHR(_temp_vf.S4(), _temp_t.S4(), 31);
						gsAsm->USHR(_temp_vf.S4(), _temp_vf.S4(), 20);
						gsAsm->ADD(_temp_vf.S4(), _temp_t.S4(), _temp_vf.S4());

						// The weight and the index have to read the same 12.4 field,
						// and the loop saturates the index it re-forms from t.
						SaturateCoordinate(_temp_vf, _vscratch);

						gsAsm->TRN1(_temp_vf.H8(), _temp_vf.H8(), _temp_vf.H8());
						gsAsm->USHR(_temp_vf.H8(), _temp_vf.H8(), 12);
					}
				}
			}
			else
			{
				// s = vt.xxxx() + m_local.d[skip].s;
				// t = vt.yyyy() + m_local.d[skip].t;
				// q = vt.zzzz() + m_local.d[skip].q;

				gsAsm->LDR(_temp_s, OakMemOperand{X1, static_cast<s64>(offsetof(GSScanlineLocalData::skip, s))});
				gsAsm->LDR(_temp_t, OakMemOperand{X1, static_cast<s64>(offsetof(GSScanlineLocalData::skip, t))});
				gsAsm->LDR(_temp_q, OakMemOperand{X1, static_cast<s64>(offsetof(GSScanlineLocalData::skip, q))});

				gsAsm->DUP(Q2.S4(), Q4.S4(), 0);
				gsAsm->DUP(Q3.S4(), Q4.S4(), 1);
				gsAsm->DUP(Q4.S4(), Q4.S4(), 2);

				if (m_sel.stqplane)
				{
					// Integer accumulators in units of g/2^14 (GSPerspectivePlane.h); the
					// seed and the lane offsets are integers in the same lanes.
					gsAsm->ADD(_temp_s.S4(), Q2.S4(), _temp_s.S4());
					gsAsm->ADD(_temp_t.S4(), Q3.S4(), _temp_t.S4());
					gsAsm->ADD(_temp_q.S4(), Q4.S4(), _temp_q.S4());
				}
				else
				{
					gsAsm->FADD(_temp_s.S4(), Q2.S4(), _temp_s.S4());
					gsAsm->FADD(_temp_t.S4(), Q3.S4(), _temp_t.S4());
					gsAsm->FADD(_temp_q.S4(), Q4.S4(), _temp_q.S4());
				}
			}

			gsAsm->LDR(_d4_stq, _local(d4.stq));
			gsAsm->LDR(_global_tmin, _global(t.min));
			gsAsm->LDR(_global_tmax, _global(t.max));
			gsAsm->LDR(_global_tmask, _global(t.mask));

			if (!m_sel.mmin)
				gsAsm->LDR(_global_tex0, _global(tex[0]));
			else
				gsAsm->ADD(_global_tex0, _globals, offsetof(GSScanlineGlobalData, tex));

			if (m_sel.tlu)
				gsAsm->LDR(_global_clut, _global(clut));
		}

		if (!(m_sel.tfx == TFX_DECAL && m_sel.tcc))
		{
			if (m_sel.iip)
			{
				// GSVector4i vc = GSVector4i(v.c);

				gsAsm->LDR(Q6, OakMemOperand{_v, static_cast<s64>(offsetof(GSVertexSW, c))});
				gsAsm->LDR(Q1, OakMemOperand{_block_ptr, static_cast<s64>(offsetof(GSScanlineLocalData::skip, rb))});
				gsAsm->LDR(_vscratch, OakMemOperand{_block_ptr, static_cast<s64>(offsetof(GSScanlineLocalData::skip, ga))});
				gsAsm->FCVTZS(Q6.S4(), Q6.S4());

				// vc = vc.upl16(vc.zwxy());

				gsAsm->EXT(Q5.B16(), Q6.B16(), Q6.B16(), 8);
				gsAsm->ZIP1(Q6.H8(), Q6.H8(), Q5.H8());

				// rb = vc.xxxx().add16(m_local.d[skip].rb);
				// ga = vc.zzzz().add16(m_local.d[skip].ga);

				gsAsm->DUP(_temp_rb.S4(), Q6.S4(), 0);
				gsAsm->DUP(_temp_ga.S4(), Q6.S4(), 2);

				gsAsm->ADD(_temp_rb.H8(), _temp_rb.H8(), Q1.H8());
				gsAsm->ADD(_temp_ga.H8(), _temp_ga.H8(), _vscratch.H8());

				if (!m_block_split)
					gsAsm->LDR(_d4_c, _local(d4.c));
			}
			else
			{
				gsAsm->LDR(_temp_rb, _local(c.rb));
				gsAsm->LDR(_temp_ga, _local(c.ga));
			}
		}
	}

	if (m_sel.atst != ATST_ALWAYS && m_sel.atst != ATST_NEVER)
	{
		gsAsm->LDR(_local_aref, _global(aref));
	}

	if (m_sel.fwrite && m_sel.fge)
	{
		gsAsm->LDR(_global_frb, _global(frb));
		gsAsm->LDR(_global_fga, _global(fga));
	}

	if (!m_sel.notest)
		gsAsm->LDR(_const_movemskw_mask, _global(const_movemaskw_mask));

	if (m_sel.mmin && !m_sel.lcm)
	{
		gsAsm->LDR(_global_lodshift, _global(lodshift));
		gsAsm->LDR(_global_lodk, _global(lodk));
		gsAsm->LDR(_global_mxl, _global(lodmxl));
		gsAsm->LDR(_global_lodtab, _global(lodtab));
	}

	// Every colour destination is dithered, not just 16-bit ones (see WriteFrame
	// in GSDrawScanline.cpp). fmt 3 is not a frame-buffer format.
	if (m_sel.dthe && m_sel.fpsm != 3)
		gsAsm->LDR(_global_dimx, _global(dimx));

	// Lane-offset loads are done; hand the cursor to Step. dw has the same row
	// stride as d, so this is one add.
	if (m_block_split)
		gsAsm->ADD(_block_ptr, _block_ptr, OFFSETOF(GSScanlineLocalData, dw));
}

void GSDrawScanlineCodeGenerator::Step()
{
	// steps -= 4;

	gsAsm->SUB(_steps, _steps, 4);

	// fza_offset++;

	gsAsm->ADD(X8, X8, 8);

	if (m_sel.prim != GS_SPRITE_CLASS)
	{
		// z += m_local.d4.z;

		if (m_sel.zb && !m_sel.zequal)
		{
			gsAsm->FADD(_temp_z1.D2(), _temp_z1.D2(), _d4_z.D2());
			gsAsm->FADD(_temp_z0.D2(), _temp_z0.D2(), _d4_z.D2());
		}

		// f = f.add16(m_local.d4.f);

		if (m_sel.fwrite && m_sel.fge)
		{
			if (m_block_split)
			{
				gsAsm->LDR(_vscratch, OakMemOperand{_block_ptr, static_cast<s64>(offsetof(GSScanlineLocalData::blockstep, f))});
				gsAsm->ADD(_temp_f.H8(), _temp_f.H8(), _vscratch.H8());
			}
			else
			{
				gsAsm->ADD(_temp_f.H8(), _temp_f.H8(), _d4_f.H8());
			}
		}
	}

	if (m_sel.fb)
	{
		if (m_sel.tfx != TFX_NONE)
		{
			if (m_sel.fst)
			{
				// GSVector4i stq = m_local.d4.stq;

				// s += stq.xxxx();
				// if (!sprite) t += stq.yyyy();

				gsAsm->DUP(_vscratch.S4(), _d4_stq.S4(), 0);
				if (m_sel.prim != GS_SPRITE_CLASS || m_sel.mmin)
					gsAsm->DUP(_vscratch2.S4(), _d4_stq.S4(), 1);

				gsAsm->ADD(_temp_s.S4(), _temp_s.S4(), _vscratch.S4());

				if (m_sel.prim != GS_SPRITE_CLASS || m_sel.mmin)
					gsAsm->ADD(_temp_t.S4(), _temp_t.S4(), _vscratch2.S4());
			}
			else
			{
				// GSVector4 stq = m_local.d4.stq;

				// s += stq.xxxx();
				// t += stq.yyyy();
				// q += stq.zzzz();

				gsAsm->DUP(_vscratch.S4(), _d4_stq.S4(), 0);
				gsAsm->DUP(_vscratch2.S4(), _d4_stq.S4(), 1);
				gsAsm->DUP(Q1.S4(), _d4_stq.S4(), 2);

				if (m_sel.stqplane)
				{
					gsAsm->ADD(_temp_s.S4(), _temp_s.S4(), _vscratch.S4());
					gsAsm->ADD(_temp_t.S4(), _temp_t.S4(), _vscratch2.S4());
					gsAsm->ADD(_temp_q.S4(), _temp_q.S4(), Q1.S4());
				}
				else
				{
					gsAsm->FADD(_temp_s.S4(), _temp_s.S4(), _vscratch.S4());
					gsAsm->FADD(_temp_t.S4(), _temp_t.S4(), _vscratch2.S4());
					gsAsm->FADD(_temp_q.S4(), _temp_q.S4(), Q1.S4());
				}
			}
		}

		if (!(m_sel.tfx == TFX_DECAL && m_sel.tcc))
		{
			if (m_sel.iip)
			{
				// GSVector4i c = m_local.d4.c;

				// rb = rb.add16(c.xxxx());
				// ga = ga.add16(c.yyyy());

				if (m_block_split)
				{
					gsAsm->LDR(_vscratch, OakMemOperand{_block_ptr, static_cast<s64>(offsetof(GSScanlineLocalData::blockstep, rb))});
					gsAsm->LDR(_vscratch2, OakMemOperand{_block_ptr, static_cast<s64>(offsetof(GSScanlineLocalData::blockstep, ga))});
				}
				else
				{
					gsAsm->DUP(_vscratch.S4(), _d4_c.S4(), 0);
					gsAsm->DUP(_vscratch2.S4(), _d4_c.S4(), 1);
				}
				gsAsm->MOVI(Q1.H8(), 0);

				gsAsm->ADD(_temp_rb.H8(), _temp_rb.H8(), _vscratch.H8());
				gsAsm->ADD(_temp_ga.H8(), _temp_ga.H8(), _vscratch2.H8());

				// FIXME: color may underflow and roll over at the end of the line, if decreasing

				gsAsm->SMAX(_temp_rb.H8(), _temp_rb.H8(), Q1.H8());
				gsAsm->SMAX(_temp_ga.H8(), _temp_ga.H8(), Q1.H8());
			}
		}
	}

	if (m_block_split)
	{
		// Hop to the other phase of the pair.
		gsAsm->ADD(_block_ptr, _block_ptr, _block_hop);
		gsAsm->NEG(_block_hop, _block_hop);
	}

	if (!m_sel.notest)
	{
		// test = m_test[7 + (steps & (steps >> 31))];

		gsAsm->ASR(W1, _steps, 31);
		gsAsm->AND(W1, W1, _steps);
		gsAsm->LSL(W1, W1, 4);

		gsAsm->ADD(_scratchaddr, _globals, offsetof(GSScanlineGlobalData, const_test_128b[7]));
		gsAsm->LDR(_test, _scratchaddr, W1, oak::IndexExt::SXTW);
	}
}

void GSDrawScanlineCodeGenerator::TestZ(oak::QReg temp1, oak::QReg temp2)
{
	if (!m_sel.zb)
	{
		return;
	}

	// int za = fza_base.y + fza_offset->y;

	gsAsm->LDR(W9, OakMemOperand{X7, static_cast<s64>(4)});
	gsAsm->LDR(_wscratch, OakMemOperand{X8, static_cast<s64>(4)});
	gsAsm->ADD(W9, W9, _wscratch);
	gsAsm->AND(W9, W9, HALF_VM_SIZE - 1);

	// GSVector4i zs = zi;

	oak::QReg zs(0);
	if (m_sel.prim != GS_SPRITE_CLASS)
	{
		if (m_sel.zequal)
		{
			zs = _temp_z0;
		}
		else if (m_sel.zoverflow)
		{
			// GSVector4i zl = z0.add64(VectorF::m_xc1e00000000fffff).f64toi32();
			// GSVector4i zh = z1.add64(VectorF::m_xc1e00000000fffff).f64toi32();

			gsAsm->MOVI(temp1.D2(), GSVector4::m_xc1e00000000fffff.U64[0]);
			gsAsm->FADD(_temp_z0.D2(), _temp_z0.D2(), temp1.D2());
			gsAsm->FADD(_temp_z1.D2(), _temp_z1.D2(), temp1.D2());

			// zs = GSVector8i(zl, zh);
			gsAsm->FCVTZS(Q0.D2(), _temp_z0.D2());
			gsAsm->FCVTZS(temp1.D2(), _temp_z1.D2());
			gsAsm->MOVI(_vscratch.S4(), 0x80000000);
			gsAsm->UZP1(Q0.S4(), Q0.S4(), temp1.S4());

			// zs += VectorI::X80000000();
			gsAsm->ADD(Q0.S4(), Q0.S4(), _vscratch.S4());
			zs = Q0;
		}
		else
		{
			// zs = GSVector8i(z0.f64toi32(), z1.f64toi32());

			gsAsm->FCVTZS(Q0.D2(), _temp_z0.D2());
			gsAsm->FCVTZS(temp1.D2(), _temp_z1.D2());
			gsAsm->UZP1(Q0.S4(), Q0.S4(), temp1.S4());
			zs = Q0;
		}


		// Clamp Z to ZPSM_FMT_MAX
		if (m_sel.zclamp)
		{
			gsAsm->MOVI(temp1.S4(), 0xFFFFFFFFu >> ((m_sel.zpsm & 0x3) * 8));
			gsAsm->UMIN(Q0.S4(), zs.S4(), temp1.S4());
			zs = Q0;
		}

		if (m_sel.zwrite)
			gsAsm->MOV(_temp_zs, zs);
	}
	else
	{
		zs = _temp_z0;
	}

	if (m_sel.ztest)
	{
		oak::QReg zd(_temp_zd);
		ReadPixel(zd, W9);

		// zd &= 0xffffffff >> m_sel.zpsm * 8;

		if (m_sel.zpsm)
		{
			gsAsm->SHL(Q1.S4(), zd.S4(), m_sel.zpsm * 8);
			gsAsm->USHR(Q1.S4(), Q1.S4(), m_sel.zpsm * 8);
			zd = Q1;
		}

		if (m_sel.zpsm == 0)
		{
			// GSVector4i o = GSVector4i::X80000000();
			gsAsm->MOVI(temp1.S4(), 0x80000000u);

			// GSVector4i zso = zs - o;
			// GSVector4i zdo = zd - o;
			gsAsm->SUB(Q0.S4(), zs.S4(), temp1.S4());
			gsAsm->SUB(Q1.S4(), zd.S4(), temp1.S4());
			zs = Q0;
			zd = Q1;
		}

		switch (m_sel.ztst)
		{
			case ZTST_GEQUAL:
				// test |= zso < zdo; // ~(zso >= zdo)
				gsAsm->CMGT(Q1.S4(), zd.S4(), zs.S4());
				gsAsm->ORR(_test.B16(), _test.B16(), Q1.B16());
				break;

			case ZTST_GREATER: // TODO: tidus hair and chocobo wings only appear fully when this is tested as ZTST_GEQUAL
				// test |= zso <= zdo; // ~(zso > zdo)
				gsAsm->CMGT(Q0.S4(), zs.S4(), zd.S4());
				gsAsm->MVN(Q0.B16(), Q0.B16());
				gsAsm->ORR(_test.B16(), _test.B16(), Q0.B16());
				break;
		}

		alltrue(_test, temp1);
	}
}

// The formed coordinate saturates into a signed 12.4 field (GSCoordinateWalk.h):
// take the sixteenth, saturate it to sixteen signed bits, put it back. The bits
// below the sixteenth go with it; nothing downstream reads them (the texel is
// bits 16 and up, the weight bits 12 to 15).
//
// SQXTN is the saturation; the C++ reference's min/max form computes the same
// function.
void GSDrawScanlineCodeGenerator::SaturateCoordinate(oak::QReg c, oak::QReg scratch)
{
	gsAsm->SSHR(scratch.S4(), c.S4(), GS_COORD_SIXTEENTH_SHIFT);
	gsAsm->SQXTN(oak::VReg_4H(scratch.index()), scratch.S4());
	gsAsm->SXTL(scratch.S4(), oak::VReg_4H(scratch.index()));
	gsAsm->SHL(c.S4(), scratch.S4(), GS_COORD_SIXTEENTH_SHIFT);
}

void GSDrawScanlineCodeGenerator::SampleTexture()
{
	if (!m_sel.fb || m_sel.tfx == TFX_NONE)
	{
		return;
	}

	const auto& uf = Q4;
	const auto& vf = Q7;

	oak::QReg ureg = _temp_s;
	oak::QReg vreg = _temp_t;

	// All-ones in every lane whose pixel takes the linear filter. lod > 0 is
	// exactly Q < the crossing constant, so the per-pixel MMAG/MMIN choice is one
	// compare. Cheap enough to emit twice rather than hold a register.
	const auto emit_ltfx_mask = [this](oak::QReg dst) {
		oak::QReg q = _temp_q;

		if (m_sel.stqplane)
		{
			// Q as a float: the count of g/4 times 2^(E - 16) (GSPerspectivePlane.h).
			gsAsm->SSHR(_vscratch.S4(), _temp_q.S4(), GS_PLANE_PIXEL_SHIFT);
			gsAsm->SCVTF(_vscratch.S4(), _vscratch.S4());
			gsAsm->LDR(_vscratch2, _local(pwalk.qscale));
			gsAsm->FMUL(_vscratch.S4(), _vscratch.S4(), _vscratch2.S4());
			q = _vscratch;
		}

		gsAsm->LDR(dst, _global(ltfx_q));
		gsAsm->FCMGT(dst.S4(), dst.S4(), q.S4());

		if (m_sel.ltfx_ge)
			gsAsm->MVN(dst.B16(), dst.B16());
	};

	if (!m_sel.fst)
	{
		// Multiply by a reciprocal truncated to fourteen mantissa bits, not a divide.
		// Clearing the low nine bits of the float32 mantissa is that grid; two BICs
		// rather than a shift pair so the sign survives a negative Q. See
		// GSDrawScanline.cpp for why the width is fourteen.
		if (m_sel.stqplane)
		{
			// Floor each g/2^14 accumulator to a count of g/4 (the shift) and convert, so S/Q is
			// a quotient of two counts and the power of two that takes it to 16.16 texels
			// (2^(16 + TW), 2^(16 + TH)) rides on the product. The reciprocal is taken of the
			// count because cutting its mantissa commutes with a power-of-two scale.
			gsAsm->SSHR(Q0.S4(), _temp_q.S4(), GS_PLANE_PIXEL_SHIFT);
			gsAsm->SCVTF(Q0.S4(), Q0.S4());
			gsAsm->FMOV(Q1.S4(), oak::FImm8(0x70));
			gsAsm->FDIV(Q0.S4(), Q1.S4(), Q0.S4());
			gsAsm->MOVI(Q1.S4(), GS_RECIP_ROUND_UP);
			gsAsm->ADD(Q0.S4(), Q0.S4(), Q1.S4());
			gsAsm->BIC(Q0.S4(), 0xff, 0);

			gsAsm->LDR(Q1, _local(pwalk.stscale));
			gsAsm->SSHR(Q2.S4(), _temp_s.S4(), GS_PLANE_PIXEL_SHIFT);
			gsAsm->SSHR(Q3.S4(), _temp_t.S4(), GS_PLANE_PIXEL_SHIFT);
			gsAsm->SCVTF(Q2.S4(), Q2.S4());
			gsAsm->SCVTF(Q3.S4(), Q3.S4());
			gsAsm->FMUL(Q2.S4(), Q2.S4(), Q0.S4());
			gsAsm->FMUL(Q3.S4(), Q3.S4(), Q0.S4());
			gsAsm->FMUL(Q2.S4(), Q2.S4(), oak::SReg(Q1.index()), 0);
			gsAsm->FMUL(Q3.S4(), Q3.S4(), oak::SReg(Q1.index()), 1);
		}
		else
		{
			gsAsm->FMOV(Q0.S4(), oak::FImm8(0x70));
			gsAsm->FDIV(Q0.S4(), Q0.S4(), _temp_q.S4());
			gsAsm->MOVI(Q1.S4(), GS_RECIP_ROUND_UP);
			gsAsm->ADD(Q0.S4(), Q0.S4(), Q1.S4());
			gsAsm->BIC(Q0.S4(), 0xff, 0);

			gsAsm->FMUL(Q2.S4(), _temp_s.S4(), Q0.S4());
			gsAsm->FMUL(Q3.S4(), _temp_t.S4(), Q0.S4());
		}

		ureg = Q2;
		vreg = Q3;

		gsAsm->FCVTZS(Q2.S4(), Q2.S4());
		gsAsm->FCVTZS(Q3.S4(), Q3.S4());
	}

	// The coordinate DDA's lag: one 16.16 unit on an axis that walks forward, zero
	// on one that is still or walks back. See GSDrawScanline.cpp. On the FST side
	// ureg is the live accumulator, so the biased copy goes to the scratch pair the
	// packing below consumes anyway.
	if (m_sel.prim != GS_SPRITE_CLASS)
	{
		gsAsm->LDR(Q0, _local(tclag.u));
		gsAsm->SUB(Q2.S4(), ureg.S4(), Q0.S4());
		gsAsm->LDR(Q0, _local(tclag.v));
		gsAsm->SUB(Q3.S4(), vreg.S4(), Q0.S4());
		ureg = Q2;
		vreg = Q3;
	}

	// GSTruncateCoordinate: the sixteenth index truncates toward zero, so a
	// negative coordinate takes a sixteenth less one before the shifts below floor
	// it into a texel and a weight. The result goes to the scratch pair, so this
	// costs no register even where ureg is the live FST accumulator.
	gsAsm->SSHR(Q0.S4(), ureg.S4(), 31);
	gsAsm->SSHR(Q1.S4(), vreg.S4(), 31);
	gsAsm->USHR(Q0.S4(), Q0.S4(), 20);
	gsAsm->USHR(Q1.S4(), Q1.S4(), 20);
	gsAsm->ADD(Q2.S4(), ureg.S4(), Q0.S4());
	gsAsm->ADD(Q3.S4(), vreg.S4(), Q1.S4());
	ureg = Q2;
	vreg = Q3;

	if (!m_sel.fst && m_sel.ltf)
	{
		// u -= 0x8000;
		// v -= 0x8000;
		//
		// Exactly eight sixteenths, so it moves the texel index and never the
		// weight. It is our step onto the tap pair, not part of the hardware
		// coordinate, so it comes after the truncation. It applies only to pixels
		// that filter linearly; the two filters do not sample the same point.
		gsAsm->MOVI(Q1.S4(), 0x8000);

		if (m_sel.ltfx)
		{
			emit_ltfx_mask(Q0);
			gsAsm->AND(Q1.B16(), Q1.B16(), Q0.B16());
		}

		gsAsm->SUB(ureg.S4(), ureg.S4(), Q1.S4());
		gsAsm->SUB(vreg.S4(), vreg.S4(), Q1.S4());
	}

	// The 12.4 field, taken after the half-texel step (so the top of the field
	// reads weight 15, not the 7 that clamping first would give) and before the
	// tap pair below.
	SaturateCoordinate(ureg, Q0);
	SaturateCoordinate(vreg, Q0);

	if (m_sel.ltf)
	{
		// GSVector4i uf = u.xxzzlh().srl16(12);

		gsAsm->TRN1(uf.H8(), ureg.H8(), ureg.H8());
		gsAsm->USHR(uf.H8(), uf.H8(), 12);

		if (m_sel.prim != GS_SPRITE_CLASS)
		{
			// GSVector4i vf = v.xxzzlh().srl16(12);

			gsAsm->TRN1(vf.H8(), vreg.H8(), vreg.H8());
			gsAsm->USHR(vf.H8(), vf.H8(), 12);
		}

		// A zero weight collapses the four-tap blend onto the nearest tap, so the
		// nearest side of the crossing needs no separate path.
		if (m_sel.ltfx)
		{
			emit_ltfx_mask(Q0);
			gsAsm->TRN1(Q1.H8(), Q0.H8(), Q0.H8());

			gsAsm->AND(uf.B16(), uf.B16(), Q1.B16());

			if (m_sel.prim != GS_SPRITE_CLASS)
				gsAsm->AND(vf.B16(), vf.B16(), Q1.B16());
		}
	}

	// GSVector4i uv0 = u.sra32(16).ps32(v.sra32(16));

	gsAsm->SSHR(Q2.S4(), ureg.S4(), 16);
	gsAsm->SSHR(Q3.S4(), vreg.S4(), 16);
	gsAsm->SQXTN(oak::VReg_4H(Q2.index()), Q2.S4());
	gsAsm->SQXTN2(Q2.H8(), Q3.S4());

	if (m_sel.ltf)
	{
		// GSVector4i uv1 = uv0.add16(GSVector4i::X0001());

		gsAsm->MOVI(Q1.H8(), 1);
		gsAsm->ADD(Q3.H8(), Q2.H8(), Q1.H8());

		// uv0 = Wrap(uv0);
		// uv1 = Wrap(uv1);

		Wrap(Q2, Q3);
	}
	else
	{
		// uv0 = Wrap(uv0);

		Wrap(Q2);
	}

	SampleTexture_TexelReadHelper(0);
}

void GSDrawScanlineCodeGenerator::SampleTexture_TexelReadHelper(int mip_offset)
{
	const auto& uf = Q4;
	const auto& vf = (m_sel.prim != GS_SPRITE_CLASS || m_sel.mmin) ? Q7 : _temp_vf;

	// GSVector4i y0 = uv0.uph16() << tw;
	// GSVector4i X0 = uv0.upl16();

	gsAsm->MOVI(Q0.H8(), 0);

	gsAsm->ZIP1(Q5.H8(), Q2.H8(), Q0.H8());
	gsAsm->ZIP2(Q2.H8(), Q2.H8(), Q0.H8());
	gsAsm->SHL(Q2.S4(), Q2.S4(), m_sel.tw + 3);

	if (m_sel.ltf)
	{
		// GSVector4i X1 = uv1.upl16();
		// GSVector4i y1 = uv1.uph16() << tw;

		gsAsm->ZIP1(Q1.H8(), Q3.H8(), Q0.H8());
		gsAsm->ZIP2(Q3.H8(), Q3.H8(), Q0.H8());
		gsAsm->SHL(Q3.S4(), Q3.S4(), m_sel.tw + 3);

		// GSVector4i addr00 = y0 + X0;
		// GSVector4i addr01 = y0 + X1;
		// GSVector4i addr10 = y1 + X0;
		// GSVector4i addr11 = y1 + X1;

		gsAsm->ADD(Q0.S4(), Q3.S4(), Q1.S4()); // addr11
		gsAsm->ADD(Q1.S4(), Q1.S4(), Q2.S4()); // addr01
		gsAsm->ADD(Q2.S4(), Q2.S4(), Q5.S4()); // addr00
		gsAsm->ADD(Q3.S4(), Q3.S4(), Q5.S4()); // addr10

		// c00 = addr00.gather32_32((const u32/u8*)tex[, clut]);
		// c01 = addr01.gather32_32((const u32/u8*)tex[, clut]);
		// c10 = addr10.gather32_32((const u32/u8*)tex[, clut]);
		// c11 = addr11.gather32_32((const u32/u8*)tex[, clut]);

		//         D0  D1  d2s0  d3s1 S2 S3
		ReadTexel4(Q5, Q6, Q0, Q2, Q1, Q3, mip_offset);

		// GSVector4i rb00 = c00 & mask;
		// GSVector4i ga00 = (c00 >> 8) & mask;

		split16_2x8(Q3, Q6, Q6);

		// GSVector4i rb01 = c01 & mask;
		// GSVector4i ga01 = (c01 >> 8) & mask;

		split16_2x8(Q0, Q1, Q0);

		// rb00 = rb00.lerp16_4(rb01, uf);
		// ga00 = ga00.lerp16_4(ga01, uf);

		lerp16_4(Q0, Q3, uf);
		lerp16_4(Q1, Q6, uf);

		// GSVector4i rb10 = c10 & mask;
		// GSVector4i ga10 = (c10 >> 8) & mask;

		split16_2x8(Q2, Q3, Q2);

		// GSVector4i rb11 = c11 & mask;
		// GSVector4i ga11 = (c11 >> 8) & mask;

		split16_2x8(Q5, Q6, Q5);

		// rb10 = rb10.lerp16_4(rb11, uf);
		// ga10 = ga10.lerp16_4(ga11, uf);

		lerp16_4(Q5, Q2, uf);
		lerp16_4(Q6, Q3, uf);

		// rb00 = rb00.lerp16_4(rb10, vf);
		// ga00 = ga00.lerp16_4(ga10, vf);

		lerp16_4(Q5, Q0, vf);
		lerp16_4(Q6, Q1, vf);
	}
	else
	{
		// GSVector4i addr00 = y0 + X0;

		gsAsm->ADD(Q2.S4(), Q2.S4(), Q5.S4());

		// c00 = addr00.gather32_32((const u32/u8*)tex[, clut]);

		ReadTexel1(Q5, Q2, Q0, mip_offset);

		// GSVector4i mask = GSVector4i::x00ff();

		// c[0] = c00 & mask;
		// c[1] = (c00 >> 8) & mask;

		split16_2x8(Q5, Q6, Q5);
	}
}

void GSDrawScanlineCodeGenerator::Wrap(oak::QReg uv)
{
	// Q0, Q1, Q4, Q5, Q6 = free

	int wms_clamp = ((m_sel.wms + 1) >> 1) & 1;
	int wmt_clamp = ((m_sel.wmt + 1) >> 1) & 1;

	int region = ((m_sel.wms | m_sel.wmt) >> 1) & 1;

	if (wms_clamp == wmt_clamp)
	{
		if (wms_clamp)
		{
			if (region)
			{
				gsAsm->SMAX(uv.H8(), uv.H8(), _global_tmin.H8());
			}
			else
			{
				gsAsm->MOVI(Q0.H8(), 0);
				gsAsm->SMAX(uv.H8(), uv.H8(), Q0.H8());
			}

			gsAsm->SMIN(uv.H8(), uv.H8(), _global_tmax.H8());
		}
		else
		{
			gsAsm->AND(uv.B16(), uv.B16(), _global_tmin.B16());

			if (region)
				gsAsm->ORR(uv.B16(), uv.B16(), _global_tmax.B16());
		}
	}
	else
	{
		// GSVector4i repeat = (t & m_local.gd->t.min) | m_local.gd->t.max;

		gsAsm->AND(Q1.B16(), uv.B16(), _global_tmin.B16());

		if (region)
			gsAsm->ORR(Q1.B16(), Q1.B16(), _global_tmax.B16());

		// GSVector4i clamp = t.sat_i16(m_local.gd->t.min, m_local.gd->t.max);

		gsAsm->SMAX(uv.H8(), uv.H8(), _global_tmin.H8());
		gsAsm->SMIN(_vscratch.H8(), uv.H8(), _global_tmax.H8());

		// clamp.blend8(repeat, m_local.gd->t.mask);
		gsAsm->SSHR(uv.B16(), _global_tmask.B16(), 7);
		gsAsm->BSL(uv.B16(), Q1.B16(), _vscratch.B16());
	}
}

void GSDrawScanlineCodeGenerator::Wrap(oak::QReg uv0, oak::QReg uv1)
{
	// Q0, Q1, Q4, Q5, Q6 = free

	int wms_clamp = ((m_sel.wms + 1) >> 1) & 1;
	int wmt_clamp = ((m_sel.wmt + 1) >> 1) & 1;

	int region = ((m_sel.wms | m_sel.wmt) >> 1) & 1;

	if (wms_clamp == wmt_clamp)
	{
		if (wms_clamp)
		{
			if (region)
			{
				gsAsm->SMAX(uv0.H8(), uv0.H8(), _global_tmin.H8());
				gsAsm->SMAX(uv1.H8(), uv1.H8(), _global_tmin.H8());
			}
			else
			{
				gsAsm->MOVI(Q0.B16(), 0);
				gsAsm->SMAX(uv0.H8(), uv0.H8(), Q0.H8());
				gsAsm->SMAX(uv1.H8(), uv1.H8(), Q0.H8());
			}

			gsAsm->SMIN(uv0.H8(), uv0.H8(), _global_tmax.H8());
			gsAsm->SMIN(uv1.H8(), uv1.H8(), _global_tmax.H8());
		}
		else
		{
			gsAsm->AND(uv0.B16(), uv0.B16(), _global_tmin.B16());
			gsAsm->AND(uv1.B16(), uv1.B16(), _global_tmin.B16());

			if (region)
			{
				gsAsm->ORR(uv0.B16(), uv0.B16(), _global_tmax.B16());
				gsAsm->ORR(uv1.B16(), uv1.B16(), _global_tmax.B16());
			}
		}
	}
	else
	{
		// uv0

		// GSVector4i repeat = (t & m_local.gd->t.min) | m_local.gd->t.max;

		gsAsm->AND(Q1.B16(), uv0.B16(), _global_tmin.B16());

		if (region)
			gsAsm->ORR(Q1.B16(), Q1.B16(), _global_tmax.B16());

		// GSVector4i clamp = t.sat_i16(m_local.gd->t.min, m_local.gd->t.max);

		gsAsm->SMAX(uv0.H8(), uv0.H8(), _global_tmin.H8());
		gsAsm->SMIN(_vscratch.H8(), uv0.H8(), _global_tmax.H8());

		// clamp.blend8(repeat, m_local.gd->t.mask);
		gsAsm->SSHR(uv0.B16(), _global_tmask.B16(), 7);
		gsAsm->BSL(uv0.B16(), Q1.B16(), _vscratch.B16());

		// uv1

		// GSVector4i repeat = (t & m_local.gd->t.min) | m_local.gd->t.max;

		gsAsm->AND(Q1.B16(), uv1.B16(), _global_tmin.B16());

		if (region)
			gsAsm->ORR(Q1.B16(), Q1.B16(), _global_tmax.B16());

		// GSVector4i clamp = t.sat_i16(m_local.gd->t.min, m_local.gd->t.max);

		gsAsm->SMAX(uv1.H8(), uv1.H8(), _global_tmin.H8());
		gsAsm->SMIN(_vscratch.H8(), uv1.H8(), _global_tmax.H8());

		// clamp.blend8(repeat, m_local.gd->t.mask);
		gsAsm->SSHR(uv1.B16(), _global_tmask.B16(), 7);
		gsAsm->BSL(uv1.B16(), Q1.B16(), _vscratch.B16());
	}
}

/// Input: Q4=q, Q2=s, Q3=t
/// Output: _rb, _ga
void GSDrawScanlineCodeGenerator::SampleTextureLOD()
{
	if (!m_sel.fb || m_sel.tfx == TFX_NONE)
	{
		return;
	}

	const auto& uf = Q4;
	const auto& vf = Q7;
	const auto& local0 = _vscratch; // used for uv
	const auto& local1 = _vscratch2; // used for uv
	const auto& local2 = _vscratch3;

	oak::QReg uv0(_temp_s);
	oak::QReg uv1(_temp_t);

	if (!m_sel.fst)
	{
		// Truncated reciprocal, as in SampleTexture above.
		if (m_sel.stqplane)
		{
			gsAsm->SSHR(local2.S4(), _temp_q.S4(), GS_PLANE_PIXEL_SHIFT);
			gsAsm->SCVTF(local2.S4(), local2.S4());
			gsAsm->FMOV(local1.S4(), oak::FImm8(0x70));
			gsAsm->FDIV(local2.S4(), local1.S4(), local2.S4());
			gsAsm->MOVI(local1.S4(), GS_RECIP_ROUND_UP);
			gsAsm->ADD(local2.S4(), local2.S4(), local1.S4());
			gsAsm->BIC(local2.S4(), 0xff, 0);

			gsAsm->LDR(Q0, _local(pwalk.stscale));
			gsAsm->SSHR(local0.S4(), _temp_s.S4(), GS_PLANE_PIXEL_SHIFT);
			gsAsm->SSHR(local1.S4(), _temp_t.S4(), GS_PLANE_PIXEL_SHIFT);
			gsAsm->SCVTF(local0.S4(), local0.S4());
			gsAsm->SCVTF(local1.S4(), local1.S4());
			gsAsm->FMUL(local0.S4(), local0.S4(), local2.S4());
			gsAsm->FMUL(local1.S4(), local1.S4(), local2.S4());
			gsAsm->FMUL(local0.S4(), local0.S4(), oak::SReg(Q0.index()), 0);
			gsAsm->FMUL(local1.S4(), local1.S4(), oak::SReg(Q0.index()), 1);
		}
		else
		{
			gsAsm->FMOV(local2.S4(), oak::FImm8(0x70));
			gsAsm->FDIV(local2.S4(), local2.S4(), _temp_q.S4());
			gsAsm->MOVI(local0.S4(), GS_RECIP_ROUND_UP);
			gsAsm->ADD(local2.S4(), local2.S4(), local0.S4());
			gsAsm->BIC(local2.S4(), 0xff, 0);

			gsAsm->FMUL(local0.S4(), _temp_s.S4(), local2.S4());
			gsAsm->FMUL(local1.S4(), _temp_t.S4(), local2.S4());
		}

		gsAsm->FCVTZS(local0.S4(), local0.S4());
		gsAsm->FCVTZS(local1.S4(), local1.S4());

		uv0 = local0;
		uv1 = local1;
	}

	// The coordinate DDA's lag, taken before the level shift divides it away. See
	// SampleTexture above.
	if (m_sel.prim != GS_SPRITE_CLASS)
	{
		gsAsm->LDR(local2, _local(tclag.u));
		gsAsm->SUB(local0.S4(), uv0.S4(), local2.S4());
		gsAsm->LDR(local2, _local(tclag.v));
		gsAsm->SUB(local1.S4(), uv1.S4(), local2.S4());
		uv0 = local0;
		uv1 = local1;
	}

	if (!m_sel.lcm)
	{
		// LOD16 = K + (-e) * 2^(4+L) - T[L][idx], in sixteenths of a level.
		//
		// The logarithm is a 128-entry table on the top seven fractional bits of Q's
		// mantissa (GSLevelOfDetail.h), not a polynomial log2.

		// With the plane rule Q is the count of g/4 times 2^(E - 16) (GSPerspectivePlane.h);
		// local2 held the reciprocal and is spent.
		oak::QReg qf = _temp_q;

		if (m_sel.stqplane)
		{
			gsAsm->SSHR(local2.S4(), _temp_q.S4(), GS_PLANE_PIXEL_SHIFT);
			gsAsm->SCVTF(local2.S4(), local2.S4());
			gsAsm->LDR(Q1, _local(pwalk.qscale));
			gsAsm->FMUL(local2.S4(), local2.S4(), Q1.S4());
			qf = local2;
		}

		// -e = 127 - ((q >> 23) & 0xff)
		gsAsm->USHR(Q0.S4(), qf.S4(), 23);
		gsAsm->MOVI(Q1.S4(), 0xff);
		gsAsm->AND(Q0.B16(), Q0.B16(), Q1.B16());
		gsAsm->MOVI(Q1.S4(), 127);
		gsAsm->SUB(Q0.S4(), Q1.S4(), Q0.S4());

		// (-e) << (4 + L), then + K
		gsAsm->DUP(Q1.S4(), _global_lodshift);
		gsAsm->SSHL(Q0.S4(), Q0.S4(), Q1.S4());
		gsAsm->DUP(Q1.S4(), _global_lodk);
		gsAsm->ADD(Q0.S4(), Q0.S4(), Q1.S4());

		// - T[idx], idx = (q >> 16) & 0x7f. One extract-add-load triple per lane,
		// as in ReadTexelImpl, which is why the table is held as words.
		gsAsm->USHR(Q4.S4(), qf.S4(), 16);
		gsAsm->MOVI(Q1.S4(), 0x7f);
		gsAsm->AND(Q4.B16(), Q4.B16(), Q1.B16());

		for (int i = 0; i < 4; i++)
		{
			gsAsm->MOV(_scratchaddr.toW(), Q4.S4(), i);
			gsAsm->ADD(_scratchaddr, _global_lodtab, _scratchaddr.toW(), oak::AddSubExt::UXTW, 2);

			if (i == 0)
				gsAsm->LDR(oak::SReg(Q1.index()), OakMemOperand{_scratchaddr, static_cast<s64>(0)});
			else
				gsAsm->LD1(Q1.S4(), i, OakMemOperand{_scratchaddr, static_cast<s64>(0)});
		}

		gsAsm->SUB(Q0.S4(), Q0.S4(), Q1.S4());

		// Sixteenths -> 16.16, then the ceiling. The `+ 0x8000` round-off below is
		// then (LOD16 + 8) >> 4 with ties up, and the trilinear weight from the top
		// four fraction bits is LOD16 & 15.
		gsAsm->SHL(Q4.S4(), Q0.S4(), 12);
		gsAsm->DUP(Q0.S4(), _global_mxl);
		gsAsm->SMIN(Q4.S4(), Q4.S4(), Q0.S4());
		gsAsm->MOVI(Q0.S4(), 0);
		gsAsm->SMAX(Q4.S4(), Q4.S4(), Q0.S4());

		if (m_sel.mmin == 1) // round-off mode
		{
			gsAsm->MOVI(Q0.S4(), 0x8000);
			gsAsm->ADD(Q4.S4(), Q4.S4(), Q0.S4());
		}

		gsAsm->USHR(Q0.S4(), Q4.S4(), 16);

		// NOTE: Must go to memory, it gets indexed
		gsAsm->STR(Q0, _local(temp.lod.i));

		if (m_sel.mmin == 2) // trilinear mode
		{
			gsAsm->TRN1(Q1.H8(), Q4.H8(), Q4.H8());
			gsAsm->STR(Q1.S4(), _local(temp.lod.f));
		}

		// shift u/v/minmax by (int)lod

		gsAsm->NEG(Q0.S4(), Q0.S4());
		gsAsm->SSHL(local0.S4(), uv0.S4(), Q0.S4());
		gsAsm->SSHL(local1.S4(), uv1.S4(), Q0.S4());
		uv0 = local0;
		uv1 = local1;

		// m_local.gd->t.minmax => m_local.temp.uv_minmax[0/1]

		gsAsm->MOVI(Q1.S4(), 0);
		gsAsm->ZIP1(Q5.H8(), _global_tmin.H8(), Q1.H8()); // minu
		gsAsm->ZIP2(Q6.H8(), _global_tmin.H8(), Q1.H8()); // minv
		gsAsm->USHL(Q5.S4(), Q5.S4(), Q0.S4());
		gsAsm->USHL(Q6.S4(), Q6.S4(), Q0.S4());
		gsAsm->SQXTUN(oak::VReg_4H(Q5.index()), Q5.S4());
		gsAsm->SQXTUN2(Q5.H8(), Q6.S4());

		gsAsm->ZIP1(Q6.H8(), _global_tmax.H8(), Q1.H8()); // maxu
		gsAsm->ZIP2(Q4.H8(), _global_tmax.H8(), Q1.H8()); // maxu
		gsAsm->USHL(Q6.S4(), Q6.S4(), Q0.S4());
		gsAsm->USHL(Q4.S4(), Q4.S4(), Q0.S4());
		gsAsm->SQXTUN(oak::VReg_4H(Q6.index()), Q6.S4());
		gsAsm->SQXTUN2(Q6.H8(), Q4.S4());

		if (m_sel.mmin != 1)
		{
			gsAsm->STR(Q5, _local(temp.uv_minmax[0]));
			gsAsm->STR(Q6, _local(temp.uv_minmax[1]));
		}
	}
	else
	{
		// lod = K

		gsAsm->ADD(_scratchaddr, _globals, offsetof(GSScanlineGlobalData, lod));
		gsAsm->LD1R(Q0.S4(), OakMemOperand{_scratchaddr, static_cast<s64>(0)});
		gsAsm->NEG(Q0.S4(), Q0.S4());

		gsAsm->SSHL(local0.S4(), uv0.S4(), Q0.S4());
		gsAsm->SSHL(local1.S4(), uv1.S4(), Q0.S4());
		uv0 = local0;
		uv1 = local1;

		gsAsm->LDR(Q5, _local(temp.uv_minmax[0]));
		gsAsm->LDR(Q6, _local(temp.uv_minmax[1]));
	}

	// GSTruncateCoordinate, as in SampleTexture above. Into the scratch pair, so
	// uv0/uv1 keep this level's own coordinate for the level above it.
	gsAsm->SSHR(Q0.S4(), uv0.S4(), 31);
	gsAsm->SSHR(Q1.S4(), uv1.S4(), 31);
	gsAsm->USHR(Q0.S4(), Q0.S4(), 20);
	gsAsm->USHR(Q1.S4(), Q1.S4(), 20);
	gsAsm->ADD(Q2.S4(), uv0.S4(), Q0.S4());
	gsAsm->ADD(Q3.S4(), uv1.S4(), Q1.S4());

	if (m_sel.ltf)
	{
		// u -= 0x8000;
		// v -= 0x8000;

		gsAsm->MOVI(Q4.S4(), 0x8000);
		gsAsm->SUB(Q2.S4(), Q2.S4(), Q4.S4());
		gsAsm->SUB(Q3.S4(), Q3.S4(), Q4.S4());
	}

	// The 12.4 field, as in SampleTexture above, taken at the same point in this
	// level's copy of the chain.
	SaturateCoordinate(Q2, Q0);
	SaturateCoordinate(Q3, Q0);

	if (m_sel.ltf)
	{
		// GSVector4i uf = u.xxzzlh().srl16(1);

		gsAsm->TRN1(uf.H8(), Q2.H8(), Q2.H8());
		gsAsm->USHR(uf.H8(), uf.H8(), 12);

		// GSVector4i vf = v.xxzzlh().srl16(1);

		gsAsm->TRN1(vf.H8(), Q3.H8(), Q3.H8());
		gsAsm->USHR(vf.H8(), vf.H8(), 12);
	}

	// GSVector4i uv0 = u.sra32(16).ps32(v.sra32(16));

	gsAsm->SSHR(Q2.S4(), Q2.S4(), 16);
	gsAsm->SSHR(Q3.S4(), Q3.S4(), 16);
	gsAsm->SQXTN(oak::VReg_4H(Q2.index()), Q2.S4());
	gsAsm->SQXTN2(Q2.H8(), Q3.S4());

	if (m_sel.ltf)
	{
		// GSVector4i uv1 = uv0.add16(GSVector4i::X0001());

		gsAsm->MOVI(Q1.H8(), 1);
		gsAsm->ADD(Q3.H8(), Q2.H8(), Q1.H8());

		// uv0 = Wrap(uv0);
		// uv1 = Wrap(uv1);

		WrapLOD(Q2, Q3, Q0, Q1, Q5, Q6);
	}
	else
	{
		// uv0 = Wrap(uv0);

		WrapLOD(Q2, Q0, Q1, Q5, Q6);
	}

	SampleTexture_TexelReadHelper(0);

	if (m_sel.mmin != 1) // !round-off mode
	{
		gsAsm->SSHR(Q2.S4(), uv0.S4(), 1);
		gsAsm->SSHR(Q3.S4(), uv1.S4(), 1);

		gsAsm->MOV(local0, Q5);
		gsAsm->MOV(local1, Q6);

		gsAsm->LDR(Q5, _local(temp.uv_minmax[0]));
		gsAsm->LDR(Q6, _local(temp.uv_minmax[1]));

		gsAsm->USHR(Q5.H8(), Q5.H8(), 1);
		gsAsm->USHR(Q6.H8(), Q6.H8(), 1);

		// GSTruncateCoordinate, as above.

		gsAsm->SSHR(Q0.S4(), Q2.S4(), 31);
		gsAsm->SSHR(Q1.S4(), Q3.S4(), 31);
		gsAsm->USHR(Q0.S4(), Q0.S4(), 20);
		gsAsm->USHR(Q1.S4(), Q1.S4(), 20);
		gsAsm->ADD(Q2.S4(), Q2.S4(), Q0.S4());
		gsAsm->ADD(Q3.S4(), Q3.S4(), Q1.S4());

		if (m_sel.ltf)
		{
			// u -= 0x8000;
			// v -= 0x8000;

			gsAsm->MOVI(Q4.S4(), 0x8000);
			gsAsm->SUB(Q2.S4(), Q2.S4(), Q4.S4());
			gsAsm->SUB(Q3.S4(), Q3.S4(), Q4.S4());
		}

		// The 12.4 field, as in SampleTexture above, taken at the same point in this
		// level's copy of the chain.
		SaturateCoordinate(Q2, Q0);
		SaturateCoordinate(Q3, Q0);

		if (m_sel.ltf)
		{
			// GSVector4i uf = u.xxzzlh().srl16(1);

			gsAsm->TRN1(uf.H8(), Q2.H8(), Q2.H8());
			gsAsm->USHR(uf.H8(), uf.H8(), 12);

			// GSVector4i vf = v.xxzzlh().srl16(1);

			gsAsm->TRN1(vf.H8(), Q3.H8(), Q3.H8());
			gsAsm->USHR(vf.H8(), vf.H8(), 12);
		}

		// GSVector4i uv0 = u.sra32(16).ps32(v.sra32(16));

		gsAsm->SSHR(Q2.S4(), Q2.S4(), 16);
		gsAsm->SSHR(Q3.S4(), Q3.S4(), 16);
		gsAsm->SQXTN(oak::VReg_4H(Q2.index()), Q2.S4());
		gsAsm->SQXTN2(Q2.H8(), Q3.S4());

		if (m_sel.ltf)
		{
			// GSVector4i uv1 = uv0.add16(GSVector4i::X0001());

			gsAsm->MOVI(Q1.H8(), 1);
			gsAsm->ADD(Q3.H8(), Q2.H8(), Q1.H8());

			// uv0 = Wrap(uv0);
			// uv1 = Wrap(uv1);

			WrapLOD(Q2, Q3, Q0, Q1, Q5, Q6);
		}
		else
		{
			// uv0 = Wrap(uv0);

			WrapLOD(Q2, Q0, Q1, Q5, Q6);
		}

		gsAsm->LDR(local2, m_sel.lcm ? _global(lod.f) : _local(temp.lod.f));

		SampleTexture_TexelReadHelper(1);

		// Q5: rb
		// Q6: ga

		// Four-bit trilinear weight, truncated: (f & 0xf000) >> 1, which is the
		// >> 1 the lerp wants folded into the quantisation. See GSDrawScanline.cpp.
		gsAsm->USHR(Q0.H8(), local2.H8(), 12);
		gsAsm->SHL(Q0.H8(), Q0.H8(), 11);

		lerp16(Q5, local0, Q0, 0);
		lerp16(Q6, local1, Q0, 0);
	}
}

void GSDrawScanlineCodeGenerator::WrapLOD(oak::QReg uv,
	oak::QReg tmp, oak::QReg tmp2,
	oak::QReg min, oak::QReg max)
{
	const int wms_clamp = ((m_sel.wms + 1) >> 1) & 1;
	const int wmt_clamp = ((m_sel.wmt + 1) >> 1) & 1;
	const int region = ((m_sel.wms | m_sel.wmt) >> 1) & 1;

	if (wms_clamp == wmt_clamp)
	{
		if (wms_clamp)
		{
			if (region)
			{
				gsAsm->SMAX(uv.H8(), uv.H8(), min.H8());
			}
			else
			{
				gsAsm->MOVI(tmp.H8(), 0);
				gsAsm->SMAX(uv.H8(), uv.H8(), tmp.H8());
			}

			gsAsm->SMIN(uv.H8(), uv.H8(), max.H8());
		}
		else
		{
			gsAsm->AND(uv.B16(), uv.B16(), min.B16());

			if (region)
				gsAsm->ORR(uv.B16(), uv.B16(), max.B16());
		}
	}
	else
	{

		// GSVector4i repeat = (t & m_local.gd->t.min) | m_local.gd->t.max;
		gsAsm->AND(tmp.B16(), uv.B16(), min.B16());
		if (region)
			gsAsm->ORR(tmp.B16(), tmp.B16(), max.B16());

		// GSVector4i clamp = t.sat_i16(m_local.gd->t.min, m_local.gd->t.max);
		gsAsm->SMAX(uv.H8(), uv.H8(), min.H8());
		gsAsm->SMIN(tmp2.H8(), uv.H8(), max.H8());

		// clamp.blend8(repeat, m_local.gd->t.mask);
		gsAsm->SSHR(uv.B16(), _global_tmask.B16(), 7);
		gsAsm->BSL(uv.B16(), tmp.B16(), tmp2.B16());
	}
}

void GSDrawScanlineCodeGenerator::WrapLOD(
	oak::QReg uv0, oak::QReg uv1,
	oak::QReg tmp, oak::QReg tmp2,
	oak::QReg min, oak::QReg max)
{
	const int wms_clamp = ((m_sel.wms + 1) >> 1) & 1;
	const int wmt_clamp = ((m_sel.wmt + 1) >> 1) & 1;
	const int region = ((m_sel.wms | m_sel.wmt) >> 1) & 1;

	if (wms_clamp == wmt_clamp)
	{
		if (wms_clamp)
		{
			if (region)
			{
				gsAsm->SMAX(uv0.H8(), uv0.H8(), min.H8());
				gsAsm->SMAX(uv1.H8(), uv1.H8(), min.H8());
			}
			else
			{
				gsAsm->MOVI(tmp.H8(), 0);
				gsAsm->SMAX(uv0.H8(), uv0.H8(), tmp.H8());
				gsAsm->SMAX(uv1.H8(), uv1.H8(), tmp.H8());
			}

			gsAsm->SMIN(uv0.H8(), uv0.H8(), max.H8());
			gsAsm->SMIN(uv1.H8(), uv1.H8(), max.H8());
		}
		else
		{
			gsAsm->AND(uv0.B16(), uv0.B16(), min.B16());
			gsAsm->AND(uv1.B16(), uv1.B16(), min.B16());

			if (region)
			{
				gsAsm->ORR(uv0.B16(), uv0.B16(), max.B16());
				gsAsm->ORR(uv1.B16(), uv1.B16(), max.B16());
			}
		}
	}
	else
	{
		for (oak::QReg uv : {uv0, uv1})
		{
			// GSVector4i repeat = (t & m_local.gd->t.min) | m_local.gd->t.max;
			gsAsm->AND(tmp.B16(), uv.B16(), min.B16());
			if (region)
				gsAsm->ORR(tmp.B16(), tmp.B16(), max.B16());

			// GSVector4i clamp = t.sat_i16(m_local.gd->t.min, m_local.gd->t.max);
			gsAsm->SMAX(uv.H8(), uv.H8(), min.H8());
			gsAsm->SMIN(tmp2.H8(), uv.H8(), max.H8());

			// clamp.blend8(repeat, m_local.gd->t.mask);

			gsAsm->SSHR(uv.B16(), _global_tmask.B16(), 7);
			gsAsm->BSL(uv.B16(), tmp.B16(), tmp2.B16());
		}
	}
}

void GSDrawScanlineCodeGenerator::AlphaTFX()
{
	if (!m_sel.fb)
	{
		return;
	}

	switch (m_sel.tfx)
	{
		case TFX_MODULATE:

			// GSVector4i ga = iip ? gaf : m_local.c.ga;

			// gat = gat.modulate16<1>(ga.srl16<7>().sll16<7>()).clamp8();
			storedVertexColor(_vscratch, _temp_ga);
			modulate16(Q6, _vscratch, 1);
			clamp16(Q6, Q3);

			// if (!tcc) gat = gat.mix16(ga.srl16(7));

			if (!m_sel.tcc)
			{
				walkColorByte(Q4, _temp_ga);

				mix16(Q6, Q4, Q3);
			}

			break;

		case TFX_DECAL:

			// if (!tcc) gat = gat.mix16(ga.srl16(7));

			if (!m_sel.tcc)
			{
				// GSVector4i ga = iip ? gaf : m_local.c.ga;

				walkColorByte(Q4, _temp_ga);
				mix16(Q6, Q4, Q3);
			}

			break;

		case TFX_HIGHLIGHT:

			// GSVector4i ga = iip ? gaf : m_local.c.ga;

			// gat = gat.mix16(!tcc ? ga.srl16(7) : gat.addus8(ga.srl16(7)));
			walkColorByte(Q4, _temp_ga);

			if (m_sel.tcc)
				gsAsm->UQADD(Q4.B16(), Q4.B16(), Q6.B16());

			mix16(Q6, Q4, Q3);

			break;

		case TFX_HIGHLIGHT2:

			// if (!tcc) gat = gat.mix16(ga.srl16(7));

			if (!m_sel.tcc)
			{
				// GSVector4i ga = iip ? gaf : m_local.c.ga;
				walkColorByte(Q4, _temp_ga);

				mix16(Q6, Q4, Q3);
			}

			break;

		case TFX_NONE:

			// gat = iip ? ga.srl16(7) : ga;

			if (m_sel.iip)
				walkColorByte(Q6, _temp_ga);
			else
				gsAsm->MOV(Q6, _temp_ga);

			break;
	}

	if (m_sel.aa1)
	{
		// gs_user figure 3-2: anti-aliasing after tfx, before tests, modifies alpha

		// FIXME: bios config screen cubes

		if (!m_sel.abe)
		{
			// a = cov

			if (m_sel.edge)
				gsAsm->LDR(Q0, _local(temp.cov));
			else
				gsAsm->MOVI(Q0.H8(), 0x0080);

			mix16(Q6, Q0, Q1);
		}
		else
		{
			// a = a == 0x80 ? cov : a

			gsAsm->MOVI(Q0.H8(), 0x0080);

			if (m_sel.edge)
				gsAsm->LDR(Q1, _local(temp.cov));
			else
				gsAsm->MOV(Q1, Q0);

			gsAsm->CMEQ(Q0.H8(), Q0.H8(), Q6.H8());
			gsAsm->USHR(Q0.S4(), Q0.S4(), 16);
			gsAsm->SHL(Q0.S4(), Q0.S4(), 16);

			blend8(Q6, Q1, Q0, _vscratch);
		}
	}
}

void GSDrawScanlineCodeGenerator::ReadMask()
{
	if (m_sel.fwrite)
		gsAsm->LDR(Q3, _global(fm));

	if (m_sel.zwrite)
		gsAsm->LDR(Q4, _global(zm));
}

void GSDrawScanlineCodeGenerator::TestAlpha()
{
	switch (m_sel.atst)
	{
		case ATST_NEVER:
			// t = GSVector4i::xffffffff();
			// pcmpeqd(Q1, Q1);
			gsAsm->MOVI(Q1.D2(), 0xFFFFFFFFFFFFFFFFULL);
			break;

		case ATST_ALWAYS:
			return;

		case ATST_LESS:
		case ATST_LEQUAL:
			// t = (ga >> 16) > m_local.gd->aref;
			gsAsm->DUP(_vscratch.S4(), _local_aref);
			gsAsm->USHR(Q1.S4(), Q6.S4(), 16);
			gsAsm->CMGT(Q1.S4(), Q1.S4(), _vscratch.S4());
			break;

		case ATST_EQUAL:
			// t = (ga >> 16) != m_local.gd->aref;
			gsAsm->DUP(_vscratch.S4(), _local_aref);
			gsAsm->USHR(Q1.S4(), Q6.S4(), 16);
			gsAsm->CMEQ(Q1.S4(), Q1.S4(), _vscratch.S4());
			gsAsm->MVN(Q1.B16(), Q1.B16());
			break;

		case ATST_GEQUAL:
		case ATST_GREATER:
			// t = (ga >> 16) < m_local.gd->aref;
			gsAsm->DUP(_vscratch.S4(), _local_aref);
			gsAsm->USHR(Q1.S4(), Q6.S4(), 16);
			gsAsm->CMGT(Q1.S4(), _vscratch.S4(), Q1.S4());
			break;

		case ATST_NOTEQUAL:
			// t = (ga >> 16) == m_local.gd->aref;
			gsAsm->DUP(_vscratch.S4(), _local_aref);
			gsAsm->USHR(Q1.S4(), Q6.S4(), 16);
			gsAsm->CMEQ(Q1.S4(), Q1.S4(), _vscratch.S4());
			break;
	}

	switch (m_sel.afail)
	{
		case AFAIL_KEEP:
			// test |= t;
			gsAsm->ORR(_test.B16(), _test.B16(), Q1.B16());
			alltrue(_test, _vscratch);
			break;

		case AFAIL_FB_ONLY:
			// zm |= t;
			gsAsm->ORR(Q4.B16(), Q4.B16(), Q1.B16());
			break;

		case AFAIL_ZB_ONLY:
			// fm |= t;
			gsAsm->ORR(Q3.B16(), Q3.B16(), Q1.B16());
			break;

		case AFAIL_RGB_ONLY:
			// zm |= t;
			gsAsm->ORR(Q4.B16(), Q4.B16(), Q1.B16());

			// fm |= t & GSVector4i::xff000000();
			gsAsm->USHR(Q1.S4(), Q1.S4(), 24);
			gsAsm->SHL(Q1.S4(), Q1.S4(), 24);
			gsAsm->ORR(Q3.B16(), Q3.B16(), Q1.B16());
			break;
	}
}

void GSDrawScanlineCodeGenerator::ColorTFX()
{
	if (!m_sel.fwrite)
	{
		return;
	}

	switch (m_sel.tfx)
	{
		case TFX_MODULATE:

			// GSVector4i rb = iip ? rbf : m_local.c.rb;

			// rbt = rbt.modulate16<1>(rb.srl16<7>().sll16<7>()).clamp8();

			storedVertexColor(_vscratch, _temp_rb);
			modulate16(Q5, _vscratch, 1);
			clamp16(Q5, Q1);

			break;

		case TFX_DECAL:

			break;

		case TFX_HIGHLIGHT:
		case TFX_HIGHLIGHT2:

			// GSVector4i ga = iip ? gaf : m_local.c.ga;
			// gat = gat.modulate16<1>(ga).add16(af).clamp8().mix16(gat);

			gsAsm->MOV(Q1, Q6);
			storedVertexColor(_vscratch, _temp_ga);
			modulate16(Q6, _vscratch, 1);

			// af is the walked alpha as the stored byte, so a negative the walk carries
			// must be clamped to zero before the shift (GSWalkColorByte / walkColorByte).
			// A plain logical shift turns a negative into up to 511, HIGHLIGHT adds it to
			// the colour, and the clamp below drives every channel to 255.
			gsAsm->TRN2(_vscratch.H8(), _temp_ga.H8(), _temp_ga.H8());
			walkColorByte(Q2, _vscratch);
			gsAsm->ADD(Q6.H8(), Q6.H8(), Q2.H8());

			clamp16(Q6, Q0);

			mix16(Q6, Q1, Q0);

			// GSVector4i rb = iip ? rbf : m_local.c.rb;

			// rbt = rbt.modulate16<1>(rb).add16(af).clamp8();

			storedVertexColor(_vscratch, _temp_rb);
			modulate16(Q5, _vscratch, 1);
			gsAsm->ADD(Q5.H8(), Q5.H8(), Q2.H8());

			clamp16(Q5, Q0);

			break;

		case TFX_NONE:

			// rbt = iip ? rb.srl16(7) : rb;

			if (m_sel.iip)
				walkColorByte(Q5, _temp_rb);
			else
				gsAsm->MOV(Q5, _temp_rb);

			break;
	}
}

void GSDrawScanlineCodeGenerator::Fog()
{
	if (!m_sel.fwrite || !m_sel.fge)
	{
		return;
	}

	// rb = m_local.gd->frb.lerp16<0>(rb, floor(f));
	// ga = m_local.gd->fga.lerp16<0>(ga, floor(f)).mix16(ga);
	//
	// The blend takes an integer F; see the C++ twin in GSDrawScanline.cpp for
	// why lerp16<0> is already the 256 - F floor rule. storedVertexColor is that
	// truncation plus the pack's saturation: fog rides the colour DDA, so a fog
	// lane carried below zero saturates to 0 like a colour lane. It goes to a
	// scratch because _temp_f is the walk's accumulator and Step carries it on
	// with its fraction intact.
	storedVertexColor(_vscratch3, _temp_f);

	gsAsm->DUP(_vscratch.S4(), _global_frb);
	gsAsm->DUP(_vscratch2.S4(), _global_fga);
	gsAsm->MOV(Q1, Q6);

	lerp16(Q5, _vscratch, _vscratch3, 0);

	lerp16(Q6, _vscratch2, _vscratch3, 0);
	mix16(Q6, Q1, Q0);
}

void GSDrawScanlineCodeGenerator::ReadFrame()
{
	if (!m_sel.fb)
	{
		return;
	}

	// int fa = fza_base.x + fza_offset->x;

	gsAsm->LDR(W6, OakMemOperand{X7, static_cast<s64>(0)});
	gsAsm->LDR(_wscratch, OakMemOperand{X8, static_cast<s64>(0)});
	gsAsm->ADD(W6, W6, _wscratch);
	gsAsm->AND(W6, W6, HALF_VM_SIZE - 1);

	if (!m_sel.rfb)
	{
		return;
	}

	ReadPixel(Q2, W6);
}

void GSDrawScanlineCodeGenerator::TestDestAlpha()
{
	if (!m_sel.date || (m_sel.fpsm != 0 && m_sel.fpsm != 2))
	{
		return;
	}

	// test |= ((fd [<< 16]) ^ m_local.gd->datm).sra32(31);

	if (m_sel.datm)
	{
		if (m_sel.fpsm == 2)
		{
			gsAsm->SHL(Q1.S4(), _fd.S4(), 16);
			gsAsm->USHR(Q1.S4(), Q1.S4(), 31);
			gsAsm->CMEQ(Q1.S4(), Q1.S4(), 0);
		}
		else
		{
			gsAsm->MVN(Q1.B16(), _fd.B16());
			gsAsm->SSHR(Q1.S4(), Q1.S4(), 31);
		}
	}
	else
	{
		if (m_sel.fpsm == 2)
		{
			gsAsm->SHL(Q1.S4(), _fd.S4(), 16);
			gsAsm->SSHR(Q1.S4(), Q1.S4(), 31);
		}
		else
		{
			gsAsm->SSHR(Q1.S4(), _fd.S4(), 31);
		}
	}

	gsAsm->ORR(_test.B16(), _test.B16(), Q1.B16());

	alltrue(_test, _vscratch);
}

void GSDrawScanlineCodeGenerator::WriteMask()
{
	if (m_sel.notest)
	{
		return;
	}

	// fm |= test;
	// zm |= test;

	if (m_sel.fwrite)
		gsAsm->ORR(Q3.B16(), Q3.B16(), _test.B16());

	if (m_sel.zwrite)
		gsAsm->ORR(Q4.B16(), Q4.B16(), _test.B16());

	// int fzm = ~(fm == GSVector4i::xffffffff()).ps32(zm == GSVector4i::xffffffff()).mask();

	gsAsm->MOVI(Q1.S4(), 0xFFFFFFFFu);

	if (m_sel.fwrite && m_sel.zwrite)
	{
		gsAsm->CMEQ(Q0.S4(), Q1.S4(), Q4.S4());
		gsAsm->CMEQ(Q1.S4(), Q1.S4(), Q3.S4());
		gsAsm->SQXTN(oak::VReg_4H(Q1.index()), Q1.S4());
		gsAsm->SQXTN2(Q1.H8(), Q0.S4());
	}
	else if (m_sel.fwrite)
	{
		gsAsm->CMEQ(_vscratch.S4(), Q1.S4(), Q3.S4());
		gsAsm->SQXTN(oak::VReg_4H(Q1.index()), _vscratch.S4());
		gsAsm->SQXTN2(Q1.H8(), _vscratch.S4());
	}
	else if (m_sel.zwrite)
	{
		// Both halves narrow from the register the compare wrote. Narrowing from
		// anything else gives a depth-only draw an arbitrary write mask.
		gsAsm->CMEQ(_vscratch.S4(), Q1.S4(), Q4.S4());
		gsAsm->SQXTN(oak::VReg_4H(Q1.index()), _vscratch.S4());
		gsAsm->SQXTN2(Q1.H8(), _vscratch.S4());
	}

	gsAsm->AND(Q1.B16(), Q1.B16(), _const_movemskw_mask.B16());
	gsAsm->ADDV(oak::HReg(Q1.index()), Q1.H8());
	gsAsm->UMOV(W1, Q1.H8(), 0);
	gsAsm->MVN(W1, W1);
}

void GSDrawScanlineCodeGenerator::WriteZBuf()
{
	if (!m_sel.zwrite)
	{
		return;
	}

	gsAsm->MOV(Q1, m_sel.prim != GS_SPRITE_CLASS ? _temp_zs : _temp_z0);

	if (m_sel.ztest && m_sel.zpsm < 2)
	{
		// zs = zs.blend8(zd, zm);

		blend8(Q1, _temp_zd, Q4, _vscratch);
	}

	// Clamp Z to ZPSM_FMT_MAX
	if (m_sel.zclamp)
	{
		gsAsm->MOVI(Q7.S4(), 0xFFFFFFFFu >> (u8)((m_sel.zpsm & 0x3) * 8));
		gsAsm->SMIN(Q1.S4(), Q1.S4(), Q7.S4());
	}

	bool fast = m_sel.ztest ? m_sel.zpsm < 2 : m_sel.zpsm == 0 && m_sel.notest;

	WritePixel(Q1, W9, W1, true, fast, m_sel.zpsm, 1);
}

void GSDrawScanlineCodeGenerator::AlphaBlend()
{
	if (!m_sel.fwrite)
	{
		return;
	}

	if (m_sel.abe == 0 && m_sel.aa1 == 0)
	{
		return;
	}

	if (((m_sel.aba != m_sel.abb) && (m_sel.aba == 1 || m_sel.abb == 1 || m_sel.abc == 1)) || m_sel.abd == 1)
	{
		switch (m_sel.fpsm)
		{
			case 0:
			case 1:

				// c[2] = fd & mask;
				// c[3] = (fd >> 8) & mask;

				split16_2x8(Q0, Q1, Q2);

				break;

			case 2:

				// c[2] = ((fd & 0x7c00) << 9) | ((fd & 0x001f) << 3);
				// c[3] = ((fd & 0x8000) << 8) | ((fd & 0x03e0) >> 2);

				gsAsm->MOVI(Q7.S4(), 0x1F);
				gsAsm->AND(Q0.B16(), Q2.B16(), Q7.B16());
				gsAsm->SHL(Q0.S4(), Q0.S4(), 3);

				gsAsm->MOVI(Q7.S4(), 0x7C00);
				gsAsm->AND(Q4.B16(), Q2.B16(), Q7.B16());
				gsAsm->MOVI(Q7.S4(), 0x3E0);
				gsAsm->SHL(Q4.S4(), Q4.S4(), 9);

				gsAsm->ORR(Q0.B16(), Q0.B16(), Q4.B16());

				gsAsm->AND(Q1.B16(), Q2.B16(), Q7.B16());
				gsAsm->USHR(Q1.S4(), Q1.S4(), 2);

				gsAsm->MOVI(Q7.S4(), 0x8000);
				gsAsm->AND(Q4.B16(), Q2.B16(), Q7.B16());
				gsAsm->SHL(Q4.S4(), Q4.S4(), 8);

				gsAsm->ORR(Q1.B16(), Q1.B16(), Q4.B16());
				break;
		}
	}

	if (m_sel.pabe || ((m_sel.aba != m_sel.abb) && (m_sel.abb == 0 || m_sel.abd == 0)))
	{
		// movdqa(Q4, Q5);
		gsAsm->MOV(Q4, Q5);
	}

	if (m_sel.aba != m_sel.abb)
	{
		// rb = c[aba * 2 + 0];

		switch (m_sel.aba)
		{
			case 0:
				break;
			case 1:
				gsAsm->MOV(Q5, Q0);
				break;
			case 2:
				gsAsm->MOVI(Q5.B16(), 0);
				break;
		}

		// rb = rb.sub16(c[abb * 2 + 0]);

		switch (m_sel.abb)
		{
			case 0:
				gsAsm->SUB(Q5.H8(), Q5.H8(), Q4.H8());
				break;
			case 1:
				gsAsm->SUB(Q5.H8(), Q5.H8(), Q0.H8());
				break;
			case 2:
				break;
		}

		if (!(m_sel.fpsm == 1 && m_sel.abc == 1))
		{
			// GSVector4i a = abc < 2 ? c[abc * 2 + 1].yywwlh().sll16(7) : m_local.gd->afix;

			switch (m_sel.abc)
			{
				case 0:
				case 1:
					gsAsm->TRN2(Q7.H8(), m_sel.abc ? Q1.H8() : Q6.H8(), m_sel.abc ? Q1.H8() : Q6.H8());
					gsAsm->SHL(Q7.H8(), Q7.H8(), 7);
					break;
				case 2:
					gsAsm->LDR(Q7, _global(afix));
					break;
			}

			// rb = rb.modulate16<1>(a);

			modulate16(Q5, Q7, 1);
		}

		// rb = rb.add16(c[abd * 2 + 0]);

		switch (m_sel.abd)
		{
			case 0:
				gsAsm->ADD(Q5.H8(), Q5.H8(), Q4.H8());
				break;
			case 1:
				gsAsm->ADD(Q5.H8(), Q5.H8(), Q0.H8());
				break;
			case 2:
				break;
		}
	}
	else
	{
		// rb = c[abd * 2 + 0];

		switch (m_sel.abd)
		{
			case 0:
				break;
			case 1:
				gsAsm->MOV(Q5, Q0);
				break;
			case 2:
				gsAsm->MOVI(Q5.B16(), 0);
				break;
		}
	}

	if (m_sel.pabe)
	{
		// mask = (c[1] << 8).sra32(31);

		gsAsm->SHL(Q0.S4(), Q6.S4(), 8);
		gsAsm->SSHR(Q0.S4(), Q0.S4(), 31);

		// rb = c[0].blend8(rb, mask);

		blend8r(Q5, Q4, Q0, _vscratch);
	}

	gsAsm->MOV(Q4, Q6);

	if (m_sel.aba != m_sel.abb)
	{
		// ga = c[aba * 2 + 1];

		switch (m_sel.aba)
		{
			case 0:
				break;
			case 1:
				gsAsm->MOV(Q6, Q1);
				break;
			case 2:
				gsAsm->MOVI(Q6.B16(), 0);
				break;
		}

		// ga = ga.sub16(c[abeb * 2 + 1]);

		switch (m_sel.abb)
		{
			case 0:
				gsAsm->SUB(Q6.H8(), Q6.H8(), Q4.H8());
				break;
			case 1:
				gsAsm->SUB(Q6.H8(), Q6.H8(), Q1.H8());
				break;
			case 2:
				break;
		}

		if (!(m_sel.fpsm == 1 && m_sel.abc == 1))
		{
			// ga = ga.modulate16<1>(a);

			modulate16(Q6, Q7, 1);
		}

		// ga = ga.add16(c[abd * 2 + 1]);

		switch (m_sel.abd)
		{
			case 0:
				gsAsm->ADD(Q6.H8(), Q6.H8(), Q4.H8());
				break;
			case 1:
				gsAsm->ADD(Q6.H8(), Q6.H8(), Q1.H8());
				break;
			case 2:
				break;
		}
	}
	else
	{
		// ga = c[abd * 2 + 1];

		switch (m_sel.abd)
		{
			case 0:
				break;
			case 1:
				gsAsm->MOV(Q6, Q1);
				break;
			case 2:
				gsAsm->MOVI(Q6.B16(), 0);
				break;
		}
	}

	if (m_sel.pabe)
	{
		gsAsm->USHR(Q0.S4(), Q0.S4(), 16); // zero out high words to select the source alpha in blend (so it also does mix16)

		// ga = c[1].blend8(ga, mask).mix16(c[1]);

		blend8r(Q6, Q4, Q0, _vscratch);
	}
	else
	{
		if (m_sel.fpsm != 1) // TODO: fm == 0xffxxxxxx
		{
			mix16(Q6, Q4, Q7);
		}
	}
}

void GSDrawScanlineCodeGenerator::WriteFrame()
{
	if (!m_sel.fwrite)
	{
		return;
	}

	if (m_sel.dthe && m_sel.fpsm != 3)
	{
		gsAsm->AND(W5, _top, 3);
		gsAsm->LSL(W5, W5, 5);
		gsAsm->LDR(_vscratch, _global_dimx, X5);
		gsAsm->ADD(X5, X5, sizeof(GSVector4i));
		gsAsm->LDR(_vscratch2, _global_dimx, X5);
		gsAsm->ADD(Q5.H8(), Q5.H8(), _vscratch.H8());
		gsAsm->ADD(Q6.H8(), Q6.H8(), _vscratch2.H8());
	}

	if (m_sel.colclamp == 0)
	{
		// c[0] &= 0x000000ff;
		// c[1] &= 0x000000ff;

		gsAsm->MOVI(Q7.H8(), 0xFF);

		gsAsm->AND(Q5.B16(), Q5.B16(), Q7.B16());
		gsAsm->AND(Q6.B16(), Q6.B16(), Q7.B16());
	}

	// GSVector4i fs = c[0].upl16(c[1]).pu16(c[0].uph16(c[1]));

	gsAsm->ZIP2(Q7.H8(), Q5.H8(), Q6.H8());
	gsAsm->ZIP1(Q5.H8(), Q5.H8(), Q6.H8());
	gsAsm->SQXTUN(oak::VReg_8B(Q5.index()), Q5.H8());
	gsAsm->SQXTUN2(Q5.B16(), Q7.H8());

	if (m_sel.fba && m_sel.fpsm != 1)
	{
		// fs |= 0x80000000;

		gsAsm->MOVI(Q7.S4(), 0x80000000);
		gsAsm->ORR(Q5.B16(), Q5.B16(), Q7.B16());
	}

	if (m_sel.fpsm == 2)
	{
		// GSVector4i rb = fs & 0x00f800f8;
		// GSVector4i ga = fs & 0x8000f800;

		gsAsm->MOVI(Q6.S4(), 0x00f800f8);

		gsAsm->MOVI(Q7.S4(), 0x8000f800);

		gsAsm->AND(Q4.B16(), Q5.B16(), Q6.B16());
		gsAsm->AND(Q5.B16(), Q5.B16(), Q7.B16());

		// fs = (ga >> 16) | (rb >> 9) | (ga >> 6) | (rb >> 3);

		gsAsm->USHR(Q6.S4(), Q4.S4(), 9);
		gsAsm->USHR(Q7.S4(), Q5.S4(), 16);
		gsAsm->USHR(Q4.S4(), Q4.S4(), 3);
		gsAsm->USHR(Q5.S4(), Q5.S4(), 6);

		gsAsm->ORR(Q5.B16(), Q5.B16(), Q4.B16());
		gsAsm->ORR(Q7.B16(), Q7.B16(), Q6.B16());
		gsAsm->ORR(Q5.B16(), Q5.B16(), Q7.B16());
	}

	oak::QReg pixel = m_sel.rfb ? Q3 : Q5;
	if (m_sel.rfb)
	{
		// fs = fs.blend(fd, fm);

		gsAsm->BSL(Q3.B16(), Q2.B16(), Q5.B16());
	}

	const bool fast = m_sel.rfb ? m_sel.fpsm < 2 : m_sel.fpsm == 0 && m_sel.notest;

	WritePixel(pixel, W6, W1, false, fast, m_sel.fpsm, 0);
}

void GSDrawScanlineCodeGenerator::ReadPixel(oak::QReg dst, oak::WReg addr)
{
	pxAssert(addr.IsW());
	gsAsm->LSL(_wscratch, addr, 1); // *2
	gsAsm->LDR(oak::DReg(dst.index()), _vm, _xscratch);
	gsAsm->ADD(_scratchaddr, _vm_high, _xscratch);
	gsAsm->LD1(dst.D2(), 1, OakMemOperand{_scratchaddr, static_cast<s64>(0)});
}

void GSDrawScanlineCodeGenerator::WritePixel(oak::QReg src, oak::WReg addr, oak::WReg mask, bool high, bool fast, int psm, int fz)
{
	pxAssert(addr.IsW() && mask.IsW());
	if (m_sel.notest)
	{
		if (fast)
		{
			gsAsm->LSL(_wscratch, addr, 1); // *2
			gsAsm->STR(oak::DReg(src.index()), _vm, _xscratch);
			gsAsm->ADD(_scratchaddr, _vm_high, _xscratch);
			gsAsm->ST1(src.D2(), 1, OakMemOperand{_scratchaddr, static_cast<s64>(0)});
		}
		else
		{
			WritePixel(src, addr, 0, psm);
			WritePixel(src, addr, 1, psm);
			WritePixel(src, addr, 2, psm);
			WritePixel(src, addr, 3, psm);
		}
	}
	else
	{
		if (fast)
		{
			// if (fzm & 0x0f) GSVector4i::storel(&vm16[addr + 0], fs);
			// if (fzm & 0xf0) GSVector4i::storeh(&vm16[addr + 8], fs);

			oak::Label skip_low, skip_high;
			gsAsm->LSL(_wscratch, addr, 1); // *2

			gsAsm->TST(mask, high ? 0x0F00 : 0x0F);
			gsAsm->B(oak::Cond::EQ, skip_low);
			gsAsm->STR(oak::DReg(src.index()), _vm, _xscratch);
			gsAsm->l(skip_low);

			gsAsm->TST(mask, high ? 0xF000 : 0xF0);
			gsAsm->B(oak::Cond::EQ, skip_high);
			gsAsm->ADD(_scratchaddr, _vm_high, _xscratch);
			gsAsm->ST1(src.D2(), 1, OakMemOperand{_scratchaddr, static_cast<s64>(0)});
			gsAsm->l(skip_high);
		}
		else
		{
			// if (fzm & 0x03) WritePixel(fpsm, &vm16[addr + 0], fs.extract32<0>());
			// if (fzm & 0x0c) WritePixel(fpsm, &vm16[addr + 2], fs.extract32<1>());
			// if (fzm & 0x30) WritePixel(fpsm, &vm16[addr + 8], fs.extract32<2>());
			// if (fzm & 0xc0) WritePixel(fpsm, &vm16[addr + 10], fs.extract32<3>());

			oak::Label skip_0, skip_1, skip_2, skip_3;

			gsAsm->TST(mask, high ? 0x0300 : 0x03);
			gsAsm->B(oak::Cond::EQ, skip_0);
			WritePixel(src, addr, 0, psm);
			gsAsm->l(skip_0);

			gsAsm->TST(mask, high ? 0x0c00 : 0x0c);
			gsAsm->B(oak::Cond::EQ, skip_1);
			WritePixel(src, addr, 1, psm);
			gsAsm->l(skip_1);

			gsAsm->TST(mask, high ? 0x3000 : 0x30);
			gsAsm->B(oak::Cond::EQ, skip_2);
			WritePixel(src, addr, 2, psm);
			gsAsm->l(skip_2);

			gsAsm->TST(mask, high ? 0xc000 : 0xc0);
			gsAsm->B(oak::Cond::EQ, skip_3);
			WritePixel(src, addr, 3, psm);
			gsAsm->l(skip_3);
		}
	}
}

static const int s_offsets[4] = {0, 2, 8, 10};

void GSDrawScanlineCodeGenerator::WritePixel(oak::QReg src, oak::WReg addr, u8 i, int psm)
{
	pxAssert(addr.IsW());
	// Address dst = ptr[addr * 2 + (size_t)m_local.gd->vm + s_offsets[i] * 2];
	gsAsm->LSL(_wscratch, addr, 1); // *2
	gsAsm->ADD(_scratchaddr, _vm, s_offsets[i] * 2);

	switch (psm)
	{
		case 0:
			if (i == 0)
			{
				gsAsm->STR(oak::SReg(src.index()), _scratchaddr, _xscratch);
			}
			else
			{
				gsAsm->ADD(_scratchaddr, _scratchaddr, _xscratch);
				gsAsm->ST1(src.S4(), i, OakMemOperand{_scratchaddr, static_cast<s64>(0)});
			}
			break;
		case 1:
			gsAsm->LDR(_wscratch2, _scratchaddr, _xscratch);

			gsAsm->MOV(W5, src.S4(), i);

			gsAsm->EOR(W5, W5, _wscratch2);
			gsAsm->AND(W5, W5, 0xffffff);
			gsAsm->EOR(_wscratch2, _wscratch2, W5);
			gsAsm->STR(_wscratch2, _scratchaddr, _xscratch);

			break;
		case 2:
			gsAsm->UMOV(W5, src.H8(), i * 2);
			gsAsm->STRH(W5, _scratchaddr, _xscratch);
			break;
	}
}

void GSDrawScanlineCodeGenerator::ReadTexel1(oak::QReg dst, oak::QReg src, oak::QReg tmp1, int mip_offset)
{
	const oak::QReg no(31); // Hopefully this will assert if we accidentally use it
	ReadTexelImpl(dst, tmp1, src, no, no, no, 1, mip_offset);
}

void GSDrawScanlineCodeGenerator::ReadTexel4(
	oak::QReg D0, oak::QReg D1,
	oak::QReg d2s0, oak::QReg d3s1,
	oak::QReg S2, oak::QReg S3,
	int mip_offset)
{
	ReadTexelImpl(D0, D1, d2s0, d3s1, S2, S3, 4, mip_offset);
}

void GSDrawScanlineCodeGenerator::ReadTexelImplLoadTexLOD(oak::XReg addr, int lod, int mip_offset)
{
	pxAssert(addr.IsX());
	pxAssert(m_sel.mmin);
	// Runtime-indexed U32 lane: hand-compute the offset since `lod` isn't constexpr.
	const size_t lod_lane = static_cast<size_t>(lod) * sizeof(u32);
	gsAsm->LDR(addr.toW(), m_sel.lcm
		? OakMemOperand{_globals, static_cast<s64>(offsetof(GSScanlineGlobalData, lod.i.U32) + lod_lane)}
		: OakMemOperand{_locals, static_cast<s64>(offsetof(GSScanlineLocalData, temp.lod.i.U32) + lod_lane)});
	if (mip_offset != 0)
		gsAsm->ADD(addr.toW(), addr.toW(), mip_offset);
	gsAsm->LDR(addr.toX(), _global_tex0, addr, oak::IndexExt::LSL, 3);
}

void GSDrawScanlineCodeGenerator::ReadTexelImpl(
	oak::QReg D0, oak::QReg D1,
	oak::QReg d2s0, oak::QReg d3s1,
	oak::QReg S2, oak::QReg S3,
	int pixels, int mip_offset)
{
	//mip_offset *= wordsize;

	const bool preserve[] = {false, false, true, true};
	const oak::QReg dst[] = {D0, D1, d2s0, d3s1};
	const oak::QReg src[] = {d2s0, d3s1, S2, S3};

	if (m_sel.mmin && !m_sel.lcm)
	{
		for (int j = 0; j < 4; j++)
		{
			ReadTexelImplLoadTexLOD(_xscratch, j, mip_offset);

			for (int i = 0; i < pixels; i++)
			{
				ReadTexelImpl(dst[i], src[i], j, _xscratch, preserve[i]);
			}
		}
	}
	else
	{
		oak::XReg base_register(_global_tex0);

		if (m_sel.mmin && m_sel.lcm)
		{
			ReadTexelImplLoadTexLOD(_xscratch, 0, mip_offset);
			base_register = _xscratch;
		}

		for (int i = 0; i < pixels; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				ReadTexelImpl(dst[i], src[i], j, base_register, false);
			}
		}
	}
}

void GSDrawScanlineCodeGenerator::ReadTexelImpl(oak::QReg dst,
	oak::QReg addr, u8 i, oak::XReg baseRegister, bool preserveDst)
{
	// const Address& src = m_sel.tlu ? ptr[W1 + W5 * 4] : ptr[W6 + W5 * 4];
	pxAssert(baseRegister.index() != _scratchaddr.index());
	pxAssert(baseRegister.IsX());
	gsAsm->MOV(_scratchaddr.toW(), addr.S4(), i);

	if (m_sel.tlu)
	{
		gsAsm->LDRB(_scratchaddr.toW(), baseRegister, _scratchaddr);

		gsAsm->ADD(_scratchaddr, _global_clut, _scratchaddr.toW(), oak::AddSubExt::UXTW, 2);
		if (i == 0 && !preserveDst)
			gsAsm->LDR(oak::SReg(dst.index()), OakMemOperand{_scratchaddr, static_cast<s64>(0)});
		else
			gsAsm->LD1(dst.S4(), i, OakMemOperand{_scratchaddr, static_cast<s64>(0)});
	}
	else
	{
		gsAsm->ADD(_scratchaddr, baseRegister, _scratchaddr.toW(), oak::AddSubExt::UXTW, 2);
		if (i == 0 && !preserveDst)
			gsAsm->LDR(oak::SReg(dst.index()), OakMemOperand{_scratchaddr, static_cast<s64>(0)});
		else
			gsAsm->LD1(dst.S4(), i, OakMemOperand{_scratchaddr, static_cast<s64>(0)});
	}
}


void GSDrawScanlineCodeGenerator::modulate16(oak::QReg a, oak::QReg f, u8 shift)
{
	modulate16(a, a, f, shift);
}

void GSDrawScanlineCodeGenerator::modulate16(oak::QReg d, oak::QReg a, oak::QReg f, u8 shift)
{
	// GSVector4i::modulate16<shift>(f) is `sll16<shift + 1>().mul16hs(f)`: the
	// shift happens in 16-bit lanes and wraps, then the widening multiply takes
	// the product's high half.
	//
	// SQDMULH after a shift of `shift` agrees on every reachable lane but differs
	// once a lane reaches 8192 (the reference wraps `a << (shift + 1)`) and it
	// saturates where the reference does not. Matching the reference keeps X86,
	// the C++ fallback and this path identical.
	//
	// Shifting by shift + 1 puts the 16-bit wrap where the reference has it;
	// halving afterwards is exact because the shifted value is even, and SQDMULH's
	// doubling puts it back. Three instructions, no extra register.
	gsAsm->SHL(d.H8(), a.H8(), shift + 1);
	gsAsm->SSHR(d.H8(), d.H8(), 1);
	gsAsm->SQDMULH(d.H8(), d.H8(), f.H8());
}

// The walk's carried colour as the stored byte. SQSHRUN is a signed shift
// right by seven then unsigned saturating narrow, so a lane carried below zero
// saturates to 0 instead of becoming 511; UXTL widens back to sixteen bits.
// The top saturation changes nothing: 32767 >> 7 is already 255.
//
// The walk itself is not clamped; see the C++ twin in GSDrawScanline.cpp.
void GSDrawScanlineCodeGenerator::walkColorByte(oak::QReg d, oak::QReg c)
{
	gsAsm->SQSHRUN(oak::VReg_8B(d.index()), c.H8(), 7);
	gsAsm->UXTL(d.H8(), oak::VReg_8B(d.index()));
}

// The stored eight-bit colour, put back on the seven-fraction grid the
// modulate expects. The texture function multiplies the stored byte, not the
// wider DDA value; same rule as GSStoredVertexColor in GSDrawScanline.cpp.
void GSDrawScanlineCodeGenerator::storedVertexColor(oak::QReg d, oak::QReg c)
{
	walkColorByte(d, c);
	gsAsm->SHL(d.H8(), d.H8(), 7);
}

void GSDrawScanlineCodeGenerator::lerp16(oak::QReg a, oak::QReg b, oak::QReg f, u8 shift)
{
	gsAsm->SUB(a.H8(), a.H8(), b.H8());
	modulate16(a, f, shift);
	gsAsm->ADD(a.H8(), a.H8(), b.H8());
}

void GSDrawScanlineCodeGenerator::lerp16_4(oak::QReg a, oak::QReg b, oak::QReg f)
{
	gsAsm->SUB(a.H8(), a.H8(), b.H8());
	gsAsm->MUL(a.H8(), a.H8(), f.H8());
	gsAsm->SSHR(a.H8(), a.H8(), 4);
	gsAsm->ADD(a.H8(), a.H8(), b.H8());
}

void GSDrawScanlineCodeGenerator::mix16(oak::QReg a, oak::QReg b, oak::QReg temp)
{
	pxAssert(a.index() != temp.index() && b.index() != temp.index());

	gsAsm->MOV(temp, a);
	gsAsm->MOVI(a.S4(), 0xFFFF0000);
	gsAsm->BSL(a.B16(), b.B16(), temp.B16());
}

void GSDrawScanlineCodeGenerator::clamp16(oak::QReg a, oak::QReg temp)
{
	gsAsm->SQXTUN(oak::VReg_8B(a.index()), a.H8());
	gsAsm->USHLL(a.H8(), oak::VReg_8B(a.index()), 0);
}

void GSDrawScanlineCodeGenerator::alltrue(oak::QReg test, oak::QReg temp)
{
	gsAsm->UMINV(oak::SReg(temp.index()), test.S4());
	gsAsm->FMOV(_wscratch, oak::SReg(temp.index()));
	gsAsm->CMN(_wscratch, 1);
	gsAsm->B(oak::Cond::EQ, m_step_label);
}

void GSDrawScanlineCodeGenerator::blend8(oak::QReg a, oak::QReg b, oak::QReg mask, oak::QReg temp)
{
	gsAsm->SSHR(temp.B16(), mask.B16(), 7);
	gsAsm->BSL(temp.B16(), b.B16(), a.B16());
	gsAsm->MOV(a, temp);
}

void GSDrawScanlineCodeGenerator::blend8r(oak::QReg b, oak::QReg a, oak::QReg mask, oak::QReg temp)
{
	gsAsm->SSHR(temp.B16(), mask.B16(), 7);
	gsAsm->BSL(temp.B16(), b.B16(), a.B16());
	gsAsm->MOV(b, temp);
}

void GSDrawScanlineCodeGenerator::split16_2x8(oak::QReg l, oak::QReg h, oak::QReg src)
{
	// l = src & 0xFF; (1 left shift + 1 right shift)
	// h = (src >> 8) & 0xFF; (1 right shift)

	if (src.index() == h.index())
	{
		gsAsm->MOV(l, src);
		gsAsm->USHR(h.H8(), src.H8(), 8);
		gsAsm->BIC(l.H8(), 0xFF, 8);
	}
	else if (src.index() == l.index())
	{
		gsAsm->USHR(h.H8(), src.H8(), 8);
		gsAsm->BIC(l.H8(), 0xFF, 8);
	}
	else
	{
		gsAsm->MOV(l, src);
		gsAsm->USHR(h.H8(), src.H8(), 8);
		gsAsm->BIC(l.H8(), 0xFF, 8);
	}
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
