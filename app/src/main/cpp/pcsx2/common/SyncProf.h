// Thread-sync wait profiling (debug-only diagnostics).
//
// Compiles to nothing unless EMUCOREX_SYNC_PROF is defined: native self-test debug builds
// (EMUCOREX_SYNC_PROF_BUILD from CMake && !NDEBUG), or EMUCOREX_SYNC_PROF_FORCE for a profiling
// build. At runtime it is additionally off unless system property debug.emucorex.sync_prof=1.
//
// Counters are only touched on slow paths: a timer is armed right before a thread is about to
// spin-wait, yield or sleep, never on a non-blocking fast path. Sync semantics are not changed.
//
// Every 60 vsyncs SyncProf::OnVsync() logs one logcat line (tag EmuSync):
//   frames=60 ee_wait[name=ms/count ...] vu_wait[...] gs_wait[...] iop_wait[...] cnt[name=count ...]
// ms and count are per frame; only non-zero sites are listed.
#pragma once

#include <cstdint>

#if (defined(EMUCOREX_SYNC_PROF_BUILD) && !defined(NDEBUG)) || defined(EMUCOREX_SYNC_PROF_FORCE)
#define EMUCOREX_SYNC_PROF 1
#endif

namespace SyncProf
{
	// Which WorkSema instance (set once by the owner of the semaphore).
	enum class Tag : uint8_t
	{
		Other = 0,
		Mtvu,
		Mtgs,
		Iop,
		GsJob,
		GsBack,
		Count
	};

	// Who called MTVU WaitVU().
	enum class Caller : uint8_t
	{
		Unknown = 0,
		Vif,
		VU1micro,
		VUmicroMem,
		Dmac,
		SPR,
		Memory,
		Internal,
		Count
	};
} // namespace SyncProf

#ifdef EMUCOREX_SYNC_PROF

#include <atomic>
#include <cstdio>
#include <time.h>
#if defined(__ANDROID__)
#include <android/log.h>
#include <sys/system_properties.h>
#endif

#define SYNCPROF_CALLER_PARAM SyncProf::Caller sp_caller = SyncProf::Caller::Unknown
#define SYNCPROF_CALLER_DEF SyncProf::Caller sp_caller
#define SYNCPROF_CALLER(x) SyncProf::Caller::x
#define SYNCPROF_SET_TAG(sema, tag) (sema).SetProfTag(SyncProf::Tag::tag)

namespace SyncProf
{
	// Which thread waits (report grouping). Cnt sites report count only.
	enum class Group : uint8_t { EE, VU, GS, IOP, Cnt };

	// Per WorkSema tag, five sites.
	enum WsKind : uint32_t { WsWorkSpin = 0, WsWorkSleep, WsEmptySpin, WsEmptySleep, WsWake, WsKindCount };
	constexpr uint32_t kWsSites = static_cast<uint32_t>(Tag::Count) * WsKindCount;

	enum Site : uint32_t
	{
		// WorkSema sites occupy [0, kWsSites)
		SiteWaitVUFirst = kWsSites, // + Caller
		SiteVuWaitOnSize = SiteWaitVUFirst + static_cast<uint32_t>(Caller::Count),
		SiteVuKick,
		SiteVuExecute,
		SiteMtgsVsync,
		SiteMtgsWaitGsStrong,
		SiteMtgsWaitGsWeak,
		SiteMtgsRingResetSleep,
		SiteMtgsRingSpin,
		SiteMtgsXgkick,
		SiteGifPushFull,
		SiteIopIdleWait,
		SiteIopLagSpin,
		SiteIopHoldWait,
		SiteIopI2EFull,
		SiteCount
	};

	struct alignas(64) Counter
	{
		std::atomic<uint64_t> count{0};
		std::atomic<uint64_t> blocked{0};
		std::atomic<uint64_t> ns{0};
	};

	inline Counter g_sites[SiteCount];
	inline std::atomic<int> g_state{0}; // 0 = unread, 1 = off, 2 = on

	inline uint64_t NowNs()
	{
		timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
	}

	inline bool ReadProperty()
	{
#if defined(__ANDROID__)
		char value[PROP_VALUE_MAX] = {};
		return __system_property_get("debug.emucorex.sync_prof", value) > 0 && value[0] == '1';
#else
		return false;
#endif
	}

