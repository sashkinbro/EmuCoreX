// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <cstdint>
#include <string_view>

namespace OracleGuard
{
struct Limits
{
    uint64_t milliseconds, rss_kib;
    unsigned descriptors, threads, cpu_seconds;
    uint64_t address_bytes = 0;
};
// Both implementations execute the same explicitly selected HW-copy fixture.
// Keep an exact allowlist so unrelated GS suites retain their existing budget.
constexpr Limits LimitsForGSSuite(std::string_view suite)
{
    const bool hw_copy = suite == "gs-hw-software-copies" || suite == "gs-hw-software-copies-jit";
    return {20000, (hw_copy ? 192u : 384u) * 1024, 1024, 128, 12};
}
struct Usage
{
    uint64_t milliseconds = 0, rss_kib = 0;
    unsigned descriptors = 0, threads = 0;
    bool valid = true;
};
enum class Stop { None, Time, Memory, Descriptors, Threads, Monitor };
constexpr Stop Exceeded(const Limits& limits, const Usage& usage)
{
    if (!usage.valid) return Stop::Monitor;
    if (usage.milliseconds >= limits.milliseconds) return Stop::Time;
    if (usage.rss_kib >= limits.rss_kib) return Stop::Memory;
    if (usage.descriptors >= limits.descriptors) return Stop::Descriptors;
    if (usage.threads >= limits.threads) return Stop::Threads;
    return Stop::None;
}
}

