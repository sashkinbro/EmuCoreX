#include "emucorex/android_runtime.h"
#include "emucorex/upstream_vm_bridge.h"
#include "pcsx2/GS/Renderers/Vulkan/VKShaderCache.h"

#include "common/Error.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/Achievements.h"
#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Config.h"
#include "pcsx2/GameList.h"
#include "pcsx2/R5900.h"
#include "pcsx2/SIO/Memcard/MemoryCardFile.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "emucorex/retro_achievements_android.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/DEV9/ACJV.h"
#include "pcsx2/USB/USB.h"
#include "pcsx2/Host.h"
#include "pcsx2/VMManager.h"
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
#include "pcsx2/GS/GS.h"
#include "pcsx2/GS/Renderers/Vulkan/GSDeviceVK.h"
#include "pcsx2/GS/Renderers/Vulkan/VKLoader.h"
#include "pcsx2/GS/Renderers/Vulkan/VKShaderCache.h"
#endif

#include <SDL3/SDL_main.h>

#include <android/log.h>
#include <android/native_window.h>

#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>

namespace emucorex::android
{
namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";
// PCSX2 game metadata probing and VM lifecycle operations share the global CDVD/ISO
// reader. Keep them mutually exclusive so a library scan cannot close or replace the
// reader while another thread is detecting a disc or while the VM is starting/stopping.
std::mutex s_disc_access_mutex;

// PCSX2 core diagnostics (GS/Vulkan/OpenGL errors, warnings and status lines) are
// written to stdout, which Android discards for app processes. Without this bridge a
// black-screen bug on a specific device leaves no evidence anywhere, so route the
// core log to logcat. INFO keeps dev/trace spam out while keeping every error and
// warning from the graphics stack visible.
void AndroidConsoleLogCallback(LOGLEVEL level, ConsoleColors, std::string_view message)
{
	const int priority = (level <= LOGLEVEL_ERROR) ? ANDROID_LOG_ERROR :
		((level <= LOGLEVEL_WARNING) ? ANDROID_LOG_WARN : ANDROID_LOG_INFO);
	__android_log_print(priority, LOG_TAG, "%.*s", static_cast<int>(message.size()), message.data());
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
struct TFXPreparationTestBarrier
{
	std::mutex mutex;
	std::condition_variable cv;
	bool enabled = false;
	bool reached = false;
	bool released = false;
};

TFXPreparationTestBarrier s_tfx_preparation_test_barrier;

void ResetTFXPreparationTestBarrier()
{
	std::lock_guard lock(s_tfx_preparation_test_barrier.mutex);
	s_tfx_preparation_test_barrier.enabled = true;
	s_tfx_preparation_test_barrier.reached = false;
	s_tfx_preparation_test_barrier.released = false;
}

bool WaitForTFXPreparationTestBarrier()
{
	std::unique_lock lock(s_tfx_preparation_test_barrier.mutex);
	return s_tfx_preparation_test_barrier.cv.wait_for(lock, std::chrono::seconds(5), [] {
		return s_tfx_preparation_test_barrier.reached;
	});
}

void ReleaseTFXPreparationTestBarrier()
{
	{
		std::lock_guard lock(s_tfx_preparation_test_barrier.mutex);
		s_tfx_preparation_test_barrier.released = true;
	}
	s_tfx_preparation_test_barrier.cv.notify_all();
}

bool WaitAtTFXPreparationTestBarrier()
{
	std::unique_lock lock(s_tfx_preparation_test_barrier.mutex);
	if (!s_tfx_preparation_test_barrier.enabled)
		return false;
	s_tfx_preparation_test_barrier.reached = true;
	s_tfx_preparation_test_barrier.cv.notify_all();
	s_tfx_preparation_test_barrier.cv.wait(lock, [] {
		return s_tfx_preparation_test_barrier.released;
	});
	return true;
}
#endif

// Android input callbacks run on UI/Binder threads while the emulated pad is
// owned by the CPU thread. Publish only compact values here and consume them at
// the CPU-thread input poll, avoiding both a C++ data race and a mutex/task
// allocation for every touch/axis event.
static_assert(PadDualshock2::LENGTH <= 32);
using PendingPadValues = std::array<std::atomic<u32>, PadDualshock2::LENGTH>;
std::array<PendingPadValues, Pad::NUM_CONTROLLER_PORTS> s_pending_pad_values{};
std::array<PendingPadValues, Pad::NUM_CONTROLLER_PORTS> s_pending_pad_press_values{};
// Lower 32 bits mark changed bindings; upper 32 bits latch a press edge. Publishing both
// in one atomic word prevents the CPU poll from observing/clearing only half an event.
std::array<std::atomic<u64>, Pad::NUM_CONTROLLER_PORTS> s_pending_pad_events{};
std::atomic<u32> s_pending_pressure_modifier{};
std::atomic_bool s_pending_pressure_modifier_dirty{false};

// Arcade/light-gun virtual keys live beside the DualShock2 pad. They use their own
// held-bit mask so the CPU-thread poll can turn them into GunCon2 USB binds and JVS
// actions (coin/service) without racing the UI thread.
constexpr int ANDROID_KEY_GUN_TRIGGER = 125;
constexpr int ANDROID_KEY_GUN_PEDAL = 126;
constexpr int ANDROID_KEY_GUN_RELOAD = 127;
constexpr int ANDROID_KEY_GUN_RECALIBRATE = 128;
constexpr int ANDROID_KEY_COIN = 129;
constexpr int ANDROID_KEY_SERVICE = 130;
constexpr int ANDROID_KEY_ARCADE_LAST = ANDROID_KEY_SERVICE;

constexpr u32 ArcadeKeyBit(int index)
{
	return 1u << static_cast<u32>(index - ANDROID_KEY_GUN_TRIGGER);
}

bool IsArcadePadKey(int index)
{
	return index >= ANDROID_KEY_GUN_TRIGGER && index <= ANDROID_KEY_ARCADE_LAST;
}

std::array<std::atomic<u32>, Pad::NUM_CONTROLLER_PORTS> s_pending_arcade_buttons{};
// Coin and service are momentary cabinet actions: latch each press as a counter so a
// tap that starts and ends between two CPU polls is still delivered exactly once.
std::array<std::atomic<u32>, Pad::NUM_CONTROLLER_PORTS> s_pending_coin_edges{};
std::array<std::atomic<u32>, Pad::NUM_CONTROLLER_PORTS> s_pending_service_edges{};
// Owned by the CPU thread; only touched from the pending-input poll.
std::array<u32, Pad::NUM_CONTROLLER_PORTS> s_applied_arcade_buttons{};

// GunCon2 bind indices, kept in sync with pcsx2/USB/usb-lightgun/guncon2.cpp.
constexpr u32 GUNCON2_BIND_PEDAL = 3;            // BID_A
constexpr u32 GUNCON2_BIND_TRIGGER = 13;         // BID_TRIGGER
constexpr u32 GUNCON2_BIND_SHOOT_OFFSCREEN = 16; // BID_SHOOT_OFFSCREEN
constexpr u32 GUNCON2_BIND_RECALIBRATE = 17;     // BID_RECALIBRATE

u32 FloatToBits(float value)
{
	return std::bit_cast<u32>(value);
}

float BitsToFloat(u32 value)
{
	return std::bit_cast<float>(value);
}

void PublishPadValue(u32 controller, u32 bind, float value, bool latch_press = false)
{
	u64 event = u64{1} << bind;
	if (latch_press)
	{
		s_pending_pad_press_values[controller][bind].store(FloatToBits(value), std::memory_order_relaxed);
		event |= u64{1} << (bind + 32);
	}
	s_pending_pad_values[controller][bind].store(FloatToBits(value), std::memory_order_relaxed);
	s_pending_pad_events[controller].fetch_or(event, std::memory_order_release);
}

std::string SettingKey(const std::string& section, const std::string& key)
{
	return section + '\n' + key;
}

void ApplyAndroidEmuFolders(const std::string& data_root)
{
	if (data_root.empty())
		return;

	EmuFolders::AppRoot = data_root;
	EmuFolders::DataRoot = data_root;
	EmuFolders::Resources = Path::Combine(data_root, "resources");
	EmuFolders::Settings = Path::Combine(data_root, "inis");
	EmuFolders::Cache = Path::Combine(data_root, "cache");
}

void ApplyCustomDriverPathEnvironment(const std::string& value)
{
	if (!value.empty())
	{
		setenv("LIBVULKAN_PATH", value.c_str(), 1);
		const std::size_t slash = value.find_last_of("/\\");
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Runtime custom Vulkan driver path set: %s",
			value.c_str() + ((slash == std::string::npos) ? 0 : slash + 1));
	}
	else
	{
		unsetenv("LIBVULKAN_PATH");
		__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Runtime custom Vulkan driver path cleared");
	}
}

std::string BasenameWithoutExtension(const std::string& path)
{
	const std::size_t slash = path.find_last_of("/\\");
	const std::size_t begin = (slash == std::string::npos) ? 0 : slash + 1;
	const std::size_t dot = path.find_last_of('.');
	const std::size_t end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
	return path.substr(begin, end - begin);
}

std::string FormatGameMetadata(const std::string& title, const std::string& serial, u32 crc)
{
	std::ostringstream out;
	out << title << '|';
	if (!serial.empty())
		out << serial;
	out << '|';
	if (!serial.empty() && crc != 0)
		out << serial << '_' << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << crc;
	return out.str();
}

std::string JsonEscape(std::string_view value)
{
	std::string out;
	out.reserve(value.size() + 8);
	for (const char ch : value)
	{
		switch (ch)
		{
			case '\\':
				out += "\\\\";
				break;
			case '"':
				out += "\\\"";
				break;
			case '\b':
				out += "\\b";
				break;
			case '\f':
				out += "\\f";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				out += (static_cast<unsigned char>(ch) < 0x20) ? ' ' : ch;
				break;
		}
	}
	return out;
}

MemoryCardType ToMemoryCardType(int value)
{
	switch (value)
	{
		case static_cast<int>(MemoryCardType::Folder):
			return MemoryCardType::Folder;
		case static_cast<int>(MemoryCardType::File):
		default:
			return MemoryCardType::File;
	}
}

MemoryCardFileType ToMemoryCardFileType(int value)
{
	switch (value)
	{
		case static_cast<int>(MemoryCardFileType::PS2_16MB):
			return MemoryCardFileType::PS2_16MB;
		case static_cast<int>(MemoryCardFileType::PS2_32MB):
			return MemoryCardFileType::PS2_32MB;
		case static_cast<int>(MemoryCardFileType::PS2_64MB):
			return MemoryCardFileType::PS2_64MB;
		case static_cast<int>(MemoryCardFileType::PS1):
			return MemoryCardFileType::PS1;
		case static_cast<int>(MemoryCardFileType::PS2_8MB):
		default:
			return MemoryCardFileType::PS2_8MB;
	}
}

void EnsureMemoryCardFolder(const std::string& path)
{
	if (path.empty())
		return;

	EmuFolders::MemoryCards = path;
	FileSystem::CreateDirectoryPath(path.c_str(), false);
}

std::optional<u32> MapAndroidPadKeyToDualShock2Input(int index)
{
	switch (index)
	{
		case 19:
			return PadDualshock2::PAD_UP;
		case 22:
			return PadDualshock2::PAD_RIGHT;
		case 20:
			return PadDualshock2::PAD_DOWN;
		case 21:
			return PadDualshock2::PAD_LEFT;
		case 100:
			return PadDualshock2::PAD_TRIANGLE;
		case 97:
			return PadDualshock2::PAD_CIRCLE;
		case 96:
			return PadDualshock2::PAD_CROSS;
		case 99:
			return PadDualshock2::PAD_SQUARE;
		case 109:
			return PadDualshock2::PAD_SELECT;
		case 108:
			return PadDualshock2::PAD_START;
		case 102:
			return PadDualshock2::PAD_L1;
		case 104:
			return PadDualshock2::PAD_L2;
		case 103:
			return PadDualshock2::PAD_R1;
		case 105:
			return PadDualshock2::PAD_R2;
		case 106:
			return PadDualshock2::PAD_L3;
		case 107:
			return PadDualshock2::PAD_R3;
		case 110:
			return PadDualshock2::PAD_L_UP;
		case 111:
			return PadDualshock2::PAD_L_RIGHT;
		case 112:
			return PadDualshock2::PAD_L_DOWN;
		case 113:
			return PadDualshock2::PAD_L_LEFT;
		case 120:
			return PadDualshock2::PAD_R_UP;
		case 121:
			return PadDualshock2::PAD_R_RIGHT;
		case 122:
			return PadDualshock2::PAD_R_DOWN;
		case 123:
			return PadDualshock2::PAD_R_LEFT;
		case 124:
			return PadDualshock2::PAD_PRESSURE;
		default:
			return std::nullopt;
	}
}

float PadValueFromAndroidRange(int range, bool pressed)
{
	if (range > 0)
		return std::clamp(static_cast<float>(range) / 255.0f, 0.0f, 1.0f);
	return pressed ? 1.0f : 0.0f;
}

bool IsTrueSettingValue(const std::string& value)
{
	return value == "true" || value == "1";
}
}

