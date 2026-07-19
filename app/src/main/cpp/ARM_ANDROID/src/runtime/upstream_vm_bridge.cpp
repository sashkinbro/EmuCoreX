#include "emucorex/upstream_vm_bridge.h"
#include "emucorex/retro_achievements_android.h"
#include "emucorex/android_crash_diagnostics.h"
#include "emucorex/android_runtime.h"

#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Config.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/VMManager.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/Threading.h"

#include <SDL3/SDL_hints.h>
#include <android/log.h>

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace emucorex::android
{
namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";

std::mutex s_vm_bridge_mutex;
MemorySettingsInterface s_base_settings;
MemorySettingsInterface s_secrets_settings;
std::vector<u8> s_imgui_standard_font_data;
std::vector<u8> s_imgui_emoji_font_data;
bool s_cpu_runtime_initialized = false;

void SetStringSetting(SettingsInterface& si, const std::string& compound_key, const std::string& value)
{
	const std::size_t split = compound_key.find('\n');
	if (split == std::string::npos)
		return;

	const std::string section = compound_key.substr(0, split);
	const std::string key = compound_key.substr(split + 1);
	si.SetStringValue(section.c_str(), key.c_str(), value.c_str());
}

int GetIntSetting(const RuntimeSettings& settings, const char* section, const char* key, int fallback)
{
	const auto it = settings.find(std::string(section) + '\n' + key);
	if (it == settings.end())
		return fallback;

	try
	{
		return std::stoi(it->second);
	}
	catch (...)
	{
		return fallback;
	}
}

float GetFloatSetting(const RuntimeSettings& settings, const char* section, const char* key, float fallback)
{
	const auto it = settings.find(std::string(section) + '\n' + key);
	if (it == settings.end())
		return fallback;

	try
	{
		return std::stof(it->second);
	}
	catch (...)
	{
		return fallback;
	}
}

bool GetBoolSetting(const RuntimeSettings& settings, const char* section, const char* key, bool fallback)
{
	const auto it = settings.find(std::string(section) + '\n' + key);
	if (it == settings.end())
		return fallback;

	const std::string& value = it->second;
	if (value == "true" || value == "1")
		return true;
	if (value == "false" || value == "0")
		return false;
	return fallback;
}

bool FileExists(const std::string& path)
{
	return !path.empty() && FileSystem::FileExists(path.c_str());
}

void ApplyAngleOpenGLLibraryHints(const VmLaunchConfig& config)
{
	const bool requested = GetBoolSetting(config.settings, "EmuCore/GS", "AndroidUseAngleOpenGL", false);
	const int renderer = GetIntSetting(config.settings, "EmuCore/GS", "Renderer", static_cast<s32>(GSRendererType::OGL));
	const bool eligible = requested && renderer == static_cast<s32>(GSRendererType::OGL);

	const std::string egl_path = Path::Combine(config.paths.native_library_dir, "libEGL_angle.so");
	const std::string gles_path = Path::Combine(config.paths.native_library_dir, "libGLESv2_angle.so");
	if (eligible && FileExists(egl_path) && FileExists(gles_path))
	{
		setenv("EMUCOREX_ANGLE_EGL_LIBRARY", egl_path.c_str(), 1);
		setenv("EMUCOREX_ANGLE_GLES_LIBRARY", gles_path.c_str(), 1);
		SDL_SetHintWithPriority(SDL_HINT_EGL_LIBRARY, egl_path.c_str(), SDL_HINT_OVERRIDE);
		SDL_SetHintWithPriority(SDL_HINT_OPENGL_LIBRARY, gles_path.c_str(), SDL_HINT_OVERRIDE);
		SDL_SetHintWithPriority(SDL_HINT_OPENGL_ES_DRIVER, "1", SDL_HINT_OVERRIDE);
	}
	else
	{
		unsetenv("EMUCOREX_ANGLE_EGL_LIBRARY");
		unsetenv("EMUCOREX_ANGLE_GLES_LIBRARY");
		SDL_SetHintWithPriority(SDL_HINT_EGL_LIBRARY, nullptr, SDL_HINT_OVERRIDE);
		SDL_SetHintWithPriority(SDL_HINT_OPENGL_LIBRARY, nullptr, SDL_HINT_OVERRIDE);
		SDL_SetHintWithPriority(SDL_HINT_OPENGL_ES_DRIVER, nullptr, SDL_HINT_OVERRIDE);
	}
}

void ApplyOldCoreJitSettings(SettingsInterface& si, const VmLaunchConfig& config)
{
	const bool autotest_mode = GetBoolSetting(config.settings, "EmuCoreX", "AutotestMode", false);
	const std::string data_root = config.paths.data_root.empty() ? EmuFolders::DataRoot : config.paths.data_root;

	EmuFolders::AppRoot = data_root;
	EmuFolders::DataRoot = data_root;
	EmuFolders::Resources = Path::Combine(data_root, "resources");
	EmuFolders::Settings = Path::Combine(data_root, "inis");
	EmuFolders::Cache = Path::Combine(data_root, "cache");

	VMManager::SetDefaultSettings(si, true, true, true, true, true);

	for (const auto& [key, value] : config.settings)
		SetStringSetting(si, key, value);

	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableEE",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "EnableEE", true));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableIOP",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "EnableIOP", true));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVU0",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "EnableVU0", true));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableVU1",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "EnableVU1", true));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableFastmem",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "EnableFastmem", true));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "EnableEECache",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "EnableEECache", false));
	si.SetIntValue("EmuCore/CPU", "FPU.Roundmode",
		GetIntSetting(config.settings, "EmuCore/CPU", "FPU.Roundmode", 3));
	si.SetIntValue("EmuCore/CPU", "VU0.Roundmode",
		GetIntSetting(config.settings, "EmuCore/CPU", "VU0.Roundmode", 3));
	si.SetIntValue("EmuCore/CPU", "VU1.Roundmode",
		GetIntSetting(config.settings, "EmuCore/CPU", "VU1.Roundmode", 3));
	si.SetIntValue("EmuCoreX/CPU", "EEClampMode",
		GetIntSetting(config.settings, "EmuCoreX/CPU", "EEClampMode", 1));
	si.SetIntValue("EmuCoreX/CPU", "VU0ClampMode",
		GetIntSetting(config.settings, "EmuCoreX/CPU", "VU0ClampMode", 1));
	si.SetIntValue("EmuCoreX/CPU", "VU1ClampMode",
		GetIntSetting(config.settings, "EmuCoreX/CPU", "VU1ClampMode", 0));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "fpuOverflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "fpuOverflow", true));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "fpuExtraOverflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "fpuExtraOverflow", false));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "fpuFullMode",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "fpuFullMode", false));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "vu0Overflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu0Overflow", true));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "vu0ExtraOverflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu0ExtraOverflow", false));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "vu0SignOverflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu0SignOverflow", false));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "vu1Overflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu1Overflow", false));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "vu1ExtraOverflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu1ExtraOverflow", false));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "vu1SignOverflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu1SignOverflow", false));
	const bool wait_loop_speedhack = GetBoolSetting(config.settings, "EmuCore/Speedhacks", "WaitLoop", true);
	const bool intc_stat_speedhack = GetBoolSetting(config.settings, "EmuCore/Speedhacks", "IntcStat", true);
	const bool vu_flag_hack = GetBoolSetting(config.settings, "EmuCore/Speedhacks", "vuFlagHack", true);
	const bool instant_vu1 = GetBoolSetting(config.settings, "EmuCore/Speedhacks", "vu1Instant", true);
	const bool vu_thread = GetBoolSetting(config.settings, "EmuCore/Speedhacks", "vuThread", true);
	si.SetBoolValue("EmuCore/Speedhacks", "WaitLoop", wait_loop_speedhack);
	si.SetBoolValue("EmuCore/Speedhacks", "IntcStat", intc_stat_speedhack);
	si.SetBoolValue("EmuCore/Speedhacks", "vuFlagHack", vu_flag_hack);
	si.SetBoolValue("EmuCore/Speedhacks", "vu1Instant",
		instant_vu1);
	si.SetBoolValue("EmuCore/Speedhacks", "vuThread",
		vu_thread);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
		"Effective speedhacks waitLoop=%d intcStat=%d vuFlag=%d instantVu1=%d vuThread=%d",
		wait_loop_speedhack ? 1 : 0, intc_stat_speedhack ? 1 : 0,
		vu_flag_hack ? 1 : 0, instant_vu1 ? 1 : 0, vu_thread ? 1 : 0);
	si.SetIntValue("EmuCore/Speedhacks", "EECycleRate",
		GetIntSetting(config.settings, "EmuCore/Speedhacks", "EECycleRate", 0));
	si.SetIntValue("EmuCore/Speedhacks", "EECycleSkip",
		GetIntSetting(config.settings, "EmuCore/Speedhacks", "EECycleSkip", 0));
	const bool enable_fast_boot = GetBoolSetting(config.settings, "EmuCore", "EnableFastBoot", !config.path.empty() || config.boot_elf);
	si.SetBoolValue("EmuCore", "EnableFastBoot", enable_fast_boot);
	si.SetBoolValue("Achievements", "Enabled", GetBoolSetting(config.settings, "Achievements", "Enabled", false));
	si.SetBoolValue("Achievements", "ChallengeMode", GetBoolSetting(config.settings, "Achievements", "ChallengeMode", false));
	si.SetBoolValue("Logging", "EnableFileLogging", autotest_mode);
	si.SetBoolValue("Logging", "EnableEEConsole", autotest_mode);
	si.SetBoolValue("Logging", "EnableIOPConsole", autotest_mode);
	si.SetIntValue("EmuCore/GS", "Renderer",
		GetIntSetting(config.settings, "EmuCore/GS", "Renderer", static_cast<s32>(GSRendererType::OGL)));
	si.SetBoolValue("EmuCore/GS", "VsyncEnable",
		GetBoolSetting(config.settings, "EmuCore/GS", "VsyncEnable", false));
	si.SetStringValue("SPU2/Output", "Backend", "SDL");
	si.SetStringValue("SPU2/Output", "DriverName", "");
	si.SetStringValue("SPU2/Output", "DeviceName", "");
	si.SetIntValue("SPU2/Output", "BufferMS",
		GetIntSetting(config.settings, "SPU2/Output", "BufferMS", AudioStreamParameters::DEFAULT_BUFFER_MS));
	si.SetBoolValue("InputSources", "SDL", true);
	si.SetBoolValue("InputSources", "PadVibration", GetBoolSetting(config.settings, "InputSources", "PadVibration", true));
	si.SetStringValue("Pad1", "Type", "DualShock2");
	si.SetFloatValue("Pad1", "PressureModifier", GetFloatSetting(config.settings, "Pad1", "PressureModifier", 0.5f));
	si.SetStringValue("Pad1", "LargeMotor", "SDL-0/Motor0");
	si.SetStringValue("Pad1", "SmallMotor", "SDL-0/Motor1");
	si.SetStringValue("Pad2", "Type", "DualShock2");
	si.SetFloatValue("Pad2", "PressureModifier", GetFloatSetting(config.settings, "Pad2", "PressureModifier", 0.5f));
	si.SetStringValue("Pad2", "LargeMotor", "SDL-1/Motor0");
	si.SetStringValue("Pad2", "SmallMotor", "SDL-1/Motor1");

	EmuFolders::LoadConfig(si);
	EmuFolders::EnsureFoldersExist();
}

