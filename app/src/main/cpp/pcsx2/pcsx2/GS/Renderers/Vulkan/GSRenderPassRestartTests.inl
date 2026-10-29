// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Real restart regressions. Production restart functions remain the test subject.
void GSDeviceVK::TestRenderPassRestarts(void (*check)(bool, const char*))
{
    check(m_tfx_preparation_device && !m_allocator, "GS restart fixture starts on an isolated preparation device");
    if (!m_tfx_preparation_device || m_allocator) return;
    std::vector<u64> initial_keys;
    for (const auto& item : m_render_pass_cache) initial_keys.push_back(item.first);
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    ScopedGuard cleanup = [&]() {
        EndRenderPass();
        vkDeviceWaitIdle(m_device);
        m_null_texture.reset();
        m_texture_stream_buffer.Destroy(false);
        m_index_stream_buffer.Destroy(false);
        for (FrameResources& frame : m_frame_resources)
        {
            for (auto& resource : frame.cleanup_resources) resource();
            if (frame.fence) vkDestroyFence(m_device, frame.fence, nullptr);
            if (frame.command_pool) vkDestroyCommandPool(m_device, frame.command_pool, nullptr);
            if (frame.descriptor_pool) vkDestroyDescriptorPool(m_device, frame.descriptor_pool, nullptr);
            for (auto pool : frame.extra_descriptor_pools) vkDestroyDescriptorPool(m_device, pool, nullptr);
            frame = FrameResources{};
        }
        if (pipeline) vkDestroyPipeline(m_device, pipeline, nullptr);
        if (vs) vkDestroyShaderModule(m_device, vs, nullptr);
        if (fs) vkDestroyShaderModule(m_device, fs, nullptr);
        if (sampler) vkDestroySampler(m_device, sampler, nullptr);
        if (m_utility_pipeline_layout) vkDestroyPipelineLayout(m_device, m_utility_pipeline_layout, nullptr);
        if (m_utility_ds_layout) vkDestroyDescriptorSetLayout(m_device, m_utility_ds_layout, nullptr);
        m_utility_pipeline_layout = VK_NULL_HANDLE;
        m_utility_ds_layout = VK_NULL_HANDLE;
        for (auto it = m_render_pass_cache.begin(); it != m_render_pass_cache.end();)
        {
            if (std::find(initial_keys.begin(), initial_keys.end(), it->first) == initial_keys.end())
            {
                vkDestroyRenderPass(m_device, it->second, nullptr);
                it = m_render_pass_cache.erase(it);
            }
            else ++it;
        }
        if (m_allocator) vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
        m_index_buffer = VK_NULL_HANDLE;
        m_current_pipeline = VK_NULL_HANDLE;
        m_current_command_buffer = VK_NULL_HANDLE;
        m_current_framebuffer = VK_NULL_HANDLE;
        m_current_render_target = m_current_depth_target = nullptr;
        m_utility_texture = nullptr;
        m_utility_sampler = VK_NULL_HANDLE;
        m_utility_push_constants_size = 0;
        m_tfx_textures.fill(nullptr);
        m_current_frame = 0;
        m_next_fence_counter = 1;
        m_completed_fence_counter = 0;
    };
    const bool initialized = CreateAllocator() && CreateCommandBuffers() &&
        m_texture_stream_buffer.Create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1024 * 1024, "restart-texture") &&
        m_index_stream_buffer.Create(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 4096, "restart-index") && CreateNullTexture();
    check(initialized, "GS restart fixture creates real upload, index and null-texture resources");
    if (!initialized) return;
    m_index_buffer = m_index_stream_buffer.GetBuffer();
    const VkFormat color_format = LookupNativeFormat(GSTexture::Format::Color);
    const VkFormat depth_format = LookupNativeFormat(GSTexture::Format::DepthStencil);
    for (u32 color = 0; color < 3; ++color) for (u32 depth = 0; depth < 3; ++depth) for (u32 stencil = 0; stencil < 3; ++stencil)
    {
        const auto original = GetRenderPass(color_format, depth_format, static_cast<VkAttachmentLoadOp>(color),
            VK_ATTACHMENT_STORE_OP_STORE, static_cast<VkAttachmentLoadOp>(depth), VK_ATTACHMENT_STORE_OP_STORE,
            static_cast<VkAttachmentLoadOp>(stencil), VK_ATTACHMENT_STORE_OP_STORE);
        const auto restarted = GetRenderPassForRestarting(original);
        RenderPassCacheKey old_key{}, new_key{};
        for (const auto& item : m_render_pass_cache)
        {
            if (item.second == original) old_key.key = item.first;
            if (item.second == restarted) new_key.key = item.first;
        }
        check(restarted && new_key.color_load_op == VK_ATTACHMENT_LOAD_OP_LOAD &&
            new_key.depth_load_op == VK_ATTACHMENT_LOAD_OP_LOAD && new_key.stencil_load_op == VK_ATTACHMENT_LOAD_OP_LOAD,
            "GS restart maps all27 color/depth/stencil load combinations to LOAD");
        old_key.color_load_op = old_key.depth_load_op = old_key.stencil_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
        check(new_key.key == old_key.key && GetRenderPassForRestarting(restarted) == restarted,
            "GS restart preserves non-load key fields and reuses an already-loading pass");
    }
    const auto read = [&](GSTextureVK& texture) {
        std::vector<u8> result;
        auto download = GSDownloadTextureVK::Create(texture.GetWidth(), texture.GetHeight(), texture.GetFormat());
        if (!download) return result;
        const auto rect = texture.GetRect();
        download->CopyFromTexture(rect, &texture, rect, 0);
        download->Flush();
        if (!download->Map(rect)) return result;
        result.resize(static_cast<size_t>(rect.width()) * rect.height() * 4);
        for (int y = 0; y < rect.height(); ++y)
            std::memcpy(result.data() + y * rect.width() * 4, download->GetMapPointer() + y * download->GetMapPitch(), rect.width() * 4);
        // D24 copies have an unused X byte; compare only the defined depth.
        if (texture.IsDepthStencil() && depth_format == VK_FORMAT_D24_UNORM_S8_UINT)
            for (size_t at = 0; at < result.size(); at += 4)
            {
                u32 value;
                std::memcpy(&value, result.data() + at, 4);
                value &= 0xffffff;
                std::memcpy(result.data() + at, &value, 4);
            }
        download->Unmap();
        return result;
    };
    // Every final pixel is defined by clear-attachment writes, even when the
    // initial load is DONT_CARE. No shader/texture state can mask this test.
    for (auto initial : {VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_DONT_CARE})
    {
        std::vector<u8> reference_color, reference_depth;
        for (bool restart : {false, true})
        {
            auto color = GSTextureVK::Create(GSTexture::RenderTarget, GSTexture::Format::Color, 16, 16, 1);
            auto depth = GSTextureVK::Create(GSTexture::DepthStencil, GSTexture::Format::DepthStencil, 16, 16, 1);
            check(color && depth, "GS restart creates real color/depth attachments");
            if (!color || !depth) return;
            const GSVector4i rect(0, 0, 16, 16);
            OMSetRenderTargets(color.get(), depth.get(), rect);
            const auto pass = GetRenderPass(color_format, depth_format, initial, VK_ATTACHMENT_STORE_OP_STORE,
                initial, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);
            VkClearValue values[2]{};
            values[1].depthStencil = {0.5f, 0};
            if (initial == VK_ATTACHMENT_LOAD_OP_CLEAR) BeginClearRenderPass(pass, rect, values, 2);
            else BeginRenderPass(pass, rect);
            for (u32 half = 0; half < 2; ++half)
            {
                if (half && restart) ExecuteCommandBufferAndRestartRenderPass(false, "GS restart pixel regression");
                VkClearAttachment attachments[2]{};
                attachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                attachments[0].colorAttachment = 0;
                const u32 rgba = half ? 0xffc08040 : 0xff2468ac;
                GSVector4::store<false>(&attachments[0].clearValue.color, GSVector4::unorm8(rgba));
                attachments[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                attachments[1].clearValue.depthStencil = {half ? 0.75f : 0.25f, 0};
                const VkClearRect area{{{static_cast<s32>(half * 8), 0}, {8, 16}}, 0, 1};
                vkCmdClearAttachments(GetCurrentCommandBuffer(), 2, attachments, 1, &area);
            }
            EndRenderPass();
            const auto c = read(*color), d = read(*depth);
            if (!restart)
            {
                bool color_defined = c.size() == 16 * 16 * 4;
                bool depth_defined = d.size() == 16 * 16 * 4;
                if (color_defined && depth_defined)
                    for (u32 y = 0; y < 16; ++y) for (u32 x = 0; x < 16; ++x)
                    {
                        u32 color_value, depth_value;
                        std::memcpy(&color_value, c.data() + (y * 16 + x) * 4, 4);
                        std::memcpy(&depth_value, d.data() + (y * 16 + x) * 4, 4);
                        color_defined &= color_value == (x >= 8 ? 0xffc08040u : 0xff2468acu);
                        if (depth_format == VK_FORMAT_D24_UNORM_S8_UINT)
                        {
                            const u32 expected = x >= 8 ? 0xbfffffu : 0x400000u;
                            depth_defined &= depth_value >= expected - 1 && depth_value <= expected + 1;
                        }
                        else depth_defined &= depth_value == (x >= 8 ? 0x3f400000u : 0x3e800000u);
                    }
                check(color_defined, "GS uninterrupted reference has independently expected left/right color pixels");
                check(depth_defined, "GS uninterrupted reference has independently expected quarter/three-quarter depth");
                reference_color = c;
                reference_depth = d;
            }
            else
            {
                check(c.size() == 16 * 16 * 4 && c == reference_color, "GS restarted CLEAR/DONT_CARE color pixels match uninterrupted rendering");
                check(d.size() == 16 * 16 * 4 && d == reference_depth, "GS restarted CLEAR/DONT_CARE depth pixels match uninterrupted rendering");
            }
        }
    }
    // A real sampled utility draw exercises descriptor re-application and
    // the existing nontrivial push-constant restore after an empty/pass restart.
    Vulkan::DescriptorSetLayoutBuilder dsl;
    if (m_use_push_descriptors) dsl.SetPushFlag();
    dsl.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    m_utility_ds_layout = dsl.Create(m_device);
    Vulkan::PipelineLayoutBuilder pl;
    pl.AddDescriptorSet(m_utility_ds_layout);
    pl.AddPushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, CONVERT_PUSH_CONSTANTS_SIZE);
    m_utility_pipeline_layout = pl.Create(m_device);
    Vulkan::SamplerBuilder sb; sb.SetPointSampler(); sampler = sb.Create(m_device);
    vs = g_vulkan_shader_cache->GetVertexShader(R"(#version 450
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.0-1.0,0,1);})");
    fs = g_vulkan_shader_cache->GetFragmentShader(R"(#version 450
layout(set=0,binding=0) uniform sampler2D source;
layout(push_constant) uniform Params{vec4 factor;} params;
layout(location=0) out vec4 color;
void main(){color=texelFetch(source,ivec2(0),0)*params.factor;})");
    check(m_utility_ds_layout && m_utility_pipeline_layout && sampler && vs && fs, "GS restart fixture builds its sampled shader resources");
    if (!m_utility_ds_layout || !m_utility_pipeline_layout || !sampler || !vs || !fs) return;
    const auto pass = GetRenderPass(color_format, VK_FORMAT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
    Vulkan::GraphicsPipelineBuilder gp;
    gp.SetVertexShader(vs); gp.SetFragmentShader(fs); gp.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    gp.SetNoCullRasterizationState(); gp.SetNoDepthTestState(); gp.SetNoStencilState(); gp.SetNoBlendingState();
    gp.SetDynamicViewportAndScissorState(); gp.SetPipelineLayout(m_utility_pipeline_layout); gp.SetRenderPass(pass, 0);
    pipeline = gp.Create(m_device);
    check(pipeline != VK_NULL_HANDLE, "GS restart fixture builds its sampled graphics pipeline");
    if (!pipeline) return;
    auto source = GSTextureVK::Create(GSTexture::Texture, GSTexture::Format::Color, 1, 1, 1);
    const u32 pixel = 0xffc08040;
    check(source && source->Update(GSVector4i(0, 0, 1, 1), &pixel, 4), "GS restart uploads its distinctive source texel");
    if (!source) return;
    std::vector<u8> reference;
    for (bool restart : {false, true})
    {
        auto target = GSTextureVK::Create(GSTexture::RenderTarget, GSTexture::Format::Color, 16, 16, 1);
        if (!target) { check(false, "GS restart allocates sampled target"); return; }
        const GSVector4i rect(0, 0, 16, 16);
        SetUtilityTexture(source.get(), sampler);
        m_tfx_textures.fill(source.get());
        const auto tfx = m_tfx_textures;
        OMSetRenderTargets(target.get(), nullptr, rect);
        BeginRenderPass(pass, rect);
        SetPipeline(pipeline);
        SetViewport(VkViewport{0, 0, 16, 16, 0, 1}); SetScissor(rect);
        const float factor[4] = {1, 0.5f, 0.25f, 1};
        SetUtilityPushConstants(factor, sizeof(factor));
        if (restart)
        {
            ExecuteCommandBufferAndRestartRenderPass(false, "GS bound sampler regression");
            check(m_tfx_textures == tfx && m_utility_texture == source.get(), "GS restart preserves all7TFX slots and the utility source binding");
        }
        check(ApplyUtilityState(), "GS restart applies its sampled utility state");
        vkCmdDraw(GetCurrentCommandBuffer(), 3, 1, 0, 0);
        EndRenderPass();
        const auto result = read(*target);
        if (!restart)
        {
            bool correct = result.size() == 16 * 16 * 4;
            for (size_t at = 0; correct && at < result.size(); at += 4)
            {
                u32 value;
                std::memcpy(&value, result.data() + at, 4);
                correct &= value == 0xff304040u;
            }
            check(correct, "GS sampled reference matches independently expected texel and push-constant factors");
            reference = result;
        }
        else check(result.size() == 16 * 16 * 4 && result == reference && !reference.empty() && reference[3] == 255,
            "GS restarted sampler draw preserves pixels and existing utility push constants without rebinding");
    }
    // Updating a restored source after a resumed draw must stay in draw order.
    // An old use-fence marker routes the update into the init command buffer,
    // which executes before that draw and changes its sampled pixels.
    const auto has_pixel = [](const std::vector<u8>& bytes, u32 expected) {
        if (bytes.size() != 16 * 16 * 4) return false;
        for (size_t at = 0; at < bytes.size(); at += 4)
        {
            u32 value;
            std::memcpy(&value, bytes.data() + at, 4);
            if (value != expected) return false;
        }
        return true;
    };
    for (bool restart : {false, true})
    {
        auto before = GSTextureVK::Create(GSTexture::RenderTarget, GSTexture::Format::Color, 16, 16, 1);
        auto after = GSTextureVK::Create(GSTexture::RenderTarget, GSTexture::Format::Color, 16, 16, 1);
        const u32 replacement = 0xff204080;
        const GSVector4i rect(0, 0, 16, 16);
        check(before && after && source->Update(GSVector4i(0, 0, 1, 1), &pixel, 4),
            "GS restart prepares source-update ordering targets and original texel");
        if (!before || !after) return;
        // Keep this source utility-only so TFX use tracking cannot hide a
        // missing utility-source fence refresh.
        m_tfx_textures.fill(nullptr);
        SetUtilityTexture(source.get(), sampler);
        OMSetRenderTargets(before.get(), nullptr, rect);
        BeginRenderPass(pass, rect);
        SetPipeline(pipeline);
        SetViewport(VkViewport{0, 0, 16, 16, 0, 1}); SetScissor(rect);
        const float identity[4] = {1, 1, 1, 1};
        SetUtilityPushConstants(identity, sizeof(identity));
        if (restart) ExecuteCommandBufferAndRestartRenderPass(false, "GS source update ordering regression");
        check(ApplyUtilityState(), "GS restart applies source state before an ordered update");
        vkCmdDraw(GetCurrentCommandBuffer(), 3, 1, 0, 0);
        EndRenderPass();
        check(source->Update(GSVector4i(0, 0, 1, 1), &replacement, 4), "GS restart uploads a replacement after its first sampled draw");
        SetUtilityTexture(source.get(), sampler);
        OMSetRenderTargets(after.get(), nullptr, rect);
        BeginRenderPass(pass, rect);
        SetPipeline(pipeline);
        check(ApplyUtilityState(), "GS restart reapplies source state after an ordered update");
        vkCmdDraw(GetCurrentCommandBuffer(), 3, 1, 0, 0);
        EndRenderPass();
        const auto first = read(*before);
        const auto second = read(*after);
        check(has_pixel(first, pixel), "GS source update preserves pixels already sampled in the current command buffer");
        check(has_pixel(second, replacement), "GS source update supplies new pixels to the subsequent sampled draw");
    }
}