#if !defined(_WIN32)
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace OracleGuard
{
struct Result
{
    int code = 125;
    Stop stop = Stop::None;
    Usage peak{};
};
inline Usage ReadUsage(pid_t child, uint64_t elapsed)
{
    Usage usage{elapsed, 0, 0, 0, false};
    char path[64], line[256];
    std::snprintf(path, sizeof(path), "/proc/%d/status", child);
    FILE* status = std::fopen(path, "r");
    bool resident = false, threads = false;
    char state = 0;
    if (!status) return usage;
    while (std::fgets(line, sizeof(line), status))
    {
        unsigned long long rss = 0;
        if (std::sscanf(line, "VmRSS: %llu kB", &rss) == 1)
        { usage.rss_kib = rss; resident = true; }
        if (std::sscanf(line, "Threads: %u", &usage.threads) == 1) threads = true;
        std::sscanf(line, "State: %c", &state);
    }
    std::fclose(status);
    // An exiting thread-group leader (zombie/dead) has no VmRSS and may hide
    // its fd table while other threads finish teardown (e.g. EGL unload).
    // That is a process finishing, not an unavailable monitor.
    if (state == 'Z' || state == 'X')
    {
        usage.valid = threads;
        return usage;
    }
    std::snprintf(path, sizeof(path), "/proc/%d/fd", child);
    DIR* fds = opendir(path);
    if (!fds) return usage;
    while (const dirent* entry = readdir(fds))
        if (entry->d_name[0] != '.') ++usage.descriptors;
    closedir(fds);
    usage.valid = resident && threads;
    return usage;
}
inline const char* Reason(Stop stop)
{
    switch (stop)
    {
        case Stop::Time: return "wall-time";
        case Stop::Memory: return "resident-memory";
        case Stop::Descriptors: return "file-descriptors";
        case Stop::Threads: return "threads";
        case Stop::Monitor: return "resource-monitor-unavailable";
        default: return "completed";
    }
}
inline Result Run(int (*run)(void*), void* context, const Limits& limits)
{
    Result result;
    const pid_t parent = getpid();
    const pid_t child = fork();
    if (child < 0) { std::perror("oracle guard fork"); return result; }
    if (child == 0)
    {
        if (setpgid(0, 0) != 0) _exit(125);
#if defined(__linux__)
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent) _exit(125);
#endif
        const rlimit core{0, 0};
        const rlimit cpu{limits.cpu_seconds, limits.cpu_seconds + 1};
        const rlimit fds{limits.descriptors, limits.descriptors};
        if (setrlimit(RLIMIT_CORE, &core) || setrlimit(RLIMIT_CPU, &cpu) ||
            setrlimit(RLIMIT_NOFILE, &fds)) _exit(125);
        if (limits.address_bytes)
        {
            const rlimit address{limits.address_bytes, limits.address_bytes};
            if (setrlimit(RLIMIT_AS, &address)) _exit(125);
        }
        errno = 0;
        const int priority = getpriority(PRIO_PROCESS, 0);
        if (errno || setpriority(PRIO_PROCESS, 0, std::max(priority, 10))) _exit(125);
        const int code = run(context);
        std::fflush(nullptr);
        _exit(code);
    }
    const auto start = std::chrono::steady_clock::now();
    int status = 0;
    bool monitor_gap = false;
    uint64_t monitor_gap_start = 0;
    const auto completed = [&]() {
        siginfo_t exited{};
        if (waitid(P_PID, child, &exited, WEXITED | WNOHANG | WNOWAIT) != 0)
            return errno == EINTR ? 0 : -1;
        if (!exited.si_pid) return 0;
        // Keep the exited leader unreaped until its process group is cleaned up;
        // its PID cannot be reused by an unrelated process in between.
        result.code = exited.si_code == CLD_EXITED ? exited.si_status : 128 + exited.si_status;
        return 1;
    };
    for (;;)
    {
        const int done = completed();
        if (done != 0)
        {
            if (done < 0) result.stop = Stop::Monitor;
            break;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        result.peak.milliseconds = elapsed;
        if (static_cast<uint64_t>(elapsed) >= limits.milliseconds)
        { result.stop = Stop::Time; break; }
        Usage usage = ReadUsage(child, static_cast<uint64_t>(elapsed));
        if (!usage.valid)
        {
            // A child can exit between waitpid and opening its proc files.
            if (completed() == 1) break;
            // proc files disappear before a fast-exiting child becomes waitable.
            // Retry briefly without extending the independent wall deadline.
            if (!monitor_gap) { monitor_gap = true; monitor_gap_start = elapsed; }
            if (static_cast<uint64_t>(elapsed) - monitor_gap_start < 50)
            { usleep(1000); continue; }
        }
        else monitor_gap = false;
        result.peak.milliseconds = usage.milliseconds;
        result.peak.rss_kib = std::max(result.peak.rss_kib, usage.rss_kib);
        result.peak.descriptors = std::max(result.peak.descriptors, usage.descriptors);
        result.peak.threads = std::max(result.peak.threads, usage.threads);
        result.stop = Exceeded(limits, usage);
        if (result.stop != Stop::None) break;
        usleep(10000);
    }
    // Also remove descendants when the payload exits normally or by signal.
    kill(-child, SIGKILL);
    kill(child, SIGKILL);
    bool reaped = false;
    const auto cleanup_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    do
    {
        const pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child || (done < 0 && errno == ECHILD)) { reaped = true; break; }
        usleep(1000);
    } while (std::chrono::steady_clock::now() < cleanup_deadline);
    if (!reaped)
    {
        std::fprintf(stderr, "ORACLE_GUARD cleanup pending after SIGKILL; refusing further work\n");
        if (result.stop == Stop::None) result.stop = Stop::Monitor;
    }
    if (result.stop != Stop::None)
    {
        result.code = result.stop == Stop::Time ? 124 : 137;
    }
    std::fprintf(stderr, "ORACLE_GUARD %s exit=%d elapsed_ms=%llu peak_rss_kib=%llu peak_fds=%u peak_threads=%u\n",
        Reason(result.stop), result.code, static_cast<unsigned long long>(result.peak.milliseconds),
        static_cast<unsigned long long>(result.peak.rss_kib), result.peak.descriptors, result.peak.threads);
    return result;
}
}
#endif
