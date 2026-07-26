// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/Common/GSGPUProfile.h"

#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <initializer_list>
#include <string>

namespace
{
struct ModelCase
{
	const char* renderer;
	MobileGpuArchitecture architecture;
	u16 model;
};

static u64 WorkaroundMask(std::initializer_list<DriverWorkaround> workarounds)
{
	u64 mask = 0;
	for (const DriverWorkaround workaround : workarounds)
		mask |= (u64{1} << static_cast<u8>(workaround));
	return mask;
}

static void ExpectTuningInvariants(const GpuProfileSelection& profile)
{
	EXPECT_GE(profile.gs_tuning.pooled_targets, 40u);
	EXPECT_LE(profile.gs_tuning.pooled_targets, 160u);
	EXPECT_EQ(profile.gs_tuning.pooled_targets, profile.gs_tuning.pooled_textures);
	EXPECT_EQ(profile.gs_tuning.constrained, profile.gs_tuning.pooled_targets < 128u);
	EXPECT_TRUE(profile.gs_tuning.prefer_new_textures);
}

static MobileDriverContext MakeOpenGLContext(RuntimeGpuProfile vendor, bool angle)
{
	MobileDriverContext context;
	context.api = MobileGpuApi::OpenGL;
	context.android_sdk = 35;
	context.driver_name = angle ? "ANGLE Vulkan backend" : "";
	switch (vendor)
	{
		case RuntimeGpuProfile::Adreno:
			context.api_version_string = "OpenGL ES 3.2 V@0842.41";
			break;
		case RuntimeGpuProfile::Mali:
			context.api_version_string = "OpenGL ES 3.2 v1.r54p1-01rel0";
			break;
		case RuntimeGpuProfile::PowerVR:
			context.api_version_string = "OpenGL ES 3.2 build 1.9@4850625";
			break;
		default:
			break;
	}
	return context;
}

static MobileDriverContext MakeVulkanContext(RuntimeGpuProfile vendor, bool mesa)
{
	MobileDriverContext context;
	context.api = MobileGpuApi::Vulkan;
	context.android_sdk = 35;
	context.max_draw_indirect_count = 0xffffffffu;
	switch (vendor)
	{
		case RuntimeGpuProfile::Adreno:
			context.vendor_id = 0x5143;
			context.driver_id = mesa ? 18 : 8;
			context.driver_name = mesa ? "Mesa Turnip" : "Qualcomm proprietary";
			context.driver_version = (512u << 22) | (900u << 12);
			break;
		case RuntimeGpuProfile::Mali:
			context.vendor_id = 0x13B5;
			context.driver_id = mesa ? 20 : 9;
			context.driver_name = mesa ? "Mesa PanVK" : "ARM proprietary";
			context.driver_version = (54u << 22) | (1u << 12);
			break;
		case RuntimeGpuProfile::PowerVR:
			context.vendor_id = 0x1010;
			context.driver_id = mesa ? 25 : 7;
			context.driver_name = mesa ? "Mesa PowerVR" : "Imagination proprietary";
			// First raw version at which the legacy swapchain-width workaround is disabled.
			context.driver_version = 0x00582558u;
			break;
		default:
			break;
	}
	return context;
}

static u64 ExpectedOpenGLWorkarounds(
	RuntimeGpuProfile vendor, MobileGpuArchitecture architecture, u16 model)
{
	switch (vendor)
	{
		case RuntimeGpuProfile::Adreno:
			return WorkaroundMask({DriverWorkaround::RewriteBooleanNegation});
		case RuntimeGpuProfile::Mali:
			return WorkaroundMask(model == 57 ?
				std::initializer_list<DriverWorkaround>{
					DriverWorkaround::ScalarizeVectorBitwiseAnd,
					DriverWorkaround::ForceFifoPresent} :
				std::initializer_list<DriverWorkaround>{
					DriverWorkaround::ScalarizeVectorBitwiseAnd});
		case RuntimeGpuProfile::PowerVR:
			return WorkaroundMask(architecture == MobileGpuArchitecture::PowerVRSeries5 ?
				std::initializer_list<DriverWorkaround>{
					DriverWorkaround::GenerateMipmapManuallyForTallTextures} :
				std::initializer_list<DriverWorkaround>{});
		default:
			return 0;
	}
}

static u64 ExpectedVulkanWorkarounds(
	RuntimeGpuProfile vendor, MobileGpuArchitecture architecture, u16 model)
{
	switch (vendor)
	{
		case RuntimeGpuProfile::Adreno:
		{
			u64 mask = WorkaroundMask({
				DriverWorkaround::DisableProvokingVertex,
				DriverWorkaround::PreferCoherentReadback,
			});
			if (architecture == MobileGpuArchitecture::Adreno5xx)
			{
				mask |= WorkaroundMask({
					DriverWorkaround::EmulateColorWriteMask,
				});
			}
			return mask;
		}
		case RuntimeGpuProfile::Mali:
		{
			u64 mask = WorkaroundMask({
				DriverWorkaround::UseDescriptorSets,
				DriverWorkaround::DisableAttachmentFeedbackLoopLayout,
				DriverWorkaround::PreferCoherentReadback,
				DriverWorkaround::ScalarizeVectorBitwiseAnd,
			});
			if (model == 57)
				mask |= WorkaroundMask({DriverWorkaround::ForceFifoPresent});
			return mask;
		}
		case RuntimeGpuProfile::PowerVR:
			return WorkaroundMask({
				DriverWorkaround::UseDescriptorSets,
				DriverWorkaround::DisableAttachmentFeedbackLoopLayout,
			});
		default:
			return 0;
	}
}

static void VerifyModelAndDriverIsolation(const ModelCase& test_case, const char* vendor_hint,
	RuntimeGpuProfile vendor)
{
	SCOPED_TRACE(test_case.renderer);

	const GpuProfileSelection detected =
		GpuProfileDetector::Resolve("auto", vendor_hint, test_case.renderer);
	EXPECT_EQ(detected.runtime_profile, vendor);
	EXPECT_EQ(detected.gpu.architecture, test_case.architecture);
	EXPECT_EQ(detected.gpu.model_number, test_case.model);
	EXPECT_TRUE(detected.gpu.recognized);
	ExpectTuningInvariants(detected);

	const GpuProfileSelection gl = GpuProfileDetector::Resolve(
		"auto", vendor_hint, test_case.renderer, MakeOpenGLContext(vendor, false));
	EXPECT_EQ(gl.driver.workarounds,
		ExpectedOpenGLWorkarounds(vendor, test_case.architecture, test_case.model));

	const GpuProfileSelection angle = GpuProfileDetector::Resolve(
		"auto", vendor_hint, test_case.renderer, MakeOpenGLContext(vendor, true));
	EXPECT_EQ(angle.driver.driver, MobileGpuDriver::Angle);
	EXPECT_EQ(angle.driver.workarounds, 0u);

	const GpuProfileSelection vk = GpuProfileDetector::Resolve(
		"auto", vendor_hint, test_case.renderer, MakeVulkanContext(vendor, false));
	EXPECT_EQ(vk.driver.workarounds,
		ExpectedVulkanWorkarounds(vendor, test_case.architecture, test_case.model));

	const GpuProfileSelection mesa = GpuProfileDetector::Resolve(
		"auto", vendor_hint, test_case.renderer, MakeVulkanContext(vendor, true));
	EXPECT_EQ(mesa.driver.workarounds, 0u);
}

static void VerifyAdrenoGeneration(
	MobileGpuArchitecture architecture, std::initializer_list<const char*> models)
{
	for (const char* model : models)
	{
		const std::string renderer = "Adreno (TM) " + std::string(model);
		const u16 model_number = static_cast<u16>(std::stoi(model));
		VerifyModelAndDriverIsolation(
			{renderer.c_str(), architecture, model_number}, "Qualcomm", RuntimeGpuProfile::Adreno);
	}
}

static void VerifyMaliGeneration(char series, MobileGpuArchitecture architecture,
	std::initializer_list<u16> models)
{
	for (const u16 model : models)
	{
		const std::string renderer = (series == 'U') ?
			("Mali-" + std::to_string(model)) :
			("Mali-" + std::string(1, series) + std::to_string(model));
		VerifyModelAndDriverIsolation(
			{renderer.c_str(), architecture, model}, "ARM Mali", RuntimeGpuProfile::Mali);
	}
}

static void VerifyPowerVRGeneration(MobileGpuArchitecture architecture,
	std::initializer_list<const char*> models)
{
	for (const char* model : models)
	{
		const std::string renderer = "PowerVR " + std::string(model);
		size_t digit = 0;
		while (model[digit] != '\0' && !std::isdigit(static_cast<unsigned char>(model[digit])))
			digit++;
		const u16 model_number = static_cast<u16>(std::stoi(model + digit));
		VerifyModelAndDriverIsolation(
			{renderer.c_str(), architecture, model_number},
			"Imagination Technologies", RuntimeGpuProfile::PowerVR);
	}
}
} // namespace

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

	const GpuProfileSelection sgx_mp =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR SGX 544MP2");
	EXPECT_EQ(sgx_mp.gpu.architecture, MobileGpuArchitecture::PowerVRSeries5);
	EXPECT_EQ(sgx_mp.gpu.model_number, 544);
	EXPECT_TRUE(sgx_mp.gpu.recognized);

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

	MobileDriverContext turnip = proprietary;
	turnip.driver_id = 18;
	turnip.driver_name = "Mesa Turnip";
	const GpuProfileSelection mesa =
		GpuProfileDetector::Resolve("auto", "Qualcomm Adreno", "Adreno (TM) 740", turnip);
	EXPECT_EQ(mesa.driver.driver, MobileGpuDriver::MesaTurnip);
	EXPECT_FALSE(mesa.driver.HasBug(DriverBug::BrokenProvokingVertex));
	EXPECT_FALSE(mesa.driver.UsesWorkaround(DriverWorkaround::DisableProvokingVertex));

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
	EXPECT_TRUE(old.driver.HasBug(DriverBug::BrokenConstantLoad));

	MobileDriverContext r52 = r38;
	r52.driver_version = (52u << 22);
	const GpuProfileSelection current =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC4", r52);
	EXPECT_FALSE(current.driver.HasBug(DriverBug::BrokenImagelessFramebuffer));
	EXPECT_FALSE(current.driver.HasBug(DriverBug::BrokenDynamicRendering));
	EXPECT_FALSE(current.driver.HasBug(DriverBug::BrokenExtendedDynamicState));
	EXPECT_FALSE(current.driver.HasBug(DriverBug::BrokenConstantLoad));
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

	MobileDriverContext powervr;
	powervr.api = MobileGpuApi::Vulkan;
	powervr.vendor_id = 0x1010;
	powervr.driver_id = 7;
	powervr.driver_version = 0x00582557u;
	const GpuProfileSelection old_powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", powervr);
	EXPECT_TRUE(old_powervr.driver.UsesWorkaround(DriverWorkaround::AlignSwapchainWidthTo32));
	EXPECT_FALSE(old_powervr.driver.UsesWorkaround(DriverWorkaround::AvoidClearLoadOpRenderPass));

	powervr.driver_version = 0x00582558u;
	const GpuProfileSelection fixed_powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", powervr);
	EXPECT_FALSE(fixed_powervr.driver.UsesWorkaround(DriverWorkaround::AlignSwapchainWidthTo32));
}

