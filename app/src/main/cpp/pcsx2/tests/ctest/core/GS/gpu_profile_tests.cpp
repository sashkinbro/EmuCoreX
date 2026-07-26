// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSGPUProfile.h"

#include <gtest/gtest.h>

TEST(GpuProfile, ResolvesExactAdrenoModels)
{
	const GpuProfileSelection flagship =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 740");
	EXPECT_EQ(flagship.runtime_profile, RuntimeGpuProfile::Adreno);
	EXPECT_EQ(flagship.gpu.architecture, MobileGpuArchitecture::Adreno7xx);
	EXPECT_EQ(flagship.gpu.model_number, 740);
	EXPECT_TRUE(flagship.gpu.recognized);
	EXPECT_EQ(flagship.gs_tuning.pooled_targets, 160u);

	const GpuProfileSelection low_end =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 619L");
	EXPECT_EQ(low_end.gpu.name, "Adreno 619L");
	EXPECT_TRUE(low_end.gpu.recognized);
	EXPECT_TRUE(low_end.gs_tuning.constrained);
	EXPECT_EQ(low_end.gs_tuning.pooled_targets, 84u);
}

TEST(GpuProfile, KeepsUnknownAdrenoInsideItsGeneration)
{
	const GpuProfileSelection profile =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 799");
	EXPECT_EQ(profile.runtime_profile, RuntimeGpuProfile::Adreno);
	EXPECT_EQ(profile.gpu.architecture, MobileGpuArchitecture::Adreno7xx);
	EXPECT_EQ(profile.gpu.model_number, 799);
	EXPECT_FALSE(profile.gpu.recognized);
	EXPECT_EQ(profile.gs_tuning.pooled_targets, 128u);
}

TEST(GpuProfile, ResolvesMaliArchitectureAndCoreCount)
{
	const GpuProfileSelection small =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC2");
	EXPECT_EQ(small.runtime_profile, RuntimeGpuProfile::Mali);
	EXPECT_EQ(small.gpu.architecture, MobileGpuArchitecture::MaliValhall1);
	EXPECT_EQ(small.gpu.core_count, 2);
	EXPECT_TRUE(small.gpu.recognized);
	EXPECT_EQ(small.gs_tuning.pooled_targets, 64u);

	const GpuProfileSelection mid =
		GpuProfileDetector::Resolve("auto", "ARM", "Mali-G710 MC6");
	EXPECT_EQ(mid.gpu.architecture, MobileGpuArchitecture::MaliValhall2);
	EXPECT_EQ(mid.gpu.core_count, 6);
	EXPECT_EQ(mid.gs_tuning.pooled_targets, 112u);

	const GpuProfileSelection legacy =
		GpuProfileDetector::Resolve("auto", "ARM", "Mali-T880 MP12");
	EXPECT_EQ(legacy.gpu.architecture, MobileGpuArchitecture::MaliMidgard);
	EXPECT_EQ(legacy.gpu.core_count, 12);
}

TEST(GpuProfile, ResolvesRecentImmortalisExactly)
{
	const GpuProfileSelection profile =
		GpuProfileDetector::Resolve("auto", "ARM", "Mali-G925-Immortalis MC12");
	EXPECT_EQ(profile.runtime_profile, RuntimeGpuProfile::Mali);
	EXPECT_EQ(profile.gpu.architecture, MobileGpuArchitecture::MaliFifthGen);
	EXPECT_EQ(profile.gpu.name, "Immortalis-G925 MC12");
	EXPECT_TRUE(profile.gpu.recognized);
	EXPECT_FALSE(profile.gs_tuning.constrained);
	EXPECT_EQ(profile.gs_tuning.pooled_targets, 160u);
}

TEST(GpuProfile, DoesNotInferGpuVendorFromSocVendorAlone)
{
	const GpuProfileSelection mediatek =
		GpuProfileDetector::Resolve("auto", "MediaTek", "Unknown GPU");
	EXPECT_EQ(mediatek.runtime_profile, RuntimeGpuProfile::Unknown);
	EXPECT_EQ(mediatek.gpu.architecture, MobileGpuArchitecture::Unknown);

	const GpuProfileSelection powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320");
	EXPECT_EQ(powervr.runtime_profile, RuntimeGpuProfile::PowerVR);

	const GpuProfileSelection unrelated_img =
		GpuProfileDetector::Resolve("auto", "Example", "IMG-compatible display");
	EXPECT_EQ(unrelated_img.runtime_profile, RuntimeGpuProfile::Unknown);
}

