// SPDX-License-Identifier: GPL-3.0+

#include "emucorex/android_crash_diagnostics.h"
#include "emucorex/android_runtime.h"
#include "emucorex/retro_achievements_android.h"

#include "pcsx2/Achievements.h"
#include "pcsx2/Counters.h"
#include "pcsx2/GS/GS.h"
#include "pcsx2/GS/Renderers/Common/GSDevice.h"
#include "pcsx2/Host.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/SPU2/spu2.h"
#include "pcsx2/VMManager.h"

#include "common/ProgressCallback.h"
#include "common/HostSys.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/WindowInfo.h"

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <sys/system_properties.h>
#include <thread>
#include <vector>

namespace Host
{
using FileSelectorCallback = std::function<void(const std::string& path)>;
using FileSelectorFilters = std::vector<std::string>;
void BeginTextInput();
void EndTextInput();
void RequestExitApplication(bool allow_confirm);
void RequestExitBigPicture();
void OnCoverDownloaderOpenRequested();
void OnCreateMemoryCardOpenRequested();
bool LocaleCircleConfirm();
bool ShouldPreferHostFileSelector();
void OpenHostFileSelectorAsync(std::string_view title, bool select_directory, FileSelectorCallback callback,
	FileSelectorFilters filters = FileSelectorFilters(), std::string_view initial_directory = std::string_view());
}

namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";

std::mutex s_cpu_tasks_mutex;
std::deque<std::function<void()>> s_cpu_tasks;
std::thread::id s_cpu_thread_id;
std::chrono::steady_clock::time_point s_last_metrics_dispatch;
std::mutex s_metrics_mutex;
std::string s_metrics_snapshot;
std::atomic_bool s_performance_metrics_enabled{false};
std::atomic_bool s_performance_metrics_detailed{false};
std::atomic_bool s_performance_gpu_timing_requested{false};
std::string s_performance_gpu_name;
bool s_performance_gpu_timing_active = false;

void LogHostMessage(android_LogPriority priority, std::string_view title, std::string_view message)
{
	if (title.empty())
		__android_log_write(priority, LOG_TAG, std::string(message).c_str());
	else
		__android_log_print(priority, LOG_TAG, "%.*s: %.*s",
			static_cast<int>(title.size()), title.data(),
			static_cast<int>(message.size()), message.data());
}

void AppendLine(std::string& text, const std::string& line)
{
	if (!text.empty())
		text.push_back('\n');
	text.append(line);
}

void AppendFormat(std::string& text, const char* format, ...)
{
	char buffer[192];
	va_list args;
	va_start(args, format);
	const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (written <= 0)
		return;

	text.append(buffer, static_cast<size_t>(std::min<int>(written, static_cast<int>(sizeof(buffer) - 1))));
}

void AppendProcessorStat(std::string& text, const char* label, double usage, double time)
{
	if (!std::isfinite(usage))
		usage = 0.0;
	if (!std::isfinite(time))
		time = 0.0;

	text.append(label);
	if (usage >= 99.95)
		AppendFormat(text, "100%% (%.2fms)", time);
	else
		AppendFormat(text, "%.1f%% (%.2fms)", usage, time);
}

std::string GetAndroidSystemProperty(const char* name)
{
	char value[PROP_VALUE_MAX] = {};
	const int length = __system_property_get(name, value);
	return length > 0 ? std::string(value, static_cast<size_t>(length)) : std::string();
}

std::string GetHostCpuName(const CPUInfo& cpu_info)
{
	if (!cpu_info.name.empty() && cpu_info.name != "Unknown")
		return cpu_info.name;

	std::string name = GetAndroidSystemProperty("ro.soc.model");
	if (name.empty())
		name = GetAndroidSystemProperty("ro.board.platform");
	if (name.empty())
		name = GetAndroidSystemProperty("ro.hardware");
	return name.empty() ? "Unknown" : name;
}

std::string GetCompactGpuName(std::string name)
{
	constexpr std::string_view QUALCOMM_PREFIX = "Qualcomm ";
	if (name.starts_with(QUALCOMM_PREFIX))
		name.erase(0, QUALCOMM_PREFIX.size());

	constexpr std::string_view TRADEMARK = "(TM) ";
	for (size_t position = name.find(TRADEMARK); position != std::string::npos;
		position = name.find(TRADEMARK, position))
	{
		name.erase(position, TRADEMARK.size());
	}
	return name;
}

