// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Common.h"

#include <atomic>
#include <string>

struct VURegs;

namespace VU1Trace
{
	enum class Engine : u8
	{
		Interpreter = 0,
		Recompiler = 1,
	};

	extern std::atomic<u8> g_recompiler_capture_active;

	constexpr u32 DEFAULT_MAX_TRACE_STEPS = 2048;

	std::string BeginCapture(u32 duration_ms);
	void Shutdown();

	bool HasInterpreterCapture();
	bool HasRecompilerCapture();
	void LogInterpreterStep(const VURegs& vu, u32 executed_pc);
	void LogRecompilerStep(u32 executed_pc);
} // namespace VU1Trace
