// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#include "GS/Renderers/SW/GSSetupPrimCodeGenerator.arm64.h"
#include "GS/Renderers/SW/GSVertexSW.h"

#include "common/StringUtil.h"
#include "common/Perf.h"

#include <cstdint>

MULTI_ISA_UNSHARED_IMPL;

using namespace oak::util;

static constexpr oak::XReg _vertex{0};
static constexpr oak::XReg _index{1};
static constexpr oak::XReg _dscan{2};
static constexpr oak::XReg _locals{3};
static constexpr oak::XReg _scratchaddr{7};
static constexpr oak::QReg _vscratch{31};

// Yay, you can't offsetof with non-constant array indices in GCC
#define OFFSETOF(base, field) (reinterpret_cast<uptr>(&reinterpret_cast<base*>(0)->field))
#define _local(field) OakMemOperand{_locals, static_cast<s64>(OFFSETOF(GSScanlineLocalData, field))}

static void gsLoad16(oak::WReg dst, OakMemOperand mem)
{
	oakLoad16(dst, mem);
}

static void gsLoad128(oak::QReg dst, OakMemOperand mem)
{
	oakLoad128(dst, mem);
}

static void gsStore128(oak::QReg src, OakMemOperand mem)
{
	oakStore128(src, mem);
}

static void gsLoadReplicate32(oak::QReg dst, OakMemOperand mem)
{
	oakLoad32(OAK_WSCRATCH, mem);
	oakAsm->DUP(dst.S4(), OAK_WSCRATCH);
}

static void gsLoadReplicate64(oak::QReg dst, OakMemOperand mem)
{
	oakLoad64(OAK_XSCRATCH, mem);
	oakAsm->DUP(dst.D2(), OAK_XSCRATCH);
}

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
	oakAsm = &emitter;

	const bool needs_shift = ((m_en.z || m_en.f) && m_sel.prim != GS_SPRITE_CLASS) || m_en.t || (m_en.c && m_sel.iip);
	if (needs_shift)
	{
		oakAsm->MOVP2R(X4, g_const.m_shift_128b);
		for (int i = 0; i < (m_sel.notest ? 2 : 5); i++)
		{
			gsLoad128(oak::QReg(3 + i), OakMemOperand{X4, static_cast<s64>(i * sizeof(g_const.m_shift_128b[0]))});
		}
	}

	Depth();

	Texture();

	Color();

	oakAsm->RET();

	m_size = static_cast<size_t>(emitter.offset());
	if (m_size >= m_capacity)
		pxAssert(false);
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
		if (m_en.f)
		{
			// GSVector4 df = t.wwww();
			oakAsm->ADD(_scratchaddr, _dscan, offsetof(GSVertexSW, t.w));
			gsLoadReplicate32(Q1, OakMemOperand{_scratchaddr, 0});

			// m_local.d4.f = GSVector4i(df * 4.0f).xxzzlh();
			oakAsm->FMUL(Q2.S4(), Q1.S4(), Q3.S4());
			oakAsm->FCVTZS(Q2.S4(), Q2.S4());
			oakAsm->TRN1(Q2.H8(), Q2.H8(), Q2.H8());

			gsStore128(Q2, _local(d4.f));

			for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
			{
				// m_local.d[i].f = GSVector4i(df * m_shift[i]).xxzzlh();
				oakAsm->FMUL(Q2.S4(), Q1.S4(), oak::QReg(4 + i).S4());
				oakAsm->FCVTZS(Q2.S4(), Q2.S4());
				oakAsm->TRN1(Q2.H8(), Q2.H8(), Q2.H8());

				gsStore128(Q2, _local(d[i].f));
			}
		}

		if (m_en.z)
		{
			// VectorF dz = VectorF::broadcast64(&dscan.p.z)
			oakAsm->ADD(_scratchaddr, _dscan, offsetof(GSVertexSW, p.z));
			gsLoadReplicate64(_vscratch, OakMemOperand{_scratchaddr, 0});

			// m_local.d4.z = dz.mul64(GSVector4::f32to64(shift));
			oakAsm->FCVTL(Q1.D2(), Q3.toD().S2());
			oakAsm->FMUL(Q1.D2(), Q1.D2(), _vscratch.D2());
			gsStore128(Q1, _local(d4.z));

			oakAsm->FCVTN(Q0.toD().S2(), _vscratch.D2());
			oakAsm->FCVTN2(Q0.S4(), _vscratch.D2());

			for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
			{
				// m_local.d[i].z0 = dz.mul64(VectorF::f32to64(half_shift[2 * i + 2]));
				// m_local.d[i].z1 = dz.mul64(VectorF::f32to64(half_shift[2 * i + 3]));
				oakAsm->FMUL(Q1.S4(), Q0.S4(), oak::QReg(4 + i).S4());
				gsStore128(Q1, _local(d[i].z));
			}
		}
	}
	else
	{
		// GSVector4 p = vertex[index[1]].p;
		gsLoad16(W4, OakMemOperand{_index, sizeof(u16)});
		oakAsm->LSL(W4, W4, 6); // * sizeof(GSVertexSW)
		oakAsm->ADD(X4, _vertex, X4);

		if (m_en.f)
		{
			// m_local.p.f = GSVector4i(p).zzzzh().zzzz();
			gsLoad128(Q0, OakMemOperand{X4, offsetof(GSVertexSW, p)});

			oakAsm->FCVTZS(Q1.S4(), Q0.S4());
			oakAsm->DUP(Q1.H8(), Q1.Helem()[6]);

			gsStore128(Q1, OakMemOperand{_locals, offsetof(GSScanlineLocalData, p.f)});
		}

		if (m_en.z)
		{
			// uint32 z is bypassed in t.w
			oakAsm->ADD(_scratchaddr, X4, offsetof(GSVertexSW, t.w));
			gsLoadReplicate32(Q0, OakMemOperand{_scratchaddr, 0});
			gsStore128(Q0, OakMemOperand{_locals, offsetof(GSScanlineLocalData, p.z)});
		}
	}
}

