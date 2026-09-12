#include "emucorex/upstream_vm_bridge.h"
#include "emucorex/retro_achievements_android.h"
#include "emucorex/android_crash_diagnostics.h"
#include "emucorex/android_runtime.h"
#include "emucorex/debug_logcat.h"

#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Common.h"
#include "pcsx2/Config.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/OpcodeFamilies.h"
#include "pcsx2/MTVU.h"
#include "pcsx2/VUmicro.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900.h"
#include "pcsx2/VMManager.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/StringUtil.h"
#include "common/Threading.h"

#include <SDL3/SDL_hints.h>
#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string_view>
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

std::mutex s_last_boot_error_mutex;
std::string s_last_boot_error;

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

// ---------------------------------------------------------------------------
// EmuCoreX opcode-family blocklists: per-core u64 family masks plus individual
// opcode id sets, mirrored from the Android settings UI (see OpcodeFamilies.h
// for the shared numeric conventions). When EE/IOP blocklists change we flush
// the respective recompiler caches so blocks recompile with the new policy.
// ---------------------------------------------------------------------------
void ApplyOpcodeFamilyBlocklists(const RuntimeSettings& settings)
{
	struct CoreKeys
	{
		const char* mask_key;
		const char* ids_key;
		OpcodeFamilies::Core core;
		bool has_ids;
	};
	static constexpr CoreKeys core_keys[] = {
		{"DisabledFamilyMaskEE", "DisabledOpcodeIdsEE", OpcodeFamilies::CORE_EE, true},
		{"DisabledFamilyMaskIOP", "DisabledOpcodeIdsIOP", OpcodeFamilies::CORE_IOP, true},
		{"DisabledFamilyMaskVU0", nullptr, OpcodeFamilies::CORE_VU0, false},
		{"DisabledFamilyMaskVU1", nullptr, OpcodeFamilies::CORE_VU1, false},
	};

	static u64 s_last_mask[OpcodeFamilies::CORE_COUNT] = {0, 0, 0, 0};
	static u64 s_last_ids_hash[OpcodeFamilies::CORE_COUNT] = {0, 0, 0, 0};

	auto find_setting = [&settings](const char* key) -> const std::string* {
		const auto it = settings.find(std::string("EmuCoreX/JIT\n") + key);
		return (it != settings.end()) ? &it->second : nullptr;
	};

	for (const CoreKeys& ck : core_keys)
	{
		const std::string* mask_str = find_setting(ck.mask_key);
		u64 mask = 0;
		if (mask_str && !mask_str->empty())
		{
			try
			{
				mask = std::stoull(*mask_str, nullptr, 10);
			}
			catch (...)
			{
				mask = 0;
			}
		}

		std::unordered_set<u32> ids;
		u64 ids_hash = 0;
		if (ck.has_ids)
		{
			if (const std::string* ids_str = find_setting(ck.ids_key); ids_str && !ids_str->empty())
			{
				std::string_view view(*ids_str);
				size_t start = 0;
				while (start <= view.size())
				{
					const size_t comma = view.find(',', start);
					const std::string_view token = view.substr(start,
						(comma == std::string_view::npos) ? std::string_view::npos : comma - start);
					if (!token.empty())
					{
						try
						{
							const u32 id = static_cast<u32>(std::stoul(std::string(token)));
							ids.insert(id);
							ids_hash = ids_hash * 1000003ull + id;
						}
						catch (...)
						{
						}
					}
					if (comma == std::string_view::npos)
						break;
					start = comma + 1;
				}
			}
		}

		const bool mask_changed = (mask != s_last_mask[ck.core]);
		const bool ids_changed = ck.has_ids && (ids_hash != s_last_ids_hash[ck.core]);
		if (!mask_changed && !ids_changed)
			continue;

		// Settings are applied on the CPU thread. Drain the worker before
		// publishing policy or resetting code which it may still be executing.
		if (VMManager::HasValidVM())
		{
			vu1Thread.WaitVU();
			if (ck.core == OpcodeFamilies::CORE_VU0)
				vu0Finish();
			else if (ck.core == OpcodeFamilies::CORE_VU1)
				vu1Finish(false);
		}

		s_last_mask[ck.core] = mask;
		s_last_ids_hash[ck.core] = ids_hash;
		OpcodeFamilies::SetFamilyMask(ck.core, mask);
		OpcodeFamilies::SetDisabledOpcodes(ck.core, ids);

		__android_log_print(ANDROID_LOG_INFO, LOG_TAG,
			"Opcode family blocklist core=%u mask=0x%llx ids=%zu", ck.core,
			static_cast<unsigned long long>(mask), ids.size());

		// Flush the matching recompiler so cached blocks pick up the policy.
		// (VU uses a runtime check, no cache flush needed.)
		if (ck.core == OpcodeFamilies::CORE_EE && Cpu)
		{
			Cpu->Reset();
		}
		else if (ck.core == OpcodeFamilies::CORE_IOP && psxCpu)
		{
			psxCpu->Reset();
		}
	}
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

	ApplyOpcodeFamilyBlocklists(config.settings);

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
	// The interpreter accesses EE-owned VPU_STAT/VIF state. It cannot execute
	// on the MTVU worker, whose packets deliberately omit the shared busy bit.
	const bool vu_thread = GetBoolSetting(config.settings, "EmuCore/Speedhacks", "vuThread", true) &&
		OpcodeFamilies::g_familyMask[OpcodeFamilies::CORE_VU1] == 0;
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

	emucorex::SetDebugLogcatEnabled(
		GetBoolSetting(config.settings, "EmuCoreX", "DebugLogcatGS", false));
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Debug GS logcat: %s",
		emucorex::IsDebugLogcatEnabled() ? "enabled" : "disabled");

	emucorex::SetProfilerLogcatEnabled(
		GetBoolSetting(config.settings, "EmuCoreX", "ProfilerLogcat", false));
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Profiler logcat: %s",
		emucorex::IsProfilerLogcatEnabled() ? "enabled" : "disabled");
	si.SetIntValue("EmuCore/GS", "Renderer",
		GetIntSetting(config.settings, "EmuCore/GS", "Renderer", static_cast<s32>(GSRendererType::OGL)));
	si.SetIntValue("EmuCore/GS", "CropLeft",
		std::clamp(GetIntSetting(config.settings, "EmuCore/GS", "CropLeft", 0), 0, 64));
	si.SetIntValue("EmuCore/GS", "CropTop",
		std::clamp(GetIntSetting(config.settings, "EmuCore/GS", "CropTop", 0), 0, 64));
	si.SetIntValue("EmuCore/GS", "CropRight",
		std::clamp(GetIntSetting(config.settings, "EmuCore/GS", "CropRight", 0), 0, 64));
	si.SetIntValue("EmuCore/GS", "CropBottom",
		std::clamp(GetIntSetting(config.settings, "EmuCore/GS", "CropBottom", 0), 0, 64));
	si.SetBoolValue("EmuCore/GS", "VsyncEnable",
		GetBoolSetting(config.settings, "EmuCore/GS", "VsyncEnable", false));
	const auto audio_backend_setting = config.settings.find("SPU2/Output\nBackend");
	const std::string_view requested_audio_backend =
		(audio_backend_setting != config.settings.end()) ? std::string_view(audio_backend_setting->second) : std::string_view("SDL");
	const char* const audio_backend = (requested_audio_backend == "OpenSLES") ? "OpenSLES" : "SDL";
	si.SetStringValue("SPU2/Output", "Backend", audio_backend);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Effective audio backend=%s", audio_backend);
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
		// Arcade manifests must go through VMManager::AutoDetectSource(). Treating a
		// SAF-backed .acgame URI as an ISO bypasses manifest parsing and hands the
		// encoded document ID to CDVD instead.
		// GS dumps also require auto-detection: forcing ISO bypasses the
		// replayer and incorrectly sends their packet stream to CDVD.
		if (StringUtil::EndsWithNoCase(config.path, ".acgame") || VMManager::IsGSDumpFileName(config.path))
			params.source_type.reset();
		else if (!config.path.empty())
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
	SetLastBootError({});

	if (!s_cpu_runtime_initialized && !VMManager::Internal::CPUThreadInitialize())
	{
		ClearPendingHostCpuTasks();
		SetLastBootError("VM CPU thread initialization failed.");
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
		SetLastBootError(error.GetDescription());
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

void SetLastBootError(const std::string& message)
{
	std::lock_guard lock(s_last_boot_error_mutex);
	s_last_boot_error = message;
}

std::string GetLastBootError()
{
	std::lock_guard lock(s_last_boot_error_mutex);
	return s_last_boot_error;
}
}
