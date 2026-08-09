#include "HangTrace.h"

#if !defined(NDEBUG) || defined(PCSX2_DEVBUILD)

#include "arm64/OaknutHelpers-arm64.h"
#include "Config.h"
#include "CDVD/CDVDcommon.h"
#include "DebugTools/Debug.h"
#include "Host.h"
#include "IopMem.h"
#include "JitProfiler.h"
#include "Memory.h"
#include "MemoryTypes.h"
#include "PerformanceMetrics.h"
#include "R3000A.h"
#include "R5900.h"
#include "VMManager.h"
#include "VUmicro.h"
#include "common/Path.h"
#include "vtlb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern void EE_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks);
extern void IOP_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks);
extern void VU0_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks);
extern void VU1_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks);

namespace HangTrace
{
namespace
{
	constexpr size_t RING_SIZE = 4096;
	constexpr auto AUTOSAVE_INTERVAL = std::chrono::seconds(2);

	struct TraceEvent
	{
		u64 sequence = 0;
		u64 time_us = 0;
		u32 cpu = CPU_EE;
		u32 pc = 0;
		u32 code = 0;
		u32 ee_pc = 0;
		u32 iop_pc = 0;
		u32 ee_cycle = 0;
		u32 iop_cycle = 0;
		u64 frame = 0;
		float speed = 0.0f;
	};

	std::atomic_bool s_active{false};
	std::atomic<u64> s_sequence{0};
	std::mutex s_mutex;
	std::array<TraceEvent, RING_SIZE> s_ring = {};
	size_t s_write_index = 0;
	size_t s_event_count = 0;
	std::thread s_writer_thread;
	char s_last_report_path[512] = {};

	u64 NowUs()
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch();
		return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
	}

	const char* CpuName(u32 cpu)
	{
		switch (cpu)
		{
			case CPU_EE:
				return "EE";
			case CPU_IOP:
				return "IOP";
			case CPU_VU0:
				return "VU0";
			case CPU_VU1:
				return "VU1";
			default:
				return "UNK";
		}
	}

	bool ReadEEMemory(u32 address, void* dest, u32 size)
	{
		if (vtlb_memSafeReadBytes(address, dest, size))
			return true;

		const u32 paddr = address & 0x1fffffff;
		if (paddr + size <= Ps2MemSize::TotalRam && eeMem)
		{
			std::memcpy(dest, &eeMem->Main[paddr], size);
			return true;
		}

		if (address >= 0x70000000 && address < 0x70004000)
		{
			const u32 offset = address & 0x3fff;
			if (offset + size <= Ps2MemSize::Scratch && eeMem)
			{
				std::memcpy(dest, &eeMem->Scratch[offset], size);
				return true;
			}
		}

		return false;
	}

	bool ReadCode(u32 cpu, u32 pc, u32& code)
	{
		code = 0;
		switch (cpu)
		{
			case CPU_EE:
				return ReadEEMemory(pc, &code, sizeof(code));
			case CPU_IOP:
				return iopMemSafeReadBytes(pc, &code, sizeof(code));
			case CPU_VU0:
				if (VU0.Micro && pc + sizeof(code) <= VU0_PROGSIZE)
				{
					std::memcpy(&code, &VU0.Micro[pc], sizeof(code));
					return true;
				}
				return false;
			case CPU_VU1:
				if (VU1.Micro && pc + sizeof(code) <= VU1_PROGSIZE)
				{
					std::memcpy(&code, &VU1.Micro[pc], sizeof(code));
					return true;
				}
				return false;
			default:
				return false;
		}
	}