const char* GetInternalFpsMethodSuffix()
{
	switch (PerformanceMetrics::GetInternalFPSMethod())
	{
		case PerformanceMetrics::InternalFPSMethod::GSPrivilegedRegister:
			return " [P]";
		case PerformanceMetrics::InternalFPSMethod::DISPFBBlit:
			return " [B]";
		case PerformanceMetrics::InternalFPSMethod::None:
		default:
			return "";
	}
}

std::string GetFirstStatsSegment(const SmallStringBase& stats)
{
	if (stats.empty())
		return {};

	std::string_view segment(stats.c_str());
	const size_t separator = segment.find('|');
	if (separator != std::string_view::npos)
		segment = segment.substr(0, separator);
	while (!segment.empty() && segment.back() == ' ')
		segment.remove_suffix(1);

	return std::string(segment);
}

}

namespace emucorex::android
{
void RequestCurrentVmStop()
{
	if (!VMManager::HasValidVM())
		return;

	VMManager::SetState(VMState::Stopping);
}

void ClearPendingHostCpuTasks()
{
	std::lock_guard lock(s_cpu_tasks_mutex);
	s_cpu_tasks.clear();
	s_cpu_thread_id = {};
}

std::string GetPerformanceMetricsSnapshot()
{
	std::lock_guard lock(s_metrics_mutex);
	return s_metrics_snapshot;
}

void SetPerformanceMetricsCallbackEnabled(bool enabled, bool detailed, bool gpu_timing)
{
	s_performance_metrics_enabled.store(enabled, std::memory_order_relaxed);
	s_performance_metrics_detailed.store(enabled && detailed, std::memory_order_relaxed);
	const bool request_gpu_timing = enabled && detailed && gpu_timing;
	s_performance_gpu_timing_requested.store(request_gpu_timing, std::memory_order_relaxed);
	if (MTGS::IsOpen())
	{
		Host::RunOnGSThread([request_gpu_timing]() {
			bool timing_active = false;
			std::string gpu_name;
			if (g_gs_device)
			{
				gpu_name = g_gs_device->GetName();
				if (request_gpu_timing)
					timing_active = g_gs_device->SetGPUTimingEnabled(true);
				else
					g_gs_device->SetGPUTimingEnabled(false);
			}
			GSConfig.OsdShowGPU = timing_active;
			emucorex::android::SetPerformanceGpuState(std::move(gpu_name), timing_active);
		});
	}
	if (!enabled)
	{
		std::lock_guard lock(s_metrics_mutex);
		s_metrics_snapshot.clear();
	}
}

bool IsPerformanceGpuTimingRequested()
{
	return s_performance_gpu_timing_requested.load(std::memory_order_relaxed);
}

void SetPerformanceGpuState(std::string name, bool timing_active)
{
	std::lock_guard lock(s_metrics_mutex);
	s_performance_gpu_name = std::move(name);
	s_performance_gpu_timing_active = timing_active;
}
}

void Host::CommitBaseSettingChanges()
{
	std::string username = Host::GetStringSettingValue("Achievements", "Username");
	std::string token = Host::GetStringSettingValue("Achievements", "Token");
	bool enabled = Host::GetBoolSettingValue("Achievements", "Enabled", false);
	bool hardcore = Host::GetBoolSettingValue("Achievements", "ChallengeMode", false);

	NotifySettingsChanged("Achievements", "Username", username.c_str());
	NotifySettingsChanged("Achievements", "Token", token.c_str());
	NotifySettingsChanged("Achievements", "Enabled", enabled ? "true" : "false");
	NotifySettingsChanged("Achievements", "ChallengeMode", hardcore ? "true" : "false");
}

void Host::LoadSettings(SettingsInterface&, std::unique_lock<std::mutex>&)
{
}

void Host::CheckForSettingsChanges(const Pcsx2Config&)
{
}

bool Host::RequestResetSettings(bool, bool, bool, bool, bool)
{
	return false;
}

void Host::SetDefaultUISettings(SettingsInterface&)
{
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
	return ProgressCallback::CreateNullProgressCallback();
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
	LogHostMessage(ANDROID_LOG_INFO, title, message);
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
	LogHostMessage(ANDROID_LOG_ERROR, title, message);
}

void Host::OpenURL(const std::string_view url)
{
	LogHostMessage(ANDROID_LOG_INFO, "OpenURL", url);
}

int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs)
{
	const size_t count = std::min(lhs.size(), rhs.size());
	const int result = std::char_traits<char>::compare(lhs.data(), rhs.data(), count);
	if (result != 0)
		return result;

	return (lhs.size() > rhs.size()) ? 1 : ((lhs.size() < rhs.size()) ? -1 : 0);
}

bool Host::InBatchMode()
{
	return false;
}

bool Host::InNoGUIMode()
{
	return false;
}