AndroidRuntime& AndroidRuntime::Instance()
{
	static AndroidRuntime runtime;
	return runtime;
}

AndroidRuntime::~AndroidRuntime()
{
	Shutdown();
	ClearSurface();
}

bool AndroidRuntime::IsTFXPreparationActiveLocked() const
{
	return tfx_readiness_probe_active_ ||
		(tfx_preparation_job_ && TFXPreparationJob::IsActiveState(tfx_preparation_job_->Snapshot().state));
}

void AndroidRuntime::ApplyDeferredRuntimeGlobalsLocked()
{
	if (deferred_data_root_apply_)
	{
		ApplyAndroidEmuFolders(paths_.data_root);
		deferred_data_root_apply_ = false;
	}
	if (deferred_settings_layer_init_)
	{
		emucorex::android::InitializeSettingsLayer();
		deferred_settings_layer_init_ = false;
	}
	if (deferred_native_library_environment_)
	{
		if (paths_.native_library_dir.empty())
		{
			unsetenv("ANDROID_NATIVE_LIB_DIR");
			__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Native library directory environment cleared");
		}
		else
		{
			setenv("ANDROID_NATIVE_LIB_DIR", paths_.native_library_dir.c_str(), 1);
			__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Native library directory environment set");
		}
		deferred_native_library_environment_ = false;
	}
	if (deferred_custom_driver_environment_)
	{
		const auto it = settings_.find(SettingKey("EmuCoreX", "CustomDriverPath"));
		ApplyCustomDriverPathEnvironment(it != settings_.end() ? it->second : std::string());
		deferred_custom_driver_environment_ = false;
	}
	if (deferred_app_version_environment_)
	{
		const auto it = settings_.find(SettingKey("EmuCoreX", "AppVersion"));
		if (it == settings_.end())
			unsetenv("EMUCOREX_APP_VERSION");
		else
			setenv("EMUCOREX_APP_VERSION", it->second.c_str(), 1);
		deferred_app_version_environment_ = false;
	}
}

void AndroidRuntime::Initialize(std::string data_root, int api_version)
{
	std::lock_guard lock(mutex_);
	SDL_SetMainReady();
	Log::SetHostOutputLevel(LOGLEVEL_INFO, &AndroidConsoleLogCallback);
	paths_.data_root = std::move(data_root);
	if (IsTFXPreparationActiveLocked())
	{
		deferred_data_root_apply_ = true;
		deferred_settings_layer_init_ = true;
	}
	else
	{
		ApplyAndroidEmuFolders(paths_.data_root);
		emucorex::android::InitializeSettingsLayer();
	}
	api_version_ = api_version;
	initialized_ = true;
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Android runtime initialized for PCSX2 v2.8.0 adapter");
}

void AndroidRuntime::ReloadDataRoot(std::string data_root)
{
	std::lock_guard lock(mutex_);
	paths_.data_root = std::move(data_root);
	if (IsTFXPreparationActiveLocked())
		deferred_data_root_apply_ = true;
	else
		ApplyAndroidEmuFolders(paths_.data_root);
}

