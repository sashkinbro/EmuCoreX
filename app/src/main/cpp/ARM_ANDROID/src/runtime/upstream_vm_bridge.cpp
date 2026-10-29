#include "common/AndroidLogging.h"
#include "emucorex/upstream_vm_bridge.h"
#include "emucorex/retro_achievements_android.h"
#include "emucorex/android_crash_diagnostics.h"
#include "emucorex/android_runtime.h"

#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Common.h"
#include "pcsx2/Config.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/OpcodeFamilies.h"
#include "pcsx2/GS/GSUtil.h"
#include "pcsx2/GS/GSXXH.h"
#include "pcsx2/GS/Renderers/Common/GSDevice.h"
#include "pcsx2/GS/Renderers/Vulkan/GSDeviceVK.h"
#include "pcsx2/GS/Renderers/Vulkan/VKLoader.h"
#include "pcsx2/GS/Renderers/Vulkan/VKShaderCache.h"
#include "pcsx2/MTVU.h"
#include "pcsx2/VUmicro.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/JitProfiler.h"
#include "pcsx2/HangTrace.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900.h"
#include "pcsx2/VMManager.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/SettingsWrapper.h"
#include "common/StringUtil.h"
#include "common/Threading.h"

#include <SDL3/SDL_hints.h>
#include <android/log.h>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

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

		EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, LOG_TAG,
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

void ApplyOldCoreJitSettings(SettingsInterface& si, const VmLaunchConfig& config, bool apply_vm_side_effects = true)
{
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	const bool autotest_mode = GetBoolSetting(config.settings, "EmuCoreX", "AutotestMode", false);
#else
	constexpr bool autotest_mode = false;
#endif
	const std::string data_root = config.paths.data_root.empty() ? EmuFolders::DataRoot : config.paths.data_root;

	EmuFolders::AppRoot = data_root;
	EmuFolders::DataRoot = data_root;
	EmuFolders::Resources = Path::Combine(data_root, "resources");
	EmuFolders::Settings = Path::Combine(data_root, "inis");
	EmuFolders::Cache = Path::Combine(data_root, "cache");

	VMManager::SetDefaultSettings(si, true, true, true, true, true);

	for (const auto& [key, value] : config.settings)
		SetStringSetting(si, key, value);

	if (apply_vm_side_effects)
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
		GetIntSetting(config.settings, "EmuCoreX/CPU", "VU1ClampMode", 1));
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
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu1Overflow", true));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "vu1ExtraOverflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu1ExtraOverflow", false));
	si.SetBoolValue("EmuCore/CPU/Recompiler", "vu1SignOverflow",
		GetBoolSetting(config.settings, "EmuCore/CPU/Recompiler", "vu1SignOverflow", false));
	si.SetBoolValue("EmuCore/Speedhacks", "iopThread",
		GetBoolSetting(config.settings, "EmuCore/Speedhacks", "iopThread", false));
	si.SetBoolValue("EmuCore/Speedhacks", "iopThreadLockstep",
		GetBoolSetting(config.settings, "EmuCore/Speedhacks", "iopThreadLockstep", false));
	si.SetIntValue("EmuCore/Speedhacks", "iopThreadLag",
		GetIntSetting(config.settings, "EmuCore/Speedhacks", "iopThreadLag", 24576));
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
	EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, LOG_TAG,
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
		GetIntSetting(config.settings, "EmuCore/GS", "Renderer", static_cast<s32>(GSRendererType::VK)));
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
	EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, LOG_TAG, "Effective audio backend=%s", audio_backend);
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

