// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Included only in native selftest builds. Hooks are scoped to the isolated
// fixture; structural fake passes never reach the driver or any framebuffer.
namespace GSOrderingOracle
{
struct PassDescription
{
    std::vector<VkSubpassDependency> dependencies;
    u32 inputs = 0;
};
struct ImageDependency
{
    VkPipelineStageFlags source, destination;
    VkDependencyFlags flags;
    VkImageMemoryBarrier image;
};
static thread_local PassDescription* capturedPass = nullptr;
static thread_local std::vector<ImageDependency>* capturedBarriers = nullptr;
static PFN_vkCmdPipelineBarrier realBarrier = nullptr;

static VKAPI_ATTR VkResult VKAPI_CALL CapturePass(VkDevice, const VkRenderPassCreateInfo* info,
    const VkAllocationCallbacks*, VkRenderPass* pass)
{
    capturedPass->dependencies.clear();
    if (info->dependencyCount)
        capturedPass->dependencies.assign(info->pDependencies, info->pDependencies + info->dependencyCount);
    capturedPass->inputs = info->pSubpasses[0].inputAttachmentCount;
    *pass = reinterpret_cast<VkRenderPass>(static_cast<uintptr_t>(0x12340000));
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL CaptureBarrier(VkCommandBuffer command,
    VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags flags,
    u32 memory_count, const VkMemoryBarrier* memory, u32 buffer_count, const VkBufferMemoryBarrier* buffers,
    u32 image_count, const VkImageMemoryBarrier* images)
{
    if (capturedBarriers)
        for (u32 i = 0; i < image_count; ++i)
            capturedBarriers->push_back({source, destination, flags, images[i]});
    realBarrier(command, source, destination, flags, memory_count, memory, buffer_count, buffers, image_count, images);
}
}

void GSDeviceVK::TestRenderPassOrdering(void (*check)(bool, const char*), VkPipeline sampled_pipeline,
    VkSampler sampler, VkRenderPass sampled_pass)
{
    using namespace GSOrderingOracle;
    check(m_tfx_preparation_device && m_allocator, "GS ordering uses the initialized isolated restart fixture");
    if (!m_tfx_preparation_device || !m_allocator) return;
    const VkFormat color_format = LookupNativeFormat(GSTexture::Format::Color);
    const VkFormat depth_format = LookupNativeFormat(GSTexture::Format::DepthStencil);
    // Inspect the real builder, including unsupported policy combinations, by
    // intercepting creation. Preserve every preexisting real cache entry.
    {
        const auto old_features = m_features;
        const auto old_extensions = m_optional_extensions;
        const auto old_create = vkCreateRenderPass;
        PassDescription description;
        capturedPass = &description;
        vkCreateRenderPass = CapturePass;
        ScopedGuard restore = [&]() {
            vkCreateRenderPass = old_create;
            capturedPass = nullptr;
            m_features = old_features;
            m_optional_extensions = old_extensions;
        };
        for (u32 policy = 0; policy < 3; ++policy)
        for (bool color : {false, true}) for (bool depth : {false, true})
        for (bool color_read : {false, true}) for (bool depth_read : {false, true})
        {
            if ((!color && color_read) || (!depth && depth_read)) continue;
            const bool sampler_path = policy == 2;
            m_features.texture_barrier = true;
            m_features.framebuffer_fetch = policy == 1;
            m_features.depth_feedback = DepthFeedbackSupport::Depth;
            m_optional_extensions.vk_ext_rasterization_order_attachment_access = policy != 2;
            m_optional_extensions.vk_ext_attachment_feedback_loop_layout = sampler_path;
            RenderPassCacheKey key{};
            key.color_format = color ? color_format : VK_FORMAT_UNDEFINED;
            key.depth_format = depth ? depth_format : VK_FORMAT_UNDEFINED;
            key.color_load_op = key.depth_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
            key.color_store_op = key.depth_store_op = VK_ATTACHMENT_STORE_OP_STORE;
            key.stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            key.stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            key.color_feedback_loop = color_read;
            key.depth_sampling = depth_read;
            std::optional<VkRenderPass> old;
            if (auto it = m_render_pass_cache.find(key.key); it != m_render_pass_cache.end())
            { old = it->second; m_render_pass_cache.erase(it); }
            description = {};
            const auto pass = CreateCachedRenderPass(key);
            m_render_pass_cache.erase(key.key);
            if (old) m_render_pass_cache.emplace(key.key, *old);
            VkSubpassDependency expected{};
            expected.srcSubpass = VK_SUBPASS_EXTERNAL;
            expected.dstSubpass = 0;
            expected.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            if (color)
            {
                expected.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                expected.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            }
            if (depth)
            {
                expected.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                expected.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
            expected.dstStageMask = expected.srcStageMask;
            expected.dstAccessMask = expected.srcAccessMask;
            if (color_read || depth_read)
            {
                expected.dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                expected.dstAccessMask |= sampler_path ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
            }
            u32 incoming = 0, depth_self = 0, color_self = 0;
            bool exact = true;
            for (const auto& dep : description.dependencies)
            {
                if (dep.srcSubpass == VK_SUBPASS_EXTERNAL && dep.dstSubpass == 0)
                {
                    ++incoming;
                    exact &= dep.srcStageMask == expected.srcStageMask && dep.dstStageMask == expected.dstStageMask &&
                        dep.srcAccessMask == expected.srcAccessMask && dep.dstAccessMask == expected.dstAccessMask &&
                        dep.dependencyFlags == expected.dependencyFlags;
                }
                if (dep.srcSubpass == 0 && dep.dstSubpass == 0)
                {
                    color_self += (dep.srcAccessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) != 0;
                    depth_self += (dep.srcAccessMask & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) != 0;
                }
            }
            char label[176];
            std::snprintf(label, sizeof(label), "GS incoming dependency policy=%u color=%u depth=%u colorRead=%u depthRead=%u has exact framebuffer/read scopes",
                policy, color, depth, color_read, depth_read);
            check(pass && exact && incoming == u32(color || depth), label);
            check(depth_self == u32(depth_read) && color_self == u32(color_read && policy != 1),
                "GS incoming edge preserves depth and non-ROAA color self dependencies");
            check(description.inputs == (sampler_path ? 0u : u32(color_read) + u32(depth_read)),
                "GS ordering retains mutually exclusive input-attachment and sampler read spellings");
        }
    }

    std::vector<ImageDependency> barriers;
    capturedBarriers = &barriers;
    realBarrier = vkCmdPipelineBarrier;
    vkCmdPipelineBarrier = CaptureBarrier;
    ScopedGuard restore_barriers = [&]() {
        vkCmdPipelineBarrier = realBarrier;
        realBarrier = nullptr;
        capturedBarriers = nullptr;
    };
    const auto read = [&](GSTextureVK& texture) {
        std::vector<u8> result;
        auto download = GSDownloadTextureVK::Create(texture.GetWidth(), texture.GetHeight(), texture.GetFormat());
        if (!download) return result;
        const auto rect = texture.GetRect();
        download->CopyFromTexture(rect, &texture, rect, 0);
        download->Flush();
        if (!download->Map(rect)) return result;
        result.resize(size_t(rect.width()) * rect.height() * 4);
        for (int y = 0; y < rect.height(); ++y)
            std::memcpy(result.data() + y * rect.width() * 4, download->GetMapPointer() + y * download->GetMapPitch(), rect.width() * 4);
        download->Unmap();
        return result;
    };
    constexpr u32 left = 0xff203040, right = 0xff8090a0, replacement = 0xff9abcde;
    const GSVector4i rect(0, 0, 16, 16);
    const auto check_color = [&](const std::vector<u8>& bytes, u32 a, u32 b) {
        if (bytes.size() != 16 * 16 * 4) return false;
        for (u32 y = 0; y < 16; ++y) for (u32 x = 0; x < 16; ++x)
        { u32 value; std::memcpy(&value, bytes.data() + (y * 16 + x) * 4, 4); if (value != (x < 8 ? a : b)) return false; }
        return true;
    };
    const auto has_scope = [&](VkImage image, VkPipelineStageFlags stages, VkAccessFlags access, bool destination, size_t begin) {
        for (size_t i = begin; i < barriers.size(); ++i)
        {
            const auto& b = barriers[i];
            if (b.image.image == image && ((destination ? b.destination : b.source) & stages) == stages &&
                ((destination ? b.image.dstAccessMask : b.image.srcAccessMask) & access) == access) return true;
        }
        return false;
    };
    auto target = GSTextureVK::Create(GSTexture::FeedbackTarget, GSTexture::Format::Color, 16, 16, 1);
    auto depth = GSTextureVK::Create(GSTexture::FeedbackDepth, GSTexture::Format::DepthStencil, 16, 16, 1);
    check(target && depth, "GS ordering creates real feedback-capable color and depth images");
    if (!target || !depth) return;
    const auto clear_pass = GetRenderPass(color_format, depth_format, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
    const auto load_pass = GetRenderPass(color_format, depth_format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
    VkClearValue values[2]{};
    GSVector4::store<false>(&values[0].color, GSVector4::unorm8(left));
    values[1].depthStencil = {0.25f, 0};
    for (bool restart : {false, true})
    {
        OMSetRenderTargets(target.get(), depth.get(), rect);
        BeginClearRenderPass(clear_pass, rect, values, 2);
        barriers.clear();
        EndRenderPass();
        check(m_device_properties.vendorID != 0x13b5 || barriers.empty(),
            "GS pass end avoids blanket ARM attachment barriers after declaring incoming ordering");
        OMSetRenderTargets(target.get(), depth.get(), rect);
        BeginRenderPass(load_pass, rect);
        if (restart) ExecuteCommandBufferAndRestartRenderPass(false, "GS incoming ordering restart");
        VkClearAttachment clear[2]{};
        clear[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        GSVector4::store<false>(&clear[0].clearValue.color, GSVector4::unorm8(right));
        clear[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clear[1].clearValue.depthStencil = {0.75f, 0};
        const VkClearRect half{{{8, 0}, {8, 16}}, 0, 1};
        vkCmdClearAttachments(GetCurrentCommandBuffer(), 2, clear, 1, &half);
        EndRenderPass();
        check(check_color(read(*target), left, right), "GS same-target LOAD and partial clears preserve earlier color across pass/restart");
        const auto depth_bytes = read(*depth);
        bool exact_depth = depth_bytes.size() == 16 * 16 * 4;
        for (u32 y = 0; exact_depth && y < 16; ++y) for (u32 x = 0; x < 16; ++x)
        {
            u32 value; std::memcpy(&value, depth_bytes.data() + (y * 16 + x) * 4, 4);
            if (depth_format == VK_FORMAT_D24_UNORM_S8_UINT)
            { value &= 0xffffff; const u32 expected = x < 8 ? 0x400000 : 0xbfffff; exact_depth &= value >= expected - 1 && value <= expected + 1; }
            else exact_depth &= value == (x < 8 ? 0x3e800000u : 0x3f400000u);
        }
        check(exact_depth, "GS same-target LOAD and partial clears preserve earlier depth across pass/restart");
    }

    auto sampled = GSTextureVK::Create(GSTexture::RenderTarget, GSTexture::Format::Color, 16, 16, 1);
    check(sampled != nullptr, "GS ordering creates a real fragment-sample destination");
    if (!sampled) return;
    SetUtilityTexture(target.get(), sampler);
    OMSetRenderTargets(sampled.get(), nullptr, rect);
    BeginRenderPass(sampled_pass, rect);
    SetPipeline(sampled_pipeline);
    SetViewport(VkViewport{0, 0, 16, 16, 0, 1}); SetScissor(rect);
    const float identity[4] = {1, 1, 1, 1};
    SetUtilityPushConstants(identity, sizeof(identity));
    check(ApplyUtilityState(), "GS ordering applies its actual fragment-sampling state");
    vkCmdDraw(GetCurrentCommandBuffer(), 3, 1, 0, 0);
    EndRenderPass();
    check(check_color(read(*sampled), left, left), "GS attachment to ordinary fragment sample preserves exact source pixels");

    // Execute the device's real feedback road. The alternate unsupported road
    // was inspected structurally above, never passed to an unsupported driver.
    for (bool read_depth : {false, true})
    {
        auto depth_output = read_depth ? GSTextureVK::Create(GSTexture::FeedbackTarget, GSTexture::Format::Color, 16, 16, 1) : nullptr;
        GSTextureVK* output = read_depth ? depth_output.get() : target.get();
        GSTextureVK* input = read_depth ? depth.get() : target.get();
        check(output != nullptr, "GS ordering creates its real input/depth feedback output");
        if (!output) return;
        const bool sampled_feedback = UseFeedbackLoopLayout();
        VkDescriptorSetLayout input_layout = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
        VkPipeline feedback_pipeline = VK_NULL_HANDLE, plain_pipeline = VK_NULL_HANDLE;
        VkShaderModule plain_fragment = VK_NULL_HANDLE;
        bool carried_pixels = false;
        ScopedGuard cleanup_input = [&]() {
            EndRenderPass(); vkDeviceWaitIdle(m_device);
            if (feedback_pipeline) vkDestroyPipeline(m_device, feedback_pipeline, nullptr);
            if (plain_pipeline) vkDestroyPipeline(m_device, plain_pipeline, nullptr);
            if (plain_fragment) vkDestroyShaderModule(m_device, plain_fragment, nullptr);
            if (fragment) vkDestroyShaderModule(m_device, fragment, nullptr);
            if (vertex) vkDestroyShaderModule(m_device, vertex, nullptr);
            if (layout) vkDestroyPipelineLayout(m_device, layout, nullptr);
            if (input_layout) vkDestroyDescriptorSetLayout(m_device, input_layout, nullptr);
            InvalidateCachedState();
        };
        Vulkan::DescriptorSetLayoutBuilder ds;
        if (m_use_push_descriptors) ds.SetPushFlag();
        const VkDescriptorType descriptor_type = sampled_feedback ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        ds.AddBinding(0, descriptor_type, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
        input_layout = ds.Create(m_device);
        Vulkan::PipelineLayoutBuilder pb; pb.AddDescriptorSet(input_layout); layout = pb.Create(m_device);
        vertex = g_vulkan_shader_cache->GetVertexShader(R"(#version 450
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.0-1.0,0,1);})");
        std::string shader = "#version 450\n";
        if (sampled_feedback) shader += "layout(set=0,binding=0) uniform sampler2D source;\n";
        else shader += read_depth ? "layout(input_attachment_index=1,set=0,binding=0) uniform subpassInput source;\n" :
                                    "layout(input_attachment_index=0,set=0,binding=0) uniform subpassInput source;\n";
        shader += "layout(location=0) out vec4 color;void main(){vec4 value=";
        shader += sampled_feedback ? "texelFetch(source,ivec2(gl_FragCoord.xy),0);" : "subpassLoad(source);";
        shader += read_depth ? "color=vec4(value.r,value.r,value.r,1);}" : "color=value;}";
        fragment = g_vulkan_shader_cache->GetFragmentShader(shader.c_str());
        const auto feedback_pass = GetRenderPass(color_format, read_depth ? depth_format : VK_FORMAT_UNDEFINED,
            read_depth ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE, true, read_depth);
        if (layout && vertex && fragment && feedback_pass)
        {
            Vulkan::GraphicsPipelineBuilder gp;
            gp.SetVertexShader(vertex); gp.SetFragmentShader(fragment);
            gp.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            gp.SetNoCullRasterizationState(); gp.SetNoDepthTestState(); gp.SetNoStencilState(); gp.SetNoBlendingState();
            gp.SetDynamicViewportAndScissorState(); gp.SetPipelineLayout(layout); gp.SetRenderPass(feedback_pass, 0);
            if (UsesRasterizationOrderAttachmentAccessForFeedback())
                gp.AddBlendFlags(VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT);
            if (sampled_feedback)
                gp.AddPipelineFlags(VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT |
                    (read_depth ? VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT : 0));
            feedback_pipeline = gp.Create(m_device);
        }
        check(input_layout && layout && feedback_pipeline, "GS ordering builds a real input-attachment or sampler-feedback pipeline");
        if (!input_layout || !layout || !feedback_pipeline) return;
        FeedbackLoopFlag flags = FeedbackLoopFlag_ReadAndWriteRT;
        if (read_depth) flags = static_cast<FeedbackLoopFlag>(flags | FeedbackLoopFlag_ReadDepth);
        OMSetRenderTargets(output, read_depth ? depth.get() : nullptr, rect, flags);
        BeginRenderPass(feedback_pass, rect);
        Vulkan::DescriptorSetUpdateBuilder update;
        if (sampled_feedback)
            update.AddCombinedImageSamplerDescriptorWrite(VK_NULL_HANDLE, 0, input->GetView(), sampler, input->GetVkLayout());
        else update.AddInputAttachmentDescriptorWrite(VK_NULL_HANDLE, 0, input->GetView(), VK_IMAGE_LAYOUT_GENERAL);
        if (m_use_push_descriptors) update.PushUpdate(GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0);
        else
        {
            const auto descriptor = AllocateFrameDescriptorSet(input_layout);
            check(descriptor != VK_NULL_HANDLE, "GS ordering allocates its real feedback descriptor");
            if (!descriptor) return;
            update.UpdateToDescriptorSet(m_device, descriptor, true);
            vkCmdBindDescriptorSets(GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptor, 0, nullptr);
        }
        vkCmdBindPipeline(GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, feedback_pipeline);
        const VkViewport viewport{0, 0, 16, 16, 0, 1};
        const VkRect2D scissor{{0, 0}, {16, 16}};
        vkCmdSetViewport(GetCurrentCommandBuffer(), 0, 1, &viewport);
        vkCmdSetScissor(GetCurrentCommandBuffer(), 0, 1, &scissor);
        vkCmdDraw(GetCurrentCommandBuffer(), 3, 1, 0, 0);
        if (!read_depth && UsesRasterizationOrderAttachmentAccessForFeedback())
        {
            GSHWDrawConfig plain{}; plain.rt = output;
            plain.road = GSDecideHWDrawRoad(plain, m_features);
            PipelineSelector selected{};
            UpdateHWPipelineSelector(plain, selected);
            ApplyHWDrawFeedbackCarry(plain, selected, output, nullptr);
            check(selected.IsRTFeedbackLoop(), "GS actual ROAA reader to non-reader preserves the compatible feedback pass");
            plain_fragment = g_vulkan_shader_cache->GetFragmentShader(R"(#version 450
layout(location=0) out vec4 color;void main(){color=vec4(222.0,188.0,154.0,255.0)/255.0;})");
            if (selected.IsRTFeedbackLoop() && plain_fragment)
            {
                Vulkan::GraphicsPipelineBuilder gp;
                gp.SetVertexShader(vertex); gp.SetFragmentShader(plain_fragment);
                gp.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                gp.SetNoCullRasterizationState(); gp.SetNoDepthTestState(); gp.SetNoStencilState(); gp.SetNoBlendingState();
                gp.SetDynamicViewportAndScissorState(); gp.SetPipelineLayout(layout); gp.SetRenderPass(feedback_pass, 0);
                gp.AddBlendFlags(VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT);
                plain_pipeline = gp.Create(m_device);
            }
            check(plain_pipeline != VK_NULL_HANDLE, "GS actual ROAA non-reader builds the matching render-pass/pipeline schema");
            if (plain_pipeline)
            {
                const auto active = m_current_render_pass;
                OMSetRenderTargets(output, nullptr, rect, static_cast<FeedbackLoopFlag>(selected.feedback_loop_flags));
                check(InRenderPass() && m_current_render_pass == active,
                    "GS actual ROAA non-reader retains the open pass without changing attachment layout");
                vkCmdBindPipeline(GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, plain_pipeline);
                const VkRect2D half{{8, 0}, {8, 16}};
                vkCmdSetScissor(GetCurrentCommandBuffer(), 0, 1, &half);
                vkCmdDraw(GetCurrentCommandBuffer(), 3, 1, 0, 0);
                // The following reader uses the very same input descriptor and
                // consumes the non-reader's newly produced pixels in this pass.
                vkCmdBindPipeline(GetCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, feedback_pipeline);
                vkCmdSetScissor(GetCurrentCommandBuffer(), 0, 1, &scissor);
                vkCmdDraw(GetCurrentCommandBuffer(), 3, 1, 0, 0);
                carried_pixels = true;
            }
        }
        EndRenderPass();
        if (!read_depth)
        {
            check(check_color(read(*output), left, carried_pixels ? replacement : right),
                carried_pixels ? "GS actual ROAA reader/non-reader/reader preserves exact new pixels in one pass" :
                    "GS consecutive color feedback pass reads the producer's exact per-pixel values");
            if (carried_pixels)
            {
                // Restore the independent later compute fixture's source.
                OMSetRenderTargets(output, nullptr, rect);
                BeginRenderPass(sampled_pass, rect);
                VkClearAttachment restore{}; restore.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                GSVector4::store<false>(&restore.clearValue.color, GSVector4::unorm8(right));
                const VkClearRect half{{{8, 0}, {8, 16}}, 0, 1};
                vkCmdClearAttachments(GetCurrentCommandBuffer(), 1, &restore, 1, &half);
                EndRenderPass();
            }
        }
        else
        {
            const auto bytes = read(*output);
            bool exact = bytes.size() == 16 * 16 * 4;
            for (u32 y = 0; exact && y < 16; ++y) for (u32 x = 0; x < 16; ++x)
            {
                const u32 expected = x < 8 ? 64 : 191;
                for (u32 c = 0; c < 3; ++c)
                { const u32 value = bytes[(y * 16 + x) * 4 + c]; exact &= value >= expected - 1 && value <= expected + 1; }
                exact &= bytes[(y * 16 + x) * 4 + 3] == 255;
            }
            check(exact, "GS depth input/sampler reads retain quarter and three-quarter producer depth");
        }
    }

    target->TransitionToLayout(GSTextureVK::Layout::ShaderReadOnly);
    size_t begin = barriers.size();
    target->TransitionToLayout(GetCASSourceLayout());
    check(has_scope(target->GetImage(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, true, begin),
        "GS fragment-to-CAS read changes semantic scope even when VkImageLayout stays read-only");
    begin = barriers.size();
    target->TransitionToLayout(GSTextureVK::Layout::ShaderReadOnly);
    check(has_scope(target->GetImage(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, false, begin),
        "GS CAS-to-fragment read retains the prior compute read synchronization scope");
    // The helper is the same source policy actual DoCAS uses. A custom copy
    // kernel isolates that production consumer from CAS numerical filtering.
    check(!m_cas_pipeline_layout && !m_cas_ds_layout && !m_cas_pipelines[0] && !m_cas_pipelines[1],
        "GS ordering preparation device has no preexisting CAS resources");
    if (m_cas_pipeline_layout || m_cas_ds_layout || m_cas_pipelines[0] || m_cas_pipelines[1]) return;
    VkShaderModule compute = VK_NULL_HANDLE;
    ScopedGuard compute_cleanup = [&]() {
        EndRenderPass(); vkDeviceWaitIdle(m_device);
        if (m_cas_pipelines[0]) vkDestroyPipeline(m_device, m_cas_pipelines[0], nullptr);
        if (m_cas_pipeline_layout) vkDestroyPipelineLayout(m_device, m_cas_pipeline_layout, nullptr);
        if (m_cas_ds_layout) vkDestroyDescriptorSetLayout(m_device, m_cas_ds_layout, nullptr);
        if (compute) vkDestroyShaderModule(m_device, compute, nullptr);
        m_cas_pipelines.fill(VK_NULL_HANDLE);
        m_cas_pipeline_layout = VK_NULL_HANDLE; m_cas_ds_layout = VK_NULL_HANDLE;
        InvalidateCachedState();
    };
    Vulkan::DescriptorSetLayoutBuilder dsl;
    if (m_use_push_descriptors) dsl.SetPushFlag();
    dsl.AddBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl.AddBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    m_cas_ds_layout = dsl.Create(m_device);
    Vulkan::PipelineLayoutBuilder pl;
    pl.AddDescriptorSet(m_cas_ds_layout);
    pl.AddPushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, NUM_CAS_CONSTANTS * sizeof(u32));
    m_cas_pipeline_layout = pl.Create(m_device);
    compute = g_vulkan_shader_cache->GetComputeShader(R"(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x=16,local_size_y=16,local_size_z=1) in;
layout(set=0,binding=0) uniform texture2D source;
layout(set=0,binding=1,rgba8) writeonly uniform image2D target;
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(all(lessThan(p,imageSize(target))))imageStore(target,p,texelFetch(source,p,0));})");
    if (m_cas_pipeline_layout && compute)
    {
        Vulkan::ComputePipelineBuilder cp;
        cp.SetPipelineLayout(m_cas_pipeline_layout); cp.SetShader(compute, "main");
        m_cas_pipelines[0] = cp.Create(m_device);
    }
    check(m_cas_ds_layout && m_cas_pipeline_layout && m_cas_pipelines[0], "GS ordering creates a real sampled-copy compute pipeline for actual DoCAS");
    if (!m_cas_ds_layout || !m_cas_pipeline_layout || !m_cas_pipelines[0]) return;
    auto result = GSTextureVK::Create(GSTexture::ShaderWriteTexture, GSTexture::Format::Color, 16, 16, 1);
    check(result != nullptr, "GS ordering allocates real compute storage output");
    if (!result) return;
    OMSetRenderTargets(target.get(), nullptr, rect);
    BeginRenderPass(sampled_pass, rect); EndRenderPass();
    begin = barriers.size();
    const std::array<u32, NUM_CAS_CONSTANTS> constants{};
    check(DoCAS(target.get(), result.get(), false, constants), "GS ordering dispatches the actual DoCAS consumer");
    check(has_scope(target->GetImage(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, true, begin),
        "GS attachment-to-CAS RAW barrier names compute sampled-image reads");
    begin = barriers.size();
    OMSetRenderTargets(target.get(), nullptr, rect);
    const auto overwrite = GetRenderPass(color_format, VK_FORMAT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
    BeginClearRenderPass(overwrite, rect, replacement); EndRenderPass();
    check(has_scope(target->GetImage(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, false, begin),
        "GS CAS-read-to-attachment-write WAR barrier names prior compute reads");
    check(check_color(read(*result), left, right), "GS compute samples old attachment pixels before a subsequent overwrite");
    check(check_color(read(*target), replacement, replacement), "GS subsequent attachment overwrite reaches transfer readback");
}
