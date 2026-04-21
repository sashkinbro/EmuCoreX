// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "PrecompiledHeader.h"
#include "AutoTestTTYCapture.h"

#include "Config.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "platform/host/Host.h"

#include <cstdio>
#include <mutex>

namespace AutoTestTTYCapture
{
namespace
{
	struct State
	{
		std::mutex mutex;
		std::FILE* file = nullptr;
		std::string path;
	};

	State s_state;

	static const char* GetVU1BackendLabel()
	{
		return EmuConfig.Cpu.Recompiler.EnableVU1 ? "vu1_microvu" : "vu1_interpreter";
	}
} // namespace

void BeginForElf(const std::string& elf_path)
{
	std::unique_lock lock(s_state.mutex);

	if (s_state.file)
	{
		std::fflush(s_state.file);
		std::fclose(s_state.file);
		s_state.file = nullptr;
	}

	s_state.path.clear();

	const std::string autotest_dir = Path::Combine(EmuFolders::Logs, "autotests");
	if (!FileSystem::DirectoryExists(autotest_dir.c_str()) &&
		!FileSystem::CreateDirectoryPath(autotest_dir.c_str(), false))
	{
		Console.Error("AutoTestTTYCapture: failed to create directory '%s'", autotest_dir.c_str());
		return;
	}

	std::string test_name = Path::SanitizeFileName(Path::GetFileTitle(elf_path));
	if (test_name.empty())
		test_name = "autotest";

	s_state.path = Path::Combine(autotest_dir, fmt::format("{}_{}.log", test_name, GetVU1BackendLabel()));
	s_state.file = FileSystem::OpenCFile(s_state.path.c_str(), "wb");
	if (!s_state.file)
	{
		Console.Error("AutoTestTTYCapture: failed to open '%s'", s_state.path.c_str());
		s_state.path.clear();
		return;
	}

	Console.WriteLn("AutoTestTTYCapture: capturing TTY to '%s'", s_state.path.c_str());
	Host::AddOSDMessage(fmt::format("Autotest log: {}", Path::GetFileName(s_state.path)), Host::OSD_INFO_DURATION);
}

void Append(const std::string_view text)
{
	if (text.empty())
		return;

	std::unique_lock lock(s_state.mutex);
	if (!s_state.file)
		return;

	std::fwrite(text.data(), 1, text.size(), s_state.file);
	std::fflush(s_state.file);
}

void End()
{
	std::unique_lock lock(s_state.mutex);
	if (!s_state.file)
		return;

	std::fflush(s_state.file);
	std::fclose(s_state.file);
	s_state.file = nullptr;
}

bool IsActive()
{
	std::unique_lock lock(s_state.mutex);
	return (s_state.file != nullptr);
}

std::string GetCurrentPath()
{
	std::unique_lock lock(s_state.mutex);
	return s_state.path;
}
} // namespace AutoTestTTYCapture
