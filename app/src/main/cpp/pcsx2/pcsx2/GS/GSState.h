// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/GS.h"
#include "GS/GSBackQueue.h"
#include "GS/GSPerfMon.h"
#include "GS/GSLocalMemory.h"
#include "GS/GSVertexKick.h"
#include "GS/GSDrawingContext.h"
#include "GS/GSDrawingEnvironment.h"
#include "GS/Renderers/Common/GSVertex.h"
#include "GS/Renderers/Common/GSVertexTrace.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "GS/GSVector.h"
#include "GS/GSFusedVertexTrace.h"
#include "GS/GSCarryingVertex.h"
#include "GSAlignedClass.h"

#include <array>
#include <mutex>

class GSDumpBase;

class GSState : public GSAlignedClass<32>
{
	friend class GSVertexTrace;
	friend class GSFrontState;
public:
	GSState(GSBackQueue::Channel* shared_chan = nullptr, bool is_front_parser = false);
	virtual ~GSState();

	GSBackQueue::Channel* GetBackChannel() { return m_chan; }
	bool IsBackThreadRunning() const { return m_chan && m_chan->consumer_running; }
	void StopBackThread();
	void DrainBackQueue();
	static constexpr int GetSaveStateSize(int version);

private:
	// RESTRICT prevents multiple loads of the same part of the register when accessing its bitfields (the compiler is happy to know that memory writes in-between will not go there)

	typedef void (GSState::*GIFPackedRegHandler)(const GIFPackedReg* RESTRICT r);

	GIFPackedRegHandler m_fpGIFPackedRegHandlers[16] = {};
	GIFPackedRegHandler m_fpGIFPackedRegHandlerXYZ[8][4] = {};

	void CheckFlushes();

	void GIFPackedRegHandlerNull(const GIFPackedReg* RESTRICT r);
	void GIFPackedRegHandlerRGBA(const GIFPackedReg* RESTRICT r);
	void GIFPackedRegHandlerSTQ(const GIFPackedReg* RESTRICT r);
	void GIFPackedRegHandlerUV(const GIFPackedReg* RESTRICT r);
	void GIFPackedRegHandlerUV_Hack(const GIFPackedReg* RESTRICT r);
	template<u32 prim, u32 adc, bool auto_flush> void GIFPackedRegHandlerXYZF2(const GIFPackedReg* RESTRICT r);
	template<u32 prim, u32 adc, bool auto_flush> void GIFPackedRegHandlerXYZ2(const GIFPackedReg* RESTRICT r);
	void GIFPackedRegHandlerFOG(const GIFPackedReg* RESTRICT r);
	void GIFPackedRegHandlerA_D(const GIFPackedReg* RESTRICT r);
	void GIFPackedRegHandlerNOP(const GIFPackedReg* RESTRICT r);

	typedef void (GSState::*GIFRegHandler)(const GIFReg* RESTRICT r);

	GIFRegHandler m_fpGIFRegHandlers[256] = {};
	GIFRegHandler m_fpGIFRegHandlerXYZ[8][4] = {};

	typedef void (GSState::*GIFPackedRegHandlerC)(const GIFPackedReg* RESTRICT r, u32 size);

	GIFPackedRegHandlerC m_fpGIFPackedRegHandlersC[2] = {};
	GIFPackedRegHandlerC m_fpGIFPackedRegHandlerSTQRGBAXYZF2[8] = {};
	GIFPackedRegHandlerC m_fpGIFPackedRegHandlerSTQRGBAXYZ2[8] = {};

	template<u32 prim, bool auto_flush> void GIFPackedRegHandlerSTQRGBAXYZF2(const GIFPackedReg* RESTRICT r, u32 size);
	template<u32 prim, bool auto_flush> void GIFPackedRegHandlerSTQRGBAXYZ2(const GIFPackedReg* RESTRICT r, u32 size);
	void GIFPackedRegHandlerNOP(const GIFPackedReg* RESTRICT r, u32 size);

	template<int i> void ApplyTEX0(GIFRegTEX0& TEX0);
	void ApplyPRIM(u32 prim);

