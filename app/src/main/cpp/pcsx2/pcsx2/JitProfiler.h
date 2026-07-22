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
	void EmitBlockIncrement(void* counter_ptr);
	void RecordBlockCompile(int type, u32 startpc, u32 guest_size, u32 host_size);
	void RecordCodeCacheReset(int type, u64 discarded_host_bytes);
}
