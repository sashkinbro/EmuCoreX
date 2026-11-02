// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Adreno-only actual-renderer selector fixture, included only in native
// self-test builds. The production selector remains outside this fixture.
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "cpuinfo.h"

namespace GSAdrenoCheapFeedbackTest
{
struct DrawInput
{
	u32 accuracy = static_cast<u32>(AccBlendLevel::Basic);
	u32 equation_c = 1; // Ad
	u32 frame_mask = 0xFF000000; // Preserve destination alpha, write RGB.
	u32 target_alpha = 64; // A literal Ad sample contained by the conservative RT range.
	u32 target_alpha_min = 0;
	u32 target_alpha_max = 255; // Keep Ad variable so the real stage cannot fold it into AFIX.
	// GSRendererHW::PRIM_OVERLAP is {UNKNOWN=0, YES=1, NO=2}.
	u32 overlap = 2;
	u32 source_alpha_min = 64;
	u32 source_alpha_max = 192;
	bool date = false;
	bool alpha_test = false;
	u32 alpha_test_function = ATST_ALWAYS;
	u32 alpha_reference = 0;
};

} // namespace GSAdrenoCheapFeedbackTest

bool GSRendererHW::PrepareAdrenoCheapFeedbackStage(GSTexture* target, GSTexture* depth,
	const GSAdrenoCheapFeedbackTest::DrawInput& input, GSHWDrawConfig* output,
	AccBlendLevel* actual_accuracy, bool* date_enabled_after_stages)
{
	if (!g_gs_device || g_gs_renderer || g_texture_cache || !target || !output)
		return false;

	const auto settings = GSConfig;
	GSConfig = Pcsx2Config::GSOptions();
	GSConfig.AccurateBlendingUnit = static_cast<AccBlendLevel>(input.accuracy);
	GSConfig.HWAccurateAlphaTest = true;
	g_gs_renderer = std::make_unique<GSRendererHW>();
	auto& renderer = *static_cast<GSRendererHW*>(g_gs_renderer.get());
	ScopedGuard cleanup([&]() {
		renderer.m_last_rt = nullptr;
		g_gs_renderer.reset();
		GSConfig = settings;
	});

	GIFRegTEX0 tex0{};
	tex0.PSM = PSMCT32;
	tex0.TBW = 1;
	GSTextureCache::Target rt(tex0, GSTextureCache::RenderTarget, target->GetSize(), 1.0f, target);
	ScopedGuard detach([&]() { rt.m_texture = nullptr; });
	rt.m_valid = target->GetRect();
	rt.m_alpha_min = input.target_alpha_min;
	rt.m_alpha_max = input.target_alpha_max;
	std::optional<GSTextureCache::Target> ds;
	if (depth)
	{
		GIFRegTEX0 zbuf{};
		zbuf.PSM = PSMZ32;
		zbuf.TBW = 1;
		ds.emplace(zbuf, GSTextureCache::DepthStencil, depth->GetSize(), 1.0f, depth);
	}
	ScopedGuard detach_depth([&]() { if (ds) ds->m_texture = nullptr; });

	renderer.ResetStates();
	renderer.m_conf.colclip_mode = GSHWDrawConfig::ColClipMode::NoModify;
	renderer.m_draw_env = &renderer.m_env;
	renderer.PRIM = &renderer.m_env.PRIM;
	renderer.m_env.PRIM.ABE = true;
	renderer.m_env.PRIM.AA1 = false;
	renderer.m_env.PRIM.TME = false;
	renderer.m_env.PRIM.FGE = false;
	renderer.m_env.PABE.PABE = false;
	renderer.m_env.COLCLAMP.CLAMP = true;
	renderer.m_context->ALPHA.A = 0; // Cs
	renderer.m_context->ALPHA.B = 1; // Cd
	renderer.m_context->ALPHA.C = input.equation_c;
	renderer.m_context->ALPHA.D = 1; // Cd
	renderer.m_context->ALPHA.FIX = 128;
	renderer.m_context->FRAME.PSM = PSMCT32;
	renderer.m_context->FRAME.FBW = 1;
	renderer.m_context->FRAME.FBMSK = input.frame_mask;
	renderer.m_cached_ctx.FRAME = renderer.m_context->FRAME;
	renderer.m_cached_ctx.TEST = GIFRegTEST{};
	renderer.m_cached_ctx.TEST.ZTE = false;
	renderer.m_cached_ctx.TEST.ATE = input.alpha_test;
	renderer.m_cached_ctx.TEST.ATST = input.alpha_test_function;
	renderer.m_cached_ctx.TEST.AREF = input.alpha_reference;
	renderer.m_cached_ctx.TEST.DATE = input.date;
	renderer.m_cached_ctx.TEST.DATM = 1;
	renderer.m_cached_ctx.ZBUF.PSM = PSMZ32;
	renderer.m_cached_ctx.ZBUF.ZMSK = true;
	renderer.m_vt.m_primclass = GS_TRIANGLE_CLASS;
	renderer.m_prim_overlap = static_cast<GSRendererHW::PRIM_OVERLAP>(input.overlap);
	renderer.m_vt.m_alpha = {static_cast<int>(input.source_alpha_min), static_cast<int>(input.source_alpha_max), true};
	renderer.m_vt.m_eq.value = 0;
	renderer.m_vt.m_min.c = GSVector4i(160, 96, 32, input.source_alpha_min);
	renderer.m_vt.m_max.c = GSVector4i(160, 96, 32, input.source_alpha_max);
	renderer.m_r = target->GetRect();
	renderer.m_conf.rt = target;
	renderer.m_conf.ds = depth;
	renderer.m_conf.colormask.wrgba = 7;
	renderer.m_conf.depth = GSHWDrawConfig::DepthStencilSelector::NoDepth();
	// Match DrawPrims' real no-texture setup. TFX=MODULATE with the absent
	// texture samples zero and erases the vertex color; TFX=4 means use C.
	const float scale_factor = rt.GetScale();
	renderer.m_conf.cb_ps.ScaleFactor = GSVector4(scale_factor * (1.0f / 16.0f),
		1.0f / scale_factor, scale_factor, 0.0f);
	renderer.m_conf.ps.tfx = 4;
	renderer.m_conf.ps.no_color1 = true;
	renderer.m_conf.topology = GSHWDrawConfig::Topology::Triangle;
	renderer.m_conf.drawarea = renderer.m_conf.scissor = target->GetRect();

	// Run the real framebuffer-mask, alpha-test, and blend stages. DATE method
	// selection itself is inline in DrawPrims; feed its GS TEST input into the
	// same implemented consumers here instead of calling its declaration-only
	// legacy helper.
	renderer.EmulateTextureShuffleAndFbmask(&rt, nullptr);
	GSRendererHW::DATEOptions date{};
	date.enabled = input.date;
	int blend_alpha_min = static_cast<int>(input.target_alpha_min);
	int blend_alpha_max = static_cast<int>(input.target_alpha_max);
	renderer.EmulateAlphaTest(date);
	bool new_scale = false;
	renderer.EmulateBlending(blend_alpha_min, blend_alpha_max, date, &rt, false, new_scale);
	if (date_enabled_after_stages)
		*date_enabled_after_stages = date.enabled;
	*output = renderer.m_conf;
	output->road = GSDecideHWDrawRoad(*output, g_gs_device->Features());
	if (actual_accuracy)
		*actual_accuracy = GSConfig.AccurateBlendingUnit;
	return true;
}

