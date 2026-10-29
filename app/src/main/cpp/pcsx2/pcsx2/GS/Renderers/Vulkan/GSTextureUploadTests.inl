// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Real Vulkan copies across growth of the device's texture upload ring.
void GSDeviceVK::TestTextureUploadRing(void (*check)(bool, const char*))
{
    check(m_tfx_preparation_device && !m_allocator && !m_texture_stream_buffer.IsValid(),
        "GS upload oracle starts from an isolated preparation device");
    if (!m_tfx_preparation_device || m_allocator || m_texture_stream_buffer.IsValid()) return;
    bool retired = false, retired_after_fence = false;
    ScopedGuard cleanup = [this]() {
        if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
        m_texture_stream_buffer.Destroy(false);
        for (FrameResources& frame : m_frame_resources)
        {
            for (auto& resource : frame.cleanup_resources) resource();
            if (frame.fence) vkDestroyFence(m_device, frame.fence, nullptr);
            if (frame.command_pool) vkDestroyCommandPool(m_device, frame.command_pool, nullptr);
            if (frame.descriptor_pool) vkDestroyDescriptorPool(m_device, frame.descriptor_pool, nullptr);
            for (auto pool : frame.extra_descriptor_pools) vkDestroyDescriptorPool(m_device, pool, nullptr);
            frame = FrameResources{};
        }
        if (m_allocator) vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
        m_current_command_buffer = VK_NULL_HANDLE;
        m_current_frame = 0;
        m_next_fence_counter = 1;
        m_completed_fence_counter = 0;
    };
    const bool initialized = CreateAllocator() && CreateCommandBuffers() &&
        m_texture_stream_buffer.Create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, TEXTURE_BUFFER_SIZE,
            "texture", TEXTURE_BUFFER_MAX_SIZE);
    check(initialized, "GS upload oracle allocates real command buffers and the64MiB texture ring");
    if (!initialized) return;
    auto before = GSTextureVK::Create(GSTexture::Usage::Texture, GSTexture::Format::Color, 37, 23, 1);
    auto after = GSTextureVK::Create(GSTexture::Usage::Texture, GSTexture::Format::Color, 37, 23, 1);
    check(before && after, "GS upload oracle allocates its two comparison images");
    if (!before || !after) return;
    std::vector<u32> expected(37 * 23), second(37 * 23);
    for (u32 i = 0; i < expected.size(); ++i)
    {
        expected[i] = (i * 0x10301u) ^ 0xa5813c27u;
        second[i] = ~expected[i];
    }
    GSTexture::GSMap mapped = {};
    const bool mapped_ok = before->Map(mapped);
    check(mapped_ok, "GS upload oracle records a mapped upload before ring growth");
    if (!mapped_ok) return;
    for (u32 y = 0; y < 23; ++y)
        std::memcpy(mapped.bits + y * mapped.pitch, expected.data() + y * 37, 37 * 4);
    before->Unmap();
    VKStreamBuffer& ring = m_texture_stream_buffer;
    const VkBuffer old_buffer = ring.GetBuffer();
    const u32 alignment = GetBufferCopyOffsetAlignment();
    const u32 fill = ring.GetCurrentSize() - Common::AlignUp(ring.GetCurrentOffset(), alignment) - alignment;
    const bool filled = ring.ReserveMemory(fill, alignment);
    check(filled, "GS upload oracle pressures the current in-flight ring");
    if (!filled) return;
    ring.CommitMemory(fill);
    const u32 retiring_frame = m_current_frame;
    const u64 retiring_counter = GetCurrentFenceCounter();
    const VkFence retiring_fence = m_frame_resources[retiring_frame].fence;
    const size_t old_cleanup = m_frame_resources[retiring_frame].cleanup_resources.size();
    const bool grew = ring.ReserveMemory(1024 * 1024, alignment);
    check(grew && ring.GetCurrentSize() == 128u * 1024 * 1024 && ring.GetMaxSize() == 128u * 1024 * 1024 &&
        ring.GetBuffer() != old_buffer, "GS texture upload pressure grows64to128MiB without submitting");
    if (!grew) return;
    check(GetCurrentFenceCounter() == retiring_counter && GetCompletedFenceCounter() < retiring_counter &&
        m_frame_resources[retiring_frame].cleanup_resources.size() == old_cleanup + 1,
        "GS grown texture ring queues its old allocation on the still-unsignaled upload fence");
    m_frame_resources[retiring_frame].cleanup_resources.push_back([&, retiring_fence]() {
        retired = true;
        retired_after_fence = vkGetFenceStatus(m_device, retiring_fence) == VK_SUCCESS;
    });
    check(!retired, "GS grown texture ring keeps the old upload allocation until submission completes");
    check(after->Update(after->GetRect(), second.data(), 37 * 4),
        "GS upload oracle records scratch-style Update bytes after growth");
    ExecuteCommandBuffer(WaitType::Sleep);
    check(retired && retired_after_fence && GetCompletedFenceCounter() >= retiring_counter,
        "GS grown texture ring retirement runs only after the consuming GPU fence signals");
    const auto compare = [&](GSTextureVK& texture, const std::vector<u32>& pixels) {
        auto download = GSDownloadTextureVK::Create(37, 23, GSTexture::Format::Color);
        if (!download) return false;
        const GSVector4i rect(0, 0, 37, 23);
        download->CopyFromTexture(rect, &texture, rect, 0);
        download->Flush();
        if (!download->Map(rect)) return false;
        bool same = true;
        for (u32 y = 0; y < 23; ++y)
            same &= std::memcmp(download->GetMapPointer() + y * download->GetMapPitch(), pixels.data() + y * 37, 37 * 4) == 0;
        download->Unmap();
        return same;
    };
    check(compare(*before, expected), "GS GPU readback preserves every pre-growth mapped upload byte");
    check(compare(*after, second), "GS GPU readback preserves every post-growth scratch upload byte");
    check(ring.GetCurrentSize() == TEXTURE_BUFFER_MAX_SIZE, "GS texture upload ring remains bounded at128MiB");
}