void AndroidRuntime::SetNativeLibraryDir(std::string path)
{
	std::lock_guard lock(mutex_);
	paths_.native_library_dir = std::move(path);
	if (IsTFXPreparationActiveLocked())
	{
		deferred_native_library_environment_ = true;
	}
	else if (!paths_.native_library_dir.empty())
	{
		setenv("ANDROID_NATIVE_LIB_DIR", paths_.native_library_dir.c_str(), 1);
		__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Native library directory environment set");
	}
	else
	{
		unsetenv("ANDROID_NATIVE_LIB_DIR");
		__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Native library directory environment cleared");
	}
}

void AndroidRuntime::BeginSettingsBatch()
{
	std::lock_guard lock(mutex_);
	settings_batch_ = true;
}

void AndroidRuntime::EndSettingsBatch()
{
	VmLaunchConfig config;
	bool apply_settings = false;
	{
		std::lock_guard lock(mutex_);
		settings_batch_ = false;
		apply_settings = vm_active_;
		if (apply_settings)
			config = CreateLaunchConfigLocked(current_path_, false, 0, false);
	}

	if (apply_settings)
	{
		// Settings batches which recreate GS must finish before the UI is allowed to enqueue the
		// next renderer/surface operation. MTGS::ApplySettings performs the matching GS-thread wait.
		Host::RunOnCPUThread([config = std::move(config)]() {
			ApplyRuntimeSettingsToUpstream(config);
		}, false);
	}
}

void AndroidRuntime::SetSetting(std::string section, std::string key, std::string, std::string value)
{
	std::lock_guard lock(mutex_);
	const bool custom_driver_changed = section == "EmuCoreX" && key == "CustomDriverPath";
	const bool app_version_changed = section == "EmuCoreX" && key == "AppVersion";
	const auto hardcore_it = settings_.find(SettingKey("Achievements", "ChallengeMode"));
	const bool hardcore_requested =
		(section == "Achievements" && key == "ChallengeMode" && IsTrueSettingValue(value)) ||
		(hardcore_it != settings_.end() && IsTrueSettingValue(hardcore_it->second));
	if (section == "EmuCore" && key == "EnableCheats" &&
		IsTrueSettingValue(value) &&
		hardcore_requested)
	{
		value = "false";
	}
	settings_[SettingKey(section, key)] = std::move(value);

	if (hardcore_requested)
	{
		settings_[SettingKey("EmuCore", "EnableCheats")] = "false";
	}

	if (custom_driver_changed)
	{
		if (IsTFXPreparationActiveLocked())
			deferred_custom_driver_environment_ = true;
		else
			ApplyCustomDriverPathEnvironment(settings_[SettingKey("EmuCoreX", "CustomDriverPath")]);
	}
	if (app_version_changed)
	{
		if (IsTFXPreparationActiveLocked())
			deferred_app_version_environment_ = true;
		else
			setenv("EMUCOREX_APP_VERSION", settings_[SettingKey("EmuCoreX", "AppVersion")].c_str(), 1);
	}
}

std::string AndroidRuntime::GetSetting(const std::string& section, const std::string& key) const
{
	std::lock_guard lock(mutex_);
	const auto it = settings_.find(SettingKey(section, key));
	return (it == settings_.end()) ? std::string() : it->second;
}

void AndroidRuntime::SetFrameLimitEnabled(bool enabled)
{
	bool active = false;
	{
		std::lock_guard lock(mutex_);
		settings_[SettingKey("EmuCore/GS", "FrameLimitEnable")] = enabled ? "true" : "false";
		active = vm_active_;
	}

	if (!active)
		return;

	Host::RunOnCPUThread([enabled]() {
		if (VMManager::HasValidVM())
			VMManager::SetLimiterMode(enabled ? LimiterModeType::Nominal : LimiterModeType::Unlimited);
	}, false);
}

void AndroidRuntime::SetTurboModeEnabled(bool enabled)
{
	bool active = false;
	bool frame_limit_enabled = true;
	{
		std::lock_guard lock(mutex_);
		active = vm_active_;
		const auto it = settings_.find(SettingKey("EmuCore/GS", "FrameLimitEnable"));
		if (it != settings_.end())
			frame_limit_enabled = (it->second != "false");
	}

	if (!active)
		return;

	Host::RunOnCPUThread([enabled, frame_limit_enabled]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::SetLimiterMode(enabled ? LimiterModeType::Turbo :
			(frame_limit_enabled ? LimiterModeType::Nominal : LimiterModeType::Unlimited));
	}, false);
}

void AndroidRuntime::ReloadPatches()
{
	bool active = false;
	{
		std::lock_guard lock(mutex_);
		active = vm_active_;
	}
	if (!active)
		return;

	Host::RunOnCPUThread([]() {
		if (emucorex::android::IsRetroAchievementsFeatureBlocked(
				emucorex::android::HardcoreRestrictedFeature::Cheats))
		{
			EmuConfig.EnableCheats = false;
		}
		VMManager::ReloadPatches(true, true, true, true);
	}, false);
}

void AndroidRuntime::SetNativeSurface(void* window, int width, int height, float refresh_rate)
{
	const float safe_refresh_rate =
		(std::isfinite(refresh_rate) && refresh_rate > 0.0f) ? std::min(refresh_rate, 240.0f) : 0.0f;
	__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "SetNativeSurface: window=%p size=%dx%d rate=%.2f",
		window, width, height, safe_refresh_rate);
	bool update_display_window = false;
	ANativeWindow* old_window = nullptr;
	{
		std::lock_guard lock(mutex_);
		old_window = static_cast<ANativeWindow*>(native_window_);
		native_window_ = window;
		surface_width_ = width;
		surface_height_ = height;
		surface_refresh_rate_ = safe_refresh_rate;
		// Also update when a valid old surface is replaced by an invalid/zero-sized
		// one, so GS is detached instead of continuing to present to old_window.
		update_display_window = MTGS::IsOpen() &&
			(old_window || (native_window_ && surface_width_ > 0 && surface_height_ > 0));
	}

	if (update_display_window)
	{
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "SetNativeSurface: scheduling MTGS::UpdateDisplayWindow (old_window=%p)", old_window);
		// Surface callbacks run on Android's UI/Binder threads, while the MTGS ring
		// buffer has exactly one producer: the CPU thread. Keep the old window alive
		// until the serialized GS update has detached from it.
		auto old_window_ref = std::shared_ptr<ANativeWindow>(old_window, [](ANativeWindow* value) {
			if (value)
				ANativeWindow_release(value);
		});
		Host::RunOnCPUThread([old_window_ref = std::move(old_window_ref)]() {
			(void)old_window_ref;
			if (MTGS::IsOpen())
			{
				MTGS::UpdateDisplayWindow();
				MTGS::WaitGS(false, false, false);
			}
		}, false);
		return;
	}

	if (old_window)
		ANativeWindow_release(old_window);
}

void AndroidRuntime::ClearSurface()
{
	__android_log_write(ANDROID_LOG_DEBUG, LOG_TAG, "ClearSurface: clearing native window");
	ANativeWindow* old_window = nullptr;
	bool update_display_window = false;
	{
		std::lock_guard lock(mutex_);
		old_window = static_cast<ANativeWindow*>(native_window_);
		native_window_ = nullptr;
		surface_width_ = 0;
		surface_height_ = 0;
		surface_refresh_rate_ = 0.0f;
		update_display_window = (old_window && MTGS::IsOpen());
	}

	if (update_display_window)
	{
		__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "ClearSurface: scheduling MTGS::UpdateDisplayWindow (old_window=%p)", old_window);
		auto old_window_ref = std::shared_ptr<ANativeWindow>(old_window, [](ANativeWindow* value) {
			if (value)
				ANativeWindow_release(value);
		});
		Host::RunOnCPUThread([old_window_ref = std::move(old_window_ref)]() {
			(void)old_window_ref;
			if (MTGS::IsOpen())
			{
				MTGS::UpdateDisplayWindow();
				MTGS::WaitGS(false, false, false);
			}
		}, true);
		return;
	}

	if (old_window)
		ANativeWindow_release(old_window);
}