void ConfigureImGuiFonts()
{
	if (s_imgui_standard_font_data.empty())
	{
		std::optional<std::vector<u8>> font_data = FileSystem::ReadBinaryFile(
			Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf").c_str());
		if (font_data.has_value())
			s_imgui_standard_font_data = std::move(font_data.value());
	}

	if (s_imgui_emoji_font_data.empty())
	{
		std::optional<std::vector<u8>> font_data = FileSystem::ReadBinaryFile(
			Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Twemoji.Mozilla.ttf").c_str());
		if (font_data.has_value())
			s_imgui_emoji_font_data = std::move(font_data.value());
	}

	std::vector<ImGuiManager::FontInfo> fonts;
	if (!s_imgui_standard_font_data.empty())
		fonts.push_back({s_imgui_standard_font_data, {}, nullptr, false});
	if (!s_imgui_emoji_font_data.empty())
		fonts.push_back({s_imgui_emoji_font_data, {}, nullptr, true});

	if (fonts.empty())
		__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "ImGui font setup failed: no PCSX2 font resources loaded");
	else
		ImGuiManager::SetFonts(std::move(fonts));
}

void InstallHostSettings(const VmLaunchConfig& config)
{
	ApplyAngleOpenGLLibraryHints(config);

	const auto driver_it = config.settings.find("EmuCoreX\nCustomDriverPath");
	if (driver_it != config.settings.end() && !driver_it->second.empty())
	{
		setenv("LIBVULKAN_PATH", driver_it->second.c_str(), 1);
		const std::size_t slash = driver_it->second.find_last_of("/\\");
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Custom Vulkan driver path set: %s",
			driver_it->second.c_str() + ((slash == std::string::npos) ? 0 : slash + 1));
	}
	else
	{
		unsetenv("LIBVULKAN_PATH");
		__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Custom Vulkan driver path not set");
	}

	std::unique_lock settings_lock = Host::GetSettingsLock();
	if (!Host::Internal::GetBaseSettingsLayer())
		Host::Internal::SetBaseSettingsLayer(&s_base_settings);
	s_base_settings.Clear();
	ApplyOldCoreJitSettings(s_base_settings, config);
	ConfigureImGuiFonts();
	settings_lock.unlock();

	std::unique_lock secrets_lock = Host::GetSecretsSettingsLock();
	if (!Host::Internal::GetSecretsSettingsLayer())
		Host::Internal::SetSecretsSettingsLayer(&s_secrets_settings);
	s_secrets_settings.Clear();

	const auto it = config.settings.find("Achievements\nToken");
	if (it != config.settings.end() && !it->second.empty())
	{
		s_secrets_settings.SetStringValue("Achievements", "Token", it->second.c_str());
	}
	secrets_lock.unlock();
}

