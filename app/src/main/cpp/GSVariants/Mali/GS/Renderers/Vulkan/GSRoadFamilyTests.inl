// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native policy and actual builder/command captures. Synthetic unsupported
// capabilities never reach the real Vulkan driver.
namespace GSRoadFamilyOracle
{
struct PipelineCapture { VkPipelineCreateFlags flags = 0; VkPipelineColorBlendStateCreateFlags blend = 0; std::vector<VkDynamicState> dynamic; };
static thread_local PipelineCapture* pipeline = nullptr;
static thread_local std::vector<u32>* commands = nullptr;
static VKAPI_ATTR VkResult VKAPI_CALL CapturePipeline(VkDevice, VkPipelineCache, u32 count,
    const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks*, VkPipeline* result)
{
    if (count != 1) return VK_ERROR_UNKNOWN;
    pipeline->flags = info->flags;
    pipeline->blend = info->pColorBlendState ? info->pColorBlendState->flags : 0;
    pipeline->dynamic.clear();
    if (info->pDynamicState) pipeline->dynamic.assign(info->pDynamicState->pDynamicStates,
        info->pDynamicState->pDynamicStates + info->pDynamicState->dynamicStateCount);
    *result = reinterpret_cast<VkPipeline>(uintptr_t(0x76123000));
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL CaptureLoop(VkCommandBuffer, VkImageAspectFlags aspects) { commands->push_back(aspects); }
static VKAPI_ATTR void VKAPI_CALL CaptureDraw(VkCommandBuffer, u32, u32, u32, s32, u32) { commands->push_back(0xf0000000); }
}

void GSDeviceVK::TestRoadFamilies(void (*check)(bool, const char*))
{
    using namespace GSVulkanRoad;
    using namespace GSRoadFamilyOracle;
    const auto caps = [] { Capabilities c; c.roaa = c.layout = c.dynamic_state = c.dynamic_command = c.push_descriptors = true; c.max_push_descriptors = 7; return c; };
    struct Expected { bool barrier, fetch, layout; GSFeedbackCarry carry; bool dynamic, raster, push; };
    const auto test = [&](const char* label, Identity id, Capabilities c, Expected expected) {
        const auto facts = ResolveFacts(id, c.roaa, c.max_push_descriptors);
        const auto selected = ResolveSelection(facts, c);
        const bool layout = selected.road.spelling == GSSelfReadSpelling::FeedbackLoopLayout;
        check(selected.road.texture_barrier == expected.barrier && selected.road.in_tile_read == expected.fetch &&
            layout == expected.layout && selected.carry == expected.carry && selected.dynamic_loop == expected.dynamic &&
            selected.raster_order_on_overlap == expected.raster && selected.push_descriptors == expected.push, label);
    };
    constexpr auto none = GSFeedbackCarry::None, nonread = GSFeedbackCarry::NonReaders, all = GSFeedbackCarry::All;
    Identity mali{0x13b5, Driver::Arm, "Mali-G57", "Arm", "v1.r44p1-malisx2.0.3.0", "", false};
    auto c = caps(); c.mobile_profile = true;
    test("GPU road custom Mali retains exact ROAA/input carry and seven push descriptors", mali, c, {true,true,false,nonread,false,false,true});
    auto stock = mali; stock.info = "v1.r44p1";
    test("GPU road stock r44p1 G57 uses its crash-safe copy workaround", stock, c, {false,false,false,none,false,false,false});
    auto mt6897 = stock; mt6897.name = "Mali-G615"; mt6897.platform = "ro.board.platform=MT6897"; mt6897.mediatek = true;
    test("GPU road measured MT6897 exempts the r44p1 copy and MediaTek fetch denies", mt6897, c, {true,true,false,nonread,false,false,false});
    auto healthy = stock; healthy.name = "Mali-G76"; healthy.info = "r32p0";
    test("GPU road healthy stock Mali may use advertised ROAA without a custom tag", healthy, c, {true,true,false,nonread,false,false,false});
    c.texture_barriers = 0;
    test("GPU road explicit barriers off wins over custom fetch and declared facts", mali, c, {false,false,false,none,false,false,true});
    c = caps(); c.mobile_profile = true; c.disable_fetch = true;
    test("GPU road disabled Mali fetch retains the r44p1 input/barrier spelling", mali, c, {true,false,false,none,false,false,true});
    c = caps(); c.max_push_descriptors = 6; c.mobile_profile = true;
    test("GPU road Mali push descriptor count below seven safely falls back", mali, c, {true,true,false,nonread,false,false,false});
    Identity turnip{0x5143, Driver::Turnip, "Adreno (TM) 650", "turnip", "Mesa 26.2.0", "", false};
    c = caps();
    test("GPU road untagged Turnip a650 retains known-safe copy with trusted push descriptors", turnip, c, {false,false,false,none,false,false,true});
    turnip.info = "Mesa 26.2.0 (git-axfl1-005)";
    test("GPU road tagged Turnip a650 declares ordered sampler loop dynamically", turnip, c, {true,false,true,nonread,true,false,true});
    auto a610 = turnip; a610.name = "Adreno 610";
    test("GPU road a610 never inherits a650 ordering from the same driver tag", a610, c, {false,false,false,none,false,false,true});
    auto a740 = turnip; a740.name = "Adreno 740"; a740.info = "Mesa 26.2.0";
    test("GPU road stock Turnip a740 declares sampler loop but keeps barriers", a740, c, {true,false,true,nonread,true,false,true});
    auto a722 = a740; a722.name = "Adreno 722"; a722.info = "Mesa (git-axfl2-004)";
    test("GPU road a722 retains barriers even with the generation two ordering tag", a722, c, {true,false,true,nonread,true,false,true});
    a740.info = "Mesa (git-axfl2-004)";
    test("GPU road generation two a740 requests overlap ordering per pipeline", a740, c, {true,false,true,nonread,true,true,true});
    auto missing = c; missing.roaa = false;
    test("GPU road generation two without ROAA capability keeps a740 barriers", a740, missing, {true,false,true,nonread,true,false,true});
    missing = c; missing.layout = false;
    test("GPU road absent feedback layout cannot enable a declared loop", a740, missing, {false,false,false,none,false,false,true});
    missing = c; missing.dynamic_state = false;
    test("GPU road absent dynamic extension uses compatible static-create declaration", a740, missing, {true,false,true,nonread,false,true,true});
    missing = c; missing.dynamic_command = false;
    test("GPU road missing dynamic PFN also uses static-create declaration", a740, missing, {true,false,true,nonread,false,true,true});
    missing = c; missing.texture_barriers = 0;
    test("GPU road explicit copy overrides tagged Turnip driver facts", a740, missing, {false,false,false,none,false,false,true});
    auto hinted_turnip = a740; hinted_turnip.driver = Driver::Unknown; hinted_turnip.driver_name = "Turnip";
    test("GPU unknown raw driver ID may recognize a Turnip tag but never inherits dynamic or push trust", hinted_turnip, c,
        {true,false,true,nonread,false,true,false});
    auto proprietary = a740; proprietary.driver = Driver::Qualcomm; proprietary.driver_name = "Qualcomm";
    test("GPU road Qualcomm proprietary ignores a Mesa-looking ordering tag", proprietary, c, {false,false,false,none,false,false,true});
    proprietary.name = "Adreno 830";
    test("GPU road Adreno eight proprietary never trusts stale ROAA reads", proprietary, c, {false,false,false,none,false,false,true});
    Identity img{0x1010, Driver::Imagination, "PowerVR CXTP-48-1536", "Imagination", "1.0", "", false};
    missing = c; missing.roaa = false; missing.mobile_profile = true;
    test("GPU road proprietary PowerVR uses coherent input read without ROAA or sampler layout", img, missing, {true,true,false,nonread,false,false,true});
    auto hinted_img = img; hinted_img.driver = Driver::Unknown;
    test("GPU unknown raw PowerVR driver ID never inherits proprietary input-ordering guarantee", hinted_img, missing,
        {true,false,true,none,false,false,true});
    auto pvrmesa = img; pvrmesa.driver = Driver::MesaPowerVR;
    test("GPU road Mesa PowerVR retains its sampler barrier road without proprietary assumptions", pvrmesa, missing, {true,false,true,none,false,false,true});
    Identity honey{0x106b, Driver::Honeykrisp, "Apple M2", "Honeykrisp", "Mesa", "", false};
    missing = c; missing.roaa = false;
    test("GPU road Honeykrisp measured sampler road carries non-readers and declares dynamically", honey, missing, {true,false,true,nonread,true,false,true});
    auto hinted_honey = honey; hinted_honey.driver = Driver::Unknown;
    test("GPU Honeykrisp name without raw driver ID never inherits measured carry or dynamic defaults", hinted_honey, missing,
        {true,false,true,none,false,false,true});
    Identity v3d{0x14e4, Driver::Unknown, "V3D", "V3DV", "Mesa", "", false};
    test("GPU road Broadcom retains upstream all-draw carry policy", v3d, missing, {true,false,true,all,false,false,true});
    Identity desktop{0x10de, Driver::Unknown, "NVIDIA", "NVIDIA", "", "", false};
    test("GPU road unmeasured desktop sampler path retains static declaration and draw-local flags", desktop, missing, {true,false,true,none,false,false,true});
    auto samsung = desktop; samsung.vendor = 0x144d; samsung.name = "Xclipse";
    test("GPU road Samsung never claims advertised ROAA fetch", samsung, c, {true,false,false,none,false,false,true});
    bool tags = FixGeneration("Mesa (git-axfl2-004)") == 2;
    for (auto invalid : {"Mesa (git-armsx2-001)", "xxgit-axfl2-004", "git-axfl0-005", "git-axfl20000-005", "git-axfl2x-005"}) tags &= FixGeneration(invalid) == 0;
    check(tags, "GPU road ordering tag parser rejects malformed legacy embedded and overflowing tags");

    // Intercept only builders/commands under synthetic capabilities. Fake
    // handles are never cached, bound or destroyed as real GPU resources.
    const auto old_features = m_features; const auto old_extensions = m_optional_extensions;
    const auto old_force = m_force_feedback_loop_layout, old_dynamic = m_declare_loop_per_draw, old_declared = m_loop_declared_in_pass;
    const auto old_pipe = m_pipeline_selector;
    const auto old_create = vkCreateGraphicsPipelines; const auto old_set = vkCmdSetAttachmentFeedbackLoopEnableEXT;
    const auto old_draw = vkCmdDrawIndexed;
    ScopedGuard restore = [&]() {
        m_features = old_features; m_optional_extensions = old_extensions;
        m_force_feedback_loop_layout = old_force; m_declare_loop_per_draw = old_dynamic; m_loop_declared_in_pass = old_declared;
        m_pipeline_selector = old_pipe;
        vkCreateGraphicsPipelines = old_create; vkCmdSetAttachmentFeedbackLoopEnableEXT = old_set; vkCmdDrawIndexed = old_draw;
        pipeline = nullptr; commands = nullptr;
    };
    PipelineCapture captured; pipeline = &captured;
    vkCreateGraphicsPipelines = CapturePipeline;
    for (u32 policy = 0; policy < 4; ++policy)
    {
        m_features.texture_barrier = true;
        m_features.framebuffer_fetch = policy == 0 || policy == 1;
        m_features.declared_loop_overlap_needs_raster_order = policy == 3;
        m_optional_extensions.vk_ext_rasterization_order_attachment_access = policy != 1;
        m_optional_extensions.vk_ext_attachment_feedback_loop_layout = policy >= 2;
        m_force_feedback_loop_layout = policy >= 2;
        m_declare_loop_per_draw = policy == 3;
        PipelineSelector selected{}; selected.rt = true;
        selected.feedback_loop_flags = FeedbackLoopFlag_ReadAndWriteRT;
        selected.raster_order = policy == 3;
        Vulkan::GraphicsPipelineBuilder builder;
        ConfigureTFXFeedbackPipeline(builder, selected);
        const auto fake = builder.Create(m_device);
        const bool dynamic = std::find(captured.dynamic.begin(), captured.dynamic.end(), VK_DYNAMIC_STATE_ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT) != captured.dynamic.end();
        const bool roaa = (captured.blend & VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT) != 0;
        const bool flag = (captured.flags & VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT) != 0;
        check(fake && roaa == (policy == 0 || policy == 3) && dynamic == (policy == 3) && flag == (policy == 2),
            "GPU actual pipeline builder agrees on Mali ROAA PowerVR input static sampler and requested dynamic overlap schemas");
    }
    m_features.framebuffer_fetch = false;
    m_optional_extensions.vk_ext_rasterization_order_attachment_access = true;
    m_optional_extensions.vk_ext_attachment_feedback_loop_layout = true;
    m_force_feedback_loop_layout = false;
    check(GetFeedbackPath() == VulkanFeedbackPath::InputAttachment,
        "GPU available ROAA retains reference input spelling unless a driver fact forces sampler layout");
    m_force_feedback_loop_layout = true;
    check(GetFeedbackPath() == VulkanFeedbackPath::AttachmentFeedbackLoopLayout,
        "GPU declared-loop driver fact overrides the input spelling before resources are built");

    // Real Draw(config,offset,count) seam; capture consumes unsupported dynamic
    // commands and the draw itself rather than forwarding fake capabilities.
    std::vector<u32> writes; commands = &writes;
    vkCmdSetAttachmentFeedbackLoopEnableEXT = CaptureLoop; vkCmdDrawIndexed = CaptureDraw;
    m_declare_loop_per_draw = true; m_loop_declared_in_pass = false;
    m_pipeline_selector = {}; m_pipeline_selector.feedback_loop_flags = FeedbackLoopFlag_ReadAndWriteRT;
    GSHWDrawConfig conf{}; conf.ps.tex_is_fb = true;
    m_pipeline_selector.ps = conf.ps;
    const auto first = [&]() { writes.clear(); Draw(conf, 0, 0); return writes; };
    check(first() == std::vector<u32>({0, VK_IMAGE_ASPECT_COLOR_BIT, 0xf0000000}),
        "GPU actual draw declares zero then color before the first reader of a pass");
    check(first() == std::vector<u32>({VK_IMAGE_ASPECT_COLOR_BIT, 0xf0000000}),
        "GPU actual draw reapplies dynamic feedback after every pipeline bind and split group");
    conf.ps.tex_is_fb = false; m_pipeline_selector.ps = conf.ps;
    check(first() == std::vector<u32>({0, 0xf0000000}),
        "GPU carried non-reader explicitly declares no sampled color feedback");
    m_pipeline_selector.feedback_loop_flags = FeedbackLoopFlag_None;
    check(first() == std::vector<u32>({0xf0000000}),
        "GPU statically non-feedback pipeline never receives an illegal dynamic setter");
    m_pipeline_selector.feedback_loop_flags = FeedbackLoopFlag_ReadDepth;
    check(first() == std::vector<u32>({VK_IMAGE_ASPECT_DEPTH_BIT, 0xf0000000}),
        "GPU depth sampler declaration preserves the attached depth aspect");
    m_declare_loop_per_draw = false;
    check(first() == std::vector<u32>({0xf0000000}),
        "GPU static-create fallback issues no dynamic command");

    // Real supported native pass boundaries reset the declaration even when
    // command-buffer restart reuses a compatible framebuffer and pipeline.
    m_features = old_features; m_optional_extensions = old_extensions;
    m_force_feedback_loop_layout = old_force; m_declare_loop_per_draw = old_dynamic;
    auto target = GSTextureVK::Create(GSTexture::RenderTarget, GSTexture::Format::Color, 16, 16, 1);
    check(target != nullptr, "GPU dynamic lifecycle allocates a real supported target");
    if (!target) return;
    const GSVector4i rect(0, 0, 16, 16);
    const auto pass = GetRenderPass(LookupNativeFormat(GSTexture::Format::Color), VK_FORMAT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);
    OMSetRenderTargets(target.get(), nullptr, rect);
    m_loop_declared_in_pass = true;
    BeginRenderPass(pass, rect);
    check(!m_loop_declared_in_pass, "GPU real pass begin resets first-reader dynamic declaration tracking");
    m_loop_declared_in_pass = true;
    ExecuteCommandBufferAndRestartRenderPass(false, "GPU dynamic declaration restart");
    check(!m_loop_declared_in_pass, "GPU real command restart requires a fresh first-reader zero pulse");
    m_loop_declared_in_pass = true;
    EndRenderPass();
    check(!m_loop_declared_in_pass, "GPU real pass end discards prior dynamic declaration state");
    OMSetRenderTargets(nullptr, nullptr, rect);
}
