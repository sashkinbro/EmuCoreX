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

#if defined(NDEBUG) && !defined(PCSX2_DEVBUILD)
	// Diagnostic collection is intentionally unavailable in production builds.
	// Keeping these compile-time constants in the public API also eliminates direct
	// hot-path checks which do not go through the macros below.
	inline constexpr bool IsDebugLogcatEnabled() { return false; }
	inline constexpr void SetDebugLogcatEnabled(bool) {}
	inline constexpr bool IsProfilerLogcatEnabled() { return false; }
	inline constexpr void SetProfilerLogcatEnabled(bool) {}
#else
	inline std::atomic<bool> s_debug_logcat_enabled{false};
	inline std::atomic<bool> s_profiler_logcat_enabled{false};

	inline bool IsDebugLogcatEnabled()
	{
		return s_debug_logcat_enabled.load(std::memory_order_relaxed);
	}

	inline void SetDebugLogcatEnabled(bool enabled)
	{
		s_debug_logcat_enabled.store(enabled, std::memory_order_relaxed);
	}

	inline bool IsProfilerLogcatEnabled()
	{
		return s_profiler_logcat_enabled.load(std::memory_order_relaxed);
	}

	inline void SetProfilerLogcatEnabled(bool enabled)
	{
		s_profiler_logcat_enabled.store(enabled, std::memory_order_relaxed);
	}