bool AndroidRuntime::GetNativeSurface(void** window, int* width, int* height, float* refresh_rate) const
{
	std::lock_guard lock(mutex_);
	if (!native_window_ || surface_width_ <= 0 || surface_height_ <= 0)
		return false;

	*window = native_window_;
	*width = surface_width_;
	*height = surface_height_;
	*refresh_rate = surface_refresh_rate_;
	return true;
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
int AndroidRuntime::RunTFXPreparationRuntimeTests()
{
	unsigned checks = 0;
	unsigned failures = 0;
	const auto check = [&](bool condition, const char* name) {
		++checks;
		if (!condition)
			++failures;
		std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	};

	// The isolated native oracle process uses only its own working directory. Do not
	// initialize the app's persistent Android data root in this test.
	Initialize(".", 35);
	{
		std::lock_guard lock(mutex_);
		next_tfx_preparation_job_id_ = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - 1;
	}
	ResetTFXPreparationTestBarrier();
	if (!StartTFXPreparation())
	{
		check(false, "runtime admits a headless preparation job");
		std::printf("RESULT checks=%u failures=%u\n", checks, failures);
		return 1;
	}
	check(true, "runtime admits a headless preparation job");

	const bool reached = WaitForTFXPreparationTestBarrier();
	check(reached, "worker pauses at deterministic pre-bridge barrier");
	if (!reached)
	{
		ReleaseTFXPreparationTestBarrier();
		Shutdown();
		std::printf("RESULT checks=%u failures=%u\n", checks, failures);
		return 1;
	}

	TFXPreparationStatus status = GetTFXPreparationStatus();
	const uint64_t job_id = status.job_id;
	check(job_id == static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
		"last positive Java Long job identity is preserved exactly");
	check(job_id != 0 && status.attempt == 1 && TFXPreparationJob::IsActiveState(status.state),
		"start reserves job identity before bridge work");
	check(!StartTFXPreparation(), "duplicate start is rejected while active");
	check(!ResumeTFXPreparation(job_id), "resume is rejected while the current attempt is active");
	check(!CancelTFXPreparation(job_id + 1), "cancel rejects a stale job id");

	VmStartupState startup_before;
	bool vm_thread_before;
	{
		std::lock_guard lock(mutex_);
		startup_before = startup_state_;
		vm_thread_before = vm_thread_.joinable();
	}
	const bool vm_started = StartVm(".invalid-test-game", false, 0);
	bool vm_admission_unchanged;
	{
		std::lock_guard lock(mutex_);
		vm_admission_unchanged = !vm_active_ && startup_state_ == startup_before &&
			vm_thread_.joinable() == vm_thread_before;
	}
	check(!vm_started && vm_admission_unchanged,
		"VM start is rejected during preparation before startup state changes");

	auto status_future = std::async(std::launch::async, [this]() { return GetTFXPreparationStatus(); });
	const bool status_responsive = status_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
	if (!status_responsive)
		ReleaseTFXPreparationTestBarrier();
	status = status_future.get();
	check(status_responsive && status.job_id == job_id,
		"status polling remains responsive while worker is before bridge");

	auto cancel_future = std::async(std::launch::async, [this, job_id]() {
		return CancelTFXPreparation(job_id);
	});
	const bool cancel_responsive = cancel_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
	if (!cancel_responsive)
		ReleaseTFXPreparationTestBarrier();
	const bool cancel_accepted = cancel_future.get();
	check(cancel_responsive && cancel_accepted, "cancel remains responsive and is accepted at pre-bridge barrier");
	ReleaseTFXPreparationTestBarrier();

	const auto wait_for_terminal = [this]() {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		TFXPreparationStatus current;
		do
		{
			current = GetTFXPreparationStatus();
			if (!TFXPreparationJob::IsActiveState(current.state))
				return current;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		} while (std::chrono::steady_clock::now() < deadline);
		return current;
	};
	status = wait_for_terminal();
	check(status.state == TFXPreparationState::Cancelled && status.cancel_requested,
		"cancel before bridge reaches terminal status after barrier release");
	check(ResumeTFXPreparation(job_id), "terminal cancelled job starts a fresh resume attempt");
	ResetTFXPreparationTestBarrier();
	const bool resume_reached = WaitForTFXPreparationTestBarrier();
	status = GetTFXPreparationStatus();
	check(resume_reached && status.job_id == job_id && status.attempt == 2 &&
		TFXPreparationJob::IsActiveState(status.state), "resume resets attempt progress and reuses the job identity");

	check(CancelTFXPreparation(job_id), "resumed attempt accepts cancellation");
	ReleaseTFXPreparationTestBarrier();
	status = wait_for_terminal();
	check(status.state == TFXPreparationState::Cancelled, "resumed attempt reaches terminal cancellation");

	ResetTFXPreparationTestBarrier();
	check(ResumeTFXPreparation(job_id), "terminal resumed attempt can start shutdown test attempt");
	const bool shutdown_barrier_reached = WaitForTFXPreparationTestBarrier();
	check(shutdown_barrier_reached, "shutdown test worker reaches pre-bridge barrier");
	auto shutdown_future = std::async(std::launch::async, [this]() { Shutdown(); });
	auto shutdown_status_future = std::async(std::launch::async, [this]() {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		TFXPreparationStatus current;
		do
		{
			current = GetTFXPreparationStatus();
			if (current.cancel_requested)
				return current;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		} while (std::chrono::steady_clock::now() < deadline);
		return current;
	});
	const bool status_during_shutdown =
		shutdown_status_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
	if (!status_during_shutdown)
		ReleaseTFXPreparationTestBarrier();
	const TFXPreparationStatus shutdown_status = shutdown_status_future.get();
	check(status_during_shutdown && shutdown_status.cancel_requested,
		"shutdown requests cancellation without holding runtime mutex across join");
	ReleaseTFXPreparationTestBarrier();
	const bool shutdown_joined = shutdown_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
	if (!shutdown_joined)
		ReleaseTFXPreparationTestBarrier();
	if (shutdown_joined)
		shutdown_future.get();
	status = GetTFXPreparationStatus();
	check(shutdown_joined && status.state == TFXPreparationState::Cancelled,
		"shutdown joins preparation worker after cancellation cleanup");
	{
		std::lock_guard lock(s_tfx_preparation_test_barrier.mutex);
		s_tfx_preparation_test_barrier.enabled = false;
		s_tfx_preparation_test_barrier.released = true;
	}
	s_tfx_preparation_test_barrier.cv.notify_all();

	const bool exhausted_id_started = StartTFXPreparation();
	check(!exhausted_id_started, "exhausted positive Long identities reject admission without wraparound");
	if (exhausted_id_started)
	{
		CancelTFXPreparation(GetTFXPreparationStatus().job_id);
		Shutdown();
	}
	check(GetTFXPreparationStatus().job_id == job_id,
		"rejected identity exhaustion retains the previous job snapshot");
	{
		std::lock_guard lock(mutex_);
		next_tfx_preparation_job_id_ = 0;
	}

	SetSetting("EmuCore/GS", "Renderer", "int",
		std::to_string(static_cast<int>(GSRendererType::VK)));
	const Pcsx2Config::GSOptions previous_gs_config = GSConfig;
	const bool start_real_job = StartTFXPreparation();
	check(start_real_job, "runtime starts the real asynchronous Vulkan bridge job");
	if (start_real_job)
	{
		const uint64_t real_job_id = GetTFXPreparationStatus().job_id;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
		TFXPreparationStatus real_status;
		do
		{
			real_status = GetTFXPreparationStatus();
			if (!TFXPreparationJob::IsActiveState(real_status.state))
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		} while (std::chrono::steady_clock::now() < deadline);

		const bool real_job_finished = !TFXPreparationJob::IsActiveState(real_status.state);
		if (!real_job_finished)
		{
			CancelTFXPreparation(real_job_id);
			Shutdown();
			real_status = GetTFXPreparationStatus();
		}
		else
		{
			// Terminal status follows bridge cleanup. Shutdown also joins the finished
			// thread so this checks that no worker lifetime remains.
			Shutdown();
			real_status = GetTFXPreparationStatus();
		}

		check(real_job_finished && real_status.state == TFXPreparationState::Completed &&
			real_status.total == 168 && real_status.finished && real_status.in_flight == 0 &&
			real_status.failed == 0 && real_status.cancelled == 0 &&
			real_status.completed + real_status.already_available == real_status.total,
			"real runtime job compiles and accounts all 168 canonical TFX selectors");
		check(real_status.checkpoint_saved && !real_status.adapter_name.empty(),
			"real runtime job reports its adapter and persisted pipeline checkpoint");
		check(CheckTFXPreparationReadiness(), "real completed preparation validates its durable receipt before another game");

		bool runtime_has_no_worker_threads;
		{
			std::lock_guard lock(mutex_);
			runtime_has_no_worker_threads = !vm_active_ && !vm_thread_.joinable() &&
				!tfx_preparation_thread_.joinable();
		}
		check(runtime_has_no_worker_threads, "real preparation leaves no VM or preparation worker thread");
		check(!g_gs_device && !g_vulkan_shader_cache && !Vulkan::IsVulkanLibraryLoaded() &&
			VMManager::GetState() == VMState::Shutdown && !VMManager::HasValidVM() &&
			GSConfig == previous_gs_config,
			"real preparation releases Vulkan and restores GS state without initializing a VM");
	}

	std::printf("RESULT checks=%u failures=%u\n", checks, failures);
	return failures ? 1 : 0;
}
#endif

bool AndroidRuntime::CheckTFXPreparationReadiness()
{
	// A short surfaceless probe runs on the caller's IO thread. Disc admission
	// excludes VM/start/shutdown while the probe owns restored upstream globals;
	// the runtime mutex remains free for status and deferred settings updates.
	std::unique_lock disc_access_lock(s_disc_access_mutex);
	std::thread old_vm_thread, old_preparation_thread;
	{
		std::lock_guard lock(mutex_);
		if (!initialized_ || settings_batch_ || vm_active_ || startup_state_ == VmStartupState::Pending ||
			IsTFXPreparationActiveLocked()) return false;
		if (vm_thread_.joinable()) old_vm_thread = std::move(vm_thread_);
		if (tfx_preparation_thread_.joinable()) old_preparation_thread = std::move(tfx_preparation_thread_);
	}
	if (old_vm_thread.joinable()) old_vm_thread.join();
	if (old_preparation_thread.joinable()) old_preparation_thread.join();
	VmLaunchConfig config;
	{
		std::lock_guard lock(mutex_);
		if (!initialized_ || settings_batch_ || vm_active_ || startup_state_ == VmStartupState::Pending ||
			IsTFXPreparationActiveLocked()) return false;
		config = CreateLaunchConfigLocked({}, false, 0, false);
		tfx_readiness_probe_active_ = true;
	}
	bool prepared = false;
	try
	{
		TFXPreparationJob probe(1, 1);
		prepared = RunTFXPreparation(config, probe, true).state == TFXPreparationState::Completed;
	}
	catch (...) { prepared = false; }
	{
		std::lock_guard lock(mutex_);
		// Settings/data-root changes during the probe invalidate its answer.
		prepared &= !settings_batch_ && paths_.data_root == config.paths.data_root &&
			paths_.native_library_dir == config.paths.native_library_dir && settings_ == config.settings;
		tfx_readiness_probe_active_ = false;
		ApplyDeferredRuntimeGlobalsLocked();
	}
	return prepared;
}

bool AndroidRuntime::StartTFXPreparation()
{
	// Serialize only admission with VM start/stop. The worker owns the bridge lock;
	// this disc lock is released before any Vulkan preparation begins.
	std::unique_lock disc_access_lock(s_disc_access_mutex);
	std::thread finished_vm_thread;
	std::thread finished_preparation_thread;
	{
		std::lock_guard lock(mutex_);
		if (!initialized_ || settings_batch_ || vm_active_ || startup_state_ == VmStartupState::Pending ||
			IsTFXPreparationActiveLocked() ||
			next_tfx_preparation_job_id_ >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
			return false;
		if (vm_thread_.joinable())
			finished_vm_thread = std::move(vm_thread_);
		if (tfx_preparation_thread_.joinable())
			finished_preparation_thread = std::move(tfx_preparation_thread_);
	}
	if (finished_vm_thread.joinable())
		finished_vm_thread.join();
	if (finished_preparation_thread.joinable())
		finished_preparation_thread.join();

	std::lock_guard lock(mutex_);
	if (!initialized_ || settings_batch_ || vm_active_ || startup_state_ == VmStartupState::Pending ||
		IsTFXPreparationActiveLocked() ||
		next_tfx_preparation_job_id_ >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
		return false;
	// JNI cancellation/resume use positive jlong IDs. Never truncate or recycle one.
	++next_tfx_preparation_job_id_;
	auto job = std::make_shared<TFXPreparationJob>(next_tfx_preparation_job_id_, 1);
	VmLaunchConfig config = CreateLaunchConfigLocked({}, false, 0, false);
	tfx_preparation_job_ = job;
	tfx_preparation_config_ = config;
	try
	{
		tfx_preparation_thread_ = std::thread(&AndroidRuntime::TFXPreparationThreadMain, this,
			std::move(config), std::move(job));
	}
	catch (...)
	{
		if (tfx_preparation_job_)
			tfx_preparation_job_->Finish(TFXPreparationState::Failed, TFXPreparationError::InternalError);
		return false;
	}
	return true;
}

bool AndroidRuntime::CancelTFXPreparation(uint64_t job_id)
{
	std::lock_guard lock(mutex_);
	if (!tfx_preparation_job_ || tfx_preparation_job_->Snapshot().job_id != job_id)
		return false;
	return tfx_preparation_job_->RequestCancel();
}

bool AndroidRuntime::ResumeTFXPreparation(uint64_t job_id)
{
	std::unique_lock disc_access_lock(s_disc_access_mutex);
	std::thread finished_vm_thread;
	std::thread finished_preparation_thread;
	std::shared_ptr<TFXPreparationJob> original_job;
	uint32_t attempt = 0;
	{
		std::lock_guard lock(mutex_);
		if (!tfx_preparation_job_ || tfx_preparation_job_->Snapshot().job_id != job_id ||
			vm_active_ || startup_state_ == VmStartupState::Pending || settings_batch_)
			return false;
		const TFXPreparationStatus status = tfx_preparation_job_->Snapshot();
		if (status.state != TFXPreparationState::Cancelled && status.state != TFXPreparationState::Failed)
			return false;
		if (status.attempt == std::numeric_limits<uint32_t>::max())
			return false;
		attempt = status.attempt + 1;
		original_job = tfx_preparation_job_;
		if (vm_thread_.joinable())
			finished_vm_thread = std::move(vm_thread_);
		if (tfx_preparation_thread_.joinable())
			finished_preparation_thread = std::move(tfx_preparation_thread_);
	}
	if (finished_vm_thread.joinable())
		finished_vm_thread.join();
	if (finished_preparation_thread.joinable())
		finished_preparation_thread.join();

	std::lock_guard lock(mutex_);
	if (tfx_preparation_job_ != original_job || !tfx_preparation_job_ ||
		tfx_preparation_job_->Snapshot().job_id != job_id || vm_active_ ||
		startup_state_ == VmStartupState::Pending || settings_batch_)
		return false;
	const TFXPreparationStatus status = tfx_preparation_job_->Snapshot();
	if ((status.state != TFXPreparationState::Cancelled && status.state != TFXPreparationState::Failed) ||
		status.attempt + 1 != attempt)
		return false;

	auto job = std::make_shared<TFXPreparationJob>(job_id, attempt);
	tfx_preparation_job_ = job;
	try
	{
		tfx_preparation_thread_ = std::thread(&AndroidRuntime::TFXPreparationThreadMain, this,
			tfx_preparation_config_, std::move(job));
	}
	catch (...)
	{
		tfx_preparation_job_->Finish(TFXPreparationState::Failed, TFXPreparationError::InternalError);
		return false;
	}
	return true;
}

TFXPreparationStatus AndroidRuntime::GetTFXPreparationStatus() const
{
	std::lock_guard lock(mutex_);
	return tfx_preparation_job_ ? tfx_preparation_job_->Snapshot() : TFXPreparationStatus{};
}

void AndroidRuntime::TFXPreparationThreadMain(VmLaunchConfig config, std::shared_ptr<TFXPreparationJob> job)
{
#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	const bool used_test_barrier = WaitAtTFXPreparationTestBarrier();
	TFXPreparationBridgeResult result;
	if (used_test_barrier)
	{
		// Lifecycle state-machine cases intentionally stop before Vulkan and
		// never enter the bridge, even when a test fails.
		result = job->CancellationRequested() ?
			TFXPreparationBridgeResult{TFXPreparationState::Cancelled, TFXPreparationError::None} :
			TFXPreparationBridgeResult{TFXPreparationState::Failed, TFXPreparationError::InternalError};
	}
	else if (job->CancellationRequested())
	{
		result = {TFXPreparationState::Cancelled, TFXPreparationError::None};
	}
	else
	{
		result = RunTFXPreparation(config, *job);
	}
#else
	TFXPreparationBridgeResult result;
	if (job->CancellationRequested())
	{
		result = {TFXPreparationState::Cancelled, TFXPreparationError::None};
	}
	else
	{
		result = RunTFXPreparation(config, *job);
	}
#endif

	{
		std::lock_guard lock(mutex_);
		if (tfx_preparation_job_ == job)
		{
			// Serialize terminal publication with CancelTFXPreparation(). If a
			// completion races a cancel, whichever acquires mutex_ first decides
			// whether cancellation was accepted for this attempt.
			if (job->CancellationRequested())
			{
				if (!job->Finish(TFXPreparationState::Cancelled))
					job->Finish(TFXPreparationState::Failed, TFXPreparationError::InternalError);
			}
			else if (result.state == TFXPreparationState::Completed)
			{
				const bool committed = job->CommitCompleted([&result]() {
					return !result.receipt_path.empty() &&
						VKShaderCache::SaveTFXPreparationReceipt(result.receipt_path, result.receipt_identity);
				});
				if (!committed && TFXPreparationJob::IsActiveState(job->Snapshot().state))
					job->Finish(TFXPreparationState::Failed, TFXPreparationError::InternalError);
			}
			else if (result.state == TFXPreparationState::Cancelled)
			{
				job->RequestCancel();
				if (!job->Finish(TFXPreparationState::Cancelled, result.error))
					job->Finish(TFXPreparationState::Failed, TFXPreparationError::InternalError);
			}
			else if (result.state == TFXPreparationState::Unsupported)
			{
				if (!job->Finish(TFXPreparationState::Unsupported, result.error))
					job->Finish(TFXPreparationState::Failed, TFXPreparationError::InternalError);
			}
			else
			{
				if (!job->Finish(TFXPreparationState::Failed,
					result.error == TFXPreparationError::None ? TFXPreparationError::InternalError : result.error))
					job->Finish(TFXPreparationState::Failed, TFXPreparationError::InternalError);
			}
			ApplyDeferredRuntimeGlobalsLocked();
		}
	}
}

bool AndroidRuntime::StartVm(std::string path, bool boot_elf, int probe_steps, bool boot_irx)
{
	std::lock_guard disc_access_lock(s_disc_access_mutex);
	{
		std::lock_guard lock(mutex_);
		if (IsTFXPreparationActiveLocked())
		{
			__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "VM start refused: TFX preparation is active");
			return false;
		}
	}

	std::thread finished_thread;
	{
		std::lock_guard lock(mutex_);
		if (!vm_active_ && vm_thread_.joinable())
			finished_thread = std::move(vm_thread_);
	}

	if (finished_thread.joinable())
		finished_thread.join();

	{
		std::lock_guard lock(mutex_);
		if (IsTFXPreparationActiveLocked())
		{
			__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "VM start refused: TFX preparation is active");
			return false;
		}

		if (!initialized_)
		{
			__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "VM start refused: runtime is not initialized");
			return false;
		}

		if (vm_active_)
		{
			__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "VM start refused: VM is already active");
			return false;
		}
	}

	if (!IsUpstreamVmBridgeAvailable())
	{
		VmLaunchConfig config;
		{
			std::lock_guard lock(mutex_);
			config = CreateLaunchConfigLocked(std::move(path), boot_elf, probe_steps, boot_irx);
		}
		return RunUpstreamVm(config);
	}

	VmLaunchConfig config;
	{
		std::unique_lock lock(mutex_);
		config = CreateLaunchConfigLocked(std::move(path), boot_elf, probe_steps, boot_irx);
		current_path_ = config.path;
		vm_active_ = true;
		paused_ = false;
		startup_state_ = VmStartupState::Pending;
		vm_thread_ = std::thread(&AndroidRuntime::VmThreadMain, this, std::move(config));

		const bool startup_reported = startup_cv_.wait_for(lock, std::chrono::seconds(45), [this]() {
			return startup_state_ != VmStartupState::Pending;
		});
		if (!startup_reported)
		{
			__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "VM start timed out waiting for upstream initialization");
			return false;
		}

		return startup_state_ == VmStartupState::Succeeded;
	}
}

