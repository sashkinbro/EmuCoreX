// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// GPL-3.0+
// INTENTIONALLY UNREFERENCED RED fixture scaffold for the Lenovo blend observer.
// Do not include or route until the owner freezes current production packages and
// adds the Debug-only observer API. This actual-renderer stage should currently
// fail the expectation that a no-barrier, no-fetch copy road lacks affirmative
// cheap-read evidence before alpha-masked Ad selects its shortcut.
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "cpuinfo.h"

void GSRendererHW::TestAdrenoCopyRoadAlphaMaskedAd(void (*check)(bool, const char*))
{
	cpuinfo_initialize();
	const bool isolated = !g_gs_renderer && !g_texture_cache && !g_gs_device;
	check(isolated, "Adreno Basic blend RED fixture owns isolated CPU renderer and uncreated device");
	if (!isolated)
		return;

	const auto settings = GSConfig;
	GSConfig = Pcsx2Config::GSOptions();
	GSConfig.AccurateBlendingUnit = AccBlendLevel::Basic;
	g_gs_device = std::make_unique<GSDeviceVK>();
	g_gs_renderer = std::make_unique<GSRendererHW>();
	auto& r = *static_cast<GSRendererHW*>(g_gs_renderer.get());
	const ScopedGuard restore([&]() {
		g_gs_device->SetColorClipTexture(nullptr);
		g_gs_renderer.reset();
		g_gs_device.reset();
		GSConfig = settings;
	});

	class HostTexture final : public GSTexture
	{
	public:
		HostTexture(Format format, Usage usage)
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
	} color(GSTexture::Format::Color, GSTexture::RenderTarget),
	  depth(GSTexture::Format::DepthStencil, GSTexture::DepthStencil);

	GIFRegTEX0 tex0{};
	tex0.PSM = PSMCT32;
	tex0.TBW = 1;
	GSTextureCache::Target rt(tex0, GSTextureCache::RenderTarget, GSVector2i(8, 8), 1.0f, &color);
	const ScopedGuard detach([&]() { rt.m_texture = nullptr; });
	rt.m_valid = GSVector4i(0, 0, 8, 8);

	GSDevice::FeatureSupport features{};
	features.texture_barrier = false;
	features.framebuffer_fetch = false;
	features.dual_source_blend = true;
	features.multidraw_fb_copy = false;
	g_gs_device->SetFeaturesForTesting(features);
	r.ResetStates();
	r.m_conf.colclip_mode = GSHWDrawConfig::ColClipMode::NoModify;
	r.m_draw_env = &r.m_env;
	r.m_env.COLCLAMP.CLAMP = 1;
	r.m_env.PABE.PABE = false;
	r.PRIM = &r.m_env.PRIM;
	r.m_env.PRIM.ABE = true;
	r.m_env.PRIM.AA1 = false;
	r.m_env.PRIM.TME = false;
	r.m_env.PRIM.FGE = false;
	r.m_context->FRAME.PSM = PSMCT32;
	r.m_context->FRAME.FBW = 1;
	r.m_context->FRAME.FBMSK = 0;
	r.m_cached_ctx.FRAME = r.m_context->FRAME;
	r.m_context->ALPHA.A = 0;
	r.m_context->ALPHA.B = 1;
	r.m_context->ALPHA.C = 1; // Ad destination alpha.
	r.m_context->ALPHA.D = 2;
	r.m_context->ALPHA.FIX = 128;
	r.m_cached_ctx.TEST = GIFRegTEST{};
	r.m_cached_ctx.TEST.ZTE = true;
	r.m_cached_ctx.TEST.ZTST = ZTST_ALWAYS;
	r.m_cached_ctx.ZBUF.ZMSK = true;
	r.m_cached_ctx.ZBUF.PSM = PSMZ32;
	r.m_vt.m_primclass = GS_TRIANGLE_CLASS;
	r.m_prim_overlap = GSRendererHW::PRIM_OVERLAP_NO;
	r.m_vt.m_alpha = {32, 192, true};
	r.m_vt.m_eq.value = 0;
	r.m_vt.m_min.c = GSVector4i(16, 32, 64, 32);
	r.m_vt.m_max.c = GSVector4i(192, 224, 240, 192);
	r.m_conf.rt = &color;
	r.m_conf.ds = &depth;
	r.m_conf.colormask.wrgba = 0x7; // Alpha is masked.
	r.m_conf.depth.ztst = ZTST_ALWAYS;
	r.m_conf.depth.zwe = false;
	r.m_conf.ps.no_color1 = false;
	r.m_r = GSVector4i(0, 0, 8, 8);

	DATEOptions date{};
	r.EmulateAlphaTest(date);
	bool scale = false;
	r.EmulateBlending(0, 255, date, &rt, false, scale);

	const bool selected_current_shortcut = static_cast<bool>(r.m_conf.ps.a_masked);
	std::fprintf(stderr,
		"GS_BLEND_OBSERVER_RED road=copy basic=1 alphaMaskedAd=1 dualSrc=1 shortcut=%u aMasked=%u oneBarrier=%u fullBarrier=%u\n",
		unsigned(selected_current_shortcut), unsigned(r.m_conf.ps.a_masked), unsigned(r.m_conf.require_one_barrier),
		unsigned(r.m_conf.require_full_barrier));
	// Expected failing against the current broad `!texture_barrier` shortcut. No
	// production selector is changed by this fixture; its purpose is to expose the
	// policy mismatch to a routed native run.
	check(!selected_current_shortcut,
		"Adreno copy-road alpha-masked Ad does not infer cheap feedback from missing texture barriers");
}

extern "C" void EmuCoreXOracleAdrenoCopyRoadAlphaMaskedAd(void (*check)(bool, const char*))
{
	GSRendererHW::TestAdrenoCopyRoadAlphaMaskedAd(check);
}
