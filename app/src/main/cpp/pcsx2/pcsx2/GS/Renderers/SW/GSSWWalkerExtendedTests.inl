// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+
// Production-consumer cases ported from the pinned upstream ctest suites.
#if __has_include("GS/Renderers/SW/GSBlockWalk.h")
#include "GS/Renderers/SW/GSBlockWalk.h"
#include "GS/GSLocalMemory.h"
#include "GS/GSState.h"
#include "GS/MultiISA.h"
#include "GS/Renderers/SW/GSBlockWalk.h"
#include "GS/Renderers/SW/GSColourWalk.h"
#include "GS/Renderers/SW/GSDrawScanline.h"
#include "GS/Renderers/SW/GSDrawScanlineCodeGenerator.arm64.h"
#include "GS/Renderers/SW/GSLevelOfDetail.h"
#include "GS/Renderers/SW/GSPerspectivePlane.h"
#include "GS/Renderers/SW/GSRasterizer.h"
#include "GS/Renderers/SW/GSScanlineEnvironment.h"
#include "GS/Renderers/SW/GSSetupPrimCodeGenerator.arm64.h"
#include "GS/Renderers/SW/GSVertexSW.h"
#include "common/HostSys.h"
#include "common/Pcsx2Defs.h"
#include <cmath>
#include <cstring>
#include <sys/mman.h>
#include <vector>

namespace SWParityHarness {
struct Abort {};
struct TestCase { const char* name;const char* suite;void(*setup)();void(*teardown)();void(*run)(); };
inline std::vector<TestCase>& Cases(){static std::vector<TestCase> tests;return tests;}
static bool failed;
struct Expect {
    Expect(bool okay,bool fatal,const char* file,int line) {
        if(!okay) { failed=true;std::fprintf(stderr,"SW_PARITY_ASSERT %s:%d\n",file,line);if(fatal)throw Abort{}; }
    }
    template<class T> Expect& operator<<(const T&){return *this;}
};
struct SWParityTestBase {static void SetUpTestSuite(){}static void TearDownTestSuite(){}};
}
using SWParityHarness::SWParityTestBase;
#define SWPARITY_TEST(fixture,name) \
struct fixture##_##name##_Native: public fixture {void Body();static void SWNativeSetUp(){SetUpTestSuite();}static void SWNativeTearDown(){TearDownTestSuite();}static void SWNativeRun(){fixture##_##name##_Native test;test.Body();}}; \
static const bool fixture##_##name##_registered=[](){SWParityHarness::Cases().push_back({#fixture "." #name,#fixture,&fixture##_##name##_Native::SWNativeSetUp,&fixture##_##name##_Native::SWNativeTearDown,&fixture##_##name##_Native::SWNativeRun});return true;}(); \
void fixture##_##name##_Native::Body()
#define TEST_F(fixture,name) SWPARITY_TEST(fixture,name)
#define TEST(fixture,name) SWPARITY_TEST(SWParityTestBase,fixture##_##name)
#define SWEXPECT(op,a,b,fatal) SWParityHarness::Expect((a) op (b),fatal,__FILE__,__LINE__)
#define EXPECT_EQ(a,b) SWEXPECT(==,a,b,false)
#define EXPECT_NE(a,b) SWEXPECT(!=,a,b,false)
#define EXPECT_LT(a,b) SWEXPECT(<,a,b,false)
#define EXPECT_GT(a,b) SWEXPECT(>,a,b,false)
#define EXPECT_LE(a,b) SWEXPECT(<=,a,b,false)
#define EXPECT_GE(a,b) SWEXPECT(>=,a,b,false)
#define EXPECT_TRUE(a) SWEXPECT(==,bool(a),true,false)
#define EXPECT_FLOAT_EQ(a,b) SWParityHarness::Expect(std::abs(float(a)-float(b))<=std::max(1e-7f,4.0f*std::numeric_limits<float>::epsilon()*std::max(std::abs(float(a)),std::abs(float(b)))),false,__FILE__,__LINE__)
#define ASSERT_EQ(a,b) SWEXPECT(==,a,b,true)
#define ASSERT_NE(a,b) SWEXPECT(!=,a,b,true)
#define ASSERT_GT(a,b) SWEXPECT(>,a,b,true)
#define ASSERT_GE(a,b) SWEXPECT(>=,a,b,true)
#define ASSERT_TRUE(a) SWEXPECT(==,bool(a),true,true)
namespace SWParity_colour_walk_tables {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// The colour and fog step tables a scanline reads, across primitives.
//
// A primitive with a colour walk (a triangle's main pass) fills the tables per
// row. A primitive without one (sprite, line, point, AA1 edge) must see them all
// zero on every row it draws, whatever was drawn before it. The rasterizer is
// free to keep zero tables from one walkless primitive to the next instead of
// rewriting them, so these cases pin what the scanline sees in both orders:
// walked then walkless, and walkless then walkless.
//
// Nothing here compiles a scanline: the setup and scanline callbacks are stubs
// that inspect the tables, so what is under test is the rasterizer's handling of
// them. The rasterizer is per-architecture, so this rides ARCH_ARM64 like its
// siblings.


#ifdef ARCH_ARM64




namespace
{
struct SpanLog
{
	int spans = 0;
	int zero_spans = 0;
	int live_spans = 0;
};

SpanLog g_log;

bool IsZero(const void* p, size_t size)
{
	const u8* b = static_cast<const u8*>(p);
	for (size_t i = 0; i < size; i++)
	{
		if (b[i] != 0)
			return false;
	}
	return true;
}

// Every table SetupColourWalkTables writes.
bool ColourTablesAreZero(const GSScanlineLocalData& local)
{
	for (int s = 0; s < 8; s++)
	{
		if (!IsZero(&local.d[s].rb, sizeof(local.d[s].rb)) || !IsZero(&local.d[s].ga, sizeof(local.d[s].ga)) ||
			!IsZero(&local.d[s].f, sizeof(local.d[s].f)))
			return false;
	}
#if _M_SSE >= 0x501
	return IsZero(&local.d8.c, sizeof(local.d8.c)) && IsZero(&local.d8.p.f, sizeof(local.d8.p.f));
#else
	if (!IsZero(&local.d4.c, sizeof(local.d4.c)) || !IsZero(&local.d4.f, sizeof(local.d4.f)))
		return false;
	return IsZero(local.dw, sizeof(local.dw));
#endif
}

void RecordSpan(int, int, int, const GSVertexSW&, GSScanlineLocalData& local)
{
	g_log.spans++;
	g_log.zero_spans += ColourTablesAreZero(local) ? 1 : 0;
	g_log.live_spans += local.cwalk.live ? 1 : 0;
}

void NoSetup(const GSVertexSW*, const u16*, const GSVertexSW&, GSScanlineLocalData&) {}

class ColourWalkTables : public SWParityTestBase
{
protected:
	isa_native::GSRasterizer m_r{nullptr, 0, 1};

	SpanLog Run(GS_PRIM_CLASS primclass, GSVertexSW* vertex, int count, bool iip)
	{
		static const u16 index[6] = {0, 1, 2, 3, 4, 5};

		g_log = {};

		isa_native::GSRasterizerData data;
		data.primclass = primclass;
		data.vertex = vertex;
		data.vertex_count = count;
		data.index = const_cast<u16*>(index);
		data.index_count = count;
		data.scissor = GSVector4i(0, 0, 256, 256);
		data.bbox = GSVector4i(0, 0, 256, 256);
		data.global.sel.key = 0;
		data.global.sel.iip = iip ? 1 : 0;
		data.global.sel.fb = 1;
		data.global.sel.fge = 1;
		data.setup_prim = &NoSetup;
		data.draw_scanline = &RecordSpan;
		data.draw_edge = nullptr;

		m_r.Draw(data);

		// Draw owns nothing it was handed.
		data.vertex = nullptr;
		data.index = nullptr;
		return g_log;
	}

	static GSVertexSW Vertex(float x, float y, float r, float g, float b, float fog)
	{
		GSVertexSW v = GSVertexSW::zero();
		v.p = GSVector4(x, y, 0.0f, 0.0f);
		v.p.F64[1] = 0.0;
		v.c = GSVector4(r, g, b, 128.0f) * GSVector4(128.0f);
		v.t = GSVector4(0.0f, 0.0f, 1.0f, fog);
		return v;
	}

	// A gouraud triangle with a colour and fog gradient on both axes, so its rows
	// leave non-zero tables behind.
	SpanLog Gouraud()
	{
		GSVertexSW v[3] = {
			Vertex(10.0f, 10.0f, 0.0f, 40.0f, 255.0f, 0.0f),
			Vertex(90.0f, 20.0f, 255.0f, 0.0f, 10.0f, 200.0f),
			Vertex(30.0f, 80.0f, 60.0f, 255.0f, 0.0f, 90.0f),
		};
		return Run(GS_TRIANGLE_CLASS, v, 3, true);
	}

	SpanLog Sprites()
	{
		GSVertexSW v[4] = {
			Vertex(8.0f, 8.0f, 0.0f, 0.0f, 0.0f, 0.0f),
			Vertex(20.0f, 16.0f, 200.0f, 100.0f, 50.0f, 0.0f),
			Vertex(40.0f, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f),
			Vertex(47.0f, 51.0f, 10.0f, 20.0f, 30.0f, 0.0f),
		};
		return Run(GS_SPRITE_CLASS, v, 4, false);
	}

	SpanLog Lines()
	{
		GSVertexSW v[2] = {
			Vertex(5.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f),
			Vertex(60.0f, 30.0f, 255.0f, 255.0f, 255.0f, 100.0f),
		};
		return Run(GS_LINE_CLASS, v, 2, true);
	}
};
} // namespace

TEST_F(ColourWalkTables, AGouraudTriangleLeavesNonZeroTables)
{
	// The precondition every other case leans on: without it, "zero after a
	// triangle" would prove nothing.
	const SpanLog tri = Gouraud();
	ASSERT_GT(tri.spans, 0);
	EXPECT_EQ(tri.live_spans, tri.spans);
	EXPECT_LT(tri.zero_spans, tri.spans);
}

TEST_F(ColourWalkTables, SpritesAfterATriangleSeeZeroTables)
{
	Gouraud();
	const SpanLog spr = Sprites();
	ASSERT_GT(spr.spans, 0);
	EXPECT_EQ(spr.live_spans, 0);
	EXPECT_EQ(spr.zero_spans, spr.spans);
}

TEST_F(ColourWalkTables, SpritesAfterSpritesSeeZeroTables)
{
	Gouraud();
	Sprites();
	const SpanLog spr = Sprites();
	ASSERT_GT(spr.spans, 0);
	EXPECT_EQ(spr.zero_spans, spr.spans);
}

TEST_F(ColourWalkTables, LinesAfterATriangleSeeZeroTables)
{
	Gouraud();
	const SpanLog line = Lines();
	ASSERT_GT(line.spans, 0);
	EXPECT_EQ(line.zero_spans, line.spans);
}

TEST_F(ColourWalkTables, ATriangleAfterSpritesRebuildsItsTables)
{
	Sprites();
	Sprites();
	const SpanLog tri = Gouraud();
	ASSERT_GT(tri.spans, 0);
	EXPECT_LT(tri.zero_spans, tri.spans);
}

TEST_F(ColourWalkTables, SpritesBetweenTwoTrianglesStillSeeZeroTables)
{
	Gouraud();
	Sprites();
	Gouraud();
	const SpanLog spr = Sprites();
	ASSERT_GT(spr.spans, 0);
	EXPECT_EQ(spr.zero_spans, spr.spans);
}

#endif // ARCH_ARM64

}

namespace SWParity_fog_blend {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// What the fog blend stores, and what it is handed.
//
// gs-fog (SCPH-30001, 2026-09-07, two byte-identical runs) drew a flat ladder of
// all 26 fog coefficients under a per-channel FOGCOL -- R = 0, G = 0xff, B = 0xff
// -- against vertex colours chosen so each channel isolates a different half of
// the expression. 104 fills, four channels, and exactly one rule fits every one:
//
//     stored = Cf + floor((Cv - Cf) * F / 256)   ==   (F*Cv + (256 - F)*Cf) >> 8
//
// The fog colour's weight is 256 - F, NOT the documented 255 - F, and the
// multiply floors. The documented rule mismatches 100 of 104 fills on green and
// 104 of 104 on blue. The two agree exactly wherever the fog colour is zero --
// the (256-F)*Cf term vanishes -- which is every fog environment the nine earlier
// GS captures wrote, and is why none of them could have seen it.
//
// What the ladder reads, at the integer F silicon hands the blend:
//
//     R  Cv=0xff Cf=0     ->  (F*255)>>8, which is F-1 for F >= 1 and 0 at F = 0
//     G  Cv=0    Cf=0xff  ->  255 - F exactly, at every F
//     B  Cv=0xff Cf=0xff  ->  255 at every F, because Cv = Cf and F cancels
//     A  never fogged     ->  the vertex alpha
//
// AND THE BLEND RECEIVES AN INTEGER. The fog lane walks in the colour unit, a
// 128th of a level, so its value carries a fraction between pixels; silicon
// truncates it to eight bits before the multiply. gs-fog scores the truncating
// form at 100.000% over all 115,040 fog readings and the fractional form at
// 34.6%, 73.6% and 48.4% by section, so it is not a tie the capture could not
// break. That half is written differentially below -- sweeping the fraction must
// change nothing -- so it pins the rule without restating the blend.
//
// Rides ARCH_ARM64 like its siblings gs_sw_scanline_texfunc_tests.cpp and
// gs_sw_scanline_dither_tests.cpp: all three compile the JIT scanline directly,
// and the generator is per-architecture. Both the generated scanline and the C++
// one are driven, because a shipping build takes the first and a locked-down
// platform takes the second.


#ifdef ARCH_ARM64


#ifndef MULTI_ISA_SHARED_COMPILATION




#ifndef _WIN32
#else
#endif

namespace
{
using DrawScanlinePtr = void (*)(int pixels, int left, int top, const GSVertexSW& scan, GSScanlineLocalData& local);

/// The rule, written once, in the console's own terms.
int FogBlend(int cv, int cf, int f)
{
	const int d = cv - cf;
	// A floor, not a truncation toward zero: the product is negative whenever the
	// fog colour is above the vertex colour, and gs-fog's green channel -- which
	// is exactly that case -- reads 255 - F with no rounding anywhere.
	const int q = (d * f) >> 8;

	return cf + q;
}

class SwFogBlendTest : public SWParityTestBase
{
protected:
	static constexpr size_t kCodeSlotSize = 16 * 1024;
	static constexpr size_t kCodeBufferSize = 4 * kCodeSlotSize;

