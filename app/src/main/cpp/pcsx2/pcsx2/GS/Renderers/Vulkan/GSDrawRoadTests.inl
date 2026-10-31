// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// This fixture reaches the real Vulkan selector and active-pass carry seam;
// capabilities are restored before any real Vulkan resource use.
void GSDeviceVK::TestDrawRoad(void (*check)(bool, const char*))
{
    const auto old_features = m_features;
    const auto old_extensions = m_optional_extensions;
    ScopedGuard restore_features = [&]() { m_features = old_features; m_optional_extensions = old_extensions; };
    const auto road = [&](GSHWDrawConfig& conf) { conf.road = GSDecideHWDrawRoad(conf, m_features); };
    auto target = GSTextureVK::Create(GSTexture::FeedbackTarget, GSTexture::Format::Color, 16, 16, 1);
    auto other = GSTextureVK::Create(GSTexture::FeedbackTarget, GSTexture::Format::Color, 16, 16, 1);
    auto depth = GSTextureVK::Create(GSTexture::FeedbackDepth, GSTexture::Format::DepthStencil, 16, 16, 1);
    auto other_depth = GSTextureVK::Create(GSTexture::FeedbackDepth, GSTexture::Format::DepthStencil, 16, 16, 1);
    check(target && other && depth && other_depth, "GS draw road allocates isolated real attachments");
    if (!target || !other || !depth || !other_depth) return;
    EndRenderPass();
    // Synthetic road combinations exercise CPU selection only. Unsupported
    // extension/feature combinations never reach Vulkan pipeline/pass creation.
    for (u32 policy = 0; policy < 3; ++policy)
    {
        m_features.texture_barrier = policy != 0;
        m_features.framebuffer_fetch = policy == 1;
        m_features.framebuffer_fetch_orders_overlap = policy == 1;
        m_features.feedback_loop_layout = policy == 2;
        m_features.feedback_carry = policy == 1 ? GSFeedbackCarry::NonReaders : GSFeedbackCarry::None;
        m_optional_extensions.vk_ext_rasterization_order_attachment_access = policy == 1;
        m_optional_extensions.vk_ext_attachment_feedback_loop_layout = policy == 2;
        GSHWDrawConfig conf{};
        conf.rt = target.get(); conf.ds = depth.get();
        conf.ps.tex_is_fb = true; conf.require_one_barrier = true;
        road(conf);
        PipelineSelector pipe{};
        UpdateHWPipelineSelector(conf, pipe);
        check(pipe.IsRTFeedbackLoop() == (policy != 0) && !pipe.IsTestingAndSamplingDepth(),
            "GS renderer road selects only actual color read even on mobile GPUs");
        conf.require_one_barrier = false;
        road(conf); UpdateHWPipelineSelector(conf, pipe);
        check(pipe.IsRTFeedbackLoop() == (policy != 0),
            "GS ordered reader retains feedback pipeline after dropping its barrier");
        conf.ps.tex_is_fb = false; conf.tex = depth.get();
        road(conf); UpdateHWPipelineSelector(conf, pipe);
        check(!pipe.IsRTFeedbackLoop() && pipe.IsTestingAndSamplingDepth() && !pipe.IsDepthFeedbackLoop(),
            "GS depth sampler road is distinct from a writable depth loop");
        conf.tex = nullptr; conf.require_one_barrier = false;
        road(conf); UpdateHWPipelineSelector(conf, pipe);
        check(pipe.feedback_loop_flags == FeedbackLoopFlag_None,
            "GS non-reader starts draw-local until a compatible open pass can carry");
        check(conf.road.carry_rt == (policy == 1) && conf.road.carry_depth == (policy == 1),
            "GS only actual Mali fetch policy carries non-reader state");
        conf.depth.zwe = true; road(conf);
        check(!conf.road.carry_depth, "GS main-pass depth writer excludes inherited depth bits");
        conf.depth.zwe = false; conf.alpha_second_pass.enable = true; conf.alpha_second_pass.depth.zwe = true; road(conf);
        check(!conf.road.carry_depth, "GS alpha-second-pass depth writer excludes inherited depth bits");
        conf.alpha_second_pass.depth.zwe = false; conf.alpha_second_pass.require_one_barrier = true; road(conf);
        check(!conf.road.carry_rt && !conf.road.carry_depth,
            "GS alpha-second-pass reader supplies its own synchronization instead of carrying");
        const auto dev = GSGetDrawRoadDevice(m_features);
        check(GSOffsetSelfReadNeedsCopy(dev) == (policy == 1),
            "GS offset sampler self-read snapshots only actual in-tile fetch road");
        check(!GSDrawDropsBarriers(dev, true, false, false) && !GSDrawDropsBarriers(dev, false, false, true),
            "GS offset and direct-depth reads preserve barriers on every road");
        check(GSDrawDropsBarriers(dev, false, true, false) == (policy == 1),
            "GS overlapping primitives drop barriers only with actual ordering support");
        conf = {}; conf.rt = target.get(); conf.ps.tex_is_fb = true; conf.require_one_barrier = true;
        road(conf); check(conf.road.clone_rt == (policy == 0), "GS copy road clones a one-barrier color reader");
        conf.offset_read_hits_write = true; road(conf);
        check(conf.road.clone_rt, "GS filtered offset overlap snapshots on every road");
        conf = {}; conf.rt = target.get(); conf.ds = depth.get();
        conf.alpha_second_pass.enable = true; conf.alpha_second_pass.ps.tex_is_fb = true;
        road(conf); UpdateHWPipelineSelector(conf, pipe);
        check(pipe.IsRTFeedbackLoop() == (policy != 0),
            "GS alpha-only color reader declares a compatible feedback pass before either pass");
        conf.alpha_second_pass.require_one_barrier = true; road(conf);
        check(conf.road.clone_rt == (policy == 0),
            "GS copy road snapshots an alpha-only destination reader before the primary pass");
        check(GSNeedsRTSnapshotBinding(conf),
            "GS alpha-only destination reader retains its RT snapshot binding for both passes");
        conf = {}; conf.rt = target.get(); conf.ds = depth.get();
        conf.ps.tex_is_fb = true; conf.ps.depth_feedback = true;
        conf.ps.rov_color = true; conf.ps.rov_depth = GSHWDrawConfig::PS_ROV_DEPTH::READ_ONLY;
        road(conf); UpdateHWPipelineSelector(conf, pipe);
        check(!pipe.rt && !pipe.ds && pipe.feedback_loop_flags == FeedbackLoopFlag_None,
            "GS ROV storage-image readers never declare removed attachment input loops");
        conf = {}; conf.road.rt_loop = conf.road.depth_loop = true;
        UpdateHWPipelineSelector(conf, pipe);
        check(pipe.feedback_loop_flags == FeedbackLoopFlag_None,
            "GS absent attachments cannot acquire feedback pipeline flags from stale road bits");
    }
    check(!GSUsesInTileFeedbackRoad(true, false),
        "GS explicit texture-barrier-off fallback disables the in-tile fetch road");
    check(GSUsesInTileFeedbackRoad(true, true) && !GSUsesInTileFeedbackRoad(false, true),
        "GS actual in-tile selection retains real availability and enabled-barrier requirements");
    // Actual native capabilities from here onward, including ordinary LOAD,
    // attachment transitions and pass end. No synthetic driver facts reach GPU.
    m_features = old_features; m_optional_extensions = old_extensions;
    const GSVector4i rect(0, 0, 16, 16);
    const auto pass = GetRenderPass(LookupNativeFormat(GSTexture::Format::Color),
        LookupNativeFormat(GSTexture::Format::DepthStencil), VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);
    const auto seed = [&]() {
        OMSetRenderTargets(target.get(), depth.get(), rect);
        BeginRenderPass(pass, rect);
        // The carry seam reads semantic state; no feedback draw is issued in
        // this ordinary pass. Restore these flags before any real render call.
        m_current_framebuffer_feedback_loop = static_cast<FeedbackLoopFlag>(
            FeedbackLoopFlag_ReadAndWriteRT | FeedbackLoopFlag_ReadDepth);
    };
    const auto select = [&](GSTextureVK* rt, GSTextureVK* ds, bool carry_rt, bool carry_depth) {
        GSHWDrawConfig conf{}; conf.rt = rt; conf.ds = ds;
        conf.road.carry_rt = carry_rt; conf.road.carry_depth = carry_depth;
        PipelineSelector pipe{};
        pipe.rt = rt != nullptr; pipe.ds = ds != nullptr;
        ApplyHWDrawFeedbackCarry(conf, pipe, rt, ds);
        return pipe.feedback_loop_flags;
    };
    seed();
    {
        GSHWDrawConfig plain{};
        PipelineSelector pipe{};
        auto* old_depth = m_current_depth_target;
        m_current_depth_target = nullptr;
        check(!HWDrawNeedsAttachmentRestart(plain, pipe),
            "GS textureless color-only draw never aliases a missing depth attachment");
        m_current_depth_target = old_depth;
        auto* old_color = m_current_render_target;
        m_current_render_target = nullptr;
        check(!HWDrawNeedsAttachmentRestart(plain, pipe),
            "GS textureless depth-only draw never aliases a missing color attachment");
        m_current_render_target = old_color;
        plain.tex = target.get();
        check(HWDrawNeedsAttachmentRestart(plain, pipe),
            "GS actual color sampler alias still requests attachment restart");
        plain.tex = depth.get();
        check(HWDrawNeedsAttachmentRestart(plain, pipe),
            "GS actual depth sampler alias still requests attachment restart");
        plain.tex = other.get();
        check(!HWDrawNeedsAttachmentRestart(plain, pipe),
            "GS unrelated sampled texture preserves its compatible pass");
    }
    check(select(target.get(), depth.get(), true, true) ==
        (FeedbackLoopFlag_ReadAndWriteRT | FeedbackLoopFlag_ReadDepth),
        "GS compatible active attachments independently carry matching color/depth bits");
    check(select(other.get(), depth.get(), true, true) == FeedbackLoopFlag_ReadDepth,
        "GS switched color target never inherits old color feedback state");
    check(select(target.get(), other_depth.get(), true, true) == FeedbackLoopFlag_ReadAndWriteRT,
        "GS switched depth target never inherits old depth feedback state");
    check(select(target.get(), depth.get(), true, false) == FeedbackLoopFlag_ReadAndWriteRT,
        "GS depth writer drops depth carry without dropping compatible color carry");
    check(select(target.get(), depth.get(), false, false) == FeedbackLoopFlag_None,
        "GS draw-local policy never broad-ORs old feedback flags");
    GSHWDrawConfig clone{}; clone.road.carry_rt = clone.road.carry_depth = true; clone.road.clone_rt = true;
    PipelineSelector clone_pipe{};
    ApplyHWDrawFeedbackCarry(clone, clone_pipe, target.get(), depth.get());
    check(clone_pipe.feedback_loop_flags == FeedbackLoopFlag_None, "GS clone boundary rejects all inherited feedback state");
    target->SetState(GSTexture::State::Cleared);
    check(select(target.get(), depth.get(), true, true) == FeedbackLoopFlag_ReadDepth,
        "GS pending color clear drops color carry independently");
    target->SetState(GSTexture::State::Dirty);
    depth->SetState(GSTexture::State::Cleared);
    check(select(target.get(), depth.get(), true, true) == FeedbackLoopFlag_ReadAndWriteRT,
        "GS pending depth clear drops depth carry independently");
    depth->SetState(GSTexture::State::Dirty);
    m_current_framebuffer_feedback_loop = FeedbackLoopFlag_None;
    EndRenderPass();
    check(select(target.get(), depth.get(), true, true) == FeedbackLoopFlag_None,
        "GS closed pass cannot leak feedback carry through stale attachment pointers");
    seed();
    CopyRect(target.get(), other.get(), rect, 0, 0);
    check(!InRenderPass() && select(target.get(), depth.get(), true, true) == FeedbackLoopFlag_None,
        "GS source-copy boundary closes the pass and drops stale feedback carry");
    seed();
    m_current_framebuffer_feedback_loop = FeedbackLoopFlag_None;
    ExecuteCommandBufferAndRestartRenderPass(false, "GS draw road restart boundary");
    check(select(target.get(), depth.get(), true, true) == FeedbackLoopFlag_None,
        "GS command restart uses restored pass state rather than stale feedback flags");
    EndRenderPass();
    OMSetRenderTargets(nullptr, nullptr, rect);
}