VMBootParameters CreateBootParameters(const VmLaunchConfig& config)
{
	VMBootParameters params;
	params.fast_boot = GetBoolSetting(config.settings, "EmuCore", "EnableFastBoot", !config.path.empty() || config.boot_elf);
	params.fullscreen = false;
	params.start_turbo = false;
	params.start_unlimited = !GetBoolSetting(config.settings, "EmuCore/GS", "FrameLimitEnable", true);
	params.disable_achievements_hardcore_mode = false;

	if (config.boot_irx)
	{
		params.irx_override = config.path;
		params.source_type = CDVD_SourceType::NoDisc;
		params.fast_boot = false;
	}
	else if (config.boot_elf)
	{
		params.elf_override = config.path;
		params.source_type = CDVD_SourceType::NoDisc;
	}
	else
	{
		params.filename = config.path;
		if (!config.path.empty())
			params.source_type = CDVD_SourceType::Iso;
		else
			params.source_type = CDVD_SourceType::NoDisc;
	}

	return params;
}

}

void InitializeSettingsLayer()
{
	std::unique_lock settings_lock = Host::GetSettingsLock();
	if (!Host::Internal::GetBaseSettingsLayer())
		Host::Internal::SetBaseSettingsLayer(&s_base_settings);
}

bool IsUpstreamVmBridgeAvailable()
{
	return true;
}