	static void SetUpTestSuite()
	{
#ifdef _WIN32
		s_code = static_cast<u8*>(VirtualAlloc(nullptr, kCodeBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#elif defined(__APPLE__)
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#else
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#endif
		s_code_used = 0;
		s_mem = new GSLocalMemory();
	}

	static void TearDownTestSuite()
	{
		delete s_mem;
		s_mem = nullptr;

		if (s_code)
		{
#ifdef _WIN32
			VirtualFree(s_code, 0, MEM_RELEASE);
#else
			munmap(s_code, kCodeBufferSize);
#endif
			s_code = nullptr;
		}
	}

	static DrawScanlinePtr Compile(GSScanlineSelector sel)
	{
		if (!s_code || s_code_used + kCodeSlotSize > kCodeBufferSize)
			return nullptr;

		u8* slot = s_code + s_code_used;
		s_code_used += kCodeSlotSize;

		HostSys::BeginCodeWrite();
		GSDrawScanlineCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<DrawScanlinePtr>(const_cast<u8*>(cg.GetCode()));
	}

	/// A flat, untextured, unblended, FOGGED sprite into PSMCT32 -- the ladder's
	/// own draw. A sprite so that the fog coefficient comes from local.p.f and is
	/// constant across the row, which is what makes a ladder rung one value.
	static GSScanlineSelector MakeSelector()
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.fpsm = 0;
		sel.zpsm = 3;
		sel.atst = ATST_ALWAYS;
		sel.tfx = TFX_NONE;
		sel.ababcd = 0xff;
		sel.prim = GS_SPRITE_CLASS;
		sel.iip = 0;
		sel.fge = 1;
		sel.fwrite = 1;
		sel.notest = 1;
		sel.colclamp = 1;
		return sel;
	}

	static const GSPixelOffset4* GetOffsets()
	{
		GIFRegFRAME frame;
		frame.U64 = 0;
		frame.FBP = 0;
		frame.FBW = 1;
		frame.PSM = PSMCT32;

		GIFRegZBUF zbuf;
		zbuf.U64 = 0;
		zbuf.ZBP = 256;
		zbuf.PSM = PSMZ32;

		return s_mem->GetPixelOffset4(frame, zbuf);
	}

	static u32 PixelAddr(int x, int y)
	{
		return GSLocalMemory::m_psm[PSMCT32].info.pa(x, y, 0, 1);
	}

	/// One four-pixel row. `frac` is the sub-unit part the fog DDA would be
	/// carrying mid-gradient, in the walk's own 128ths of a level.
	static u32 RunRow(DrawScanlinePtr fn, GSScanlineSelector sel, const GSPixelOffset4* off,
		u32 cvr, u32 cvg, u32 cvb, u32 cva, u32 cfr, u32 cfg, u32 cfb, u32 f, u32 frac)
	{
		u32* vm32 = s_mem->vm32();
		for (int x = 0; x < 4; x++)
			vm32[PixelAddr(x, 0)] = 0x0Du;

		alignas(32) GSScanlineGlobalData global{};
		alignas(32) GSScanlineLocalData local = {};

		global.sel = sel;
		global.vm = s_mem->vm8();
		global.fzbr = off->row;
		global.fzbc = off->col;
		global.fm = GSVector4i(0);
		global.zm = GSVector4i(static_cast<int>(0xffffffffu));
		// FOGCOL, split the way GSRendererSW splits it: r and b in one register,
		// g and a in the other, one channel per 16-bit lane.
		global.frb = GSVector4i(static_cast<int>(cfr | (cfb << 16)));
		global.fga = GSVector4i(static_cast<int>(cfg));

		local.gd = &global;
		// TFX_NONE, so the flat colour is already the byte -- the setup's own
		// srl16<7> has happened before this point in a real draw.
		local.c.rb = GSVector4i(static_cast<int>(cvr | (cvb << 16)));
		local.c.ga = GSVector4i(static_cast<int>(cvg | (cva << 16)));
		// A sprite's fog constant, on the walk's own scale with a fraction on it.
		local.p.f = GSVector4i(static_cast<int>(((f << 7) | frac) * 0x00010001u));

		const GSVertexSW scan = GSVertexSW::zero();
		fn(4, 0, 0, scan, local);

		return vm32[PixelAddr(0, 0)];
	}

	static u8* s_code;
	static size_t s_code_used;
	static GSLocalMemory* s_mem;
};

u8* SwFogBlendTest::s_code = nullptr;
size_t SwFogBlendTest::s_code_used = 0;
GSLocalMemory* SwFogBlendTest::s_mem = nullptr;

// The ladder's own coefficients: both ends, the two values either side of each
// end, and the midpoint.
constexpr u32 kFogValues[] = {0, 1, 2, 128, 254, 255};

// gs-fog's three channel configurations, plus an interior pair that is neither
// end and neither equal -- the one arrangement where the floor and a rounding
// multiply disagree most often.
struct Channels
{
	u32 cv, cf;
	const char* what;
};
constexpr Channels kPairs[] = {
	{0xff, 0x00, "vertex 255 over fog 0"},
	{0x00, 0xff, "vertex 0 over fog 255"},
	{0xff, 0xff, "vertex 255 over fog 255"},
	{0x00, 0x00, "both zero"},
	{0xc3, 0x2b, "an interior pair"},
};
} // namespace

// ---------------------------------------------------------------------------
// The rule itself.
// ---------------------------------------------------------------------------

TEST_F(SwFogBlendTest, TheBlendWeightsTheFogColourBy256MinusF)
{
	const GSScanlineSelector sel = MakeSelector();
	DrawScanlinePtr jit = Compile(sel);
	ASSERT_NE(jit, nullptr) << "no code memory";

	const GSPixelOffset4* off = GetOffsets();

	for (const Channels& p : kPairs)
	{
		for (u32 f : kFogValues)
		{


			// Red and blue carry the pair, green carries gs-fog's own G column,
			// alpha carries a value fog must not touch.
			const u32 got = RunRow(jit, sel, off, p.cv, 0x00, p.cv, 0x5a, p.cf, 0xff, p.cf, f, 0);

			EXPECT_EQ(static_cast<int>(got & 0xff), FogBlend(p.cv, p.cf, f)) << "red";
			EXPECT_EQ(static_cast<int>((got >> 8) & 0xff), FogBlend(0x00, 0xff, f)) << "green";
			EXPECT_EQ(static_cast<int>((got >> 16) & 0xff), FogBlend(p.cv, p.cf, f)) << "blue";
			EXPECT_EQ(static_cast<int>((got >> 24) & 0xff), 0x5a) << "alpha is never fogged";
		}
	}
}

// The three columns gs-fog printed, stated as the closed forms it stated them in,
// so a failure says which half of the expression moved rather than only that a
// byte did.
TEST_F(SwFogBlendTest, TheLaddersOwnColumns)
{
	const GSScanlineSelector sel = MakeSelector();
	DrawScanlinePtr jit = Compile(sel);
	ASSERT_NE(jit, nullptr) << "no code memory";

	const GSPixelOffset4* off = GetOffsets();

	for (u32 f : kFogValues)
	{


		// R: vertex 255 over fog 0. G: vertex 0 over fog 255. B: vertex 255 over
		// fog 255 -- gs-fog's own arrangement.
		const u32 got = RunRow(jit, sel, off, 0xff, 0x00, 0xff, 0x11, 0x00, 0xff, 0xff, f, 0);

		EXPECT_EQ(static_cast<int>(got & 0xff), static_cast<int>((f * 255) >> 8)) << "red";
		EXPECT_EQ(static_cast<int>((got >> 8) & 0xff), static_cast<int>(255 - f)) << "green";
		EXPECT_EQ(static_cast<int>((got >> 16) & 0xff), 255) << "blue";
	}

	// The documented weight would put green at 255 - ((255*f) >> 8), which is one
	// level high at every F the two disagree on. If this ever stops separating
	// them the case above has stopped deciding anything.
	int separating = 0;
	for (u32 f : kFogValues)
	{
		if (static_cast<int>(255 - f) != 255 - static_cast<int>((255 * f) >> 8))
			separating++;
	}
	EXPECT_GT(separating, 3) << "the ladder no longer tells the two weights apart";
}

// ---------------------------------------------------------------------------
// And what the blend is handed: the byte, never the walk's fraction.
// ---------------------------------------------------------------------------

TEST_F(SwFogBlendTest, TheBlendIgnoresTheFogFraction)
{
	const GSScanlineSelector sel = MakeSelector();
	DrawScanlinePtr jit = Compile(sel);
	ASSERT_NE(jit, nullptr) << "no code memory";

	const GSPixelOffset4* off = GetOffsets();

	for (const Channels& p : kPairs)
	{
		for (u32 f : kFogValues)
		{
			const u32 base = RunRow(jit, sel, off, p.cv, 0x00, p.cv, 0x5a, p.cf, 0xff, p.cf, f, 0);

			for (u32 frac : {1u, 63u, 64u, 100u, 127u})
			{


				EXPECT_EQ(RunRow(jit, sel, off, p.cv, 0x00, p.cv, 0x5a, p.cf, 0xff, p.cf, f, frac), base);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// The C++ scanline is the whole renderer wherever there is no code memory to
// compile into, so it answers the same or it is a bug nobody would see in a
// shipping build.
// ---------------------------------------------------------------------------

TEST_F(SwFogBlendTest, TheCppScanlineAgrees)
{
	const GSScanlineSelector sel = MakeSelector();
	DrawScanlinePtr jit = Compile(sel);
	ASSERT_NE(jit, nullptr) << "no code memory";

	DrawScanlinePtr cpp = static_cast<DrawScanlinePtr>(&isa_native::GSDrawScanline::CDrawScanline);
	const GSPixelOffset4* off = GetOffsets();

	for (const Channels& p : kPairs)
	{
		for (u32 f : kFogValues)
		{
			for (u32 frac : {0u, 63u, 127u})
			{


				const u32 a = RunRow(jit, sel, off, p.cv, 0x00, p.cv, 0x5a, p.cf, 0xff, p.cf, f, frac);
				const u32 b = RunRow(cpp, sel, off, p.cv, 0x00, p.cv, 0x5a, p.cf, 0xff, p.cf, f, frac);

				EXPECT_EQ(a, b);
				EXPECT_EQ(static_cast<int>(b & 0xff), FogBlend(p.cv, p.cf, f)) << "red";
			}
		}
	}
}

#endif // MULTI_ISA_SHARED_COMPILATION
#endif // ARCH_ARM64

}

namespace SWParity_perspective_plane_scanline {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// A perspective triangle drawn with the console's S, T and Q planes.
//
// gs_sw_perspective_plane_tests.cpp pins the arithmetic that builds a plane (GSPerspectivePlane.h); this
// suite pins that the software renderer carries it to the pixel. A triangle goes through the real
// GSRasterizer, once with the C++ scanline and once with the generated one, against a texture whose every
// texel names itself, so a stored word says which texel a pixel sampled. The expected texel is the header's
// plane through the scanline's divide (a multiply by a reciprocal on a fifteen-bit grid, the coordinate
// truncated to a sixteenth; there is no lag on this route). What is on trial is the rasterizer's row seeds,
// the setup's steps, and the scanline's floor to g/4 and conversion to float.
//
// Each scene is chosen so that the exact plane names a different texel on dozens of pixels, so these cases
// fail without the plane rule; the last case pins that they do.
//
// Built only for ARCH_ARM64: the generators are per-architecture, and the plane is not enabled on x86.





#if defined(ARCH_ARM64) && !defined(MULTI_ISA_SHARED_COMPILATION)


#ifndef _WIN32
#else
#endif

namespace
{
using DrawScanlinePtr = void (*)(int pixels, int left, int top, const GSVertexSW& scan, GSScanlineLocalData& local);
using SetupPrimPtr = void (*)(const GSVertexSW* vertex, const u16* index, const GSVertexSW& dscan, GSScanlineLocalData& local);

// The naming texture is 1024 texels wide (`1 << (sel.tw + 3)` at tw 7) and 64 high.
// A texel's word carries its own coordinates, so the framebuffer says which texel a
// pixel sampled.
constexpr int kTexW = 1024;
constexpr int kTexH = 64;
constexpr int kTW = 10; // log2 kTexW
constexpr int kTH = 6;  // log2 kTexH

constexpr int kSize = 64; // the framebuffer is one 64-pixel page wide

constexpr u32 Texel(int x, int y)
{
	return 0x80000000u | (static_cast<u32>(y) << 16) | static_cast<u32>(x);
}

struct Scene
{
	const char* name;
	s32 x16[3], y16[3];
	float s[3], t[3], q[3];
};

// Six triangles. A is led by Q >= 2 (E = 1), B has negative coordinates under REPEAT (E = 0), C is led by S
// (E = 1 with every Q below 2), D has a twice-area that is a power of two, E and F are A with some or all of
// its vertices negated whole. A to C were chosen by search so that the plane rule and the exact plane name
// different texels on a few percent of their pixels.
const Scene kScenes[] = {
	{"A", {104, 962, 379}, {150, 328, 936}, {0.547561646f, 1.88167548f, 0.400869966f}, {2.19578218f, 1.75813711f, 1.84474099f}, {3.09918571f, 2.01724887f, 2.0887816f}},
	{"B", {100, 868, 176}, {77, 672, 925}, {-0.546903789f, 0.468648851f, 0.0164142307f}, {0.0105292341f, 0.810239851f, 0.0555147342f}, {0.659195065f, 1.73486447f, 0.913543701f}},
	{"C", {109, 977, 469}, {17, 221, 921}, {3.04391146f, 3.31216383f, 2.9509778f}, {0.666566133f, 0.681976318f, 0.623073995f}, {1.94477153f, 1.89738989f, 1.86753953f}},
	{"D", {128, 384, 128}, {128, 128, 384}, {0.5f, 2.3125f, 1.0f}, {0.25f, 0.75f, 3.0999999f}, {2.29999995f, 3.4000001f, 2.70000005f}},
	// A with every S, T and Q negated: a vertex with a negative Q is negated whole, so it draws as A does.
	{"E", {104, 962, 379}, {150, 328, 936}, {-0.547561646f, -1.88167548f, -0.400869966f}, {-2.19578218f, -1.75813711f, -1.84474099f}, {-3.09918571f, -2.01724887f, -2.0887816f}},
	// A with the first and last vertices negated: mixed signs, still A.
	{"F", {104, 962, 379}, {150, 328, 936}, {-0.547561646f, 1.88167548f, -0.400869966f}, {-2.19578218f, 1.75813711f, -1.84474099f}, {-3.09918571f, 2.01724887f, -2.0887816f}},
};

/// The floats the vertex conversion hands the scanline: S and T scaled by
/// 2^(16 + TW) and 2^(16 + TH), Q as it is (GSRendererSW ConvertVertexBuffer).
GSVertexSW Vertex(const Scene& sc, int i)
{
	GSVertexSW v = GSVertexSW::zero();

	v.p = GSVector4(static_cast<float>(sc.x16[i]) / 16.0f, static_cast<float>(sc.y16[i]) / 16.0f, 0.0f, 0.0f);
	v.p.F64[1] = 0.0;
	v.c = GSVector4(16384.0f, 16384.0f, 16384.0f, 16384.0f);
	v.t = GSVector4(std::ldexp(sc.s[i], 16 + kTW), std::ldexp(sc.t[i], 16 + kTH), sc.q[i], 0.0f);

	return v;
}

/// What the scanline's divide makes of one pixel's plane values: the multiply by a reciprocal on a
/// fifteen-bit grid rounded as floor(x + 0.7), then the coordinate's truncation toward zero to a sixteenth
/// (no lag on the plane route), and REPEAT addressing. Mirrors the ARM64 scanline
/// (GSDrawScanlineCodeGenerator SampleTexture) and its C++ twin.
void Reference(const GSPerspectivePlane& p, int x, int y, int& u, int& v)
{
	const float sh = static_cast<float>(GSPerspectivePlanePixel(GSPerspectivePlaneValue(p, 0, x, y)));
	const float th = static_cast<float>(GSPerspectivePlanePixel(GSPerspectivePlaneValue(p, 1, x, y)));
	const float qh = static_cast<float>(GSPerspectivePlanePixel(GSPerspectivePlaneValue(p, 2, x, y)));

	float r = 1.0f / qh;
	u32 rb;

	std::memcpy(&rb, &r, sizeof(rb));
	rb = (rb + GS_RECIP_ROUND_UP) & ~((1u << GS_RECIP_GRID_SHIFT) - 1);
	std::memcpy(&r, &rb, sizeof(r));

	s32 cu = static_cast<s32>(sh * r * std::ldexp(1.0f, 16 + kTW));
	s32 cv = static_cast<s32>(th * r * std::ldexp(1.0f, 16 + kTH));

	cu += cu < 0 ? 0xfff : 0;
	cv += cv < 0 ? 0xfff : 0;

	// The 12.4 field saturates; these scenes stay well inside it.
	u = (cu >> 16) & (kTexW - 1);
	v = (cv >> 16) & (kTexH - 1);
}

class SwPerspectivePlaneTest : public SWParityTestBase
{
protected:
	static constexpr size_t kCodeSlotSize = 16 * 1024;
	static constexpr size_t kCodeBufferSize = 64 * kCodeSlotSize;

	static void SetUpTestSuite()
	{
        std::fill(std::begin(s_setup),std::end(s_setup),nullptr);
        std::fill(std::begin(s_draw),std::end(s_draw),nullptr);
#ifdef _WIN32
		s_code = static_cast<u8*>(VirtualAlloc(nullptr, kCodeBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#elif defined(__APPLE__)
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#else
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#endif
		s_code_used = 0;
		s_mem = new GSLocalMemory();
	}

	static void TearDownTestSuite()
	{
		delete s_mem;
		s_mem = nullptr;

		if (s_code)
		{
#ifdef _WIN32
			VirtualFree(s_code, 0, MEM_RELEASE);
#else
			munmap(s_code, kCodeBufferSize);
#endif
			s_code = nullptr;
		}
	}

	static u8* Slot()
	{
		if (!s_code || s_code_used + kCodeSlotSize > kCodeBufferSize)
			return nullptr;

		u8* slot = s_code + s_code_used;
		s_code_used += kCodeSlotSize;
		return slot;
	}

	static DrawScanlinePtr CompileScanline(GSScanlineSelector sel)
	{
		u8* slot = Slot();
		if (!slot)
			return nullptr;

		HostSys::BeginCodeWrite();
		GSDrawScanlineCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<DrawScanlinePtr>(const_cast<u8*>(cg.GetCode()));
	}

	// The setup generator takes a reduced key, exactly as GetScanlineGlobalData
	// builds it, so this compiles the variant a real draw would get.
	static SetupPrimPtr CompileSetup(GSScanlineSelector full)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.iip = full.iip;
		sel.tfx = full.tfx;
		sel.tcc = full.tcc;
		sel.fst = full.fst;
		sel.uvwalk = full.uvwalk;
		sel.stqplane = full.stqplane;
		sel.fge = full.fge;
		sel.prim = full.prim;
		sel.fb = full.fb;
		sel.zb = full.zb;
		sel.zoverflow = full.zoverflow;
		sel.zequal = full.zequal;
		sel.notest = full.notest;

		u8* slot = Slot();
		if (!slot)
			return nullptr;

		HostSys::BeginCodeWrite();
		GSSetupPrimCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<SetupPrimPtr>(const_cast<u8*>(cg.GetCode()));
	}

	// DECAL with TCC on and NEAREST: the stored pixel is the sampled texel and
	// nothing else. Not `notest`: a coverage test keeps a ragged span from writing a
	// whole vector past its end.
	static GSScanlineSelector MakeSelector(bool plane)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.fpsm = 0;
		sel.zpsm = 3;
		sel.atst = ATST_ALWAYS;
		sel.ztst = ZTST_ALWAYS;
		sel.tfx = TFX_DECAL;
		sel.tcc = 1;
		sel.fst = 0;
		sel.stqplane = plane ? 1 : 0;
		sel.ltf = 0;
		sel.tlu = 0;
		sel.tw = kTW - 3;
		sel.wms = CLAMP_REPEAT;
		sel.wmt = CLAMP_REPEAT;
		sel.ababcd = 0xff;
		sel.prim = GS_TRIANGLE_CLASS;
		sel.iip = 0;
		sel.fwrite = 1;
		sel.notest = 0;
		sel.colclamp = 1;
		return sel;
	}

	static const GSPixelOffset4* GetOffsets()
	{
		GIFRegFRAME frame;
		frame.U64 = 0;
		frame.FBP = 0;
		frame.FBW = 1;
		frame.PSM = PSMCT32;

		GIFRegZBUF zbuf;
		zbuf.U64 = 0;
		zbuf.ZBP = 256;
		zbuf.PSM = PSMZ32;

		return s_mem->GetPixelOffset4(frame, zbuf);
	}

	static u32 PixelAddr(int x, int y)
	{
		return GSLocalMemory::m_psm[PSMCT32].info.pa(x, y, 0, 1);
	}

	/// Draws one scene through the real rasterizer, with the C++ scanline or the
	/// generated one, and returns the framebuffer (zero where nothing was drawn).
	static bool Draw(const Scene& sc, bool jit, bool plane, std::vector<u32>& out)
	{
		const GSScanlineSelector sel = MakeSelector(plane);

		SetupPrimPtr setup = &isa_native::GSDrawScanline::CSetupPrim;
		DrawScanlinePtr draw = static_cast<DrawScanlinePtr>(&isa_native::GSDrawScanline::CDrawScanline);

		if (jit)
		{
			const int slot = plane ? 1 : 0;

			if (!s_setup[slot])
			{
				s_setup[slot] = CompileSetup(sel);
				s_draw[slot] = CompileScanline(sel);
			}

			setup = s_setup[slot];
			draw = s_draw[slot];

			if (!setup || !draw)
				return false;
		}

		u32* vm32 = s_mem->vm32();
		for (int y = 0; y < kSize; y++)
		{
			for (int x = 0; x < kSize; x++)
				vm32[PixelAddr(x, y)] = 0;
		}

		alignas(32) static u32 tex[kTexW * kTexH];
		for (int y = 0; y < kTexH; y++)
		{
			for (int x = 0; x < kTexW; x++)
				tex[y * kTexW + x] = Texel(x, y);
		}

		isa_native::GSRasterizerData data;
		data.primclass = GS_TRIANGLE_CLASS;
		data.scissor = GSVector4i(0, 0, kSize, kSize);
		data.bbox = GSVector4i(0, 0, kSize, kSize);
		data.scanmsk_value = 0;
		data.setup_prim = setup;
		data.draw_scanline = draw;
		data.draw_edge = nullptr;

		data.global.sel = sel;
		data.global.vm = s_mem->vm8();
		data.global.fzbr = GetOffsets()->row;
		data.global.fzbc = GetOffsets()->col;
		data.global.fm = GSVector4i(0);
		data.global.zm = GSVector4i(static_cast<int>(0xffffffffu));
		data.global.tex[0] = tex;
		data.global.plane_shift[0] = 16 + kTW;
		data.global.plane_shift[1] = 16 + kTH;

		// REPEAT on both axes: the minimum carries the AND mask, the maximum the OR,
		// and the blend mask picks the repeat form (GSRendererSW).
		data.global.t.min = GSVector4i::zero();
		data.global.t.max = GSVector4i::zero();
		data.global.t.min.U16[0] = kTexW - 1;
		data.global.t.min.U16[4] = kTexH - 1;
		data.global.t.min = data.global.t.min.xxxxlh();
		data.global.t.max = data.global.t.max.xxxxlh();
		data.global.t.mask = GSVector4i(static_cast<int>(0xffffffffu));
		data.global.t.invmask = ~data.global.t.mask;

		GSVertexSW vertex[3] = {Vertex(sc, 0), Vertex(sc, 1), Vertex(sc, 2)};
		u16 index[3] = {0, 1, 2};

		data.vertex = vertex;
		data.vertex_count = 3;
		data.index = index;
		data.index_count = 3;

		isa_native::GSRasterizer r(nullptr, 0, 1);
		r.Draw(data);

		out.assign(static_cast<size_t>(kSize) * kSize, 0);
		for (int y = 0; y < kSize; y++)
		{
			for (int x = 0; x < kSize; x++)
				out[static_cast<size_t>(y) * kSize + x] = vm32[PixelAddr(x, y)];
		}

		return true;
	}

	/// Every pixel the draw stored, against the texel the header's plane names there.
	/// Returns how many pixels were stored.
	static void ExpectThePlane(const Scene& sc, const std::vector<u32>& got, int& drawn)
	{
		float s[3], t[3], q[3];
		s32 x16[3], y16[3];

		for (int i = 0; i < 3; i++)
		{
			s[i] = sc.s[i];
			t[i] = sc.t[i];
			q[i] = sc.q[i];
			x16[i] = sc.x16[i];
			y16[i] = sc.y16[i];
		}

		GSPerspectivePlane plane;

		GSPerspectivePlaneSetup(x16, y16, s, t, q, plane);
		ASSERT_TRUE(plane.valid);

		drawn = 0;

		for (int y = 0; y < kSize; y++)
		{
			for (int x = 0; x < kSize; x++)
			{
				const u32 word = got[static_cast<size_t>(y) * kSize + x];

				if (word == 0)
					continue;

				drawn++;

				int u, v;

				Reference(plane, x, y, u, v);

				EXPECT_EQ(word, Texel(u, v)) << sc.name << " pixel (" << x << ", " << y << "): sampled texel ("
											 << (word & 0xffff) << ", " << ((word >> 16) & 0x7fff) << "), the plane says ("
											 << u << ", " << v << ")";
			}
		}
	}

	static SetupPrimPtr s_setup[2];
	static DrawScanlinePtr s_draw[2];
	static u8* s_code;
	static size_t s_code_used;
	static GSLocalMemory* s_mem;
};

SetupPrimPtr SwPerspectivePlaneTest::s_setup[2] = {};
DrawScanlinePtr SwPerspectivePlaneTest::s_draw[2] = {};
u8* SwPerspectivePlaneTest::s_code = nullptr;
size_t SwPerspectivePlaneTest::s_code_used = 0;
GSLocalMemory* SwPerspectivePlaneTest::s_mem = nullptr;

// Every pixel the draw stores must name the texel the header's plane names, through the same divide. The
// scenes differ from the exact plane on dozens of pixels each, so this fails without the plane rule.
TEST_F(SwPerspectivePlaneTest, TheCppScanlineDrawsThePlane)
{
	for (const Scene& sc : kScenes)
	{
		std::vector<u32> got;
		ASSERT_TRUE(Draw(sc, false, true, got));

		int drawn = 0;
		ExpectThePlane(sc, got, drawn);

		EXPECT_GT(drawn, 100) << sc.name << ": the triangle drew almost nothing";
	}
}

TEST_F(SwPerspectivePlaneTest, TheGeneratedScanlineDrawsThePlane)
{
	for (const Scene& sc : kScenes)
	{
		std::vector<u32> got;
		ASSERT_TRUE(Draw(sc, true, true, got));

		int drawn = 0;
		ExpectThePlane(sc, got, drawn);

		EXPECT_GT(drawn, 100) << sc.name << ": the triangle drew almost nothing";
	}
}

// E is A with every vertex negated and F is A with two of them negated. A vertex with
// a negative Q is negated whole by the setup, so both must store A's words exactly.
TEST_F(SwPerspectivePlaneTest, ANegatedVertexDrawsAsTheOriginalDoes)
{
	for (int jit = 0; jit <= 1; jit++)
	{
		std::vector<u32> a, e, f;
		ASSERT_TRUE(Draw(kScenes[0], jit != 0, true, a));
		ASSERT_TRUE(Draw(kScenes[4], jit != 0, true, e));
		ASSERT_TRUE(Draw(kScenes[5], jit != 0, true, f));

		EXPECT_EQ(a, e) << (jit ? "generated" : "C++") << " scanline, every vertex negated";
		EXPECT_EQ(a, f) << (jit ? "generated" : "C++") << " scanline, two vertices negated";
	}
}

// The two transcriptions of one design must store the same word at every pixel; a
// drift between them is invisible where only the generated one ships.
TEST_F(SwPerspectivePlaneTest, TheTwoScanlinesAgreeWordForWord)
{
	for (const Scene& sc : kScenes)
	{
		std::vector<u32> cpp, jit;
		ASSERT_TRUE(Draw(sc, false, true, cpp));
		ASSERT_TRUE(Draw(sc, true, true, jit));

		EXPECT_EQ(cpp, jit) << sc.name;
	}
}

// The plane rule is off here (the selector bit clear). Against the header's plane it must disagree on
// dozens of pixels on each of the first three scenes; otherwise the cases above would pass on the exact
// plane too and would not test the rule.
TEST_F(SwPerspectivePlaneTest, WithoutTheRuleTheScenesDisagreeWithThePlane)
{
	for (const Scene& sc : kScenes)
	{
		if (sc.name[0] == 'D')
			continue;

		std::vector<u32> got;
		ASSERT_TRUE(Draw(sc, true, false, got));

		float s[3], t[3], q[3];
		s32 x16[3], y16[3];

		for (int i = 0; i < 3; i++)
		{
			s[i] = sc.s[i];
			t[i] = sc.t[i];
			q[i] = sc.q[i];
			x16[i] = sc.x16[i];
			y16[i] = sc.y16[i];
		}

		GSPerspectivePlane plane;

		GSPerspectivePlaneSetup(x16, y16, s, t, q, plane);

		int differing = 0;

		for (int y = 0; y < kSize; y++)
		{
			for (int x = 0; x < kSize; x++)
			{
				const u32 word = got[static_cast<size_t>(y) * kSize + x];

				if (word == 0)
					continue;

				int u, v;

				Reference(plane, x, y, u, v);
				differing += (word != Texel(u, v)) ? 1 : 0;
			}
		}

		EXPECT_GT(differing, 30) << sc.name;
	}
}
} // namespace

namespace
{
// A span in the plane rule's own units: counts of g/4 for S, T and Q at the first
// pixel and their whole-count steps per pixel, under a block exponent kE.
struct PlaneSpan
{
	s32 s0, t0, q0;
	s32 ds, dt, dq;
};

constexpr int kE = 1;    // the block exponent; g/4 = 2^(kE - 16)
constexpr int kSTW = 4;  // the sampler tests' texture is sixteen texels square
constexpr int kSTH = 4;

struct Row
{
	u32 px[4];

	bool operator==(const Row& o) const
	{
		return px[0] == o.px[0] && px[1] == o.px[1] && px[2] == o.px[2] && px[3] == o.px[3];
	}
};

/// Everything a sampler mode needs that is not in the selector.
struct Mode
{
	const char* name;
	bool ltf = false;
	int ltfx = 0;      ///< 0 off, 1 crossover with the linear side above the threshold, 2 below it
	int mmin = 0;      ///< 0 off, 1 round, 2 trilinear
	bool lcm = true;   ///< constant level of detail
	float ltfx_q = 0.0f;
};

const u32* AddressTexture()
{
	static u32 tex[256];

	for (int i = 0; i < 256; i++)
	{
		const u32 b = static_cast<u32>(i);
		tex[i] = b | (b << 8) | (b << 16) | (b << 24);
	}

	return tex;
}

const u32* InvertedTexture()
{
	static u32 tex[256];

	for (int i = 0; i < 256; i++)
	{
		const u32 b = static_cast<u32>(255 - i);
		tex[i] = b | (b << 8) | (b << 16) | (b << 24);
	}

	return tex;
}
} // namespace

namespace
{
// The floor to g/4 is the plane rule's only per-pixel arithmetic. A span whose seed and step are whole
// counts of g/4 has nothing for it to floor, and every value the float pipeline would carry is then
// exactly representable, so the rule and the float pipeline must store the same word at every pixel, in
// every sampler mode: nearest and linear filter, the per-pixel filter crossover, constant and Q-driven
// level of detail. This pins the conversion the scanline adds (the shift, the scale that takes the
// quotient to 16.16 texels, and the Q that the level and the crossover read). Both the C++ scanline and
// the generated one are run, and must agree.
class SwPerspectivePlaneSamplerTest : public SwPerspectivePlaneTest
{
protected:
	static GSScanlineSelector ModeSelector(const Mode& m, bool plane)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.fpsm = 0;
		sel.zpsm = 3;
		sel.atst = ATST_ALWAYS;
		sel.tfx = TFX_DECAL;
		sel.tcc = 1;
		sel.fst = 0;
		sel.stqplane = plane ? 1 : 0;
		sel.ltf = m.ltf ? 1 : 0;
		sel.tlu = 0;
		sel.tw = 1; // sixteen texels wide
		sel.ababcd = 0xff;
		sel.prim = GS_TRIANGLE_CLASS;
		sel.iip = 0;
		sel.fwrite = 1;
		sel.notest = 1;
		sel.colclamp = 1;
		sel.mmin = m.mmin;
		sel.lcm = m.lcm ? 1 : 0;

		if (m.ltfx)
		{
			sel.ltfx = 1;
			sel.ltfx_ge = m.ltfx == 2 ? 1 : 0;
		}

		return sel;
	}

	static void Run(const Mode& m, const PlaneSpan& sp, bool plane, bool jit, Row& out)
	{
		const GSScanlineSelector sel = ModeSelector(m, plane);

		SetupPrimPtr setup = &isa_native::GSDrawScanline::CSetupPrim;
		DrawScanlinePtr draw = static_cast<DrawScanlinePtr>(&isa_native::GSDrawScanline::CDrawScanline);

		if (jit)
		{
			setup = CompileSetup(sel);
			draw = CompileScanline(sel);
			ASSERT_TRUE(setup && draw) << "the code buffer ran out";
		}

		u32* vm32 = s_mem->vm32();
		for (int x = 0; x < 4; x++)
			vm32[PixelAddr(x, 0)] = 0x0Du;

		alignas(32) GSScanlineGlobalData global{};
		alignas(32) GSScanlineLocalData local = {};

		global.sel = sel;
		global.vm = s_mem->vm8();
		global.fzbr = GetOffsets()->row;
		global.fzbc = GetOffsets()->col;
		global.fm = GSVector4i(0);
		global.zm = GSVector4i(static_cast<int>(0xffffffffu));

		for (int i = 0; i < 8; i++)
			global.tex[i] = (i & 1) ? InvertedTexture() : AddressTexture();

		// REPEAT on both axes over sixteen texels.
		global.t.min = GSVector4i::zero();
		global.t.max = GSVector4i::zero();
		global.t.minmax = GSVector4i::zero();
		global.t.mask = GSVector4i(static_cast<int>(0xffffffffu));
		for (int i = 0; i < 8; i++)
			global.t.min.U16[i] = 15;
		global.t.minmax.U16[0] = 15;
		global.t.minmax.U16[1] = 15;

		global.ltfx_q = GSVector4(m.ltfx_q);
		global.plane_shift[0] = 16 + kSTW;
		global.plane_shift[1] = 16 + kSTH;

		// A constant level of detail of one and a half, or Q-driven with TEX1.L = 0.
		global.lod.i = GSVector4i(1);
		global.lod.f = GSVector4i(0x8000).xxxxlh();
		global.lodtab = GSLevelOfDetailTable[0];
		global.lodk = 0;
		global.lodshift = 4;
		global.lodmxl = 4 << 16;

		local.gd = &global;

		if (sel.mmin && sel.lcm)
		{
			GSVector4i v = global.t.minmax.srl16(global.lod.i.extract32<0>());
			v = v.upl16(v);
			local.temp.uv_minmax[0] = v.upl32(v);
			local.temp.uv_minmax[1] = v.uph32(v);
		}

		GSVertexSW vertex[3];
		u16 index[3] = {0, 1, 2};
		for (int i = 0; i < 3; i++)
			vertex[i] = GSVertexSW::zero();

		GSVertexSW dscan = GSVertexSW::zero();
		GSVertexSW scan = GSVertexSW::zero();

		if (plane)
		{
			// Integers in units of g/2^14: a count of g/4 is 2^12 of them.
			dscan.t = GSVector4::cast(GSVector4i(sp.ds << GS_PLANE_PIXEL_SHIFT, sp.dt << GS_PLANE_PIXEL_SHIFT,
				sp.dq << GS_PLANE_PIXEL_SHIFT, 0));
			scan.t = GSVector4::cast(GSVector4i(sp.s0 << GS_PLANE_PIXEL_SHIFT, sp.t0 << GS_PLANE_PIXEL_SHIFT,
				sp.q0 << GS_PLANE_PIXEL_SHIFT, 0));

			local.pwalk.stscale = GSVector4(std::ldexp(1.0f, 16 + kSTW), std::ldexp(1.0f, 16 + kSTH), 1.0f, 1.0f);
			local.pwalk.qscale = GSVector4(std::ldexp(1.0f, kE - 16));
		}
		else
		{
			// The same values as floats: S and T scaled to 16.16 texels, Q as it is.
			const float su = std::ldexp(1.0f, kE - 16 + 16 + kSTW);
			const float sv = std::ldexp(1.0f, kE - 16 + 16 + kSTH);
			const float sq = std::ldexp(1.0f, kE - 16);

			dscan.t = GSVector4(sp.ds * su, sp.dt * sv, sp.dq * sq, 0.0f);
			scan.t = GSVector4(sp.s0 * su, sp.t0 * sv, sp.q0 * sq, 0.0f);
		}

		setup(vertex, index, dscan, local);
		draw(4, 0, 0, scan, local);

		for (int x = 0; x < 4; x++)
			out.px[x] = vm32[PixelAddr(x, 0)];
	}

	static void ExpectModeAgrees(const Mode& m, const PlaneSpan& sp)
	{
		Row cpp_float{}, cpp_plane{}, jit_float{}, jit_plane{};

		Run(m, sp, false, false, cpp_float);
		Run(m, sp, true, false, cpp_plane);
		Run(m, sp, false, true, jit_float);
		Run(m, sp, true, true, jit_plane);

		EXPECT_EQ(cpp_float, jit_float) << m.name << ": the float pipeline's two scanlines disagree";

		for (int i = 0; i < 4; i++)
		{
			EXPECT_EQ(cpp_plane.px[i], cpp_float.px[i]) << m.name << ": C++ scanline, pixel " << i;
			EXPECT_EQ(jit_plane.px[i], cpp_float.px[i]) << m.name << ": generated scanline, pixel " << i;
		}

		// Not four words of nothing: the span has to make the sampler do something.
		EXPECT_TRUE(cpp_float.px[0] != cpp_float.px[3] || cpp_float.px[1] != cpp_float.px[2]) << m.name;
	}
};

TEST_F(SwPerspectivePlaneSamplerTest, NearestMatchesTheFloatPipeline)
{
	ExpectModeAgrees(Mode{"nearest"}, PlaneSpan{14110, 9000, 42598, 1863, 1100, -300});
}

TEST_F(SwPerspectivePlaneSamplerTest, LinearMatchesTheFloatPipeline)
{
	Mode m{"linear"};
	m.ltf = true;

	ExpectModeAgrees(m, PlaneSpan{14110, 9000, 42598, 1863, 1100, -300});
}

// Q crosses the threshold between pixels 1 and 2: the linear filter takes one side.
// The threshold is in true Q, so the scanline's Q must come out of the count in the
// right unit for the compare to land where the float pipeline's does.
TEST_F(SwPerspectivePlaneSamplerTest, TheFilterCrossoverReadsQInItsOwnUnit)
{
	for (int side = 1; side <= 2; side++)
	{
		Mode m{"crossover"};
		m.ltf = true;
		m.ltfx = side;
		// Counts of 12000, 12400, 12800, 13200 are Q = 0.366, 0.378, 0.391, 0.403.
		m.ltfx_q = 12600.0f * std::ldexp(1.0f, kE - 16);

		ExpectModeAgrees(m, PlaneSpan{5000, 3000, 12000, 700, 420, 400});
	}
}

TEST_F(SwPerspectivePlaneSamplerTest, ConstantLevelOfDetailMatchesTheFloatPipeline)
{
	for (int mmin = 1; mmin <= 2; mmin++)
	{
		Mode m{"constant lod"};
		m.mmin = mmin;
		m.lcm = true;
		m.ltf = mmin == 2;

		ExpectModeAgrees(m, PlaneSpan{14110, 9000, 42598, 1863, 1100, -300});
	}
}

// The level comes from Q's own exponent and mantissa (GSLevelOfDetail.h), so the
// scanline has to hand it Q as a float and not as a count: a count of g/4 has a
// different exponent by 2^(E - 16). Q near 0.4 puts the level at about 1.9.
TEST_F(SwPerspectivePlaneSamplerTest, QDrivenLevelOfDetailReadsQAsAFloat)
{
	for (int mmin = 1; mmin <= 2; mmin++)
	{
		Mode m{"q-driven lod"};
		m.mmin = mmin;
		m.lcm = false;
		m.ltf = mmin == 2;

		ExpectModeAgrees(m, PlaneSpan{5200, 3300, 13107, 640, 380, 150});
	}
}

// The lag the accumulator routes trail by (the UV register, STQ at Q = 1) is one unit of the 16.16
// coordinate off a forward walk, which moves a sample that lands exactly on a texel boundary one texel
// down. On the plane route the coordinate lands on boundaries all the time (a value floored to g/4 over a
// Q of exactly two), and the console does not trail there. Q is 2.0 here (count 65,536 at E = 1), so the
// divide is exact and S/Q steps one texel a pixel from texel 3: a lag would read 2, 3, 4, 5.
TEST_F(SwPerspectivePlaneSamplerTest, ThePlaneRouteDoesNotLag)
{
	const PlaneSpan sp{3 * 4096, 5 * 4096, 65536, 4096, 0, 0};

	for (int jit = 0; jit <= 1; jit++)
	{
		Row got{};

		Run(Mode{"lagless"}, sp, true, jit != 0, got);

		for (int i = 0; i < 4; i++)
		{
			const u32 want = AddressTexture()[5 * 16 + 3 + i];

			EXPECT_EQ(got.px[i], want) << (jit ? "generated" : "C++") << " scanline, pixel " << i;
		}
	}
}
} // namespace

#endif // ARCH_ARM64 && !MULTI_ISA_SHARED_COMPILATION

}

namespace SWParity_scanline_lod {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// The three texture rules the other scanline suites do not reach: the truncated
// perspective reciprocal, the per-pixel choice between MMAG and MMIN, and the
// four-bit trilinear weight.
//
// Each of them is implemented twice -- once in the C++ reference scanline and once
// in the ARM64 generator -- and the two are what ShouldUseCDrawScanline picks
// between at runtime, so every test here runs the same span through both and
// requires the same pixels out. That catches a rule transcribed into one path and
// not the other, which is the failure this suite exists for.
//
// Where a rule has an observable that does not need a model, the test also states
// it directly, so reverting the rule in BOTH paths still fails rather than quietly
// agreeing on the wrong answer.
//
// It reads what the scanline sampled by drawing DECAL out of a texture whose every
// texel names its own address, the same trick the tclag suite uses.
//
// Rides ARCH_ARM64 like its siblings: the generators are per-architecture.


#ifdef ARCH_ARM64




#ifndef _WIN32
#else
#endif

namespace
{
using DrawScanlinePtr = void (*)(int pixels, int left, int top, const GSVertexSW& scan, GSScanlineLocalData& local);
using SetupPrimPtr = void (*)(const GSVertexSW* vertex, const u16* index, const GSVertexSW& dscan, GSScanlineLocalData& local);

// One texel, in the 16.16 the coordinate is carried in.
constexpr int kTexel = 0x10000;

// What one span of four pixels stored, per pixel.
struct Row
{
	u32 px[4];

	bool operator==(const Row& o) const
	{
		return px[0] == o.px[0] && px[1] == o.px[1] && px[2] == o.px[2] && px[3] == o.px[3];
	}
};

// Everything a span needs that is not in the selector. The two texture levels are
// filled by the caller; level 1 is only read when mmin is on.
struct Span
{
	float s0, t0, q0;
	float ds, dt, dq;
	float ltfx_q = 0.0f;
	u32 lod_i = 0;
	GSVector4i lod_f = GSVector4i::zero();
};

// The reciprocal the scanline multiplies by, in scalar, so a test can say what it
// expects rather than only that the two paths agree. Same rule as
// GSPerspectiveRecip: a float32 reciprocal on a grid of fifteen bits below its
// leading bit, rounded as floor(x + 0.7) -- 179 of the 256 units of the mantissa's
// last bit under the grid, added and then the low eight bits cleared.
float GridRecip(float q)
{
	float r = 1.0f / q;
	u32 bits;

	std::memcpy(&bits, &r, sizeof(bits));
	bits = (bits + GS_RECIP_ROUND_UP) & ~((1u << GS_RECIP_GRID_SHIFT) - 1);
	std::memcpy(&r, &bits, sizeof(bits));

	return r;
}

class SwScanlineLodTest : public SWParityTestBase
{
protected:
	static constexpr size_t kCodeSlotSize = 16 * 1024;
	static constexpr size_t kCodeBufferSize = 64 * kCodeSlotSize;

	static void SetUpTestSuite()
	{
#ifdef _WIN32
		s_code = static_cast<u8*>(VirtualAlloc(nullptr, kCodeBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#elif defined(__APPLE__)
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#else
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#endif
		s_code_used = 0;
		s_mem = new GSLocalMemory();
	}

	static void TearDownTestSuite()
	{
		delete s_mem;
		s_mem = nullptr;

		if (s_code)
		{
#ifdef _WIN32
			VirtualFree(s_code, 0, MEM_RELEASE);
#else
			munmap(s_code, kCodeBufferSize);
#endif
			s_code = nullptr;
		}
	}

	static u8* Slot()
	{
		if (!s_code || s_code_used + kCodeSlotSize > kCodeBufferSize)
			return nullptr;

		u8* slot = s_code + s_code_used;
		s_code_used += kCodeSlotSize;
		return slot;
	}

	static DrawScanlinePtr CompileScanline(GSScanlineSelector sel)
	{
		u8* slot = Slot();
		if (!slot)
			return nullptr;

		HostSys::BeginCodeWrite();
		GSDrawScanlineCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<DrawScanlinePtr>(const_cast<u8*>(cg.GetCode()));
	}

	// The reduced key GetScanlineGlobalData builds, so this compiles the same setup
	// variant a real draw would.
	static SetupPrimPtr CompileSetup(GSScanlineSelector full)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.iip = full.iip;
		sel.tfx = full.tfx;
		sel.tcc = full.tcc;
		sel.fst = full.fst;
		sel.fge = full.fge;
		sel.prim = full.prim;
		sel.fb = full.fb;
		sel.zb = full.zb;
		sel.zoverflow = full.zoverflow;
		sel.zequal = full.zequal;
		sel.notest = full.notest;

		u8* slot = Slot();
		if (!slot)
			return nullptr;

		HostSys::BeginCodeWrite();
		GSSetupPrimCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<SetupPrimPtr>(const_cast<u8*>(cg.GetCode()));
	}

	// DECAL out of a 16x16 texture into PSMCT32: the stored pixel IS the sampled
	// texel, with nothing between the sampler and the readback.
	static GSScanlineSelector BaseSelector()
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.fpsm = 0;
		sel.zpsm = 3;
		sel.atst = ATST_ALWAYS;
		sel.tfx = TFX_DECAL;
		sel.tcc = 1;
		sel.tlu = 0;
		sel.tw = 1; // 1 << (tw + 3) == 16 texels wide
		sel.ababcd = 0xff;
		sel.prim = GS_TRIANGLE_CLASS;
		sel.iip = 0;
		sel.fwrite = 1;
		sel.notest = 1;
		sel.colclamp = 1;
		return sel;
	}

	static const GSPixelOffset4* GetOffsets()
	{
		GIFRegFRAME frame;
		frame.U64 = 0;
		frame.FBP = 0;
		frame.FBW = 1;
		frame.PSM = PSMCT32;

		GIFRegZBUF zbuf;
		zbuf.U64 = 0;
		zbuf.ZBP = 256;
		zbuf.PSM = PSMZ32;

		return s_mem->GetPixelOffset4(frame, zbuf);
	}

	static u32 PixelAddr(int x, int y)
	{
		return GSLocalMemory::m_psm[PSMCT32].info.pa(x, y, 0, 1);
	}

	static void Run(GSScanlineSelector sel, SetupPrimPtr setup, DrawScanlinePtr draw,
		const Span& span, const u32* tex0, const u32* tex1, Row& out)
	{
		u32* vm32 = s_mem->vm32();
		for (int x = 0; x < 4; x++)
			vm32[PixelAddr(x, 0)] = 0x0Du;

		alignas(32) GSScanlineGlobalData global{};
		alignas(32) GSScanlineLocalData local = {};

		global.sel = sel;
		global.vm = s_mem->vm8();
		global.fzbr = GetOffsets()->row;
		global.fzbc = GetOffsets()->col;
		global.fm = GSVector4i(0);
		global.zm = GSVector4i(static_cast<int>(0xffffffffu));
		global.tex[0] = tex0;
		global.tex[1] = tex1;

		// REPEAT on both axes over sixteen texels, as GSRendererSW derives it.
		global.t.min = GSVector4i::zero();
		global.t.max = GSVector4i::zero();
		global.t.minmax = GSVector4i::zero();
		global.t.mask = GSVector4i(static_cast<int>(0xffffffffu));
		for (int i = 0; i < 8; i++)
			global.t.min.U16[i] = 15;
		global.t.minmax.U16[0] = 15; // min u
		global.t.minmax.U16[1] = 15; // min v

		global.ltfx_q = GSVector4(span.ltfx_q);
		global.lod.i = GSVector4i(static_cast<int>(span.lod_i));
		global.lod.f = span.lod_f;

		local.gd = &global;

		// GSDrawScanline::BeginDraw does this for a constant-LOD draw; the harness
		// calls the two halves of the pipeline directly, so it does it here.
		if (sel.mmin && sel.lcm)
		{
			GSVector4i v = global.t.minmax.srl16(global.lod.i.extract32<0>());
			v = v.upl16(v);
			local.temp.uv_minmax[0] = v.upl32(v);
			local.temp.uv_minmax[1] = v.uph32(v);
		}

		// DECAL with TCC on takes no vertex colour, and there is no depth or fog, so
		// the setup runs its texture half alone.
		GSVertexSW vertex[3];
		u16 index[3] = {0, 1, 2};
		for (int i = 0; i < 3; i++)
			vertex[i] = GSVertexSW::zero();

		GSVertexSW dscan = GSVertexSW::zero();
		dscan.t = GSVector4(span.ds, span.dt, span.dq, 0.0f);

		setup(vertex, index, dscan, local);

		GSVertexSW scan = GSVertexSW::zero();
		scan.t = GSVector4(span.s0, span.t0, span.q0, 0.0f);

		draw(4, 0, 0, scan, local);

		for (int x = 0; x < 4; x++)
			out.px[x] = vm32[PixelAddr(x, 0)];
	}

	// One span through both implementations. They must agree; the caller then makes
	// its claim about the value they agreed on.
	static Row Both(GSScanlineSelector sel, const Span& span, const u32* tex0, const u32* tex1, const char* what)
	{
		Row cpp{}, jit{};

		Run(sel, &isa_native::GSDrawScanline::CSetupPrim,
			static_cast<DrawScanlinePtr>(&isa_native::GSDrawScanline::CDrawScanline),
			span, tex0, tex1, cpp);

		SetupPrimPtr setup = CompileSetup(sel);
		DrawScanlinePtr draw = CompileScanline(sel);
		EXPECT_TRUE(setup && draw) << what << ": the code buffer ran out";
		if (!setup || !draw)
			return cpp;

		Run(sel, setup, draw, span, tex0, tex1, jit);

		for (int i = 0; i < 4; i++)
			EXPECT_EQ(cpp.px[i], jit.px[i]) << what << ": the C++ scanline and the generated one "
											   "disagree at pixel " << i;
		return cpp;
	}

	// A texture whose every texel names its own address, in all four channels.
	static const u32* AddressTexture()
	{
		static u32 tex[256];
		for (int i = 0; i < 256; i++)
		{
			const u32 b = static_cast<u32>(i);
			tex[i] = b | (b << 8) | (b << 16) | (b << 24);
		}
		return tex;
	}

	// The two mip levels the trilinear tests blend: level 0 black, level 1 a mid
	// value in every channel, so a blend is visible in every channel.
	static const u32* Level0Texture()
	{
		static u32 tex[256] = {};
		return tex;
	}

	static const u32* Level1Texture()
	{
		static u32 tex[256];
		for (int i = 0; i < 256; i++)
			tex[i] = 0x40404040u;
		return tex;
	}

	static u8* s_code;
	static size_t s_code_used;
	static GSLocalMemory* s_mem;
};

u8* SwScanlineLodTest::s_code = nullptr;
size_t SwScanlineLodTest::s_code_used = 0;
GSLocalMemory* SwScanlineLodTest::s_mem = nullptr;

// The GS multiplies by a reciprocal on a coarse grid rather than dividing, so a
// perspective coordinate is not the true quotient: fifteen bits below the
// reciprocal's leading bit, rounded as floor(x + 0.7) (GSPerspectivePlane.h). The
// rounding goes up by as much as a grid step, so the sampled texel changes wherever
// the exact quotient sits just short of a boundary.
//
// q = 3 has a reciprocal with bits below the grid, and s is placed a sixth of a
// sixteenth of a texel under the boundary of texel 2: the exact quotient and a
// reciprocal truncated to fourteen bits both read texel 1, the grid's rounded-up
// one reads texel 2.
TEST_F(SwScanlineLodTest, PerspectiveSamplesTheGridReciprocal)
{
	GSScanlineSelector sel = BaseSelector();
	sel.fst = 0; // perspective
	sel.ltf = 0; // nearest, so the stored pixel is one texel and not a blend

	// s and q are constant across the span, so nothing here depends on the DDA: the
	// only thing between s/q and the texel is the reciprocal.
	const float q = 3.0f;
	const float s = 3.0f * 2.0f * static_cast<float>(kTexel) - 0.09375f;

	Span span{};
	span.s0 = s;
	span.t0 = 0.0f;
	span.q0 = q;
	span.ds = 0.0f;
	span.dt = 0.0f;
	span.dq = 0.0f;

	u32 old_bits;
	float old_r = 1.0f / q;
	std::memcpy(&old_bits, &old_r, sizeof(old_bits));
	old_bits &= 0xfffffe00u;
	std::memcpy(&old_r, &old_bits, sizeof(old_r));

	const int exact = static_cast<int>(s / q) >> 16;
	const int old_truncated = static_cast<int>(s * old_r) >> 16;
	const int grid = static_cast<int>(s * GridRecip(q)) >> 16;
	ASSERT_EQ(exact, 1);
	ASSERT_EQ(old_truncated, 1);
	ASSERT_EQ(grid, 2) << "the shape of this test is wrong: the grid must differ from both here";

	const Row got = Both(sel, span, AddressTexture(), nullptr, "perspective recip");

	for (int i = 0; i < 4; i++)
	{
		EXPECT_EQ(got.px[i] & 0xff, static_cast<u32>(grid))
			<< "pixel " << i << ": the coordinate did not come from the fifteen-bit rounded reciprocal";
	}
}

// ---- per-pixel MMAG / MMIN ----------------------------------------------------

namespace
{
// A perspective span whose q crosses ltfx_q between pixel 1 and pixel 2, with s and
// t proportional to q so the sampled coordinate sits a quarter of a texel into texel
// (5, 5) on every pixel. Nearest reads texel (5,5) = 85; linear straddles the pair
// below it and reads a blend, which is a different number.
Span CrossoverSpan()
{
	constexpr float kCoord = 5.0f * static_cast<float>(kTexel) + 0x4000;

	Span span{};
	span.q0 = 0.5f;
	span.dq = 0.25f; // q = 0.5, 0.75, 1.0, 1.25
	span.s0 = kCoord * span.q0;
	span.t0 = kCoord * span.q0;
	span.ds = kCoord * span.dq;
	span.dt = kCoord * span.dq;
	span.ltfx_q = 0.875f; // pixels 0 and 1 minify, pixels 2 and 3 do not
	return span;
}
} // namespace

TEST_F(SwScanlineLodTest, LtfxFiltersOnlyThePixelsBelowTheCrossing)
{
	const Span span = CrossoverSpan();
	const u32* tex = AddressTexture();

	GSScanlineSelector all_linear = BaseSelector();
	all_linear.fst = 0;
	all_linear.ltf = 1;

	GSScanlineSelector per_pixel = all_linear;
	per_pixel.ltfx = 1;
	per_pixel.ltfx_ge = 0;

	const Row linear = Both(all_linear, span, tex, nullptr, "ltf everywhere");
	const Row mixed = Both(per_pixel, span, tex, nullptr, "ltfx, linear below the crossing");

	// The whole point of the rule: one span, two filters, chosen per pixel.
	EXPECT_EQ(mixed.px[0], linear.px[0]);
	EXPECT_EQ(mixed.px[1], linear.px[1]);
	EXPECT_NE(mixed.px[2], linear.px[2]);
	EXPECT_NE(mixed.px[3], linear.px[3]);

	// And the nearest side reads the texel the coordinate is actually in, which is
	// what withholding both the half-texel bias and the weight buys.
	EXPECT_EQ(mixed.px[2] & 0xff, 5u * 16u + 5u);
	EXPECT_EQ(mixed.px[3] & 0xff, 5u * 16u + 5u);
}

TEST_F(SwScanlineLodTest, LtfxGeFlipsWhichSideFilters)
{
	const Span span = CrossoverSpan();
	const u32* tex = AddressTexture();

	GSScanlineSelector all_linear = BaseSelector();
	all_linear.fst = 0;
	all_linear.ltf = 1;

	GSScanlineSelector flipped = all_linear;
	flipped.ltfx = 1;
	flipped.ltfx_ge = 1;

	const Row linear = Both(all_linear, span, tex, nullptr, "ltf everywhere");
	const Row mixed = Both(flipped, span, tex, nullptr, "ltfx, linear above the crossing");

	// Exactly the other two pixels.
	EXPECT_NE(mixed.px[0], linear.px[0]);
	EXPECT_NE(mixed.px[1], linear.px[1]);
	EXPECT_EQ(mixed.px[2], linear.px[2]);
	EXPECT_EQ(mixed.px[3], linear.px[3]);

	EXPECT_EQ(mixed.px[0] & 0xff, 5u * 16u + 5u);
	EXPECT_EQ(mixed.px[1] & 0xff, 5u * 16u + 5u);
}

// ---- the trilinear weight -----------------------------------------------------

namespace
{
// An affine span sitting on texel (0,0), so the only thing that varies between the
// tests below is the level blend.
Span FlatSpan()
{
	Span span{};
	span.s0 = 0.0f;
	span.t0 = 0.0f;
	span.q0 = 1.0f;
	span.ds = 0.0f;
	span.dt = 0.0f;
	span.dq = 0.0f;
	return span;
}
} // namespace

TEST_F(SwScanlineLodTest, EveryLaneOfAConstantLodFractionReachesTheBlend)
{
	// Level 0 is black and level 1 is 0x40 in every channel, so a blend shows up in
	// all four channels of all four pixels -- and a lane that never reaches the blend
	// leaves its channel at level 0's value.
	const u32* lo = Level0Texture();
	const u32* hi = Level1Texture();

	GSScanlineSelector sel = BaseSelector();
	sel.fst = 1;
	sel.ltf = 0;
	sel.mmin = 2; // trilinear
	sel.lcm = 1;  // constant level, so lod.i and lod.f come straight from the global

	Span span = FlatSpan();
	span.lod_i = 0;
	span.lod_f = GSVector4i(0x8000).xxxxlh();

	const Row got = Both(sel, span, lo, hi, "constant lod fraction");

	// Uniform weight, so a uniform result -- in every channel of every pixel.
	for (int i = 0; i < 4; i++)
	{
		EXPECT_EQ(got.px[i], got.px[0]) << "pixel " << i << " blended on a different weight";
		for (int ch = 0; ch < 4; ch++)
		{
			const u32 v = (got.px[i] >> (ch * 8)) & 0xff;
			EXPECT_GT(v, 0u) << "pixel " << i << " channel " << ch << " never left level 0";
			EXPECT_LT(v, 0x40u) << "pixel " << i << " channel " << ch << " jumped to level 1";
		}
	}

	// The shape of the bug this pins: the fraction used to be broadcast with
	// xxxxl().xxzz(), which leaves lanes 5 and 7 zero -- the blue and alpha of pixels
	// 2 and 3. Feed the scanline exactly that and those two pixels come out different
	// from the other two, which is what a real draw was doing.
	Span holed = span;
	holed.lod_f = GSVector4i(0x8000).xxxxlh();
	holed.lod_f.U16[5] = 0;
	holed.lod_f.U16[7] = 0;

	const Row bad = Both(sel, holed, lo, hi, "lod fraction with lanes 5 and 7 zeroed");
	EXPECT_EQ(bad.px[0], got.px[0]);
	EXPECT_EQ(bad.px[1], got.px[1]);
	EXPECT_NE(bad.px[2], got.px[2]);
	EXPECT_NE(bad.px[3], got.px[3]);
}

TEST_F(SwScanlineLodTest, TheTrilinearWeightIsFourBits)
{
	const u32* lo = Level0Texture();
	const u32* hi = Level1Texture();

	GSScanlineSelector sel = BaseSelector();
	sel.fst = 1;
	sel.ltf = 0;
	sel.mmin = 2;
	sel.lcm = 1;

	Span a = FlatSpan();
	a.lod_f = GSVector4i(0x1000).xxxxlh();
	Span b = FlatSpan();
	b.lod_f = GSVector4i(0x1fff).xxxxlh();
	Span c = FlatSpan();
	c.lod_f = GSVector4i(0x2000).xxxxlh();

	const Row ra = Both(sel, a, lo, hi, "weight 0x1000");
	const Row rb = Both(sel, b, lo, hi, "weight 0x1fff");
	const Row rc = Both(sel, c, lo, hi, "weight 0x2000");

	// Everything below the top four bits is thrown away, and the next step up is a
	// different colour: sixteen steps across the level, not two hundred and fifty.
	EXPECT_EQ(ra.px[0], rb.px[0]);
	EXPECT_NE(ra.px[0], rc.px[0]);
}

// The renderer builds the constant fraction with xxxxlh(), which is what puts it in
// all eight 16-bit lanes. The previous xxxxl().xxzz() left lanes 5 and 7 zero, and
// the test above shows what the scanline then does with it.
TEST(SwScanlineLodFraction, TheConstantFractionBroadcastFillsEveryLane)
{
	const GSVector4i f = GSVector4i(0x1234).xxxxlh();

	for (int i = 0; i < 8; i++)
		EXPECT_EQ(f.U16[i], 0x1234) << "lane " << i;
}

// TEX1.K arrives SIGNED -- `s32 K : 12` -- so reading it needs the 1:7:4 scale and
// nothing else. The draw dumper used to sign-extend it a second time, which turns
// every negative K into a number an order of magnitude too large: the -30 that
// Spider-Man 3's draw 16033 sets (-2 levels, so its mip chain runs) printed as
// -258, and a -258 reads as a chain that can never leave level 0. That misreading
// cost a round's premise, which is why the conversion is a named function with a
// test rather than an expression at the format string.
TEST(Tex1KDecode, TheFieldIsAlreadySignedAndScalesByASixteenth)
{
	GIFRegTEX1 t = {};

	t.K = -32;
	EXPECT_EQ(t.K, -32);
	EXPECT_FLOAT_EQ(t.KLevels(), -2.0f);

	t.K = -30;
	EXPECT_FLOAT_EQ(t.KLevels(), -1.875f);

	// The two ends of the twelve-bit field, and zero.
	t.K = 2047;
	EXPECT_FLOAT_EQ(t.KLevels(), 127.9375f);
	t.K = -2048;
	EXPECT_FLOAT_EQ(t.KLevels(), -128.0f);
	t.K = 0;
	EXPECT_FLOAT_EQ(t.KLevels(), 0.0f);

	// What the second sign-extension did, stated so a revert fails here. -32 is the
	// value the misread was found on, and -258 is what the context file printed.
	t.K = -32;
	const float doubled = static_cast<float>((static_cast<int>(t.K) ^ 0x800) - 0x800) / 16.0f;
	EXPECT_NE(doubled, t.KLevels());
	EXPECT_FLOAT_EQ(doubled, -258.0f);
}

// The renderer's own use of the field: `TEX1.K << 12` puts K into the 16.16 the LOD
// walk carries, folding the sixteenth in (1/16 * 65536 = 4096). A doubly
// sign-extended K would land 137 levels away from where the walk needs it.
TEST(Tex1KDecode, TheRendererCarriesKAsSixteenPointSixteen)
{
	GIFRegTEX1 t = {};
	t.K = -32;

	const int k = static_cast<int>(t.K) << 12;

	EXPECT_EQ(k, -131072);
	EXPECT_FLOAT_EQ(static_cast<float>(k) / 65536.0f, t.KLevels());
}

// The console's logarithm is a table, and the table has a shape the capture pinned
// exactly. These tests are on the table itself rather than on a scanline, because
// a wrong entry is a wrong level everywhere and the scanline suites would only
// show it where their own cases happen to land.
TEST(LevelOfDetailTable, TheThirdRowIsTheTrueLogarithmRounded)
{
	for (int i = 0; i < GS_LOD_TABLE_SIZE; i++)
	{
		const double exact = std::log2(1.0 + i / 128.0) * 128.0;

		EXPECT_EQ(GSLevelOfDetailTable[3][i], static_cast<s32>(std::lround(exact))) << "index " << i;
	}
}

TEST(LevelOfDetailTable, TheSecondRowIsTheThirdHalvedUpwards)
{
	for (int i = 0; i < GS_LOD_TABLE_SIZE; i++)
	{
		const s32 t3 = GSLevelOfDetailTable[3][i];

		EXPECT_EQ(GSLevelOfDetailTable[2][i], (t3 + 1) / 2) << "index " << i;
	}
}

// ⚠️ Index 115 -- mantissa 1.8984375 -- steps BACKWARDS in the two shallow rows,
// in both byte-identical console runs and at sixteen independent K values. It is
// reproduced on purpose: a level can fall and rise again inside one Q ramp, and a
// monotone table would be a smoothing of the hardware, not a fix to it.
TEST(LevelOfDetailTable, TheBrokenEntryIsReproduced)
{
	EXPECT_EQ(GSLevelOfDetailTable[0][114], 15);
	EXPECT_EQ(GSLevelOfDetailTable[0][115], 14);
	EXPECT_EQ(GSLevelOfDetailTable[0][116], 15);

	EXPECT_EQ(GSLevelOfDetailTable[1][114], 30);
	EXPECT_EQ(GSLevelOfDetailTable[1][115], 29);
	EXPECT_EQ(GSLevelOfDetailTable[1][116], 30);

	// And only there, and only in those two rows.
	for (int l = 0; l < 4; l++)
	{
		for (int i = 1; i < GS_LOD_TABLE_SIZE; i++)
		{
			if (l < 2 && i == 115)
				continue;

			EXPECT_GE(GSLevelOfDetailTable[l][i], GSLevelOfDetailTable[l][i - 1])
				<< "row " << l << " index " << i;
		}
	}
}

// The index is the top seven fractional bits of Q's MANTISSA, and the exponent
// enters only through the shift -- which is what makes the table repeat per octave
// exactly. gs-lod separated the two: a table on 1/Q's mantissa would step at
// values that are not dyadic in Q, and every measured step lands on a multiple of
// 1/128 of Q's.
TEST(LevelOfDetailTable, TheIndexIsQsMantissaAndTheTableRepeatsPerOctave)
{
	for (int i = 0; i < GS_LOD_TABLE_SIZE; i++)
	{
		const float m = 1.0f + static_cast<float>(i) / 128.0f;

		u32 bits;
		std::memcpy(&bits, &m, sizeof(bits));
		EXPECT_EQ(GSLevelOfDetailIndex(bits), static_cast<u32>(i)) << "index " << i;
		EXPECT_EQ(GSLevelOfDetailExponent(bits), 0) << "index " << i;

		// The same mantissa two octaves down: same index, exponent lower by two, so
		// the level of detail is exactly 2 * 2^(4+L) of a level deeper.
		const float q = m / 4.0f;
		const s32 a = GSLevelOfDetail16(m, GSLevelOfDetailTable[1], 0, 5);
		const s32 b = GSLevelOfDetail16(q, GSLevelOfDetailTable[1], 0, 5);

		EXPECT_EQ(b - a, 2 * 32) << "index " << i;
	}
}

// TEX1.K is already in sixteenths of a level, so it enters the sum unscaled, and
// the round-off mode is (LOD16 + 8) >> 4 with ties going UP -- measured at K = -8,
// +8, +24, +40, +56 and +72.
TEST(LevelOfDetailTable, KIsSixteenthsAndATieRoundsUp)
{
	// Mantissa 1.0, exponent 0: the table returns 0, so LOD16 is K exactly.
	const float one = 1.0f;

	for (const s32 k : {-8, 8, 24, 40, 56, 72})
	{
		const s32 lod16 = GSLevelOfDetail16(one, GSLevelOfDetailTable[0], k, 4);
		EXPECT_EQ(lod16, k);

		// What the scanline then does, on the 16.16 it carries the level in.
		const s32 rounded = ((lod16 << 12) + 0x8000) >> 16;
		EXPECT_EQ(rounded, (k + 8) >> 4) << "K = " << k;
	}

	// A tie takes the higher level, not the nearer even one.
	EXPECT_EQ((8 + 8) >> 4, 1);
	EXPECT_EQ((24 + 8) >> 4, 2);
}

} // namespace

#endif // ARCH_ARM64

}

namespace SWParity_scanline_tclag {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// Console-conformance pins for where a triangle's texture coordinate DDA lands.
//
// The gs-shade hardware capture (SCPH-30001, 2026-08-13) read the sampled
// coordinate back directly, through a palette whose every entry names its own
// texel, and found ours one sixteenth of a texel HIGH and never low -- but only
// where the exact coordinate lands on a sixteenth, and only where the walk is
// going forward:
//
//   gradient of four sixteenths per pixel, forward   3,840 of 3,840 readings high
//   the same gradient, descending                    0 of 2,048 -- exact
//   a gradient whose per-pixel step does not
//     terminate in binary                            12 of 3,072
//   a still coordinate                               exact, in every section
//
// Read together those say the hardware's coordinate trails the exact plane in the
// direction the walk is going, by less than a sixteenth. Trailing a forward walk
// puts it below a boundary, so it floors one sixteenth lower than we do; trailing
// a backward walk puts it above, where it floors where we already do; and a still
// coordinate does not trail at all, which is what makes the lag the step's rather
// than the seed's. Sprites are exact on silicon (gs-interp, 3,072 of 3,072,
// negative gradients included) and take nothing.
//
// Where the lag comes from inside the hardware's walk is unfitted -- it is one
// unit of our own 16.16 coordinate, the smallest bias that reproduces every
// reading.
//
// The suite drives the real GSSetupPrim and GSDrawScanline generators, so it pins
// the two halves together: setup deciding which axes walk forward, and the
// scanline spending that on the coordinate. It reads the sampled texel directly
// by drawing DECAL out of a texture whose every texel names its own address.
//
// Rides ARCH_ARM64 like its siblings: the generators are per-architecture.


#ifdef ARCH_ARM64



#ifndef _WIN32
#else
#endif

namespace
{
using DrawScanlinePtr = void (*)(int pixels, int left, int top, const GSVertexSW& scan, GSScanlineLocalData& local);
using SetupPrimPtr = void (*)(const GSVertexSW* vertex, const u16* index, const GSVertexSW& dscan, GSScanlineLocalData& local);

// One texel, in the 16.16 the coordinate is carried in.
constexpr int kTexel = 0x10000;

// The affine accumulator's grid: fifteen fractional bits of a texel, so every
// value on it is even in our 16.16. See GSCoordinateWalk.h.
constexpr int kGrid = ~1;

// The rule this suite exists to pin, written once.
//
// The affine coordinate walks on the console's 12.15 grid (GSCoordinateWalk.h):
// the seed and the per-pixel step are floored onto it, and a lane sits at
// seed + n * step -- NOT at the exact plane's value. On top of that a triangle
// trails by one unit on each axis whose walk goes forward, which can only change
// the texel when the coordinate lands on a boundary.
//
// The forward test is on the WALKED step, so a gradient below one grid unit per
// pixel is still and takes no lag.
int SampledAddress(int u0, int du, int v0, int dv, int lane, bool sprite)
{
	const int su = du & kGrid;
	const int sv = dv & kGrid;

	int u = (u0 & kGrid) + lane * su;
	int v = (v0 & kGrid) + lane * sv;

	// The exemption this suite's geometry cannot reach: a triangle whose twice-area
	// is a power of two takes no trail on either axis (GSCoordinateWalk.h). Every
	// case here hands setup three vertices at the origin, so twice the area is
	// zero, which is not a power of two -- these spans all trail.
	if (!sprite)
	{
		if (su > 0)
			u -= 1;
		if (sv > 0)
			v -= 1;
	}

	// The sixteenth-of-a-texel index truncates toward zero rather than flooring
	// (GSDrawScanline.cpp), so a negative coordinate takes a sixteenth less one
	// before the shift below floors it. This sweep is the only case in the suite
	// that reaches a negative coordinate at all -- a descending walk from a seed
	// of zero -- and it is the scalar mirror of the vector form the renderer uses.
	u += (u >> 31) & 0xfff;
	v += (v >> 31) & 0xfff;

	// 16x16, repeating on both axes; the texture names its own address.
	return (((v >> 16) & 15) * 16) + ((u >> 16) & 15);
}

class SwScanlineTcLagTest : public SWParityTestBase
{
protected:
	static constexpr size_t kCodeSlotSize = 16 * 1024;
	static constexpr size_t kCodeBufferSize = 8 * kCodeSlotSize;

	static void SetUpTestSuite()
	{
#ifdef _WIN32
		s_code = static_cast<u8*>(VirtualAlloc(nullptr, kCodeBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#elif defined(__APPLE__)
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#else
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#endif
		s_code_used = 0;
		s_mem = new GSLocalMemory();
	}

	static void TearDownTestSuite()
	{
		delete s_mem;
		s_mem = nullptr;

		if (s_code)
		{
#ifdef _WIN32
			VirtualFree(s_code, 0, MEM_RELEASE);
#else
			munmap(s_code, kCodeBufferSize);
#endif
			s_code = nullptr;
		}
	}

	static u8* Slot()
	{
		if (!s_code || s_code_used + kCodeSlotSize > kCodeBufferSize)
			return nullptr;

		u8* slot = s_code + s_code_used;
		s_code_used += kCodeSlotSize;
		return slot;
	}

	static DrawScanlinePtr CompileScanline(GSScanlineSelector sel)
	{
		u8* slot = Slot();
		if (!slot)
			return nullptr;

		HostSys::BeginCodeWrite();
		GSDrawScanlineCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<DrawScanlinePtr>(const_cast<u8*>(cg.GetCode()));
	}

	// The setup generator takes a reduced key, exactly as GetScanlineGlobalData
	// builds it -- so this compiles the same variant a real draw would.
	static SetupPrimPtr CompileSetup(GSScanlineSelector full)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.iip = full.iip;
		sel.tfx = full.tfx;
		sel.tcc = full.tcc;
		sel.fst = full.fst;
		sel.uvwalk = full.uvwalk;
		sel.fge = full.fge;
		sel.prim = full.prim;
		sel.fb = full.fb;
		sel.zb = full.zb;
		sel.zoverflow = full.zoverflow;
		sel.zequal = full.zequal;
		sel.notest = full.notest;

		u8* slot = Slot();
		if (!slot)
			return nullptr;

		HostSys::BeginCodeWrite();
		GSSetupPrimCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<SetupPrimPtr>(const_cast<u8*>(cg.GetCode()));
	}

	// DECAL out of a nearest-filtered 16x16 texture into PSMCT32: the stored pixel
	// is the texel, and the texel is its own address, so every pixel reports the
	// coordinate the scanline sampled at.
	static GSScanlineSelector MakeSelector(u32 prim, bool uvwalk)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.fpsm = 0;
		sel.zpsm = 3;
		sel.atst = ATST_ALWAYS;
		sel.tfx = TFX_DECAL;
		sel.tcc = 1;
		sel.fst = 1;
		sel.uvwalk = uvwalk ? 1 : 0;
		sel.ltf = 0;
		sel.tlu = 0;
		sel.tw = 1; // 1 << (tw + 3) == 16 texels wide
		sel.ababcd = 0xff;
		sel.prim = prim;
		sel.iip = 0;
		sel.fwrite = 1;
		sel.notest = 1;
		sel.colclamp = 1;
		return sel;
	}

	static const GSPixelOffset4* GetOffsets()
	{
		GIFRegFRAME frame;
		frame.U64 = 0;
		frame.FBP = 0;
		frame.FBW = 1;
		frame.PSM = PSMCT32;

		GIFRegZBUF zbuf;
		zbuf.U64 = 0;
		zbuf.ZBP = 256;
		zbuf.PSM = PSMZ32;

		return s_mem->GetPixelOffset4(frame, zbuf);
	}

	static u32 PixelAddr(int x, int y)
	{
		return GSLocalMemory::m_psm[PSMCT32].info.pa(x, y, 0, 1);
	}

	// Draws four pixels of one primitive whose coordinate starts at (u0, v0) and
	// steps by (du, dv) per pixel, all in 16.16, and returns the texture address
	// each pixel sampled.
	static void RunWalk(GSScanlineSelector sel, SetupPrimPtr setup, DrawScanlinePtr draw,
		int u0, int du, int v0, int dv, int out[4])
	{
		u32* vm32 = s_mem->vm32();
		for (int x = 0; x < 4; x++)
			vm32[PixelAddr(x, 0)] = 0x0Du;

		alignas(32) GSScanlineGlobalData global{};
		alignas(32) GSScanlineLocalData local = {};
		alignas(32) u32 tex[256];
		for (int i = 0; i < 256; i++)
			tex[i] = static_cast<u32>(i) | (static_cast<u32>(i) << 8) | (static_cast<u32>(i) << 16) | (static_cast<u32>(i) << 24);

		global.sel = sel;
		global.vm = s_mem->vm8();
		global.fzbr = GetOffsets()->row;
		global.fzbc = GetOffsets()->col;
		global.fm = GSVector4i(0);
		global.zm = GSVector4i(static_cast<int>(0xffffffffu));
		global.tex[0] = tex;

		// REPEAT on both axes over sixteen texels, as GSRendererSW derives it.
		global.t.min = GSVector4i::zero();
		global.t.max = GSVector4i::zero();
		global.t.mask = GSVector4i(static_cast<int>(0xffffffffu));
		for (int i = 0; i < 8; i++)
			global.t.min.U16[i] = 15;

		local.gd = &global;

		// Only dscan.t matters here: no depth, no fog, and DECAL with TCC on takes
		// no vertex colour, so setup runs its texture half alone.
		GSVertexSW vertex[3];
		u16 index[3] = {0, 1, 2};
		for (int i = 0; i < 3; i++)
			vertex[i] = GSVertexSW::zero();

		GSVertexSW dscan = GSVertexSW::zero();
		dscan.t = GSVector4(static_cast<float>(du), static_cast<float>(dv), 0.0f, 0.0f);

		setup(vertex, index, dscan, local);

		GSVertexSW scan = GSVertexSW::zero();
		scan.t = GSVector4(static_cast<float>(u0), static_cast<float>(v0), 0.0f, 0.0f);

		draw(4, 0, 0, scan, local);

		for (int x = 0; x < 4; x++)
			out[x] = static_cast<int>(vm32[PixelAddr(x, 0)] & 0xff);
	}

	// Compiled once per primitive class: the generators are deterministic, and a
	// pair per test would outrun the code buffer.
	static bool Walk(u32 prim, int u0, int du, int v0, int dv, int out[4], bool uvwalk = true)
	{
		const int sprite = (prim == GS_SPRITE_CLASS) ? 1 : 0;
		const int uv = uvwalk ? 1 : 0;
		if (!s_setup[sprite][uv])
		{
			const GSScanlineSelector sel = MakeSelector(prim, uvwalk);
			s_sel[sprite][uv] = sel;
			s_setup[sprite][uv] = CompileSetup(sel);
			s_draw[sprite][uv] = CompileScanline(sel);
		}

		if (!s_setup[sprite][uv] || !s_draw[sprite][uv])
			return false;

		RunWalk(s_sel[sprite][uv], s_setup[sprite][uv], s_draw[sprite][uv], u0, du, v0, dv, out);
		return true;
	}

	static GSScanlineSelector s_sel[2][2];
	static SetupPrimPtr s_setup[2][2];
	static DrawScanlinePtr s_draw[2][2];

	static u8* s_code;
	static size_t s_code_used;
	static GSLocalMemory* s_mem;
};

GSScanlineSelector SwScanlineTcLagTest::s_sel[2][2] = {};
SetupPrimPtr SwScanlineTcLagTest::s_setup[2][2] = {};
DrawScanlinePtr SwScanlineTcLagTest::s_draw[2][2] = {};
u8* SwScanlineTcLagTest::s_code = nullptr;
size_t SwScanlineTcLagTest::s_code_used = 0;
GSLocalMemory* SwScanlineTcLagTest::s_mem = nullptr;

// The defect, at its trigger: a forward walk whose exact coordinate lands on a
// texel boundary at every pixel. Before the fix these sampled texels 4,5,6,7; the
// console samples the one below each.
//
// gs-tclag2 drew exactly this shape on silicon -- one texel per pixel, NEAREST,
// CLAMP, the STQ plane at Q = 1 -- and it TRAILS, on 5,456 of 5,456 landing
// columns. Round 62 briefly had this reading the other way, from a rule fitted to
// Jak 3's palette blit; gs-tclag3 found the property that blit actually has (its
// twice-area is a power of two) and this geometry does not.
TEST_F(SwScanlineTcLagTest, AForwardWalkOnABoundarySamplesTheTexelBelow)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 4 * kTexel, kTexel, 0, 0, got));

	// Stated as the values, so a failure shows the walk rather than an arithmetic
	// identity: this is the reading the capture took.
	EXPECT_EQ(got[0], 3);
	EXPECT_EQ(got[1], 4);
	EXPECT_EQ(got[2], 5);
	EXPECT_EQ(got[3], 6);
}

// And nowhere else. The same gradient started half a texel off a boundary keeps
// every pixel where it was: the lag is smaller than the distance to the next
// sixteenth, so it can only move a coordinate that has landed exactly on one.
TEST_F(SwScanlineTcLagTest, AForwardWalkOffABoundaryDoesNotMove)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 4 * kTexel + kTexel / 2, kTexel, 0, 0, got));