	std::string Disassemble(u32 cpu, u32 code, u32 pc)
	{
		switch (cpu)
		{
			case CPU_EE:
			{
				std::string out;
				R5900::disR5900Fasm(out, code, pc);
				return out;
			}
			case CPU_IOP:
			{
				char* out = R3000A::disR3000AF(code, pc);
				return out ? std::string(out) : std::string();
			}
			case CPU_VU0:
			case CPU_VU1:
			{
				u32 lower = 0;
				const bool is_vu1 = cpu == CPU_VU1;
				const u8* micro = is_vu1 ? VU1.Micro : VU0.Micro;
				const u32 max_size = is_vu1 ? VU1_PROGSIZE : VU0_PROGSIZE;
				if (micro && pc + 8 <= max_size)
					std::memcpy(&lower, &micro[pc], sizeof(lower));

				char* upper_dis = is_vu1 ? disVU1MicroUF(code, pc / 8) : disVU0MicroUF(code, pc / 8);
				char* lower_dis = is_vu1 ? disVU1MicroLF(lower, pc / 8) : disVU0MicroLF(lower, pc / 8);
				std::ostringstream ss;
				ss << (upper_dis ? upper_dis : "") << " | " << (lower_dis ? lower_dis : "");
				return ss.str();
			}
			default:
				return {};
		}
	}

	void AppendCurrentState(std::ofstream& out)
	{
		out << "Current State\n";
		out << "-------------\n";
		out << "VM state=" << static_cast<int>(VMManager::GetState())
			<< " valid_vm=" << (VMManager::HasValidVM() ? 1 : 0)
			<< " booted_elf=" << (VMManager::Internal::HasBootedELF() ? 1 : 0)
			<< " fast_booted=" << (VMManager::Internal::WasFastBooted() ? 1 : 0)
			<< " fast_boot_in_progress=" << (VMManager::Internal::IsFastBootInProgress() ? 1 : 0)
			<< " frame=" << PerformanceMetrics::GetFrameNumber()
			<< " fps=" << std::fixed << std::setprecision(2) << PerformanceMetrics::GetFPS()
			<< " speed=" << PerformanceMetrics::GetSpeed() << "\n";
		out << "Disc source=" << static_cast<int>(CDVDsys_GetSourceType())
			<< " path=" << VMManager::GetDiscPath()
			<< " serial=" << VMManager::GetDiscSerial()
			<< " crc=0x" << std::hex << std::setw(8) << std::setfill('0') << VMManager::GetDiscCRC()
			<< std::dec << std::setfill(' ') << "\n";
		out << std::hex << std::setfill('0');
		out << "EE  pc=0x" << std::setw(8) << cpuRegs.pc
			<< " code=0x" << std::setw(8) << cpuRegs.code
			<< " cycle=0x" << std::setw(8) << cpuRegs.cycle
			<< " next=0x" << std::setw(8) << cpuRegs.nextEventCycle
			<< " branch=" << std::dec << cpuRegs.branch
			<< " opmode=" << cpuRegs.opmode << "\n";
		out << std::hex << std::setfill('0');
		out << "EE  cp0 status=0x" << std::setw(8) << cpuRegs.CP0.n.Status.val
			<< " cause=0x" << std::setw(8) << cpuRegs.CP0.n.Cause
			<< " epc=0x" << std::setw(8) << cpuRegs.CP0.n.EPC
			<< " badvaddr=0x" << std::setw(8) << cpuRegs.CP0.n.BadVAddr << "\n";
		out << "IOP pc=0x" << std::setw(8) << psxRegs.pc
			<< " code=0x" << std::setw(8) << psxRegs.code
			<< " cycle=0x" << std::setw(8) << psxRegs.cycle
			<< " next=0x" << std::setw(8) << psxRegs.iopNextEventCycle
			<< " interrupt=0x" << std::setw(8) << psxRegs.interrupt << "\n";
		out << "VU0 tpc=0x" << std::setw(8) << VU0.VI[REG_TPC].UL
			<< " cpc=0x" << std::setw(8) << VU0.VI[REG_CMSAR0].UL
			<< " cycle=0x" << std::setw(8) << VU0.cycle
			<< " code=0x" << std::setw(8) << VU0.code << "\n";
		out << "VU1 tpc=0x" << std::setw(8) << VU1.VI[REG_TPC].UL
			<< " cpc=0x" << std::setw(8) << VU1.VI[REG_CMSAR1].UL
			<< " cycle=0x" << std::setw(8) << VU1.cycle
			<< " code=0x" << std::setw(8) << VU1.code << "\n\n";
		out << std::dec << std::setfill(' ');
	}

