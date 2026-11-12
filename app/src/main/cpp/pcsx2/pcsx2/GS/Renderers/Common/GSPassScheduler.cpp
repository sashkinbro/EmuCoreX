// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSPassScheduler.h"

#include "common/Assertions.h"

#include <algorithm>
#include <cstring>

GSPassScheduler::GSPassScheduler() = default;

GSPassScheduler::~GSPassScheduler() = default;

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
bool GSPassScheduler::TestQueueSafety()
{
	class TestTexture final : public GSTexture
	{
	public:
		TestTexture()
		{
			m_usage = Usage::RenderTarget;
			m_format = Format::Color;
		}
		void* GetNativeHandle() const override { return nullptr; }
		bool DoUpdate(const GSVector4i&, const void*, int, int = 0) override { return false; }
		bool DoMap(GSMap&, const GSVector4i*, int) override { return false; }
		void Unmap() override {}
		void GenerateMipmap() override {}
#ifdef PCSX2_DEVBUILD
		void SetDebugName(std::string_view) override {}
#endif
	};

	TestTexture target_a, target_b, sampled, sampled_again;
	GSVertex vertex{};
	u16 index = 0;
	GSHWDrawConfig first{};
	first.rt = &target_a;
	first.tex = &sampled;
	first.verts = &vertex;
	first.indices = &index;
	first.nverts = 1;
	first.nindices = 1;
	if (!IsDeferrable(first))
		return false;

	// Auxiliary draws remain atomic within one RenderHW record. Exercise the actual
	// eligibility gate, including dependencies which exist only in the alpha pass.
	GSHWDrawConfig auxiliary = first;
	auxiliary.colormask.wrgba = 7;
	auxiliary.alpha_second_pass.enable = true;
	auxiliary.alpha_second_pass.colormask.wrgba = 8;
	auxiliary.alpha_second_pass.depth = GSHWDrawConfig::DepthStencilSelector::NoDepth();
	auxiliary.alpha_second_pass.depth.zwe = true;
	auxiliary.alpha_second_pass.ps_aref = 73.0f;
	auxiliary.blend_multi_pass.enable = true;
	auxiliary.blend_multi_pass.blend.enable = true;
	auxiliary.blend_multi_pass.blend.constant_enable = true;
	auxiliary.blend_multi_pass.blend.constant = 91;
	auxiliary.blend_multi_pass.blend_hw = 2;
	auxiliary.blend_multi_pass.no_color1 = true;
	auxiliary.blend_multi_pass.dither = 2;
	auxiliary.blend_multi_pass.clear_sw_blend = true;
	if (!IsDeferrable(auxiliary))
		return false;

	const auto rejects = [&](auto&& mutate) {
		GSHWDrawConfig unsafe = auxiliary;
		mutate(unsafe);
		return !IsDeferrable(unsafe);
	};
	if (!rejects([](auto& c) { c.alpha_second_pass.require_one_barrier = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.require_full_barrier = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.color_feedback = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.depth_feedback = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.tex_is_fb = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.fbmask = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.rov_color = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.rov_depth = GSHWDrawConfig::PS_ROV_DEPTH::READ_ONLY; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.date = 1; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.colclip_hw = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.ps.stencil_counter = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.colormask.logic_op = 1; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.depth.date = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.depth.date_one = true; }) ||
		!rejects([](auto& c) { c.alpha_second_pass.depth.alpha_bit_stencil = 1; }) ||
		!rejects([](auto& c) { c.stencil_counter.enable = true; }) ||
		!rejects([](auto& c) { c.ps.stencil_counter = true; }) ||
		!rejects([](auto& c) { c.ps.rov_color = true; }) ||
		!rejects([](auto& c) { c.ps.rov_depth = GSHWDrawConfig::PS_ROV_DEPTH::READ_ONLY; }) ||
		!rejects([](auto& c) { c.ps.colclip_hw = true; }) ||
		!rejects([](auto& c) { c.ps.date = 1; }) ||
		!rejects([](auto& c) { c.colormask.logic_op = 1; }) ||
		!rejects([](auto& c) { c.depth.date = true; }) ||
		!rejects([](auto& c) { c.depth.date_one = true; }) ||
		!rejects([](auto& c) { c.depth.alpha_bit_stencil = 1; }) ||
		!rejects([](auto& c) { c.date_copy = 1; }) ||
		!rejects([](auto& c) { c.logic_op_split = 1; }) ||
		!rejects([](auto& c) { c.road.rt_loop = true; }) ||
		!rejects([](auto& c) { c.road.depth_loop = true; }) ||
		!rejects([](auto& c) { c.road.depth_read = true; }) ||
		!rejects([](auto& c) { c.road.clone_rt = true; }) ||
		!rejects([&](auto& c) { c.ds_as_rt = &target_b; }) ||
		!rejects([&](auto& c) { c.pal = c.rt; }) ||
		!rejects([&](auto& c) { c.ds = &target_b; c.pal = c.ds; }))
	{
		return false;
	}

	// Disabled auxiliary payload is irrelevant, even if left over from a prior draw.
	GSHWDrawConfig disabled = first;
	disabled.alpha_second_pass.ps.color_feedback = true;
	disabled.alpha_second_pass.require_full_barrier = true;
	if (!IsDeferrable(disabled))
		return false;

	// Masks never weaken attachment ownership: alpha-only and second-pass-only depth
	// writes still exclude RAW/WAR/WAW dependencies across otherwise different runs.
	const auto conflicts = [&](GSHWDrawConfig writer, GSHWDrawConfig next) {
		GSPassScheduler queue;
		return IsDeferrable(writer) && IsDeferrable(next) &&
			queue.TryEnqueue(writer) == Disposition::Queued &&
			queue.TryEnqueue(next) == Disposition::NeedsFlush && queue.GetCount() == 1;
	};
	GSHWDrawConfig writer = auxiliary;
	writer.ds = &sampled_again;
	GSHWDrawConfig next = auxiliary;
	next.rt = &target_b;
	next.tex = &sampled_again;
	if (!conflicts(writer, next)) // second-pass depth write -> texture read
		return false;
	next.tex = &sampled;
	next.pal = &target_a;
	if (!conflicts(writer, next)) // alpha-only write -> palette read
		return false;
	next.pal = nullptr;
	next.ds = writer.ds;
	if (!conflicts(writer, next)) // shared DS, distinct RTs
		return false;
	next.ds = writer.rt;
	if (!conflicts(writer, next)) // RT -> DS alias across runs
		return false;
	writer = auxiliary;
	writer.pal = &target_b;
	next = auxiliary;
	next.rt = &target_b;
	if (!conflicts(writer, next)) // palette read -> alpha-only write
		return false;
	writer.pal = nullptr;
	writer.tex = &target_b;
	if (!conflicts(writer, next)) // texture read -> alpha-only write
		return false;

	// Reject oversized first draws without consuming attachment clear metadata.
	target_a.SetClearColor(0xabcdef01);
	GSPassScheduler rejected;
	GSHWDrawConfig oversized = first;
	oversized.nverts = (MAX_VERTEX_BYTES / sizeof(GSVertex)) + 1;
	if (rejected.TryEnqueue(oversized) != Disposition::NeedsFlush || !rejected.IsEmpty() ||
		target_a.GetState() != GSTexture::State::Cleared || target_a.GetClearColor() != 0xabcdef01)
		return false;

	GSPassScheduler scheduler;
	if (scheduler.TryEnqueue(first) != Disposition::Queued || scheduler.GetCount() != 1 ||
		target_a.GetState() != GSTexture::State::Dirty || !scheduler.References(&target_a) ||
		!scheduler.References(&sampled) ||
		!scheduler.ShouldFlushForTextureMutation(&target_a, false) ||
		!scheduler.ShouldFlushForTextureMutation(&sampled, false) ||
		scheduler.ShouldFlushForTextureMutation(&target_a, true) ||
		scheduler.ShouldFlushForTextureMutation(&sampled_again, false))
	{
		return false;
	}

	GSHWDrawConfig reader{};
	reader.rt = &sampled;
	reader.tex = &sampled_again;
	reader.verts = &vertex;
	reader.indices = &index;
	reader.nverts = 1;
	reader.nindices = 1;
	if (!IsDeferrable(reader) || scheduler.TryEnqueue(reader) != Disposition::NeedsFlush || scheduler.GetCount() != 1)
		return false;

	GSHWDrawConfig feedback = first;
	feedback.tex = feedback.rt;
	if (IsDeferrable(feedback))
		return false;

	GSPassScheduler same_target;
	GSHWDrawConfig second = first;
	second.tex = &sampled_again;
	if (same_target.TryEnqueue(first) != Disposition::Queued ||
		same_target.TryEnqueue(second) != Disposition::Queued || same_target.GetCount() != 2)
	{
		return false;
	}

	GSPassScheduler independent;
	second.rt = &target_b;
	if (independent.TryEnqueue(first) != Disposition::Queued ||
		independent.TryEnqueue(second) != Disposition::Queued || independent.GetCount() != 2)
	{
		return false;
	}

	independent.Clear();
	same_target.Clear();
	if (!independent.IsEmpty() || !same_target.IsEmpty() || independent.References(&target_a))
		return false;

	// Exercise the production run emitter with alternating targets. Geometry is copied at enqueue,
	// clear state is restored before the first draw, and draws to one target retain their order.
	target_a.SetClearColor(0x12345678);
	target_b.SetClearColor(0x87654321);
	GSPassScheduler emitted;
	GSHWDrawConfig draw_a = auxiliary;
	draw_a.tex = &sampled;
	GSHWDrawConfig draw_b = auxiliary;
	draw_b.alpha_second_pass.enable = false;
	draw_b.blend_multi_pass.blend.constant = 39;
	draw_b.rt = &target_b;
	draw_b.tex = &sampled_again;
	GSHWDrawConfig draw_a_again = draw_a;
	draw_a_again.tex = &sampled_again;
	draw_a_again.alpha_second_pass.ps_aref = 117.0f;
	draw_a_again.alpha_second_pass.colormask.wrgba = 3;
	draw_a_again.sampler.key = 5;
	draw_a_again.cb_ps.FogColor_AREF.a = 21.0f;
	const std::array<GSHWDrawConfig, 3> expected = {draw_a, draw_a_again, draw_b};
	vertex.XYZ.X = 11;
	index = 7;
	if (emitted.TryEnqueue(draw_a) != Disposition::Queued)
		return false;
	vertex.XYZ.X = 22;
	index = 9;
	if (emitted.TryEnqueue(draw_b) != Disposition::Queued)
		return false;
	vertex.XYZ.X = 33;
	index = 12;
	if (emitted.TryEnqueue(draw_a_again) != Disposition::Queued || vertex.XYZ.X == 11)
		return false;

	// Overwrite caller-owned config after enqueue; the auxiliary payload must be owned.
	draw_a.alpha_second_pass.ps_aref = 255.0f;
	draw_a.blend_multi_pass.blend.constant = 0;
	draw_a_again.alpha_second_pass.colormask.wrgba = 15;
	draw_a_again.cb_ps.FogColor_AREF.a = 255.0f;
	std::array<GSTexture*, 3> output_targets{};
	std::array<float, 3> output_vertices{};
	std::array<u16, 3> output_indices{};
	u32 output_count = 0;
	emitted.EmitRuns([&](const GSHWDrawConfig& config) {
		const u32 expected_clear = (config.rt == &target_a) ? 0x12345678 : 0x87654321;
		if (config.rt->GetState() != GSTexture::State::Cleared || config.rt->GetClearColor() != expected_clear ||
			output_count >= output_targets.size())
			return;
		const GSHWDrawConfig& owned = expected[output_count];
		if (config.alpha_second_pass.enable != owned.alpha_second_pass.enable ||
			config.alpha_second_pass.ps != owned.alpha_second_pass.ps ||
			config.alpha_second_pass.ps_aref != owned.alpha_second_pass.ps_aref ||
			config.alpha_second_pass.colormask.key != owned.alpha_second_pass.colormask.key ||
			config.alpha_second_pass.depth.key != owned.alpha_second_pass.depth.key ||
			config.blend_multi_pass.enable != owned.blend_multi_pass.enable ||
			config.blend_multi_pass.blend.key != owned.blend_multi_pass.blend.key ||
			config.blend_multi_pass.blend_hw != owned.blend_multi_pass.blend_hw ||
			config.blend_multi_pass.dither != owned.blend_multi_pass.dither ||
			config.blend_multi_pass.no_color1 != owned.blend_multi_pass.no_color1 ||
			config.blend_multi_pass.clear_sw_blend != owned.blend_multi_pass.clear_sw_blend ||
			config.colormask.key != owned.colormask.key || config.depth.key != owned.depth.key ||
			config.sampler.key != owned.sampler.key || config.cb_ps != owned.cb_ps)
		{
			return;
		}
		output_targets[output_count] = config.rt;
		output_vertices[output_count] = static_cast<float>(config.verts[0].XYZ.X);
		output_indices[output_count] = config.indices[0];
		output_count++;
	});
	return output_count == 3 && output_targets[0] == &target_a && output_targets[1] == &target_a &&
	       output_targets[2] == &target_b && output_vertices[0] == 11.0f && output_vertices[1] == 33.0f &&
	       output_vertices[2] == 22.0f && output_indices[0] == 7 && output_indices[1] == 12 && output_indices[2] == 9 &&
	       emitted.IsEmpty();
}
#endif


