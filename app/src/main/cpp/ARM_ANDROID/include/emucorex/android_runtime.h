#pragma once

#include <jni.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace emucorex::android
{
struct RuntimePaths
{
	std::string data_root;
	std::string native_library_dir;
};

using RuntimeSettings = std::unordered_map<std::string, std::string>;

struct VmLaunchConfig
{
	RuntimePaths paths;
	RuntimeSettings settings;
	std::string path;
	bool boot_elf = false;
	bool boot_irx = false;
	int probe_steps = 0;
};

class AndroidRuntime
{
public:
	static AndroidRuntime& Instance();

	void Initialize(std::string data_root, int api_version);
	void ReloadDataRoot(std::string data_root);
	void SetNativeLibraryDir(std::string path);
	void BeginSettingsBatch();
	void EndSettingsBatch();
	void SetSetting(std::string section, std::string key, std::string type, std::string value);
	std::string GetSetting(const std::string& section, const std::string& key) const;
	void SetFrameLimitEnabled(bool enabled);
	void SetTurboModeEnabled(bool enabled);
	void ReloadPatches();
	void SetNativeSurface(void* window, int width, int height);
	void ClearSurface();
	bool GetNativeSurface(void** window, int* width, int* height) const;

	bool StartVm(std::string path, bool boot_elf, int probe_steps, bool boot_irx = false);
	bool ChangeDisc(std::string path);
	void Pause();
	void Resume();
	void Shutdown();
	bool HasValidVm() const;

	std::string GetGameTitle(const std::string& path) const;
	std::string GetGameSerial() const;
	std::string GetSaveStatePathForFile(const std::string& path, int slot) const;
	bool SaveStateToSlot(int slot);
	bool LoadStateFromSlot(int slot);
	std::string ListMemoryCards() const;
	bool CreateMemoryCard(const std::string& name, int type, int file_type);

	void SetPadButton(int pad_index, int index, int range, bool pressed);
	void SetPadPressureModifierAmount(int amount_percent);
	void ResetPadState(int pad_index);
	void ResetKeyStatus();
	void OnHostKeyEvent(int key_code, bool pressed);
	void OnHostMousePosition(float x, float y);
	void OnHostMouseButton(int button, bool pressed);
	void OnHostMouseWheel(float horizontal, float vertical);
	void Log(const std::string& message) const;

private:
	enum class VmStartupState
	{
		Idle,
		Pending,
		Succeeded,
		Failed
	};

	AndroidRuntime() = default;
	~AndroidRuntime();

	VmLaunchConfig CreateLaunchConfigLocked(std::string path, bool boot_elf, int probe_steps, bool boot_irx) const;
	void VmThreadMain(VmLaunchConfig config);
	void NotifyVmStartup(bool succeeded);
	static void NotifyVmStartupThunk(void* userdata, bool succeeded);

	mutable std::mutex mutex_;
	std::condition_variable startup_cv_;
	RuntimePaths paths_;
	bool initialized_ = false;
	bool settings_batch_ = false;
	bool vm_active_ = false;
	bool paused_ = false;
	VmStartupState startup_state_ = VmStartupState::Idle;
	int api_version_ = 0;
	void* native_window_ = nullptr;
	int surface_width_ = 0;
	int surface_height_ = 0;
	std::string current_path_;
	RuntimeSettings settings_;
	std::thread vm_thread_;
};

std::string JStringToString(JNIEnv* env, jstring value);
jstring StringToJString(JNIEnv* env, const std::string& value);
void ConfigureNativeAppCallbacks(JNIEnv* env, jclass native_app_class);
JavaVM* GetJavaVM();
void DispatchPadVibration(int pad_index, float large_motor, float small_motor);
void DispatchRetroAchievementsNotification(const char* kind, const char* title, const char* message, const char* image_path);
bool DispatchRetroAchievementsSound(const char* path);
void SetPerformanceMetricsCallbackEnabled(bool enabled, bool detailed, bool gpu_timing);
bool IsPerformanceGpuTimingRequested();
void SetPerformanceGpuState(std::string name, bool timing_active);
std::string GetPerformanceMetricsSnapshot();
void RequestCurrentVmStop();
void ClearPendingHostCpuTasks();
void PollPendingPadUpdatesOnCPUThread();
}
