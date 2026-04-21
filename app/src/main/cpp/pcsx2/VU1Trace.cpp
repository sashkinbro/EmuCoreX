// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "VU1Trace.h"

#include "Config.h"
#include "VMManager.h"
#include "VU.h"
#include "VUmicro.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "platform/host/Host.h"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace VU1Trace
{
namespace
{
	struct CaptureState
	{
		std::mutex mutex;
		FileSystem::ManagedCFilePtr file;
		std::string path;
		std::chrono::steady_clock::time_point deadline = {};
		Engine engine = Engine::Interpreter;
		u64 step_count = 0;
		u32 flush_counter = 0;
		bool active = false;
	};

	CaptureState& GetCaptureState()
	{
		static CaptureState s_state;
		return s_state;
	}

	std::atomic<u8> g_interpreter_capture_active{0};

	const char* GetEngineLabel(const Engine engine)
	{
		return (engine == Engine::Recompiler) ? "microvu" : "interpreter";
	}

	void SetActiveFlags(const Engine engine, const bool active)
	{
		const u8 value = active ? 1 : 0;
		if (engine == Engine::Recompiler)
			g_recompiler_capture_active.store(value, std::memory_order_release);
		else
			g_interpreter_capture_active.store(value, std::memory_order_release);
	}

	void ClearAllActiveFlags()
	{
		g_recompiler_capture_active.store(0, std::memory_order_release);
		g_interpreter_capture_active.store(0, std::memory_order_release);
	}

	void WriteCaptureHeader(CaptureState& state, const u32 duration_ms)
	{
		if (!state.file)
			return;

		const std::string serial = VMManager::GetDiscSerial();
		const u32 crc = VMManager::GetDiscCRC();
		const int header_result = std::fprintf(
			state.file.get(),
			"# EmuCoreX VU1 trace\n"
			"engine=%s\n"
			"serial=%s\n"
			"crc=%08X\n"
			"duration_ms=%u\n"
			"format=pc,cycle,lower,upper,vf00..vf31(hex xyzw)\n",
			GetEngineLabel(state.engine),
			serial.c_str(),
			crc,
			duration_ms);
		if (header_result >= 0)
			std::fflush(state.file.get());
	}

	std::string CloseCaptureLocked(CaptureState& state, const char* reason)
	{
		if (!state.active)
			return {};

		const std::string path = state.path;
		const Engine engine = state.engine;

		if (state.file)
		{
			if (reason && reason[0] != '\0')
				std::fprintf(state.file.get(), "# capture_end=%s steps=%llu\n", reason,
					static_cast<unsigned long long>(state.step_count));
			std::fflush(state.file.get());
		}

		state.file.reset();
		state.path.clear();
		state.deadline = {};
		state.step_count = 0;
		state.flush_counter = 0;
		state.active = false;
		SetActiveFlags(engine, false);
		return path;
	}

	void MaybeNotifyClosedCapture(const std::string& path)
	{
		if (path.empty())
			return;

		Host::AddOSDMessage(
			fmt::format("VU1 trace saved: {}", path),
			Host::OSD_INFO_DURATION);
	}

	void AppendVectorBits(std::string& line, const VURegs& vu, const u32 reg)
	{
		line += fmt::format(
			" vf{:02}={:08X},{:08X},{:08X},{:08X}",
			reg,
			vu.VF[reg].UL[0],
			vu.VF[reg].UL[1],
			vu.VF[reg].UL[2],
			vu.VF[reg].UL[3]);
	}

	void WriteStepLocked(CaptureState& state, const VURegs& vu, const u32 executed_pc)
	{
		if (!state.file)
			return;

		const u32 vu_pc = executed_pc & VU1_PROGMASK;
		const u32* ptr = reinterpret_cast<const u32*>(&vu.Micro[vu_pc]);

		std::string line = fmt::format(
			"pc={:04X} cycle={} lower={:08X} upper={:08X}",
			vu_pc,
			static_cast<unsigned long long>(vu.cycle),
			ptr[0],
			ptr[1]);

		line.reserve(line.size() + 32 * 42);
		for (u32 reg = 0; reg < 32; reg++)
			AppendVectorBits(line, vu, reg);

		line.push_back('\n');
		std::fwrite(line.data(), 1, line.size(), state.file.get());

		state.step_count++;
		state.flush_counter++;
		if (state.flush_counter >= 64)
		{
			std::fflush(state.file.get());
			state.flush_counter = 0;
		}
	}

	void TryLogStep(const Engine engine, const VURegs& vu, const u32 executed_pc)
	{
		std::string finished_path;

		{
			CaptureState& state = GetCaptureState();
			std::lock_guard lock(state.mutex);

			if (!state.active || state.engine != engine || !state.file)
				return;

			if (std::chrono::steady_clock::now() >= state.deadline)
			{
				finished_path = CloseCaptureLocked(state, "deadline");
			}
			else
			{
				WriteStepLocked(state, vu, executed_pc);
				if (std::chrono::steady_clock::now() >= state.deadline)
					finished_path = CloseCaptureLocked(state, "deadline");
			}
		}

		MaybeNotifyClosedCapture(finished_path);
	}
} // namespace

std::atomic<u8> g_recompiler_capture_active{0};

std::string BeginCapture(const u32 duration_ms)
{
	if (!VMManager::HasValidVM())
		return {};

	const u32 clamped_duration = std::max(duration_ms, 1u);
	const Engine engine = EmuConfig.Cpu.Recompiler.EnableVU1 ? Engine::Recompiler : Engine::Interpreter;

	if (!FileSystem::DirectoryExists(EmuFolders::Logs.c_str()) &&
		!FileSystem::CreateDirectoryPath(EmuFolders::Logs.c_str(), false))
	{
		Console.Error("VU1Trace: failed to create logs directory '%s'", EmuFolders::Logs.c_str());
		return {};
	}

	const auto now = std::chrono::system_clock::now();
	const auto unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
	const std::string path = Path::Combine(
		EmuFolders::Logs,
		fmt::format("vu1_trace_{}_{}.log", GetEngineLabel(engine), unix_ms));

	Error error;
	FileSystem::ManagedCFilePtr file = FileSystem::OpenManagedCFile(path.c_str(), "wb", &error);
	if (!file)
	{
		Console.Error("VU1Trace: failed to open '%s': %s", path.c_str(), error.GetDescription().c_str());
		return {};
	}

	{
		CaptureState& state = GetCaptureState();
		std::lock_guard lock(state.mutex);

		CloseCaptureLocked(state, "replaced");
		state.file = std::move(file);
		state.path = path;
		state.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(clamped_duration);
		state.engine = engine;
		state.step_count = 0;
		state.flush_counter = 0;
		state.active = true;
		ClearAllActiveFlags();
		SetActiveFlags(engine, true);
		WriteCaptureHeader(state, clamped_duration);
	}

	Host::AddOSDMessage(
		fmt::format("VU1 trace armed for {} ms ({})", clamped_duration, GetEngineLabel(engine)),
		Host::OSD_INFO_DURATION);
	return path;
}

void Shutdown()
{
	std::string finished_path;
	{
		CaptureState& state = GetCaptureState();
		std::lock_guard lock(state.mutex);
		finished_path = CloseCaptureLocked(state, "shutdown");
	}
	MaybeNotifyClosedCapture(finished_path);
}

bool HasInterpreterCapture()
{
	return g_interpreter_capture_active.load(std::memory_order_acquire) != 0;
}

void LogInterpreterStep(const VURegs& vu, const u32 executed_pc)
{
	TryLogStep(Engine::Interpreter, vu, executed_pc);
}

void LogRecompilerStep(const u32 executed_pc)
{
	TryLogStep(Engine::Recompiler, VU1, executed_pc);
}
} // namespace VU1Trace