	EXPECT_EQ(got[0], 4);
	EXPECT_EQ(got[1], 5);
	EXPECT_EQ(got[2], 6);
	EXPECT_EQ(got[3], 7);
}

// A descending walk trails the other way, which floors where we already do. The
// capture's descending section is identical to the console on all 2,048 readings,
// so this direction must NOT move.
TEST_F(SwScanlineTcLagTest, ADescendingWalkDoesNotMove)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 12 * kTexel, -kTexel, 0, 0, got));

	EXPECT_EQ(got[0], 12);
	EXPECT_EQ(got[1], 11);
	EXPECT_EQ(got[2], 10);
	EXPECT_EQ(got[3], 9);
}

// A still coordinate does not trail. This is the cell that makes the whole thing
// attributable: with neither interpolator moving, every arm was already exact
// against the console, and it has to stay that way.
TEST_F(SwScanlineTcLagTest, AStillCoordinateDoesNotMove)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 4 * kTexel, 0, 0, 0, got));

	for (int x = 0; x < 4; x++)
		EXPECT_EQ(got[x], 4) << "pixel " << x;
}

// Inside one primitive, a quarter-texel gradient crosses a boundary on one pixel
// in four -- so one pixel moves and three do not. The capture measured exactly
// this shape, at four sixteenths per pixel.
TEST_F(SwScanlineTcLagTest, OnlyThePixelsOnABoundaryMove)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 4 * kTexel, kTexel / 4, 0, 0, got));

	EXPECT_EQ(got[0], 3); // exactly on texel 4, so it takes the one below
	EXPECT_EQ(got[1], 4);
	EXPECT_EQ(got[2], 4);
	EXPECT_EQ(got[3], 4);
}