#endif

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
		std::atomic<u32> gif_path1_total{0};
		std::atomic<u32> gif_path2_max_used{0};
		std::atomic<u32> gif_path2_total{0};
		std::atomic<u32> gif_path3_max_used{0};
		std::atomic<u32> gif_path3_total{0};

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

		// GPU fence wait timing
		std::atomic<u64> gpu_fence_wait_total_us{0};
		std::atomic<u64> gpu_fence_wait_count{0};
		std::atomic<u64> gpu_fence_wait_max_us{0};

		// Vulkan present timing
		std::atomic<u64> vk_present_total_us{0};
		std::atomic<u64> vk_present_count{0};
		std::atomic<u64> vk_present_max_us{0};

		// Frame throttle timing
		std::atomic<u64> frame_throttle_total_us{0};
		std::atomic<u64> frame_throttle_count{0};
		std::atomic<u64> frame_throttle_max_us{0};

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
			gif_path1_total.store(0, std::memory_order_relaxed);
			gif_path2_max_used.store(0, std::memory_order_relaxed);
			gif_path2_total.store(0, std::memory_order_relaxed);
			gif_path3_max_used.store(0, std::memory_order_relaxed);
			gif_path3_total.store(0, std::memory_order_relaxed);
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
			gpu_fence_wait_total_us.store(0, std::memory_order_relaxed);
			gpu_fence_wait_count.store(0, std::memory_order_relaxed);
			gpu_fence_wait_max_us.store(0, std::memory_order_relaxed);
			vk_present_total_us.store(0, std::memory_order_relaxed);
			vk_present_count.store(0, std::memory_order_relaxed);
			vk_present_max_us.store(0, std::memory_order_relaxed);
			frame_throttle_total_us.store(0, std::memory_order_relaxed);
			frame_throttle_count.store(0, std::memory_order_relaxed);
			frame_throttle_max_us.store(0, std::memory_order_relaxed);
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
			const u64 gfc = gpu_fence_wait_count.load(std::memory_order_relaxed);
			const u64 vpc = vk_present_count.load(std::memory_order_relaxed);
			const u64 ftc = frame_throttle_count.load(std::memory_order_relaxed);

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
				"GIF Path Max Used: P1=%u/%u (%u%%) P2=%u/%u (%u%%)",
				(unsigned)gif_path1_max_used.load(std::memory_order_relaxed),
				(unsigned)gif_path1_total.load(std::memory_order_relaxed),
				(unsigned)(gif_path1_total.load(std::memory_order_relaxed) ? (gif_path1_max_used.load(std::memory_order_relaxed) * 100 / gif_path1_total.load(std::memory_order_relaxed)) : 0),
				(unsigned)gif_path2_max_used.load(std::memory_order_relaxed),
				(unsigned)gif_path2_total.load(std::memory_order_relaxed),
				(unsigned)(gif_path2_total.load(std::memory_order_relaxed) ? (gif_path2_max_used.load(std::memory_order_relaxed) * 100 / gif_path2_total.load(std::memory_order_relaxed)) : 0));
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
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"GPUFence: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)gfc,
				(unsigned long long)(gfc ? gpu_fence_wait_total_us.load(std::memory_order_relaxed) / gfc : 0),
				(unsigned long long)gpu_fence_wait_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"VkPresent: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)vpc,
				(unsigned long long)(vpc ? vk_present_total_us.load(std::memory_order_relaxed) / vpc : 0),
				(unsigned long long)vk_present_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "DebugGS",
				"Throttle: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)ftc,
				(unsigned long long)(ftc ? frame_throttle_total_us.load(std::memory_order_relaxed) / ftc : 0),
				(unsigned long long)frame_throttle_max_us.load(std::memory_order_relaxed));

			// Reset after dump
			Reset();
		}
	};

	inline GSDebugMetrics s_gs_debug_metrics;

	// Profiler metrics for EE/IOP/VU0/VU1
	struct ProfilerMetrics
	{
		// EE event test shared
		std::atomic<u64> ee_event_test_total_us{0};
		std::atomic<u64> ee_event_test_count{0};
		std::atomic<u64> ee_event_test_max_us{0};

		// IOP execution block
		std::atomic<u64> iop_exec_total_us{0};
		std::atomic<u64> iop_exec_count{0};
		std::atomic<u64> iop_exec_max_us{0};

		// DMA interrupt processing
		std::atomic<u64> dma_interrupt_total_us{0};
		std::atomic<u64> dma_interrupt_count{0};
		std::atomic<u64> dma_interrupt_max_us{0};

		// VU0 sync
		std::atomic<u64> vu0_sync_total_us{0};
		std::atomic<u64> vu0_sync_count{0};
		std::atomic<u64> vu0_sync_max_us{0};

		// VU1 sync (ExecuteBlock from event test, different from MTVU Execute)
		std::atomic<u64> vu1_sync_total_us{0};
		std::atomic<u64> vu1_sync_count{0};
		std::atomic<u64> vu1_sync_max_us{0};

		// Counter/timer updates
		std::atomic<u64> rcnt_update_total_us{0};
		std::atomic<u64> rcnt_update_count{0};
		std::atomic<u64> rcnt_update_max_us{0};

		// IOP event test
		std::atomic<u64> iop_event_test_total_us{0};
		std::atomic<u64> iop_event_test_count{0};
		std::atomic<u64> iop_event_test_max_us{0};

		// EE JIT execution (time between event tests)
		std::atomic<u64> ee_jit_exec_total_us{0};
		std::atomic<u64> ee_jit_exec_count{0};
		std::atomic<u64> ee_jit_exec_max_us{0};

		// Per-frame tracking
		std::atomic<uint64_t> last_vsync_start_time_us{0};
		std::atomic<uint64_t> frame_delta_max_us{0};
		std::atomic<uint64_t> frame_delta_total_us{0};
		std::atomic<uint64_t> frame_delta_count{0};

		// CPU-side VSync path breakdown
		std::atomic<u64> vsync_start_total_us{0};
		std::atomic<u64> vsync_start_count{0};
		std::atomic<u64> vsync_start_max_us{0};
		std::atomic<u64> vsync_cpu_total_us{0};
		std::atomic<u64> vsync_cpu_count{0};
		std::atomic<u64> vsync_cpu_max_us{0};
		std::atomic<u64> vsync_throttle_total_us{0};
		std::atomic<u64> vsync_throttle_count{0};
		std::atomic<u64> vsync_throttle_max_us{0};
		std::atomic<u64> frame_limiter_total_us{0};
		std::atomic<u64> frame_limiter_count{0};
		std::atomic<u64> frame_limiter_max_us{0};
		std::atomic<u64> vsync_gs_post_total_us{0};
		std::atomic<u64> vsync_gs_post_count{0};
		std::atomic<u64> vsync_gs_post_max_us{0};
		std::atomic<u64> vsync_input_total_us{0};
		std::atomic<u64> vsync_input_count{0};
		std::atomic<u64> vsync_input_max_us{0};
		std::atomic<u64> vsync_irq_total_us{0};
		std::atomic<u64> vsync_irq_count{0};
		std::atomic<u64> vsync_irq_max_us{0};
		std::atomic<u64> rcnt_core_total_us{0};
		std::atomic<u64> rcnt_core_count{0};
		std::atomic<u64> rcnt_core_max_us{0};
		std::atomic<u64> mtgs_vsync_wait_total_us{0};
		std::atomic<u64> mtgs_vsync_wait_count{0};
		std::atomic<u64> mtgs_vsync_wait_max_us{0};
		std::atomic<u64> mtgs_ring_stall_total_us{0};
		std::atomic<u64> mtgs_ring_stall_count{0};
		std::atomic<u64> mtgs_ring_stall_max_us{0};
		std::atomic<u64> mtgs_mtvu_packet_total_us{0};
		std::atomic<u64> mtgs_mtvu_packet_count{0};
		std::atomic<u64> mtgs_mtvu_packet_max_us{0};
		std::atomic<u64> mtgs_mtvu_wait_total_us{0};
		std::atomic<u64> mtgs_mtvu_wait_count{0};
		std::atomic<u64> mtgs_mtvu_wait_max_us{0};
		std::atomic<u64> mtgs_transfer_total_us{0};
		std::atomic<u64> mtgs_transfer_count{0};
		std::atomic<u64> mtgs_transfer_max_us{0};
		std::atomic<u64> mtgs_vsync_process_total_us{0};
		std::atomic<u64> mtgs_vsync_process_count{0};
		std::atomic<u64> mtgs_vsync_process_max_us{0};
		std::atomic<u64> gs_flush_total_us{0};
		std::atomic<u64> gs_flush_count{0};
		std::atomic<u64> gs_flush_max_us{0};
		std::atomic<u64> gs_image_total_us{0};
		std::atomic<u64> gs_image_count{0};
		std::atomic<u64> gs_image_max_us{0};

		void Reset()
		{
			ee_event_test_total_us.store(0, std::memory_order_relaxed);
			ee_event_test_count.store(0, std::memory_order_relaxed);
			ee_event_test_max_us.store(0, std::memory_order_relaxed);
			iop_exec_total_us.store(0, std::memory_order_relaxed);
			iop_exec_count.store(0, std::memory_order_relaxed);
			iop_exec_max_us.store(0, std::memory_order_relaxed);
			dma_interrupt_total_us.store(0, std::memory_order_relaxed);
			dma_interrupt_count.store(0, std::memory_order_relaxed);
			dma_interrupt_max_us.store(0, std::memory_order_relaxed);
			vu0_sync_total_us.store(0, std::memory_order_relaxed);
			vu0_sync_count.store(0, std::memory_order_relaxed);
			vu0_sync_max_us.store(0, std::memory_order_relaxed);
			vu1_sync_total_us.store(0, std::memory_order_relaxed);
			vu1_sync_count.store(0, std::memory_order_relaxed);
			vu1_sync_max_us.store(0, std::memory_order_relaxed);
			rcnt_update_total_us.store(0, std::memory_order_relaxed);
			rcnt_update_count.store(0, std::memory_order_relaxed);
			rcnt_update_max_us.store(0, std::memory_order_relaxed);
			iop_event_test_total_us.store(0, std::memory_order_relaxed);
			iop_event_test_count.store(0, std::memory_order_relaxed);
			iop_event_test_max_us.store(0, std::memory_order_relaxed);
			ee_jit_exec_total_us.store(0, std::memory_order_relaxed);
			ee_jit_exec_count.store(0, std::memory_order_relaxed);
			ee_jit_exec_max_us.store(0, std::memory_order_relaxed);
			last_vsync_start_time_us.store(0, std::memory_order_relaxed);
			frame_delta_max_us.store(0, std::memory_order_relaxed);
			frame_delta_total_us.store(0, std::memory_order_relaxed);
			frame_delta_count.store(0, std::memory_order_relaxed);
			vsync_start_total_us.store(0, std::memory_order_relaxed);
			vsync_start_count.store(0, std::memory_order_relaxed);
			vsync_start_max_us.store(0, std::memory_order_relaxed);
			vsync_cpu_total_us.store(0, std::memory_order_relaxed);
			vsync_cpu_count.store(0, std::memory_order_relaxed);
			vsync_cpu_max_us.store(0, std::memory_order_relaxed);
			vsync_throttle_total_us.store(0, std::memory_order_relaxed);
			vsync_throttle_count.store(0, std::memory_order_relaxed);
			vsync_throttle_max_us.store(0, std::memory_order_relaxed);
			frame_limiter_total_us.store(0, std::memory_order_relaxed);
			frame_limiter_count.store(0, std::memory_order_relaxed);
			frame_limiter_max_us.store(0, std::memory_order_relaxed);
			vsync_gs_post_total_us.store(0, std::memory_order_relaxed);
			vsync_gs_post_count.store(0, std::memory_order_relaxed);
			vsync_gs_post_max_us.store(0, std::memory_order_relaxed);
			vsync_input_total_us.store(0, std::memory_order_relaxed);
			vsync_input_count.store(0, std::memory_order_relaxed);
			vsync_input_max_us.store(0, std::memory_order_relaxed);
			vsync_irq_total_us.store(0, std::memory_order_relaxed);
			vsync_irq_count.store(0, std::memory_order_relaxed);
			vsync_irq_max_us.store(0, std::memory_order_relaxed);
			rcnt_core_total_us.store(0, std::memory_order_relaxed);
			rcnt_core_count.store(0, std::memory_order_relaxed);
			rcnt_core_max_us.store(0, std::memory_order_relaxed);
			mtgs_vsync_wait_total_us.store(0, std::memory_order_relaxed);
			mtgs_vsync_wait_count.store(0, std::memory_order_relaxed);
			mtgs_vsync_wait_max_us.store(0, std::memory_order_relaxed);
			mtgs_ring_stall_total_us.store(0, std::memory_order_relaxed);
			mtgs_ring_stall_count.store(0, std::memory_order_relaxed);
			mtgs_ring_stall_max_us.store(0, std::memory_order_relaxed);
			mtgs_mtvu_packet_total_us.store(0, std::memory_order_relaxed);
			mtgs_mtvu_packet_count.store(0, std::memory_order_relaxed);
			mtgs_mtvu_packet_max_us.store(0, std::memory_order_relaxed);
			mtgs_mtvu_wait_total_us.store(0, std::memory_order_relaxed);
			mtgs_mtvu_wait_count.store(0, std::memory_order_relaxed);
			mtgs_mtvu_wait_max_us.store(0, std::memory_order_relaxed);
			mtgs_transfer_total_us.store(0, std::memory_order_relaxed);
			mtgs_transfer_count.store(0, std::memory_order_relaxed);
			mtgs_transfer_max_us.store(0, std::memory_order_relaxed);
			mtgs_vsync_process_total_us.store(0, std::memory_order_relaxed);
			mtgs_vsync_process_count.store(0, std::memory_order_relaxed);
			mtgs_vsync_process_max_us.store(0, std::memory_order_relaxed);
			gs_flush_total_us.store(0, std::memory_order_relaxed);
			gs_flush_count.store(0, std::memory_order_relaxed);
			gs_flush_max_us.store(0, std::memory_order_relaxed);
			gs_image_total_us.store(0, std::memory_order_relaxed);
			gs_image_count.store(0, std::memory_order_relaxed);
			gs_image_max_us.store(0, std::memory_order_relaxed);
		}

		void DumpToLogcat()
		{
			const u64 eet = ee_event_test_count.load(std::memory_order_relaxed);
			const u64 ipt = iop_exec_count.load(std::memory_order_relaxed);
			const u64 dit = dma_interrupt_count.load(std::memory_order_relaxed);
			const u64 v0t = vu0_sync_count.load(std::memory_order_relaxed);
			const u64 v1t = vu1_sync_count.load(std::memory_order_relaxed);
			const u64 rpt = rcnt_update_count.load(std::memory_order_relaxed);
			const u64 ipt2 = iop_event_test_count.load(std::memory_order_relaxed);
			const u64 ejt = ee_jit_exec_count.load(std::memory_order_relaxed);
			const u64 fdc = frame_delta_count.load(std::memory_order_relaxed);
			const u64 vst = vsync_start_count.load(std::memory_order_relaxed);
			const u64 vct = vsync_cpu_count.load(std::memory_order_relaxed);
			const u64 vtt = vsync_throttle_count.load(std::memory_order_relaxed);
			const u64 flt = frame_limiter_count.load(std::memory_order_relaxed);
			const u64 vgt = vsync_gs_post_count.load(std::memory_order_relaxed);
			const u64 vit = vsync_input_count.load(std::memory_order_relaxed);
			const u64 vir = vsync_irq_count.load(std::memory_order_relaxed);
			const u64 rct = rcnt_core_count.load(std::memory_order_relaxed);
			const u64 mwt = mtgs_vsync_wait_count.load(std::memory_order_relaxed);
			const u64 mrt = mtgs_ring_stall_count.load(std::memory_order_relaxed);
			const u64 mpt = mtgs_mtvu_packet_count.load(std::memory_order_relaxed);
			const u64 mxt = mtgs_mtvu_wait_count.load(std::memory_order_relaxed);
			const u64 mtt = mtgs_transfer_count.load(std::memory_order_relaxed);
			const u64 mvp = mtgs_vsync_process_count.load(std::memory_order_relaxed);
			const u64 gft = gs_flush_count.load(std::memory_order_relaxed);
			const u64 git = gs_image_count.load(std::memory_order_relaxed);

			__android_log_print(ANDROID_LOG_INFO, "Profiler", "=== EE/IOP/VU Profiler ===");
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "EEEventTest: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)eet,
				(unsigned long long)(eet ? ee_event_test_total_us.load(std::memory_order_relaxed) / eet : 0),
				(unsigned long long)ee_event_test_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "IOPExec: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)ipt,
				(unsigned long long)(ipt ? iop_exec_total_us.load(std::memory_order_relaxed) / ipt : 0),
				(unsigned long long)iop_exec_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "DMAIntr: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)dit,
				(unsigned long long)(dit ? dma_interrupt_total_us.load(std::memory_order_relaxed) / dit : 0),
				(unsigned long long)dma_interrupt_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "VU0Sync: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)v0t,
				(unsigned long long)(v0t ? vu0_sync_total_us.load(std::memory_order_relaxed) / v0t : 0),
				(unsigned long long)vu0_sync_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "VU1Sync: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)v1t,
				(unsigned long long)(v1t ? vu1_sync_total_us.load(std::memory_order_relaxed) / v1t : 0),
				(unsigned long long)vu1_sync_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "RcntUpd: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)rpt,
				(unsigned long long)(rpt ? rcnt_update_total_us.load(std::memory_order_relaxed) / rpt : 0),
				(unsigned long long)rcnt_update_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "IOPEventTest: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)ipt2,
				(unsigned long long)(ipt2 ? iop_event_test_total_us.load(std::memory_order_relaxed) / ipt2 : 0),
				(unsigned long long)iop_event_test_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "EEJitExec: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)ejt,
				(unsigned long long)(ejt ? ee_jit_exec_total_us.load(std::memory_order_relaxed) / ejt : 0),
				(unsigned long long)ee_jit_exec_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "FrameDelta: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)fdc,
				(unsigned long long)(fdc ? frame_delta_total_us.load(std::memory_order_relaxed) / fdc : 0),
				(unsigned long long)frame_delta_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "VSyncStart: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)vst,
				(unsigned long long)(vst ? vsync_start_total_us.load(std::memory_order_relaxed) / vst : 0),
				(unsigned long long)vsync_start_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "VSyncCPU: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)vct,
				(unsigned long long)(vct ? vsync_cpu_total_us.load(std::memory_order_relaxed) / vct : 0),
				(unsigned long long)vsync_cpu_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "VSyncThrottle: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)vtt,
				(unsigned long long)(vtt ? vsync_throttle_total_us.load(std::memory_order_relaxed) / vtt : 0),
				(unsigned long long)vsync_throttle_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "FrameLimiter: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)flt,
				(unsigned long long)(flt ? frame_limiter_total_us.load(std::memory_order_relaxed) / flt : 0),
				(unsigned long long)frame_limiter_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "VSyncGSPost: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)vgt,
				(unsigned long long)(vgt ? vsync_gs_post_total_us.load(std::memory_order_relaxed) / vgt : 0),
				(unsigned long long)vsync_gs_post_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "VSyncInput: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)vit,
				(unsigned long long)(vit ? vsync_input_total_us.load(std::memory_order_relaxed) / vit : 0),
				(unsigned long long)vsync_input_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "VSyncIRQ: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)vir,
				(unsigned long long)(vir ? vsync_irq_total_us.load(std::memory_order_relaxed) / vir : 0),
				(unsigned long long)vsync_irq_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "RcntCore: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)rct,
				(unsigned long long)(rct ? rcnt_core_total_us.load(std::memory_order_relaxed) / rct : 0),
				(unsigned long long)rcnt_core_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "MTGSVSyncWait: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)mwt,
				(unsigned long long)(mwt ? mtgs_vsync_wait_total_us.load(std::memory_order_relaxed) / mwt : 0),
				(unsigned long long)mtgs_vsync_wait_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "MTGSRingStall: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)mrt,
				(unsigned long long)(mrt ? mtgs_ring_stall_total_us.load(std::memory_order_relaxed) / mrt : 0),
				(unsigned long long)mtgs_ring_stall_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "MTGSMTVUPacket: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)mpt,
				(unsigned long long)(mpt ? mtgs_mtvu_packet_total_us.load(std::memory_order_relaxed) / mpt : 0),
				(unsigned long long)mtgs_mtvu_packet_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "MTGSMTVUWait: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)mxt,
				(unsigned long long)(mxt ? mtgs_mtvu_wait_total_us.load(std::memory_order_relaxed) / mxt : 0),
				(unsigned long long)mtgs_mtvu_wait_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "MTGSTransfer: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)mtt,
				(unsigned long long)(mtt ? mtgs_transfer_total_us.load(std::memory_order_relaxed) / mtt : 0),
				(unsigned long long)mtgs_transfer_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "MTGSVSyncProcess: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)mvp,
				(unsigned long long)(mvp ? mtgs_vsync_process_total_us.load(std::memory_order_relaxed) / mvp : 0),
				(unsigned long long)mtgs_vsync_process_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "GSFlush: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)gft,
				(unsigned long long)(gft ? gs_flush_total_us.load(std::memory_order_relaxed) / gft : 0),
				(unsigned long long)gs_flush_max_us.load(std::memory_order_relaxed));
			__android_log_print(ANDROID_LOG_INFO, "Profiler", "GSImage: count=%llu avg=%lluus max=%lluus",
				(unsigned long long)git,
				(unsigned long long)(git ? gs_image_total_us.load(std::memory_order_relaxed) / git : 0),
				(unsigned long long)gs_image_max_us.load(std::memory_order_relaxed));
			Reset();
		}
	};

	inline ProfilerMetrics s_profiler_metrics;
}