void GSSetupPrimCodeGenerator::Texture()
{
	if (!m_en.t)
	{
		return;
	}

	// GSVector4 t = dscan.t;
	gsLoad128(Q0, OakMemOperand{_dscan, offsetof(GSVertexSW, t)});
	oakAsm->FMUL(Q1.S4(), Q0.S4(), Q3.S4());

	if (m_sel.fst)
	{
		// m_local.d4.stq = GSVector4i(t * 4.0f);
		oakAsm->FCVTZS(Q1.S4(), Q1.S4());
		gsStore128(Q1, OakMemOperand{_locals, offsetof(GSScanlineLocalData, d4.stq)});
	}
	else
	{
		// m_local.d4.stq = t * 4.0f;
		gsStore128(Q1, OakMemOperand{_locals, offsetof(GSScanlineLocalData, d4.stq)});
	}

	for (int j = 0, k = m_sel.fst ? 2 : 3; j < k; j++)
	{
		// GSVector4 ds = t.xxxx();
		// GSVector4 dt = t.yyyy();
		// GSVector4 dq = t.zzzz();
		oakAsm->DUP(Q1.S4(), Q0.Selem()[j]);

		for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
		{
			// GSVector4 v = ds/dt * m_shift[i];
			oakAsm->FMUL(Q2.S4(), Q1.S4(), oak::QReg(4 + i).S4());

			if (m_sel.fst)
			{
				// m_local.d[i].s/t = GSVector4i(v);
				oakAsm->FCVTZS(Q2.S4(), Q2.S4());

				switch (j)
				{
					case 0: gsStore128(Q2, _local(d[i].s)); break;
					case 1: gsStore128(Q2, _local(d[i].t)); break;
				}
			}
			else
			{
				// m_local.d[i].s/t/q = v;
				switch (j)
				{
					case 0: gsStore128(Q2, _local(d[i].s)); break;
					case 1: gsStore128(Q2, _local(d[i].t)); break;
					case 2: gsStore128(Q2, _local(d[i].q)); break;
				}
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
		// GSVector4 c = dscan.c;
		gsLoad128(Q16, OakMemOperand{_dscan, offsetof(GSVertexSW, c)});

		// GSVector4i tmp = GSVector4i(dscan.c * step_shift).xzyw();
		// local.d4.c = tmp.uzp1_16(tmp); // Not currently in GSVector since that's mainly targeting x86 for now
		oakAsm->FMUL(Q2.S4(), Q16.S4(), Q3.S4());
		oakAsm->FCVTZS(Q2.S4(), Q2.S4());
		oakAsm->REV64(_vscratch.S4(), Q2.S4());
		oakAsm->UZP1(Q2.S4(), Q2.S4(), _vscratch.S4());
		oakAsm->UZP1(Q2.H8(), Q2.H8(), Q2.H8());
		gsStore128(Q2, OakMemOperand{_locals, offsetof(GSScanlineLocalData, d4.c)});

		// GSVector4 dr = c.xxxx();
		// GSVector4 db = c.zzzz();
		oakAsm->DUP(Q0.S4(), Q16.Selem()[0]);
		oakAsm->DUP(Q1.S4(), Q16.Selem()[2]);

		for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
		{
			// VectorI r = VectorI(dr * shift[1 + i]);
			oakAsm->FMUL(Q2.S4(), Q0.S4(), oak::QReg(4 + i).S4());
			oakAsm->FCVTZS(Q2.S4(), Q2.S4());

			// VectorI b = VectorI(db * shift[1 + i]);
			oakAsm->FMUL(Q3.S4(), Q1.S4(), oak::QReg(4 + i).S4());
			oakAsm->FCVTZS(Q3.S4(), Q3.S4());

			// m_local.d[i].rb = r.trn1_16(b); // Not currently in GSVector since that's mainly targeting x86 for now
			oakAsm->TRN1(Q2.H8(), Q2.H8(), Q3.H8());
			gsStore128(Q2, _local(d[i].rb));
		}

		// GSVector4 c = dscan.c;
		// GSVector4 dg = c.yyyy();
		// GSVector4 da = c.wwww();
		oakAsm->DUP(Q0.S4(), Q16.Selem()[1]);
		oakAsm->DUP(Q1.S4(), Q16.Selem()[3]);

		for (int i = 0; i < (m_sel.notest ? 1 : 4); i++)
		{
			// VectorI g = VectorI(dg * shift[1 + i]);
			oakAsm->FMUL(Q2.S4(), Q0.S4(), oak::QReg(4 + i).S4());
			oakAsm->FCVTZS(Q2.S4(), Q2.S4());

			// VectorI a = VectorI(da * shift[1 + i]);
			oakAsm->FMUL(Q3.S4(), Q1.S4(), oak::QReg(4 + i).S4());
			oakAsm->FCVTZS(Q3.S4(), Q3.S4());

			// m_local.d[i].ga = g.trn1_16(a); // Not currently in GSVector since that's mainly targeting x86 for now
			oakAsm->TRN1(Q2.H8(), Q2.H8(), Q3.H8());
			gsStore128(Q2, _local(d[i].ga));
		}
	}
	else
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
			gsLoad16(W4, OakMemOperand{_index, static_cast<s64>(sizeof(u16) * last)});
			oakAsm->LSL(W4, W4, 6); // * sizeof(GSVertexSW)
			oakAsm->ADD(X4, _vertex, X4);
		}

		gsLoad128(Q0, OakMemOperand{X4, offsetof(GSVertexSW, c)});
		oakAsm->FCVTZS(Q0.S4(), Q0.S4());

		// c = c.upl16(c.zwxy());
		oakAsm->EXT(Q1.B16(), Q0.B16(), Q0.B16(), 8);
		oakAsm->ZIP1(Q0.H8(), Q0.H8(), Q1.H8());

		// if (!tme) c = c.srl16(7);
		if (m_sel.tfx == TFX_NONE)
			oakAsm->USHR(Q0.H8(), Q0.H8(), 7);

		// m_local.c.rb = c.xxxx();
		// m_local.c.ga = c.zzzz();
		oakAsm->DUP(Q1.S4(), Q0.Selem()[0]);
		oakAsm->DUP(Q2.S4(), Q0.Selem()[2]);

		gsStore128(Q1, _local(c.rb));
		gsStore128(Q2, _local(c.ga));
	}
}