// The V axis carries the same rule and is decided separately: here U is still and
// V walks forward, so only V moves.
TEST_F(SwScanlineTcLagTest, EachAxisIsDecidedOnItsOwnStep)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 4 * kTexel, 0, 4 * kTexel, kTexel, got));

	// U stays at texel 4 while V takes the row below each exact one.
	EXPECT_EQ(got[0], 3 * 16 + 4);
	EXPECT_EQ(got[1], 4 * 16 + 4);
	EXPECT_EQ(got[2], 5 * 16 + 4);
	EXPECT_EQ(got[3], 6 * 16 + 4);
}

// Sprites take nothing. Silicon's sprite coordinate is exact and so is ours, and
// sprites are how every two-dimensional game on the machine draws -- a lag here
// would shift blits by a whole texel.
TEST_F(SwScanlineTcLagTest, ASpriteTakesNoLag)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_SPRITE_CLASS, 4 * kTexel, kTexel, 0, 0, got));

	EXPECT_EQ(got[0], 4);
	EXPECT_EQ(got[1], 5);
	EXPECT_EQ(got[2], 6);
	EXPECT_EQ(got[3], 7);
}

// The affine step is floored ONCE onto the accumulator's grid and then
// accumulated, which is not the same sequence as truncating each product.
//
// Step three units: it floors to two, so lane three sits at 0x2FFFE and stays
// inside texel two. Truncating the product instead puts lane three at 0x30001,
// one texel further on -- which is what our arm did before the width fit.
TEST_F(SwScanlineTcLagTest, TheAffineStepIsFlooredOnceThenAccumulated)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_SPRITE_CLASS, 0x2FFF8, 3, 3 * kTexel, 0, got));

	EXPECT_EQ(got[0], 3 * 16 + 2);
	EXPECT_EQ(got[1], 3 * 16 + 2);
	EXPECT_EQ(got[2], 3 * 16 + 2);
	EXPECT_EQ(got[3], 3 * 16 + 2);
}

