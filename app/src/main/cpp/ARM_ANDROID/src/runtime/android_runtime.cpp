#include "emucorex/android_runtime.h"
#include "emucorex/upstream_vm_bridge.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/Achievements.h"
#include "pcsx2/Config.h"
#include "pcsx2/GameList.h"
#include "pcsx2/R5900.h"
#include "pcsx2/SIO/Memcard/MemoryCardFile.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "emucorex/retro_achievements_android.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/Host.h"
#include "pcsx2/VMManager.h"

#include <SDL3/SDL_main.h>

#include <android/log.h>
#include <android/native_window.h>

#include <chrono>
#include <cstdlib>
#include <iomanip>
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

void AndroidRuntime::Initialize(std::string data_root, int api_version)
{
	std::lock_guard lock(mutex_);
	SDL_SetMainReady();
	paths_.data_root = std::move(data_root);
	ApplyAndroidEmuFolders(paths_.data_root);
	emucorex::android::InitializeSettingsLayer();
	api_version_ = api_version;
	initialized_ = true;
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "Android runtime initialized for PCSX2 v2.7.316 adapter");
}

void AndroidRuntime::ReloadDataRoot(std::string data_root)
{
	std::lock_guard lock(mutex_);
	paths_.data_root = std::move(data_root);
	ApplyAndroidEmuFolders(paths_.data_root);
}

void AndroidRuntime::SetNativeLibraryDir(std::string path)
{
	std::lock_guard lock(mutex_);
	paths_.native_library_dir = std::move(path);
	if (!paths_.native_library_dir.empty())
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
		}, true);
	}
}

void AndroidRuntime::SetSetting(std::string section, std::string key, std::string, std::string value)
{
	std::lock_guard lock(mutex_);
	if (section == "EmuCoreX" && key == "CustomDriverPath")
		ApplyCustomDriverPathEnvironment(value);
	if (section == "EmuCoreX" && key == "AppVersion")
		setenv("EMUCOREX_APP_VERSION", value.c_str(), 1);
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

void AndroidRuntime::SetNativeSurface(void* window, int width, int height)
{
	bool update_display_window = false;
	ANativeWindow* old_window = nullptr;
	{
		std::lock_guard lock(mutex_);
		old_window = static_cast<ANativeWindow*>(native_window_);
		native_window_ = window;
		surface_width_ = width;
		surface_height_ = height;
		// Also update when a valid old surface is replaced by an invalid/zero-sized
		// one, so GS is detached instead of continuing to present to old_window.
		update_display_window = MTGS::IsOpen() &&
			(old_window || (native_window_ && surface_width_ > 0 && surface_height_ > 0));
	}

	if (update_display_window)
	{
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
	ANativeWindow* old_window = nullptr;
	bool update_display_window = false;
	{
		std::lock_guard lock(mutex_);
		old_window = static_cast<ANativeWindow*>(native_window_);
		native_window_ = nullptr;
		surface_width_ = 0;
		surface_height_ = 0;
		update_display_window = (old_window && MTGS::IsOpen());
	}

	if (update_display_window)
	{
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

bool AndroidRuntime::GetNativeSurface(void** window, int* width, int* height) const
{
	std::lock_guard lock(mutex_);
	if (!native_window_ || surface_width_ <= 0 || surface_height_ <= 0)
		return false;

	*window = native_window_;
	*width = surface_width_;
	*height = surface_height_;
	return true;
}

bool AndroidRuntime::StartVm(std::string path, bool boot_elf, int probe_steps, bool boot_irx)
{
	std::lock_guard disc_access_lock(s_disc_access_mutex);

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
	}

	if (thread_to_join.joinable())
	{
		Host::RunOnCPUThread([]() {
			RequestCurrentVmStop();
		}, false);
		thread_to_join.join();
		ClearPendingHostCpuTasks();
	}
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
	{
		std::lock_guard lock(mutex_);
		if (vm_active_)
			return BasenameWithoutExtension(path);
		data_root = paths_.data_root;
	}

	if (!data_root.empty())
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
	Host::RunOnCPUThread([slot, result]() {
		if (!VMManager::HasValidVM())
			return;

		VMManager::SaveStateToSlot(slot, true, [slot](const std::string& error) {
			__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "save state slot %d failed: %s", slot, error.c_str());
		});
		*result = true;
	}, true);
	return *result;
}

bool AndroidRuntime::LoadStateFromSlot(int slot)
{
	if (!HasValidVm())
		return false;

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

	const std::optional<u32> bind = MapAndroidPadKeyToDualShock2Input(index);
	if (!bind.has_value())
		return;

	const u32 controller = static_cast<u32>(std::clamp(pad_index, 0, static_cast<int>(Pad::NUM_CONTROLLER_PORTS - 1)));
	const float value = PadValueFromAndroidRange(range, pressed);
	Pad::SetControllerState(controller, bind.value(), value);
}

void AndroidRuntime::SetPadPressureModifierAmount(int amount_percent)
{
	if (!VMManager::HasValidVM())
		return;

	const float amount = static_cast<float>(std::clamp(amount_percent, 1, 100)) / 100.0f;
	for (u32 controller = 0; controller < Pad::NUM_CONTROLLER_PORTS; controller++)
	{
		if (PadBase* pad = Pad::GetPad(static_cast<u8>(controller)))
			pad->SetPressureModifier(amount);
	}
}

void AndroidRuntime::ResetPadState(int pad_index)
{
	if (!VMManager::HasValidVM())
		return;

	const u32 controller = static_cast<u32>(std::clamp(pad_index, 0, static_cast<int>(Pad::NUM_CONTROLLER_PORTS - 1)));
	for (u32 i = 0; i < PadDualshock2::LENGTH; i++)
		Pad::SetControllerState(controller, i, 0.0f);
}

void AndroidRuntime::ResetKeyStatus()
{
	ResetPadState(0);
	ResetPadState(1);
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