TEST(GpuDriverProfile, ScopesColorWriteMaskEmulationToProprietaryAdreno5xx)
{
	MobileDriverContext proprietary;
	proprietary.api = MobileGpuApi::Vulkan;
	proprietary.vendor_id = 0x5143;
	proprietary.driver_id = 8;

	const GpuProfileSelection adreno530 =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 530", proprietary);
	EXPECT_TRUE(adreno530.driver.UsesWorkaround(DriverWorkaround::EmulateColorWriteMask));

	const GpuProfileSelection adreno740 =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 740", proprietary);
	EXPECT_FALSE(adreno740.driver.UsesWorkaround(DriverWorkaround::EmulateColorWriteMask));

	proprietary.driver_id = 18;
	proprietary.driver_name = "Mesa Turnip";
	const GpuProfileSelection turnip530 =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 530", proprietary);
	EXPECT_FALSE(turnip530.driver.UsesWorkaround(DriverWorkaround::EmulateColorWriteMask));
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
	EXPECT_TRUE(profile.driver.UsesWorkaround(DriverWorkaround::ForceFifoPresent));
}

TEST(GpuDriverProfile, KeepsOpenGLFastStreamingAndBoundsCompilerWorkarounds)
{
	MobileDriverContext gl;
	gl.api = MobileGpuApi::OpenGL;
	gl.api_version_string = "OpenGL ES 3.2 V@0800";
	gl.android_sdk = 35;

	const GpuProfileSelection adreno =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 840", gl);
	EXPECT_EQ(adreno.driver.driver, MobileGpuDriver::QualcommProprietary);
	EXPECT_TRUE(adreno.driver.UsesWorkaround(DriverWorkaround::RewriteBooleanNegation));

	gl.api_version_string = "OpenGL ES 3.2 build 1.9@4850625";
	const GpuProfileSelection powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", gl);
	EXPECT_EQ(powervr.driver.driver, MobileGpuDriver::ImaginationProprietary);
	EXPECT_EQ(powervr.driver.version.major, 1);
	EXPECT_EQ(powervr.driver.version.minor, 9);
	EXPECT_EQ(powervr.driver.version.build, 4850625u);
	EXPECT_FALSE(powervr.driver.UsesWorkaround(DriverWorkaround::StoreBitwiseNegationInTemporary));
	EXPECT_FALSE(powervr.driver.UsesWorkaround(DriverWorkaround::GenerateMipmapManuallyForTallTextures));

	gl.api_version_string = "OpenGL ES 3.2 build 1.7@4000000";
	const GpuProfileSelection legacy_powervr =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR SGX544", gl);
	EXPECT_TRUE(legacy_powervr.driver.UsesWorkaround(DriverWorkaround::StoreBitwiseNegationInTemporary));
	EXPECT_TRUE(legacy_powervr.driver.UsesWorkaround(DriverWorkaround::GenerateMipmapManuallyForTallTextures));
}

