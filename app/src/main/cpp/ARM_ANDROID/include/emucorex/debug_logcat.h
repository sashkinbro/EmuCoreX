#pragma once

#ifdef __ANDROID__

#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace emucorex
{
	using u32 = uint32_t;
	using u64 = uint64_t;

	inline std::atomic<bool> s_debug_logcat_enabled{false};

	inline bool IsDebugLogcatEnabled()
	{
		return s_debug_logcat_enabled.load(std::memory_order_relaxed);
	}

	inline void SetDebugLogcatEnabled(bool enabled)
	{
		s_debug_logcat_enabled.store(enabled, std::memory_order_relaxed);
	}

	// Performance metrics collection for GS/VU1 optimization
	struct GSDebugMetrics
	{
		// XGKICK latency tracking (in microseconds)
		std::atomic<u64> xgkick_total_us{0};
		std::atomic<u64> xgkick_count{0};
		std::atomic<u64> xgkick_max_us{0};

		// Ring buffer usage (in SIMD128 entries, max 524288 = 8MB)
		std::atomic<u32> ring_buffer_max_used{0};
		std::atomic<u32> ring_buffer_stall_count{0};
		std::atomic<u64> ring_buffer_stall_total_us{0};
		std::atomic<u64> ring_buffer_stall_max_us{0};

		// SemaXGkick wait time (microseconds GS thread waits for VU1)
		std::atomic<u64> sema_xgkick_wait_total_us{0};
		std::atomic<u64> sema_xgkick_wait_count{0};
		std::atomic<u64> sema_xgkick_wait_max_us{0};

		// GIF path utilization (bytes used / total buffer)
		std::atomic<u32> gif_path1_max_used{0};
		std::atomic<u32> gif_path2_max_used{0};
		std::atomic<u32> gif_path3_max_used{0};

		// VU1 execution time tracking (microseconds)
		std::atomic<u64> vu1_exec_total_us{0};
		std::atomic<u64> vu1_exec_count{0};
		std::atomic<u64> vu1_exec_max_us{0};

		// Frame pacing
		std::atomic<u32> vsync_queue_depth_max{0};
		std::atomic<u64> frame_total_us{0};
		std::atomic<u64> frame_count{0};

		// WaitGS metrics
		std::atomic<u64> wait_gs_total_us{0};
		std::atomic<u64> wait_gs_count{0};
		std::atomic<u64> wait_gs_max_us{0};

		// Weak wait spin iterations
		std::atomic<u64> weak_wait_spin_total{0};
		std::atomic<u64> weak_wait_spin_count{0};

		// GS Transfer timing (microseconds)
		std::atomic<u64> gs_transfer_total_us{0};
		std::atomic<u64> gs_transfer_count{0};
		std::atomic<u64> gs_transfer_max_us{0};

		// GS Image Transfer timing (VRAM writes)
		std::atomic<u64> gs_image_transfer_total_us{0};
		std::atomic<u64> gs_image_transfer_count{0};
		std::atomic<u64> gs_image_transfer_max_us{0};

		// GS VSync timing (Flush + Present)
		std::atomic<u64> gs_vsync_total_us{0};
		std::atomic<u64> gs_vsync_count{0};
		std::atomic<u64> gs_vsync_max_us{0};

		void Reset()
		{
			xgkick_total_us.store(0, std::memory_order_relaxed);
			xgkick_count.store(0, std::memory_order_relaxed);
			xgkick_max_us.store(0, std::memory_order_relaxed);
			ring_buffer_max_used.store(0, std::memory_order_relaxed);
			ring_buffer_stall_count.store(0, std::memory_order_relaxed);
			ring_buffer_stall_total_us.store(0, std::memory_order_relaxed);
			ring_buffer_stall_max_us.store(0, std::memory_order_relaxed);
			sema_xgkick_wait_total_us.store(0, std::memory_order_relaxed);
			sema_xgkick_wait_count.store(0, std::memory_order_relaxed);
			sema_xgkick_wait_max_us.store(0, std::memory_order_relaxed);
			gif_path1_max_used.store(0, std::memory_order_relaxed);
			gif_path2_max_used.store(0, std::memory_order_relaxed);
			gif_path3_max_used.store(0, std::memory_order_relaxed);
			vu1_exec_total_us.store(0, std::memory_order_relaxed);
			vu1_exec_count.store(0, std::memory_order_relaxed);
			vu1_exec_max_us.store(0, std::memory_order_relaxed);
			vsync_queue_depth_max.store(0, std::memory_order_relaxed);
			frame_total_us.store(0, std::memory_order_relaxed);
			frame_count.store(0, std::memory_order_relaxed);
			wait_gs_total_us.store(0, std::memory_order_relaxed);
			wait_gs_count.store(0, std::memory_order_relaxed);
			wait_gs_max_us.store(0, std::memory_order_relaxed);
			weak_wait_spin_total.store(0, std::memory_order_relaxed);
			weak_wait_spin_count.store(0, std::memory_order_relaxed);
			gs_transfer_total_us.store(0, std::memory_order_relaxed);
			gs_transfer_count.store(0, std::memory_order_relaxed);
			gs_transfer_max_us.store(0, std::memory_order_relaxed);
			gs_image_transfer_total_us.store(0, std::memory_order_relaxed);
			gs_image_transfer_count.store(0, std::memory_order_relaxed);
			gs_image_transfer_max_us.store(0, std::memory_order_relaxed);
			gs_vsync_total_us.store(0, std::memory_order_relaxed);
			gs_vsync_count.store(0, std::memory_order_relaxed);
			gs_vsync_max_us.store(0, std::memory_order_relaxed);
		}

		void DumpToLogcat()
		{
			const u64 xc = xgkick_count.load(std::memory_order_relaxed);
			const u64 sc = sema_xgkick_wait_count.load(std::memory_order_relaxed);
			const u64 vc = vu1_exec_count.load(std::memory_order_relaxed);
			const u64 fc = frame_count.load(std::memory_order_relaxed);
			const u64 wgc = wait_gs_count.load(std::memory_order_relaxed);
			const u64 gtc = gs_transfer_count.load(std::memory_order_relaxed);
			const u64 gitc = gs_image_transfer_count.load(std::memory_order_relaxed);
			const u64 gvc = gs_vsync_count.load(std::memory_order_relaxed);

			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"=== GS Performance Metrics ===");
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"XGKICK: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)xc,
				(unsigned long long)(xc ? xgkick_total_us.load(std::memory_order_relaxed) / xc : 0),
				(unsigned long long)xgkick_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"SemaXGkickWait: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)sc,
				(unsigned long long)(sc ? sema_xgkick_wait_total_us.load(std::memory_order_relaxed) / sc : 0),
				(unsigned long long)sema_xgkick_wait_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"RingBuffer: max_used=%u stalls=%llu stall_time=%lluus max_stall=%lluus",
				(unsigned)ring_buffer_max_used.load(std::memory_order_relaxed),
				(unsigned long long)ring_buffer_stall_count.load(std::memory_order_relaxed),
				(unsigned long long)ring_buffer_stall_total_us.load(std::memory_order_relaxed),
				(unsigned long long)ring_buffer_stall_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"GIF Path Max Used: P1=%u P2=%u P3=%u (of 9MB each)",
				(unsigned)gif_path1_max_used.load(std::memory_order_relaxed),
				(unsigned)gif_path2_max_used.load(std::memory_order_relaxed),
				(unsigned)gif_path3_max_used.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"VU1: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)vc,
				(unsigned long long)(vc ? vu1_exec_total_us.load(std::memory_order_relaxed) / vc : 0),
				(unsigned long long)vu1_exec_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"WaitGS: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)wgc,
				(unsigned long long)(wgc ? wait_gs_total_us.load(std::memory_order_relaxed) / wgc : 0),
				(unsigned long long)wait_gs_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"VSyncQueue: max_depth=%u avg_frame=%lluus",
				(unsigned)vsync_queue_depth_max.load(std::memory_order_relaxed),
				(unsigned long long)(fc ? frame_total_us.load(std::memory_order_relaxed) / fc : 0));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"GSTransfer: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)gtc,
				(unsigned long long)(gtc ? gs_transfer_total_us.load(std::memory_order_relaxed) / gtc : 0),
				(unsigned long long)gs_transfer_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"GSImageXfer: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)gitc,
				(unsigned long long)(gitc ? gs_image_transfer_total_us.load(std::memory_order_relaxed) / gitc : 0),
				(unsigned long long)gs_image_transfer_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"GSVSync: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)gvc,
				(unsigned long long)(gvc ? gs_vsync_total_us.load(std::memory_order_relaxed) / gvc : 0),
				(unsigned long long)gs_vsync_max_us.load(std::memory_order_relaxed));

			// Reset after dump
			Reset();
		}
	};

	inline GSDebugMetrics s_gs_debug_metrics;
}