bool AndroidRuntime::ChangeDisc(std::string path)
{
	if (path.empty())
		return false;

	std::lock_guard disc_access_lock(s_disc_access_mutex);
	{
		std::lock_guard lock(mutex_);
		if (!vm_active_)
			return false;
		// The caller resumes after the swap. Keeping a single resume authority avoids
		// racing the Activity lifecycle and the in-game menu's paused state.
		paused_ = true;
	}

	// Wake the CPU loop so it drains the blocking task even when it was executing
	// recompiled guest code when Android delivered the picker result.
	if (Cpu)
		Cpu->ExitExecution();

	auto result = std::make_shared<bool>(false);
	Host::RunOnCPUThread([path, result]() mutable {
		if (!VMManager::HasValidVM())
			return;
		VMManager::SetPaused(true);
		*result = VMManager::ChangeDisc(CDVD_SourceType::Iso, std::move(path));
	}, true);

	if (*result)
	{
		std::lock_guard lock(mutex_);
		current_path_ = std::move(path);
	}
	return *result;
}

void AndroidRuntime::Pause()
{
	bool active = false;
	bool already_paused = false;
	{
		std::lock_guard lock(mutex_);
		active = vm_active_;
		already_paused = paused_;
		if (active)
			paused_ = true;
	}
	if (!active || already_paused)
		return;

	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "VM pause requested");
	if (Cpu)
		Cpu->ExitExecution();
	Host::RunOnCPUThread([]() {
		if (Cpu)
			Cpu->ExitExecution();
		VMManager::SetPaused(true);
	}, false);
}

