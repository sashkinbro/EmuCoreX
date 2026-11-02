// SPDX-License-Identifier: GPL-3.0+
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)

#include "emucorex/upstream_vm_bridge.h"

#include "pcsx2/Config.h"
#include "pcsx2/GS/GS.h"
#include "pcsx2/Host.h"
#include "common/SettingsInterface.h"
#include "pcsx2/GS/Renderers/Common/GSDevice.h"
#include "pcsx2/GS/Renderers/Vulkan/GSDeviceVK.h"
#include "pcsx2/GS/Renderers/Vulkan/VKShaderCache.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/GS/Renderers/Vulkan/VKLoader.h"

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace
{
using namespace emucorex::android;

unsigned s_checks = 0;
unsigned s_failures = 0;
constexpr const char* SETTINGS_SENTINEL_SECTION = "EmuCoreXPreparationOracle";
constexpr const char* SETTINGS_SENTINEL_KEY = "LayerSentinel";

struct HostSettingsSnapshot
{
	SettingsInterface* base_layer = nullptr;
	SettingsInterface* secrets_layer = nullptr;
	std::string base_sentinel;
	std::optional<std::string> secrets_sentinel;
};

void Check(bool pass, const char* message)
{
	++s_checks;
	if (!pass)
		++s_failures;
	std::printf("%s %s\n", pass ? "PASS" : "FAIL", message);
}

VmLaunchConfig MakeConfig(std::string data_root = ".")
{
	VmLaunchConfig config;
	config.paths.data_root = std::move(data_root);
	config.settings.emplace("EmuCore/GS\nRenderer", std::to_string(static_cast<int>(GSRendererType::VK)));
	return config;
}

bool HasNoGraphicsState()
{
	return !g_gs_device && !g_vulkan_shader_cache && !Vulkan::IsVulkanLibraryLoaded();
}

std::array<std::string, 20> SnapshotFolders()
{
	return {{EmuFolders::AppRoot, EmuFolders::DataRoot, EmuFolders::Settings, EmuFolders::Bios,
		EmuFolders::Snapshots, EmuFolders::Savestates, EmuFolders::MemoryCards, EmuFolders::Logs,
		EmuFolders::Cheats, EmuFolders::Patches, EmuFolders::Resources, EmuFolders::UserResources,
		EmuFolders::Cache, EmuFolders::Covers, EmuFolders::GameSettings, EmuFolders::Textures,
		EmuFolders::InputProfiles, EmuFolders::Videos, EmuFolders::DebuggerLayouts,
		EmuFolders::DebuggerSettings}};
}

bool RuntimeGlobalsRestored(const Pcsx2Config::GSOptions& old_gs_config,
	const std::array<std::string, 20>& old_folders, const HostSettingsSnapshot& old_settings)
{
	bool settings_restored;
	{
		std::unique_lock lock = Host::GetSettingsLock();
		settings_restored = Host::Internal::GetBaseSettingsLayer() == old_settings.base_layer &&
			old_settings.base_layer && old_settings.base_layer->GetStringValue(SETTINGS_SENTINEL_SECTION,
				SETTINGS_SENTINEL_KEY) == old_settings.base_sentinel;
	}
	{
		std::unique_lock lock = Host::GetSecretsSettingsLock();
		SettingsInterface* const secrets = Host::Internal::GetSecretsSettingsLayer();
		settings_restored = settings_restored && secrets == old_settings.secrets_layer;
		if (old_settings.secrets_sentinel.has_value())
			settings_restored = settings_restored && secrets && secrets->GetStringValue(SETTINGS_SENTINEL_SECTION,
				SETTINGS_SENTINEL_KEY) == old_settings.secrets_sentinel.value();
	}
	return settings_restored && GSConfig == old_gs_config && SnapshotFolders() == old_folders && HasNoGraphicsState() &&
		VMManager::GetState() == VMState::Shutdown && !VMManager::HasValidVM();
}

void CheckFinishedResult(TFXPreparationBridgeResult result, TFXPreparationJob& job, TFXPreparationState expected_state,
	TFXPreparationError expected_error, const char* label)
{
	Check(result.state == expected_state && result.error == expected_error, label);
	Check(job.Finish(result.state, result.error), "bridge terminal result can be published after lifecycle release");
}
} // namespace