#if !defined(NDEBUG) || defined(PCSX2_DEVBUILD)

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

// Profiler macros - separate toggle for EE/IOP/VU0/VU1 profiling
#define DEBUG_PROF_LOG(level, ...) \
	do { \
		if (::emucorex::IsProfilerLogcatEnabled()) \
			__android_log_print(level, "Profiler", __VA_ARGS__); \
	} while(0)

#define DEBUG_PROF_TIMING_START(var) \
	uint64_t _prof_##var##_start = 0; \
	do { \
		if (::emucorex::IsProfilerLogcatEnabled()) \
			_prof_##var##_start = std::chrono::duration_cast<std::chrono::microseconds>( \
				std::chrono::steady_clock::now().time_since_epoch()).count(); \
	} while(0)

#define DEBUG_PROF_TIMING_END(var, counter) \
	do { \
		if (::emucorex::IsProfilerLogcatEnabled()) { \
			uint64_t _end = std::chrono::duration_cast<std::chrono::microseconds>( \
				std::chrono::steady_clock::now().time_since_epoch()).count(); \
			uint64_t _elapsed = _end - _prof_##var##_start; \
			if (_elapsed > 0) { \
				::emucorex::s_profiler_metrics.counter##_total_us.fetch_add(_elapsed, std::memory_order_relaxed); \
				::emucorex::s_profiler_metrics.counter##_count.fetch_add(1, std::memory_order_relaxed); \
				uint64_t _prev_max = ::emucorex::s_profiler_metrics.counter##_max_us.load(std::memory_order_relaxed); \
				while (_elapsed > _prev_max && \
					!::emucorex::s_profiler_metrics.counter##_max_us.compare_exchange_weak( \
						_prev_max, _elapsed, std::memory_order_relaxed)) \
					; \
			} \
		} \
	} while(0)