	inline bool Enabled()
	{
		int s = g_state.load(std::memory_order_relaxed);
		if (s == 0)
		{
			s = ReadProperty() ? 2 : 1;
			g_state.store(s, std::memory_order_relaxed);
		}
		return s == 2;
	}

	inline void Add(uint32_t site, uint64_t ns, bool blocked)
	{
		Counter& c = g_sites[site];
		c.count.fetch_add(1, std::memory_order_relaxed);
		if (blocked)
			c.blocked.fetch_add(1, std::memory_order_relaxed);
		c.ns.fetch_add(ns, std::memory_order_relaxed);
	}

	// Count-only event.
	inline void Count(uint32_t site)
	{
		if (Enabled())
			g_sites[site].count.fetch_add(1, std::memory_order_relaxed);
	}

	inline uint32_t WsSite(Tag t, WsKind k) { return static_cast<uint32_t>(t) * WsKindCount + k; }

	// Times a wait that blocks for its whole duration (sleep, or a yield loop).
	class BlockTimer
	{
		uint32_t m_site;
		uint64_t m_t0;

	public:
		explicit BlockTimer(uint32_t site, bool armed = true)
			: m_site(site)
			, m_t0((armed && Enabled()) ? NowNs() : 0)
		{
		}
		// For waits that decide inside a loop whether they block (armed = false at construction).
		void Arm()
		{
			if (!m_t0 && Enabled())
				m_t0 = NowNs();
		}
		~BlockTimer()
		{
			if (m_t0)
				Add(m_site, NowNs() - m_t0, true);
		}
		BlockTimer(const BlockTimer&) = delete;
		BlockTimer& operator=(const BlockTimer&) = delete;
	};

	// Times a spin phase that may end in a sleep: spin ns and sleep ns go to separate sites.
	class SpinSleepTimer
	{
		uint32_t m_spin, m_sleep;
		uint64_t m_t0, m_t1 = 0;

	public:
		SpinSleepTimer(uint32_t spin, uint32_t sleep, bool armed)
			: m_spin(spin)
			, m_sleep(sleep)
			, m_t0((armed && Enabled()) ? NowNs() : 0)
		{
		}
		void Sleeping()
		{
			if (m_t0)
				m_t1 = NowNs();
		}
		~SpinSleepTimer()
		{
			if (!m_t0)
				return;
			const uint64_t end = NowNs();
			if (m_t1)
			{
				Add(m_spin, m_t1 - m_t0, false);
				Add(m_sleep, end - m_t1, true);
			}
			else
				Add(m_spin, end - m_t0, false);
		}
		SpinSleepTimer(const SpinSleepTimer&) = delete;
		SpinSleepTimer& operator=(const SpinSleepTimer&) = delete;
	};

	struct SiteInfo
	{
		const char* name;
		Group group;
	};

