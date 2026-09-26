// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSGPUProfile.h"
#include "GS/Renderers/Common/GSInterlaceModePolicy.h"
#include "GS/Renderers/Common/GSPresentationPolicy.h"
#include "GS/Renderers/Vulkan/VulkanFeedbackPolicy.h"

#include <gtest/gtest.h>

#include <array>

TEST(VulkanFeedbackPolicy, KeepsShaderDescriptorsAndRenderPassOnTheSamePath)
{
	// When ROAA is unavailable or driver-denied, keep shader and descriptor reads on the
	// attachment-feedback-loop-layout route whenever that extension is usable.
	EXPECT_EQ(SelectVulkanFeedbackPath(true, false, true),
		VulkanFeedbackPath::AttachmentFeedbackLoopLayout);

	// Older Adreno plus proprietary Mali/PowerVR drivers without a trusted feedback-loop-layout
	// implementation must use matching subpassInput/input-attachment descriptors.
	EXPECT_EQ(SelectVulkanFeedbackPath(true, false, false),
		VulkanFeedbackPath::InputAttachment);

	// ROAA changes ordering guarantees but continues to use input attachments.
	EXPECT_EQ(SelectVulkanFeedbackPath(true, true, true),
		VulkanFeedbackPath::InputAttachment);

	// Without texture barriers, the renderer uses the existing sampled-image copy fallback.
	EXPECT_EQ(SelectVulkanFeedbackPath(false, false, true),
		VulkanFeedbackPath::SampledImage);
	EXPECT_EQ(SelectVulkanFeedbackPath(false, false, false),
		VulkanFeedbackPath::SampledImage);
}

TEST(VulkanFeedbackPolicy, DeclaresEverySampledAttachmentOnTheGraphicsPipeline)
{
	const VulkanFeedbackPath layout_path = VulkanFeedbackPath::AttachmentFeedbackLoopLayout;
	EXPECT_EQ(GetVulkanFeedbackPipelineAspects(layout_path, true, false),
		VulkanFeedbackPipelineAspectColor);
	EXPECT_EQ(GetVulkanFeedbackPipelineAspects(layout_path, false, true),
		VulkanFeedbackPipelineAspectDepthStencil);
	EXPECT_EQ(GetVulkanFeedbackPipelineAspects(layout_path, true, true),
		VulkanFeedbackPipelineAspectColor | VulkanFeedbackPipelineAspectDepthStencil);

	// Input attachments and copied sampled images do not use the attachment-feedback-loop
	// pipeline-create flags.
	EXPECT_EQ(GetVulkanFeedbackPipelineAspects(VulkanFeedbackPath::InputAttachment, true, true),
		VulkanFeedbackPipelineAspectNone);
	EXPECT_EQ(GetVulkanFeedbackPipelineAspects(VulkanFeedbackPath::SampledImage, true, true),
		VulkanFeedbackPipelineAspectNone);
}

TEST(VulkanFeedbackPolicy, RefreshesDescriptorsWhenAnImageLayoutChanges)
{
	EXPECT_FALSE(ShouldRefreshVulkanTextureDescriptor(true, false));
	EXPECT_TRUE(ShouldRefreshVulkanTextureDescriptor(true, true));
	EXPECT_TRUE(ShouldRefreshVulkanTextureDescriptor(false, false));

	EXPECT_TRUE(ShouldDirtyVulkanAliasedTextureDescriptor(true, true));
	EXPECT_FALSE(ShouldDirtyVulkanAliasedTextureDescriptor(true, false));
	EXPECT_FALSE(ShouldDirtyVulkanAliasedTextureDescriptor(false, true));
}

TEST(GSInterlaceModePolicy, AutomaticFullFrameOutputRemainsPassThrough)
{
	const GSInterlaceModeSelection selection =
		SelectGSInterlaceMode(0, true, false, false, false);
	EXPECT_EQ(selection.field_offset, 0);
	EXPECT_EQ(selection.shader_mode, -1);
}

TEST(GSInterlaceModePolicy, AutomaticTemporalSourcesUseFastMAD)
{
	EXPECT_EQ(SelectGSInterlaceMode(0, true, true, false, false).shader_mode, 3);
	EXPECT_EQ(SelectGSInterlaceMode(0, true, false, true, false).shader_mode, 3);
	EXPECT_EQ(SelectGSInterlaceMode(0, true, false, false, true).shader_mode, 3);
}

