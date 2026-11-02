// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Adreno-only real Vulkan consumer fixture, included only in native
// self-test builds.
// It consumes the actual GSRendererHW stage through the sibling C bridge and
// never substitutes device feature facts or edits a generated shader.
#include "GS/Renderers/Common/GSVertex.h"

extern "C" bool EmuCoreXPrepareAdrenoCheapFeedbackDraw(GSTexture*, GSTexture*, const GSVertex*, const u16*,
	GSHWDrawConfig*, u32, u32, u32, bool, bool, bool, u32);

static void DumpAdrenoCheapFeedbackDraw(const char* label, const char* phase,
	const GSVertex& vertex, const GSHWDrawConfig& config)
{
	std::fprintf(stderr,
		"GS_ADRENO_AD_DRAW label=%s phase=%s vertex=%u,%u,%u,%u vs=%u ps=%08llx tfx=%u iip=%u "
		"blend=%u/%u,%u,%u,%u a_masked=%u no_color1=%u atst=%u alpha_test=%u date=%u datm=%u "
		"rtA=%u/%u depth=%u blendstate=%u,%u,%u,%u,%u cb_vs_scale=%.6g,%.6g cb_vs_offset=%.6g,%.6g "
		"cb_ps_scale=%.6g,%.6g,%.6g,%.6g road=%u%u%u%u%u%u\n",
		label, phase, unsigned(vertex.RGBAQ.R), unsigned(vertex.RGBAQ.G), unsigned(vertex.RGBAQ.B), unsigned(vertex.RGBAQ.A),
		unsigned(config.vs.key), static_cast<unsigned long long>(config.ps.key_lo), unsigned(config.ps.tfx), unsigned(config.ps.iip),
		unsigned(config.ps.blend_hw), unsigned(config.ps.blend_a), unsigned(config.ps.blend_b),
		unsigned(config.ps.blend_c), unsigned(config.ps.blend_d), unsigned(config.ps.a_masked),
		unsigned(config.ps.no_color1), unsigned(config.ps.atst), unsigned(config.alpha_test), unsigned(config.ps.date),
		unsigned(config.datm), unsigned(config.rt != nullptr), unsigned(config.ds != nullptr), unsigned(config.depth.date),
		unsigned(config.blend.enable), unsigned(config.blend.src_factor), unsigned(config.blend.dst_factor),
		unsigned(config.blend.src_factor_alpha), unsigned(config.blend.dst_factor_alpha),
		config.cb_vs.vertex_scale.x, config.cb_vs.vertex_scale.y, config.cb_vs.vertex_offset.x, config.cb_vs.vertex_offset.y,
		config.cb_ps.ScaleFactor.x, config.cb_ps.ScaleFactor.y, config.cb_ps.ScaleFactor.z, config.cb_ps.ScaleFactor.w,
		unsigned(config.road.rt_loop), unsigned(config.road.depth_loop), unsigned(config.road.depth_read),
		unsigned(config.road.clone_rt), unsigned(config.road.carry_rt), unsigned(config.road.carry_depth));
}