// A constant-Q triangle's own ST plane arrives on the same road -- the scanline
// bit-casts a 16.16 integer for it too -- and it does NOT take the accumulator's
// grid. gs-grad's tc-snap is that shape and refuses it: two of its cases go from
// zero to 200 differing console words when the step is floored on. So the same
// walk with uvwalk clear keeps the step whole, and lane three, one unit of lag
// included, lands on the boundary into texel three instead of short of it.
TEST_F(SwScanlineTcLagTest, ATrianglePlaneKeepsTheWholeStep)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 0x2FFF8, 3, 3 * kTexel, 0, got, false));

	EXPECT_EQ(got[0], 3 * 16 + 2);
	EXPECT_EQ(got[1], 3 * 16 + 2);
	EXPECT_EQ(got[2], 3 * 16 + 2);
	EXPECT_EQ(got[3], 3 * 16 + 3);
}

// And the lag follows the walked step, not the plane's. A gradient of one unit
// per pixel floors to zero, so the coordinate is STILL -- and a still coordinate
// does not trail, which is the gs-shade reading the lag was built from. Testing
// the plane's sign instead pulls this whole span down to texel three.
TEST_F(SwScanlineTcLagTest, AStepBelowTheGridWalksNowhereAndTakesNoLag)
{
	int got[4];
	ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 4 * kTexel, 1, 0, 0, got));

	EXPECT_EQ(got[0], 4);
	EXPECT_EQ(got[1], 4);
	EXPECT_EQ(got[2], 4);
	EXPECT_EQ(got[3], 4);
}

// The rule over a spread of walks, so the cases above are a sample of it rather
// than the whole of it: both directions on both axes, sub-texel and multi-texel
// steps, and a step that does not terminate in binary.
TEST_F(SwScanlineTcLagTest, TheRuleHoldsAcrossAWalkSweep)
{
	static const int kStarts[] = {0, 4 * kTexel, 4 * kTexel + kTexel / 2, 15 * kTexel, 4 * kTexel + 7};
	static const int kSteps[] = {0, 1, -1, kTexel / 16, -kTexel / 16, kTexel / 4, kTexel, 2 * kTexel,
		-2 * kTexel, 11117};

	for (u32 prim : {static_cast<u32>(GS_TRIANGLE_CLASS), static_cast<u32>(GS_SPRITE_CLASS)})
	{
		const bool sprite = (prim == GS_SPRITE_CLASS);

		for (int u0 : kStarts)
		{
			for (int du : kSteps)
			{
				int got[4];
				// V still: a sprite's V does not step in the scanline at all, so
				// sweeping it would compare against a rule that path never applies.
				ASSERT_TRUE(Walk(prim, u0, du, 3 * kTexel, 0, got));

				for (int x = 0; x < 4; x++)
				{
					EXPECT_EQ(got[x], SampledAddress(u0, du, 3 * kTexel, 0, x, sprite))
						<< (sprite ? "sprite" : "triangle") << " u0=" << u0
						<< " du=" << du << " pixel " << x;
				}
			}
		}
	}

	// And the V axis, on the primitive class whose V walks.
	for (int v0 : kStarts)
	{
		for (int dv : kSteps)
		{
			int got[4];
			ASSERT_TRUE(Walk(GS_TRIANGLE_CLASS, 5 * kTexel, 0, v0, dv, got));

			for (int x = 0; x < 4; x++)
			{
				EXPECT_EQ(got[x], SampledAddress(5 * kTexel, 0, v0, dv, x, false))
					<< "triangle v0=" << v0 << " dv=" << dv << " pixel " << x;
			}
		}
	}
}

} // namespace

#endif // ARCH_ARM64

}

namespace SWParity_scanline_texfunc {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// Console-conformance pins for what the texture function multiplies.
//
// The gs-shade hardware capture (SCPH-30001, 2026-08-13) put a moving vertex
// colour and a moving palettised texel in series for the first time and asked
// which colour the product is formed from. It answered without a model of either
// interpolator: the same colour read back through four different multipliers
// brackets the value the hardware holds, and that bracket excludes the product of
// the eight-bit STORED colour on 0 of 24,576 readings. Ours excluded it on 17.99%
// -- because the scanline carries the colour as fixed point with seven fractional
// bits and fed all fifteen to the multiply.
//
// So: the texture function multiplies the byte, and the fraction the DDA carries
// between pixels is not part of the product. A gouraud gradient has a fraction at
// almost every pixel, which is why a flat-shaded corpus cannot see this and why
// the capture had to.
//
// The suite is written differentially first -- a fractional colour must store
// exactly what its truncation stores -- so it pins the rule without recomputing a
// format conversion or a clamp. The absolute test that follows states the product
// itself, so a failure names the cause rather than only the symptom.
//
// Rides ARCH_ARM64 like its siblings gs_sw_scanline_date_tests.cpp and
// gs_sw_scanline_dither_tests.cpp: all three compile the JIT scanline directly,
// and the generator is per-architecture.


#ifdef ARCH_ARM64



#ifndef _WIN32
#else
#endif

namespace
{
using DrawScanlinePtr = void (*)(int pixels, int left, int top, const GSVertexSW& scan, GSScanlineLocalData& local);

// What the console does, per channel: the stored eight-bit colour times the
// texel, shifted by seven, saturating.
u32 ConsoleModulate(u32 colour, u32 texel)
{
	const u32 p = (colour * texel) >> 7;
	return p > 255 ? 255 : p;
}

class SwScanlineTexFuncTest : public SWParityTestBase
{
protected:
	static constexpr size_t kCodeSlotSize = 16 * 1024;
	static constexpr size_t kCodeBufferSize = 8 * kCodeSlotSize;

	static void SetUpTestSuite()
	{
#ifdef _WIN32
		s_code = static_cast<u8*>(VirtualAlloc(nullptr, kCodeBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#elif defined(__APPLE__)
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#else
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#endif
		s_code_used = 0;

		// Also populates the static m_psm swizzle tables on first construction.
		s_mem = new GSLocalMemory();
	}

	static void TearDownTestSuite()
	{
		delete s_mem;
		s_mem = nullptr;

		if (s_code)
		{
#ifdef _WIN32
			VirtualFree(s_code, 0, MEM_RELEASE);
#else
			munmap(s_code, kCodeBufferSize);
#endif
			s_code = nullptr;
		}
	}

	static DrawScanlinePtr Compile(GSScanlineSelector sel)
	{
		if (!s_code || s_code_used + kCodeSlotSize > kCodeBufferSize)
			return nullptr;

		u8* slot = s_code + s_code_used;
		s_code_used += kCodeSlotSize;

		HostSys::BeginCodeWrite();
		GSDrawScanlineCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<DrawScanlinePtr>(const_cast<u8*>(cg.GetCode()));
	}

	// A flat, nearest-filtered, unblended textured sprite into PSMCT32. Every
	// axis is pinned so that only the texture function varies: the coordinate is
	// clamped to a single texel below, so filtering, wrapping and addressing
	// cannot contribute to what is stored.
	static GSScanlineSelector MakeSelector(u32 tfx, bool tcc)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.fpsm = 0;
		sel.zpsm = 3;
		sel.atst = ATST_ALWAYS;
		sel.tfx = tfx;
		sel.tcc = tcc ? 1 : 0;
		sel.fst = 1;
		sel.ltf = 0;
		sel.tlu = 0;
		sel.tw = 0;
		sel.ababcd = 0xff;
		sel.prim = GS_SPRITE_CLASS;
		sel.iip = 0;
		sel.fwrite = 1;
		sel.notest = 1;
		sel.colclamp = 1;
		return sel;
	}

	// FBP 0, FBW 1. The depth side is unused but the offset table needs a
	// well-formed ZBUF.
	static const GSPixelOffset4* GetOffsets()
	{
		GIFRegFRAME frame;
		frame.U64 = 0;
		frame.FBP = 0;
		frame.FBW = 1;
		frame.PSM = PSMCT32;

		GIFRegZBUF zbuf;
		zbuf.U64 = 0;
		zbuf.ZBP = 256;
		zbuf.PSM = PSMZ32;

		return s_mem->GetPixelOffset4(frame, zbuf);
	}

	static u32 PixelAddr(int x, int y)
	{
		return GSLocalMemory::m_psm[PSMCT32].info.pa(x, y, 0, 1);
	}

	// One four-pixel row with the flat colour supplied on the scanline's own
	// grid: rb and ga each hold two channels in 16-bit lanes, scaled by 128, so
	// `frac` is the sub-unit part the DDA would be carrying mid-gradient.
	//
	// Returns what landed in the first pixel; all four are the same draw.
	static u32 RunRow(DrawScanlinePtr fn, GSScanlineSelector sel, const GSPixelOffset4* off,
		u32 texel, u32 r, u32 g, u32 b, u32 a, u32 frac)
	{
		u32* vm32 = s_mem->vm32();
		for (int x = 0; x < 4; x++)
			vm32[PixelAddr(x, 0)] = 0x0Du;

		alignas(32) GSScanlineGlobalData global{};
		alignas(32) GSScanlineLocalData local = {};
		alignas(32) u32 tex[4] = {texel, texel, texel, texel};

		global.sel = sel;
		global.vm = s_mem->vm8();
		global.fzbr = off->row;
		global.fzbc = off->col;
		global.fm = GSVector4i(0);
		global.zm = GSVector4i(static_cast<int>(0xffffffffu));
		global.tex[0] = tex;
		// min = max = 0 with an all-zero mask is CLAMP to texel zero on both
		// axes, so every lane fetches tex[0] whatever the coordinate is.
		global.t.min = GSVector4i::zero();
		global.t.max = GSVector4i::zero();
		global.t.mask = GSVector4i::zero();

		local.gd = &global;
		local.c.rb = GSVector4i(static_cast<int>(((r << 7) | frac) | (((b << 7) | frac) << 16)));
		local.c.ga = GSVector4i(static_cast<int>(((g << 7) | frac) | (((a << 7) | frac) << 16)));

		const GSVertexSW scan = GSVertexSW::zero();
		fn(4, 0, 0, scan, local);

		return vm32[PixelAddr(0, 0)];
	}

	static u8* s_code;
	static size_t s_code_used;
	static GSLocalMemory* s_mem;
};

u8* SwScanlineTexFuncTest::s_code = nullptr;
size_t SwScanlineTexFuncTest::s_code_used = 0;
GSLocalMemory* SwScanlineTexFuncTest::s_mem = nullptr;

// A spread of colours and texels, including both ends and values whose product
// straddles the shift. Kept small because every entry is crossed with every
// fraction below.
constexpr u32 kLevels[] = {0, 1, 0x1f, 0x40, 0x7f, 0x80, 0xc3, 0xfe, 0xff};

// The defect, stated as the console states it: what the multiply sees is the
// byte. Sweeping the fraction across its whole range must change nothing.
//
// This is the test that was red before the fix -- every non-zero fraction that
// carried the product over an integer boundary stored a different pixel.
TEST_F(SwScanlineTexFuncTest, ModulateIgnoresTheColourFraction)
{
	const GSScanlineSelector sel = MakeSelector(TFX_MODULATE, true);
	DrawScanlinePtr fn = Compile(sel);
	ASSERT_NE(fn, nullptr);
	const GSPixelOffset4* off = GetOffsets();

	for (u32 texel : kLevels)
	{
		const u32 t = texel | (texel << 8) | (texel << 16) | (texel << 24);

		for (u32 c : kLevels)
		{
			const u32 reference = RunRow(fn, sel, off, t, c, c, c, c, 0);

			for (u32 frac : {1u, 2u, 63u, 64u, 65u, 126u, 127u})
			{
				EXPECT_EQ(reference, RunRow(fn, sel, off, t, c, c, c, c, frac))
					<< "texel=" << texel << " colour=" << c << " fraction=" << frac;
			}
		}
	}
}

// And what it stores is the console's product of that byte. Stated absolutely so
// a regression that truncates the colour but then rounds, or shifts by the wrong
// amount, cannot pass the differential test above on its own.
TEST_F(SwScanlineTexFuncTest, ModulateStoresTheProductOfTheStoredByte)
{
	const GSScanlineSelector sel = MakeSelector(TFX_MODULATE, true);
	DrawScanlinePtr fn = Compile(sel);
	ASSERT_NE(fn, nullptr);
	const GSPixelOffset4* off = GetOffsets();

	for (u32 texel : kLevels)
	{
		const u32 t = texel | (texel << 8) | (texel << 16) | (texel << 24);

		for (u32 c : kLevels)
		{
			for (u32 frac : {0u, 99u, 127u})
			{
				const u32 stored = RunRow(fn, sel, off, t, c, c, c, c, frac);
				const u32 want = ConsoleModulate(c, texel);

				EXPECT_EQ(stored & 0xff, want) << "R texel=" << texel << " colour=" << c;
				EXPECT_EQ((stored >> 8) & 0xff, want) << "G texel=" << texel << " colour=" << c;
				EXPECT_EQ((stored >> 16) & 0xff, want) << "B texel=" << texel << " colour=" << c;
				EXPECT_EQ((stored >> 24) & 0xff, want) << "A texel=" << texel << " colour=" << c;
			}
		}
	}
}

// With TCC off the alpha is the vertex's own byte and no product at all -- the
// capture measured that at 512/512, with the texture's alpha refuted on every
// separating reading. It shares the truncation with the colour path, so it
// belongs in the same sweep.
TEST_F(SwScanlineTexFuncTest, ModulateWithoutTCCStoresTheVertexAlphaByte)
{
	const GSScanlineSelector sel = MakeSelector(TFX_MODULATE, false);
	DrawScanlinePtr fn = Compile(sel);
	ASSERT_NE(fn, nullptr);
	const GSPixelOffset4* off = GetOffsets();

	for (u32 a : kLevels)
	{
		for (u32 frac : {0u, 64u, 127u})
		{
			const u32 stored = RunRow(fn, sel, off, 0x80808080u, 0x40, 0x40, 0x40, a, frac);
			EXPECT_EQ(stored >> 24, a) << "alpha=" << a << " fraction=" << frac;
		}
	}
}

// HIGHLIGHT and HIGHLIGHT2 modulate the same way and then add the vertex alpha
// into RGB, saturating. The capture scored both at 2,048/2,048 against that rule
// with a stepped colour -- which nothing had exercised before it -- so the same
// fraction sweep has to hold through them.
TEST_F(SwScanlineTexFuncTest, HighlightModesIgnoreTheColourFraction)
{
	const GSPixelOffset4* off = GetOffsets();

	for (u32 tfx : {static_cast<u32>(TFX_HIGHLIGHT), static_cast<u32>(TFX_HIGHLIGHT2)})
	{
		const GSScanlineSelector sel = MakeSelector(tfx, true);
		DrawScanlinePtr fn = Compile(sel);
		ASSERT_NE(fn, nullptr);

		for (u32 texel : kLevels)
		{
			const u32 t = texel | (texel << 8) | (texel << 16) | (texel << 24);

			for (u32 c : kLevels)
			{
				const u32 reference = RunRow(fn, sel, off, t, c, c, c, c, 0);

				for (u32 frac : {1u, 64u, 127u})
				{
					EXPECT_EQ(reference, RunRow(fn, sel, off, t, c, c, c, c, frac))
						<< "tfx=" << tfx << " texel=" << texel << " colour=" << c
						<< " fraction=" << frac;
				}
			}
		}
	}
}

// DECAL takes the texel and nothing else, which the capture confirmed at
// 1,024/1,024 by stepping the colour and watching nothing move. It is the control
// on the whole colour path: if a future change leaks the vertex colour into a
// product it should not form, this is where it shows.
TEST_F(SwScanlineTexFuncTest, DecalIgnoresTheVertexColourEntirely)
{
	const GSScanlineSelector sel = MakeSelector(TFX_DECAL, true);
	DrawScanlinePtr fn = Compile(sel);
	ASSERT_NE(fn, nullptr);
	const GSPixelOffset4* off = GetOffsets();

	for (u32 texel : kLevels)
	{
		const u32 t = texel | (texel << 8) | (texel << 16) | (texel << 24);
		const u32 reference = RunRow(fn, sel, off, t, 0, 0, 0, 0, 0);

		for (u32 c : kLevels)
		{
			for (u32 frac : {0u, 127u})
			{
				EXPECT_EQ(reference, RunRow(fn, sel, off, t, c, c, c, c, frac))
					<< "texel=" << texel << " colour=" << c << " fraction=" << frac;
			}
		}
		EXPECT_EQ(reference & 0xff, texel) << "texel=" << texel;
	}
}

} // namespace

#endif // ARCH_ARM64

}

namespace SWParity_setup_reciprocal {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// Which reciprocal the triangle setup truncates, and which attributes take it.
//
// The GS does not divide to form an attribute gradient: it multiplies by a
// reciprocal read out of a table eight significant bits wide. That much was
// measured on an SCPH-30001 over twenty-four baselines, but on one triangle
// SHAPE -- flat top, vertical left side, and a height of 1024, a power of two.
// Under that shape the reciprocal of the horizontal baseline and the reciprocal
// of the setup's cross product have the identical mantissa, so the capture could
// not say which quantity the hardware inverts, and neither could anybody reading
// it.
//
// A second capture swept the height alone. The exact gradient does not contain
// the height at all, so the reading needs no model: if the denominator were the
// baseline every height would draw the same row, and if it is the cross product
// the rows move. On the console they move -- up to 340 of 442 readings between
// two heights -- while two heights a power of two apart stay byte-identical, as
// every candidate requires. Across four shape classes the cross product's
// reciprocal explains 93.7% to 96.3% where the exact quotient explains 56% to
// 81%, and the class whose cross product is neither edge's length (a slanted
// left side, W*H - K*P) prefers it just as strongly.
//
// It does NOT apply to everything the setup forms. On the same sweep, at heights
// whose truncated reciprocals would move a sampled texture coordinate by three
// sixteenths of a texel and a depth value by more than a whole pixel step, the
// console's texture coordinates and depth values do not move at all -- and our
// arm, which divides exactly, is 100.00% against silicon on both. Colour and fog
// take the truncated reciprocal; s, t, q and depth take the exact quotient. The
// tests below hold both halves, because a fix that spreads is as wrong as one
// that does not land.
//
// Capture: the gs-shape console capture, SCPH-30001, GS revision 0x15, two
// byte-identical console runs.
//
// The rule lives in GSRasterizer::DrawTriangle, which has an ARM64/SSE4 body and an
// AVX2 twin, so this runs on both hosts: on x86 it is the AVX2 twin that is on trial.
// The gate is multi-ISA, not architecture -- a multi-ISA x86 build compiles the
// rasterizer into isa_sse4/isa_avx/isa_avx2 and has no isa_native at all, so the
// header cannot even be included there. That is every x86 CI configuration today, so
// on x86 this suite only runs in a local build with DISABLE_ADVANCE_SIMD=OFF.


#ifndef MULTI_ISA_SHARED_COMPILATION



namespace
{
GSVertexSW g_dscan;
bool g_setup_ran = false;

void RecordSetup(const GSVertexSW*, const u16*, const GSVertexSW& dscan, GSScanlineLocalData&)
{
	g_dscan = dscan;
	g_setup_ran = true;
}

void NoSpan(int, int, int, const GSVertexSW&, GSScanlineLocalData&) {}

// gs-shape's own geometry: a flat top from (8.5, 7) to (232.5, 7) -- a baseline of
// 224 pixels -- and a vertical left side running `height` rows down. The colour
// rises from black to (255, 191, 127, 63) along the top edge and is flat down the
// left one, which is what makes the walk's seed the first vertex's own value
// exactly, with no edge interpolation between the seed and the measurement.
//
// Colours are in the pipeline's own 1/128 grid (GSRendererSW builds them as
// byte << 7), so a gradient printed here is 128 times the per-pixel colour step.
GSVertexSW SetupOfTriangle(float height)
{
	GSVertexSW vertex[3];
	for (int i = 0; i < 3; i++)
	{
		vertex[i] = GSVertexSW::zero();
		vertex[i].p.F64[1] = 0.0;
	}

	vertex[0].p = GSVector4(8.5f, 7.0f, 0.0f, 0.0f);
	vertex[1].p = GSVector4(232.5f, 7.0f, 0.0f, 0.0f);
	vertex[2].p = GSVector4(8.5f, 7.0f + height, 0.0f, 0.0f);

	vertex[0].c = GSVector4(0.0f, 0.0f, 0.0f, 0.0f);
	vertex[1].c = GSVector4(255.0f * 128.0f, 191.0f * 128.0f, 127.0f * 128.0f, 63.0f * 128.0f);
	vertex[2].c = GSVector4(0.0f, 0.0f, 0.0f, 0.0f);

	// t is (s, t, q, f) on a triangle. The coordinate runs 48 texels -- 768
	// sixteenths -- across the baseline; the fog coefficient runs 0 to 255, on
	// the same 1/128 grid colour rides (GSRendererSW builds it as byte << 7), so
	// that its gradient is the size the interpolator actually sees. A fog value
	// of a bare 255 is a 128th of a real one and its gradient falls below the
	// resolution of the gradient itself.
	vertex[0].t = GSVector4(0.0f, 136.0f, 1.0f, 0.0f);
	vertex[1].t = GSVector4(768.0f, 136.0f, 1.0f, 255.0f * 128.0f);
	vertex[2].t = GSVector4(0.0f, 136.0f, 1.0f, 0.0f);

	// Depth rides the double lane, formed from the plane rather than from the
	// float32 barycentric coefficients the other attributes use.
	vertex[0].p.F64[1] = 1048576.0;
	vertex[1].p.F64[1] = 1048576.0 + 12582912.0;
	vertex[2].p.F64[1] = 1048576.0;

	static const u16 index[3] = {0, 1, 2};

	g_dscan = GSVertexSW::zero();
	g_setup_ran = false;

	isa_native::GSRasterizerData data;
	data.primclass = GS_TRIANGLE_CLASS;
	data.vertex = vertex;
	data.vertex_count = 3;
	data.index = const_cast<u16*>(index);
	data.index_count = 3;
	data.scissor = GSVector4i(0, 0, 640, 640);
	data.bbox = GSVector4i(0, 0, 640, 640);
	data.global.sel.key = 0;
	data.global.sel.iip = 1;
	data.setup_prim = &RecordSetup;
	data.draw_scanline = &NoSpan;
	// ⚠️ nullptr, deliberately. HasEdge() is "is there an edge callback", not "is
	// AA1 on", and a triangle with one runs a SECOND Flush carrying a zeroed
	// dscan -- which lands on the callback after the real one and reads as a
	// renderer that computes no gradients at all.
	data.draw_edge = nullptr;

	isa_native::GSRasterizer r(nullptr, 0, 1);
	r.Draw(data);

	EXPECT_TRUE(g_setup_ran) << "the setup callback never ran, so nothing was measured";
	return g_dscan;
}

// The exact quotient for this plane: 255 * 128 / 224, whatever the height.
constexpr float kExactRedGradient = 255.0f * 128.0f / 224.0f; // 145.714286

// What the console's OWN pixels admit. Sweeping the gradient and walking the
// block DDA against the row silicon stored at height 1024, the readings agree
// best (216 of 221) over exactly this interval -- and the exact quotient above
// sits outside it, which is the whole finding in one number.
constexpr float kConsoleBracketLo = 145.375f;
constexpr float kConsoleBracketHi = 145.500f;
} // namespace

TEST(SwSetupReciprocal, TheColourGradientMovesWithTheTriangleHeight)
{
	// The console's twin: one plane, one baseline, one left edge, two heights.
	// The exact gradient does not contain the height, so a renderer that divides
	// produces the identical number twice. Silicon does not.
	const GSVertexSW a = SetupOfTriangle(1024.0f);
	const GSVertexSW b = SetupOfTriangle(2287.0f);

	EXPECT_NE(a.c.x, b.c.x) << "the colour gradient is the same at both heights, so "
	                           "the setup is still dividing by the cross product "
	                           "rather than multiplying by its truncated reciprocal";
}

TEST(SwSetupReciprocal, TheConsoleExcludesTheExactQuotient)
{
	const GSVertexSW a = SetupOfTriangle(1024.0f);

	EXPECT_GE(a.c.x, kConsoleBracketLo);
	EXPECT_LE(a.c.x, kConsoleBracketHi);
	// Stated separately so a failure says which half is wrong: the value we ship
	// today is outside the bracket the console's own row admits.
	EXPECT_GT(kExactRedGradient, kConsoleBracketHi);
}

TEST(SwSetupReciprocal, TwoHeightsAPowerOfTwoApartGiveOneGradient)
{
	// The control, and it is the one that must be green both before and after: two
	// cross products differing by a factor of two share a mantissa, so no
	// candidate -- including the one being landed -- may separate them. The
	// console reads them byte-identical, 0 of 442.
	const GSVertexSW a = SetupOfTriangle(1024.0f);
	const GSVertexSW b = SetupOfTriangle(2048.0f);

	EXPECT_EQ(a.c.x, b.c.x);
	EXPECT_EQ(a.c.y, b.c.y);
	EXPECT_EQ(a.c.z, b.c.z);
	EXPECT_EQ(a.c.w, b.c.w);
}

TEST(SwSetupReciprocal, EveryColourChannelTakesIt)
{
	// The shortfall scales with the channel delta, so a rule that is additive
	// rather than proportional would show here. Deltas 255, 191, 127, 63.
	const GSVertexSW a = SetupOfTriangle(1024.0f);
	const GSVertexSW b = SetupOfTriangle(2287.0f);

	EXPECT_NE(a.c.y, b.c.y);
	EXPECT_NE(a.c.z, b.c.z);
	EXPECT_NE(a.c.w, b.c.w);
}

TEST(SwSetupReciprocal, FogTakesItToo)
{
	// Console: the fog rows move with the height at exactly the heights where the
	// colour rows move, and are identical at exactly the heights where the colour
	// rows are identical.
	const GSVertexSW a = SetupOfTriangle(1024.0f);
	const GSVertexSW b = SetupOfTriangle(2287.0f);

	EXPECT_NE(a.t.w, b.t.w);
}

TEST(SwSetupReciprocal, TheTextureCoordinateDoesNotTakeIt)
{
	// The guard on the other side. Under the truncated reciprocal these two
	// heights would put the sampled coordinate in a different sixteenth of a texel
	// on 206 of 221 pixels, so the readout could plainly have seen it; the console
	// moved 0 of 442, and our exact quotient scores 100.00% against silicon on
	// that section. If this test ever fails the fix has spread past its evidence.
	const GSVertexSW a = SetupOfTriangle(1024.0f);
	const GSVertexSW b = SetupOfTriangle(2287.0f);

	EXPECT_EQ(a.t.x, b.t.x);
	EXPECT_EQ(a.t.y, b.t.y);
	EXPECT_EQ(a.t.z, b.t.z);
	EXPECT_FLOAT_EQ(a.t.x, 768.0f / 224.0f);
}

TEST(SwSetupReciprocal, DepthDoesNotTakeIt)
{
	// Same guard, and the stronger one: at these heights the two models differ by
	// 94,607 depth units at the far end of the span against a per-pixel step of
	// 56,173 -- more than a whole pixel. The console moved 0 of 442.
	const GSVertexSW a = SetupOfTriangle(1024.0f);
	const GSVertexSW b = SetupOfTriangle(2287.0f);

	EXPECT_EQ(a.p.F64[1], b.p.F64[1]);
}

#endif // MULTI_ISA_SHARED_COMPILATION

}

namespace SWParity_span_seed {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// The colour interpolator: what the setup decides, and what a span carries.
//
// The GS forms a colour or fog gradient by multiplying a channel delta by a
// reciprocal truncated to eight significant bits, and then truncates the product
// again -- toward zero, to a tenth of a fractional bit of a colour level. It
// walks that gradient from ONE anchor per primitive, on a block grid pinned to
// absolute screen x, and it ramps inside a block on a gradient coarser still.
//
// gs-edge (SCPH-30001, 2026-09-06, two byte-identical launches, 120,080 scored
// readings over a slope sweep, a height and baseline sweep and a second-row
// sweep) measures all of it; gs-shape (2026-09-05) and gs-walk2 hold the same
// model. GSColourWalk.h carries the model itself and what each section of the
// capture decided.
//
// Every expectation below is computed in the test, from the geometry, with its
// own truncated reciprocal and its own truncations -- never by reading the
// renderer's own numbers back.
//
// The rasterizer is compiled per-ISA. This drives the real one, so it needs a
// build with an isa_native -- ARM64, or an x86 build with DISABLE_ADVANCE_SIMD
// off. A multi-ISA x86 build compiles the rasterizer into isa_sse4/isa_avx/
// isa_avx2 and cannot even include the header.


#ifndef MULTI_ISA_SHARED_COMPILATION




namespace
{
	// One run of the rasterizer: what the setup decided, the tables the scanline
	// was handed, and the seed of every row the walk emitted.
	struct SpanRecord
	{
		bool setup_ran = false;
		bool tables_seen = false;
		int tables_top = 0;
		GSVertexSW dscan;
		GSColourWalk cwalk{};
		GSScanlineLocalData local{};
		std::vector<int> top;
		std::vector<int> left;
		std::vector<GSVector4> c;
		std::vector<GSVector4> t;
	};

