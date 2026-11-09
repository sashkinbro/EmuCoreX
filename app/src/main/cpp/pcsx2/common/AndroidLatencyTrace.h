// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>
#include <utility>

// Deliberately unavailable in Release, including Release native-self-test builds.
// Enable in Debug with debug.emucorex.latency=1. No state paths/content are logged.
#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>
#include <array>
#include <atomic>
#include <mutex>
#endif

namespace EmuCoreXLatency
{
using Token = std::uint64_t;
// Keep the bridge values in sync with NativeApp.LatencyEvent.
enum class Operation : int { Pause = 1, Resume = 2, Save = 3, Load = 4, AudioRecovery = 5 };

#if defined(__ANDROID__) && !defined(NDEBUG)
struct Slot { Token token = 0; std::uint64_t start_us = 0; unsigned events = 0; };
inline std::array<Slot, 64> slots{};
inline std::mutex slots_mutex;
inline Token next_token = 0;
inline thread_local Token current_token = 0;
inline std::atomic<Token> first_present{0};

inline std::uint64_t NowUs()
{
	timespec ts{};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<std::uint64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

inline void Event(Token token, const char* name, const char* phase = "I", std::int64_t value = 0)
{
	if (!token)
		return;
	unsigned sequence;
	std::uint64_t start_us;
	{
		const std::lock_guard lock(slots_mutex);
		Slot& slot = slots[token % slots.size()];
		// Old operations are dropped on wrap; every operation emits at most 64 events.
		if (slot.token != token || slot.events >= 64)
			return;
		sequence = ++slot.events;
		start_us = slot.start_us;
	}
	const std::uint64_t now_us = NowUs();
	__android_log_print(ANDROID_LOG_INFO, "EmuCoreXLatency",
		"id=%llu seq=%u tid=%d us=%llu elapsed_us=%llu phase=%s span=%s value=%lld",
		static_cast<unsigned long long>(token), sequence, static_cast<int>(gettid()),
		static_cast<unsigned long long>(now_us), static_cast<unsigned long long>(now_us - start_us),
		phase, name, static_cast<long long>(value));
}

inline Token Begin(Operation operation)
{
	char enabled[PROP_VALUE_MAX]{};
	if (__system_property_get("debug.emucorex.latency", enabled) != 1 || enabled[0] != '1')
		return 0;
	Token token;
	{
		const std::lock_guard lock(slots_mutex);
		token = ++next_token;
		slots[token % slots.size()] = {token, NowUs(), 0};
	}
	Event(token, "operation", "I", static_cast<int>(operation));
	return token;
}

inline Token Current() { return current_token; }
inline void SetCurrent(Token token) { current_token = token; }
inline void ArmFirstPresent(Token token) { first_present.store(token, std::memory_order_release); }
inline void FirstPresent()
{
	if (first_present.load(std::memory_order_acquire))
		Event(first_present.exchange(0, std::memory_order_acq_rel), "load.first_nonblank_present_return");
}
#else
inline void Event(Token, const char*, const char* = "I", std::int64_t = 0) {}
inline Token Begin(Operation) { return 0; }
inline Token Current() { return 0; }
inline void SetCurrent(Token) {}
inline void ArmFirstPresent(Token) {}
inline void FirstPresent() {}
#endif

class Bind
{
public:
	explicit Bind(Token token) : previous(Current()) { SetCurrent(token); }
	~Bind() { SetCurrent(previous); }
	Bind(const Bind&) = delete;
	Bind& operator=(const Bind&) = delete;
private:
	Token previous;
};

class Span
{
public:
	explicit Span(const char* name) : token(Current()), name(name) { Event(token, name, "B"); }
	~Span() { Finish(); }
	void Finish() { Event(token, name, "E"); token = 0; }
	Span(const Span&) = delete;
	Span& operator=(const Span&) = delete;
private:
	Token token;
	const char* name;
};

// Preserve the existing callable/argument layout entirely in Release builds.
template <typename F> auto Carry(F function)
{
#if defined(__ANDROID__) && !defined(NDEBUG)
	return [token = Current(), function = std::move(function)](auto&&... args) mutable -> decltype(auto) {
		const Bind bind(token);
		return function(std::forward<decltype(args)>(args)...);
	};
#else
	return function;
#endif
}
} // namespace EmuCoreXLatency