#define DEBUG_GS_LOG(level, ...) \
	do { \
		if (::emucorex::IsDebugLogcatEnabled()) \
			__android_log_print(level, "DebugGS", __VA_ARGS__); \
	} while(0)

// Performance metric helpers - only active when debug logcat is enabled
#define DEBUG_GS_TIMING_START(var) \
	uint64_t _dbg_##var##_start = 0; \
	do { \
		if (::emucorex::IsDebugLogcatEnabled()) \
			_dbg_##var##_start = std::chrono::duration_cast<std::chrono::microseconds>( \
				std::chrono::steady_clock::now().time_since_epoch()).count(); \
	} while(0)

#define DEBUG_GS_TIMING_END(var, counter) \
	do { \
		if (::emucorex::IsDebugLogcatEnabled()) { \
			uint64_t _end = std::chrono::duration_cast<std::chrono::microseconds>( \
				std::chrono::steady_clock::now().time_since_epoch()).count(); \
			uint64_t _elapsed = _end - _dbg_##var##_start; \
			::emucorex::s_gs_debug_metrics.counter##_total_time_us.fetch_add(_elapsed, std::memory_order_relaxed); \
			::emucorex::s_gs_debug_metrics.counter##_count.fetch_add(1, std::memory_order_relaxed); \
			uint64_t _prev_max = ::emucorex::s_gs_debug_metrics.counter##_max_time_us.load(std::memory_order_relaxed); \
			while (_elapsed > _prev_max && \
				!::emucorex::s_gs_debug_metrics.counter##_max_time_us.compare_exchange_weak( \
					_prev_max, _elapsed, std::memory_order_relaxed)) \
				; \
		} \
	} while(0)

