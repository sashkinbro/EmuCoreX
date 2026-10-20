// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Error.h"

#include <string>
#include <vector>

namespace GSDumpReplayer
{
#ifndef EMUCOREX_DISABLE_GS_DIAGNOSTICS
	bool IsReplayingDump();

	/// If set, playback will repeat once it reaches the last frame.
	void SetLoopCount(s32 loop_count = 0);
	int GetLoopCount();
	bool IsRunner();
	void SetIsDumpRunner(bool is_runner);

	bool Initialize(const char* filename, Error* error = nullptr);
	bool ChangeDump(const char* filename);
	void Shutdown();

	std::string GetDumpSerial();
	u32 GetDumpCRC();

	u32 GetFrameNumber();

	void RenderUI();
#else
	inline bool IsReplayingDump() { return false; }
	inline void SetLoopCount(s32 = 0) {}
	inline int GetLoopCount() { return 0; }
	inline bool IsRunner() { return false; }
	inline void SetIsDumpRunner(bool) {}
	inline bool Initialize(const char*, Error* = nullptr) { return false; }
	inline bool ChangeDump(const char*) { return false; }
	inline void Shutdown() {}
	inline std::string GetDumpSerial() { return {}; }
	inline u32 GetDumpCRC() { return 0; }
	inline u32 GetFrameNumber() { return 0; }
	inline void RenderUI() {}
#endif
} // namespace GSDumpReplayer