// Actual `EmulateBlending` decision matrix. The single intended RED is the
// copy-road candidate expectation; controls prove that Basic remains selected
// and the alpha/mask/overlap/DATE/test inputs reach the real stage unchanged.
static void DumpAdrenoCheapFeedbackStage(const char* name, bool staged, AccBlendLevel accuracy,
	const GSAdrenoCheapFeedbackTest::DrawInput& input, const GSHWDrawConfig& config)
{
	std::fprintf(stderr,
		"GS_ADRENO_CHEAP_STAGE name=%s staged=%u accuracy=%u sample_ad=%u rt_alpha=%u..%u "
		"blend=%u,%u,%u,%u a_masked=%u barriers=%u/%u mask=%u overlap=%u road=%u%u%u%u%u%u "
		"date=%u/%u/%u atst=%u alpha_test=%u\n",
		name, unsigned(staged), unsigned(accuracy), input.target_alpha, input.target_alpha_min, input.target_alpha_max,
		unsigned(config.ps.blend_a), unsigned(config.ps.blend_b), unsigned(config.ps.blend_c), unsigned(config.ps.blend_d),
		unsigned(config.ps.a_masked), unsigned(config.require_one_barrier), unsigned(config.require_full_barrier),
		unsigned(config.colormask.wrgba), unsigned(input.overlap), unsigned(config.road.rt_loop), unsigned(config.road.depth_loop),
		unsigned(config.road.depth_read), unsigned(config.road.clone_rt), unsigned(config.road.carry_rt),
		unsigned(config.road.carry_depth), unsigned(config.depth.date), unsigned(config.ps.date),
		unsigned(config.destination_alpha), unsigned(config.ps.atst), unsigned(config.alpha_test));
}