	SpanRecord g_rec;

	void RecordSetup(const GSVertexSW*, const u16*, const GSVertexSW& dscan, GSScanlineLocalData& local)
	{
		g_rec.setup_ran = true;
		g_rec.dscan = dscan;
		g_rec.cwalk = local.cwalk;
	}

	void RecordSpan(int, int left, int top, const GSVertexSW& scan, GSScanlineLocalData& local)
	{
		// The tables are built between the setup callback and the first span, so
		// this is the first place they can be read. A row builds only the entry its
		// span reads; the copy is completed so every entry of the row is checked.
		if (!g_rec.tables_seen)
		{
			g_rec.local = local;
			isa_native::GSDrawScanline::CompleteColourWalkTables(g_rec.local);
			g_rec.tables_top = top;
			g_rec.tables_seen = true;
		}

		g_rec.top.push_back(top);
		g_rec.left.push_back(left);
		g_rec.c.push_back(scan.c);
		g_rec.t.push_back(scan.t);
	}

	// ⚠️ TFX_NONE is 4, not 0, so a zeroed selector is a MODULATE-textured draw --
	// which walks a FOUR-pixel block. Every case that scores the eight-wide walk
	// has to ask for TFX_NONE by name, and leave fog and AA1 off with it.
	const SpanRecord& Walk(GSVertexSW* v, GS_PRIM_CLASS pc = GS_TRIANGLE_CLASS, int count = 3,
		int tfx = TFX_NONE, int fge = 0, int aa1 = 0)
	{
		static const u16 index[3] = {0, 1, 2};

		g_rec = SpanRecord();

		isa_native::GSRasterizerData data;
		data.primclass = pc;
		data.vertex = v;
		data.vertex_count = count;
		data.index = const_cast<u16*>(index);
		data.index_count = count;
		data.scissor = GSVector4i(0, 0, 640, 640);
		data.bbox = GSVector4i(0, 0, 640, 640);
		data.global.sel.key = 0;
		data.global.sel.iip = 1;
		data.global.sel.tfx = tfx;
		data.global.sel.fge = fge;
		data.global.sel.aa1 = aa1;
		data.setup_prim = &RecordSetup;
		data.draw_scanline = &RecordSpan;
		// nullptr, deliberately. HasEdge() is "is there an edge callback", not
		// "is AA1 on", and a triangle with one runs a SECOND Flush whose dscan is
		// zeroed, which lands on the callback after the real one.
		data.draw_edge = nullptr;

		isa_native::GSRasterizer r(nullptr, 0, 1);
		r.Draw(data);

		EXPECT_TRUE(g_rec.setup_ran) << "the setup callback never ran, so nothing was measured";
		return g_rec;
	}

	void Vertex(GSVertexSW& v, float x, float y, float red, float fog)
	{
		v = GSVertexSW::zero();
		v.p = GSVector4(x, y, 0.0f, 0.0f);
		v.p.F64[1] = 0.0;
		v.c = GSVector4(red, 0.0f, 0.0f, 0.0f);
		v.t = GSVector4(0.0f, 0.0f, 1.0f, fog);
	}

	// Colours ride the pipeline's own 1/128 grid (GSRendererSW builds them as
	// byte << 7), so these are 128 times a colour level. One such unit is the
	// unit every number below is in.
	constexpr float kLevel = 128.0f;

	// The eight-bit truncated reciprocal, as SetupTriangle forms it. Written out
	// here rather than shared, so that a change to the renderer's copy has to be
	// made deliberately in both places.
	float TruncatedReciprocal8(double v)
	{
		double r = 1.0 / v;
		u64 bits;

		std::memcpy(&bits, &r, sizeof(bits));
		bits &= ~((static_cast<u64>(1) << 45) - 1);
		std::memcpy(&r, &bits, sizeof(r));
		return static_cast<float>(r);
	}

	/// Toward zero, to an eighth of a colour unit -- the gradient's own grid.
	float Tz8(float v) { return std::trunc(v * 8.0f) * 0.125f; }

	/// Toward zero, to eight colour units -- the grid the ramp inside a block runs on.
	float TzBlock(float v) { return std::trunc(v * 0.125f) * 8.0f; }

	// The setup's plane arithmetic for one triangle, per channel, written out here.
	struct Plane
	{
		float raw[4];  // dscan.c before the ten-bit truncation
		float g[4];
		float gy[4];
		float gc[4];
		float gyc[4];
		float d8[4];
		float g8[4];
		float fraw, fg, fgy, fgc, fgyc, fd8, fg8;
	};

	Plane PlaneOf(const GSVertexSW* v)
	{
		const float d0x = v[1].p.x - v[0].p.x, d0y = v[1].p.y - v[0].p.y;
		const float d1x = v[2].p.x - v[0].p.x, d1y = v[2].p.y - v[0].p.y;
		const float cross = d0y * d1x - d0x * d1y;
		const float r = TruncatedReciprocal8(static_cast<double>(cross));

		Plane p = {};

		for (int ch = 0; ch < 4; ch++)
		{
			const float da0 = v[1].c.F32[ch] - v[0].c.F32[ch];
			const float da1 = v[2].c.F32[ch] - v[0].c.F32[ch];

			p.raw[ch] = da1 * (d0y * r) - da0 * (d1y * r);
			p.g[ch] = Tz8(p.raw[ch]);
			p.gy[ch] = Tz8(da0 * (d1x * r) - da1 * (d0x * r));
			p.gc[ch] = TzBlock(p.g[ch]);
			p.gyc[ch] = TzBlock(p.gy[ch]);
			p.d8[ch] = (p.g[ch] - p.gc[ch]) * 8.0f;
			p.g8[ch] = p.g[ch] * 8.0f;
		}

		const float df0 = v[1].t.w - v[0].t.w;
		const float df1 = v[2].t.w - v[0].t.w;

		p.fraw = df1 * (d0y * r) - df0 * (d1y * r);
		p.fg = Tz8(p.fraw);
		p.fgy = Tz8(df0 * (d1x * r) - df1 * (d0x * r));
		p.fgc = TzBlock(p.fg);
		p.fgyc = TzBlock(p.fgy);
		p.fd8 = (p.fg - p.fgc) * 8.0f;
		p.fg8 = p.fg * 8.0f;

		return p;
	}

	// One geometry's hand-computed walk, from GSColourWalk.h's rules 3 and 4.
	struct Anchor
	{
		int d;
		int S;
		int A;
		int phase;
		bool top_anchor;
		float xr;
		float yr;
		float ar;   // the anchor's red
		float fr;   // the anchor's fog
	};

	/// Rule 7, evaluated in the test. `bw` is the block width: eight for an
	/// untextured draw, four for a textured one.
	float Seed(const Plane& p, int ch, const Anchor& a, int x, int y, int bw = 8)
	{
		const int yf = a.top_anchor ? (y & ~1) : (y | 1);
		const int j = (a.d * (x - a.S)) >> ((bw == 8) ? 3 : 2);
		const float dw = (p.g[ch] - p.gc[ch]) * static_cast<float>(bw);
		const float P = a.ar + Tz8(p.g[ch] * (static_cast<float>(a.A) - a.xr))
		                + Tz8(p.gy[ch] * (static_cast<float>(yf) - a.yr));

		return std::floor(P + p.gyc[ch] * static_cast<float>(y - yf)
		                  + p.gc[ch] * static_cast<float>(x - a.A)
		                  + dw * static_cast<float>(a.d * j));
	}

	float SeedFog(const Plane& p, const Anchor& a, int x, int y, int bw = 8)
	{
		const int yf = a.top_anchor ? (y & ~1) : (y | 1);
		const int j = (a.d * (x - a.S)) >> ((bw == 8) ? 3 : 2);
		const float dw = (p.fg - p.fgc) * static_cast<float>(bw);
		const float P = a.fr + Tz8(p.fg * (static_cast<float>(a.A) - a.xr))
		                + Tz8(p.fgy * (static_cast<float>(yf) - a.yr));

		return std::floor(P + p.fgyc * static_cast<float>(y - yf)
		                  + p.fgc * static_cast<float>(x - a.A)
		                  + dw * static_cast<float>(a.d * j));
	}

	// The row of `rec` at scanline y, or -1.
	int RowAt(const SpanRecord& rec, int y)
	{
		for (size_t i = 0; i < rec.top.size(); i++)
		{
			if (rec.top[i] == y)
				return static_cast<int>(i);
		}
		return -1;
	}

	// How many pixels one packed table entry covers -- the scanline's vector width.
	constexpr int kLanes = static_cast<int>(sizeof(GSScanlineLocalData::skip::rb) / sizeof(s16)) / 2;

	template <typename T>
	int Lane16(const T& v, int i)
	{
		return static_cast<int>(v.I16[i]);
	}
} // namespace

// ---------------------------------------------------------------------------
// The four subjects. Each one's walk is worked out by hand in the comment and
// asserted in TheAnchorAndGridComeFromTheSpine below.
// ---------------------------------------------------------------------------

namespace
{
	// Middle vertex on the right, spine leaning right as it descends, so the
	// anchor is the spine's TOP end.
	//   spine v0->v2 at y = 50 is x = 113.83; the middle vertex is at 200.5.
	//   d = +1, R = v0, S = ceil(100.5) & ~1 = 100, A = 102, phase = 4.
	void MidRightTriangle(GSVertexSW* v)
	{
		Vertex(v[0], 100.5f, 10.0f, 0.0f, 0.0f);
		Vertex(v[1], 200.5f, 50.0f, 255.0f * kLevel, 255.0f * kLevel);
		Vertex(v[2], 140.5f, 130.0f, 120.0f * kLevel, 60.0f * kLevel);
	}

	constexpr Anchor kMidRight = {1, 100, 102, 4, true, 100.5f, 10.0f, 0.0f, 0.0f};

	// Middle vertex on the right, spine leaning LEFT as it descends, so the
	// anchor is the spine's BOTTOM end and the row pairs are bottom-anchored.
	//   spine v0->v2 at y = 50 is x = 126.42; the middle vertex is at 200.5.
	//   d = +1, R = v2, S = ceil(98.25) & ~1 = 98, A = 100, phase = 2.
	void MidRightFallingTriangle(GSVertexSW* v)
	{
		Vertex(v[0], 140.5f, 10.0f, 0.0f, 0.0f);
		Vertex(v[1], 200.5f, 50.0f, 255.0f * kLevel, 255.0f * kLevel);
		Vertex(v[2], 98.25f, 130.0f, 120.0f * kLevel, 60.0f * kLevel);
	}

	constexpr Anchor kMidRightFalling = {1, 98, 100, 2, false, 98.25f, 130.0f, 120.0f * kLevel, 60.0f * kLevel};

	// Middle vertex on the LEFT, so the walk runs leftward and the anchor is the
	// spine's larger-x end, which here is the top.
	//   spine v0->v2 at y = 50 is x = 193.83; the middle vertex is at 100.5.
	//   d = -1, R = v0, S = floor(200.5) | 1 = 201, A = 199, phase = (201+1)&7 = 2.
	void MidLeftTriangle(GSVertexSW* v)
	{
		Vertex(v[0], 200.5f, 10.0f, 0.0f, 0.0f);
		Vertex(v[1], 100.5f, 50.0f, 255.0f * kLevel, 255.0f * kLevel);
		Vertex(v[2], 180.5f, 130.0f, 120.0f * kLevel, 60.0f * kLevel);
	}

	constexpr Anchor kMidLeft = {-1, 201, 199, 2, true, 200.5f, 10.0f, 0.0f, 0.0f};

	// A flat bottom whose left edge is vertical. Both bottom vertices share a y,
	// so the spine ends at the LEFT one of the two -- which makes the spine the
	// left edge and the middle vertex the bottom-right corner.
	//   d = +1, R = v0, S = ceil(50.5) & ~1 = 50, A = 52, phase = 2.
	void FlatBottomTriangle(GSVertexSW* v)
	{
		Vertex(v[0], 50.5f, 10.0f, 0.0f, 0.0f);
		Vertex(v[1], 50.5f, 90.0f, 120.0f * kLevel, 60.0f * kLevel);
		Vertex(v[2], 150.5f, 90.0f, 255.0f * kLevel, 255.0f * kLevel);
	}

	constexpr Anchor kFlatBottom = {1, 50, 52, 2, true, 50.5f, 10.0f, 0.0f, 0.0f};

	// The same mid-right shape with a VERTICAL left edge, so `left` is the same
	// pixel on every row and a row pair's two seeds can only differ by the
	// vertical step.
	//   d = +1, R = v0, S = 100, A = 102, phase = 4.
	void VerticalEdgeTriangle(GSVertexSW* v)
	{
		Vertex(v[0], 100.5f, 10.0f, 0.0f, 0.0f);
		Vertex(v[1], 200.5f, 50.0f, 255.0f * kLevel, 255.0f * kLevel);
		Vertex(v[2], 100.5f, 130.0f, 120.0f * kLevel, 60.0f * kLevel);
	}