TEST(GpuDriverProfile, KeepsEveryAdrenoGenerationOnDirectReadbackPath)
{
	MobileDriverContext proprietary;
	proprietary.api = MobileGpuApi::Vulkan;
	proprietary.vendor_id = 0x5143;
	proprietary.driver_id = 8;
	proprietary.driver_version = (512u << 22) | (900u << 12);
	proprietary.android_sdk = 35;

	for (const char* renderer : {
			 "Adreno (TM) 530", "Adreno (TM) 650", "Adreno (TM) 740", "Adreno (TM) 840"})
	{
		SCOPED_TRACE(renderer);
		const GpuProfileSelection profile =
			GpuProfileDetector::Resolve("auto", "Qualcomm", renderer, proprietary);
		EXPECT_TRUE(profile.driver.HasBug(DriverBug::SlowOptimalImageToBufferCopy));
		EXPECT_TRUE(profile.driver.UsesWorkaround(DriverWorkaround::PreferCoherentReadback));
		EXPECT_EQ(profile.driver.workarounds,
			ExpectedVulkanWorkarounds(
				RuntimeGpuProfile::Adreno, profile.gpu.architecture, profile.gpu.model_number));
	}
}

TEST(GpuDriverProfile, ScopesVulkanMaliG57FifoToArmProprietaryDriver)
{
	MobileDriverContext proprietary = MakeVulkanContext(RuntimeGpuProfile::Mali, false);
	const GpuProfileSelection stock =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC4", proprietary);
	EXPECT_EQ(stock.driver.driver, MobileGpuDriver::ArmProprietary);
	EXPECT_TRUE(stock.driver.UsesWorkaround(DriverWorkaround::ForceFifoPresent));

	MobileDriverContext panvk = MakeVulkanContext(RuntimeGpuProfile::Mali, true);
	const GpuProfileSelection mesa =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC4", panvk);
	EXPECT_EQ(mesa.driver.driver, MobileGpuDriver::MesaPanVK);
	EXPECT_FALSE(mesa.driver.UsesWorkaround(DriverWorkaround::ForceFifoPresent));

	const GpuProfileSelection g68 =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G68 MC4", proprietary);
	EXPECT_FALSE(g68.driver.UsesWorkaround(DriverWorkaround::ForceFifoPresent));
}