bool GSPassScheduler::IsDeferrable(const GSHWDrawConfig& config)
{
	// No target to group by.
	if (!config.rt || config.ds_as_rt || !config.verts || !config.indices || config.nverts == 0 || config.nindices == 0)
		return false;

	// A barrier means the draw reads what it (or the previous prim) just wrote, so its
	// position in the stream is load-bearing.
	if (config.require_one_barrier || config.require_full_barrier)
		return false;

	// Same reason, expressed through the sampler instead: the draw samples its own colour
	// or depth attachment.
	if (config.tex_hazard != GSHWDrawConfig::TEX_HAZARD_NONE)
		return false;
	if (config.ps.IsFeedbackLoopRT() || config.ps.IsFeedbackLoopDepth() || config.ps.tex_is_fb)
		return false;
	if ((config.tex && (config.tex == config.rt || config.tex == config.ds)) ||
		(config.pal && (config.pal == config.rt || config.pal == config.ds)))
		return false;

	// Destination alpha testing carries stencil or primitive-ID state between passes.
	if (config.destination_alpha != GSHWDrawConfig::DestinationAlphaMode::Off ||
		config.ps.rov_color || config.ps.HasDepthROV())
		return false;

	// Colour clipping is a cross-draw state machine (colclip_mode and colclip_update_area
	// survive ResetStates deliberately), so its draws must stay where they are.
	if (config.colclip_mode != GSHWDrawConfig::ColClipMode::NoModify)
		return false;

	// Both backends issue auxiliary draws inside this RenderHW record, using the same
	// attachments, textures and geometry. Admit only ordinary attachment accesses in every
	// enabled pass: the alpha selector is independent of the primary selector, and local
	// stencil/logic-op/colclip/ROV paths have additional backend state we do not model here.
	if (config.alpha_second_pass.enable || config.blend_multi_pass.enable)
	{
		const auto is_plain_pass = [](const GSHWDrawConfig::PSSelector& ps,
			GSHWDrawConfig::ColorMaskSelector mask, GSHWDrawConfig::DepthStencilSelector depth) {
			return !ps.IsFeedbackLoopRT() && !ps.IsFeedbackLoopDepth() && !ps.tex_is_fb &&
				!ps.HasColorROV() && !ps.HasDepthROV() && !ps.date && !ps.colclip_hw &&
				!ps.stencil_counter && !mask.logic_op && !depth.date && !depth.date_one &&
				!depth.alpha_bit_stencil;
		};
		if (config.stencil_counter.enable || config.logic_op_split || config.date_copy ||
			config.road.rt_loop || config.road.depth_loop || config.road.depth_read || config.road.clone_rt ||
			!is_plain_pass(config.ps, config.colormask, config.depth))
		{
			return false;
		}
		if (config.alpha_second_pass.enable &&
			(config.alpha_second_pass.require_one_barrier || config.alpha_second_pass.require_full_barrier ||
				!is_plain_pass(config.alpha_second_pass.ps, config.alpha_second_pass.colormask, config.alpha_second_pass.depth)))
		{
			return false;
		}
		if (config.blend_multi_pass.enable)
		{
			GSHWDrawConfig::PSSelector blend_ps = config.ps;
			config.blend_multi_pass.ApplyTo(blend_ps);
			if (!is_plain_pass(blend_ps, config.colormask, config.depth))
				return false;
		}
	}

	// A drawlist means the backend splits the draw per primitive group for barriers; it is
	// only ever set alongside require_full_barrier, but reject it explicitly since the
	// pointer aims at GSRendererHW's per-draw vectors.
	if (config.drawlist || config.drawlist_bbox)
		return false;

	return true;
}

