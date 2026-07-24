#pragma once

#ifdef __ANDROID__

#include <android/log.h>
#include <atomic>

namespace emucorex
{
	inline std::atomic<bool> s_debug_logcat_enabled{false};

	inline bool IsDebugLogcatEnabled()
	{
		return s_debug_logcat_enabled.load(std::memory_order_relaxed);
	}

	inline void SetDebugLogcatEnabled(bool enabled)
	{
		s_debug_logcat_enabled.store(enabled, std::memory_order_relaxed);
	}
}

#define DEBUG_GS_LOG(level, ...) \
	do { \
		if (::emucorex::IsDebugLogcatEnabled()) \
			__android_log_print(level, "DebugGS", __VA_ARGS__); \
	} while(0)

#else
#define DEBUG_GS_LOG(level, ...) do {} while(0)
#endif