bool RunUpstreamVm(const VmLaunchConfig& config, VmStartupCallback startup_callback, void* startup_userdata)
{
	std::lock_guard bridge_lock(s_vm_bridge_mutex);
	ClearPendingHostCpuTasks();
	InstallHostSettings(config);
	RecordVmLaunchForCrashDiagnostics(config.path, config.boot_elf, config.probe_steps);

	if (!s_cpu_runtime_initialized && !VMManager::Internal::CPUThreadInitialize())
	{
		ClearPendingHostCpuTasks();
		__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "VM CPU thread initialization failed");
		if (startup_callback)
			startup_callback(startup_userdata, false);
		return false;
	}
	s_cpu_runtime_initialized = true;

	PerformanceMetrics::SetCPUThread(Threading::ThreadHandle::GetForCallingThread());
	PerformanceMetrics::SetGSSWThreadCount(0);

	Error error;
	const VMBootParameters boot_parameters = CreateBootParameters(config);
	const VMBootResult boot_result = VMManager::Initialize(boot_parameters, &error);
	if (boot_result != VMBootResult::StartupSuccess)
	{
		PerformanceMetrics::SetCPUThread(Threading::ThreadHandle());
		PerformanceMetrics::SetGSSWThreadCount(0);
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "VMManager::Initialize failed: %s", error.GetDescription().c_str());
		ClearPendingHostCpuTasks();
		if (startup_callback)
			startup_callback(startup_userdata, false);
		return false;
	}

	if (startup_callback)
		startup_callback(startup_userdata, true);
	VMManager::SetState(VMState::Running);
	QueryAndNotifyAchievementsState();
	for (;;)
	{
		Host::PumpMessagesOnCPUThread();
		const VMState state = VMManager::GetState();
		if (state == VMState::Stopping || state == VMState::Shutdown)
			break;

		if (state == VMState::Paused)
		{
			Host::PumpMessagesOnCPUThread();
			VMManager::IdlePollUpdate();
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
			continue;
		}

		RecordVmExecutePhaseForCrashDiagnostics("execute");
		VMManager::Execute();
		RecordVmExecutePhaseForCrashDiagnostics("execute-returned");
	}
	RecordVmExecutePhaseForCrashDiagnostics("shutdown");
	VMManager::Shutdown(false);
	PerformanceMetrics::SetCPUThread(Threading::ThreadHandle());
	PerformanceMetrics::SetGSSWThreadCount(0);
	ClearPendingHostCpuTasks();
	return true;
}

void ApplyRuntimeSettingsToUpstream(const VmLaunchConfig& config)
{
	InstallHostSettings(config);
	VMManager::ApplySettings();
}
}