void GSDeviceVK::TestAdrenoCheapFeedbackPixels(void (*check)(bool, const char*))
{
	// The copy-road diagnosis is specifically about Android Adreno Vulkan. Other
	// adapters still execute the source matrix, but cannot stand in for Lenovo.
	if (m_device_properties.vendorID != 0x5143u || m_features.texture_barrier || m_features.framebuffer_fetch)
	{
		std::fprintf(stderr,
			"SKIP GS_ADRENO_CHEAP_FEEDBACK_PIXELS vendor=%04x barrier=%u fetch=%u\n",
			m_device_properties.vendorID, unsigned(m_features.texture_barrier), unsigned(m_features.framebuffer_fetch));
		return;
	}
	check(!m_features.cheap_rt_feedback_read,
		"Adreno Vulkan physical feature table correctly identifies copy-road feedback as not cheap");
	check(m_allocator && m_null_texture && !m_global_descriptor_pool,
		"Adreno cheap-feedback pixel test starts with real Vulkan resources and no synthetic descriptor pool");
	if (!m_allocator || !m_null_texture || m_global_descriptor_pool)
		return;
	EndRenderPass();

	const auto old_timestamp = m_timestamp_query_pool;
	const auto old_statistics = m_pipeline_statistics_query_pool;
	const auto old_point = m_point_sampler;
	const auto old_linear = m_linear_sampler;
	const auto old_tfx_sampler = m_tfx_sampler;
	const auto old_tfx_sampler_key = m_tfx_sampler_sel;
	std::vector<u32> sampler_keys;
	for (const auto& item : m_samplers)
		sampler_keys.push_back(item.first);
	ScopedGuard cleanup([&]() {
		EndRenderPass();
		ExecuteCommandBuffer(true);
		if (m_tfx_ubo_descriptor_set)
			FreePersistentDescriptorSet(m_tfx_ubo_descriptor_set);
		m_tfx_ubo_descriptor_set = VK_NULL_HANDLE;
		m_vertex_stream_buffer.Destroy(false);
		m_expand_index_stream_buffer.Destroy(false);
		m_vertex_uniform_stream_buffer.Destroy(false);
		m_fragment_uniform_stream_buffer.Destroy(false);
		if (m_global_descriptor_pool)
			vkDestroyDescriptorPool(m_device, m_global_descriptor_pool, nullptr);
		m_global_descriptor_pool = VK_NULL_HANDLE;
		if (m_timestamp_query_pool != old_timestamp && m_timestamp_query_pool)
			vkDestroyQueryPool(m_device, m_timestamp_query_pool, nullptr);
		m_timestamp_query_pool = old_timestamp;
		if (m_pipeline_statistics_query_pool != old_statistics && m_pipeline_statistics_query_pool)
			vkDestroyQueryPool(m_device, m_pipeline_statistics_query_pool, nullptr);
		m_pipeline_statistics_query_pool = old_statistics;
		for (auto it = m_samplers.begin(); it != m_samplers.end();)
		{
			if (std::find(sampler_keys.begin(), sampler_keys.end(), it->first) == sampler_keys.end())
			{
				if (it->second)
					vkDestroySampler(m_device, it->second, nullptr);
				it = m_samplers.erase(it);
			}
			else
				++it;
		}
		m_point_sampler = old_point;
		m_linear_sampler = old_linear;
		m_tfx_sampler = old_tfx_sampler;
		m_tfx_sampler_sel = old_tfx_sampler_key;
		m_current_pipeline = VK_NULL_HANDLE;
		InvalidateCachedState();
	});

	const bool initialized = CreateGlobalDescriptorPool() &&
		m_vertex_stream_buffer.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			(m_features.vs_expand ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : 0), 65536, "adreno-ad-feedback-vertex") &&
		m_expand_index_stream_buffer.Create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			m_features.aa1 ? INDEX_BUFFER_SIZE : 4, "adreno-ad-feedback-expand") &&
		m_vertex_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 65536, "adreno-ad-feedback-vs-ubo") &&
		m_fragment_uniform_stream_buffer.Create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 65536, "adreno-ad-feedback-ps-ubo") &&
		CreatePersistentDescriptorSets();
	check(initialized, "Adreno cheap-feedback test creates actual Vulkan TFX streams descriptors and physical layouts");
	if (!initialized)
		return;
	InitializeState();

	// The restart fixture intentionally creates only TFX layouts and render
	// passes. SetupDATE needs its utility conversion pipeline and the dedicated
	// stencil-load render pass, so construct only the actual DATM1 consumer here.
	// Reuse the restart fixture's utility layout when available and restore any
	// temporary layout and render-pass cache state during cleanup.
	const VkRenderPass old_date_render_pass = m_date_setup_render_pass;
	constexpr u32 date_draw_rt = 1, date_draw_ds = 1, date_draw_colclip = 0, date_draw_date = 1,
		date_draw_feedback = 0, date_draw_depth_sampling = 0;
	std::array<std::array<VkRenderPass, 3>, 3> old_date_draw_render_passes{};
	for (u32 rt_op = VK_ATTACHMENT_LOAD_OP_LOAD; rt_op <= VK_ATTACHMENT_LOAD_OP_DONT_CARE; ++rt_op)
	{
		for (u32 ds_op = VK_ATTACHMENT_LOAD_OP_LOAD; ds_op <= VK_ATTACHMENT_LOAD_OP_DONT_CARE; ++ds_op)
		{
			old_date_draw_render_passes[rt_op][ds_op] =
				m_tfx_render_pass[date_draw_rt][date_draw_ds][date_draw_colclip][date_draw_date]
					[date_draw_feedback][date_draw_depth_sampling][rt_op][ds_op];
		}
	}
	const size_t old_convert_size = m_convert.size();
	const auto date_shader = ShaderConvertSelector(SetDATMShader(SetDATM::DATM1));
	const u32 date_pipeline_index = date_shader.Index();
	const VkPipeline old_date_pipeline = old_convert_size > date_pipeline_index ? m_convert[date_pipeline_index] : VK_NULL_HANDLE;
	const VkPipelineLayout old_utility_pipeline_layout = m_utility_pipeline_layout;
	const VkDescriptorSetLayout old_utility_ds_layout = m_utility_ds_layout;
	VkPipeline date_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout test_utility_pipeline_layout = m_utility_pipeline_layout;
	VkDescriptorSetLayout test_utility_ds_layout = m_utility_ds_layout;
	bool date_resources_ready = false;
	bool date_draw_render_pass_ready = false;
	if (!m_point_sampler)
		m_point_sampler = GetSampler(GSHWDrawConfig::SamplerSelector::Point());
	if (!m_linear_sampler)
		m_linear_sampler = GetSampler(GSHWDrawConfig::SamplerSelector::Linear());
	m_utility_sampler = m_point_sampler;
	if (!test_utility_ds_layout)
	{
		Vulkan::DescriptorSetLayoutBuilder dslb;
		if (m_use_push_descriptors)
			dslb.SetPushFlag();
		dslb.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, NUM_UTILITY_SAMPLERS, VK_SHADER_STAGE_FRAGMENT_BIT);
		test_utility_ds_layout = dslb.Create(m_device);
		m_utility_ds_layout = test_utility_ds_layout;
	}
	if (!test_utility_pipeline_layout && test_utility_ds_layout)
	{
		Vulkan::PipelineLayoutBuilder plb;
		plb.AddPushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, CONVERT_PUSH_CONSTANTS_SIZE);
		plb.AddDescriptorSet(test_utility_ds_layout);
		test_utility_pipeline_layout = plb.Create(m_device);
		m_utility_pipeline_layout = test_utility_pipeline_layout;
	}
	if (old_date_render_pass == VK_NULL_HANDLE && test_utility_pipeline_layout != VK_NULL_HANDLE)
	{
		const VkFormat depth_format = LookupNativeFormat(GSTexture::Format::DepthStencil);
		m_date_setup_render_pass = GetRenderPass(VK_FORMAT_UNDEFINED, depth_format,
			VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			m_features.stencil_buffer ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			m_features.stencil_buffer ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE);
		date_draw_render_pass_ready = true;
		const VkFormat color_format = LookupNativeFormat(GSTexture::Format::Color);
		const VkAttachmentLoadOp date_stencil_load = m_features.stencil_buffer ?
			VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		const VkAttachmentStoreOp date_stencil_store = m_features.stencil_buffer ?
			VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		for (u32 rt_op = VK_ATTACHMENT_LOAD_OP_LOAD; rt_op <= VK_ATTACHMENT_LOAD_OP_DONT_CARE; ++rt_op)
		{
			for (u32 ds_op = VK_ATTACHMENT_LOAD_OP_LOAD; ds_op <= VK_ATTACHMENT_LOAD_OP_DONT_CARE; ++ds_op)
			{
				VkRenderPass& date_draw_render_pass =
					m_tfx_render_pass[date_draw_rt][date_draw_ds][date_draw_colclip][date_draw_date]
						[date_draw_feedback][date_draw_depth_sampling][rt_op][ds_op];
				if (date_draw_render_pass == VK_NULL_HANDLE)
				{
					date_draw_render_pass = GetRenderPass(color_format, depth_format,
						static_cast<VkAttachmentLoadOp>(rt_op), VK_ATTACHMENT_STORE_OP_STORE,
						static_cast<VkAttachmentLoadOp>(ds_op), VK_ATTACHMENT_STORE_OP_STORE,
						date_stencil_load, date_stencil_store, false, false);
				}
				date_draw_render_pass_ready &= date_draw_render_pass != VK_NULL_HANDLE;
			}
		}
		const std::optional<std::string> convert_source = ReadShaderSource("shaders/vulkan/convert.glsl");
		if (m_date_setup_render_pass && convert_source)
		{
			const std::string shader_source = fmt::format(
				"#define HAS_BILN {}\n#define HAS_STENCIL_OUTPUT {}\n#define HAS_INTEGER_OUTPUT {}\n"
				"#define HAS_DEPTH_OUTPUT {}\n#define HAS_FLOAT32_INPUT {}\n#define HAS_FLOAT32_OUTPUT {}\n",
				static_cast<int>(date_shader.Biln()), static_cast<int>(date_shader.StencilOutput()),
				static_cast<int>(date_shader.IntegerOutputBpp() != 0), static_cast<int>(date_shader.DepthOutput()),
				static_cast<int>(date_shader.Float32Input()), static_cast<int>(date_shader.Float32Output())) + *convert_source;
			VkShaderModule vs = GetUtilityVertexShader(*convert_source, nullptr);
			VkShaderModule ps = GetUtilityFragmentShader(shader_source, date_shader.EntryPoint());
			ScopedGuard shader_cleanup([this, &vs, &ps]() {
				if (vs) vkDestroyShaderModule(m_device, vs, nullptr);
				if (ps) vkDestroyShaderModule(m_device, ps, nullptr);
			});
			if (vs && ps)
			{
				Vulkan::GraphicsPipelineBuilder gpb;
				SetPipelineProvokingVertex(m_features, gpb);
				AddUtilityVertexAttributes(gpb);
				gpb.SetPipelineLayout(test_utility_pipeline_layout);
				gpb.SetDynamicViewportAndScissorState();
				gpb.AddDynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
				gpb.AddDynamicState(VK_DYNAMIC_STATE_LINE_WIDTH);
				gpb.SetNoCullRasterizationState();
				gpb.SetNoBlendingState();
				gpb.SetVertexShader(vs);
				gpb.SetRenderPass(m_date_setup_render_pass, 0);
				const VkStencilOpState stencil_state = {
					VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP,
					VK_COMPARE_OP_ALWAYS, 1u, 1u, 1u};
				gpb.SetDepthState(false, false, VK_COMPARE_OP_ALWAYS);
				gpb.SetStencilState(true, stencil_state, stencil_state);
				gpb.SetColorWriteMask(0, date_shader.Mask());
				gpb.SetFragmentShader(ps);
				date_pipeline = gpb.Create(m_device, g_vulkan_shader_cache->GetPipelineCache(true), false);
				if (date_pipeline)
				{
					m_convert.resize(ShaderConvertSelector::NUM_TOTAL_SHADERS, VK_NULL_HANDLE);
					m_convert[date_shader.Index()] = date_pipeline;
					date_resources_ready = true;
				}
			}
		}
	}
	check(date_resources_ready && date_draw_render_pass_ready && m_point_sampler,
		"Adreno DATE pixel control creates actual DATM1 conversion and TFX passes with a physical utility sampler");
	ScopedGuard date_cleanup([this, old_date_render_pass, old_convert_size, date_pipeline_index, old_date_pipeline,
		old_utility_pipeline_layout, old_utility_ds_layout, old_date_draw_render_passes,
		&date_pipeline]() {
		EndRenderPass();
		ExecuteCommandBuffer(true);
		if (date_pipeline)
			vkDestroyPipeline(m_device, date_pipeline, nullptr);
		if (old_convert_size > date_pipeline_index)
			m_convert[date_pipeline_index] = old_date_pipeline;
		m_convert.resize(old_convert_size);
		m_date_setup_render_pass = old_date_render_pass;
		for (u32 rt_op = VK_ATTACHMENT_LOAD_OP_LOAD; rt_op <= VK_ATTACHMENT_LOAD_OP_DONT_CARE; ++rt_op)
		{
			for (u32 ds_op = VK_ATTACHMENT_LOAD_OP_LOAD; ds_op <= VK_ATTACHMENT_LOAD_OP_DONT_CARE; ++ds_op)
			{
				m_tfx_render_pass[date_draw_rt][date_draw_ds][date_draw_colclip][date_draw_date]
					[date_draw_feedback][date_draw_depth_sampling][rt_op][ds_op] = old_date_draw_render_passes[rt_op][ds_op];
			}
		}
		if (old_utility_pipeline_layout == VK_NULL_HANDLE && m_utility_pipeline_layout)
		{
			vkDestroyPipelineLayout(m_device, m_utility_pipeline_layout, nullptr);
			m_utility_pipeline_layout = VK_NULL_HANDLE;
		}
		if (old_utility_ds_layout == VK_NULL_HANDLE && m_utility_ds_layout)
		{
			vkDestroyDescriptorSetLayout(m_device, m_utility_ds_layout, nullptr);
			m_utility_ds_layout = VK_NULL_HANDLE;
		}
		m_current_render_target = m_current_depth_target = nullptr;
		m_current_framebuffer = VK_NULL_HANDLE;
		m_tfx_textures.fill(nullptr);
	});

	auto target = GSTextureVK::Create(GSTexture::RenderTarget, GSTexture::Format::Color, 4, 4, 1);
	check(bool(target), "Adreno cheap-feedback test allocates actual 4x4 UNORM32 render target");
	if (!target)
		return;

	// Each column has constant but distinct Cs/Cd and destination alpha, so
	// bilinear/triangle interpolation cannot hide a one-byte blend error. The
	// source alpha is 192 in every column; the equation factor under test is Ad.
	static constexpr u8 source[4][3] = {
		{160, 96, 32}, {200, 80, 40}, {128, 64, 96}, {180, 96, 48}};
	static constexpr u8 destination[4][4] = {
		{32, 64, 96, 64}, {40, 64, 72, 128}, {64, 96, 128, 192}, {52, 64, 96, 64}};
	static constexpr u8 expected[4][4] = {
		{96, 80, 64, 64}, {200, 80, 40, 128}, {160, 48, 80, 192}, {116, 80, 72, 64}};
	std::array<u32, 16> initial{};
	for (u32 y = 0; y < 4; ++y)
	{
		for (u32 x = 0; x < 4; ++x)
		{
			const u32 c = x;
			initial[y * 4 + x] = u32(destination[c][0]) | (u32(destination[c][1]) << 8) |
				(u32(destination[c][2]) << 16) | (u32(destination[c][3]) << 24);
		}
	}
	const GSVector4i rect = target->GetRect();
	const bool cleared = target->Update(rect, initial.data(), 16);
	check(cleared, "Adreno cheap-feedback test initializes exact per-column Cd and Ad bytes");
	if (!cleared)
		return;

	bool prepared = true;
	for (u32 column = 0; column < 4; ++column)
	{
		alignas(32) GSVertex vertices[6]{};
		const u32 x0 = column * 16;
		const u32 x1 = (column + 1) * 16;
		const u32 px[6] = {x0, x1, x0, x1, x1, x0};
		const u32 py[6] = {0, 0, 64, 0, 64, 64};
		for (u32 i = 0; i < 6; ++i)
		{
			vertices[i].RGBAQ.R = source[column][0];
			vertices[i].RGBAQ.G = source[column][1];
			vertices[i].RGBAQ.B = source[column][2];
			vertices[i].RGBAQ.A = 192;
			vertices[i].RGBAQ.Q = 1.0f;
			vertices[i].XYZ.X = px[i];
			vertices[i].XYZ.Y = py[i];
			vertices[i].XYZ.Z = 0x1000000;
		}
		static constexpr u16 indices[6] = {0, 1, 2, 3, 4, 5};
		GSHWDrawConfig config{};
		prepared &= EmuCoreXPrepareAdrenoCheapFeedbackDraw(target.get(), nullptr, vertices, indices, &config,
			0xFF000000, destination[column][3], 2, false, false, true, 128);
		if (!prepared)
			break;
		config.drawarea = config.scissor = GSVector4i(x0 / 16, 0, x1 / 16, 4);
		config.road = GSDecideHWDrawRoad(config, m_features);
		DumpAdrenoCheapFeedbackDraw("golden", "before-draw", vertices[0], config);
		for (u32 i = 0; i < 6; ++i)
			std::fprintf(stderr, "GS_ADRENO_VERTEX draw=golden column=%u i=%u xy=%u,%u color=%u,%u,%u,%u\n",
				column, i, unsigned(vertices[i].XYZ.X), unsigned(vertices[i].XYZ.Y),
				unsigned(vertices[i].RGBAQ.R), unsigned(vertices[i].RGBAQ.G),
				unsigned(vertices[i].RGBAQ.B), unsigned(vertices[i].RGBAQ.A));
		DoRenderHW(config);
		DumpAdrenoCheapFeedbackDraw("golden", "after-draw", vertices[0], config);
	}
	check(prepared, "Adreno pixel fixture obtains four real HW blend configs from GSRendererHW with physical caps");
	if (!prepared)
		return;
	EndRenderPass();
	auto download = GSDownloadTextureVK::Create(4, 4, GSTexture::Format::Color);
	check(bool(download), "Adreno pixel fixture allocates actual Vulkan readback target");
	if (!download)
		return;
	download->CopyFromTexture(rect, target.get(), rect, 0);
	download->Flush();
	const bool mapped = download->Map(rect);
	check(mapped, "Adreno pixel fixture maps the final GS render-target pixels");
	if (!mapped)
		return;
	bool golden = true;
	for (u32 y = 0; y < 4; ++y)
	{
		const u8* row = download->GetMapPointer() + y * download->GetMapPitch();
		for (u32 x = 0; x < 4; ++x)
		{
			const u8* actual = row + x * 4;
			for (u32 channel = 0; channel < 4; ++channel)
			{
				if (actual[channel] != expected[x][channel])
				{
					std::fprintf(stderr,
						"GS_ADRENO_AD_GOLDEN x=%u y=%u actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
						x, y, actual[0], actual[1], actual[2], actual[3], expected[x][0], expected[x][1], expected[x][2], expected[x][3]);
					golden = false;
					break;
				}
			}
		}
	}
	download->Unmap();
	check(golden,
		"Adreno actual Vulkan pixels match (Cs-Cd)*Ad+Cd for Ad64/128/192 source-alpha192 gradient with destination alpha masked");
	EndRenderPass();
	ExecuteCommandBuffer(true);
	InvalidateCachedState();

	// Actual consumer controls use the same renderer stage and real target. Each
	// control draws one full-target quad, then reads the resulting bytes back.
	// Run each control through the Adreno HW stage seam and the real Vulkan
	// consumer, keeping each expected pixel independent of the copy selector.
	auto run_control = [&](u32 frame_mask, u32 destination_alpha, u32 overlap, bool date,
		bool alpha_test, bool alpha_test_pass, const u8 expected_pixel[4], const char* label) {
		std::fprintf(stderr, "GS_ADRENO_CONTROL_PHASE label=%s phase=begin\n", label);
		if (date && (!date_resources_ready || !date_draw_render_pass_ready || !m_point_sampler))
		{
			check(false, label);
			return;
		}
		auto control_target = GSTextureVK::Create(GSTexture::RenderTarget, GSTexture::Format::Color, 4, 4, 1);
		if (!control_target)
		{
			check(false, label);
			return;
		}
		const u32 initial_pixel = 32u | (64u << 8) | (96u << 16) | (destination_alpha << 24);
		std::array<u32, 16> pixels{};
		pixels.fill(initial_pixel);
		if (!control_target->Update(control_target->GetRect(), pixels.data(), 16))
		{
			check(false, label);
			return;
		}
		auto control_depth = date ? GSTextureVK::Create(GSTexture::DepthStencil, GSTexture::Format::DepthStencil, 4, 4, 1) : nullptr;
		if (date && !control_depth)
		{
			check(false, label);
			return;
		}
		if (control_depth)
			ClearDepth(control_depth.get(), 0.0f);
		alignas(32) GSVertex quad[6]{};
		static constexpr u32 px[6] = {0, 128, 0, 128, 128, 0};
		static constexpr u32 py[6] = {0, 0, 128, 0, 128, 128};
		for (u32 i = 0; i < 6; ++i)
		{
			quad[i].RGBAQ.R = 160;
			quad[i].RGBAQ.G = 96;
			quad[i].RGBAQ.B = 32;
			quad[i].RGBAQ.A = 192;
			quad[i].RGBAQ.Q = 1.0f;
			quad[i].XYZ.X = px[i];
			quad[i].XYZ.Y = py[i];
			quad[i].XYZ.Z = 0x1000000;
		}
		static constexpr u16 quad_indices[6] = {0, 1, 2, 3, 4, 5};
		GSHWDrawConfig control_config{};
		bool ok = EmuCoreXPrepareAdrenoCheapFeedbackDraw(control_target.get(), control_depth.get(), quad, quad_indices,
			&control_config, frame_mask, destination_alpha, overlap, date, alpha_test, alpha_test_pass, 128);
		if (ok)
		{
			control_config.drawarea = control_config.scissor = control_target->GetRect();
			control_config.road = GSDecideHWDrawRoad(control_config, m_features);
			if (overlap == 1)
			{
				const bool expected_basic_factors = control_config.blend.enable &&
					control_config.blend.src_factor == GSDevice::DST_ALPHA &&
					control_config.blend.dst_factor == GSDevice::INV_DST_ALPHA &&
					control_config.blend.op == GSDevice::OP_ADD;
				check(expected_basic_factors,
					"Adreno overlap control uses the actual Basic DST_ALPHA blend approximation");
				ok &= expected_basic_factors;
			}
			DumpAdrenoCheapFeedbackDraw(label, "before-draw", quad[0], control_config);
			DoRenderHW(control_config);
			DumpAdrenoCheapFeedbackDraw(label, "after-first-draw", quad[0], control_config);
			// Model a second overlapping primitive batch only in the YES control.
			// The two triangles in the quad share an edge and are not themselves
			// an overlap test; a second full-cover draw provides the overlap.
			if (overlap == 1)
			{
				DoRenderHW(control_config);
				DumpAdrenoCheapFeedbackDraw(label, "after-overlap-draw", quad[0], control_config);
			}
			EndRenderPass();
			auto control_download = GSDownloadTextureVK::Create(4, 4, GSTexture::Format::Color);
			ok = bool(control_download);
			if (ok)
			{
				const GSVector4i control_rect = control_target->GetRect();
				control_download->CopyFromTexture(control_rect, control_target.get(), control_rect, 0);
				control_download->Flush();
				ok = control_download->Map(control_rect);
				if (ok)
				{
					const u8* actual = control_download->GetMapPointer();
					for (u32 channel = 0; channel < 4; ++channel)
						ok &= actual[channel] == expected_pixel[channel];
					if (!ok)
						std::fprintf(stderr, "GS_ADRENO_CONTROL label=%s actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
							label, actual[0], actual[1], actual[2], actual[3], expected_pixel[0], expected_pixel[1], expected_pixel[2], expected_pixel[3]);
					control_download->Unmap();
				}
			}
		}
		EndRenderPass();
		ExecuteCommandBuffer(true);
		InvalidateCachedState();
		check(ok, label);
		std::fprintf(stderr, "GS_ADRENO_CONTROL_PHASE label=%s phase=end ok=%u\n", label, unsigned(ok));
	};

	static constexpr u8 alpha_pass[4] = {96, 80, 64, 64};
	static constexpr u8 alpha_fail[4] = {32, 64, 96, 64};
	static constexpr u8 fbmask_control[4] = {96, 80, 96, 64};
	// The actual Basic stage emits DST_ALPHA / (1-DST_ALPHA), with preserved
	// destination alpha 64/255. Applying that exact factor twice to Cs=(160,96,32)
	// and Cd=(32,64,96), quantizing each UNORM draw, yields these bytes.
	static constexpr u8 overlap_control[4] = {88, 78, 68, 64};
	static constexpr u8 date_pass[4] = {160, 96, 32, 128};
	static constexpr u8 date_fail[4] = {32, 64, 96, 64};
	run_control(0xFF000000, 64, 2, false, true, true, alpha_pass,
		"Adreno real pixels preserve alpha-test pass while applying Basic Ad blending");
	run_control(0xFF000000, 64, 2, false, true, false, alpha_fail,
		"Adreno real pixels preserve alpha-test fail and leave the destination unchanged");
	run_control(0xFFFF0000, 64, 2, false, false, true, fbmask_control,
		"Adreno real pixels preserve FBMASK-selected blue and alpha channels");
	run_control(0xFF000000, 64, 1, false, false, true, overlap_control,
		"Adreno real pixels preserve the overlap-YES two-triangle blend result");
	run_control(0xFF000000, 128, 2, true, false, true, date_pass,
		"Adreno real pixels preserve DATE pass and Basic Ad blending on supported depth-stencil targets");
	run_control(0xFF000000, 64, 2, true, false, true, date_fail,
		"Adreno real pixels preserve DATE fail without modifying destination color");

	std::printf("GS_ADRENO_AD_PIXEL_GOLDEN actual_vk=1 vendor=%04x copy_road=1 cheap_rt_feedback_read=%u dual_src=%u columns=4 alpha=64,128,192\n",
		m_device_properties.vendorID, unsigned(m_features.cheap_rt_feedback_read), unsigned(m_features.dual_source_blend));
}