TEST(GpuProfile, PreservesExplicitFamilyOverrideWithoutInventingAModel)
{
	const GpuProfileSelection profile =
		GpuProfileDetector::Resolve("mali", "Qualcomm", "Adreno (TM) 740");
	EXPECT_EQ(profile.runtime_profile, RuntimeGpuProfile::Mali);
	EXPECT_FALSE(profile.gpu.recognized);
	EXPECT_EQ(profile.gpu.name, "Unknown Mali");
	EXPECT_TRUE(profile.gs_tuning.constrained);
}

TEST(GpuProfile, ResolvesPowerVRModelsAndArchitectures)
{
	const GpuProfileSelection sgx =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR SGX544");
	EXPECT_EQ(sgx.runtime_profile, RuntimeGpuProfile::PowerVR);
	EXPECT_EQ(sgx.gpu.architecture, MobileGpuArchitecture::PowerVRSeries5);
	EXPECT_EQ(sgx.gpu.model_number, 544);
	EXPECT_TRUE(sgx.gpu.recognized);

	const GpuProfileSelection ge8320 =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320");
	EXPECT_EQ(ge8320.gpu.architecture, MobileGpuArchitecture::PowerVRRogue);
	EXPECT_EQ(ge8320.gpu.model_number, 8320);
	EXPECT_TRUE(ge8320.gs_tuning.constrained);

	const GpuProfileSelection gm9446 =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GM9446");
	EXPECT_EQ(gm9446.gpu.architecture, MobileGpuArchitecture::PowerVRRogue);
	EXPECT_TRUE(gm9446.gpu.recognized);

	const GpuProfileSelection dxt =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR DXT-48-1536");
	EXPECT_EQ(dxt.gpu.architecture, MobileGpuArchitecture::PowerVRVolcanic);
	EXPECT_FALSE(dxt.gs_tuning.constrained);

	const GpuProfileSelection b_series =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR BXM-8-256");
	EXPECT_EQ(b_series.gpu.architecture, MobileGpuArchitecture::PowerVR);
}

TEST(GpuDriverProfile, SeparatesProprietaryAdrenoFromTurnip)
{
	MobileDriverContext proprietary;
	proprietary.api = MobileGpuApi::Vulkan;
	proprietary.vendor_id = 0x5143;
	proprietary.driver_id = 8;
	proprietary.driver_version = (512u << 22) | (800u << 12);
	proprietary.android_sdk = 35;

	const GpuProfileSelection stock =
		GpuProfileDetector::Resolve("auto", "Qualcomm Adreno", "Adreno (TM) 740", proprietary);
	EXPECT_EQ(stock.driver.driver, MobileGpuDriver::QualcommProprietary);
	EXPECT_TRUE(stock.driver.HasBug(DriverBug::BrokenProvokingVertex));
	EXPECT_TRUE(stock.driver.HasBug(DriverBug::BrokenDynamicRendering));
	EXPECT_TRUE(stock.driver.UsesWorkaround(DriverWorkaround::DisableProvokingVertex));
	EXPECT_TRUE(stock.driver.UsesWorkaround(DriverWorkaround::UseD24S8Depth));
	EXPECT_TRUE(stock.driver.UsesWorkaround(DriverWorkaround::AvoidReversedDepthRange));
	EXPECT_TRUE(stock.driver.UsesWorkaround(DriverWorkaround::UseStagingImageForReadback));
	EXPECT_TRUE(stock.driver.UsesWorkaround(DriverWorkaround::SerializeShaderCompilation));

	MobileDriverContext turnip = proprietary;
	turnip.driver_id = 18;
	turnip.driver_name = "Mesa Turnip";
	const GpuProfileSelection mesa =
		GpuProfileDetector::Resolve("auto", "Qualcomm Adreno", "Adreno (TM) 740", turnip);
	EXPECT_EQ(mesa.driver.driver, MobileGpuDriver::MesaTurnip);
	EXPECT_FALSE(mesa.driver.HasBug(DriverBug::BrokenProvokingVertex));
	EXPECT_FALSE(mesa.driver.UsesWorkaround(DriverWorkaround::DisableProvokingVertex));
	EXPECT_TRUE(mesa.driver.UsesWorkaround(DriverWorkaround::SerializeShaderCompilation));

	turnip.driver_id = 0;
	turnip.driver_name = "Mesa 25.1.0 Turnip";
	const GpuProfileSelection mesa_by_name =
		GpuProfileDetector::Resolve("auto", "Qualcomm Adreno", "Adreno (TM) 740", turnip);
	EXPECT_EQ(mesa_by_name.driver.driver, MobileGpuDriver::MesaTurnip);
	EXPECT_FALSE(mesa_by_name.driver.HasBug(DriverBug::BrokenProvokingVertex));
}