bool Host::CopyTextToClipboard(const std::string_view)
{
	return false;
}

void Host::BeginTextInput()
{
}

void Host::EndTextInput()
{
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
	return std::nullopt;
}

void Host::OnInputDeviceConnected(const std::string_view identifier, const std::string_view device_name)
{
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "input connected: %.*s (%.*s)",
		static_cast<int>(identifier.size()), identifier.data(),
		static_cast<int>(device_name.size()), device_name.data());
}

void Host::OnInputDeviceDisconnected(const InputBindingKey, const std::string_view identifier)
{
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "input disconnected: %.*s",
		static_cast<int>(identifier.size()), identifier.data());
}

void Host::SetMouseMode(bool, bool)
{
}

void Host::SetMouseLock(bool)
{
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool)
{
	void* window = nullptr;
	int width = 0;
	int height = 0;
	const bool has_surface = emucorex::android::AndroidRuntime::Instance().GetNativeSurface(&window, &width, &height);

	WindowInfo info;
	info.type = (has_surface && window) ? WindowInfo::Type::Android : WindowInfo::Type::Surfaceless;
	info.window_handle = window;
	info.surface_width = static_cast<u32>(std::max(width, 0));
	info.surface_height = static_cast<u32>(std::max(height, 0));
	info.surface_scale = (width > 0 && height > 0) ? (static_cast<float>(std::max(width, height)) / 800.0f) : 1.0f;
	info.surface_refresh_rate = 60.0f;
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "AcquireRenderWindow: type=%s window=%p size=%dx%d",
		info.type == WindowInfo::Type::Android ? "Android" : "Surfaceless", window, width, height);
	return info;
}

void Host::ReleaseRenderWindow()
{
}

void Host::BeginPresentFrame()
{
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "resize requested: %dx%d", width, height);
}

void Host::OnVMStarting()
{
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "VM starting");
}

void Host::OnVMStarted()
{
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "VM started");
}

void Host::OnVMDestroyed()
{
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "VM destroyed");
}

void Host::OnVMPaused()
{
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "VM paused");
}

void Host::OnVMResumed()
{
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "VM resumed");
}

void Host::OnGameChanged(const std::string& title, const std::string&, const std::string& disc_path,
	const std::string& disc_serial, u32 disc_crc, u32 current_crc)
{
	emucorex::android::RecordGameForCrashDiagnostics(title, disc_serial, disc_crc, current_crc);
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "game changed: title=%s path=%s serial=%s disc_crc=%08x current_crc=%08x",
		title.c_str(), disc_path.c_str(), disc_serial.c_str(), disc_crc, current_crc);
	QueryAndNotifyAchievementsState();
}