TEST(GpuDriverProfile, BoundsPowerVRClearLoadOpFallbackToAffectedDriverRange)
{
	MobileDriverContext powervr;
	powervr.api = MobileGpuApi::Vulkan;
	powervr.vendor_id = 0x1010;
	powervr.driver_id = 7;
	powervr.driver_version = (1u << 22) | (8u << 12) | 42u;

	const GpuProfileSelection affected =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", powervr);
	EXPECT_TRUE(affected.driver.UsesWorkaround(DriverWorkaround::AvoidClearLoadOpRenderPass));

	powervr.driver_version = (1u << 22) | (10u << 12);
	const GpuProfileSelection fixed =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320", powervr);
	EXPECT_FALSE(fixed.driver.UsesWorkaround(DriverWorkaround::AvoidClearLoadOpRenderPass));
}

TEST(GpuProfileMatrix, VerifiesEveryAdrenoModelAndDriverIsolation)
{
	VerifyAdrenoGeneration(MobileGpuArchitecture::Adreno2xx,
		{"200", "203", "205", "220", "225"});
	VerifyAdrenoGeneration(MobileGpuArchitecture::Adreno3xx,
		{"302", "303", "304", "305", "306", "308", "320", "330"});
	VerifyAdrenoGeneration(MobileGpuArchitecture::Adreno4xx,
		{"405", "418", "420", "430"});
	VerifyAdrenoGeneration(MobileGpuArchitecture::Adreno5xx,
		{"504", "505", "506", "507", "508", "509", "510", "512", "530", "540"});
	VerifyAdrenoGeneration(MobileGpuArchitecture::Adreno6xx,
		{"605", "608", "609", "610", "610L", "612", "613", "615", "616", "618",
			"619L", "619", "620", "630", "640", "642L", "642", "643", "644", "650",
			"660", "663", "675", "680", "685", "690", "695"});
	VerifyAdrenoGeneration(MobileGpuArchitecture::Adreno7xx,
		{"702", "710", "720", "722", "725", "730", "732", "735", "740", "750",
			"760", "765", "775"});
	VerifyAdrenoGeneration(MobileGpuArchitecture::Adreno8xx,
		{"810", "820", "825", "829", "830", "840", "845", "850", "860", "870"});

	static constexpr ModelCase laptop_models[] = {
		{"Adreno X1-45", MobileGpuArchitecture::AdrenoX, 145},
		{"Adreno X1-85", MobileGpuArchitecture::AdrenoX, 185},
		{"Adreno X2-45", MobileGpuArchitecture::AdrenoX, 245},
		{"Adreno X2-85", MobileGpuArchitecture::AdrenoX, 285},
	};
	for (const ModelCase& model : laptop_models)
		VerifyModelAndDriverIsolation(model, "Qualcomm", RuntimeGpuProfile::Adreno);
}