TEST(GpuDriverProfile, KeepsMesaMaliAndPowerVRSeparateFromProprietaryDrivers)
{
	MobileDriverContext panvk;
	panvk.api = MobileGpuApi::Vulkan;
	panvk.driver_id = 20;
	panvk.driver_name = "Mesa PanVK";
	const GpuProfileSelection mali =
		GpuProfileDetector::Resolve("auto", "ARM", "Mali-G710 MC6", panvk);
	EXPECT_EQ(mali.driver.driver, MobileGpuDriver::MesaPanVK);
	EXPECT_FALSE(mali.driver.UsesWorkaround(DriverWorkaround::UseDescriptorSets));

	MobileDriverContext mesa_pvr;
	mesa_pvr.api = MobileGpuApi::Vulkan;
	mesa_pvr.driver_id = 25;
	mesa_pvr.driver_name = "Mesa PowerVR";
	const GpuProfileSelection powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", mesa_pvr);
	EXPECT_EQ(powervr.driver.driver, MobileGpuDriver::MesaPowerVR);
	EXPECT_FALSE(powervr.driver.UsesWorkaround(DriverWorkaround::AvoidClearLoadOpRenderPass));
}

TEST(GpuDriverProfile, AppliesVersionBoundedMaliRules)
{
	MobileDriverContext r38;
	r38.api = MobileGpuApi::Vulkan;
	r38.vendor_id = 0x13B5;
	r38.driver_id = 9;
	r38.driver_version = (38u << 22) | (1u << 12);
	r38.android_sdk = 34;
	r38.max_draw_indirect_count = 0xffffffffu;

	const GpuProfileSelection old =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC4", r38);
	EXPECT_EQ(old.driver.driver, MobileGpuDriver::ArmProprietary);
	EXPECT_EQ(old.driver.version.major, 38);
	EXPECT_EQ(old.driver.version.minor, 1);
	EXPECT_TRUE(old.driver.HasBug(DriverBug::BrokenImagelessFramebuffer));
	EXPECT_TRUE(old.driver.HasBug(DriverBug::BrokenDynamicRendering));
	EXPECT_TRUE(old.driver.UsesWorkaround(DriverWorkaround::UseDescriptorSets));
	EXPECT_TRUE(old.driver.UsesWorkaround(DriverWorkaround::RewriteConstantLoads));

	MobileDriverContext r52 = r38;
	r52.driver_version = (52u << 22);
	const GpuProfileSelection current =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC4", r52);
	EXPECT_FALSE(current.driver.HasBug(DriverBug::BrokenImagelessFramebuffer));
	EXPECT_FALSE(current.driver.HasBug(DriverBug::BrokenDynamicRendering));
	EXPECT_FALSE(current.driver.HasBug(DriverBug::BrokenExtendedDynamicState));
	EXPECT_FALSE(current.driver.UsesWorkaround(DriverWorkaround::RewriteConstantLoads));
	EXPECT_TRUE(current.driver.HasBug(DriverBug::BrokenPushDescriptors));
}

TEST(GpuDriverProfile, RestrictsMaliJobManagerRuleToIndirectCountOne)
{
	MobileDriverContext jm;
	jm.api = MobileGpuApi::Vulkan;
	jm.driver_id = 9;
	jm.driver_version = (48u << 22);
	jm.max_draw_indirect_count = 1;

	const GpuProfileSelection job_manager =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC4", jm);
	EXPECT_TRUE(job_manager.driver.HasBug(DriverBug::BrokenExtendedDynamicState));

	MobileDriverContext csf = jm;
	csf.max_draw_indirect_count = 2;
	const GpuProfileSelection command_stream_frontend =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G710 MC6", csf);
	EXPECT_FALSE(command_stream_frontend.driver.HasBug(DriverBug::BrokenExtendedDynamicState));
}