	constexpr Anchor kVerticalEdge = {1, 100, 102, 4, true, 100.5f, 10.0f, 0.0f, 0.0f};
} // namespace

// ---------------------------------------------------------------------------
// 1. The gradient is ten bits.
// ---------------------------------------------------------------------------

TEST(SwColourWalk, TheGradientIsTruncatedToTenBitsOfALevel)
{
	GSVertexSW v[3];
	MidRightTriangle(v);
	const SpanRecord& rec = Walk(v);

	const Plane p = PlaneOf(v);

	// The subject has to have a gradient the truncation can move, or the case
	// decides nothing.
	ASSERT_NE(p.raw[0], p.g[0]) << "red's gradient is already on the 1/8 grid here";
	ASSERT_NE(p.fraw, p.fg) << "fog's gradient is already on the 1/8 grid here";

	const float got_c = rec.dscan.c.x;
	const float got_f = rec.dscan.t.w;

	EXPECT_FLOAT_EQ(got_c * 8.0f, std::trunc(got_c * 8.0f)) << "not a multiple of 1/8 of a unit";
	EXPECT_FLOAT_EQ(got_f * 8.0f, std::trunc(got_f * 8.0f)) << "not a multiple of 1/8 of a unit";

	// Toward zero: never further from zero than the untruncated product, and
	// never more than one step of the grid closer to it.
	EXPECT_LE(std::abs(got_c), std::abs(p.raw[0]));
	EXPECT_LT(std::abs(p.raw[0]) - std::abs(got_c), 0.125f);
	EXPECT_LE(std::abs(got_f), std::abs(p.fraw));
	EXPECT_LT(std::abs(p.fraw) - std::abs(got_f), 0.125f);

	EXPECT_FLOAT_EQ(got_c, p.g[0]);
	EXPECT_FLOAT_EQ(got_f, p.fg);
}

// ---------------------------------------------------------------------------
// 2. The anchor, the direction and the block grid.
// ---------------------------------------------------------------------------

namespace
{
	void ExpectAnchor(const GSColourWalk& w, const Anchor& a, const char* what)
	{
		EXPECT_EQ(w.live, 1) << what;
		EXPECT_EQ(w.c.w, 8) << what
						  << ": untextured with fog and AA1 off walks an eight-pixel block";
		EXPECT_EQ(w.d, a.d) << what << ": walk direction";
		EXPECT_EQ(w.top_anchor, a.top_anchor ? 1 : 0) << what << ": which end of the spine anchors";
		EXPECT_FLOAT_EQ(w.xr, a.xr) << what << ": anchor x";
		EXPECT_FLOAT_EQ(w.yr, a.yr) << what << ": anchor y";
		EXPECT_EQ(w.S, a.S) << what << ": block grid origin";
		EXPECT_EQ(w.A, a.A) << what << ": the pixel that is the plane exactly";
		EXPECT_EQ(w.c.phase, a.phase) << what << ": the eight-pixel phase";
	}
} // namespace

TEST(SwColourWalk, TheAnchorAndGridComeFromTheSpine)
{
	GSVertexSW v[3];

	MidRightTriangle(v);
	ExpectAnchor(Walk(v).cwalk, kMidRight, "middle vertex right, spine leaning right");

	MidRightFallingTriangle(v);
	ExpectAnchor(Walk(v).cwalk, kMidRightFalling, "middle vertex right, spine leaning left");

	MidLeftTriangle(v);
	ExpectAnchor(Walk(v).cwalk, kMidLeft, "middle vertex left");

	FlatBottomTriangle(v);
	ExpectAnchor(Walk(v).cwalk, kFlatBottom, "flat bottom, vertical left edge");
}

// ---------------------------------------------------------------------------
// 3. The row seed.
// ---------------------------------------------------------------------------

TEST(SwColourWalk, ARowPairsSecondRowIsTheFirstPlusTheCoarseVerticalStep)
{
	// A vertical left edge, so `left` is 101 on every row and the only thing that
	// can move between the two rows of a pair is the vertical step.
	GSVertexSW v[3];
	VerticalEdgeTriangle(v);
	const SpanRecord& rec = Walk(v);

	const Plane p = PlaneOf(v);

	ASSERT_NE(p.gyc[0], 0.0f) << "no vertical step here, so the case decides nothing";

	int checked = 0;
	for (int y = 12; y <= 126; y += 6)
	{
		// y and y + 1 share a pair when y is even and the anchor is the top.
		const int i0 = RowAt(rec, y);
		const int i1 = RowAt(rec, y + 1);
		ASSERT_GE(i0, 0) << "row " << y << " was not drawn";
		ASSERT_GE(i1, 0) << "row " << (y + 1) << " was not drawn";
		ASSERT_EQ(rec.left[i0], 101);
		ASSERT_EQ(rec.left[i1], 101);

		EXPECT_FLOAT_EQ(rec.c[i1].x - rec.c[i0].x, p.gyc[0]) << "rows " << y << " and " << (y + 1);
		EXPECT_FLOAT_EQ(rec.t[i1].w - rec.t[i0].w, p.fgyc) << "rows " << y << " and " << (y + 1);
		checked++;
	}
	EXPECT_GT(checked, 8);
}

TEST(SwColourWalk, TheSeedIsThePlaneAtTheAnchorWalkedToTheFirstPixel)
{
	GSVertexSW v[3];
	MidRightTriangle(v);
	const SpanRecord& rec = Walk(v);

	const Plane p = PlaneOf(v);

	int checked = 0;
	for (int y = 11; y <= 128; y += 3)
	{
		const int i = RowAt(rec, y);
		ASSERT_GE(i, 0) << "row " << y << " was not drawn";

		EXPECT_FLOAT_EQ(rec.c[i].x, Seed(p, 0, kMidRight, rec.left[i], y)) << "row " << y;
		EXPECT_FLOAT_EQ(rec.t[i].w, SeedFog(p, kMidRight, rec.left[i], y)) << "row " << y;
		checked++;
	}
	EXPECT_GT(checked, 20);
}

TEST(SwColourWalk, TheSeedIsThePlaneWalkingLeftToo)
{
	GSVertexSW v[3];
	MidLeftTriangle(v);
	const SpanRecord& rec = Walk(v);

	const Plane p = PlaneOf(v);

	int checked = 0;
	for (int y = 11; y <= 128; y += 3)
	{
		const int i = RowAt(rec, y);
		ASSERT_GE(i, 0) << "row " << y << " was not drawn";

		EXPECT_FLOAT_EQ(rec.c[i].x, Seed(p, 0, kMidLeft, rec.left[i], y)) << "row " << y;
		checked++;
	}
	EXPECT_GT(checked, 20);
}

// ---------------------------------------------------------------------------
// 4. The lane tables.
// ---------------------------------------------------------------------------

namespace
{
	// The walk's offset at absolute pixel i, in colour units: the ramp out to it
	// plus the block jump accumulated to its block, floored -- which is what a
	// whole-unit lane can carry, and at a four-pixel block the jump is half a unit
	// so the flooring is what makes successive blocks alternate.
	int OffW(float gc, float g, int S, int d, int bw, int i, float pf)
	{
		const float dw = (g - gc) * static_cast<float>(bw);
		const int b = d * ((d * (i - S)) >> ((bw == 8) ? 3 : 2));

		// ⚠️ The jump is floored WITH the row's own fractional part in it, which is
		// what makes the walk one floor at every pixel rather than a floored seed
		// plus a separately floored jump (gs-cwalk's landing). It is inert wherever
		// dw is whole -- every eight-wide block -- and decisive at four.
		return static_cast<int>(gc) * i
		       + static_cast<int>(std::floor(dw * static_cast<float>(b) + pf));
	}

	/// The fractional part of the row's plane constant, per channel: what OffW's
	/// jump is floored with.
	float RowFrac(const Plane& p, int ch, const Anchor& a, int y)
	{
		const int yf = a.top_anchor ? (y & ~1) : (y | 1);
		const float P = a.ar + Tz8(p.g[ch] * (static_cast<float>(a.A) - a.xr))
		                + Tz8(p.gy[ch] * (static_cast<float>(yf) - a.yr));

		return P - std::floor(P);
	}

	float RowFracFog(const Plane& p, const Anchor& a, int y)
	{
		const int yf = a.top_anchor ? (y & ~1) : (y | 1);
		const float P = a.fr + Tz8(p.fg * (static_cast<float>(a.A) - a.xr))
		                + Tz8(p.fgy * (static_cast<float>(yf) - a.yr));

		return P - std::floor(P);
	}

	/// `bw` is the colour lane's block width and `fbw` the fog lane's. They differ
	/// on exactly one kind of draw -- untextured with fog on.
	void ExpectLaneTables(const SpanRecord& rec, const Plane& p, const Anchor& a, int bw,
		int fbw, const char* what)
	{
		const GSScanlineLocalData& local = rec.local;
		ASSERT_TRUE(rec.tables_seen) << what << ": no span was drawn, so no table was read";

		// The tables follow the row, so the oracle needs the row they were read at.
		const float pf0 = RowFrac(p, 0, a, rec.tables_top);
		const float pf1 = RowFrac(p, 1, a, rec.tables_top);
		const float pf2 = RowFrac(p, 2, a, rec.tables_top);
		const float pf3 = RowFrac(p, 3, a, rec.tables_top);
		const float pff = RowFracFog(p, a, rec.tables_top);

		for (int s = 0; s < 8; s++)
		{
			const int base = s & ~(kLanes - 1);

			for (int l = 0; l < kLanes; l++)
			{
				const int i = base + l;

				const int want_r = static_cast<s16>(
					OffW(p.gc[0], p.g[0], a.S, a.d, bw, i, pf0) - OffW(p.gc[0], p.g[0], a.S, a.d, bw, s, pf0));
				const int want_b = static_cast<s16>(
					OffW(p.gc[2], p.g[2], a.S, a.d, bw, i, pf2) - OffW(p.gc[2], p.g[2], a.S, a.d, bw, s, pf2));
				const int want_g = static_cast<s16>(
					OffW(p.gc[1], p.g[1], a.S, a.d, bw, i, pf1) - OffW(p.gc[1], p.g[1], a.S, a.d, bw, s, pf1));
				const int want_a = static_cast<s16>(
					OffW(p.gc[3], p.g[3], a.S, a.d, bw, i, pf3) - OffW(p.gc[3], p.g[3], a.S, a.d, bw, s, pf3));
				const int want_f = static_cast<s16>(
					OffW(p.fgc, p.fg, a.S, a.d, fbw, i, pff) - OffW(p.fgc, p.fg, a.S, a.d, fbw, s, pff));

				EXPECT_EQ(Lane16(local.d[s].rb, 2 * l), want_r) << what << " d[" << s << "] lane " << l << " red";
				EXPECT_EQ(Lane16(local.d[s].rb, 2 * l + 1), want_b) << what << " d[" << s << "] lane " << l << " blue";
				EXPECT_EQ(Lane16(local.d[s].ga, 2 * l), want_g) << what << " d[" << s << "] lane " << l << " green";
				EXPECT_EQ(Lane16(local.d[s].ga, 2 * l + 1), want_a) << what << " d[" << s << "] lane " << l << " alpha";
				EXPECT_EQ(Lane16(local.d[s].f, 2 * l), want_f) << what << " d[" << s << "] lane " << l << " fog";
			}
		}

#if _M_SSE < 0x501
		// A whole block is one 8g however the two vectors split it, so the two
		// phases of every pair have to sum to it.
		for (int s = 0; s < 8; s++)
		{
			for (int l = 0; l < kLanes; l++)
			{
				EXPECT_EQ(Lane16(local.dw[s][0].rb, 2 * l) + Lane16(local.dw[s][1].rb, 2 * l),
					static_cast<int>(p.g8[0]))
					<< what << " dw[" << s << "] lane " << l << " red";
				EXPECT_EQ(Lane16(local.dw[s][0].ga, 2 * l) + Lane16(local.dw[s][1].ga, 2 * l),
					static_cast<int>(p.g8[1]))
					<< what << " dw[" << s << "] lane " << l << " green";
				EXPECT_EQ(Lane16(local.dw[s][0].f, 2 * l) + Lane16(local.dw[s][1].f, 2 * l),
					static_cast<int>(p.fg8))
					<< what << " dw[" << s << "] lane " << l << " fog";
			}
		}
#endif
	}
} // namespace

TEST(SwColourWalk, TheLaneTablesCarryTheBlocksOwnOffsets)
{
	GSVertexSW v[3];

	MidRightTriangle(v);
	{
		const SpanRecord& rec = Walk(v);
		ASSERT_EQ(rec.cwalk.c.phase, 4);
		ExpectLaneTables(rec, PlaneOf(v), kMidRight, 8, 8, "phase 4");
	}

	FlatBottomTriangle(v);
	{
		const SpanRecord& rec = Walk(v);
		ASSERT_EQ(rec.cwalk.c.phase, 2);
		ExpectLaneTables(rec, PlaneOf(v), kFlatBottom, 8, 8, "phase 2");
	}
}

// ---------------------------------------------------------------------------
// 5. The separation control. A test that cannot tell two anchors apart proves
//    nothing by passing, so the distance between them is asserted directly.
// ---------------------------------------------------------------------------

TEST(SwColourWalk, TheOtherAnchorIsFarEnoughAwayToSee)
{
	GSVertexSW v[3];
	MidLeftTriangle(v);
	const SpanRecord& rec = Walk(v);

	const Plane p = PlaneOf(v);

	// The middle vertex is the anchor the section walk used to take. Evaluating
	// the same plane from there instead of from v0 has to move the seed by more
	// than a colour unit, or nothing above can fail.
	const int y = 60;
	const int i = RowAt(rec, y);
	ASSERT_GE(i, 0);

	const int x = rec.left[i];
	const float from_anchor = Seed(p, 0, kMidLeft, x, y);
	const float from_middle = v[1].c.x + p.g[0] * (static_cast<float>(x) - v[1].p.x)
	                          + p.gy[0] * (static_cast<float>(y) - v[1].p.y);

	EXPECT_GT(std::abs(from_anchor - from_middle), 1.0f)
		<< "the two anchors are within a colour unit of each other here, so this "
		   "triangle cannot separate them";
}

// ---------------------------------------------------------------------------
// 5b. The block is eight pixels wide untextured and FOUR textured, and nothing
//     else about the walk moves with it.
//
//     gs-walk2's width section draws the same geometry with TME off and on: at
//     eight its untextured arm reads 100.000% and its textured arm 91.181%, and
//     at four the two swap exactly. gs-shade's CDDA reference cell -- a textured
//     draw at the texture function's identity, so the stored pixel IS the
//     interpolated colour -- reads 94.531% at eight and 100.000% at four.
//
//     Both cases below draw the SAME triangle. Only the texture function differs.
// ---------------------------------------------------------------------------

TEST(SwColourWalk, ATexturedTriangleWalksFourPixelBlocks)
{
	GSVertexSW v[3];
	FlatBottomTriangle(v);
	const SpanRecord& rec = Walk(v, GS_TRIANGLE_CLASS, 3, TFX_MODULATE);

	const Plane p = PlaneOf(v);

	EXPECT_EQ(rec.cwalk.c.w, 4) << "a textured draw walks a four-pixel block";
	EXPECT_EQ(rec.cwalk.c.wshift, 2);
	// Everything else about the walk is the geometry's, unchanged by the width.
	EXPECT_EQ(rec.cwalk.d, kFlatBottom.d);
	EXPECT_EQ(rec.cwalk.S, kFlatBottom.S);
	EXPECT_EQ(rec.cwalk.A, kFlatBottom.A);
	EXPECT_EQ(rec.cwalk.c.phase, kFlatBottom.S & 3);
	EXPECT_FLOAT_EQ(rec.dscan.c.x, p.g[0]) << "the ten-bit gradient is the same at either width";

	int checked = 0;
	for (int y = 11; y <= 89; y += 3)
	{
		const int i = RowAt(rec, y);
		ASSERT_GE(i, 0) << "row " << y << " was not drawn";

		EXPECT_FLOAT_EQ(rec.c[i].x, Seed(p, 0, kFlatBottom, rec.left[i], y, 4)) << "row " << y;
		EXPECT_FLOAT_EQ(rec.t[i].w, SeedFog(p, kFlatBottom, rec.left[i], y, 4)) << "row " << y;
		checked++;
	}
	EXPECT_GT(checked, 20);

	// The width lives in the lane tables, which is where it is scored. The seed
	// alone cannot separate the two: this subject's left edge is vertical and its
	// first pixel is in the anchor's own block at either width.
	ExpectLaneTables(rec, p, kFlatBottom, 4, 4, "textured, four-pixel block");

	// And a test that cannot tell four from eight proves nothing by passing.
	int separated = 0;
	for (int i = 0; i < 8; i++)
	{
		const float pf = RowFrac(p, 0, kFlatBottom, rec.tables_top);

		if (OffW(p.gc[0], p.g[0], kFlatBottom.S, kFlatBottom.d, 4, i, pf)
			!= OffW(p.gc[0], p.g[0], kFlatBottom.S, kFlatBottom.d, 8, i, pf))
		{
			separated++;
		}
	}
	EXPECT_GT(separated, 0) << "the two widths give the same offsets on this subject, "
	                           "so it cannot tell them apart";
}

namespace
{
	// One of the three narrowing factors, on the same triangle, with everything
	// else off. Each narrows the block on its own, both lanes together; `fbw` is
	// carried separately only so that a future factor that split them would have
	// somewhere to say so.
	void ExpectNarrowedByOneFactor(const SpanRecord& rec, const Plane& p, int fbw, const char* what)
	{
		EXPECT_EQ(rec.cwalk.c.w, 4) << what << " narrows the colour block on its own";
		EXPECT_EQ(rec.cwalk.c.wshift, 2) << what;
		EXPECT_EQ(rec.cwalk.f.w, fbw) << what << ": the fog lane's own width";
		// The factor moves the width and nothing else about the walk.
		EXPECT_EQ(rec.cwalk.d, kFlatBottom.d) << what;
		EXPECT_EQ(rec.cwalk.S, kFlatBottom.S) << what;
		EXPECT_EQ(rec.cwalk.A, kFlatBottom.A) << what;
		EXPECT_EQ(rec.cwalk.c.phase, kFlatBottom.S & 3) << what;

		int checked = 0;
		for (int y = 11; y <= 89; y += 7)
		{
			const int i = RowAt(rec, y);
			ASSERT_GE(i, 0) << what << ": row " << y << " was not drawn";

			EXPECT_FLOAT_EQ(rec.c[i].x, Seed(p, 0, kFlatBottom, rec.left[i], y, 4))
				<< what << ": row " << y;
			EXPECT_FLOAT_EQ(rec.t[i].w, SeedFog(p, kFlatBottom, rec.left[i], y, fbw))
				<< what << ": row " << y;
			checked++;
		}
		EXPECT_GT(checked, 8);

		ExpectLaneTables(rec, p, kFlatBottom, 4, fbw, what);
	}
} // namespace

// gs-pipe (SCPH-30001, 2026-09-07) swept the pipeline one factor at a time against
// an untextured base. Fog and AA1 each narrow the COLOUR block to four on their
// own, and blending, a depth test with depth writes and the target format do not.
// These are the two that do.

TEST(SwColourWalk, FogNarrowsBothLanesOnAnUntexturedDraw)
{
	// gs-pipe's fog cell holds F at 0xff on every vertex, so it has no fog gradient
	// and could only say what FGE does to the COLOUR lane. gs-fog (2026-09-07)
	// walks F itself: Model7 on F at width four, 100.000% over 115,040 readings on
	// all three sections, with texture or AA1 on top moving nothing. Fog is not a
	// second interpolator, so both lanes narrow together.
	GSVertexSW v[3];
	FlatBottomTriangle(v);
	const SpanRecord& rec = Walk(v, GS_TRIANGLE_CLASS, 3, TFX_NONE, 1, 0);

	ExpectNarrowedByOneFactor(rec, PlaneOf(v), 4, "fog");
}

TEST(SwColourWalk, AA1NarrowsBothLanesOnAnUntexturedDraw)
{
	// Nothing separates the two lanes under AA1, so it narrows both.
	GSVertexSW v[3];
	FlatBottomTriangle(v);
	const SpanRecord& rec = Walk(v, GS_TRIANGLE_CLASS, 3, TFX_NONE, 0, 1);

	ExpectNarrowedByOneFactor(rec, PlaneOf(v), 4, "AA1");
}

TEST(SwColourWalk, TheSameTriangleUntexturedStillWalksEight)
{
	GSVertexSW v[3];
	FlatBottomTriangle(v);
	const SpanRecord& rec = Walk(v);

	const Plane p = PlaneOf(v);

	EXPECT_EQ(rec.cwalk.c.w, 8)
		<< "an untextured draw with fog and AA1 off walks an eight-pixel block";
	EXPECT_EQ(rec.cwalk.c.wshift, 3);
	EXPECT_EQ(rec.cwalk.c.phase, kFlatBottom.phase);

	int checked = 0;
	for (int y = 11; y <= 89; y += 3)
	{
		const int i = RowAt(rec, y);
		ASSERT_GE(i, 0) << "row " << y << " was not drawn";

		EXPECT_FLOAT_EQ(rec.c[i].x, Seed(p, 0, kFlatBottom, rec.left[i], y)) << "row " << y;
		checked++;
	}
	EXPECT_GT(checked, 20);

	ExpectLaneTables(rec, p, kFlatBottom, 8, 8, "untextured, eight-pixel block");
}

// ---------------------------------------------------------------------------
// 6. A primitive with no walk leaves every colour table entry zero.
// ---------------------------------------------------------------------------

namespace
{
	void ExpectZeroTables(const SpanRecord& rec, const char* what)
	{
		ASSERT_TRUE(rec.tables_seen) << what << ": no span was drawn, so no table was read";
		const GSScanlineLocalData& local = rec.local;

		for (int s = 0; s < 8; s++)
		{
			for (int k = 0; k < 2 * kLanes; k++)
			{
				EXPECT_EQ(Lane16(local.d[s].rb, k), 0) << what << " d[" << s << "].rb[" << k << "]";
				EXPECT_EQ(Lane16(local.d[s].ga, k), 0) << what << " d[" << s << "].ga[" << k << "]";
				EXPECT_EQ(Lane16(local.d[s].f, k), 0) << what << " d[" << s << "].f[" << k << "]";
			}
		}

#if _M_SSE < 0x501
		for (int s = 0; s < 8; s++)
		{
			for (int ph = 0; ph < 2; ph++)
			{
				for (int k = 0; k < 2 * kLanes; k++)
				{
					EXPECT_EQ(Lane16(local.dw[s][ph].rb, k), 0) << what << " dw";
					EXPECT_EQ(Lane16(local.dw[s][ph].ga, k), 0) << what << " dw";
					EXPECT_EQ(Lane16(local.dw[s][ph].f, k), 0) << what << " dw";
				}
			}
		}
		for (int k = 0; k < 8; k++)
		{
			EXPECT_EQ(Lane16(local.d4.c, k), 0) << what << " d4.c";
			EXPECT_EQ(Lane16(local.d4.f, k), 0) << what << " d4.f";
		}
#else
		EXPECT_EQ(local.d8.c.rb, 0u) << what;
		EXPECT_EQ(local.d8.c.ga, 0u) << what;
		EXPECT_EQ(local.d8.p.f, 0u) << what;
#endif
	}
} // namespace

TEST(SwColourWalk, AFlatTriangleWalksNothing)
{
	GSVertexSW v[3];
	Vertex(v[0], 100.5f, 10.0f, 40.0f * kLevel, 20.0f * kLevel);
	Vertex(v[1], 200.5f, 50.0f, 40.0f * kLevel, 20.0f * kLevel);
	Vertex(v[2], 140.5f, 130.0f, 40.0f * kLevel, 20.0f * kLevel);

	ExpectZeroTables(Walk(v), "flat triangle");
}

TEST(SwColourWalk, ALineWalksNothing)
{
	GSVertexSW v[2];
	Vertex(v[0], 40.0f, 40.0f, 0.0f, 0.0f);
	Vertex(v[1], 200.0f, 90.0f, 255.0f * kLevel, 255.0f * kLevel);

	ExpectZeroTables(Walk(v, GS_LINE_CLASS, 2), "line");
}

TEST(SwColourWalk, ASpriteWalksNothing)
{
	GSVertexSW v[2];
	Vertex(v[0], 40.0f, 40.0f, 0.0f, 0.0f);
	Vertex(v[1], 200.0f, 90.0f, 255.0f * kLevel, 255.0f * kLevel);
	v[0].t = GSVector4(0.0f, 0.0f, 1.0f, 0.0f);
	v[1].t = GSVector4(64.0f, 32.0f, 1.0f, 0.0f);

	ExpectZeroTables(Walk(v, GS_SPRITE_CLASS, 2), "sprite");
}

#endif // MULTI_ISA_SHARED_COMPILATION

}

namespace SWParity_textured_block_width {
// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

// The block a TEXTURED draw interpolates is eight pixels wide, like every other
// draw's, and on a four-lane host that is two vectors.
//
// gs_sw_scanline_cpath_tests.cpp already pins the eight-wide walk for an
// UNTEXTURED draw. This suite is the textured half, and it exists because the two
// were different for a year: the width was read as eight without texture mapping
// and four with it, and only the untextured half was ever measured.
//
// What measured it: with the colour gradient formed the way silicon forms it, the
// gs-walk2 capture's width section splits by TME into an untextured arm that
// agrees with the console on 93.0% and a textured arm on 92.6%, and the same model
// scored at eight puts the textured arm at 96.9%. The console prefers eight on both
// arms. The four we shipped came from a document rather than a measurement, and
// the two errors -- the wrong gradient and the wrong width -- were partly
// cancelling on textured draws, so correcting the gradient is what exposed it.
//
// The draw here is MODULATE against a single texel of 0x80808080, which is the
// texture function's identity: the stored pixel is the interpolated vertex colour
// byte and nothing else, so the block walk is read directly out of the
// framebuffer. The coordinate is held still, so the colour walk is the only thing
// moving.
//
// Rides ARCH_ARM64 like its siblings: the generators are per-architecture.


#ifdef ARCH_ARM64




#ifndef _WIN32
#else
#endif

namespace
{
using DrawScanlinePtr = void (*)(int pixels, int left, int top, const GSVertexSW& scan, GSScanlineLocalData& local);
using SetupPrimPtr = void (*)(const GSVertexSW* vertex, const u16* index, const GSVertexSW& dscan, GSScanlineLocalData& local);

// The scanline carries a colour multiplied by 128: seven fractional bits under the
// byte the GS stores.
constexpr float kColorScale = 128.0f;

// MODULATE's identity texel. (c * 0x80) >> 7 == c for every c in 0..255.
constexpr u32 kIdentityTexel = 0x80808080u;

constexpr int kRowPixels = 64;

struct Span
{
	float r0, g0, b0, a0;
	float dr, dg, db, da;
};

struct Reading
{
	u32 px[kRowPixels];
	GSVector4i d4c;
	GSVector4 d4stq;
	GSScanlineLocalData::blockstep dw[8][2];
};

// The block phase these tests drive the walk at, where nothing sweeps it.
constexpr int kPhase = 3;

// This suite draws textured, so its block is four pixels wide -- GSBlockWalk.h.
constexpr int kWidth = 4;

// The walk the scanline rides. The rasterizer derives one per primitive from the
// geometry (GSColourWalk.h) and hands it over in GSScanlineLocalData::cwalk; these
// tests drive the scanline directly, so they build one from the gradient and a
// chosen block phase and hand it over the same way.
void MakeWalk(GSScanlineLocalData& local, const GSVertexSW& dscan, int phase)
{
	// This suite's selector is MODULATE, so its block is FOUR pixels wide.
	constexpr int kWidth = 4;

	GSColourWalk& w = local.cwalk;

	w = {};
	w.d = 1;
	w.top_anchor = 1;
	w.S = phase;
	w.A = phase + 2;
	w.live = 1;

	GSColourWalkGradientInit(w.c, GSColourWalkTruncUnit(dscan.c), GSVector4::zero(),
		GSVector4::zero(), GSVector4::zero(), kWidth, w.S, w.d);
	GSColourWalkGradientInit(w.f, GSColourWalkTruncUnit(dscan.t.wwww()), GSVector4::zero(),
		GSVector4::zero(), GSVector4::zero(), kWidth, w.S, w.d);
}

// The rule, stated once and independently of either transcription of it.
//
// A draw interpolates a w-pixel block at a time, and the blocks are pinned to
// absolute screen x. Inside a block the value ramps by gc, the gradient truncated
// to a multiple of eight colour units, and makes the rest up in one jump of
// dw = w*(g - gc) at the block boundary; w of those ramps and one jump is exactly
// w*g. So a pixel's value is the truncated seed, plus the ramp out to it, plus
// the jumps of every block boundary between it and the seed's block.
//
// At w = 4 that jump is half a colour unit, which the scanline's whole-unit lanes
// cannot hold, so what it actually carries is the jump ACCUMULATED and then
// truncated -- floor(dw*b) for the block index b, whose increments alternate
// between floor(dw) and ceil(dw) and whose pairs sum to 2*dw exactly. This models
// that, because that is what the renderer can do; GSColourWalk.h says what the
// remaining half unit costs against the console.
//
// w is a parameter so that the separation control below can ask what the OTHER
// width would have said; every measurement here is at four, because these draws
// are textured.
int WalkModel(float c0, float dc, int phase, int x0, int x, int w = kWidth)
{
	const float fw = static_cast<float>(w);
	const float g = std::trunc(dc * kColorScale * 8.0f) * 0.125f;
	// The ramp's grid is eight colour units at either width -- only the block
	// period and the jump that closes it move with w.
	const float gc = std::trunc(g * 0.125f) * 8.0f;
	const float dw = (g - gc) * fw;

	// How many whole blocks a pixel sits from the grid origin the harness set,
	// floor-divided. The origin is the walk's S, not S reduced into one block:
	// the truncation below anchors on the block's parity, so a constant offset of
	// one block is not a constant at all.
	const auto blk = [phase, w](int q) {
		const int n = q - phase;
		return (n >= 0) ? (n / w) : -((-n + w - 1) / w);
	};
	// FLOOR, not truncation toward zero: a block index behind the grid's origin is
	// negative, and toward zero the alternation breaks at the sign change.
	const auto jump = [dw](int b) { return static_cast<int>(std::floor(dw * static_cast<float>(b))); };

	const int seed = static_cast<int>(c0 * kColorScale);

	return (seed + static_cast<int>(gc) * (x - x0) + jump(blk(x)) - jump(blk(x0))) >> 7;
}

/// What EIGHT pixels advance by, in the packed table's low 16 bits -- two blocks
/// at this suite's width, and what the two phases of a step pair have to sum to
/// however the vectors divide them.
u32 BlockStep(float dc)
{
	const float g = std::trunc(dc * kColorScale * 8.0f) * 0.125f;
	return static_cast<u32>(static_cast<int>(g * 8.0f)) & 0xffff;
}

class SwTexturedBlockWidthTest : public SWParityTestBase
{
protected:
	static constexpr size_t kCodeSlotSize = 16 * 1024;
	static constexpr size_t kCodeBufferSize = 8 * kCodeSlotSize;