TEST(GpuProfileMatrix, VerifiesEveryMaliModelAndDriverIsolation)
{
	VerifyMaliGeneration('U', MobileGpuArchitecture::MaliUtgard, {200, 300, 400, 450, 470});
	VerifyMaliGeneration('T', MobileGpuArchitecture::MaliMidgard,
		{600, 604, 620, 624, 628, 658, 678, 720, 760, 820, 830, 860, 880});
	VerifyMaliGeneration('G', MobileGpuArchitecture::MaliBifrost, {31, 51, 52, 71, 72, 76});
	VerifyMaliGeneration('G', MobileGpuArchitecture::MaliValhall1, {57, 68, 77, 78});
	VerifyMaliGeneration('G', MobileGpuArchitecture::MaliValhall2, {310, 510, 610, 710});
	VerifyMaliGeneration('G', MobileGpuArchitecture::MaliValhall3, {615, 715});
	VerifyMaliGeneration('G', MobileGpuArchitecture::MaliFifthGen, {620, 720, 625, 725, 925});
	VerifyMaliGeneration('G', MobileGpuArchitecture::MaliG1, {1});

	static constexpr ModelCase immortalis_models[] = {
		{"Immortalis-G715", MobileGpuArchitecture::MaliValhall3, 715},
		{"Immortalis-G720", MobileGpuArchitecture::MaliFifthGen, 720},
		{"Immortalis-G925", MobileGpuArchitecture::MaliFifthGen, 925},
	};
	for (const ModelCase& model : immortalis_models)
		VerifyModelAndDriverIsolation(model, "ARM Mali", RuntimeGpuProfile::Mali);
}