bool InstallHostSettings(const VmLaunchConfig& config, bool initialize_frontend = true)
{
	if (initialize_frontend)
		ApplyAngleOpenGLLibraryHints(config);

	if (!config.paths.native_library_dir.empty())
		setenv("ANDROID_NATIVE_LIB_DIR", config.paths.native_library_dir.c_str(), 1);
	else
		unsetenv("ANDROID_NATIVE_LIB_DIR");

	const auto driver_it = config.settings.find("EmuCoreX\nCustomDriverPath");
	if (driver_it != config.settings.end() && !driver_it->second.empty())
	{
		setenv("LIBVULKAN_PATH", driver_it->second.c_str(), 1);
		const std::size_t slash = driver_it->second.find_last_of("/\\");
		EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, LOG_TAG, "Custom Vulkan driver path set: %s",
			driver_it->second.c_str() + ((slash == std::string::npos) ? 0 : slash + 1));
	}
	else
	{
		unsetenv("LIBVULKAN_PATH");
		EMUCOREX_DIAGNOSTIC_LOG_WRITE(ANDROID_LOG_INFO, LOG_TAG, "Custom Vulkan driver path not set");
	}

	std::unique_lock settings_lock = Host::GetSettingsLock();
	if (initialize_frontend)
	{
		if (!Host::Internal::GetBaseSettingsLayer())
			Host::Internal::SetBaseSettingsLayer(&s_base_settings);
	}
	else if (Host::Internal::GetBaseSettingsLayer() != &s_base_settings)
	{
		// Preparation temporarily overlays the initialized app-lifetime base layer.
		// It must not install or replace a set-once Host layer of its own.
		return false;
	}
	s_base_settings.Clear();
	ApplyOldCoreJitSettings(s_base_settings, config, initialize_frontend);
	if (initialize_frontend)
		ConfigureImGuiFonts();
	settings_lock.unlock();

	if (!initialize_frontend)
		return true;

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
	return true;
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

using TFXPreparationFolderSnapshot = std::array<std::string, 20>;

TFXPreparationFolderSnapshot CaptureTFXPreparationFolders()
{
	return {{EmuFolders::AppRoot, EmuFolders::DataRoot, EmuFolders::Settings, EmuFolders::Bios,
		EmuFolders::Snapshots, EmuFolders::Savestates, EmuFolders::MemoryCards, EmuFolders::Logs,
		EmuFolders::Cheats, EmuFolders::Patches, EmuFolders::Resources, EmuFolders::UserResources,
		EmuFolders::Cache, EmuFolders::Covers, EmuFolders::GameSettings, EmuFolders::Textures,
		EmuFolders::InputProfiles, EmuFolders::Videos, EmuFolders::DebuggerLayouts,
		EmuFolders::DebuggerSettings}};
}

void RestoreTFXPreparationFolders(const TFXPreparationFolderSnapshot& folders)
{
	EmuFolders::AppRoot = folders[0];
	EmuFolders::DataRoot = folders[1];
	EmuFolders::Settings = folders[2];
	EmuFolders::Bios = folders[3];
	EmuFolders::Snapshots = folders[4];
	EmuFolders::Savestates = folders[5];
	EmuFolders::MemoryCards = folders[6];
	EmuFolders::Logs = folders[7];
	EmuFolders::Cheats = folders[8];
	EmuFolders::Patches = folders[9];
	EmuFolders::Resources = folders[10];
	EmuFolders::UserResources = folders[11];
	EmuFolders::Cache = folders[12];
	EmuFolders::Covers = folders[13];
	EmuFolders::GameSettings = folders[14];
	EmuFolders::Textures = folders[15];
	EmuFolders::InputProfiles = folders[16];
	EmuFolders::Videos = folders[17];
	EmuFolders::DebuggerLayouts = folders[18];
	EmuFolders::DebuggerSettings = folders[19];
}

std::optional<std::string> CaptureEnvironmentVariable(const char* name)
{
	const char* value = std::getenv(name);
	if (!value)
		return std::nullopt;
	return std::string(value);
}

void RestoreEnvironmentVariable(const char* name, const std::optional<std::string>& value)
{
	if (value.has_value())
		setenv(name, value->c_str(), 1);
	else
		unsetenv(name);
}