	void AppendHotBlocks(std::ofstream& out)
	{
		std::vector<JitBlockProfile> profiles;
		EE_JitGetBlockProfiles(profiles);
		IOP_JitGetBlockProfiles(profiles);
		VU0_JitGetBlockProfiles(profiles);
		VU1_JitGetBlockProfiles(profiles);

		std::sort(profiles.begin(), profiles.end(), [](const JitBlockProfile& a, const JitBlockProfile& b) {
			return a.execution_count > b.execution_count;
		});

		out << "Hot JIT Blocks\n";
		out << "--------------\n";
		out << std::left << std::setw(6) << "CPU" << std::setw(12) << "PC"
			<< std::setw(14) << "Execs" << std::setw(8) << "Size" << "First opcode\n";
		int written = 0;
		for (const JitBlockProfile& block : profiles)
		{
			if (block.execution_count == 0)
				continue;
			if (++written > 64)
				break;

			u32 code = 0;
			const bool has_code = ReadCode(block.type, block.startpc, code);
			out << std::left << std::setw(6) << CpuName(block.type)
				<< "0x" << std::hex << std::setw(8) << std::setfill('0') << block.startpc << std::setfill(' ') << std::dec << "  "
				<< std::setw(14) << static_cast<unsigned long long>(block.execution_count)
				<< std::setw(8) << block.size;
			if (has_code)
				out << "0x" << std::hex << std::setw(8) << std::setfill('0') << code << std::setfill(' ') << std::dec
					<< "  " << Disassemble(block.type, code, block.startpc);
			else
				out << "<unreadable>";
			out << "\n";
		}
		out << "\n";
	}

	std::vector<TraceEvent> SnapshotEvents()
	{
		std::lock_guard lock(s_mutex);
		std::vector<TraceEvent> events;
		events.reserve(s_event_count);
		const size_t first = s_event_count == RING_SIZE ? s_write_index : 0;
		for (size_t i = 0; i < s_event_count; i++)
			events.push_back(s_ring[(first + i) % RING_SIZE]);
		return events;
	}

	void WriteReport(const char* reason, bool include_hot_blocks)
	{
		const std::string filepath = Path::Combine(EmuFolders::DataRoot, "hang_trace.txt");
		std::snprintf(s_last_report_path, sizeof(s_last_report_path), "%s", filepath.c_str());

		std::ofstream out(filepath, std::ios::trunc);
		if (!out.is_open())
			return;

		const std::vector<TraceEvent> events = SnapshotEvents();
		out << "=========================================\n";
		out << "        EmuCoreX Hang Trace Report       \n";
		out << "=========================================\n\n";
		out << "Reason: " << (reason ? reason : "manual") << "\n";
		out << "Events captured: " << events.size() << " / " << RING_SIZE << "\n";
		out << "Active: " << (s_active.load(std::memory_order_relaxed) ? 1 : 0) << "\n\n";

		AppendCurrentState(out);
		if (include_hot_blocks)
			AppendHotBlocks(out);

		out << "Recent Execution Trace\n";
		out << "----------------------\n";
		out << std::left << std::setw(8) << "Seq" << std::setw(6) << "CPU" << std::setw(12) << "PC"
			<< std::setw(12) << "Opcode" << std::setw(12) << "EE PC" << std::setw(12) << "IOP PC"
			<< std::setw(12) << "EE cyc" << std::setw(12) << "IOP cyc" << std::setw(10) << "Frame"
			<< "Disassembly\n";

		for (const TraceEvent& event : events)
		{
			out << std::left << std::setw(8) << event.sequence
				<< std::setw(6) << CpuName(event.cpu)
				<< std::right
				<< "0x" << std::hex << std::setw(8) << std::setfill('0') << event.pc << std::setfill(' ') << std::dec << "  "
				<< "0x" << std::hex << std::setw(8) << std::setfill('0') << event.code << std::setfill(' ') << std::dec << "  "
				<< "0x" << std::hex << std::setw(8) << std::setfill('0') << event.ee_pc << std::setfill(' ') << std::dec << "  "
				<< "0x" << std::hex << std::setw(8) << std::setfill('0') << event.iop_pc << std::setfill(' ') << std::dec << "  "
				<< std::left
				<< std::setw(12) << event.ee_cycle
				<< std::setw(12) << event.iop_cycle
				<< std::setw(10) << static_cast<unsigned long long>(event.frame)
				<< Disassemble(event.cpu, event.code, event.pc) << "\n";
		}
	}