TEST(GpuProfileMatrix, KeepsModernMediaTekMaliOnItsFastResourcePolicy)
{
	MobileDriverContext current;
	current.api = MobileGpuApi::Vulkan;
	current.driver_id = 9; // VK_DRIVER_ID_ARM_PROPRIETARY
	current.driver_name = "ARM proprietary";
	current.driver_version = (52u << 22);
	current.max_draw_indirect_count = 0xffffffffu;

	const GpuProfileSelection flagship =
		GpuProfileDetector::Resolve("mediatek", "ARM", "Mali-G925-Immortalis MC12", current);
	EXPECT_TRUE(flagship.is_mediatek_soc);
	EXPECT_EQ(flagship.runtime_profile, RuntimeGpuProfile::Mali);
	EXPECT_EQ(flagship.gpu.architecture, MobileGpuArchitecture::MaliFifthGen);
	EXPECT_EQ(flagship.gpu.model_number, 925);
	EXPECT_FALSE(flagship.gs_tuning.constrained);
	EXPECT_TRUE(flagship.gs_tuning.prefer_new_textures);
	EXPECT_FALSE(flagship.driver.HasBug(DriverBug::BrokenDynamicRendering));
	EXPECT_FALSE(flagship.driver.HasBug(DriverBug::BrokenExtendedDynamicState));

	const GpuProfileSelection g710 =
		GpuProfileDetector::Resolve("mediatek", "ARM", "Mali-G710 MC10", current);
	EXPECT_EQ(g710.gpu.architecture, MobileGpuArchitecture::MaliValhall2);
	EXPECT_FALSE(g710.gs_tuning.constrained);
	EXPECT_TRUE(g710.gs_tuning.prefer_new_textures);

	const GpuProfileSelection g715 =
		GpuProfileDetector::Resolve("mediatek", "ARM", "Immortalis-G715 MC11", current);
	EXPECT_EQ(g715.gpu.architecture, MobileGpuArchitecture::MaliValhall3);
	EXPECT_FALSE(g715.gs_tuning.constrained);
	EXPECT_TRUE(g715.gs_tuning.prefer_new_textures);

	const GpuProfileSelection midrange =
		GpuProfileDetector::Resolve("mediatek", "ARM", "Mali-G720 MC7", current);
	EXPECT_TRUE(midrange.is_mediatek_soc);
	EXPECT_EQ(midrange.gpu.architecture, MobileGpuArchitecture::MaliFifthGen);
	EXPECT_FALSE(midrange.gs_tuning.constrained);
	EXPECT_TRUE(midrange.gs_tuning.prefer_new_textures);

	MobileDriverContext powervr = current;
	powervr.driver_id = 7; // VK_DRIVER_ID_IMAGINATION_PROPRIETARY
	powervr.driver_name = "Imagination proprietary";
	powervr.driver_version = (1u << 22) | (10u << 12);
	const GpuProfileSelection legacy_powervr =
		GpuProfileDetector::Resolve("mediatek", "Imagination Technologies", "PowerVR GE8320", powervr);
	EXPECT_TRUE(legacy_powervr.is_mediatek_soc);
	EXPECT_EQ(legacy_powervr.runtime_profile, RuntimeGpuProfile::PowerVR);
	EXPECT_TRUE(legacy_powervr.driver.UsesWorkaround(DriverWorkaround::UseDescriptorSets));
	EXPECT_FALSE(legacy_powervr.driver.UsesWorkaround(DriverWorkaround::ScalarizeVectorBitwiseAnd));
}

TEST(GpuProfileMatrix, VerifiesEveryPowerVRModelAndDriverIsolation)
{
	VerifyPowerVRGeneration(MobileGpuArchitecture::PowerVRSeries5,
		{"SGX530", "SGX531", "SGX535", "SGX540", "SGX543", "SGX544", "SGX545"});
	VerifyPowerVRGeneration(MobileGpuArchitecture::PowerVRRogue,
		{"G6200", "G6230", "G6400", "G6430", "G6630", "G6110", "GE6250", "GX6250",
			"GX6450", "GX6650", "GR6500", "GT7200", "GT7400", "GT7600", "GT7800",
			"GE8100", "GE8300", "GE8310", "GE8320", "GE8322", "GE9215", "GE9216",
			"GE9230", "GE9300", "GE9310", "GE9420", "GM9246", "GM9445", "GM9446",
			"GM9624"});

	static constexpr ModelCase architecture_families[] = {
		{"PowerVR A-Series", MobileGpuArchitecture::PowerVR, 0},
		{"PowerVR BXS-4-64", MobileGpuArchitecture::PowerVR, 0},
		{"PowerVR CXT-48-1024", MobileGpuArchitecture::PowerVRVolcanic, 0},
		{"PowerVR DXT-72-2304", MobileGpuArchitecture::PowerVRVolcanic, 0},
	};
	for (const ModelCase& model : architecture_families)
		VerifyModelAndDriverIsolation(model, "Imagination Technologies", RuntimeGpuProfile::PowerVR);
}