void AndroidRuntime::Resume()
{
	{
		std::lock_guard lock(mutex_);
		if (!vm_active_)
			return;
		paused_ = false;
	}

	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "VM resume requested");
	Host::RunOnCPUThread([]() {
		VMManager::SetPaused(false);
	}, false);
}

void AndroidRuntime::Shutdown()
{
	std::lock_guard disc_access_lock(s_disc_access_mutex);

	std::thread thread_to_join;
	std::thread preparation_thread_to_join;
	{
		std::lock_guard lock(mutex_);
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "VM shutdown requested active=%d joinable=%d",
			vm_active_ ? 1 : 0, vm_thread_.joinable() ? 1 : 0);
		vm_active_ = false;
		paused_ = false;
		if (startup_state_ == VmStartupState::Pending)
		{
			startup_state_ = VmStartupState::Failed;
			startup_cv_.notify_all();
		}
		current_path_.clear();
		if (vm_thread_.joinable() && vm_thread_.get_id() != std::this_thread::get_id())
			thread_to_join = std::move(vm_thread_);
		if (tfx_preparation_job_ && TFXPreparationJob::IsActiveState(tfx_preparation_job_->Snapshot().state))
			tfx_preparation_job_->RequestCancel();
		if (tfx_preparation_thread_.joinable() &&
			tfx_preparation_thread_.get_id() != std::this_thread::get_id())
			preparation_thread_to_join = std::move(tfx_preparation_thread_);
	}

	if (thread_to_join.joinable())
	{
		Host::RunOnCPUThread([]() {
			RequestCurrentVmStop();
		}, false);
		thread_to_join.join();
		ClearPendingHostCpuTasks();
	}
	if (preparation_thread_to_join.joinable())
		preparation_thread_to_join.join();
}

bool AndroidRuntime::HasValidVm() const
{
	std::lock_guard lock(mutex_);
	return vm_active_;
}