	void WriterLoop()
	{
		while (s_active.load(std::memory_order_relaxed))
		{
			std::this_thread::sleep_for(AUTOSAVE_INTERVAL);
			if (s_active.load(std::memory_order_relaxed))
				WriteReport("periodic autosave while hang trace is active", false);
		}
	}

	void ResetRecompilers()
	{
		Host::RunOnCPUThread([]() {
			recCpu.Reset();
			psxRec.Reset();
			CpuMicroVU0.Reset();
			CpuMicroVU1.Reset();
		}, true);
	}
} // namespace

bool IsActive()
{
	return s_active.load(std::memory_order_relaxed);
}

const char* GetLastReportPath()
{
	return s_last_report_path;
}

void Start()
{
	if (s_active.exchange(true))
		return;

	{
		std::lock_guard lock(s_mutex);
		s_write_index = 0;
		s_event_count = 0;
		s_sequence.store(0, std::memory_order_relaxed);
	}
	std::snprintf(s_last_report_path, sizeof(s_last_report_path), "%s",
		Path::Combine(EmuFolders::DataRoot, "hang_trace.txt").c_str());

	ResetRecompilers();
	s_writer_thread = std::thread(WriterLoop);
}

void Stop()
{
	if (!s_active.exchange(false))
		return;

	if (s_writer_thread.joinable())
		s_writer_thread.join();

	WriteReport("stopped from in-game menu", false);
	ResetRecompilers();
}

void RecordInterpreter(CpuType cpu, u32 pc, u32 code)
{
	RecordJitBlock(static_cast<u32>(cpu), pc, code);
}

void RecordJitBlock(u32 cpu, u32 pc, u32 code)
{
	if (!s_active.load(std::memory_order_relaxed))
		return;

	TraceEvent event;
	event.sequence = s_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
	event.time_us = NowUs();
	event.cpu = cpu;
	event.pc = pc;
	event.code = code;
	event.ee_pc = cpuRegs.pc;
	event.iop_pc = psxRegs.pc;
	event.ee_cycle = cpuRegs.cycle;
	event.iop_cycle = psxRegs.cycle;
	event.frame = PerformanceMetrics::GetFrameNumber();
	event.speed = PerformanceMetrics::GetSpeed();

	std::lock_guard lock(s_mutex);
	s_ring[s_write_index] = event;
	s_write_index = (s_write_index + 1) % RING_SIZE;
	s_event_count = std::min(s_event_count + 1, RING_SIZE);
}

void EmitBlockTrace(CpuType cpu, u32 pc, u32 code)
{
	if (!oakAsm)
		return;

	oakAsm->MOV(OAK_WARG1, static_cast<u32>(cpu));
	oakAsm->MOV(OAK_WARG2, pc);
	oakAsm->MOV(OAK_WARG3, code);
	oakEmitCall(reinterpret_cast<void*>(RecordJitBlock));
}
} // namespace HangTrace

#endif