TEST(GpuDriverProfile, RecognizesLegacyMaliAndPowerVRCutoffs)
{
	MobileDriverContext mali;
	mali.api = MobileGpuApi::Vulkan;
	mali.vendor_id = 0x13B5;
	mali.driver_id = 9;
	mali.driver_version = 0xaa9c4b29u;
	const GpuProfileSelection legacy_mali =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-T880 MP12", mali);
	EXPECT_TRUE(legacy_mali.driver.version.legacy_hash);
	EXPECT_TRUE(legacy_mali.driver.HasBug(DriverBug::BrokenEmptyRenderPass));
	EXPECT_TRUE(legacy_mali.driver.HasBug(DriverBug::BrokenUniformIndexing));
	EXPECT_TRUE(legacy_mali.driver.UsesWorkaround(DriverWorkaround::RewriteUniformIndexing));
	EXPECT_TRUE(legacy_mali.driver.UsesWorkaround(DriverWorkaround::UseExplicitBarrierSubresourceCounts));

	MobileDriverContext powervr;
	powervr.api = MobileGpuApi::Vulkan;
	powervr.vendor_id = 0x1010;
	powervr.driver_id = 7;
	powervr.driver_version = 0x00582557u;
	const GpuProfileSelection old_powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", powervr);
	EXPECT_TRUE(old_powervr.driver.UsesWorkaround(DriverWorkaround::AlignSwapchainWidthTo32));
	EXPECT_TRUE(old_powervr.driver.UsesWorkaround(DriverWorkaround::AvoidClearLoadOpRenderPass));

	powervr.driver_version = 0x00582558u;
	const GpuProfileSelection fixed_powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", powervr);
	EXPECT_FALSE(fixed_powervr.driver.UsesWorkaround(DriverWorkaround::AlignSwapchainWidthTo32));
}

TEST(GpuDriverProfile, PreservesDepthStencilOnlyOnProprietaryAdreno5xx)
{
	MobileDriverContext proprietary;
	proprietary.api = MobileGpuApi::Vulkan;
	proprietary.vendor_id = 0x5143;
	proprietary.driver_id = 8;

	const GpuProfileSelection adreno530 =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 530", proprietary);
	EXPECT_TRUE(adreno530.driver.UsesWorkaround(DriverWorkaround::PreserveDepthStencilAttachment));
	EXPECT_TRUE(adreno530.driver.UsesWorkaround(DriverWorkaround::EmulateColorWriteMask));

	const GpuProfileSelection adreno740 =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 740", proprietary);
	EXPECT_FALSE(adreno740.driver.UsesWorkaround(DriverWorkaround::PreserveDepthStencilAttachment));

	proprietary.driver_id = 18;
	proprietary.driver_name = "Mesa Turnip";
	const GpuProfileSelection turnip530 =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 530", proprietary);
	EXPECT_FALSE(turnip530.driver.UsesWorkaround(DriverWorkaround::PreserveDepthStencilAttachment));
}

TEST(GpuDriverProfile, ParsesOpenGLMaliReleaseAndUsesModelRule)
{
	MobileDriverContext gl;
	gl.api = MobileGpuApi::OpenGL;
	gl.driver_name = "Mali-G57";
	gl.api_version_string = "OpenGL ES 3.2 v1.r54p1-01rel0";
	gl.android_sdk = 35;

	const GpuProfileSelection profile =
		GpuProfileDetector::Resolve("auto", "ARM", "Mali-G57 MC2", gl);
	EXPECT_EQ(profile.driver.version.major, 54);
	EXPECT_EQ(profile.driver.version.minor, 1);
	EXPECT_TRUE(profile.driver.HasBug(DriverBug::BrokenVSync));
	EXPECT_TRUE(profile.driver.UsesWorkaround(DriverWorkaround::OrphanBufferOnUpload));
	EXPECT_TRUE(profile.driver.UsesWorkaround(DriverWorkaround::ForceFifoPresent));
	EXPECT_TRUE(profile.driver.UsesWorkaround(DriverWorkaround::SerializeShaderCompilation));
}

TEST(GpuDriverProfile, SelectsOpenGLStreamingFallbackPerProprietaryVendor)
{
	MobileDriverContext gl;
	gl.api = MobileGpuApi::OpenGL;
	gl.api_version_string = "OpenGL ES 3.2";
	gl.android_sdk = 35;

	const GpuProfileSelection adreno =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 740", gl);
	EXPECT_EQ(adreno.driver.driver, MobileGpuDriver::QualcommProprietary);
	EXPECT_TRUE(adreno.driver.UsesWorkaround(DriverWorkaround::OrphanBufferOnUpload));
	EXPECT_TRUE(adreno.driver.UsesWorkaround(DriverWorkaround::RewriteBooleanNegation));

	const GpuProfileSelection powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", gl);
	EXPECT_EQ(powervr.driver.driver, MobileGpuDriver::ImaginationProprietary);
	EXPECT_TRUE(powervr.driver.UsesWorkaround(DriverWorkaround::OrphanBufferOnUpload));
	EXPECT_TRUE(powervr.driver.UsesWorkaround(DriverWorkaround::StoreBitwiseNegationInTemporary));
	EXPECT_TRUE(powervr.driver.UsesWorkaround(DriverWorkaround::GenerateMipmapManuallyForTallTextures));
}