bool GSPassScheduler::IsAttachmentOfAnyRun(const GSTexture* tex) const
{
	if (!tex)
		return false;

	for (u32 i = 0; i < m_run_count; i++)
	{
		if (m_runs[i].rt == tex || m_runs[i].ds == tex)
			return true;
	}
	return false;
}

bool GSPassScheduler::References(const GSTexture* tex) const
{
	return IsAttachmentOfAnyRun(tex) || WasSampled(tex);
}

bool GSPassScheduler::WasSampled(const GSTexture* tex) const
{
	return tex && std::find(m_sampled.begin(), m_sampled.end(), tex) != m_sampled.end();
}

bool GSPassScheduler::NoteSampled(GSTexture* tex)
{
	if (!tex || WasSampled(tex))
		return true;
	if (m_sampled.size() >= MAX_SAMPLED)
		return false;

	m_sampled.push_back(tex);
	return true;
}

bool GSPassScheduler::ConflictsWithQueued(const GSHWDrawConfig& config) const
{
	// Read-after-write: this draw samples something an already-queued draw writes.
	// Reordering could move that write in front of this read.
	if (IsAttachmentOfAnyRun(config.tex) || IsAttachmentOfAnyRun(config.pal))
		return true;

	// Write-after-read: this draw writes something an already-queued draw samples.
	// Reordering could move this write in front of that read.
	//
	// This deliberately fires even when the draw is joining the run that already owns the
	// attachment: the queued reader may live in the *other* run, and reordering this write
	// ahead of it would show it the wrong contents.
	if (WasSampled(config.rt) || WasSampled(config.ds))
		return true;

	return false;
}