TEST(GSInterlaceModePolicy, ExplicitModesMapToExpectedShadersAndFields)
{
	EXPECT_EQ(SelectGSInterlaceMode(1, false, false, false, false).shader_mode, -1);
	EXPECT_EQ(SelectGSInterlaceMode(2, false, false, false, false).shader_mode, 0);
	EXPECT_EQ(SelectGSInterlaceMode(3, false, false, false, false).field_offset, 1);
	EXPECT_EQ(SelectGSInterlaceMode(4, false, false, false, false).shader_mode, 1);
	EXPECT_EQ(SelectGSInterlaceMode(6, false, false, false, false).shader_mode, 2);
	EXPECT_EQ(SelectGSInterlaceMode(8, false, false, false, false).shader_mode, 3);
}

TEST(GSPresentationPolicy, SkipsOnlyBlankFramesBeforeFirstOutput)
{
	EXPECT_TRUE(ShouldSkipAndroidBlankFrame(true, false, true, 1));
	EXPECT_FALSE(ShouldSkipAndroidBlankFrame(true, true, true, 1));
	EXPECT_FALSE(ShouldSkipAndroidBlankFrame(false, false, true, 0));
	EXPECT_FALSE(ShouldSkipAndroidBlankFrame(false, true, true, 0));
}

TEST(GSPresentationPolicy, PreservesExistingOpenGLBlankSuppression)
{
	EXPECT_TRUE(ShouldSkipAndroidBlankFrame(true, false, false, 1));
	EXPECT_TRUE(ShouldSkipAndroidBlankFrame(true, true, false, 1));
	EXPECT_FALSE(ShouldSkipAndroidBlankFrame(true, true, false, 2));
	EXPECT_FALSE(ShouldSkipAndroidBlankFrame(false, true, false, 0));
}

TEST(GSPresentationPolicy, KeepsAlternatingMidGameFadeFramesOnSubmissionPath)
{
	// GT4 result transitions can alternate between output and blank frames while remaining in
	// SDTV 480p. Only the leading startup blank may bypass presentation.
	constexpr std::array<bool, 6> blank_frames = {true, false, true, false, true, false};
	bool has_current_output = false;
	std::array<bool, 6> skipped = {};

	for (size_t i = 0; i < blank_frames.size(); i++)
	{
		skipped[i] = ShouldSkipAndroidBlankFrame(
			blank_frames[i], has_current_output, true, blank_frames[i] ? 1 : 0);
		if (!blank_frames[i])
			has_current_output = true;
	}

	EXPECT_EQ(skipped, (std::array<bool, 6>{true, false, false, false, false, false}));
}

TEST(GpuProfile, TreatsMaliAndPowerVRAsOneMobilePath)
{
	EXPECT_EQ(GpuProfileDetector::Detect("ARM", "Mali-G57 MC2"), RuntimeGpuProfile::Mobile);
	EXPECT_EQ(GpuProfileDetector::Detect("ARM", "Mali-G715-Immortalis MC11"), RuntimeGpuProfile::Mobile);
	EXPECT_EQ(GpuProfileDetector::Detect("ARM Mali", "Mali-G78"), RuntimeGpuProfile::Mobile);
	EXPECT_EQ(GpuProfileDetector::Detect("Imagination Technologies", "PowerVR Rogue GE8320"),
		RuntimeGpuProfile::Mobile);
	EXPECT_EQ(GpuProfileDetector::Detect("Imagination PowerVR", "IMG DXT-48-1536"),
		RuntimeGpuProfile::Mobile);
}

TEST(GpuProfile, ClassifiesAdrenoIdentity)
{
	EXPECT_EQ(GpuProfileDetector::Detect("Qualcomm", "Adreno (TM) 740"), RuntimeGpuProfile::Adreno);
	EXPECT_EQ(GpuProfileDetector::Detect("Qualcomm Adreno", "Adreno (TM) 650"), RuntimeGpuProfile::Adreno);
	EXPECT_EQ(GpuProfileDetector::Detect("", "ANGLE (Qualcomm, Adreno (TM) 640, OpenGL ES 3.2)"),
		RuntimeGpuProfile::Adreno);
}