#define DEBUG_GS_TIMING_END_U64(var, counter) \
	do { \
		if (::emucorex::IsDebugLogcatEnabled()) { \
			uint64_t _end = std::chrono::duration_cast<std::chrono::microseconds>( \
				std::chrono::steady_clock::now().time_since_epoch()).count(); \
			uint64_t _elapsed = _end - _dbg_##var##_start; \
			::emucorex::s_gs_debug_metrics.counter##_total_us.fetch_add(_elapsed, std::memory_order_relaxed); \
			::emucorex::s_gs_debug_metrics.counter##_count.fetch_add(1, std::memory_order_relaxed); \
			uint64_t _prev_max = ::emucorex::s_gs_debug_metrics.counter##_max_us.load(std::memory_order_relaxed); \
			while (_elapsed > _prev_max && \
				!::emucorex::s_gs_debug_metrics.counter##_max_us.compare_exchange_weak( \
					_prev_max, _elapsed, std::memory_order_relaxed)) \
				; \
		} \
	} while(0)

#define DEBUG_GS_SET_MAX(counter, val) \
	do { \
		if (::emucorex::IsDebugLogcatEnabled()) { \
			uint32_t _v = static_cast<uint32_t>(val); \
			uint32_t _prev = ::emucorex::s_gs_debug_metrics.counter.load(std::memory_order_relaxed); \
			while (_v > _prev && \
				!::emucorex::s_gs_debug_metrics.counter.compare_exchange_weak( \
					_prev, _v, std::memory_order_relaxed)) \
				; \
		} \
	} while(0)

#define DEBUG_GS_INC_U64(counter, val) \
	do { \
		if (::emucorex::IsDebugLogcatEnabled()) \
			::emucorex::s_gs_debug_metrics.counter.fetch_add(static_cast<uint64_t>(val), std::memory_order_relaxed); \
	} while(0)

#define DEBUG_GS_DUMP_METRICS() \
	do { \
		if (::emucorex::IsDebugLogcatEnabled()) \
			::emucorex::s_gs_debug_metrics.DumpToLogcat(); \
	} while(0)

#else
#define DEBUG_GS_LOG(level, ...) do {} while(0)
#define DEBUG_GS_TIMING_START(var) do {} while(0)
#define DEBUG_GS_TIMING_END(var, counter) do {} while(0)
#define DEBUG_GS_TIMING_END_U64(var, counter) do {} while(0)
#define DEBUG_GS_SET_MAX(counter, val) do {} while(0)
#define DEBUG_GS_INC_U64(counter, val) do {} while(0)
#define DEBUG_GS_DUMP_METRICS() do {} while(0)
#endif