class ScopedTFXPreparationGlobals
{
public:
	ScopedTFXPreparationGlobals()
		: m_gs_config(GSConfig), m_folders(CaptureTFXPreparationFolders()),
		  m_custom_driver_path(CaptureEnvironmentVariable("LIBVULKAN_PATH")),
		  m_native_library_dir(CaptureEnvironmentVariable("ANDROID_NATIVE_LIB_DIR"))
	{
		{
			std::unique_lock lock = Host::GetSettingsLock();
			m_base_settings = s_base_settings;
			m_base_settings_layer = Host::Internal::GetBaseSettingsLayer();
		}
		{
			std::unique_lock lock = Host::GetSecretsSettingsLock();
			m_secrets_settings_layer = Host::Internal::GetSecretsSettingsLayer();
		}
	}

	~ScopedTFXPreparationGlobals()
	{
		if (g_gs_device)
		{
			static_cast<GSDeviceVK*>(g_gs_device.get())->DestroyTFXPreparationDevice();
			g_gs_device.reset();
		}

		GSConfig = m_gs_config;
		RestoreTFXPreparationFolders(m_folders);
		RestoreEnvironmentVariable("LIBVULKAN_PATH", m_custom_driver_path);
		RestoreEnvironmentVariable("ANDROID_NATIVE_LIB_DIR", m_native_library_dir);
		{
			std::unique_lock lock = Host::GetSettingsLock();
			s_base_settings = m_base_settings;
			pxAssertRel(Host::Internal::GetBaseSettingsLayer() == m_base_settings_layer,
				"TFX preparation must preserve the app-lifetime base settings layer");
		}
		{
			std::unique_lock lock = Host::GetSecretsSettingsLock();
			pxAssertRel(Host::Internal::GetSecretsSettingsLayer() == m_secrets_settings_layer,
				"TFX preparation must preserve the app-lifetime secrets settings layer");
		}
	}

	ScopedTFXPreparationGlobals(const ScopedTFXPreparationGlobals&) = delete;
	ScopedTFXPreparationGlobals& operator=(const ScopedTFXPreparationGlobals&) = delete;

private:
	Pcsx2Config::GSOptions m_gs_config;
	TFXPreparationFolderSnapshot m_folders;
	std::optional<std::string> m_custom_driver_path;
	std::optional<std::string> m_native_library_dir;
	MemorySettingsInterface m_base_settings;
	SettingsInterface* m_base_settings_layer = nullptr;
	SettingsInterface* m_secrets_settings_layer = nullptr;
};

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
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	// Debug-only profiling is opt-in so ordinary debug sessions retain normal speed.
	// PumpMessagesOnCPUThread() first registers this thread as
	// the CPU thread, which makes the profiler's Host::RunOnCPUThread tasks run
	// inline instead of waiting on a queue that this thread pumps.
	// IR integration tests can opt out: the IOP IR preserves opcode profiling
	// by retaining the legacy compiler whenever the profiler is active.
	char profile_value[PROP_VALUE_MAX] = {};
	const bool auto_profile = __system_property_get("debug.emucorex.jit_profile", profile_value) == 1 && profile_value[0] == '1';
	if (auto_profile && !JitProfiler::IsActive())
	{
		Host::PumpMessagesOnCPUThread();
		JitProfiler::Start();
		EMUCOREX_DIAGNOSTIC_LOG_WRITE(ANDROID_LOG_INFO, LOG_TAG, "JIT profiler auto-started for this VM session");
	}
	else if (!auto_profile)
		EMUCOREX_DIAGNOSTIC_LOG_WRITE(ANDROID_LOG_INFO, LOG_TAG, "JIT profiler auto-start disabled by debug.emucorex.jit_profile");
	if (!HangTrace::IsActive())
	{
		char trace_value[PROP_VALUE_MAX] = {};
		if (__system_property_get("debug.emucorex.hang_trace", trace_value) > 0 && trace_value[0] == '1')
		{
			// HangTrace resets recompilers synchronously through the CPU dispatcher.
			// Register this thread even when profiler auto-start was opted out.
			Host::PumpMessagesOnCPUThread();
			HangTrace::Start();
			EMUCOREX_DIAGNOSTIC_LOG_WRITE(ANDROID_LOG_INFO, LOG_TAG, "Hang trace auto-started for this VM session");
		}
	}
