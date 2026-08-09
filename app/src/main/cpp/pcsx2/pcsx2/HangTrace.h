#pragma once

#include "common/Pcsx2Defs.h"

namespace HangTrace
{
	enum CpuType : u32
	{
		CPU_EE = 0,
		CPU_IOP = 1,
		CPU_VU0 = 2,
		CPU_VU1 = 3,
	};

#if defined(NDEBUG) && !defined(PCSX2_DEVBUILD)
	inline constexpr bool IsActive() { return false; }
	inline constexpr void Start() {}
	inline constexpr void Stop() {}
	inline constexpr const char* GetLastReportPath() { return ""; }
	inline constexpr void RecordInterpreter(CpuType, u32, u32) {}
	inline constexpr void RecordJitBlock(u32, u32, u32) {}
	inline constexpr void EmitBlockTrace(CpuType, u32, u32) {}
#else
	bool IsActive();
	void Start();
	void Stop();
	const char* GetLastReportPath();

	void RecordInterpreter(CpuType cpu, u32 pc, u32 code);
	void RecordJitBlock(u32 cpu, u32 pc, u32 code);
	void EmitBlockTrace(CpuType cpu, u32 pc, u32 code);
#endif
}