void GSRendererHW::TestAdrenoCheapFeedbackStage(void (*check)(bool, const char*))
{
	cpuinfo_initialize();
	const bool isolated = !g_gs_renderer && !g_texture_cache && !g_gs_device;
	check(isolated, "Adreno cheap-feedback stage fixture owns isolated renderer and uncreated device");
	if (!isolated)
		return;

	const auto old_settings = GSConfig;
	GSConfig = Pcsx2Config::GSOptions();
	GSConfig.AccurateBlendingUnit = AccBlendLevel::Basic;
	g_gs_device = std::make_unique<GSDeviceVK>();
	class HostTexture final : public GSTexture
	{
	public:
		HostTexture(GSTexture::Format format = GSTexture::Format::Color,
			GSTexture::Usage usage = GSTexture::RenderTarget)
		{
			m_size = GSVector2i(8, 8);
			m_mipmap_levels = 1;
			m_format = format;
			m_usage = usage;
		}
		void* GetNativeHandle() const override { return nullptr; }
		bool DoUpdate(const GSVector4i&, const void*, int, int) override { return false; }
		bool DoMap(GSMap&, const GSVector4i*, int) override { return false; }
		void Unmap() override {}
		void GenerateMipmap() override {}
#ifdef PCSX2_DEVBUILD
		void SetDebugName(std::string_view) override {}
#endif
	} target;

	GSDevice::FeatureSupport copy_road{};
	copy_road.texture_barrier = false;
	copy_road.framebuffer_fetch = false;
	copy_road.multidraw_fb_copy = false;
	copy_road.texture_feedback_requires_copy = true;
	copy_road.cheap_rt_feedback_read = false;
	copy_road.dual_source_blend = true;
	g_gs_device->SetFeaturesForTesting(copy_road);
	GSHWDrawConfig copy{};
	GSAdrenoCheapFeedbackTest::DrawInput base_input{};
	AccBlendLevel copy_accuracy = AccBlendLevel::Minimum;
	const bool staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, base_input, &copy, &copy_accuracy);
	DumpAdrenoCheapFeedbackStage("copy", staged, copy_accuracy, base_input, copy);
	check(staged, "Adreno actual HW blend stage accepts the synthetic no-barrier Vulkan copy-road capability shape");
	if (!staged)
	{
		g_gs_device.reset();
		GSConfig = old_settings;
		return;
	}

	const bool actual_copy_selector_rejected = !copy.ps.a_masked &&
		copy.ps.blend_c == 1 && copy.colormask.wrgba == 7 &&
		copy_accuracy == AccBlendLevel::Basic;
	check(actual_copy_selector_rejected,
		"Adreno Basic actual stage keeps Ad equation and alpha mask but does not add RT feedback on the copy road");
	GSAdrenoCheapFeedbackTest::DrawInput constant_ad_input = base_input;
	constant_ad_input.target_alpha_min = constant_ad_input.target_alpha_max = constant_ad_input.target_alpha;
	GSHWDrawConfig constant_ad{};
	AccBlendLevel constant_ad_accuracy = AccBlendLevel::Minimum;
	const bool constant_ad_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, constant_ad_input,
		&constant_ad, &constant_ad_accuracy);
	DumpAdrenoCheapFeedbackStage("copy-constant-ad", constant_ad_staged, constant_ad_accuracy, constant_ad_input, constant_ad);
	check(constant_ad_staged && constant_ad_accuracy == AccBlendLevel::Basic && !constant_ad.ps.a_masked &&
		constant_ad.ps.blend_c == 2,
		"Adreno constant-Ad control keeps the real Basic stage AFIX fold and does not request RT feedback");

	// Non-policy controls must keep their existing actual stage behavior. Fetch
	// is useful only when the backend reports that this feedback path is cheap.
	// Vulkan ROAA publishes framebuffer_fetch as an input-attachment path, but
	// that capability alone does not establish its cost on Adreno.
	GSDevice::FeatureSupport roaa_unmeasured = copy_road;
	roaa_unmeasured.framebuffer_fetch = true;
	roaa_unmeasured.cheap_rt_feedback_read = false;
	g_gs_device->SetFeaturesForTesting(roaa_unmeasured);
	GSHWDrawConfig roaa_unmeasured_config{};
	AccBlendLevel roaa_unmeasured_accuracy = AccBlendLevel::Minimum;
	const bool roaa_unmeasured_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, base_input,
		&roaa_unmeasured_config, &roaa_unmeasured_accuracy);
	DumpAdrenoCheapFeedbackStage("roaa-unmeasured", roaa_unmeasured_staged, roaa_unmeasured_accuracy,
		base_input, roaa_unmeasured_config);
	check(roaa_unmeasured_staged && !roaa_unmeasured_config.ps.a_masked &&
		roaa_unmeasured_config.ps.blend_c == 1 && roaa_unmeasured_config.colormask.wrgba == 7 &&
		roaa_unmeasured_accuracy == AccBlendLevel::Basic,
		"Adreno ROAA input-fetch capability alone does not qualify the Basic alpha-masked Ad shortcut");

	// A fetch path qualifies once its backend has explicitly reported cheap reads;
	// native barriers alone still do not.
	GSDevice::FeatureSupport fetch_road = copy_road;
	fetch_road.framebuffer_fetch = true;
	fetch_road.cheap_rt_feedback_read = true;
	g_gs_device->SetFeaturesForTesting(fetch_road);
	GSHWDrawConfig fetch{};
	AccBlendLevel fetch_accuracy = AccBlendLevel::Minimum;
	const bool fetch_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, base_input, &fetch, &fetch_accuracy);
	DumpAdrenoCheapFeedbackStage("fetch", fetch_staged, fetch_accuracy, base_input, fetch);
	const bool fetch_control = fetch_staged && fetch.ps.a_masked && fetch.colormask.wrgba == 7 &&
		fetch_accuracy == AccBlendLevel::Basic;
	check(fetch_control, "Adreno Basic actual stage retains the preexisting in-tile-fetch alpha-masked Ad fast path");

	GSDevice::FeatureSupport barrier_road = copy_road;
	barrier_road.texture_barrier = true;
	barrier_road.texture_feedback_requires_copy = false;
	g_gs_device->SetFeaturesForTesting(barrier_road);
	GSHWDrawConfig barrier{};
	AccBlendLevel barrier_accuracy = AccBlendLevel::Minimum;
	const bool barrier_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, base_input, &barrier, &barrier_accuracy);
	DumpAdrenoCheapFeedbackStage("barrier", barrier_staged, barrier_accuracy, base_input, barrier);
	check(barrier_staged && !barrier.ps.a_masked,
		"Adreno Basic actual stage does not add the shortcut when only native texture barriers are available");
	g_gs_device->SetFeaturesForTesting(copy_road);

	// Matrix keeps the gate's decision independent from unrelated semantic
	// controls; final per-draw software/HW/DATE decisions remain the renderer's.
	for (u32 overlap : {static_cast<u32>(PRIM_OVERLAP_NO), static_cast<u32>(PRIM_OVERLAP_YES), static_cast<u32>(PRIM_OVERLAP_UNKNOW)})
	{
		GSAdrenoCheapFeedbackTest::DrawInput input{};
		input.overlap = overlap;
		GSHWDrawConfig output{};
		const bool overlap_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, input, &output);
		DumpAdrenoCheapFeedbackStage("matrix-overlap", overlap_staged, AccBlendLevel::Basic, input, output);
		check(overlap_staged && output.colormask.wrgba == 7 && output.ps.blend_c == 1,
			"Adreno actual stage preserves Basic Ad equation/write mask for each overlap class");
		GSHWDrawConfig alpha_writable{};
		input.frame_mask = 0;
		const bool alpha_writable_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, input, &alpha_writable);
		DumpAdrenoCheapFeedbackStage("matrix-alpha-writable", alpha_writable_staged, AccBlendLevel::Basic, input, alpha_writable);
		check(alpha_writable_staged && alpha_writable.colormask.wa,
			"Adreno actual stage preserves enabled destination alpha writes");
		GSHWDrawConfig as_control{};
		input.frame_mask = 0xFF000000;
		input.equation_c = 0;
		const bool as_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, input, &as_control);
		DumpAdrenoCheapFeedbackStage("matrix-as", as_staged, AccBlendLevel::Basic, input, as_control);
		check(as_staged && !as_control.ps.a_masked,
			"Adreno actual stage does not route alpha-masked As through the Ad feedback shortcut");
		GSHWDrawConfig minimum{};
		input.equation_c = 1;
		input.accuracy = static_cast<u32>(AccBlendLevel::Minimum);
		AccBlendLevel actual_minimum = AccBlendLevel::Basic;
		const bool minimum_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, input, &minimum, &actual_minimum);
		DumpAdrenoCheapFeedbackStage("minimum", minimum_staged, actual_minimum, input, minimum);
		check(minimum_staged && actual_minimum == AccBlendLevel::Minimum,
			"Adreno actual stage observes the requested MINIMUM accuracy level");
		input.accuracy = static_cast<u32>(AccBlendLevel::Basic);
		input.date = true;
		GSHWDrawConfig date{};
		HostTexture depth(GSTexture::Format::DepthStencil, GSTexture::DepthStencil);
		bool date_enabled_after_stages = false;
		const bool date_staged = PrepareAdrenoCheapFeedbackStage(&target, &depth, input, &date, nullptr,
			&date_enabled_after_stages);
		DumpAdrenoCheapFeedbackStage("matrix-date", date_staged, AccBlendLevel::Basic, input, date);
		check(date_staged && date_enabled_after_stages,
			"Adreno actual alpha/blend stages preserve enabled DATE (DrawPrims algorithm selection is separate)");
		input.date = false;
		input.alpha_test = true;
		input.alpha_test_function = ATST_GEQUAL;
		input.alpha_reference = 128;
		GSHWDrawConfig alpha_test{};
		const bool alpha_test_staged = PrepareAdrenoCheapFeedbackStage(&target, nullptr, input, &alpha_test);
		DumpAdrenoCheapFeedbackStage("matrix-alpha-test", alpha_test_staged, AccBlendLevel::Basic, input, alpha_test);
		check(alpha_test_staged && alpha_test.ps.atst == GSHWDrawConfig::PS_ATST_GEQUAL &&
			alpha_test.alpha_test == GSHWDrawConfig::AlphaTestMode::KEEP,
			"Adreno actual alpha test stage preserves a varying GEQUAL128 test");
	}

	g_gs_device.reset();
	GSConfig = old_settings;
}