std::string AndroidRuntime::GetGameTitle(const std::string& path) const
{
	if (path.empty())
		return {};

	std::lock_guard disc_access_lock(s_disc_access_mutex);

	std::string data_root;
	bool apply_folders = false;
	{
		std::lock_guard lock(mutex_);
		if (vm_active_)
			return BasenameWithoutExtension(path);
		data_root = paths_.data_root;
		apply_folders = !IsTFXPreparationActiveLocked();
	}

	if (apply_folders && !data_root.empty())
	{
		ApplyAndroidEmuFolders(data_root);
	}

	GameList::Entry entry;
	if (GameList::PopulateEntryFromPath(path, &entry))
	{
		const std::string& entry_title = entry.GetTitle(true).empty() ? entry.title : entry.GetTitle(true);
		const std::string title = entry_title.empty() ? BasenameWithoutExtension(path) : entry_title;
		return FormatGameMetadata(title, entry.serial, entry.crc);
	}

	return BasenameWithoutExtension(path);
}

std::string AndroidRuntime::GetGameSerial() const
{
	std::lock_guard lock(mutex_);
	const auto it = settings_.find(SettingKey("EmuCoreX", "LastGameSerial"));
	return (it == settings_.end()) ? std::string() : it->second;
}

std::string AndroidRuntime::GetSaveStatePathForFile(const std::string& path, int slot) const
{
	std::lock_guard disc_access_lock(s_disc_access_mutex);

	std::string data_root;
	bool vm_active = false;
	{
		std::lock_guard lock(mutex_);
		data_root = paths_.data_root;
		vm_active = vm_active_;
	}

	if (vm_active)
	{
		const std::string serial = VMManager::GetDiscSerial();
		const u32 crc = VMManager::GetDiscCRC();
		const std::string current_filename = VMManager::GetSaveStateFileName(serial.c_str(), crc, slot);
		if (!current_filename.empty())
			return current_filename;
	}
	else if (!path.empty())
	{
		std::string serial;
		u32 crc = 0;
		if (GameList::GetSerialAndCRCForFilename(path.c_str(), &serial, &crc))
		{
			const std::string filename = VMManager::GetSaveStateFileName(serial.c_str(), crc, slot);
			if (!filename.empty())
				return filename;
		}
	}

	if (data_root.empty())
		return {};
	std::ostringstream out;
	out << data_root << "/sstates/" << BasenameWithoutExtension(path) << "." << slot << ".p2s";
	return out.str();
}

bool AndroidRuntime::SaveStateToSlot(int slot)
{
	if (!HasValidVm())
		return false;

	auto result = std::make_shared<bool>(false);
	auto failed = std::make_shared<std::atomic_bool>(false);
	Host::RunOnCPUThread([slot, result, failed]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::SaveStateToSlot(slot, true, [slot, failed](const std::string& message) {
			failed->store(true);
			__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "save state slot %d failed: %s", slot, message.c_str());
		});
		*result = true;
	}, true);
	// The error callback may run on the ZIP worker. Wait before reporting
	// success so an old slot file cannot hide a failed asynchronous save.
	VMManager::WaitForSaveStateFlush();
	return *result && !failed->load();
}

void AndroidRuntime::WaitForSaveStateFlush()
{
	if (!HasValidVm())
		return;

	VMManager::WaitForSaveStateFlush();
}

bool AndroidRuntime::LoadStateFromSlot(int slot)
{
	if (!HasValidVm())
		return false;

	// Never load a state whose background zip is still running: the archive would
	// be read half-written.
	VMManager::WaitForSaveStateFlush();

	auto result = std::make_shared<bool>(false);
	Host::RunOnCPUThread([slot, result]() {
		if (!VMManager::HasValidVM())
			return;

		if (emucorex::android::IsRetroAchievementsFeatureBlocked(
				emucorex::android::HardcoreRestrictedFeature::LoadState))
		{
			__android_log_print(ANDROID_LOG_WARN, LOG_TAG,
				"load state slot %d blocked by RetroAchievements Hardcore Mode", slot);
			return;
		}

		Error error;
		*result = VMManager::LoadStateFromSlot(slot, false, &error);
		if (!*result)
			__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "load state slot %d failed: %s", slot, error.GetDescription().c_str());
	}, true);
	return *result;
}

std::string AndroidRuntime::ListMemoryCards() const
{
	EnsureMemoryCardFolder(GetSetting("Folders", "MemoryCards"));

	std::ostringstream out;
	out << '[';

	bool first = true;
	for (const AvailableMcdInfo& card : FileMcd_GetAvailableCards(true))
	{
		if (!first)
			out << ',';
		first = false;

		out << '{'
		    << "\"name\":\"" << JsonEscape(card.name) << "\","
		    << "\"path\":\"" << JsonEscape(card.path) << "\","
		    << "\"modifiedTime\":" << static_cast<long long>(card.modified_time) << ','
		    << "\"type\":" << static_cast<int>(card.type) << ','
		    << "\"fileType\":" << static_cast<int>(card.file_type) << ','
		    << "\"sizeBytes\":" << static_cast<unsigned long long>(card.size) << ','
		    << "\"formatted\":" << (card.formatted ? "true" : "false")
		    << '}';
	}

	out << ']';
	return out.str();
}

bool AndroidRuntime::CreateMemoryCard(const std::string& name, int type, int file_type)
{
	EnsureMemoryCardFolder(GetSetting("Folders", "MemoryCards"));

	const std::string file_name(Path::GetFileName(name));
	if (file_name.empty() || !Path::IsValidFileName(file_name, false))
	{
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "invalid memory card name: %s", name.c_str());
		return false;
	}

	if (FileMcd_GetCardInfo(file_name).has_value())
		return true;

	const bool created = FileMcd_CreateNewCard(file_name, ToMemoryCardType(type), ToMemoryCardFileType(file_type));
	if (!created)
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "failed to create memory card: %s", file_name.c_str());
	return created;
}