extern "C" __attribute__((visibility("default"))) int EmuCoreXRunTFXPreparationBridgeTests(const char*)
{
	s_checks = s_failures = 0;
	InitializeSettingsLayer();
	const Pcsx2Config::GSOptions old_gs_config = GSConfig;
	const std::array<std::string, 20> old_folders = SnapshotFolders();
	HostSettingsSnapshot old_settings;
	std::optional<std::string> original_base_sentinel;
	std::optional<std::string> original_secrets_sentinel;
	{
		std::unique_lock lock = Host::GetSettingsLock();
		old_settings.base_layer = Host::Internal::GetBaseSettingsLayer();
		if (old_settings.base_layer)
		{
			if (old_settings.base_layer->ContainsValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY))
				original_base_sentinel = old_settings.base_layer->GetStringValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY);
			old_settings.base_layer->SetStringValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY, "base-preserved");
			old_settings.base_sentinel = "base-preserved";
		}
	}
	{
		std::unique_lock lock = Host::GetSecretsSettingsLock();
		old_settings.secrets_layer = Host::Internal::GetSecretsSettingsLayer();
		if (old_settings.secrets_layer)
		{
			if (old_settings.secrets_layer->ContainsValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY))
				original_secrets_sentinel = old_settings.secrets_layer->GetStringValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY);
			old_settings.secrets_layer->SetStringValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY, "secrets-preserved");
			old_settings.secrets_sentinel = "secrets-preserved";
		}
	}

	Check(HasNoGraphicsState() && VMManager::GetState() == VMState::Shutdown && !VMManager::HasValidVM(),
		"TFX preparation bridge starts without VM or graphics state");
	Check(old_settings.base_layer != nullptr,
		"native bridge tests bootstrap the app-lifetime base settings layer before preparation");

	{
		VmLaunchConfig config = MakeConfig();
		TFXPreparationJob job(101, 1);
		const TFXPreparationBridgeResult result = RunTFXPreparation(config, job);
		CheckFinishedResult(result, job, TFXPreparationState::Completed, TFXPreparationError::None,
			"canonical TFX source completes a Vulkan baseline bridge run");
		const TFXPreparationStatus status = job.Snapshot();
		Check(status.job_id == 101 && status.attempt == 1 && status.state == TFXPreparationState::Completed &&
			status.total == 168 && status.completed + status.already_available == 168 && !status.failed &&
			!status.cancelled && !status.in_flight && status.finished && status.checkpoint_saved,
			"bridge returns real completed counts and durable checkpoint status");
		Check(RuntimeGlobalsRestored(old_gs_config, old_folders, old_settings),
			"successful bridge restores VM, GS, cache, folders, settings layers, and Vulkan loader state");
	}

	{
		VmLaunchConfig config = MakeConfig();
		TFXPreparationJob job(102, 1);
		Check(job.RequestCancel(), "pre-creation cancellation request is accepted");
		const TFXPreparationBridgeResult result = RunTFXPreparation(config, job);
		CheckFinishedResult(result, job, TFXPreparationState::Cancelled, TFXPreparationError::None,
			"cancellation before device creation returns a terminal cancelled result");
		Check(job.Snapshot().cancel_requested && job.Snapshot().total == 0 &&
			RuntimeGlobalsRestored(old_gs_config, old_folders, old_settings),
			"pre-creation cancellation leaves no temporary global state");
	}

	{
		VmLaunchConfig config = MakeConfig("artifacts/tfx-preparation-missing-source");
		TFXPreparationJob job(103, 1);
		const TFXPreparationBridgeResult result = RunTFXPreparation(config, job);
		CheckFinishedResult(result, job, TFXPreparationState::Failed, TFXPreparationError::MissingSource,
			"missing canonical shader source has a typed failure result");
		Check(RuntimeGlobalsRestored(old_gs_config, old_folders, old_settings), "missing source restores global runtime and Vulkan state");
	}

	{
		VmLaunchConfig config = MakeConfig();
		config.settings["EmuCore/GS\nDisableShaderCache"] = "true";
		TFXPreparationJob job(104, 1);
		const TFXPreparationBridgeResult result = RunTFXPreparation(config, job);
		CheckFinishedResult(result, job, TFXPreparationState::Unsupported, TFXPreparationError::CacheDisabled,
			"disabled pipeline cache is rejected because no durable checkpoint is possible");
		Check(RuntimeGlobalsRestored(old_gs_config, old_folders, old_settings),
			"cache-disabled rejection restores global runtime and Vulkan state");
	}

	{
		VmLaunchConfig config = MakeConfig();
		config.settings["EmuCore/GS\nRenderer"] = std::to_string(static_cast<int>(GSRendererType::SW));
		TFXPreparationJob job(105, 1);
		const TFXPreparationBridgeResult result = RunTFXPreparation(config, job);
		CheckFinishedResult(result, job, TFXPreparationState::Unsupported, TFXPreparationError::UnsupportedRenderer,
			"non-Vulkan renderer is rejected before graphics initialization");
		Check(RuntimeGlobalsRestored(old_gs_config, old_folders, old_settings),
			"unsupported renderer rejection restores global runtime and Vulkan state");
	}

	{
		std::unique_lock lock = Host::GetSettingsLock();
		if (old_settings.base_layer)
		{
			if (original_base_sentinel.has_value())
				old_settings.base_layer->SetStringValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY, original_base_sentinel->c_str());
			else
				old_settings.base_layer->DeleteValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY);
		}
	}
	{
		std::unique_lock lock = Host::GetSecretsSettingsLock();
		if (old_settings.secrets_layer)
		{
			if (original_secrets_sentinel.has_value())
				old_settings.secrets_layer->SetStringValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY, original_secrets_sentinel->c_str());
			else
				old_settings.secrets_layer->DeleteValue(SETTINGS_SENTINEL_SECTION, SETTINGS_SENTINEL_KEY);
		}
	}

	std::printf("RESULT checks=%u failures=%u\n", s_checks, s_failures);
	return s_failures ? 1 : 0;
}

#endif