// Debug-only oracle consumed by the shared family-guarded Android test router.
extern "C" void EmuCoreXOracleAdrenoCheapFeedbackStage(void (*check)(bool, const char*))
{
	GSRendererHW::TestAdrenoCheapFeedbackStage(check);
}

// Pixel consumer uses the *real* Vulkan feature table and the exact HW stage.
// Its exported preparation seam is consumed only by the sibling pixel fixture.
extern "C" bool EmuCoreXPrepareAdrenoCheapFeedbackDraw(GSTexture*, GSTexture*, const GSVertex*, const u16*,
	GSHWDrawConfig*, u32, u32, u32, bool, bool, bool, u32);

bool GSRendererHW::PrepareAdrenoCheapFeedbackDraw(GSTexture* target, GSTexture* depth, const GSVertex* vertices,
	const u16* indices, GSHWDrawConfig* output, u32 frame_mask, u32 target_alpha, u32 overlap, bool date,
	bool alpha_test, bool alpha_test_pass, u32 alpha_reference)
{
	if (!g_gs_device || g_gs_renderer || g_texture_cache || !target || !vertices || !indices || !output || (date && !depth))
		return false;
	GSAdrenoCheapFeedbackTest::DrawInput input{};
	input.frame_mask = frame_mask;
	input.target_alpha = target_alpha;
	input.overlap = overlap;
	input.date = date;
	input.alpha_test = alpha_test;
	input.alpha_test_function = alpha_test ? (alpha_test_pass ? ATST_GEQUAL : ATST_LESS) : ATST_ALWAYS;
	input.alpha_reference = alpha_reference;
	if (!PrepareAdrenoCheapFeedbackStage(target, depth, input, output))
		return false;
	// Replace the stage fixture's uniform carrier with the caller's real quad.
	output->verts = vertices;
	output->indices = indices;
	// EmulateTextureShuffleAndFbmask/other actual stages may leave per-draw
	// pointers into the temporary helper renderer. The config outlives it, so
	// supply stable caller-side two-triangle split data for backend consumers.
	static const std::vector<size_t> drawlist = {1, 1};
	static thread_local std::vector<GSVector4i> drawlist_bbox;
	drawlist_bbox.assign(2, target->GetRect());
	output->drawlist = &drawlist;
	output->drawlist_bbox = &drawlist_bbox;
	output->ds = depth;
	output->nverts = output->nindices = 6;
	output->indices_per_prim = 3;
	output->topology = GSHWDrawConfig::Topology::Triangle;
	output->cb_vs.vertex_scale = GSVector2(1.0f / 32.0f, 1.0f / 32.0f);
	output->cb_vs.vertex_offset = GSVector2(1.0f, 1.0f);
	output->cb_vs.max_depth = 0xFFFFFFFF;
	output->drawarea = output->scissor = target->GetRect();
	if (date)
	{
		// This pixel fixture tests Vulkan's real Stencil DATE consumer. The
		// renderer's algorithm selection is inline in DrawPrims and is not part
		// of this seam, so specify the selected consumer state explicitly here.
		output->destination_alpha = GSHWDrawConfig::DestinationAlphaMode::Stencil;
		output->datm = SetDATM::DATM1;
		output->depth.date = true;
	}
	output->road = GSDecideHWDrawRoad(*output, g_gs_device->Features());
	return true;
}

extern "C" bool EmuCoreXPrepareAdrenoCheapFeedbackDraw(GSTexture* target, GSTexture* depth, const GSVertex* vertices,
	const u16* indices, GSHWDrawConfig* output, u32 frame_mask, u32 target_alpha, u32 overlap, bool date,
	bool alpha_test, bool alpha_test_pass, u32 alpha_reference)
{
	return GSRendererHW::PrepareAdrenoCheapFeedbackDraw(target, depth, vertices, indices, output, frame_mask, target_alpha,
		overlap, date, alpha_test, alpha_test_pass, alpha_reference);
}