TEST(GpuProfileMatrix, RejectsAdjacentAndSyntheticModelsWithoutLeakingWorkarounds)
{
	const GpuProfileSelection adreno_zero =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 0");
	EXPECT_FALSE(adreno_zero.gpu.recognized);
	EXPECT_EQ(adreno_zero.gpu.architecture, MobileGpuArchitecture::Unknown);

	const GpuProfileSelection powervr_adjacent =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE83200");
	EXPECT_FALSE(powervr_adjacent.gpu.recognized);
	EXPECT_EQ(powervr_adjacent.gpu.architecture, MobileGpuArchitecture::PowerVR);

	const GpuProfileSelection powervr_suffix =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR GE8320foo");
	EXPECT_FALSE(powervr_suffix.gpu.recognized);

	const GpuProfileSelection powervr_sgx_mp =
		GpuProfileDetector::Resolve("auto", "Imagination Technologies", "PowerVR SGX 544MP2");
	EXPECT_TRUE(powervr_sgx_mp.gpu.recognized);
	EXPECT_EQ(powervr_sgx_mp.gpu.model_number, 544);

	const GpuProfileSelection mali_unknown =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G999");
	EXPECT_FALSE(mali_unknown.gpu.recognized);
	EXPECT_EQ(mali_unknown.gpu.architecture, MobileGpuArchitecture::MaliFifthGen);

	const GpuProfileSelection mali_suffix =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57foo");
	EXPECT_FALSE(mali_suffix.gpu.recognized);
	EXPECT_NE(mali_suffix.gpu.model_number, 57);

	const GpuProfileSelection mali_ae =
		GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G78AE");
	EXPECT_TRUE(mali_ae.gpu.recognized);
	EXPECT_EQ(mali_ae.gpu.model_number, 78);

	const GpuProfileSelection adreno_suffix =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 840foo");
	EXPECT_FALSE(adreno_suffix.gpu.recognized);
	EXPECT_NE(adreno_suffix.gpu.model_number, 840);

	const GpuProfileSelection adreno_x_adjacent =
		GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno X2-850");
	EXPECT_FALSE(adreno_x_adjacent.gpu.recognized);
	EXPECT_NE(adreno_x_adjacent.gpu.architecture, MobileGpuArchitecture::AdrenoX);

	const GpuProfileSelection adreno_x = GpuProfileDetector::Resolve(
		"auto", "Qualcomm", "Adreno X2-85", MakeVulkanContext(RuntimeGpuProfile::Adreno, false));
	EXPECT_EQ(adreno_x.driver.workarounds,
		ExpectedVulkanWorkarounds(
			RuntimeGpuProfile::Adreno, adreno_x.gpu.architecture, adreno_x.gpu.model_number));

	const GpuProfileSelection unknown_adreno = GpuProfileDetector::Resolve(
		"auto", "Qualcomm", "Adreno (TM) 9999", MakeVulkanContext(RuntimeGpuProfile::Adreno, false));
	EXPECT_EQ(unknown_adreno.driver.workarounds,
		ExpectedVulkanWorkarounds(RuntimeGpuProfile::Adreno,
			unknown_adreno.gpu.architecture, unknown_adreno.gpu.model_number));
}

TEST(GpuProfileMatrix, KeepsFastTextureAllocationWithBoundedPools)
{
	for (const std::array<const char*, 3>& gpu : {
			 std::array<const char*, 3>{"Qualcomm", "Adreno (TM) 605", "Adreno"},
			 std::array<const char*, 3>{"ARM Mali", "Mali-G52 MC2", "Mali"},
			 std::array<const char*, 3>{"Imagination Technologies", "PowerVR GE8320", "PowerVR"}})
	{
		SCOPED_TRACE(gpu[2]);
		const GpuProfileSelection profile = GpuProfileDetector::Resolve("auto", gpu[0], gpu[1]);
		EXPECT_TRUE(profile.gs_tuning.constrained);
		EXPECT_LT(profile.gs_tuning.pooled_textures, 128u);
		EXPECT_TRUE(profile.gs_tuning.prefer_new_textures);
	}

	const GpuProfileSelection unknown = GpuProfileDetector::Resolve("auto", "", "");
	EXPECT_TRUE(unknown.gs_tuning.constrained);
	EXPECT_TRUE(unknown.gs_tuning.prefer_new_textures);
}