	void GIFRegHandlerNull(const GIFReg* RESTRICT r);
	void GIFRegHandlerPRIM(const GIFReg* RESTRICT r);
	void GIFRegHandlerRGBAQ(const GIFReg* RESTRICT r);
	void GIFRegHandlerST(const GIFReg* RESTRICT r);
	void GIFRegHandlerUV(const GIFReg* RESTRICT r);
	void GIFRegHandlerUV_Hack(const GIFReg* RESTRICT r);
	template<u32 prim, u32 adc, bool auto_flush> void GIFRegHandlerXYZF2(const GIFReg* RESTRICT r);
	template<u32 prim, u32 adc, bool auto_flush> void GIFRegHandlerXYZ2(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerTEX0(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerCLAMP(const GIFReg* RESTRICT r);
	void GIFRegHandlerFOG(const GIFReg* RESTRICT r);
	void GIFRegHandlerNOP(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerTEX1(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerTEX2(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerXYOFFSET(const GIFReg* RESTRICT r);
	void GIFRegHandlerPRMODECONT(const GIFReg* RESTRICT r);
	void GIFRegHandlerPRMODE(const GIFReg* RESTRICT r);
	void GIFRegHandlerTEXCLUT(const GIFReg* RESTRICT r);
	void GIFRegHandlerSCANMSK(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerMIPTBP1(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerMIPTBP2(const GIFReg* RESTRICT r);
	void GIFRegHandlerTEXA(const GIFReg* RESTRICT r);
	void GIFRegHandlerFOGCOL(const GIFReg* RESTRICT r);
	void GIFRegHandlerTEXFLUSH(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerSCISSOR(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerALPHA(const GIFReg* RESTRICT r);
	void GIFRegHandlerDIMX(const GIFReg* RESTRICT r);
	void GIFRegHandlerDTHE(const GIFReg* RESTRICT r);
	void GIFRegHandlerCOLCLAMP(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerTEST(const GIFReg* RESTRICT r);
	void GIFRegHandlerPABE(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerFBA(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerFRAME(const GIFReg* RESTRICT r);
	template<int i> void GIFRegHandlerZBUF(const GIFReg* RESTRICT r);
	void GIFRegHandlerBITBLTBUF(const GIFReg* RESTRICT r);
	void GIFRegHandlerTRXPOS(const GIFReg* RESTRICT r);
	void GIFRegHandlerTRXREG(const GIFReg* RESTRICT r);
	void GIFRegHandlerTRXDIR(const GIFReg* RESTRICT r);
	void GIFRegHandlerHWREG(const GIFReg* RESTRICT r);

	template<bool auto_flush, bool sprites_only> void SetPrimHandlers();

	struct GSTransferBuffer
	{
		int x = 0, y = 0;
		int w = 0, h = 0;
		int start = 0, end = 0, total = 0;
		u8* buff = nullptr;
		GSVector4i rect = GSVector4i::zero();
		GIFRegBITBLTBUF m_blit = {};
		GIFRegTRXPOS m_pos = {};
		GIFRegTRXREG m_reg = {};
		bool write = false;

		GSTransferBuffer();
		~GSTransferBuffer();

		void Init(GIFRegTRXPOS& TRXPOS, GIFRegTRXREG& TRXREG, const GIFRegBITBLTBUF& blit, bool is_write);
		bool Update(int tw, int th, int bpp, int& len);

	} m_tr;

	// Async downloads are published here instead of directly becoming the EE thread's
	// view of live GS memory. This prevents a completed frame-old download from racing
	// with the next frame's writes and exposing a half-old/half-new result.
	GSLocalMemory m_async_readback_mem;
	std::mutex m_async_readback_mutex;
	std::array<u64, GS_MAX_PAGES> m_async_readback_page_generations = {};
	u64 m_async_readback_generation = 0;

	void MarkAsyncReadbackPagesWritten(const GSOffset& offset, const GSVector4i& rect);

protected:
	static constexpr int INVALID_ALPHA_MINMAX = 500;

	GSVertex m_v = {};
	float m_q = 1.0f;
	GSVector4i m_scissor_cull_min = {};
	GSVector4i m_scissor_cull_max = {};
	GSVector4i m_xyof = {};

	using GSVertexBuff = GSBackQueue::VertexBuff;
	using GSIndexBuff = GSBackQueue::IndexBuff;
	GSVertexBuff m_vertex = {};
	GSIndexBuff m_index = {};

	struct
	{
		GSVertex* buff;
	} m_draw_vertex = {};

	struct
	{
		u16* buff;
	} m_draw_index = {};

	void UpdateContext();
	void UpdateScissor();

	void UpdateVertexKick();

    void InvalidateNativeMirror();
    bool RefreshNativeMirrorBounds(const GSVector4i& cull);
    bool RepairNativeMirror(u32 xy_tail);
    template<u32 prim> __fi bool RecordNativeMirror(u32 slot,u32 xraw,u32 yraw);
    __fi bool TryNativeMirror(u32 xy_tail,u32& reject);
    void PublishNativeMirror(u32 xy_tail,u32 count,const GSVector4i& cull,
        const GSStripKickKernel::Bounds& bounds,const u64* xy,const u64* meta);
	void GrowVertexBuffer();
	void EnsureDrawStaging(u32 vertex_count,u32 index_count);
	u32 m_draw_vertex_alloc=0,m_draw_index_alloc=0;
	bool IsAutoFlushDraw(u32 prim, int& tex_layer);
    template<bool check_shuffle> bool IsAutoFlushDrawImpl(u32 prim,int& tex_layer);
    template<u32 prim> __fi bool AllScaleKickApplies();
    void SyncKickFmmFromTrace(u32 shade);
    void SyncTraceFromKickFmm(u32 shade);
    template<GSVertexKernels::PackedLayout layout> __fi void LatchAllScaleQ(const GIFPackedReg* r);
    template<u32 prim,GSVertexKernels::PackedLayout layout,bool auto_flush>
    void KickAllScaleOne(const GIFPackedReg* r,u64 uvfog,GSLimit24BitDepth depth);
    template<u32 prim,GSVertexKernels::PackedLayout layout>
    void KickAllScaleStagedRun(const GIFPackedReg* r,u32 count);
    template<u32 prim,GSVertexKernels::PackedLayout layout,bool auto_flush>
    void KickPackedAllScale(const GIFPackedReg* r,u32 count);
	template<u32 prim> void HandleAutoFlush();
	bool EarlyDetectShuffle(u32 prim);
	void CheckCLUTValidity(u32 prim);

	template <u32 prim, bool auto_flush> void VertexKick(u32 skip);

	// Transient batch state. Any callee which grows or flushes the draw must see
	// current cursors, and may replace both buffer pointers before returning.
	struct VertexKickCursor
	{
		GSVertex* vb;
		u16* ib;
		u32 head, tail, next, xy_tail, maxcount, itail;

		__fi void Load(const GSState& s)
		{
			vb = s.m_vertex.buff;
			ib = s.m_index.buff;
			head = s.m_vertex.head;
			tail = s.m_vertex.tail;
			next = s.m_vertex.next;
			xy_tail = s.m_vertex.xy_tail;
			maxcount = s.m_vertex.maxcount;
			itail = s.m_index.tail;
		}

		__fi void Store(GSState& s) const
		{
			s.m_vertex.head = head;
			s.m_vertex.tail = tail;
			s.m_vertex.next = next;
			s.m_vertex.xy_tail = xy_tail;
			s.m_index.tail = itail;
		}
	};
	GSLimit24BitDepth GetDepthClampMode() const;
	template <u32 prim> void VertexKickDirect(u32 skip, const GSVector4i& v0, const GSVector4i& v1, VertexKickCursor& c);
	template <u32 prim> void KickPackedXYZ2Batch(const GIFPackedReg* RESTRICT r, u32 size);
	template <u32 prim, bool xyzf2> void KickPackedBatch(const GIFPackedReg* RESTRICT r, u32 size);
	template <u32 prim, bool xyzf2> u32 KickPackedStripChunk(const GIFPackedReg* RESTRICT r, u32 remaining,
		VertexKickCursor& cursor, u64 uvfog, GSLimit24BitDepth depth, GSVector4i& last0, GSVector4i& last1);
	template <u32 prim, class Parser> u32 KickPackedStripChunkParsed(const GIFPackedReg* RESTRICT r, u32 remaining,
		VertexKickCursor& cursor, const Parser& parser, GSVector4i& last0, GSVector4i& last1);
	template <u32 prim, GSCarryingVertex::Layout layout, bool auto_flush>
	void GIFPackedRegHandlerCarrying(const GIFPackedReg* RESTRICT r, u32 size);
	template <u32 prim, GSCarryingVertex::Layout layout, bool auto_flush>
	static constexpr GIFPackedRegHandlerC CarryingHandlerOrNull();

	// following functions need m_vt to be initialized

	GSVertexTrace m_vt;
	GSVertexTrace::VertexAlpha& GetAlphaMinMax()
	{
		if (!m_vt.m_alpha.valid)
			CalcAlphaMinMax(0, INVALID_ALPHA_MINMAX);
		return m_vt.m_alpha;
	}
	struct TextureMinMaxResult
	{
		enum UsesBoundary
		{
			USES_BOUNDARY_LEFT   = 1 << 0,
			USES_BOUNDARY_TOP    = 1 << 1,
			USES_BOUNDARY_RIGHT  = 1 << 2,
			USES_BOUNDARY_BOTTOM = 1 << 3,
			USES_BOUNDARY_U = USES_BOUNDARY_LEFT | USES_BOUNDARY_RIGHT,
			USES_BOUNDARY_V = USES_BOUNDARY_TOP | USES_BOUNDARY_BOTTOM,
		};
		GSVector4i coverage; ///< Part of the texture used
		u8 uses_boundary;    ///< Whether or not the usage touches the left, top, right, or bottom edge (and therefore needs wrap modes preserved)
	};
	TextureMinMaxResult GetTextureMinMax(GIFRegTEX0 TEX0, GIFRegCLAMP CLAMP, bool linear, bool clamp_to_tsize);
	bool TryAlphaTest(u32& fm, u32& zm);
	bool IsOpaque();
	bool IsFlatShaded();
	bool IsMipMapDraw();
	bool IsMipMapActive();
	bool IsCoverageAlpha();
	bool AA1LineCoverageFromPixelRuns();
	bool AA1LineCoverageFromPixelRunsLive(bool tme, bool tcc);
	bool IsCoverageAlphaFixedOne();
	virtual bool IsCoverageAlphaSupported();
    virtual bool BuildsConsolePlane() { return false; }
    virtual GSHWAutoFlushLevel GetAutoFlushLevel() const;
	void CalcAlphaMinMax(const int tex_min, const int tex_max);
	void CorrectATEAlphaMinMax(const u32 atst, const int aref);

	GSVector4 GetXYWindow(const GSVertex& v);
	template<bool fst>
	GSVector4 GetTexCoordsImpl(const GSVertex& v, float q);
	template<bool fst>
	GSVector4 GetTexCoordsImpl(const GSVertex& v);
	GSVector4 GetTexCoords(const GSVertex& v, float q);
	GSVector4 GetTexCoords(const GSVertex& v);

	// Utility functions to detect and get corners of quads.
	template<u32 primclass, bool tme = false, bool fst = false>
	static bool GetQuadCornersImpl(const GSVertex* v, const u16* i, GSVertex& vout0, GSVertex& vout1);
	bool GetQuadCorners(const GSVertex* v, const u16* i, GSVertex& vout0, GSVertex& vout1);

	// Utility functions to get window/texture coordinates of a quad.
	template<u32 primclass>
	void GetQuadBBoxWindowImpl(const GSVertex& v0, const GSVertex& v1, GSVector4& xyout);
	template<u32 primclass, bool tme = false, bool fst = false>
	void GetQuadBBoxWindowImpl(const GSVertex& v0, const GSVertex& v1, GSVector4& xyout, GSVector4& texout, bool keep_tex_order = true);
	void GetQuadBBoxWindow(const GSVertex& v0, const GSVertex& v1, GSVector4& xyout);
	void GetQuadBBoxWindow(const GSVertex& v0, const GSVertex& v1, GSVector4& xyout, GSVector4& texout, bool keep_tex_order = true);

	static void GetQuadRasterizedPoints(GSVector4& xy, bool keep_order = true);
	static void GetQuadRasterizedPoints(GSVector4& xy, GSVector4& tex, bool keep_order = true);

public:
	enum EEGS_TransferType
	{
		EE_to_GS,
		GS_to_GS,
		GS_to_EE,
		Clear
	};

	struct GSUploadQueue
	{
		GIFRegBITBLTBUF blit;
		u64 draw;
		GSVector4i rect;
		EEGS_TransferType transfer_type;
		bool zero_clear = false;
	};

	enum NoGapsType
	{
		Uninitialized = 0,
		GapsFound,
		SpriteNoGaps,
		FullCover,
	};

	GIFPath m_path[4] = {};
	const GIFRegPRIM* PRIM = nullptr;
	GSPrivRegSet* m_regs = nullptr;
	GSLocalMemory m_mem;
	GSDrawingEnvironment m_env = {};
	GSDrawingEnvironment m_prev_env = {};
	GSDrawingEnvironment m_temp_env = {};
	const GSDrawingEnvironment* m_draw_env = &m_env;
	GSDrawingContext* m_context = nullptr;
	GSVector4i temp_native_draw_rect = GSVector4i::zero();
	GSVector4i temp_draw_rect = {};
#ifndef EMUCOREX_DISABLE_GS_DIAGNOSTICS
	std::unique_ptr<GSDumpBase> m_dump;
#endif
	bool m_scissor_invalid = false;
	bool m_quad_check_valid = false;
	bool m_quad_check_valid_shuffle = false;
	bool m_are_quads = false;
	bool m_are_quads_shuffle = false;
	bool m_nativeres = false;
	bool m_unsplit_stencil_counter = false;
    GSHWAutoFlushLevel m_auto_flush_level=GSHWAutoFlushLevel::Disabled;
	GSVertexKernels::CullGrid m_cull_grid = GSVertexKernels::MakeCullGrid(4,4);
	static GSVertexKernels::CullGrid CullGridFor(float scale, GSHalfPixelOffset hpo, bool native_scale_targets=false);
	static GSVertexKernels::CullGrid ConfigCullGrid();
	virtual GSVertexKernels::CullGrid EngineCullGrid() const { return ConfigCullGrid(); }
	void SetCullGrid(const GSVertexKernels::CullGrid& grid) { m_cull_grid=grid; }

	bool m_mipmap = false;
	bool m_texflush_flag = false;
	bool m_isPackedUV_HackFlag = false;
	bool m_channel_shuffle = false;
	bool m_using_temp_z = false;
	bool m_temp_z_full_copy = false;
	bool m_in_target_draw = false;
	bool m_channel_shuffle_finish = false;

	u32 m_target_offset = 0;
	u8 m_scanmask_used = 0;
	u32 m_dirty_gs_regs = 0;
	int m_backed_up_ctx = 0;
	std::vector<GSUploadQueue> m_draw_transfers;
	NoGapsType m_primitive_covers_without_gaps;
	bool m_primitive_union_covers_rect = false;
	GSVector4i m_r = {};
	GSVector4i m_r_no_scissor = {};

	static u64 s_n;
	static u64 s_last_transfer_draw_n;
	static u64 s_transfer_n;

	GSPerfMon m_perfmon_frame; // Track stat across a frame.
	GSPerfMon m_perfmon_draw;  // Track stat across a draw.

	static constexpr u32 STATE_VERSION = 9;

	#define PRIM_REG_MASK 0x7FF
	#define MIPTBP_REG_MASK ((1ULL << 60) - 1ULL)
	#define CLAMP_REG_MASK ((1ULL << 44) - 1ULL)
	#define TEX1_REG_MASK 0xFFF001803FDULL
	#define XYOFFSET_REG_MASK 0x0000FFFF0000FFFFULL
	#define TEXA_REG_MASK 0xFF000080FFULL
	#define FOGCOL_REG_MASK 0xFFFFFF
	#define SCISSOR_REG_MASK 0x7FF07FF07FF07FFULL
	#define ALPHA_REG_MASK 0xFF000000FFULL
	#define DIMX_REG_MASK 0x7777777777777777ULL
	#define FRAME_REG_MASK 0xFFFFFFFF3F3F01FFULL
	#define ZBUF_REG_MASK 0x10F0001FFULL
	#define TEST_REG_MASK 0x7FFFF

	enum REG_DIRTY
	{
		DIRTY_REG_ALPHA,
		DIRTY_REG_CLAMP,
		DIRTY_REG_COLCLAMP,
		DIRTY_REG_DIMX,
		DIRTY_REG_DTHE,
		DIRTY_REG_FBA,
		DIRTY_REG_FOGCOL,
		DIRTY_REG_FRAME,
		DIRTY_REG_MIPTBP1,
		DIRTY_REG_MIPTBP2,
		DIRTY_REG_PABE,
		DIRTY_REG_PRIM,
		DIRTY_REG_SCANMSK,
		DIRTY_REG_SCISSOR,
		DIRTY_REG_TEST,
		DIRTY_REG_TEX0,
		DIRTY_REG_TEX1,
		DIRTY_REG_TEXA,
		DIRTY_REG_XYOFFSET,
		DIRTY_REG_ZBUF
	};

	enum GSFlushReason
	{
		UNKNOWN = 1 << 0,
		RESET = 1 << 1,
		CONTEXTCHANGE = 1 << 2,
		CLUTCHANGE = 1 << 3,
		GSTRANSFER = 1 << 4,
		UPLOADDIRTYTEX = 1 << 5,
		UPLOADDIRTYFRAME = 1 << 6,
		UPLOADDIRTYZBUF = 1 << 7,
		LOCALTOLOCALMOVE = 1 << 8,
		DOWNLOADFIFO = 1 << 9,
		SAVESTATE = 1 << 10,
		LOADSTATE = 1 << 11,
		AUTOFLUSH = 1 << 12,
		VSYNC  = 1 << 13,
		GSREOPEN = 1 << 14,
		VERTEXCOUNT = 1 << 15,
	};

	GSFlushReason m_state_flush_reason = UNKNOWN;

	enum PRIM_OVERLAP
	{
		PRIM_OVERLAP_UNKNOW,
		PRIM_OVERLAP_YES,
		PRIM_OVERLAP_NO
	};

	PRIM_OVERLAP m_prim_overlap = PRIM_OVERLAP_UNKNOW;
	std::vector<size_t> m_drawlist;
	std::vector<GSVector4i> m_drawlist_bbox;

	using GSPCRTCRegs = GSBackQueue::GSPCRTCRegs;
	GSPCRTCRegs PCRTCDisplays;

public:
	/// Returns the appropriate directory for draw dumping.
#ifdef EMUCOREX_ENABLE_NATIVE_SELF_TESTS
	static void TestBackQueue(void (*check)(bool,const char*));
	static void TestCullGrid(void (*check)(bool,const char*));
#endif
	static std::string GetDrawDumpPath(const char* format, ...);

	/// Expands dither matrix, suitable for software renderer.
	static void ExpandDIMX(GSVector4i* dimx, const GIFRegDIMX DIMX);

	/// Returns a string representing the flush reason.
	static const char* GetFlushReasonString(GSFlushReason reason);

	void SubmitPcrtcSync();
	void ResetHandlers();
	void ResetPCRTC();

	GSVideoMode GetVideoMode();

	bool isinterlaced();
	bool isReallyInterlaced();

	float GetTvRefreshRate();

	virtual void Reset(bool hardware_reset);
	virtual void UpdateSettings(const Pcsx2Config::GSOptions& old_config);

	void Flush(GSFlushReason reason);
	void FlushDraw(GSFlushReason reason);
	void FlushDrawBuffered(GSFlushReason reason);
	u32 CalcMask(int exp, int max_exp);
	void FlushPrim();
	bool TestDrawChanged();
	void FlushWrite();
	void FlushWriteSlow();
	virtual void Draw() = 0;
	virtual void PurgeTextureCache(bool sources, bool targets, bool hash_cache);
	virtual void ReadbackTextureCache();
	virtual void InvalidateVideoMem(const GIFRegBITBLTBUF& BITBLTBUF, const GSVector4i& r) {}
	virtual void InvalidateLocalMem(const GIFRegBITBLTBUF& BITBLTBUF, const GSVector4i& r, bool clut = false) {}

	virtual void Move();

	GSVector4i GetTEX0Rect(GSDrawingContext prev_ctx);
	GSVector4i GetTEX0Rect() { return GetTEX0Rect(m_prev_env.CTXT[m_prev_env.PRIM.CTXT]); }
	void CheckWriteOverlap(bool req_write, bool req_read);
	void WriteQueued(const u8* mem, int len);
	void Write(const u8* mem, int len);
	void Read(u8* mem, int len);
	void InitReadFIFO(u8* mem, int len);

	void SoftReset(u32 mask);
	void WriteCSR(u32 csr) { m_regs->CSR.U32[1] = csr; }
	void ReadFIFO(u8* mem, int size);
	void ReadLocalMemoryUnsync(u8* mem, int qwc, GIFRegBITBLTBUF BITBLTBUF, GIFRegTRXPOS TRXPOS, GIFRegTRXREG TRXREG);
	GSLocalMemory& GetAsyncReadbackMemory() { return m_mem_target->m_async_readback_mem; }
	std::mutex& GetAsyncReadbackMutex() { return m_mem_target->m_async_readback_mutex; }
	u64 CaptureAsyncReadbackGeneration();
	bool AreAsyncReadbackPagesCurrent(u64 captured_generation,
		const GIFRegTEX0& TEX0, const GSVector4i& rect);
	void MarkAsyncReadbackPagesWrittenLocked(const GIFRegTEX0& TEX0, const GSVector4i& rect);
	void SyncAsyncReadbackMemory();
	template<int index> void Transfer(const u8* mem, u32 size);
	int Freeze(freezeData* fd, bool sizeonly);
	int Defrost(const freezeData* fd);

	u8* GetRegsMem() const { return reinterpret_cast<u8*>(m_regs); }
	void SetRegsMem(u8* basemem) { m_regs = reinterpret_cast<GSPrivRegSet*>(basemem); }

	GSDumpBase* GetDumpSink() const
	{
#if defined(EMUCOREX_DISABLE_GS_DIAGNOSTICS)
		return nullptr;
#else
		return m_mem_target->m_dump.get();
#endif
	}
	void DumpDrawInfo(bool dump_regs, bool dump_verts, bool dump_transfers);
	void DumpVertices(const std::string& filename);
	template <u32 primclass> void RewriteVerticesIfLargeSTImpl(const GSVector4& large_val, bool check_clamp_mode);
	void RewriteVerticesIfLargeST(const GSVector4& large_val, bool check_clamp_mode);
	void DumpTransferList(const std::string& filename);
	void DumpTransferImages();
	
	template<bool shuffle_check>
	bool TrianglesAreQuadsImpl();
	bool TrianglesAreQuads(bool shuffle_check = false);
	template <u32 primclass>
	PRIM_OVERLAP GetPrimitiveOverlapDrawlistImpl(bool save_drawlist = false, bool save_bbox = false,
		float bbox_scale = 1.0f, u32* max_size = nullptr);
	PRIM_OVERLAP GetPrimitiveOverlapDrawlist(bool save_drawlist = false, bool save_bbox = false,
		float bbox_scale = 1.0f, u32* max_size = nullptr);
	PRIM_OVERLAP PrimitiveOverlap(bool save_drawlist = false);
	bool SpriteDrawWithoutGaps();
	bool SpriteUnionCoversDrawRect();
	void CalculatePrimitiveCoversWithoutGaps(bool union_cover = true);
	GIFRegTEX0 GetTex0Layer(u32 lod);

protected:
	// Transient scratch only: appended so existing hot members keep their offsets.
	// Explicit savestate serialization does not contain this side table.
	alignas(16) u64 m_strip_side_xy[128] = {};
	alignas(16) u64 m_strip_side_meta[128] = {};
#if defined(ARCH_ARM64)
	GSFusedVertexTrace::State m_fused_trace;
#endif
	GIFPackedRegHandlerC m_fpGIFPackedCarrying[GIF_REG_COMPLEX_COUNT - 2][8] = {};
	GIFPackedRegHandlerC m_fpGIFPackedCarryingC[GIF_REG_COMPLEX_COUNT - 2] = {};
	GIFPackedLayout m_packed_layout = {};
	static constexpr int MAX_DRAW_BUFFERS=3;
	GSFusedVertexTrace::State m_buffer_fused_trace[MAX_DRAW_BUFFERS];
	int m_used_buffers_idx=1,m_current_buffer_idx=0,m_active_buffer_idx=0;
	bool m_recent_buffer_switch=false;
	bool m_track_native_draw_rect=false;
	bool CheckOverlapVerts(u32 n);
	bool CheckOverlapVertsSlow(u32 n);
	template<u32 prim> void BeforeBufferedVertex();
	template<u32 prim> void FinishBufferedVertex(u32 previous_tail,u64 previous_serial);
	template<u32 prim,u32 adc,bool auto_flush,bool fog> void BufferedPackedXYZ(const GIFPackedReg* r);
	template<u32 prim,u32 adc,bool auto_flush,bool fog> void BufferedRegXYZ(const GIFReg* r);
	template<u32 prim,GSVertexKernels::PackedLayout layout,bool auto_flush> void BufferedPackedLayout(const GIFPackedReg* r,u32 size);
	template<u32 prim,GSCarryingVertex::Layout layout,bool auto_flush> static constexpr GIFPackedRegHandlerC BufferedCarryingHandlerOrNull();
	template<bool auto_flush,bool sprites_only> void SetBufferedPrimHandlers();
	struct VertexSlots
	{
		GSState* owner; GSVertexBuff inactive[MAX_DRAW_BUFFERS]={};
		GSVertexBuff& operator[](int i) { return i==owner->m_active_buffer_idx?owner->m_vertex:inactive[i]; }
		const GSVertexBuff& operator[](int i) const { return i==owner->m_active_buffer_idx?owner->m_vertex:inactive[i]; }
	} m_vertex_buffers{this};
	struct IndexSlots
	{
		GSState* owner; GSIndexBuff inactive[MAX_DRAW_BUFFERS]={};
		GSIndexBuff& operator[](int i) { return i==owner->m_active_buffer_idx?owner->m_index:inactive[i]; }
		const GSIndexBuff& operator[](int i) const { return i==owner->m_active_buffer_idx?owner->m_index:inactive[i]; }
	} m_index_buffers{this};
	struct GSDrawBufferEnv
	{
		GSDrawingEnvironment m_env;
		int m_backed_up_ctx = 0;
		u32 m_dirty_regs = 0;
		GSVector4i draw_rect = GSVector4i::zero();
		// draw_rect asked on the native pixel grid, whatever the upscale (see
		// GSVertexKernels::PrimNativeDrawRect). Read by CheckOverlapVertsSlow and by
		// nothing else -- draw_rect above keeps the shipped rounding because it
		// reaches the texture cache. Equal to draw_rect at native resolution, by
		// construction rather than by a branch. Only maintained while
		// GSConfig.UserHacks_DrawBuffering is set, which is the only way the
		// heuristic that reads it is reachable.
		GSVector4i native_draw_rect = GSVector4i::zero();
		bool related_draw = false;
	};

	GSDrawBufferEnv m_env_buffers[MAX_DRAW_BUFFERS] = {};

	void SelectDrawBuffer(int i);
	void ResetDrawBuffers();
	void ResetDrawBufferIdx();
	void FlushBuffers(bool flush_base_only=false,bool use_flush_reason=false,GSFlushReason flush_reason=GSFlushReason::CONTEXTCHANGE);
	void PushBuffer();
	void SetDrawBufferEnv();
	void SetDrawBuffDirty();
	bool CanBufferNewDraw();
	const GSDrawingEnvironment& BufferEnv(int i) const
	{ return i==m_current_buffer_idx || !GSConfig.UserHacks_DrawBuffering?m_prev_env:m_env_buffers[i].m_env; }

	// The front/back seam: the front builds a self-contained
	// record, the Exec*Record executor consumes it — inline today, on the back
	// thread once GV7-1 lands. The executor owns the HOST->LOCAL write cursor
	// across transfer slices.
	int m_exec_tr_x = 0, m_exec_tr_y = 0;
	void ExecTransferRecord(const GSBackQueue::TransferRecord& rec);
	void SubmitMove();
	void ExecMoveRecord(const GSBackQueue::MoveRecord& rec);
	void SubmitClutLoad(const GIFRegTEX0& TEX0, const GIFRegTEXCLUT& TEXCLUT);
	void ExecClutLoadRecord(const GSBackQueue::ClutLoadRecord& rec);
	void ExecDrawRecord(const GSBackQueue::DrawRecord& rec);
	GSBackQueue::DrawPrivRegs CaptureDrawPrivRegs();
	void DrawRecordTail(u64 draw_serial);
	void ExecPcrtcSyncRecord(const GSBackQueue::PcrtcSyncRecord& rec);

	// True only on the front parser object of the split: its seam functions build
	// records and push them to the back thread. Everywhere else they skip the
	// record round-trip and call the executor tails against live state.
	bool m_back_records = false;

	// GV7-1d-ii: the front<->back channel (record ring + wake semaphore + pool
	// arenas/free rings, GSBackQueue.h). The back renderer object owns it; the
	// front parser object's m_chan points at the back object's channel. The destructor frees
	// m_chan_storage's pooled arrays — only ever this object's own storage, so
	// a front pointing elsewhere frees nothing it doesn't own.
	std::unique_ptr<GSBackQueue::Channel> m_chan_storage;
	GSBackQueue::Channel* m_chan = nullptr;

	// GV7-1d-ii: the object owning local memory, the CLUT palette, and the
	// texture cache for this session. Single-object modes: this. On the front
	// parser object it points at the back renderer, so the drained seams
	// (readbacks, savestates) reach the authoritative m_mem/TC while every
	// register decision stays front-side. Only ever dereferenced after a drain.
	GSState* m_mem_target = this;

	// GV7-1d-ii: set on the back renderer when a front parser object exists.
	// The draw executor then aims m_draw_env/PRIM/m_context around the tail
	// itself (on a single object FlushDraw owns that aiming, and the front's
	// carry-over rebuild depends on FlushDraw's restore happening after).
	bool m_split_back = false;

	// The inverse of m_mem_target: the object holding the authoritative parse
	// state (env, vertex, transfer cursor). On the back renderer under the split
	// it points at the front; everywhere else it is this. Used where the back
	// needs the state a savestate would record — the GS dump's initial freeze.
	GSState* m_parse_target = this;

	// GV7-1c: draw-node pool. Acquire is front-side (free ring first, then arena
	// growth up to the ring capacity, then backpressure); Release is the consume
	// site (inline modes: FlushPrim right after the executor returns; pipelined:
	// the back thread after DrawRecordTail).
	GSBackQueue::DrawNode* AcquireDrawNode();
	void ReleaseDrawNode(GSBackQueue::DrawNode* node);

	// GV7-1c: transfer payload pool (record modes only; mode 0 keeps
	// GSTransferBuffer's own allocation untouched). m_tr.buff aliases the
	// current node's 4MB buffer; RotateTransferPayload runs at transfer Init and
	// swaps to a fresh node once records reference the current one.
	// AdoptTransferBuffer (run by the staging object at construction) hands
	// m_tr's original buffer to the channel as node 0 (the dtor nulls m_tr.buff
	// before the arena walk so it isn't freed twice).
	GSBackQueue::PayloadNode* m_tr_payload_node = nullptr;
	bool m_tr_payload_referenced = false;
	void AdoptTransferBuffer();
	GSBackQueue::PayloadNode* AcquirePayloadNode();
	void RotateTransferPayload();
	void ExecReleasePayloadRecord(const GSBackQueue::ReleasePayloadRecord& rec);

	// The back thread, started by the back renderer object's constructor.
	// VSYNC records are NOT queued: present runs on the MTGS thread after a drain,
	// so the back thread never touches the GSDevice on present paths (and for SW,
	// at all). The split engages only for Vulkan and SW renderers — a GL device is
	// context-bound to the MTGS thread and HW draws would issue GL calls from the
	// wrong thread (GSBackThreadPolicy.h).
	std::thread m_back_thread;
	std::atomic<bool> m_back_thread_exit{false};

	void StartBackThread();
	void BackThreadLoop();
	void ExecRecordSlot(const GSBackQueue::RecordSlot& slot);
	virtual void ExecVsyncRecord(const GSBackQueue::VsyncRecord& rec);

	template <typename T>
	void PushRecord(GSBackQueue::RecordType type, const T& rec)
	{
		// One producer: the ring is single-producer, and the MTGS thread is the only one allowed.
		pxAssert(std::this_thread::get_id() == m_chan->drain_thread);
		for (;;)
		{
			GSBackQueue::RecordSlot* slot = m_chan->ring.BeginPush();
			if (slot)
			{
				slot->type = type;
				std::memcpy(slot->As<T>(), &rec, sizeof(T));
				m_chan->ring.CommitPush();
				m_chan->sema.NotifyOfWork();
				break;
			}
			// Ring full: sleep until the back has retired a batch.
			m_chan->space.Wait([this]() {
				return GSBackQueue::RecordRing::Capacity() - m_chan->ring.Size() >= GSBackQueue::Channel::kRingRefill;
			});
		}
	}

	// Only the parse object writes this counter. Backend cache/oracle counters remain compatible.
	u64 m_front_draw_serial = 0;
	u64 ParseDrawSerial() const { return m_back_records ? m_front_draw_serial : s_n; }
	GSBackQueue::DrawPrivRegs m_draw_priv = {};
	virtual bool DrawClearsScanMask() const { return false; }
	virtual bool HasMoveHook() const { return false; }

};

// The front parser object of the two-object pipelined split. Owns all parse
// state (env, vertex kick, draw buffering,
// transfer staging, CLUT decision) and emits records into the back renderer's
// channel; the back object executes them on the back thread, installing record
// state into its own members. The front never draws, and reaches the
// authoritative local memory / texture cache only through m_mem_target after a
// drain. Created by GS.cpp only when the back thread engaged (GS multi-threading on).
class GSFrontState final : public GSState
{
public:
	GSFrontState(GSState* back);
	~GSFrontState() override;

	void Draw() override;

	// Kick-time coverage-alpha query. Mixed live/stale semantics (see the
	// implementation); needs last-flushed-draw state that only exists after
	// that draw EXECUTED, so it drains the back queue — memoized per
	// (draw epoch, live ALPHA) so at most one drain per AA1 draw.
	bool IsCoverageAlphaSupported() override;

	// The back object's grid: this object culls the primitives the back will draw.
	GSVertexKernels::CullGrid EngineCullGrid() const override { return m_back->m_cull_grid; }

	// Once per frame, after the (drained) vsync executed on the back object:
	// re-mirror present-side state the back mutated (Merge's scanmask
	// decrement) so next frame's front digestion sees what a single object
	// would have.
	void MirrorPostVsyncState();

private:
	GSHWAutoFlushLevel GetAutoFlushLevel() const override { return m_back->GetAutoFlushLevel(); }
	GSState* m_back;

	// IsCoverageAlphaSupported memo (see above).
	u64 m_cov_epoch = ~0ULL;
	u64 m_cov_alpha = 0;
	bool m_cov_answer = false;
};

extern std::unique_ptr<GSFrontState> g_gs_front;

// We put this in the header because of Multi-ISA.
inline void GSState::ExpandDIMX(GSVector4i* dimx, const GIFRegDIMX DIMX)
{
	dimx[1] = GSVector4i(DIMX.DM00, 0, DIMX.DM01, 0, DIMX.DM02, 0, DIMX.DM03, 0);
	dimx[0] = dimx[1].xxzzlh();
	dimx[3] = GSVector4i(DIMX.DM10, 0, DIMX.DM11, 0, DIMX.DM12, 0, DIMX.DM13, 0);
	dimx[2] = dimx[3].xxzzlh();
	dimx[5] = GSVector4i(DIMX.DM20, 0, DIMX.DM21, 0, DIMX.DM22, 0, DIMX.DM23, 0);
	dimx[4] = dimx[5].xxzzlh();
	dimx[7] = GSVector4i(DIMX.DM30, 0, DIMX.DM31, 0, DIMX.DM32, 0, DIMX.DM33, 0);
	dimx[6] = dimx[7].xxzzlh();
}