TEST(GpuProfile, EveryRecognisedMobileGpuSharesTheUnifiedPath)
{
	// The profile value only carries identity; Mali/PowerVR and Adreno render identically.
	EXPECT_TRUE(UsesMobileGpuPath(RuntimeGpuProfile::Mobile));
	EXPECT_TRUE(UsesMobileGpuPath(RuntimeGpuProfile::Adreno));
	EXPECT_FALSE(UsesMobileGpuPath(RuntimeGpuProfile::Unknown));
}

TEST(GpuProfile, RendererIdentityWinsOverVendorHint)
{
	EXPECT_EQ(GpuProfileDetector::Detect("ARM", "Adreno (TM) 650"), RuntimeGpuProfile::Adreno);
	EXPECT_EQ(GpuProfileDetector::Detect("Qualcomm", "Mali-G715"), RuntimeGpuProfile::Mobile);
	EXPECT_EQ(GpuProfileDetector::Detect("Qualcomm Adreno", "PowerVR Rogue"), RuntimeGpuProfile::Mobile);
}

TEST(GpuProfile, DesktopGpusStayOnTheUnknownPath)
{
	EXPECT_EQ(GpuProfileDetector::Detect("NVIDIA Corporation", "NVIDIA GeForce RTX 4070"),
		RuntimeGpuProfile::Unknown);
	EXPECT_EQ(GpuProfileDetector::Detect("Advanced Micro Devices", "Radeon RX 7900 XTX"),
		RuntimeGpuProfile::Unknown);
	EXPECT_EQ(GpuProfileDetector::Detect("Intel", "Intel(R) UHD Graphics 770"), RuntimeGpuProfile::Unknown);
	EXPECT_EQ(GpuProfileDetector::Detect("", ""), RuntimeGpuProfile::Unknown);

	// "ARM" alone is a CPU vendor string and must not select the mobile GPU path.
	EXPECT_EQ(GpuProfileDetector::Detect("ARM", "Cortex-A78"), RuntimeGpuProfile::Unknown);
}

TEST(GpuProfile, ParsesAdrenoGenerationFromDeviceName)
{
	EXPECT_EQ(GpuProfileDetector::ParseAdrenoGeneration("Adreno (TM) 740"), 7u);
	EXPECT_EQ(GpuProfileDetector::ParseAdrenoGeneration("Adreno (TM) 830"), 8u);
	EXPECT_EQ(GpuProfileDetector::ParseAdrenoGeneration("Adreno X1-85"), 9u);
	EXPECT_EQ(GpuProfileDetector::ParseAdrenoGeneration("Adreno (TM) 505"), 5u);
	EXPECT_EQ(GpuProfileDetector::ParseAdrenoGeneration("Mali-G57"), 0u);
	EXPECT_EQ(GpuProfileDetector::ParseAdrenoGeneration(""), 0u);
}

TEST(GpuProfile, DetectsMediaTekSoCsWithoutFalsePositives)
{
	EXPECT_TRUE(GpuProfileDetector::LooksLikeMediaTekSoC("MediaTek Dimensity 9300"));
	EXPECT_TRUE(GpuProfileDetector::LooksLikeMediaTekSoC("ro.board.platform=mt6877"));
	EXPECT_TRUE(GpuProfileDetector::LooksLikeMediaTekSoC("MT6989Z"));
	EXPECT_TRUE(GpuProfileDetector::LooksLikeMediaTekSoC("mtk Dimensity"));
	EXPECT_FALSE(GpuProfileDetector::LooksLikeMediaTekSoC("Qualcomm Snapdragon 8 Gen 3"));
	EXPECT_FALSE(GpuProfileDetector::LooksLikeMediaTekSoC("mt"));
	EXPECT_FALSE(GpuProfileDetector::LooksLikeMediaTekSoC("synthetic mt12 platform"));
	EXPECT_FALSE(GpuProfileDetector::LooksLikeMediaTekSoC("arm mt12"));
	EXPECT_FALSE(GpuProfileDetector::LooksLikeMediaTekSoC("vendor xmt6989"));
}