void Host::OnPerformanceMetricsUpdated()
{
	if (!s_performance_metrics_enabled.load(std::memory_order_relaxed))
		return;

	const auto now = std::chrono::steady_clock::now();
	if (now - s_last_metrics_dispatch < std::chrono::seconds(1))
		return;
	s_last_metrics_dispatch = now;

	const float vps = PerformanceMetrics::GetFPS();
	const float speed = PerformanceMetrics::GetSpeed();
	const float internal_fps = PerformanceMetrics::GetInternalFPS();
	const bool internal_fps_valid = PerformanceMetrics::IsInternalFPSValid();
	const float display_fps = internal_fps_valid ? internal_fps : vps;

	std::string overlay;
	std::string line;
	if (internal_fps_valid)
		AppendFormat(line, "FPS: %.2f%s | VPS: %.2f", display_fps, GetInternalFpsMethodSuffix(), vps);
	else
		AppendFormat(line, "FPS: N/A | VPS: %.2f", vps);
	AppendLine(overlay, line);

	line.clear();
	AppendFormat(line, "Speed: %.0f%%", std::round(speed));
	const float target_speed = VMManager::GetTargetSpeed();
	if (target_speed == 0.0f)
		line.append(" | Target: Max");
	else
		AppendFormat(line, " | Target: %.0f%%", target_speed * 100.0f);
	AppendLine(overlay, line);

	if (s_performance_metrics_detailed.load(std::memory_order_relaxed))
	{
		SmallString gs_stats_line;
		SmallString gs_memory_stats_line;
		GSgetStats(gs_stats_line);
		GSgetMemoryStats(gs_memory_stats_line);

		std::string renderer = GetFirstStatsSegment(gs_stats_line);
		std::string memory = GetFirstStatsSegment(gs_memory_stats_line);
		if (!renderer.empty())
			AppendLine(overlay, renderer);
		if (!memory.empty())
			AppendLine(overlay, memory);

		line.clear();
		AppendFormat(line, "Frame: %.2f / %.2f / %.2f ms",
			PerformanceMetrics::GetMinimumFrameTime(),
			PerformanceMetrics::GetAverageFrameTime(),
			PerformanceMetrics::GetMaximumFrameTime());
		AppendLine(overlay, line);

		line.clear();
		AppendFormat(line, "Queue: %d", std::max(MTGS::GetCurrentVsyncQueueSize() - 1, 0));
		AppendLine(overlay, line);

		int internal_width = 0;
		int internal_height = 0;
		GSgetInternalResolution(&internal_width, &internal_height);
		if (internal_width > 0 && internal_height > 0)
		{
			line.clear();
			AppendFormat(line, "Res: %dx%d %s %s", internal_width, internal_height, ReportVideoMode(), ReportInterlaceMode());
			AppendLine(overlay, line);
		}

		const CPUInfo& cpu_info = GetCPUInfo();
		double cpu_usage = PerformanceMetrics::GetCPUThreadUsage() + PerformanceMetrics::GetGSThreadUsage() +
			PerformanceMetrics::GetVUThreadUsage();
		const u32 gs_sw_threads = PerformanceMetrics::GetGSSWThreadCount();
		for (u32 thread = 0; thread < gs_sw_threads; thread++)
			cpu_usage += PerformanceMetrics::GetGSSWThreadUsage(thread);
		const double normalized_cpu_usage = std::clamp(
			cpu_usage / static_cast<double>(std::max(cpu_info.num_threads, 1u)), 0.0, 100.0);
		line.clear();
		line.append("CPU:");
		line.append(GetHostCpuName(cpu_info));
		AppendFormat(line, " | %.1f%%", normalized_cpu_usage);
		AppendLine(overlay, line);

		std::string gpu_name;
		bool gpu_timing_active = false;
		{
			std::lock_guard lock(s_metrics_mutex);
			gpu_name = s_performance_gpu_name;
			gpu_timing_active = s_performance_gpu_timing_active;
		}
		line.clear();
		line.append("GPU:");
		line.append(gpu_name.empty() ? "Unknown" : GetCompactGpuName(std::move(gpu_name)));
		if (gpu_timing_active)
			AppendFormat(line, " | %.1f%% (%.2fms)", PerformanceMetrics::GetGPUUsage(), PerformanceMetrics::GetGPUAverageTime());
		else
			line.append(" | N/A");
		AppendLine(overlay, line);

		line.clear();
		AppendProcessorStat(line, "EE: ", PerformanceMetrics::GetCPUThreadUsage(), PerformanceMetrics::GetCPUThreadAverageTime());
		AppendLine(overlay, line);

		line.clear();
		AppendProcessorStat(line, "GS: ", PerformanceMetrics::GetGSThreadUsage(), PerformanceMetrics::GetGSThreadAverageTime());
		AppendLine(overlay, line);

		line.clear();
		AppendProcessorStat(line, "VU: ", PerformanceMetrics::GetVUThreadUsage(), PerformanceMetrics::GetVUThreadAverageTime());
		AppendLine(overlay, line);

		for (u32 thread = 0; thread < gs_sw_threads; thread++)
		{
			line.clear();
			AppendFormat(line, "SW-%u: ", thread);
			AppendProcessorStat(line, "", PerformanceMetrics::GetGSSWThreadUsage(thread), PerformanceMetrics::GetGSSWThreadAverageTime(thread));
			AppendLine(overlay, line);
		}

		line.clear();
		line.append("Audio:");
		switch (SPU2::GetOutputBackend())
		{
			case AudioBackend::SDL:
				line.append("AAudio");
				break;
			case AudioBackend::OpenSLES:
				line.append("OpenSL ES");
				break;
			case AudioBackend::Null:
				line.append("Null");
				break;
			default:
				line.append("Unknown");
				break;
		}
		AppendLine(overlay, line);
	}

	std::string snapshot;
	AppendFormat(snapshot, "%.3f\n%.3f\n", display_fps, speed);
	snapshot.append(overlay);
	{
		std::lock_guard lock(s_metrics_mutex);
		s_metrics_snapshot = std::move(snapshot);
	}
}

void Host::OnSaveStateLoading(const std::string_view filename)
{
	LogHostMessage(ANDROID_LOG_INFO, "save state loading", filename);
}

void Host::OnSaveStateLoaded(const std::string_view filename, bool was_successful)
{
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "save state loaded: %.*s success=%s",
		static_cast<int>(filename.size()), filename.data(), was_successful ? "true" : "false");
}

