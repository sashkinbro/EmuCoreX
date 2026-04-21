// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <string>
#include <string_view>

namespace AutoTestTTYCapture
{
	void BeginForElf(const std::string& elf_path);
	void Append(std::string_view text);
	void End();
	bool IsActive();
	std::string GetCurrentPath();
} // namespace AutoTestTTYCapture