TEST(GpuProfileMatrix, RendererIdentityWinsOverConflictingVendorHints)
{
	const GpuProfileSelection mali = GpuProfileDetector::Resolve(
		"auto", "Qualcomm Adreno", "Mali-G57", MakeOpenGLContext(RuntimeGpuProfile::Mali, false));
	EXPECT_EQ(mali.runtime_profile, RuntimeGpuProfile::Mali);
	EXPECT_EQ(mali.gpu.model_number, 57);
	EXPECT_EQ(mali.driver.workarounds,
		WorkaroundMask({
			DriverWorkaround::ScalarizeVectorBitwiseAnd,
			DriverWorkaround::ForceFifoPresent,
		}));

	const GpuProfileSelection adreno = GpuProfileDetector::Resolve(
		"auto", "ARM Mali", "Adreno (TM) 840", MakeOpenGLContext(RuntimeGpuProfile::Adreno, false));
	EXPECT_EQ(adreno.runtime_profile, RuntimeGpuProfile::Adreno);
	EXPECT_EQ(adreno.gpu.model_number, 840);
	EXPECT_EQ(adreno.driver.workarounds,
		WorkaroundMask({DriverWorkaround::RewriteBooleanNegation}));

	const GpuProfileSelection powervr = GpuProfileDetector::Resolve(
		"auto", "ARM Mali", "PowerVR GE8320",
		MakeOpenGLContext(RuntimeGpuProfile::PowerVR, false));
	EXPECT_EQ(powervr.runtime_profile, RuntimeGpuProfile::PowerVR);
	EXPECT_EQ(powervr.gpu.model_number, 8320);
	EXPECT_EQ(powervr.driver.workarounds, 0u);
}

TEST(GpuProfileMatrix, ExercisesEveryCataloguedBugAndActiveWorkaround)
{
	u64 bugs = 0;
	u64 workarounds = 0;
	const auto accumulate = [&](const GpuProfileSelection& profile) {
		bugs |= profile.driver.bugs;
		workarounds |= profile.driver.workarounds;
	};

	MobileDriverContext gl_mali = MakeOpenGLContext(RuntimeGpuProfile::Mali, false);
	accumulate(GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC2", gl_mali));

	MobileDriverContext gl_adreno = MakeOpenGLContext(RuntimeGpuProfile::Adreno, false);
	accumulate(GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 840", gl_adreno));

	MobileDriverContext gl_powervr = MakeOpenGLContext(RuntimeGpuProfile::PowerVR, false);
	gl_powervr.api_version_string = "OpenGL ES 3.2 build 1.7@4000000";
	accumulate(GpuProfileDetector::Resolve(
		"auto", "Imagination Technologies", "PowerVR SGX544", gl_powervr));

	MobileDriverContext generic_gl;
	generic_gl.api = MobileGpuApi::OpenGL;
	generic_gl.android_sdk = 35;
	accumulate(GpuProfileDetector::Resolve("auto", "", "", generic_gl));

	MobileDriverContext generic_vk = generic_gl;
	generic_vk.api = MobileGpuApi::Vulkan;
	accumulate(GpuProfileDetector::Resolve("auto", "", "", generic_vk));

	MobileDriverContext mali_r38 = MakeVulkanContext(RuntimeGpuProfile::Mali, false);
	mali_r38.driver_version = (38u << 22) | (1u << 12);
	accumulate(GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC4", mali_r38));

	MobileDriverContext mali_hash = MakeVulkanContext(RuntimeGpuProfile::Mali, false);
	mali_hash.driver_version = 0xaa9c4b29u;
	accumulate(GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-T880 MP12", mali_hash));

	MobileDriverContext mali_jm = MakeVulkanContext(RuntimeGpuProfile::Mali, false);
	mali_jm.driver_version = (48u << 22);
	mali_jm.max_draw_indirect_count = 1;
	accumulate(GpuProfileDetector::Resolve("auto", "ARM Mali", "Mali-G57 MC4", mali_jm));

	MobileDriverContext adreno_old = MakeVulkanContext(RuntimeGpuProfile::Adreno, false);
	adreno_old.driver_version = (512u << 22) | (700u << 12);
	accumulate(GpuProfileDetector::Resolve("auto", "Qualcomm", "Adreno (TM) 530", adreno_old));

	MobileDriverContext powervr_old = MakeVulkanContext(RuntimeGpuProfile::PowerVR, false);
	powervr_old.driver_version = (1u << 22) | (8u << 12) | 42u;
	accumulate(GpuProfileDetector::Resolve(
		"auto", "Imagination Technologies", "PowerVR GE8320", powervr_old));

	const u64 all_bugs = (u64{1} << static_cast<u8>(DriverBug::Count)) - 1;
	const u64 all_workarounds = (u64{1} << static_cast<u8>(DriverWorkaround::Count)) - 1;
	EXPECT_EQ(bugs, all_bugs);
	EXPECT_EQ(workarounds, all_workarounds);
}
