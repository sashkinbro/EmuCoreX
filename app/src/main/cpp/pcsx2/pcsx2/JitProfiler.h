#pragma once
#include "common/Pcsx2Defs.h"
#include <vector>
#include <string>

struct JitBlockProfile
{
	u32 startpc;
	u32 size;
	u32 host_size;
	u64 execution_count;
	int type; // 0 = EE, 1 = IOP, 2 = VU0, 3 = VU1
	u64 state_hash = 0; // VU pipeline quick-state key. Zero for EE/IOP.
	u32 variant_index = 0; // VU block variant index for the same start PC.
	u32 flags = 0; // VU metadata/debug flags.
};

namespace JitProfiler
{
#if defined(NDEBUG) && !defined(PCSX2_DEVBUILD)
	class BlockCompileScope
	{
	public:
		constexpr BlockCompileScope(int, u32) {}
		constexpr void Finish(u32, u32, const void*, const void*) {}
	};

	class OpcodeRangeScope
	{
	public:
		constexpr OpcodeRangeScope() = default;
		constexpr void Begin(int, u32, u32, u32 = 0) {}
		constexpr void End() {}
	};

	inline constexpr bool IsActive() { return false; }
	inline constexpr void Start() {}
	inline constexpr void Stop() {}
	inline constexpr void RecordCodeCacheReset(int, u64) {}
#else
	// Measures one completed JIT block compilation while profiling is active.
	// Nested scopes subtract child time so aggregate exclusive time is not double-counted
	// by microVU's recursive block compiler. In normal gameplay the constructor performs
	// one inactive-state check and emits no timing, allocation, locking, or guest code.
	class BlockCompileScope
	{
	public:
		BlockCompileScope(int type, u32 startpc);
		~BlockCompileScope();

		BlockCompileScope(const BlockCompileScope&) = delete;
		BlockCompileScope& operator=(const BlockCompileScope&) = delete;

		void Finish(u32 guest_size, u32 host_size, const void* host_begin, const void* host_end);

	private:
		void Close();

		bool m_active = false;
		int m_type = 0;
		u32 m_startpc = 0;
		u64 m_start_value = 0;
		u64 m_child_value = 0;
		u64 m_inclusive_value = 0;
		u64 m_exclusive_value = 0;
		BlockCompileScope* m_parent = nullptr;
	};

	class OpcodeRangeScope
	{
	public:
		OpcodeRangeScope() = default;
		~OpcodeRangeScope();

		void Begin(int type, u32 guest_pc, u32 opcode, u32 paired_opcode = 0);
		void End();

	private:
		bool m_active = false;
		int m_type = 0;
		u32 m_guest_pc = 0;
		u32 m_opcode = 0;
		u32 m_paired_opcode = 0;
		uptr m_host_begin = 0;
	};

	bool IsActive();
	void Start();
	void Stop();
	void RecordCodeCacheReset(int type, u64 discarded_host_bytes);
#endif
}