#endif
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
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	// Write the perf report before the VM tears down so disc serial/CRC and the
	// compiled block tables are still valid. Stop() runs inline on this thread.
	if (JitProfiler::IsActive())
	{
		JitProfiler::Stop();
		EMUCOREX_DIAGNOSTIC_LOG_WRITE(ANDROID_LOG_INFO, LOG_TAG, "JIT profiler report written to jit_profile.txt");
	}
	if (HangTrace::IsActive())
	{
		HangTrace::Stop();
		EMUCOREX_DIAGNOSTIC_LOG_WRITE(ANDROID_LOG_INFO, LOG_TAG, "Hang trace report written to hang_trace.txt");
	}
#endif
	VMManager::Shutdown(false);
	PerformanceMetrics::SetCPUThread(Threading::ThreadHandle());
	PerformanceMetrics::SetGSSWThreadCount(0);
	ClearPendingHostCpuTasks();
	return true;
}

TFXPreparationBridgeResult RunTFXPreparation(const VmLaunchConfig& config, TFXPreparationJob& job, bool validation_only)
{
	TFXPreparationBridgeResult result{TFXPreparationState::Idle, TFXPreparationError::None};
	std::unique_lock bridge_lock(s_vm_bridge_mutex, std::defer_lock);
	while (!bridge_lock.try_lock())
	{
		if (job.CancellationRequested())
			return {TFXPreparationState::Cancelled, TFXPreparationError::None};
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	if (job.CancellationRequested())
		return {TFXPreparationState::Cancelled, TFXPreparationError::None};
	if (!job.PublishStage(TFXPreparationState::Starting))
		return {TFXPreparationState::Failed, TFXPreparationError::InternalError};

	if (VMManager::GetState() != VMState::Shutdown || VMManager::HasValidVM() || g_gs_device ||
		g_vulkan_shader_cache || Vulkan::IsVulkanLibraryLoaded())
	{
		return {TFXPreparationState::Failed, TFXPreparationError::LifecycleBusy};
	}

	try
	{
		result = [&]() -> TFXPreparationBridgeResult {
			ScopedTFXPreparationGlobals restore_globals;
			if (!InstallHostSettings(config, false))
				return {TFXPreparationState::Failed, TFXPreparationError::LifecycleBusy};

			if (job.CancellationRequested())
				return {TFXPreparationState::Cancelled, TFXPreparationError::None};

			Pcsx2Config::GSOptions effective_gs_config;
			{
				std::unique_lock settings_lock = Host::GetSettingsLock();
				SettingsInterface* settings = Host::GetSettingsInterface();
				if (!settings)
					return {TFXPreparationState::Failed, TFXPreparationError::InternalError};

				SettingsLoadWrapper wrapper(*settings);
				effective_gs_config.LoadSave(wrapper);
			}
			effective_gs_config.MaskUserHacks();
			effective_gs_config.MaskUpscalingHacks();
			if (effective_gs_config.Renderer == GSRendererType::Auto)
				effective_gs_config.Renderer = GSUtil::GetPreferredRenderer();
			if (effective_gs_config.Renderer != GSRendererType::VK)
				return {TFXPreparationState::Unsupported, TFXPreparationError::UnsupportedRenderer};
			if (effective_gs_config.DisableShaderCache)
				return {TFXPreparationState::Unsupported, TFXPreparationError::CacheDisabled};

			const std::optional<std::string> tfx_source = GSDevice::ReadShaderSource("shaders/vulkan/tfx.glsl");
			if (!tfx_source.has_value() || tfx_source->empty())
				return {TFXPreparationState::Failed, TFXPreparationError::MissingSource};
			if (job.CancellationRequested())
				return {TFXPreparationState::Cancelled, TFXPreparationError::None};

			GSConfig = effective_gs_config;
			g_gs_device = std::make_unique<GSDeviceVK>();
			GSDeviceVK* const device = static_cast<GSDeviceVK*>(g_gs_device.get());
			if (!device->CreateTFXPreparationDevice(*tfx_source))
				return {TFXPreparationState::Failed, TFXPreparationError::DeviceCreationFailed};

			std::string driver_identity = "system";
			if (const char* path = std::getenv("LIBVULKAN_PATH"); path && *path)
			{
				driver_identity = std::string("requested:") + path;
				const auto binary = FileSystem::ReadBinaryFile(path);
				if (binary.has_value())
				{
					const auto hash = XXH3_128bits(binary->data(), binary->size());
					driver_identity += fmt::format(":{:016x}{:016x}", hash.high64, hash.low64);
				}
				else driver_identity += ":missing";
			}
			const std::string receipt_identity = device->GetTFXPreparationIdentity(driver_identity);
			if (validation_only)
			{
				const bool prepared = !receipt_identity.empty() && g_vulkan_shader_cache &&
					g_vulkan_shader_cache->IsTFXPreparationReceiptValid(receipt_identity);
				return {prepared ? TFXPreparationState::Completed : TFXPreparationState::Idle,
					TFXPreparationError::None};
			}
			const std::string receipt_path = g_vulkan_shader_cache ?
				g_vulkan_shader_cache->GetTFXPreparationReceiptPath() : std::string();
			const std::string adapter_name = device->GetName();
			if (job.CancellationRequested())
				return {TFXPreparationState::Cancelled, TFXPreparationError::None};

			if (!device->StartTFXPreparationBatch())
				return {TFXPreparationState::Failed, TFXPreparationError::DeviceCreationFailed};
			if (!job.CancellationRequested() && !job.PublishStage(TFXPreparationState::Running))
				return {TFXPreparationState::Failed, TFXPreparationError::InternalError};

			bool cancellation_sent = false;
			GSDeviceVK::TFXPreparationProgressSnapshot progress;
			for (;;)
			{
				if (job.CancellationRequested() && !cancellation_sent)
				{
					device->RequestTFXPreparationCancel();
					cancellation_sent = true;
				}

				progress = device->GetTFXPreparationProgress();
				TFXPreparationProgress published_progress;
				published_progress.total = progress.total;
				published_progress.completed = progress.completed;
				published_progress.already_available = progress.already_available;
				published_progress.failed = progress.failed;
				published_progress.cancelled = progress.cancelled;
				published_progress.in_flight = progress.in_flight;
				published_progress.finished = progress.finished;
				published_progress.checkpoint_saved = progress.checkpoint_saved;
				if (!job.PublishProgress(published_progress, adapter_name))
				{
					device->RequestTFXPreparationCancel();
					cancellation_sent = true;
					result = {TFXPreparationState::Failed, TFXPreparationError::InternalError};
				}
				if (progress.finished)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}

			if (result.error == TFXPreparationError::InternalError)
				return result;
			if (progress.failed)
				return {TFXPreparationState::Failed, TFXPreparationError::PipelineFailures};
			if (!progress.checkpoint_saved)
				return {TFXPreparationState::Failed, TFXPreparationError::CheckpointFailed};
			if (job.CancellationRequested())
				return {TFXPreparationState::Cancelled, TFXPreparationError::None};
			return {TFXPreparationState::Completed, TFXPreparationError::None, receipt_path, receipt_identity};
		}();
	}
	catch (...)
	{
		result = {TFXPreparationState::Failed, TFXPreparationError::InternalError};
	}

	bridge_lock.unlock();
	return result;
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
