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

enum VulkanFeedbackPipelineAspect : std::uint8_t
{
	VulkanFeedbackPipelineAspectNone = 0,
	VulkanFeedbackPipelineAspectColor = 1 << 0,
	VulkanFeedbackPipelineAspectDepthStencil = 1 << 1,
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

// Sampling an attachment through VK_EXT_attachment_feedback_loop_layout requires matching
// pipeline-create flags for every attachment aspect involved in the feedback loop. The layout
// and barriers alone are insufficient; omitting these flags makes the draw invalid Vulkan and
// can surface as a device loss on strict mobile drivers.
constexpr std::uint8_t GetVulkanFeedbackPipelineAspects(
	VulkanFeedbackPath path, bool color_feedback, bool depth_stencil_feedback)
{
	if (path != VulkanFeedbackPath::AttachmentFeedbackLoopLayout)
		return VulkanFeedbackPipelineAspectNone;

	return static_cast<std::uint8_t>(
		(color_feedback ? VulkanFeedbackPipelineAspectColor : 0) |
		(depth_stencil_feedback ? VulkanFeedbackPipelineAspectDepthStencil : 0));
}

// A descriptor stores the image layout as well as the image view. Reusing the same texture
// object is therefore not enough to skip a descriptor write when its Vulkan layout changed.
constexpr bool ShouldRefreshVulkanTextureDescriptor(bool same_texture, bool layout_changed)
{
	return !same_texture || layout_changed;
}

// Entering an attachment feedback loop can change the layout of the same image bound through
// the ordinary texture slot. Both descriptor slots must be refreshed in that aliasing case.
constexpr bool ShouldDirtyVulkanAliasedTextureDescriptor(bool aliases_main_texture, bool layout_changed)
{
	return aliases_main_texture && layout_changed;
}
