// SPDX-License-Identifier: GPL-3.0+

#include "common/Pcsx2Types.h"
#include "common/HostSys.h"
#include "common/Threading.h"
#include "emucorex/android_runtime.h"

#include <cerrno>
#include <ctime>
#include <cstdio>
#include <string>
#include <unistd.h>

#include <sys/sysinfo.h>

u64 GetPhysicalMemory()
{
	long pages = 0;
#ifdef _SC_PHYS_PAGES
	pages = sysconf(_SC_PHYS_PAGES);
#endif

	const long page_size = sysconf(_SC_PAGESIZE);
	return (pages > 0 && page_size > 0) ? static_cast<u64>(pages) * static_cast<u64>(page_size) : 0;
}

u64 GetAvailablePhysicalMemory()
{
	FILE* file = std::fopen("/proc/meminfo", "r");
	if (file)
	{
		u64 mem_available = 0;
		u64 mem_free = 0;
		u64 buffers = 0;
		u64 cached = 0;
		u64 sreclaimable = 0;
		u64 shmem = 0;
		char line[256];

		while (std::fgets(line, sizeof(line), file))
		{
			if (std::sscanf(line, "MemAvailable: %lu kB", &mem_available) == 1)
			{
				std::fclose(file);
				return mem_available * _1kb;
			}

			std::sscanf(line, "MemFree: %lu kB", &mem_free);
			std::sscanf(line, "Buffers: %lu kB", &buffers);
			std::sscanf(line, "Cached: %lu kB", &cached);
			std::sscanf(line, "SReclaimable: %lu kB", &sreclaimable);
			std::sscanf(line, "Shmem: %lu kB", &shmem);
		}
		std::fclose(file);

		return (mem_free + buffers + cached + sreclaimable - shmem) * _1kb;
	}

	struct sysinfo info = {};
	if (sysinfo(&info) != 0)
		return 0;

	return (static_cast<u64>(info.freeram) + static_cast<u64>(info.bufferram)) * static_cast<u64>(info.mem_unit);
}

u64 GetTickFrequency()
{
	return 1000000000;
}

u64 GetCPUTicks()
{
	struct timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (static_cast<u64>(ts.tv_sec) * 1000000000ULL) + static_cast<u64>(ts.tv_nsec);
}

std::string GetOSVersionString()
{
	return "Android";
}

bool Common::InhibitScreensaver(bool inhibit)
{
	(void)inhibit;
	return true;
}

void Common::SetMousePosition(int x, int y)
{
	(void)x;
	(void)y;
}

bool Common::AttachMousePositionCb(std::function<void(int, int)> cb)
{
	(void)cb;
	return false;
}

void Common::DetachMousePositionCb()
{
}

bool Common::PlaySoundAsync(const char* path)
{
	return path && *path && emucorex::android::DispatchRetroAchievementsSound(path);
}

void Threading::Sleep(int ms)
{
	usleep(1000 * ms);
}

void Threading::SleepUntil(u64 ticks)
{
	struct timespec ts = {};
	ts.tv_sec = static_cast<time_t>(ticks / 1000000000ULL);
	ts.tv_nsec = static_cast<long>(ticks % 1000000000ULL);
	// clock_nanosleep() is not restarted by SA_RESTART. Without retrying, a
	// single signal (audio callbacks, profiler sampling, GC) aborts the wait and
	// the caller is left to busy-spin the remainder of the frame against the
	// monotonic clock. TIMER_ABSTIME makes re-issuing the call with the same
	// absolute deadline correct.
	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR)
		;
}