#define DEBUG_PROF_DUMP() \
	do { \
		if (::emucorex::IsProfilerLogcatEnabled()) \
			::emucorex::s_profiler_metrics.DumpToLogcat(); \
	} while(0)

#else
#define DEBUG_GS_LOG(level, ...) do {} while(0)
#define DEBUG_GS_TIMING_START(var) do {} while(0)
#define DEBUG_GS_TIMING_END(var, counter) do {} while(0)
#define DEBUG_GS_TIMING_END_U64(var, counter) do {} while(0)
#define DEBUG_GS_SET_MAX(counter, val) do {} while(0)
#define DEBUG_GS_INC_U64(counter, val) do {} while(0)
#define DEBUG_GS_DUMP_METRICS() do {} while(0)
#define DEBUG_PROF_LOG(level, ...) do {} while(0)
#define DEBUG_PROF_TIMING_START(var) do {} while(0)
#define DEBUG_PROF_TIMING_END(var, counter) do {} while(0)
#define DEBUG_PROF_DUMP() do {} while(0)
#endif

#else
#define DEBUG_GS_LOG(level, ...) do {} while(0)
#define DEBUG_GS_TIMING_START(var) do {} while(0)
#define DEBUG_GS_TIMING_END(var, counter) do {} while(0)
#define DEBUG_GS_TIMING_END_U64(var, counter) do {} while(0)
#define DEBUG_GS_SET_MAX(counter, val) do {} while(0)
#define DEBUG_GS_INC_U64(counter, val) do {} while(0)
#define DEBUG_GS_DUMP_METRICS() do {} while(0)
#define DEBUG_PROF_LOG(level, ...) do {} while(0)
#define DEBUG_PROF_TIMING_START(var) do {} while(0)
#define DEBUG_PROF_TIMING_END(var, counter) do {} while(0)
#define DEBUG_PROF_DUMP() do {} while(0)
#endif
