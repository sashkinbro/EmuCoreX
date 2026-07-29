// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <cstdint>

enum class VulkanFeedbackPath : std::uint8_t
{
	SampledImage,
	InputAttachment,
	AttachmentFeedbackLoopLayout,
};

// Keep the shader resource declaration, descriptor type, render-pass input attachments, and
// image layout on one Vulkan feedback path. Mixing subpassInput with a sampled-image descriptor
// is invalid and can surface as alternating stale attachment contents on tile-based GPUs.
constexpr VulkanFeedbackPath SelectVulkanFeedbackPath(
	bool texture_barrier, bool framebuffer_fetch, bool attachment_feedback_loop_layout)
{
	if (!texture_barrier)
		return VulkanFeedbackPath::SampledImage;

	// Rasterization-order attachment access still reads through an input attachment; it changes
	// ordering guarantees, not the descriptor class used by the fragment shader.
	if (framebuffer_fetch || !attachment_feedback_loop_layout)
		return VulkanFeedbackPath::InputAttachment;

	return VulkanFeedbackPath::AttachmentFeedbackLoopLayout;
}