void Host::OnSaveStateSaved(const std::string_view filename)
{
	LogHostMessage(ANDROID_LOG_INFO, "save state saved", filename);
}

void Host::RunOnCPUThread(std::function<void()> function, bool block)
{
	if (!function)
		return;

	if (std::this_thread::get_id() == s_cpu_thread_id)
	{
		function();
		return;
	}

	if (!block)
	{
		std::lock_guard lock(s_cpu_tasks_mutex);
		s_cpu_tasks.push_back(std::move(function));
		return;
	}

	struct Completion
	{
		std::mutex mutex;
		std::condition_variable cv;
		bool done = false;
	};

	auto completion = std::make_shared<Completion>();
	auto task = std::make_shared<std::function<void()>>(std::move(function));
	{
		std::lock_guard lock(s_cpu_tasks_mutex);
		s_cpu_tasks.push_back([completion, task]() {
			(*task)();
			{
				std::lock_guard done_lock(completion->mutex);
				completion->done = true;
			}
			completion->cv.notify_one();
		});
	}

	std::unique_lock done_lock(completion->mutex);
	if (!completion->cv.wait_for(done_lock, std::chrono::seconds(10), [&]() { return completion->done; }))
		__android_log_write(ANDROID_LOG_ERROR, LOG_TAG, "Timed out waiting for CPU thread task");
}

void Host::RunOnGSThread(std::function<void()> function)
{
	if (!function)
		return;

	Host::RunOnCPUThread([fn = std::move(function)]() mutable {
		// MTGS has a single-producer ring buffer. Android callbacks can arrive on
		// Binder, UI, HTTP, and VM threads, so every GS command must first be
		// serialized through the CPU thread. The GS may have closed while this
		// task was waiting in the CPU queue; in that case dropping the callback is
		// safer than writing into a stopped ring buffer.
		if (MTGS::IsOpen())
			MTGS::RunOnGSThread(std::move(fn));
	}, false);
}

void Host::RefreshGameListAsync(bool)
{
}

void Host::CancelGameListRefresh()
{
}

bool Host::IsFullscreen()
{
	return false;
}

void Host::SetFullscreen(bool)
{
}

void Host::OnCaptureStarted(const std::string& filename)
{
	LogHostMessage(ANDROID_LOG_INFO, "capture started", filename);
}

void Host::OnCaptureStopped()
{
	__android_log_write(ANDROID_LOG_INFO, LOG_TAG, "capture stopped");
}

void Host::RequestExitApplication(bool)
{
	emucorex::android::AndroidRuntime::Instance().Shutdown();
}

void Host::RequestExitBigPicture()
{
}

void Host::RequestVMShutdown(bool, bool, bool)
{
	emucorex::android::RequestCurrentVmStop();
}

void Host::PumpMessagesOnCPUThread()
{
	s_cpu_thread_id = std::this_thread::get_id();

	std::deque<std::function<void()>> tasks;
	{
		std::lock_guard lock(s_cpu_tasks_mutex);
		tasks.swap(s_cpu_tasks);
	}

	for (std::function<void()>& task : tasks)
	{
		if (task)
			task();
	}
}

s32 Host::Internal::GetTranslatedStringImpl(
	const std::string_view, const std::string_view msg, char* tbuf, size_t tbuf_space)
{
	if (msg.size() > tbuf_space)
		return -1;

	std::memcpy(tbuf, msg.data(), msg.size());
	return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char*, const char* msg, const char*, int count)
{
	std::string ret(msg);
	const std::string count_string = std::to_string(count);
	for (;;)
	{
		const std::string::size_type pos = ret.find("%n");
		if (pos == std::string::npos)
			break;
		ret.replace(pos, 2, count_string);
	}
	return ret;
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
	NotifyLoginRequested(static_cast<int>(reason));
}

void Host::OnAchievementsLoginSuccess(const char* display_name, u32 points, u32 sc_points, u32 unread_messages)
{
	NotifyLoginSuccess(display_name, points, sc_points, unread_messages);
	QueryAndNotifyAchievementsState();
}

void Host::OnAchievementsRefreshed()
{
	QueryAndNotifyAchievementsState();
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
	NotifyHardcoreModeChanged(enabled);
	QueryAndNotifyAchievementsState();
}

void Host::OnCoverDownloaderOpenRequested()
{
}

void Host::OnCreateMemoryCardOpenRequested()
{
}

bool Host::LocaleCircleConfirm()
{
	return false;
}

bool Host::ShouldPreferHostFileSelector()
{
	return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view, bool, FileSelectorCallback callback, FileSelectorFilters, std::string_view)
{
	callback({});
}