	inline SiteInfo Describe(uint32_t site)
	{
		static const char* const ws_name[static_cast<uint32_t>(Tag::Count)][WsKindCount] = {
			{"other_idle_spin", "other_idle_sleep", "other_empty_spin", "other_empty_sleep", "other_wake"},
			{"mtvu_idle_spin", "mtvu_idle_sleep", "mtvu_empty_spin", "mtvu_empty_sleep", "mtvu_wake"},
			{"mtgs_idle_spin", "mtgs_idle_sleep", "mtgs_empty_spin", "mtgs_empty_sleep", "mtgs_wake"},
			{"iop_idle_spin", "iop_idle_sleep", "iop_empty_spin", "iop_empty_sleep", "iop_wake"},
			{"gsjob_idle_spin", "gsjob_idle_sleep", "gsjob_empty_spin", "gsjob_empty_sleep", "gsjob_wake"},
			{"gsback_idle_spin", "gsback_idle_sleep", "gsback_empty_spin", "gsback_empty_sleep", "gsback_wake"},
		};
		// Waiter of the work wait / empty wait, per tag.
		static const Group work_group[] = {Group::EE, Group::VU, Group::GS, Group::IOP, Group::GS, Group::GS};
		static const Group empty_group[] = {Group::EE, Group::EE, Group::EE, Group::EE, Group::GS, Group::GS};
		static const char* const caller_name[] = {"waitvu_unknown", "waitvu_vif", "waitvu_vu1micro", "waitvu_vumem",
			"waitvu_dmac", "waitvu_spr", "waitvu_memory", "waitvu_internal"};

		if (site < kWsSites)
		{
			const uint32_t t = site / WsKindCount, k = site % WsKindCount;
			Group g = (k <= WsWorkSleep) ? work_group[t] : (k <= WsEmptySleep ? empty_group[t] : Group::Cnt);
			return {ws_name[t][k], g};
		}
		if (site < SiteVuWaitOnSize)
			return {caller_name[site - SiteWaitVUFirst], Group::EE};
		switch (site)
		{
			case SiteVuWaitOnSize: return {"vu_waitonsize", Group::EE};
			case SiteVuKick: return {"vu_kick", Group::Cnt};
			case SiteVuExecute: return {"vu_execute", Group::Cnt};
			case SiteMtgsVsync: return {"vsync_wait", Group::EE};
			case SiteMtgsWaitGsStrong: return {"waitgs_strong", Group::EE};
			case SiteMtgsWaitGsWeak: return {"waitgs_weak", Group::VU};
			case SiteMtgsRingResetSleep: return {"ring_full_sleep", Group::EE};
			case SiteMtgsRingSpin: return {"ring_full_spin", Group::EE};
			case SiteMtgsXgkick: return {"xgkick_sema", Group::GS};
			case SiteGifPushFull: return {"gif_push_full", Group::VU};
			case SiteIopIdleWait: return {"iop_idlewait", Group::IOP};
			case SiteIopLagSpin: return {"iop_lagspin", Group::EE};
			case SiteIopHoldWait: return {"iop_holdwait", Group::IOP};
			case SiteIopI2EFull: return {"iop_i2e_full", Group::IOP};
		}
		return {"?", Group::Cnt};
	}

	// Called once per vsync from the EE thread (MTGS::PostVsyncStart).
	inline void OnVsync()
	{
		static uint32_t frames = 0;
		static uint64_t prev[SiteCount][3] = {};
		if (!Enabled() || ++frames < 60)
			return;
		const uint32_t n = frames;
		frames = 0;

		static const char* const group_name[] = {"ee_wait", "vu_wait", "gs_wait", "iop_wait", "cnt"};
		char line[1536];
		size_t len = static_cast<size_t>(snprintf(line, sizeof(line), "frames=%u", n));
		for (int g = 0; g < 5; g++)
		{
			bool any = false;
			for (uint32_t s = 0; s < SiteCount; s++)
			{
				const SiteInfo info = Describe(s);
				if (static_cast<int>(info.group) != g)
					continue;
				const uint64_t c = g_sites[s].count.load(std::memory_order_relaxed);
				const uint64_t b = g_sites[s].blocked.load(std::memory_order_relaxed);
				const uint64_t ns = g_sites[s].ns.load(std::memory_order_relaxed);
				const uint64_t dc = c - prev[s][0], dns = ns - prev[s][2];
				(void)b;
				prev[s][0] = c;
				prev[s][1] = b;
				prev[s][2] = ns;
				if (dc == 0 && dns == 0)
					continue;
				if (len + 80 >= sizeof(line))
					continue;
				if (!any)
				{
					len += static_cast<size_t>(snprintf(line + len, sizeof(line) - len, " %s[", group_name[g]));
					any = true;
				}
				else
					line[len++] = ' ';
				if (g == 4)
					len += static_cast<size_t>(snprintf(line + len, sizeof(line) - len, "%s=%.2f", info.name, static_cast<double>(dc) / n));
				else
					len += static_cast<size_t>(snprintf(line + len, sizeof(line) - len, "%s=%.3fms/%.2f", info.name,
						static_cast<double>(dns) / 1e6 / n, static_cast<double>(dc) / n));
			}
			if (any)
			{
				line[len++] = ']';
				line[len] = 0;
			}
		}
#if defined(__ANDROID__)
		__android_log_print(ANDROID_LOG_INFO, "EmuSync", "%s", line);
#else
		fprintf(stderr, "EmuSync %s\n", line);
#endif
	}
} // namespace SyncProf

#else // !EMUCOREX_SYNC_PROF

#define SYNCPROF_CALLER_PARAM
#define SYNCPROF_CALLER_DEF
#define SYNCPROF_CALLER(x)
#define SYNCPROF_SET_TAG(sema, tag) ((void)0)

#endif