	static void SetUpTestSuite()
	{
#ifdef _WIN32
		s_code = static_cast<u8*>(VirtualAlloc(nullptr, kCodeBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#elif defined(__APPLE__)
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#else
		s_code = static_cast<u8*>(mmap(nullptr, kCodeBufferSize, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
		if (s_code == MAP_FAILED)
			s_code = nullptr;
#endif
		s_code_used = 0;
		s_mem = new GSLocalMemory();
	}

	static void TearDownTestSuite()
	{
		delete s_mem;
		s_mem = nullptr;

		if (s_code)
		{
#ifdef _WIN32
			VirtualFree(s_code, 0, MEM_RELEASE);
#else
			munmap(s_code, kCodeBufferSize);
#endif
			s_code = nullptr;
		}
	}

	static u8* Slot()
	{
		if (!s_code || s_code_used + kCodeSlotSize > kCodeBufferSize)
			return nullptr;

		u8* slot = s_code + s_code_used;
		s_code_used += kCodeSlotSize;
		return slot;
	}

	static DrawScanlinePtr CompileScanline(GSScanlineSelector sel)
	{
		u8* slot = Slot();
		if (!slot)
			return nullptr;

		HostSys::BeginCodeWrite();
		GSDrawScanlineCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<DrawScanlinePtr>(const_cast<u8*>(cg.GetCode()));
	}

	// The reduced key GetScanlineGlobalData builds, so this is the same variant a
	// real draw would compile.
	static SetupPrimPtr CompileSetup(GSScanlineSelector full)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.iip = full.iip;
		sel.tfx = full.tfx;
		sel.tcc = full.tcc;
		sel.fst = full.fst;
		sel.fge = full.fge;
		sel.prim = full.prim;
		sel.fb = full.fb;
		sel.zb = full.zb;
		sel.zoverflow = full.zoverflow;
		sel.zequal = full.zequal;
		sel.notest = full.notest;

		u8* slot = Slot();
		if (!slot)
			return nullptr;

		HostSys::BeginCodeWrite();
		GSSetupPrimCodeGenerator cg(sel.key, slot, kCodeSlotSize);
		cg.Generate();
		HostSys::EndCodeWrite();
		HostSys::FlushInstructionCache(slot, static_cast<u32>(cg.GetSize()));

		return reinterpret_cast<SetupPrimPtr>(const_cast<u8*>(cg.GetCode()));
	}

	// A gouraud, MODULATE-textured triangle into PSMCT32, nearest-filtered and
	// unblended, with the coordinate clamped to one texel so nothing about
	// addressing or filtering reaches the framebuffer.
	static GSScanlineSelector MakeSelector(bool notest)
	{
		GSScanlineSelector sel;
		sel.key = 0;
		sel.fpsm = 0;
		sel.zpsm = 3;
		sel.atst = ATST_ALWAYS;
		sel.tfx = TFX_MODULATE;
		sel.tcc = 1;
		sel.fst = 1;
		sel.ltf = 0;
		sel.tlu = 0;
		sel.tw = 0;
		sel.ababcd = 0xff;
		sel.prim = GS_TRIANGLE_CLASS;
		sel.iip = 1;
		sel.fwrite = 1;
		sel.notest = notest ? 1 : 0;
		sel.colclamp = 1;
		return sel;
	}

	static const GSPixelOffset4* GetOffsets()
	{
		GIFRegFRAME frame;
		frame.U64 = 0;
		frame.FBP = 0;
		frame.FBW = 1;
		frame.PSM = PSMCT32;

		GIFRegZBUF zbuf;
		zbuf.U64 = 0;
		zbuf.ZBP = 256;
		zbuf.PSM = PSMZ32;

		return s_mem->GetPixelOffset4(frame, zbuf);
	}

	static u32 PixelAddr(int x, int y)
	{
		return GSLocalMemory::m_psm[PSMCT32].info.pa(x, y, 0, 1);
	}

	static void Run(GSScanlineSelector sel, SetupPrimPtr setup, DrawScanlinePtr draw,
		const Span& span, const GSVector4& dt, int left, int pixels, Reading& out, int phase)
	{
		u32* vm32 = s_mem->vm32();
		for (int x = 0; x < kRowPixels; x++)
			vm32[PixelAddr(x, 0)] = 0u;

		alignas(32) GSScanlineGlobalData global{};
		alignas(32) GSScanlineLocalData local = {};
		alignas(32) u32 tex[4] = {kIdentityTexel, kIdentityTexel, kIdentityTexel, kIdentityTexel};

		global.sel = sel;
		global.vm = s_mem->vm8();
		global.fzbr = GetOffsets()->row;
		global.fzbc = GetOffsets()->col;
		global.fm = GSVector4i(0);
		global.zm = GSVector4i(static_cast<int>(0xffffffffu));
		global.tex[0] = tex;
		// min = max = 0 with an all-zero mask is CLAMP to texel zero on both axes,
		// so every lane fetches tex[0] whatever the coordinate is.
		global.t.min = GSVector4i::zero();
		global.t.max = GSVector4i::zero();
		global.t.mask = GSVector4i::zero();

		local.gd = &global;

		GSVertexSW vertex[3];
		u16 index[3] = {0, 1, 2};
		for (int i = 0; i < 3; i++)
			vertex[i] = GSVertexSW::zero();

		GSVertexSW dscan = GSVertexSW::zero();
		dscan.c = GSVector4(span.dr, span.dg, span.db, span.da) * kColorScale;
		dscan.t = dt;

		setup(vertex, index, dscan, local);

		// The colour tables are no longer the setup's to build: the rasterizer
		// builds them from the primitive's walk right after calling it
		// (GSColourWalk.h). These tests drive the scanline directly, so they hand
		// over a walk of their own and build the tables the same way.
		MakeWalk(local, dscan, phase);
		isa_native::GSDrawScanline::SetupColourWalkTables(local, 0, 0);
		isa_native::GSDrawScanline::CompleteColourWalkTables(local);

		out.d4c = local.d4.c;
		out.d4stq = local.d4.stq;
		std::memcpy(out.dw, local.dw, sizeof(out.dw));

		GSVertexSW scan = GSVertexSW::zero();
		scan.c = GSVector4(span.r0, span.g0, span.b0, span.a0) * kColorScale;

		draw(pixels, left, 0, scan, local);

		for (int x = 0; x < kRowPixels; x++)
			out.px[x] = vm32[PixelAddr(x, 0)];
	}

	static bool RunJit(bool notest, const Span& span, const GSVector4& dt, int left, int pixels, Reading& out, int phase = kPhase)
	{
		const int slot = notest ? 1 : 0;
		if (!s_setup[slot])
		{
			s_sel[slot] = MakeSelector(notest);
			s_setup[slot] = CompileSetup(s_sel[slot]);
			s_draw[slot] = CompileScanline(s_sel[slot]);
		}

		if (!s_setup[slot] || !s_draw[slot])
			return false;

		Run(s_sel[slot], s_setup[slot], s_draw[slot], span, dt, left, pixels, out, phase);
		return true;
	}

	static void RunCpp(bool notest, const Span& span, const GSVector4& dt, int left, int pixels, Reading& out, int phase = kPhase)
	{
		Run(MakeSelector(notest), &isa_native::GSDrawScanline::CSetupPrim,
			static_cast<DrawScanlinePtr>(&isa_native::GSDrawScanline::CDrawScanline),
			span, dt, left, pixels, out, phase);
	}

	static GSScanlineSelector s_sel[2];
	static SetupPrimPtr s_setup[2];
	static DrawScanlinePtr s_draw[2];

	static u8* s_code;
	static size_t s_code_used;
	static GSLocalMemory* s_mem;
};

GSScanlineSelector SwTexturedBlockWidthTest::s_sel[2] = {};
SetupPrimPtr SwTexturedBlockWidthTest::s_setup[2] = {nullptr, nullptr};
DrawScanlinePtr SwTexturedBlockWidthTest::s_draw[2] = {nullptr, nullptr};
u8* SwTexturedBlockWidthTest::s_code = nullptr;
size_t SwTexturedBlockWidthTest::s_code_used = 0;
GSLocalMemory* SwTexturedBlockWidthTest::s_mem = nullptr;

// Gradients deliberately off any binade, so a whole-block step is not accidentally
// eight lane steps and the truncation has something to lose. This is the span the
// two widths disagree on.
constexpr Span kSeparatingSpan = {40.0f, 90.0f, 60.0f, 120.0f, 1.3f, -0.7f, 2.1f, 0.55f};

// Gradients that are whole numbers on the scanline's own 2^-7 grid. There the
// truncation is the identity, trunc(g*8) is exactly two trunc(g*4)s, and the two
// widths are the same walk.
constexpr Span kAgreeingSpan = {40.0f, 90.0f, 60.0f, 120.0f, 1.0f, -2.0f, 3.0f, 0.5f};

// The separation, checked before anything is scored against it: a test that cannot
// tell four from eight proves nothing when it passes. This is a property of the
// model alone, so it holds whatever the renderer does. gs-walk2 makes the same
// check on the console: 1,888 of its 21,408 textured RGB readings per set
// separate the two widths, and they pick four.
TEST_F(SwTexturedBlockWidthTest, TheSeparatingSpanReallySeparatesFourFromEight)
{
	int differing = 0;

	for (int left = 0; left < 8; left++)
	{
		for (int x = left; x < 40; x++)
		{
			if (WalkModel(kSeparatingSpan.r0, kSeparatingSpan.dr, kPhase, left, x, 4)
				!= WalkModel(kSeparatingSpan.r0, kSeparatingSpan.dr, kPhase, left, x, 8))
			{
				differing++;
			}
		}
	}

	EXPECT_GT(differing, 0) << "the separating span does not separate the two widths";
}

// The rule. A textured draw's block is FOUR pixels wide -- one vector on this
// host, so its alternating pair comes out a pair of equal steps.
TEST_F(SwTexturedBlockWidthTest, ATexturedGouraudSpanWalksInFourPixelBlocks)
{
	// Every phase, because the coarse ramp's one jump sits at the phase and a
	// single phase would hide where it lands.
	for (int phase = 0; phase < 8; phase++)
	{
		for (int left = 0; left < 8; left++)
		{
			Reading jit;
			ASSERT_TRUE(RunJit(false, kSeparatingSpan, GSVector4::zero(), left, 40 - left, jit, phase));

			for (int x = left; x < 40; x++)
			{


				EXPECT_EQ(static_cast<int>(jit.px[x] & 0xff),
					WalkModel(kSeparatingSpan.r0, kSeparatingSpan.dr, phase, left, x)) << "red";
				EXPECT_EQ(static_cast<int>((jit.px[x] >> 8) & 0xff),
					WalkModel(kSeparatingSpan.g0, kSeparatingSpan.dg, phase, left, x)) << "green";
				EXPECT_EQ(static_cast<int>((jit.px[x] >> 16) & 0xff),
					WalkModel(kSeparatingSpan.b0, kSeparatingSpan.db, phase, left, x)) << "blue";
				EXPECT_EQ(static_cast<int>((jit.px[x] >> 24) & 0xff),
					WalkModel(kSeparatingSpan.a0, kSeparatingSpan.da, phase, left, x)) << "alpha";
			}
		}
	}
}

// The control that has to hold on both sides of the change: where the truncation is
// the identity the two widths are one walk, so this span answers the same whatever
// the width is. If it ever moves, something other than the width did.
TEST_F(SwTexturedBlockWidthTest, AGradientOnTheGridIsTheSameWalkAtEitherWidth)
{
	for (int left = 0; left < 8; left++)
	{
		Reading jit;
		ASSERT_TRUE(RunJit(false, kAgreeingSpan, GSVector4::zero(), left, 40 - left, jit));

		for (int x = left; x < 40; x++)
		{


			ASSERT_EQ(WalkModel(kAgreeingSpan.r0, kAgreeingSpan.dr, kPhase, left, x, 4),
				WalkModel(kAgreeingSpan.r0, kAgreeingSpan.dr, kPhase, left, x, 8));

			EXPECT_EQ(static_cast<int>(jit.px[x] & 0xff),
				WalkModel(kAgreeingSpan.r0, kAgreeingSpan.dr, kPhase, left, x)) << "red";
			EXPECT_EQ(static_cast<int>((jit.px[x] >> 8) & 0xff),
				WalkModel(kAgreeingSpan.g0, kAgreeingSpan.dg, kPhase, left, x)) << "green";
			EXPECT_EQ(static_cast<int>((jit.px[x] >> 16) & 0xff),
				WalkModel(kAgreeingSpan.b0, kAgreeingSpan.db, kPhase, left, x)) << "blue";
			EXPECT_EQ(static_cast<int>((jit.px[x] >> 24) & 0xff),
				WalkModel(kAgreeingSpan.a0, kAgreeingSpan.da, kPhase, left, x)) << "alpha";
		}
	}
}

// The two halves of a split block advance by exactly one whole block step between
// them. If this fails the walk drifts a little every block, which is the error that
// only shows up hundreds of pixels along a span.
TEST_F(SwTexturedBlockWidthTest, TheTwoPhasesOfATexturedStepSumToTheBlockStep)
{
	static const float kSteps[] = {-85.0f, -17.0f, -1.3f, 0.0f, 0.55f, 17.0f, 85.0f};

	for (float dr : kSteps)
	{
		const Span span = {128.0f, 128.0f, 128.0f, 128.0f, dr, -dr, dr * 0.5f, -dr * 0.25f};

		Reading jit;
		ASSERT_TRUE(RunJit(false, span, GSVector4::zero(), 0, 32, jit));

		for (int s = 0; s < 8; s++)
		{
			for (int lane = 0; lane < 4; lane++)
			{
				// Two 16-bit channels share each word, so each is summed on its own
				// rather than letting one carry into its neighbour.
				const u32 rb0 = jit.dw[s][0].rb.U32[lane], rb1 = jit.dw[s][1].rb.U32[lane];
				const u32 ga0 = jit.dw[s][0].ga.U32[lane], ga1 = jit.dw[s][1].ga.U32[lane];


				EXPECT_EQ((rb0 + rb1) & 0xffff, BlockStep(span.dr)) << "red";
				EXPECT_EQ(((rb0 >> 16) + (rb1 >> 16)) & 0xffff, BlockStep(span.db)) << "blue";
				EXPECT_EQ((ga0 + ga1) & 0xffff, BlockStep(span.dg)) << "green";
				EXPECT_EQ(((ga0 >> 16) + (ga1 >> 16)) & 0xffff, BlockStep(span.da)) << "alpha";
			}
		}
	}
}

// The coordinate is not the colour. Its step is what the scanline adds once per
// VECTOR, and widening the colour's block must not widen that -- a coordinate
// stepping a whole block every four pixels would sample the texture twice as fast
// as the draw walks. Pinned on the setup's own output because it is the quantity
// the mistake would land in.
TEST_F(SwTexturedBlockWidthTest, TheCoordinateStepStaysOneVector)
{
	const Span span = {40.0f, 90.0f, 60.0f, 120.0f, 1.3f, -0.7f, 2.1f, 0.55f};
	const GSVector4 dt = GSVector4(2000.0f, -1500.0f, 0.0f, 0.0f);

	Reading jit;
	ASSERT_TRUE(RunJit(false, span, dt, 0, 32, jit));

	// The host vector this build walks with, in lanes.
	constexpr int vlen = static_cast<int>(sizeof(GSVector4) / sizeof(float));
	const GSVector4i step = GSVector4i::cast(jit.d4stq);

	EXPECT_EQ(step.I32[0], static_cast<int>(dt.x) * vlen) << "u";
	EXPECT_EQ(step.I32[1], static_cast<int>(dt.y) * vlen) << "v";
}

// The C++ reference and the generated scanline are two transcriptions of one
// design, and this project has had them drift. Both walk the same textured span.
TEST_F(SwTexturedBlockWidthTest, TheTwoPathsAgreeOnEveryTexturedBlockStart)
{
	for (int left = 0; left < 8; left++)
	{
		Reading jit, cpp;
		ASSERT_TRUE(RunJit(false, kSeparatingSpan, GSVector4::zero(), left, 32, jit));
		RunCpp(false, kSeparatingSpan, GSVector4::zero(), left, 32, cpp);

		for (int x = 0; x < kRowPixels; x++)
			EXPECT_EQ(cpp.px[x], jit.px[x]) << "block start " << left << ": pixel " << x;

		for (int s = 0; s < 8; s++)
		{
			for (int ph = 0; ph < 2; ph++)
			{
				for (int lane = 0; lane < 4; lane++)
				{
					EXPECT_EQ(cpp.dw[s][ph].rb.U32[lane], jit.dw[s][ph].rb.U32[lane])
						<< "block step dw[" << s << "][" << ph << "].rb lane " << lane;
					EXPECT_EQ(cpp.dw[s][ph].ga.U32[lane], jit.dw[s][ph].ga.U32[lane])
						<< "block step dw[" << s << "][" << ph << "].ga lane " << lane;
				}
			}
		}
	}
}

} // namespace

#endif // ARCH_ARM64

}
#undef TEST
#undef TEST_F
#undef SWPARITY_TEST
#undef SWEXPECT
#undef EXPECT_EQ
#undef EXPECT_NE
#undef EXPECT_LT
#undef EXPECT_GT
#undef EXPECT_LE
#undef EXPECT_GE
#undef EXPECT_TRUE
#undef EXPECT_FLOAT_EQ
#undef ASSERT_EQ
#undef ASSERT_NE
#undef ASSERT_GT
#undef ASSERT_GE
#undef ASSERT_TRUE
extern "C" void EmuCoreXOracleSWWalkerExtended(void(*check)(bool,const char*)) {
    const char* active_suite=nullptr;
    void(*teardown)()=nullptr;
    bool suite_failed=false;
    auto finish=[&](){if(teardown){try{teardown();}catch(...){check(false,"GS extended fixture suite teardown completes");}teardown=nullptr;}};
    for(const auto& test:SWParityHarness::Cases()) {
        if(!active_suite||std::strcmp(active_suite,test.suite)!=0) {
            finish();active_suite=test.suite;teardown=test.teardown;
            SWParityHarness::failed=false;
            try{test.setup();}catch(...){SWParityHarness::failed=true;}
            suite_failed=SWParityHarness::failed;
        }
        SWParityHarness::failed=suite_failed;
        std::fprintf(stderr,"SW_PARITY_BEGIN %s\n",test.name);
        if(!suite_failed) {
            try{test.run();}
            catch(const std::exception& e){std::fprintf(stderr,"SW_PARITY_EXCEPTION %s: %s\n",test.name,e.what());SWParityHarness::failed=true;}
            catch(...){SWParityHarness::failed=true;}
        }
        check(!SWParityHarness::failed,test.name);
    }
    finish();
}
#else
extern "C" void EmuCoreXOracleSWWalkerExtended(void(*check)(bool,const char*)) {
    check(false,"GS complete software walker consumer package is available");
}
#endif