GSPassScheduler::Disposition GSPassScheduler::TryEnqueue(const GSHWDrawConfig& config)
{
	pxAssert(IsDeferrable(config));

	if (ConflictsWithQueued(config))
	{
		m_last_refusal = (IsAttachmentOfAnyRun(config.tex) || IsAttachmentOfAnyRun(config.pal)) ?
		                     GSFlushCause::ConflictRaw : GSFlushCause::ConflictWar;
		return Disposition::NeedsFlush;
	}

	// Refuse capacity overflow before opening a run or changing attachment load state.
	const size_t vertex_bytes = sizeof(GSVertex) * config.nverts;
	const size_t index_bytes = sizeof(u16) * config.nindices;

	if (m_queued >= MAX_DRAWS)
	{
		m_last_refusal = GSFlushCause::CapDraws;
		return Disposition::NeedsFlush;
	}
	if ((m_vertices.size() * sizeof(GSVertex)) + vertex_bytes > MAX_VERTEX_BYTES ||
		(m_indices.size() * sizeof(u16)) + index_bytes > MAX_INDEX_BYTES)
	{
		m_last_refusal = GSFlushCause::CapBytes;
		return Disposition::NeedsFlush;
	}

	// Find the run this draw belongs to. A render pass is defined by its attachments, so
	// that pair is the whole key. Both attachments are conservatively treated as read/write
	// for the entire record, including auxiliary passes; colour masks/depth-write flags
	// never weaken a dependency. Pipeline, sampler and mask state stays in each record.
	Run* run = nullptr;
	for (u32 i = 0; i < m_run_count; i++)
	{
		if (m_runs[i].rt == config.rt && m_runs[i].ds == config.ds)
		{
			run = &m_runs[i];
			break;
		}
	}

	if (!run)
	{
		if (m_run_count >= MAX_RUNS)
		{
			m_last_refusal = GSFlushCause::MaxRuns;
			return Disposition::NeedsFlush;
		}

		// Runs get reordered against each other, so they must not share an attachment. A
		// partial match - same depth buffer, different colour target, say - is exactly the
		// aliasing write this cannot allow.
		if (IsAttachmentOfAnyRun(config.rt) || IsAttachmentOfAnyRun(config.ds))
		{
			m_last_refusal = GSFlushCause::AttachAlias;
			return Disposition::NeedsFlush;
		}

		run = &m_runs[m_run_count++];
		run->rt = config.rt;
		run->ds = config.ds;
		pxAssert(run->records.empty());

		// GSTexture::State is not private to the backend: the texture cache reads it to decide
		// whether a target has been written yet, and an undeferred draw would have flipped it
		// to Dirty on the spot, when the backend picked the attachment's load op. Do that here
		// so the deferral window is invisible, and stash the original to hand back at emit -
		// the backend still needs to see Cleared or Invalidated to get the load op right.
		run->rt_state = run->rt->GetState();
		run->rt->SetStateForDeferral(GSTexture::State::Dirty);
		if (run->ds)
		{
			run->ds_state = run->ds->GetState();
			run->ds->SetStateForDeferral(GSTexture::State::Dirty);
		}
	}

	// Every enabled pass shares tex/pal. Record both reads regardless of shader/mask state,
	// so an alpha-only or second-pass depth write cannot hide a sampler dependency.
	// A full sampled set forces a flush rather than silently dropping a hazard.
	if (!NoteSampled(config.tex) || !NoteSampled(config.pal))
	{
		m_last_refusal = GSFlushCause::CapSampled;
		return Disposition::NeedsFlush;
	}

	Record& rec = run->records.emplace_back();
	// Includes all auxiliary selectors, blend/mask/depth state, sampler and constants by
	// value. The only borrowed payloads are geometry (copied below) and textures (covered
	// by References/mutation flushes); drawlist pointers were excluded by IsDeferrable.
	rec.config = config;
	rec.vertex_offset = static_cast<u32>(m_vertices.size());
	rec.index_offset = static_cast<u32>(m_indices.size());

	// GSState reuses its vertex and index buffers for the very next draw, so the geometry
	// has to be taken by value here, not by pointer.
	m_vertices.resize(m_vertices.size() + config.nverts);
	std::memcpy(m_vertices.data() + rec.vertex_offset, config.verts, vertex_bytes);

	m_indices.resize(m_indices.size() + config.nindices);
	std::memcpy(m_indices.data() + rec.index_offset, config.indices, index_bytes);

	m_queued++;
	return Disposition::Queued;
}

void GSPassScheduler::Emit(GSDevice* dev)
{
	EmitRuns([dev](GSHWDrawConfig& config) { dev->DoRenderHW(config); });
}

void GSPassScheduler::Clear()
{
	// Keep the capacity - see the note on the members.
	for (u32 i = 0; i < m_run_count; i++)
	{
		m_runs[i].records.clear();
		m_runs[i].rt = nullptr;
		m_runs[i].ds = nullptr;
	}

	m_run_count = 0;
	m_queued = 0;
	m_sampled.clear();
	m_vertices.clear();
	m_indices.clear();
}