void AndroidRuntime::SetPadButton(int pad_index, int index, int range, bool pressed)
{
	if (!VMManager::HasValidVM())
		return;

	if (IsArcadePadKey(index))
	{
		const u32 controller =
			static_cast<u32>(std::clamp(pad_index, 0, static_cast<int>(Pad::NUM_CONTROLLER_PORTS - 1)));
		if (index == ANDROID_KEY_COIN)
		{
			if (pressed)
				s_pending_coin_edges[controller].fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (index == ANDROID_KEY_SERVICE)
		{
			if (pressed)
				s_pending_service_edges[controller].fetch_add(1, std::memory_order_relaxed);
			return;
		}

		const u32 bit = ArcadeKeyBit(index);
		u32 current = s_pending_arcade_buttons[controller].load(std::memory_order_relaxed);
		if (pressed)
			current |= bit;
		else
			current &= ~bit;
		s_pending_arcade_buttons[controller].store(current, std::memory_order_release);
		return;
	}

	const std::optional<u32> bind = MapAndroidPadKeyToDualShock2Input(index);
	if (!bind.has_value())
		return;

	const u32 controller = static_cast<u32>(std::clamp(pad_index, 0, static_cast<int>(Pad::NUM_CONTROLLER_PORTS - 1)));
	const float value = PadValueFromAndroidRange(range, pressed);
	PublishPadValue(controller, bind.value(), value, pressed);
}

void AndroidRuntime::SetPadPressureModifierAmount(int amount_percent)
{
	if (!VMManager::HasValidVM())
		return;

	const float amount = static_cast<float>(std::clamp(amount_percent, 1, 100)) / 100.0f;
	s_pending_pressure_modifier.store(FloatToBits(amount), std::memory_order_relaxed);
	s_pending_pressure_modifier_dirty.store(true, std::memory_order_release);
}

void AndroidRuntime::ResetPadState(int pad_index)
{
	if (!VMManager::HasValidVM())
		return;

	const u32 controller = static_cast<u32>(std::clamp(pad_index, 0, static_cast<int>(Pad::NUM_CONTROLLER_PORTS - 1)));
	s_pending_pad_events[controller].store(0, std::memory_order_release);
	s_pending_arcade_buttons[controller].store(0, std::memory_order_release);
	// The CPU-thread mirror must be cleared too, or the next press of an arcade/gun button
	// looks unchanged to the poll and is swallowed until the button is released again.
	s_applied_arcade_buttons[controller] = 0;
	s_pending_coin_edges[controller].store(0, std::memory_order_release);
	s_pending_service_edges[controller].store(0, std::memory_order_release);
	for (u32 i = 0; i < PadDualshock2::LENGTH; i++)
		PublishPadValue(controller, i, 0.0f);
}

void AndroidRuntime::ResetKeyStatus()
{
	ResetPadState(0);
	ResetPadState(1);
}

void ApplyPendingArcadeButtonsOnCPUThread()
{
	for (u32 controller = 0; controller < Pad::NUM_CONTROLLER_PORTS; controller++)
	{
		const u32 coin_edges = s_pending_coin_edges[controller].exchange(0, std::memory_order_acquire);
		const u32 service_edges =
			s_pending_service_edges[controller].exchange(0, std::memory_order_acquire);
		if (ACJV::enabled)
		{
			for (u32 i = 0; i < coin_edges; i++)
				ACJV::InsertCoin(controller);
			for (u32 i = 0; i < service_edges; i++)
				ACJV::ToggleDIPSwitchState(0);
		}

		const u32 current = s_pending_arcade_buttons[controller].load(std::memory_order_acquire);
		const u32 changed = current ^ s_applied_arcade_buttons[controller];
		if (changed == 0)
			continue;

		s_applied_arcade_buttons[controller] = current;

		const auto value_for = [current](int key) {
			return (current & ArcadeKeyBit(key)) != 0 ? 1.0f : 0.0f;
		};

		// GunCon2 USB binds are a no-op when the port has no light gun, so these
		// are safe for regular DualShock2 games too.
		if ((changed & ArcadeKeyBit(ANDROID_KEY_GUN_TRIGGER)) != 0)
			USB::SetDeviceBindValue(controller, GUNCON2_BIND_TRIGGER, value_for(ANDROID_KEY_GUN_TRIGGER));
		if ((changed & ArcadeKeyBit(ANDROID_KEY_GUN_PEDAL)) != 0)
			USB::SetDeviceBindValue(controller, GUNCON2_BIND_PEDAL, value_for(ANDROID_KEY_GUN_PEDAL));
		if ((changed & ArcadeKeyBit(ANDROID_KEY_GUN_RELOAD)) != 0)
			USB::SetDeviceBindValue(
				controller, GUNCON2_BIND_SHOOT_OFFSCREEN, value_for(ANDROID_KEY_GUN_RELOAD));
		if ((changed & ArcadeKeyBit(ANDROID_KEY_GUN_RECALIBRATE)) != 0)
			USB::SetDeviceBindValue(
				controller, GUNCON2_BIND_RECALIBRATE, value_for(ANDROID_KEY_GUN_RECALIBRATE));
	}
}

void PollPendingPadUpdatesOnCPUThread()
{
	if (!VMManager::HasValidVM())
	{
		for (std::atomic<u64>& events : s_pending_pad_events)
			events.store(0, std::memory_order_relaxed);
		for (std::atomic<u32>& buttons : s_pending_arcade_buttons)
			buttons.store(0, std::memory_order_relaxed);
		for (std::atomic<u32>& edges : s_pending_coin_edges)
			edges.store(0, std::memory_order_relaxed);
		for (std::atomic<u32>& edges : s_pending_service_edges)
			edges.store(0, std::memory_order_relaxed);
		s_applied_arcade_buttons.fill(0);
		s_pending_pressure_modifier_dirty.store(false, std::memory_order_relaxed);
		return;
	}

	for (u32 controller = 0; controller < Pad::NUM_CONTROLLER_PORTS; controller++)
	{
		const u64 events = s_pending_pad_events[controller].exchange(0, std::memory_order_acquire);
		u32 dirty = static_cast<u32>(events);
		const u32 pressed = static_cast<u32>(events >> 32);
		while (dirty != 0)
		{
			const u32 bind = static_cast<u32>(std::countr_zero(dirty));
			dirty &= dirty - 1;
			const u32 value = s_pending_pad_values[controller][bind].load(std::memory_order_relaxed);
			if ((pressed & (1u << bind)) != 0 && value == FloatToBits(0.0f))
			{
				// A complete press/release arrived between CPU polls. Expose the press for
				// this frame and republish the already-recorded release for the next poll.
				const u32 press_value =
					s_pending_pad_press_values[controller][bind].load(std::memory_order_relaxed);
				Pad::SetControllerState(controller, bind, BitsToFloat(press_value));
				s_pending_pad_events[controller].fetch_or(u64{1} << bind, std::memory_order_release);
			}
			else
			{
				Pad::SetControllerState(controller, bind, BitsToFloat(value));
			}
		}
	}

	if (s_pending_pressure_modifier_dirty.exchange(false, std::memory_order_acquire))
	{
		const float amount = BitsToFloat(s_pending_pressure_modifier.load(std::memory_order_relaxed));
		for (u32 controller = 0; controller < Pad::NUM_CONTROLLER_PORTS; controller++)
		{
			if (PadBase* pad = Pad::GetPad(static_cast<u8>(controller)))
				pad->SetPressureModifier(amount);
		}
	}

	ApplyPendingArcadeButtonsOnCPUThread();
}

void AndroidRuntime::OnHostKeyEvent(int key_code, bool pressed)
{
	InputManager::InvokeEvents(InputManager::MakeHostKeyboardKey(static_cast<u32>(key_code)), pressed ? 1.0f : 0.0f);
}

void AndroidRuntime::OnHostMousePosition(float x, float y)
{
	InputManager::UpdatePointerAbsolutePosition(0, x, y);
}

void AndroidRuntime::OnHostMouseButton(int button, bool pressed)
{
	u32 button_index = 0;
	switch (button)
	{
		case 1:
			button_index = 0;
			break;
		case 2:
			button_index = 1;
			break;
		case 4:
			button_index = 2;
			break;
		default:
			return;
	}
	InputManager::InvokeEvents(InputManager::MakePointerButtonKey(0, button_index), pressed ? 1.0f : 0.0f);
}

void AndroidRuntime::OnHostMouseWheel(float horizontal, float vertical)
{
	if (horizontal != 0.0f)
		InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::WheelX, horizontal);
	if (vertical != 0.0f)
		InputManager::UpdatePointerRelativeDelta(0, InputPointerAxis::WheelY, vertical);
}

void AndroidRuntime::Log(const std::string& message) const
{
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, message.c_str());
}

VmLaunchConfig AndroidRuntime::CreateLaunchConfigLocked(std::string path, bool boot_elf, int probe_steps, bool boot_irx) const
{
	VmLaunchConfig config;
	config.paths = paths_;
	config.settings = settings_;
	config.path = std::move(path);
	config.boot_elf = boot_elf;
	config.boot_irx = boot_irx;
	config.probe_steps = probe_steps;
	return config;
}

void AndroidRuntime::VmThreadMain(VmLaunchConfig config)
{
	const bool result = RunUpstreamVm(config, &AndroidRuntime::NotifyVmStartupThunk, this);
	{
		std::lock_guard lock(mutex_);
		if (startup_state_ == VmStartupState::Pending)
		{
			startup_state_ = result ? VmStartupState::Succeeded : VmStartupState::Failed;
			startup_cv_.notify_all();
		}
		vm_active_ = false;
		paused_ = false;
		current_path_.clear();
	}

	__android_log_print(result ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, LOG_TAG,
		"VM thread finished result=%s", result ? "true" : "false");
}

void AndroidRuntime::NotifyVmStartup(bool succeeded)
{
	std::lock_guard lock(mutex_);
	if (startup_state_ != VmStartupState::Pending)
		return;

	startup_state_ = succeeded ? VmStartupState::Succeeded : VmStartupState::Failed;
	startup_cv_.notify_all();
}

void AndroidRuntime::NotifyVmStartupThunk(void* userdata, bool succeeded)
{
	static_cast<AndroidRuntime*>(userdata)->NotifyVmStartup(succeeded);
}

std::string JStringToString(JNIEnv* env, jstring value)
{
	if (!value)
		return {};
	const char* chars = env->GetStringUTFChars(value, nullptr);
	if (!chars)
		return {};
	std::string result(chars);
	env->ReleaseStringUTFChars(value, chars);
	return result;
}

jstring StringToJString(JNIEnv* env, const std::string& value)
{
	return env->NewStringUTF(value.c_str());
}
}

#if defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
extern "C" __attribute__((visibility("default"))) int EmuCoreXRunTFXPreparationRuntimeTests(const char*)
{
	return emucorex::android::AndroidRuntime::Instance().RunTFXPreparationRuntimeTests();
}
#endif
