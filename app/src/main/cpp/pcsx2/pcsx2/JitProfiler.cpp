#include "JitProfiler.h"

#include "Common.h"
#include "Config.h"
#include "DebugTools/Debug.h"
#include "Hardware.h"
#include "Host.h"
#include "IopMem.h"
#include "MemoryTypes.h"
#include "MTVU.h"
#include "PerformanceMetrics.h"
#include "R3000A.h"
#include "R5900.h"
#include "VMManager.h"
#include "VUmicro.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "arm64/cpuRegistersPack-arm64.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/Threading.h"
#include "common/Timer.h"
#include "vtlb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__ANDROID__) && defined(__aarch64__)
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <ucontext.h>
#endif

extern void EE_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks);
extern void IOP_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks);
extern void VU0_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks);
extern void VU1_JitGetBlockProfiles(std::vector<JitBlockProfile>& outBlocks);

namespace JitProfiler
{
namespace
{
	static std::atomic<bool> s_active{false};
	static std::atomic<bool> s_collecting_runtime{false};
	static std::chrono::steady_clock::time_point s_start_time;
	static u64 s_start_frame = 0;
	static std::mutex s_compile_mutex;
	static std::mutex s_opcode_range_mutex;

	struct OpcodeRangeEvent
	{
		uptr host_begin = 0;
		uptr host_end = 0;
		u32 first_sample = 0;
		u32 guest_pc = 0;
		u32 opcode = 0;
		u32 paired_opcode = 0;
		int type = 0;
	};

	static std::vector<OpcodeRangeEvent> s_opcode_ranges;
	static constexpr size_t OPCODE_RANGE_CAPACITY = 1u << 19;
	static std::atomic<bool> s_opcode_range_limit_hit{false};

	struct RawPcSample
	{
		uptr pc = 0;
		uptr lr = 0;
		u8 thread_type = 0;
	};

	static constexpr u32 SAMPLE_BUFFER_CAPACITY = 1u << 18;
	static constexpr u32 SAMPLE_INTERVAL_US = 2000;
	static std::unique_ptr<RawPcSample[]> s_sample_buffer;
	static RawPcSample* s_raw_samples = nullptr;
	static std::atomic<u32> s_sample_write{0};
	static std::atomic<u32> s_sample_dropped{0};
	static std::atomic<bool> s_sampling_active{false};

#if defined(__ANDROID__) && defined(__aarch64__)
	static struct sigaction s_previous_sampling_signal = {};
	static pthread_t s_cpu_sample_target = {};
	static pthread_t s_vu_sample_target = {};
	static Threading::ThreadHandle s_cpu_sample_handle;
	static Threading::ThreadHandle s_vu_sample_handle;
	static std::thread s_sampler_thread;
	static bool s_sampler_installed = false;
#endif

	struct CompileEvent
	{
		int type = 0;
		u32 startpc = 0;
		u32 guest_size = 0;
		u32 host_size = 0;
		u64 frame = 0;
		double seconds = 0.0;
	};

	static std::vector<CompileEvent> s_compile_events;
	static constexpr size_t COMPILE_EVENT_CAPACITY = 1u << 19;
	static std::atomic<u64> s_compile_events_dropped{0};

	struct CacheResetEvent
	{
		int type = 0;
		u64 discarded_host_bytes = 0;
		u64 frame = 0;
	};

	static std::vector<CacheResetEvent> s_cache_reset_events;

	struct CpuTotals
	{
		u64 compiled_blocks = 0;
		u64 executed_blocks = 0;
		u64 executions = 0;
		u64 guest_instruction_slots = 0;
		u64 dynamic_instruction_slots = 0;
		u64 host_bytes = 0;
		u64 estimated_dynamic_host_bytes = 0;
		u64 zero_count_blocks = 0;
		u64 unreadable_instruction_slots = 0;
	};

	struct HotStat
	{
		std::string cpu;
		std::string category;
		std::string name;
		u64 dynamic_count = 0;
		u64 static_slots = 0;
		u64 estimated_dynamic_host_bytes = 0;
	};

	struct PcAggregate
	{
		std::string cpu;
		u32 pc = 0;
		u32 variants = 0;
		u64 executions = 0;
		u64 dynamic_guest_ops = 0;
		u64 estimated_dynamic_host_bytes = 0;
		u32 max_host_size = 0;
		double max_expansion = 0.0;
	};

	struct VuNormalizedAggregate
	{
		std::string cpu;
		u32 pc = 0;
		u64 normalized_state = 0;
		u32 variants = 0;
		u64 executions = 0;
		u64 dynamic_guest_ops = 0;
		u64 estimated_dynamic_host_bytes = 0;
		u32 min_host_size = std::numeric_limits<u32>::max();
		u32 max_host_size = 0;
		double max_expansion = 0.0;
		std::array<bool, 256> flag_infos = {};
		std::array<bool, 256> vi_backups = {};
	};

	struct BlockAnalysis
	{
		JitBlockProfile profile = {};
		u64 dynamic_guest_slots = 0;
		u64 dynamic_guest_ops = 0;
		u64 estimated_dynamic_host_bytes = 0;
		double host_expansion = 0.0;
		u32 unreadable_slots = 0;
		std::vector<std::string> disassembly;
	};

	struct CompileCpuTotals
	{
		u64 blocks = 0;
		u64 guest_slots = 0;
		u64 host_bytes = 0;
		u64 frames = 0;
		u64 peak_frame_blocks = 0;
		u64 peak_frame_host_bytes = 0;
		u64 peak_frame = 0;
	};

	struct CompilePcAggregate
	{
		int type = 0;
		u32 pc = 0;
		u64 blocks = 0;
		u64 guest_slots = 0;
		u64 host_bytes = 0;
		u64 first_frame = std::numeric_limits<u64>::max();
		u64 last_frame = 0;
	};

	struct CompileFrameAggregate
	{
		u64 frame = 0;
		u64 blocks = 0;
		u64 host_bytes = 0;
		std::array<u64, 4> cpu_blocks = {};
	};

#if defined(__ANDROID__) && defined(__aarch64__)
	void SamplingSignalHandler(int, siginfo_t*, void* context)
	{
		if (!s_sampling_active.load(std::memory_order_relaxed) || !s_raw_samples)
			return;

		const u32 index = s_sample_write.fetch_add(1, std::memory_order_relaxed);
		if (index >= SAMPLE_BUFFER_CAPACITY)
		{
			s_sample_dropped.fetch_add(1, std::memory_order_relaxed);
			s_sampling_active.store(false, std::memory_order_release);
			return;
		}

		const ucontext_t* const uctx = static_cast<const ucontext_t*>(context);
		s_raw_samples[index].pc = static_cast<uptr>(uctx->uc_mcontext.pc);
		s_raw_samples[index].lr = static_cast<uptr>(uctx->uc_mcontext.regs[30]);
		const pthread_t current = pthread_self();
		s_raw_samples[index].thread_type = (current == s_cpu_sample_target) ? 1 : ((current == s_vu_sample_target) ? 2 : 0);
	}

	void SamplingThreadMain()
	{
		const u64 sample_interval_ticks =
			std::max<u64>((static_cast<u64>(SAMPLE_INTERVAL_US) * Threading::GetThreadTicksPerSecond()) / 1000000u, 1u);
		u64 cpu_last_time = s_cpu_sample_handle ? s_cpu_sample_handle.GetCPUTime() : 0;
		u64 vu_last_time = s_vu_sample_handle ? s_vu_sample_handle.GetCPUTime() : 0;
		u64 cpu_credit = 0;
		u64 vu_credit = 0;

		while (s_sampling_active.load(std::memory_order_acquire))
		{
			std::this_thread::sleep_for(std::chrono::microseconds(SAMPLE_INTERVAL_US / 4));

			const auto sample_active_thread = [sample_interval_ticks](const Threading::ThreadHandle& handle,
				pthread_t target, u64& last_time, u64& credit) {
				if (!handle || !target)
					return;

				const u64 current_time = handle.GetCPUTime();
				if (current_time <= last_time)
					return;

				credit = std::min(credit + (current_time - last_time), sample_interval_ticks);
				last_time = current_time;
				if (credit >= sample_interval_ticks && pthread_kill(target, SIGUSR2) == 0)
					credit -= sample_interval_ticks;
			};

			sample_active_thread(s_cpu_sample_handle, s_cpu_sample_target, cpu_last_time, cpu_credit);
			sample_active_thread(s_vu_sample_handle, s_vu_sample_target, vu_last_time, vu_credit);
		}
	}
#endif

	bool StartPcSampler()
	{
		s_sample_buffer.reset(new (std::nothrow) RawPcSample[SAMPLE_BUFFER_CAPACITY]);
		if (!s_sample_buffer)
			return false;
		s_raw_samples = s_sample_buffer.get();
		s_sample_write.store(0, std::memory_order_relaxed);
		s_sample_dropped.store(0, std::memory_order_relaxed);

#if defined(__ANDROID__) && defined(__aarch64__)
		struct sigaction action = {};
		action.sa_sigaction = SamplingSignalHandler;
		action.sa_flags = SA_SIGINFO | SA_RESTART;
		sigemptyset(&action.sa_mask);
		if (sigaction(SIGUSR2, &action, &s_previous_sampling_signal) != 0)
			return false;

		s_sampler_installed = true;
		s_sampling_active.store(true, std::memory_order_release);
		s_sampler_thread = std::thread(SamplingThreadMain);
		return true;
#else
		return false;
#endif
	}

	void StopPcSampler()
	{
		s_sampling_active.store(false, std::memory_order_release);

#if defined(__ANDROID__) && defined(__aarch64__)
		if (!s_sampler_installed)
			return;

		if (s_sampler_thread.joinable())
			s_sampler_thread.join();
		sigaction(SIGUSR2, &s_previous_sampling_signal, nullptr);
		s_sampler_installed = false;
#endif
	}

	void RecordOpcodeRange(int type, u32 guest_pc, u32 opcode, u32 paired_opcode, uptr host_begin, uptr host_end)
	{
		if (host_end <= host_begin)
			return;

		OpcodeRangeEvent event;
		event.host_begin = host_begin;
		event.host_end = host_end;
		event.first_sample = std::min(s_sample_write.load(std::memory_order_relaxed), SAMPLE_BUFFER_CAPACITY);
		event.guest_pc = guest_pc;
		event.opcode = opcode;
		event.paired_opcode = paired_opcode;
		event.type = type;

		std::lock_guard<std::mutex> lock(s_opcode_range_mutex);
		if (s_opcode_ranges.size() >= OPCODE_RANGE_CAPACITY)
		{
			// Stop the sampler at the same boundary. Continuing to sample without
			// recording newer ranges would attribute reused JIT addresses to stale
			// code after a cache reset.
			s_opcode_range_limit_hit.store(true, std::memory_order_relaxed);
			s_sampling_active.store(false, std::memory_order_release);
			return;
		}
		s_opcode_ranges.push_back(event);
	}

	const char* CpuName(int type)
	{
		switch (type)
		{
			case 0:
				return "EE";
			case 1:
				return "IOP";
			case 2:
				return "VU0";
			case 3:
				return "VU1";
			case 4:
				return "VU0";
			case 5:
				return "VU1";
			default:
				return "UNK";
		}
	}

	u32 GuestBytesPerSlot(int type)
	{
		return (type == 2 || type == 3) ? 8 : 4;
	}

	std::string FormatPc(int type, u32 pc)
	{
		std::ostringstream out;
		out << "0x" << std::hex << std::setw((type == 2 || type == 3) ? 8 : 8) << std::setfill('0') << pc
			<< std::dec << std::setfill(' ');
		return out.str();
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

	bool ReadGuest32(int type, u32 address, u32& code)
	{
		switch (type)
		{
			case 0:
				return ReadEEMemory(address, &code, sizeof(code));
			case 1:
				return iopMemSafeReadBytes(address, &code, sizeof(code));
			default:
				return false;
		}
	}

	bool ReadVUPair(int type, u32 byte_offset, u32& upper, u32& lower)
	{
		const bool vu0 = (type == 2);
		const u32 program_size = vu0 ? VU0_PROGSIZE : VU1_PROGSIZE;
		u8* micro = vu0 ? VU0.Micro : VU1.Micro;
		if (!micro || byte_offset + 8 > program_size)
			return false;

		std::memcpy(&lower, &micro[byte_offset], sizeof(lower));
		std::memcpy(&upper, &micro[byte_offset + 4], sizeof(upper));
		return true;
	}

	std::string TrimMnemonic(std::string text)
	{
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
			text.erase(text.begin());
		const size_t pos = text.find_first_of(" \t\r\n,");
		if (pos != std::string::npos)
			text.resize(pos);
		if (text.empty())
			return "<unknown>";
		return text;
	}

	std::string DisassembleEE(u32 code, u32 pc)
	{
		std::string out;
		R5900::disR5900Fasm(out, code, pc);
		return out.empty() ? "<unknown>" : out;
	}

	std::string DisassembleIOP(u32 code, u32 pc)
	{
		char* text = R3000A::disR3000AF(code, pc);
		return (text && *text) ? std::string(text) : std::string("<unknown>");
	}

	std::string DisassembleVUUpper(int type, u32 code, u32 index)
	{
		if (code == 0x8000033c)
			return "NOP";

		char* text = (type == 2) ? disVU0MicroUF(code, index) : disVU1MicroUF(code, index);
		return (text && *text) ? std::string(text) : std::string("<unknown>");
	}

	std::string DisassembleVULower(int type, u32 code, u32 index)
	{
		char* text = (type == 2) ? disVU0MicroLF(code, index) : disVU1MicroLF(code, index);
		return (text && *text) ? std::string(text) : std::string("<unknown>");
	}

	const char* DecodeVUUpperFdName(u32 code)
	{
		static constexpr std::array<const char*, 32> fd00 = {{
			"ADDAx", "SUBAx", "MADDAx", "MSUBAx", "ITOF0", "FTOI0", "MULAx", "MULAq",
			"ADDAq", "SUBAq", "ADDA", "SUBA", nullptr, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
		}};
		static constexpr std::array<const char*, 32> fd01 = {{
			"ADDAy", "SUBAy", "MADDAy", "MSUBAy", "ITOF4", "FTOI4", "MULAy", "ABS",
			"MADDAq", "MSUBAq", "MADDA", "MSUBA", nullptr, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
		}};
		static constexpr std::array<const char*, 32> fd10 = {{
			"ADDAz", "SUBAz", "MADDAz", "MSUBAz", "ITOF12", "FTOI12", "MULAz", "MULAi",
			"ADDAi", "SUBAi", "MULA", "OPMULA", nullptr, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
		}};
		static constexpr std::array<const char*, 32> fd11 = {{
			"ADDAw", "SUBAw", "MADDAw", "MSUBAw", "ITOF15", "FTOI15", "MULAw", "CLIP",
			"MADDAi", "MSUBAi", nullptr, "NOP", nullptr, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
		}};

		const u32 fd = (code >> 6) & 0x1f;
		switch (code & 0x3f)
		{
			case 0x3c:
				return fd00[fd];
			case 0x3d:
				return fd01[fd];
			case 0x3e:
				return fd10[fd];
			case 0x3f:
				return fd11[fd];
			default:
				return nullptr;
		}
	}

	std::string FormatVUUnknownUpper(u32 code)
	{
		std::ostringstream out;
		out << "UnknownUpper.op" << std::hex << std::setw(2) << std::setfill('0') << (code & 0x3f)
		    << ".fd" << std::setw(2) << ((code >> 6) & 0x1f)
		    << ".mask" << ((code >> 21) & 0xf) << std::setfill(' ');
		return out.str();
	}

	bool IsBadVUDisassembly(const std::string& text)
	{
		return text.find("*** Bad OP ***") != std::string::npos;
	}

	std::string NormalizeVUUpperDisassembly(u32 code, const std::string& text)
	{
		if (!IsBadVUDisassembly(text))
			return text;

		if (const char* name = DecodeVUUpperFdName(code))
			return name;

		return FormatVUUnknownUpper(code);
	}

	bool VUUpperEmitsHostCode(u32 code)
	{
		if (code == 0x8000033c)
			return false;

		const u32 mask = (code >> 21) & 0xf;
		const u32 op = code & 0x3f;
		if (op < 0x30)
			return true;
		if (op < 0x3c)
			return false;

		const char* name = DecodeVUUpperFdName(code);
		if (!name || std::strcmp(name, "NOP") == 0)
			return false;

		const u32 ft = (code >> 16) & 0x1f;
		if ((ft == 0 || mask == 0) && (std::strncmp(name, "ITOF", 4) == 0 || std::strncmp(name, "FTOI", 4) == 0 || std::strcmp(name, "ABS") == 0))
			return false;

		if (mask == 0 && (std::strncmp(name, "MAX", 3) == 0 || std::strncmp(name, "MINI", 4) == 0))
			return false;

		return true;
	}

	bool VUUpperUsesNoLaneFastPath(u32 code)
	{
		if (((code >> 21) & 0xf) != 0)
			return false;

		const u32 op = code & 0x3f;
		if (op < 0x30)
			return true;

		if (op < 0x3c)
			return false;

		const char* name = DecodeVUUpperFdName(code);
		if (!name)
			return false;

		if (std::strncmp(name, "ADDA", 4) == 0 ||
		    std::strncmp(name, "SUBA", 4) == 0 ||
		    std::strncmp(name, "MADDA", 5) == 0 ||
		    std::strncmp(name, "MSUBA", 5) == 0 ||
		    std::strncmp(name, "MULA", 4) == 0 ||
		    std::strncmp(name, "OPMULA", 6) == 0)
		{
			return false;
		}

		return std::strncmp(name, "ADD", 3) == 0 ||
		       std::strncmp(name, "SUB", 3) == 0 ||
		       std::strncmp(name, "MADD", 4) == 0 ||
		       std::strncmp(name, "MSUB", 4) == 0 ||
		       std::strncmp(name, "MUL", 3) == 0;
	}

	u32 VUUpperHostWeight(u32 code)
	{
		if (!VUUpperEmitsHostCode(code))
			return 0;

		return VUUpperUsesNoLaneFastPath(code) ? 1 : 4;
	}

	bool VULowerEmitsHostCode(u32 code)
	{
		const u32 op = code >> 25;
		const u32 xyzw = (code >> 21) & 0xf;
		const u32 ft = (code >> 16) & 0x1f;

		if (op == 0x00 && (ft == 0 || xyzw == 0))
			return false;

		return true;
	}

	u32 VULowerHostWeight(u32 code)
	{
		return VULowerEmitsHostCode(code) ? 4 : 0;
	}

	bool MipsEmitsHostCode(u32 code)
	{
		return code != 0;
	}

	const char* ClassifyEE(u32 code)
	{
		const u32 op = code >> 26;
		switch (op)
		{
			case 0x01:
			case 0x02:
			case 0x03:
			case 0x04:
			case 0x05:
			case 0x06:
			case 0x07:
			case 0x14:
			case 0x15:
			case 0x16:
			case 0x17:
				return "EE_BRANCH";
			case 0x10:
				return "EE_COP0";
			case 0x11:
				return "EE_FPU_COP1";
			case 0x12:
				return "EE_COP2_VU_MACRO";
			case 0x1c:
				return "EE_MMI";
			case 0x31:
			case 0x39:
				return "EE_FPU_MEMORY";
			case 0x36:
			case 0x3e:
				return "EE_COP2_MEMORY";
			case 0x20:
			case 0x21:
			case 0x22:
			case 0x23:
			case 0x24:
			case 0x25:
			case 0x26:
			case 0x27:
			case 0x28:
			case 0x29:
			case 0x2b:
			case 0x2c:
			case 0x2d:
			case 0x2f:
			case 0x37:
			case 0x3f:
				return "EE_MEMORY";
			default:
				return "EE_CORE";
		}
	}

	const char* ClassifyIOP(u32 code)
	{
		const u32 op = code >> 26;
		switch (op)
		{
			case 0x01:
			case 0x02:
			case 0x03:
			case 0x04:
			case 0x05:
			case 0x06:
			case 0x07:
				return "IOP_BRANCH";
			case 0x10:
				return "IOP_COP0";
			case 0x12:
			case 0x32:
			case 0x3a:
				return "IOP_COP2_GTE";
			case 0x20:
			case 0x21:
			case 0x22:
			case 0x23:
			case 0x24:
			case 0x25:
			case 0x28:
			case 0x29:
			case 0x2a:
			case 0x2b:
				return "IOP_MEMORY";
			default:
				return "IOP_CORE";
		}
	}

	void AddHotStat(
		std::unordered_map<std::string, HotStat>& map,
		const std::string& cpu,
		const std::string& category,
		const std::string& name,
		u64 dynamic_count,
		u64 estimated_dynamic_host_bytes)
	{
		const std::string key = cpu + '\n' + category + '\n' + name;
		HotStat& stat = map[key];
		if (stat.name.empty())
		{
			stat.cpu = cpu;
			stat.category = category;
			stat.name = name;
		}
		stat.dynamic_count += dynamic_count;
		stat.static_slots++;
		stat.estimated_dynamic_host_bytes += estimated_dynamic_host_bytes;
	}

	std::vector<HotStat> SortedStats(const std::unordered_map<std::string, HotStat>& map)
	{
		std::vector<HotStat> out;
		out.reserve(map.size());
		for (const auto& it : map)
			out.push_back(it.second);
		std::sort(out.begin(), out.end(), [](const HotStat& a, const HotStat& b) {
			if (a.dynamic_count != b.dynamic_count)
				return a.dynamic_count > b.dynamic_count;
			return a.estimated_dynamic_host_bytes > b.estimated_dynamic_host_bytes;
		});
		return out;
	}

	std::string SanitizeFilePart(std::string text)
	{
		if (text.empty())
			return "unknown";

		for (char& c : text)
		{
			const unsigned char ch = static_cast<unsigned char>(c);
			if (!std::isalnum(ch) && c != '-' && c != '_')
				c = '_';
		}
		while (text.find("__") != std::string::npos)
			text.replace(text.find("__"), 2, "_");
		if (text.size() > 48)
			text.resize(48);
		return text;
	}

	std::string TimestampForFile()
	{
		const std::time_t now = std::time(nullptr);
		std::tm tm = {};
#ifdef _WIN32
		localtime_s(&tm, &now);
#else
		localtime_r(&now, &tm);
#endif
		std::ostringstream out;
		out << std::put_time(&tm, "%Y%m%d_%H%M%S");
		return out.str();
	}

	std::string Hex8(u32 value)
	{
		std::ostringstream out;
		out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
		return out.str();
	}

	std::string Hex16(u64 value)
	{
		std::ostringstream out;
		out << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
		return out.str();
	}

	u8 VuStateByte(u64 state, int index)
	{
		return static_cast<u8>((state >> (index * 8)) & 0xff);
	}

	u64 VuNormalizeQuickState(u64 state)
	{
		constexpr u64 FLAG_INFO_MASK = 0xffull << 8;
		constexpr u64 VI_BACKUP_MASK = 0xffull << 40;
		return state & ~(FLAG_INFO_MASK | VI_BACKUP_MASK);
	}

	std::string ByteSetText(const std::array<bool, 256>& values)
	{
		std::ostringstream out;
		bool first = true;
		for (size_t i = 0; i < values.size(); i++)
		{
			if (!values[i])
				continue;
			if (!first)
				out << ',';
			first = false;
			out << i;
		}
		return first ? "-" : out.str();
	}

	std::string VuStateFieldsText(u64 state)
	{
		std::ostringstream out;
		out << "need=" << static_cast<unsigned>(VuStateByte(state, 0))
			<< ",flag=" << static_cast<unsigned>(VuStateByte(state, 1))
			<< ",q=" << static_cast<unsigned>(VuStateByte(state, 2))
			<< ",p=" << static_cast<unsigned>(VuStateByte(state, 3))
			<< ",xg=" << static_cast<unsigned>(VuStateByte(state, 4))
			<< ",viBak=" << static_cast<unsigned>(VuStateByte(state, 5))
			<< ",bt=" << static_cast<unsigned>(VuStateByte(state, 6))
			<< ",r=" << static_cast<unsigned>(VuStateByte(state, 7));
		return out.str();
	}

	std::string VuFlagsText(const JitBlockProfile& profile)
	{
		if (profile.type != 2 && profile.type != 3)
			return "-";

		std::ostringstream out;
		out << ((profile.flags & (1u << 16)) ? "exact" : "quick");
		const u32 need_exact = profile.flags & 0xffu;
		const u32 block_type = (profile.flags >> 8) & 0xffu;
		if (need_exact != 0)
			out << ",need=" << need_exact;
		if (block_type != 0)
			out << ",bt=" << block_type;
		if (profile.flags & (1u << 17))
			out << ",vi15";
		return out.str();
	}

	struct TimeSampleStat
	{
		std::string cpu;
		std::string category;
		std::string name;
		u64 samples = 0;
		u64 direct_samples = 0;
		u64 helper_samples = 0;
	};

	struct TimePcStat : TimeSampleStat
	{
		u32 guest_pc = 0;
	};

	const OpcodeRangeEvent* FindSampleRange(
		const std::unordered_map<uptr, std::vector<const OpcodeRangeEvent*>>& page_map,
		uptr host_pc,
		u32 sample_index)
	{
		const auto page_it = page_map.find(host_pc >> 12);
		if (page_it == page_map.end())
			return nullptr;

		const OpcodeRangeEvent* best = nullptr;
		for (const OpcodeRangeEvent* range : page_it->second)
		{
			if (range->first_sample > sample_index || host_pc < range->host_begin || host_pc >= range->host_end)
				continue;

			if (!best || range->first_sample > best->first_sample ||
				(range->first_sample == best->first_sample &&
					(range->host_end - range->host_begin) < (best->host_end - best->host_begin)))
			{
				best = range;
			}
		}
		return best;
	}

	const char* VuControlName(u32 kind)
	{
		switch (kind)
		{
			case 1:
				return "branch delay: B";
			case 2:
				return "branch delay: BAL";
			case 3:
				return "branch delay: IBEQ";
			case 4:
				return "branch delay: IBGEZ";
			case 5:
				return "branch delay: IBGTZ";
			case 6:
				return "branch delay: IBLEZ";
			case 7:
				return "branch delay: IBLTZ";
			case 8:
				return "branch delay: IBNE";
			case 9:
				return "branch delay: JR";
			case 10:
				return "branch delay: JALR";
			case 16:
				return "T-bit control";
			case 17:
				return "D-bit control";
			case 18:
				return "M-bit control";
			case 19:
				return "XGKICK delay control";
			case 20:
				return "evil-block exit";
			case 21:
				return "micro-memory range wrap";
			case 22:
				return "E-bit program exit";
			case 23:
				return "block entry cycle guard";
			case 24:
				return "block entry register preload";
			default:
				return "other block control";
		}
	}

	void DescribeSampleRange(const OpcodeRangeEvent& range, std::string& cpu, std::string& category, std::string& name)
	{
		cpu = CpuName(range.type);
		switch (range.type)
		{
			case 0:
				category = ClassifyEE(range.opcode);
				name = TrimMnemonic(DisassembleEE(range.opcode, range.guest_pc));
				break;
			case 1:
				category = ClassifyIOP(range.opcode);
				name = TrimMnemonic(DisassembleIOP(range.opcode, range.guest_pc));
				break;
			case 2:
			case 3:
			{
				category = (range.type == 2) ? "VU0_PAIR" : "VU1_PAIR";
				const std::string upper = NormalizeVUUpperDisassembly(
					range.opcode, DisassembleVUUpper(range.type, range.opcode, range.guest_pc / 8));
				const std::string lower = DisassembleVULower(range.type, range.paired_opcode, range.guest_pc / 8);
				name = TrimMnemonic(upper) + " | " + TrimMnemonic(lower);
				break;
			}
			case 4:
			case 5:
				category = (range.type == 4) ? "VU0_CONTROL" : "VU1_CONTROL";
				name = VuControlName(range.opcode);
				break;
			default:
				category = "UNKNOWN";
				name = "<unknown>";
				break;
		}
	}

	std::string NativeSampleName(uptr pc)
	{
#if defined(__ANDROID__) && defined(__aarch64__)
		Dl_info info = {};
		if (dladdr(reinterpret_cast<const void*>(pc), &info) != 0)
		{
			if (info.dli_sname && *info.dli_sname)
				return info.dli_sname;
			if (info.dli_fname && *info.dli_fname)
			{
				const char* slash = std::strrchr(info.dli_fname, '/');
				std::ostringstream out;
				out << (slash ? slash + 1 : info.dli_fname);
				if (info.dli_fbase)
				{
					const uptr module_offset = pc - reinterpret_cast<uptr>(info.dli_fbase);
					out << "+0x" << std::hex << (module_offset & ~static_cast<uptr>(0x3f));
				}
				return out.str();
			}
		}
#else
		(void)pc;
#endif
		return "<native-or-unmapped>";
	}

	void AppendSamplingHotspots(std::ostringstream& out)
	{
		const u32 captured_samples = std::min(s_sample_write.load(std::memory_order_relaxed), SAMPLE_BUFFER_CAPACITY);
		const u32 dropped_samples = s_sample_dropped.load(std::memory_order_relaxed);

		std::vector<OpcodeRangeEvent> ranges;
		{
			std::lock_guard<std::mutex> lock(s_opcode_range_mutex);
			// Sampling is stopped before report generation, so ownership can move
			// into the report without temporarily duplicating tens of megabytes.
			ranges.swap(s_opcode_ranges);
		}

		std::unordered_map<uptr, std::vector<const OpcodeRangeEvent*>> page_map;
		for (const OpcodeRangeEvent& range : ranges)
		{
			const uptr first_page = range.host_begin >> 12;
			const uptr last_page = (range.host_end - 1) >> 12;
			for (uptr page = first_page; page <= last_page; page++)
				page_map[page].push_back(&range);
		}

		std::unordered_map<std::string, TimeSampleStat> opcode_stats;
		std::unordered_map<std::string, TimePcStat> pc_stats;
		std::unordered_map<std::string, u64> native_stats;
		u64 direct_samples = 0;
		u64 helper_samples = 0;
		u64 native_samples = 0;
		u64 cpu_thread_samples = 0;
		u64 vu_thread_samples = 0;

		for (u32 i = 0; i < captured_samples; i++)
		{
			const RawPcSample& sample = s_raw_samples[i];
			cpu_thread_samples += (sample.thread_type == 1);
			vu_thread_samples += (sample.thread_type == 2);
			const OpcodeRangeEvent* range = FindSampleRange(page_map, sample.pc, i);
			bool helper = false;
			if (!range && sample.lr)
			{
				range = FindSampleRange(page_map, sample.lr, i);
				helper = (range != nullptr);
			}

			if (!range)
			{
				const char* thread_name = (sample.thread_type == 1) ? "EE/IOP: " : ((sample.thread_type == 2) ? "VU1: " : "Other: ");
				native_stats[std::string(thread_name) + NativeSampleName(sample.pc)]++;
				native_samples++;
				continue;
			}

			std::string cpu;
			std::string category;
			std::string name;
			DescribeSampleRange(*range, cpu, category, name);

			const std::string opcode_key = cpu + '\n' + category + '\n' + name;
			TimeSampleStat& opcode_stat = opcode_stats[opcode_key];
			opcode_stat.cpu = cpu;
			opcode_stat.category = category;
			opcode_stat.name = name;
			opcode_stat.samples++;

			const std::string pc_key = opcode_key + '\n' + std::to_string(range->guest_pc);
			TimePcStat& pc_stat = pc_stats[pc_key];
			pc_stat.cpu = cpu;
			pc_stat.category = category;
			pc_stat.name = name;
			pc_stat.guest_pc = range->guest_pc;
			pc_stat.samples++;

			if (helper)
			{
				opcode_stat.helper_samples++;
				pc_stat.helper_samples++;
				helper_samples++;
			}
			else
			{
				opcode_stat.direct_samples++;
				pc_stat.direct_samples++;
				direct_samples++;
			}
		}

		std::vector<TimeSampleStat> sorted_opcodes;
		for (const auto& entry : opcode_stats)
			sorted_opcodes.push_back(entry.second);
		std::sort(sorted_opcodes.begin(), sorted_opcodes.end(), [](const TimeSampleStat& a, const TimeSampleStat& b) {
			return a.samples > b.samples;
		});

		std::vector<TimePcStat> sorted_pcs;
		for (const auto& entry : pc_stats)
			sorted_pcs.push_back(entry.second);
		std::sort(sorted_pcs.begin(), sorted_pcs.end(), [](const TimePcStat& a, const TimePcStat& b) {
			return a.samples > b.samples;
		});

		std::vector<std::pair<std::string, u64>> sorted_native(native_stats.begin(), native_stats.end());
		std::sort(sorted_native.begin(), sorted_native.end(), [](const auto& a, const auto& b) {
			return a.second > b.second;
		});

		out << "Statistical CPU Time Samples\n";
		out << "----------------------------\n";
		out << "Sampling interval: " << SAMPLE_INTERVAL_US << " us of consumed CPU time per target thread\n";
		out << "Captured samples: " << captured_samples << "\n";
		out << "EE/IOP thread samples: " << cpu_thread_samples << "\n";
		out << "VU1 thread samples: " << vu_thread_samples << "\n";
		out << "Direct JIT samples: " << direct_samples << "\n";
		out << "JIT helper samples attributed through LR: " << helper_samples << "\n";
		out << "Native/unmapped samples: " << native_samples << "\n";
		out << "Dropped samples: " << dropped_samples << "\n";
		out << "Recorded opcode host ranges: " << ranges.size() << "\n\n";
		out << "Opcode range capture capped: "
			<< (s_opcode_range_limit_hit.load(std::memory_order_relaxed) ? "yes" : "no") << "\n\n";

		out << "Top Opcodes/Pairs by Sampled CPU Time\n";
		out << "--------------------------------------\n";
		out << "CPU\tCategory\tName\tSamples\tDirect\tHelper\tSharePercent\n";
		for (size_t i = 0; i < std::min<size_t>(sorted_opcodes.size(), 120); i++)
		{
			const TimeSampleStat& stat = sorted_opcodes[i];
			const double share = captured_samples ? (static_cast<double>(stat.samples) * 100.0 / captured_samples) : 0.0;
			out << stat.cpu << '\t' << stat.category << '\t' << stat.name << '\t'
				<< stat.samples << '\t' << stat.direct_samples << '\t' << stat.helper_samples << '\t'
				<< std::fixed << std::setprecision(3) << share << "\n";
		}
		out << "\n";

		out << "Top Guest PCs by Sampled CPU Time\n";
		out << "----------------------------------\n";
		out << "CPU\tPC\tCategory\tName\tSamples\tDirect\tHelper\tSharePercent\n";
		for (size_t i = 0; i < std::min<size_t>(sorted_pcs.size(), 120); i++)
		{
			const TimePcStat& stat = sorted_pcs[i];
			const double share = captured_samples ? (static_cast<double>(stat.samples) * 100.0 / captured_samples) : 0.0;
			out << stat.cpu << '\t' << Hex8(stat.guest_pc) << '\t' << stat.category << '\t' << stat.name << '\t'
				<< stat.samples << '\t' << stat.direct_samples << '\t' << stat.helper_samples << '\t'
				<< std::fixed << std::setprecision(3) << share << "\n";
		}
		out << "\n";

		out << "Top Native/Unmapped Symbols\n";
		out << "---------------------------\n";
		out << "Name\tSamples\tSharePercent\n";
		for (size_t i = 0; i < std::min<size_t>(sorted_native.size(), 80); i++)
		{
			const double share = captured_samples ? (static_cast<double>(sorted_native[i].second) * 100.0 / captured_samples) : 0.0;
			out << sorted_native[i].first << '\t' << sorted_native[i].second << '\t'
				<< std::fixed << std::setprecision(3) << share << "\n";
		}
		out << "\n";
	}

	void AppendHeader(std::ostringstream& out, const std::vector<JitBlockProfile>& profiles)
	{
		const auto now = std::chrono::steady_clock::now();
		const double duration_seconds = std::chrono::duration<double>(now - s_start_time).count();
		const u64 end_frame = PerformanceMetrics::GetFrameNumber();

		out << "=========================================\n";
		out << "        EmuCoreX JIT Profiler Report     \n";
		out << "=========================================\n\n";
		out << "Purpose: identify JIT opcodes and native helpers consuming real CPU time in a captured gameplay scene.\n";
		out << "Method: periodic per-thread CPU-time PC sampling mapped back to guest opcodes through recorded ARM64 host ranges.\n";
		out << "Note: sampling adds no instructions to generated JIT blocks; results are statistical and improve with longer captures.\n\n";

		out << "Game\n";
		out << "----\n";
		out << "Title: " << VMManager::GetTitle(true) << "\n";
		out << "Serial: " << VMManager::GetDiscSerial() << "\n";
		out << "Disc CRC: " << Hex8(VMManager::GetDiscCRC()) << "\n";
		out << "Current CRC: " << Hex8(VMManager::GetCurrentCRC()) << "\n";
		out << "ELF: " << VMManager::GetDiscELF() << "\n\n";

		out << "Capture\n";
		out << "-------\n";
		out << "Duration: " << std::fixed << std::setprecision(2) << duration_seconds << " seconds\n";
		out << "Frames: " << static_cast<unsigned long long>(end_frame - s_start_frame) << "\n";
		out << "Current speed: " << std::setprecision(1) << PerformanceMetrics::GetSpeed() << "%\n";
		out << "Current VPS: " << std::setprecision(2) << PerformanceMetrics::GetFPS() << "\n";
		out << "Current frame min/avg/max: "
			<< PerformanceMetrics::GetMinimumFrameTime() << " / "
			<< PerformanceMetrics::GetAverageFrameTime() << " / "
			<< PerformanceMetrics::GetMaximumFrameTime() << " ms\n";
		out << "Current EE/GS/VU thread usage: "
			<< std::setprecision(1) << PerformanceMetrics::GetCPUThreadUsage() << "% / "
			<< PerformanceMetrics::GetGSThreadUsage() << "% / "
			<< PerformanceMetrics::GetVUThreadUsage() << "%\n";
		out << "Compiled profile records: " << profiles.size() << "\n\n";
	}

	void AppendCompilationHotspots(std::ostringstream& out)
	{
		std::vector<CompileEvent> events;
		std::vector<CacheResetEvent> reset_events;
		{
			std::lock_guard<std::mutex> lock(s_compile_mutex);
			// Report generation runs after collection has stopped. Transfer the
			// backing allocations so they are released when this function returns.
			events.swap(s_compile_events);
			reset_events.swap(s_cache_reset_events);
		}

		out << "Compilation Hotspots\n";
		out << "--------------------\n";
		if (events.empty())
		{
			out << "No compile events captured.\n\n";
			return;
		}

		std::array<CompileCpuTotals, 4> cpu_totals = {};
		std::unordered_map<u64, CompilePcAggregate> pc_map;
		std::unordered_map<u64, CompileFrameAggregate> frame_map;

		for (const CompileEvent& event : events)
		{
			const int type = (event.type >= 0 && event.type < 4) ? event.type : 0;
			CompileCpuTotals& cpu = cpu_totals[type];
			cpu.blocks++;
			cpu.guest_slots += event.guest_size;
			cpu.host_bytes += event.host_size;

			const u64 pc_key = (static_cast<u64>(type) << 32) | event.startpc;
			CompilePcAggregate& pc = pc_map[pc_key];
			pc.type = type;
			pc.pc = event.startpc;
			pc.blocks++;
			pc.guest_slots += event.guest_size;
			pc.host_bytes += event.host_size;
			pc.first_frame = std::min(pc.first_frame, event.frame);
			pc.last_frame = std::max(pc.last_frame, event.frame);

			CompileFrameAggregate& frame = frame_map[event.frame];
			frame.frame = event.frame;
			frame.blocks++;
			frame.host_bytes += event.host_size;
			frame.cpu_blocks[type]++;
		}

		for (const auto& [_, frame] : frame_map)
		{
			for (size_t type = 0; type < cpu_totals.size(); type++)
			{
				if (frame.cpu_blocks[type] == 0)
					continue;
				cpu_totals[type].frames++;
				if (frame.cpu_blocks[type] > cpu_totals[type].peak_frame_blocks)
				{
					cpu_totals[type].peak_frame_blocks = frame.cpu_blocks[type];
					cpu_totals[type].peak_frame_host_bytes = frame.host_bytes;
					cpu_totals[type].peak_frame = frame.frame;
				}
			}
		}

		out << "CPU   Blocks      Frames      GuestSlots      HostBytes       PeakFrame  PeakBlocks  PeakHostBytes\n";
		for (size_t type = 0; type < cpu_totals.size(); type++)
		{
			const CompileCpuTotals& cpu = cpu_totals[type];
			out << std::left << std::setw(5) << CpuName(static_cast<int>(type))
				<< std::right << std::setw(12) << static_cast<unsigned long long>(cpu.blocks)
				<< std::setw(12) << static_cast<unsigned long long>(cpu.frames)
				<< std::setw(16) << static_cast<unsigned long long>(cpu.guest_slots)
				<< std::setw(16) << static_cast<unsigned long long>(cpu.host_bytes)
				<< std::setw(11) << static_cast<unsigned long long>(cpu.peak_frame)
				<< std::setw(12) << static_cast<unsigned long long>(cpu.peak_frame_blocks)
				<< std::setw(16) << static_cast<unsigned long long>(cpu.peak_frame_host_bytes)
				<< "\n";
		}

		std::array<u64, 4> reset_counts = {};
		std::array<u64, 4> reset_bytes = {};
		for (const CacheResetEvent& reset : reset_events)
		{
			const int type = (reset.type >= 0 && reset.type < 4) ? reset.type : 0;
			reset_counts[type]++;
			reset_bytes[type] += reset.discarded_host_bytes;
		}
		out << "\nCode Cache Resets\n";
		out << "CPU      Resets      DiscardedHostBytes\n";
		for (size_t type = 0; type < reset_counts.size(); type++)
		{
			out << std::left << std::setw(5) << CpuName(static_cast<int>(type))
				<< std::right << std::setw(12) << static_cast<unsigned long long>(reset_counts[type])
				<< std::setw(24) << static_cast<unsigned long long>(reset_bytes[type]) << "\n";
		}
		out << "Compile events dropped at safety cap: "
			<< static_cast<unsigned long long>(s_compile_events_dropped.load(std::memory_order_relaxed)) << "\n";

		std::vector<CompileFrameAggregate> frames;
		frames.reserve(frame_map.size());
		for (const auto& [_, frame] : frame_map)
			frames.push_back(frame);
		std::sort(frames.begin(), frames.end(), [](const CompileFrameAggregate& a, const CompileFrameAggregate& b) {
			if (a.blocks != b.blocks)
				return a.blocks > b.blocks;
			return a.host_bytes > b.host_bytes;
		});

		out << "\nTop Compile Burst Frames\n";
		out << "Frame      Blocks      HostBytes       EE     IOP    VU0    VU1\n";
		for (size_t i = 0; i < std::min<size_t>(frames.size(), 24); i++)
		{
			const CompileFrameAggregate& frame = frames[i];
			out << std::setw(10) << static_cast<unsigned long long>(frame.frame)
				<< std::setw(12) << static_cast<unsigned long long>(frame.blocks)
				<< std::setw(16) << static_cast<unsigned long long>(frame.host_bytes)
				<< std::setw(7) << static_cast<unsigned long long>(frame.cpu_blocks[0])
				<< std::setw(7) << static_cast<unsigned long long>(frame.cpu_blocks[1])
				<< std::setw(7) << static_cast<unsigned long long>(frame.cpu_blocks[2])
				<< std::setw(7) << static_cast<unsigned long long>(frame.cpu_blocks[3])
				<< "\n";
		}

		std::vector<CompilePcAggregate> pcs;
		pcs.reserve(pc_map.size());
		for (const auto& [_, pc] : pc_map)
			pcs.push_back(pc);
		std::sort(pcs.begin(), pcs.end(), [](const CompilePcAggregate& a, const CompilePcAggregate& b) {
			if (a.blocks != b.blocks)
				return a.blocks > b.blocks;
			return a.host_bytes > b.host_bytes;
		});

		out << "\nTop Compile PCs\n";
		out << "CPU   PC          Blocks      GuestSlots      HostBytes       FirstFrame LastFrame\n";
		for (size_t i = 0; i < std::min<size_t>(pcs.size(), 80); i++)
		{
			const CompilePcAggregate& pc = pcs[i];
			out << std::left << std::setw(5) << CpuName(pc.type)
				<< std::right << FormatPc(pc.type, pc.pc) << "  "
				<< std::setw(10) << static_cast<unsigned long long>(pc.blocks)
				<< std::setw(16) << static_cast<unsigned long long>(pc.guest_slots)
				<< std::setw(16) << static_cast<unsigned long long>(pc.host_bytes)
				<< std::setw(11) << static_cast<unsigned long long>(pc.first_frame)
				<< std::setw(10) << static_cast<unsigned long long>(pc.last_frame)
				<< "\n";
		}
		out << "\n";
	}

	BlockAnalysis AnalyzeBlock(
		const JitBlockProfile& block,
		std::array<CpuTotals, 4>& totals,
		std::unordered_map<std::string, HotStat>& opcode_stats,
		std::unordered_map<std::string, HotStat>& category_stats)
	{
		BlockAnalysis analysis;
		analysis.profile = block;
		const int type = (block.type >= 0 && block.type < 4) ? block.type : 0;
		CpuTotals& cpu = totals[type];
		const u64 execs = block.execution_count;
		const u64 slot_count = block.size;
		const u64 dynamic_slots = execs * slot_count;
		const u64 dynamic_host_bytes = execs * static_cast<u64>(block.host_size);
		const u32 guest_bytes = block.size * GuestBytesPerSlot(type);

		cpu.compiled_blocks++;
		cpu.guest_instruction_slots += slot_count;
		cpu.host_bytes += block.host_size;
		if (execs == 0)
		{
			cpu.zero_count_blocks++;
			return analysis;
		}

		cpu.executed_blocks++;
		cpu.executions += execs;
		cpu.dynamic_instruction_slots += dynamic_slots;
		cpu.estimated_dynamic_host_bytes += dynamic_host_bytes;

		analysis.dynamic_guest_slots = dynamic_slots;
		analysis.estimated_dynamic_host_bytes = dynamic_host_bytes;
		analysis.host_expansion = (guest_bytes > 0) ? static_cast<double>(block.host_size) / static_cast<double>(guest_bytes) : 0.0;

		const std::string cpu_name = CpuName(block.type);
		if (block.type == 0 || block.type == 1)
		{
			u32 live_mips_slots = 0;
			for (u32 i = 0; i < block.size; i++)
			{
				const u32 pc = block.startpc + i * 4;
				u32 code = 0;
				if (ReadGuest32(block.type, pc, code) && MipsEmitsHostCode(code))
					live_mips_slots++;
			}

			const u64 estimated_host_per_slot = (live_mips_slots > 0) ? std::max<u64>(1, block.host_size / live_mips_slots) : 0;
			for (u32 i = 0; i < block.size; i++)
			{
				const u32 pc = block.startpc + i * 4;
				u32 code = 0;
				if (!ReadGuest32(block.type, pc, code))
				{
					analysis.unreadable_slots++;
					cpu.unreadable_instruction_slots++;
					continue;
				}

				const std::string disasm = (block.type == 0) ? DisassembleEE(code, pc) : DisassembleIOP(code, pc);
				const std::string mnemonic = TrimMnemonic(disasm);
				const std::string category = (block.type == 0) ? ClassifyEE(code) : ClassifyIOP(code);
				const u64 host_estimate = MipsEmitsHostCode(code) ? (execs * estimated_host_per_slot) : 0;
				AddHotStat(opcode_stats, cpu_name, category, mnemonic, execs, host_estimate);
				AddHotStat(category_stats, cpu_name, category, category, execs, host_estimate);

				if (analysis.disassembly.size() < 48)
				{
					std::ostringstream line;
					line << "  " << Hex8(pc) << ": " << Hex8(code) << "  " << disasm;
					analysis.disassembly.push_back(line.str());
				}
			}
			analysis.dynamic_guest_ops = analysis.dynamic_guest_slots;
		}
		else if (block.type == 2 || block.type == 3)
		{
			u32 live_vu_weight = 0;
			for (u32 i = 0; i < block.size; i++)
			{
				const u32 offset = block.startpc + i * 8;
				u32 upper = 0;
				u32 lower = 0;
				if (!ReadVUPair(block.type, offset, upper, lower))
					continue;

				live_vu_weight += VUUpperHostWeight(upper);
				live_vu_weight += VULowerHostWeight(lower);
			}

			const u64 estimated_host_per_weight = (live_vu_weight > 0) ? std::max<u64>(1, block.host_size / live_vu_weight) : 0;
			for (u32 i = 0; i < block.size; i++)
			{
				const u32 offset = block.startpc + i * 8;
				u32 upper = 0;
				u32 lower = 0;
				if (!ReadVUPair(block.type, offset, upper, lower))
				{
					analysis.unreadable_slots++;
					cpu.unreadable_instruction_slots++;
					continue;
				}

				const u32 index = offset / 8;
				const std::string upper_disasm = NormalizeVUUpperDisassembly(upper, DisassembleVUUpper(block.type, upper, index));
				const std::string lower_disasm = DisassembleVULower(block.type, lower, index);
				const std::string upper_category = cpu_name + "_UPPER";
				const std::string lower_category = cpu_name + "_LOWER";
				const u64 upper_host_estimate = execs * estimated_host_per_weight * VUUpperHostWeight(upper);
				const u64 lower_host_estimate = execs * estimated_host_per_weight * VULowerHostWeight(lower);
				AddHotStat(opcode_stats, cpu_name, upper_category, TrimMnemonic(upper_disasm), execs, upper_host_estimate);
				AddHotStat(opcode_stats, cpu_name, lower_category, TrimMnemonic(lower_disasm), execs, lower_host_estimate);
				AddHotStat(category_stats, cpu_name, upper_category, upper_category, execs, upper_host_estimate);
				AddHotStat(category_stats, cpu_name, lower_category, lower_category, execs, lower_host_estimate);

				if (analysis.disassembly.size() < 48)
				{
					std::ostringstream line;
					line << "  " << std::hex << std::setw(4) << std::setfill('0') << index << std::setfill(' ')
						<< ": upper=" << Hex8(upper) << " lower=" << Hex8(lower)
						<< "  " << upper_disasm << " | " << lower_disasm;
					analysis.disassembly.push_back(line.str());
				}
			}
			analysis.dynamic_guest_ops = analysis.dynamic_guest_slots * 2;
		}

		return analysis;
	}

	std::vector<PcAggregate> BuildPcAggregates(const std::vector<BlockAnalysis>& blocks)
	{
		std::unordered_map<std::string, PcAggregate> map;
		for (const BlockAnalysis& block : blocks)
		{
			if (block.profile.execution_count == 0)
				continue;

			const std::string key = std::string(CpuName(block.profile.type)) + '\n' + std::to_string(block.profile.startpc);
			PcAggregate& agg = map[key];
			if (agg.cpu.empty())
			{
				agg.cpu = CpuName(block.profile.type);
				agg.pc = block.profile.startpc;
			}
			agg.variants++;
			agg.executions += block.profile.execution_count;
			agg.dynamic_guest_ops += block.dynamic_guest_ops;
			agg.estimated_dynamic_host_bytes += block.estimated_dynamic_host_bytes;
			agg.max_host_size = std::max(agg.max_host_size, block.profile.host_size);
			agg.max_expansion = std::max(agg.max_expansion, block.host_expansion);
		}

		std::vector<PcAggregate> out;
		out.reserve(map.size());
		for (const auto& entry : map)
			out.push_back(entry.second);

		std::sort(out.begin(), out.end(), [](const PcAggregate& a, const PcAggregate& b) {
			if (a.estimated_dynamic_host_bytes != b.estimated_dynamic_host_bytes)
				return a.estimated_dynamic_host_bytes > b.estimated_dynamic_host_bytes;
			return a.dynamic_guest_ops > b.dynamic_guest_ops;
		});
		return out;
	}

	std::vector<VuNormalizedAggregate> BuildVuNormalizedAggregates(const std::vector<BlockAnalysis>& blocks)
	{
		std::unordered_map<std::string, VuNormalizedAggregate> map;
		for (const BlockAnalysis& block : blocks)
		{
			if (block.profile.execution_count == 0)
				continue;
			if (block.profile.type != 2 && block.profile.type != 3)
				continue;

			const u64 normalized_state = VuNormalizeQuickState(block.profile.state_hash);
			const std::string key = std::string(CpuName(block.profile.type)) + '\n' +
			                        std::to_string(block.profile.startpc) + '\n' +
			                        std::to_string(normalized_state);
			VuNormalizedAggregate& agg = map[key];
			if (agg.cpu.empty())
			{
				agg.cpu = CpuName(block.profile.type);
				agg.pc = block.profile.startpc;
				agg.normalized_state = normalized_state;
			}
			agg.variants++;
			agg.executions += block.profile.execution_count;
			agg.dynamic_guest_ops += block.dynamic_guest_ops;
			agg.estimated_dynamic_host_bytes += block.estimated_dynamic_host_bytes;
			agg.min_host_size = std::min(agg.min_host_size, block.profile.host_size);
			agg.max_host_size = std::max(agg.max_host_size, block.profile.host_size);
			agg.max_expansion = std::max(agg.max_expansion, block.host_expansion);
			agg.flag_infos[VuStateByte(block.profile.state_hash, 1)] = true;
			agg.vi_backups[VuStateByte(block.profile.state_hash, 5)] = true;
		}

		std::vector<VuNormalizedAggregate> out;
		out.reserve(map.size());
		for (const auto& entry : map)
			out.push_back(entry.second);

		std::sort(out.begin(), out.end(), [](const VuNormalizedAggregate& a, const VuNormalizedAggregate& b) {
			if (a.estimated_dynamic_host_bytes != b.estimated_dynamic_host_bytes)
				return a.estimated_dynamic_host_bytes > b.estimated_dynamic_host_bytes;
			if (a.variants != b.variants)
				return a.variants > b.variants;
			return a.executions > b.executions;
		});
		return out;
	}

	void AppendFindings(
		std::ostringstream& out,
		const std::array<CpuTotals, 4>& totals,
		const std::vector<PcAggregate>& pc_aggregates,
		const std::vector<VuNormalizedAggregate>& normalized_aggregates,
		const std::vector<HotStat>& opcode_stats)
	{
		out << "Profiler Findings\n";
		out << "-----------------\n";

		const u64 total_dynamic_host =
			totals[0].estimated_dynamic_host_bytes +
			totals[1].estimated_dynamic_host_bytes +
			totals[2].estimated_dynamic_host_bytes +
			totals[3].estimated_dynamic_host_bytes;
		if (total_dynamic_host > 0)
		{
			for (int i = 0; i < 4; i++)
			{
				const double pct = (static_cast<double>(totals[i].estimated_dynamic_host_bytes) * 100.0) /
				                   static_cast<double>(total_dynamic_host);
				out << "- " << CpuName(i) << " estimated dynamic host bytes: "
					<< std::fixed << std::setprecision(1) << pct << "%\n";
			}
		}

		int suspicious_vu = 0;
		for (const PcAggregate& agg : pc_aggregates)
		{
			if (agg.cpu != "VU0" && agg.cpu != "VU1")
				continue;
			if (agg.max_expansion < 1000.0 && agg.variants < 4)
				continue;
			if (++suspicious_vu > 8)
				break;
			out << "- Suspicious " << agg.cpu << " pc=" << Hex8(agg.pc)
				<< " variants=" << agg.variants
				<< " max_host=" << agg.max_host_size
				<< " max_expansion=" << std::fixed << std::setprecision(1) << agg.max_expansion
				<< " dyn_host=" << static_cast<unsigned long long>(agg.estimated_dynamic_host_bytes) << "\n";
		}

		int normalized_vu = 0;
		for (const VuNormalizedAggregate& agg : normalized_aggregates)
		{
			if (agg.variants < 4)
				continue;
			if (++normalized_vu > 6)
				break;
			out << "- VU quick-state split candidate: " << agg.cpu << " pc=" << Hex8(agg.pc)
				<< " normalized=" << Hex16(agg.normalized_state)
				<< " variants=" << agg.variants
				<< " flagInfo={" << ByteSetText(agg.flag_infos) << "}"
				<< " viBackUp={" << ByteSetText(agg.vi_backups) << "}"
				<< " dyn_host=" << static_cast<unsigned long long>(agg.estimated_dynamic_host_bytes)
				<< " host_minmax=" << agg.min_host_size << "/" << agg.max_host_size << "\n";
		}

		int bad_ops = 0;
		for (const HotStat& stat : opcode_stats)
		{
			if (stat.name.find("***") == std::string::npos && stat.name.find("Bad") == std::string::npos)
				continue;
			if (++bad_ops > 6)
				break;
			out << "- Bad/unknown disasm hotspot: " << stat.cpu << ' ' << stat.category
				<< ' ' << stat.name << " dyn=" << static_cast<unsigned long long>(stat.dynamic_count) << "\n";
		}

		out << "\n";
	}

	void AppendCpuSummary(std::ostringstream& out, const std::array<CpuTotals, 4>& totals)
	{
		out << "CPU Summary\n";
		out << "-----------\n";
		out << std::left
			<< std::setw(6) << "CPU"
			<< std::setw(12) << "Blocks"
			<< std::setw(12) << "HotBlocks"
			<< std::setw(16) << "BlockExecs"
			<< std::setw(18) << "DynGuestSlots"
			<< std::setw(16) << "HostBytes"
			<< std::setw(18) << "DynHostBytes"
			<< "Unreadable\n";
		for (int i = 0; i < 4; i++)
		{
			const CpuTotals& t = totals[i];
			out << std::left
				<< std::setw(6) << CpuName(i)
				<< std::setw(12) << static_cast<unsigned long long>(t.compiled_blocks)
				<< std::setw(12) << static_cast<unsigned long long>(t.executed_blocks)
				<< std::setw(16) << static_cast<unsigned long long>(t.executions)
				<< std::setw(18) << static_cast<unsigned long long>(t.dynamic_instruction_slots)
				<< std::setw(16) << static_cast<unsigned long long>(t.host_bytes)
				<< std::setw(18) << static_cast<unsigned long long>(t.estimated_dynamic_host_bytes)
				<< static_cast<unsigned long long>(t.unreadable_instruction_slots) << "\n";
		}
		out << "\n";
	}

	void AppendHotStats(std::ostringstream& out, const char* title, const std::vector<HotStat>& stats, int limit)
	{
		out << title << "\n";
		out << std::string(std::strlen(title), '-') << "\n";
		out << "CPU\tCategory\tName\tDynamicCount\tStaticSlots\tEstimatedDynamicHostBytes\n";
		int written = 0;
		for (const HotStat& stat : stats)
		{
			if (stat.dynamic_count == 0)
				continue;
			if (++written > limit)
				break;
			out << stat.cpu << '\t'
				<< stat.category << '\t'
				<< stat.name << '\t'
				<< static_cast<unsigned long long>(stat.dynamic_count) << '\t'
				<< static_cast<unsigned long long>(stat.static_slots) << '\t'
				<< static_cast<unsigned long long>(stat.estimated_dynamic_host_bytes) << "\n";
		}
		out << "\n";
	}

	template <typename Sorter>
	void AppendBlockTable(std::ostringstream& out, const char* title, std::vector<BlockAnalysis> blocks, Sorter sorter, int limit)
	{
		std::sort(blocks.begin(), blocks.end(), sorter);
		out << title << "\n";
		out << std::string(std::strlen(title), '-') << "\n";
		out << std::left
			<< std::setw(6) << "CPU"
			<< std::setw(12) << "PC"
			<< std::setw(14) << "Execs"
			<< std::setw(8) << "Slots"
			<< std::setw(12) << "HostBytes"
			<< std::setw(14) << "DynSlots"
			<< std::setw(16) << "DynHostBytes"
			<< std::setw(11) << "Expansion"
			<< std::setw(8) << "Variant"
			<< std::setw(20) << "State"
			<< std::setw(18) << "Flags"
			<< "StateFields\n";

		int written = 0;
		for (const BlockAnalysis& block : blocks)
		{
			if (block.profile.execution_count == 0)
				continue;
			if (++written > limit)
				break;
			out << std::left
				<< std::setw(6) << CpuName(block.profile.type)
				<< std::setw(12) << Hex8(block.profile.startpc)
				<< std::setw(14) << static_cast<unsigned long long>(block.profile.execution_count)
				<< std::setw(8) << block.profile.size
				<< std::setw(12) << block.profile.host_size
				<< std::setw(14) << static_cast<unsigned long long>(block.dynamic_guest_slots)
				<< std::setw(16) << static_cast<unsigned long long>(block.estimated_dynamic_host_bytes)
				<< std::setw(11) << std::fixed << std::setprecision(2) << block.host_expansion
				<< std::setw(8) << block.profile.variant_index
				<< std::setw(20) << ((block.profile.type == 2 || block.profile.type == 3) ? Hex16(block.profile.state_hash) : "-")
				<< std::setw(18) << VuFlagsText(block.profile)
				<< ((block.profile.type == 2 || block.profile.type == 3) ? VuStateFieldsText(block.profile.state_hash) : "-") << "\n";
		}
		out << "\n";
	}

	void AppendPcAggregateTable(std::ostringstream& out, const std::vector<PcAggregate>& aggregates)
	{
		out << "Top PCs Aggregated Across Variants\n";
		out << "----------------------------------\n";
		out << std::left
			<< std::setw(6) << "CPU"
			<< std::setw(12) << "PC"
			<< std::setw(10) << "Variants"
			<< std::setw(16) << "Execs"
			<< std::setw(16) << "DynOps"
			<< std::setw(18) << "DynHostBytes"
			<< std::setw(12) << "MaxHost"
			<< "MaxExpansion\n";
		int written = 0;
		for (const PcAggregate& agg : aggregates)
		{
			if (++written > 120)
				break;
			out << std::left
				<< std::setw(6) << agg.cpu
				<< std::setw(12) << Hex8(agg.pc)
				<< std::setw(10) << agg.variants
				<< std::setw(16) << static_cast<unsigned long long>(agg.executions)
				<< std::setw(16) << static_cast<unsigned long long>(agg.dynamic_guest_ops)
				<< std::setw(18) << static_cast<unsigned long long>(agg.estimated_dynamic_host_bytes)
				<< std::setw(12) << agg.max_host_size
				<< std::fixed << std::setprecision(2) << agg.max_expansion << "\n";
		}
		out << "\n";
	}

	void AppendVuNormalizedAggregateTable(std::ostringstream& out, const std::vector<VuNormalizedAggregate>& aggregates)
	{
		out << "VU Quick-State Variant Groups\n";
		out << "-----------------------------\n";
		out << "NormalizedState masks out flagInfo and viBackUp to expose duplicated quick-state variants.\n";
		out << std::left
			<< std::setw(6) << "CPU"
			<< std::setw(12) << "PC"
			<< std::setw(20) << "NormalizedState"
			<< std::setw(10) << "Variants"
			<< std::setw(16) << "Execs"
			<< std::setw(16) << "DynOps"
			<< std::setw(18) << "DynHostBytes"
			<< std::setw(12) << "MinHost"
			<< std::setw(12) << "MaxHost"
			<< std::setw(14) << "MaxExpansion"
			<< std::setw(20) << "FlagInfos"
			<< "ViBackUps\n";
		int written = 0;
		for (const VuNormalizedAggregate& agg : aggregates)
		{
			if (agg.variants < 2)
				continue;
			if (++written > 120)
				break;
			out << std::left
				<< std::setw(6) << agg.cpu
				<< std::setw(12) << Hex8(agg.pc)
				<< std::setw(20) << Hex16(agg.normalized_state)
				<< std::setw(10) << agg.variants
				<< std::setw(16) << static_cast<unsigned long long>(agg.executions)
				<< std::setw(16) << static_cast<unsigned long long>(agg.dynamic_guest_ops)
				<< std::setw(18) << static_cast<unsigned long long>(agg.estimated_dynamic_host_bytes)
				<< std::setw(12) << agg.min_host_size
				<< std::setw(12) << agg.max_host_size
				<< std::setw(14) << std::fixed << std::setprecision(2) << agg.max_expansion
				<< std::setw(20) << ByteSetText(agg.flag_infos)
				<< ByteSetText(agg.vi_backups) << "\n";
		}
		out << "\n";
	}

	void AppendDisassembly(std::ostringstream& out, std::vector<BlockAnalysis> blocks)
	{
		std::sort(blocks.begin(), blocks.end(), [](const BlockAnalysis& a, const BlockAnalysis& b) {
			if (a.dynamic_guest_ops != b.dynamic_guest_ops)
				return a.dynamic_guest_ops > b.dynamic_guest_ops;
			return a.estimated_dynamic_host_bytes > b.estimated_dynamic_host_bytes;
		});

		out << "Hottest Block Disassembly\n";
		out << "-------------------------\n";
		int written = 0;
		for (const BlockAnalysis& block : blocks)
		{
			if (block.profile.execution_count == 0)
				continue;
			if (++written > 40)
				break;

			out << CpuName(block.profile.type)
				<< " pc=" << Hex8(block.profile.startpc)
				<< " variant=" << block.profile.variant_index
				<< " state=" << ((block.profile.type == 2 || block.profile.type == 3) ? Hex16(block.profile.state_hash) : "-")
				<< " flags=" << VuFlagsText(block.profile)
				<< " execs=" << static_cast<unsigned long long>(block.profile.execution_count)
				<< " slots=" << block.profile.size
				<< " host_bytes=" << block.profile.host_size
				<< " dynamic_ops=" << static_cast<unsigned long long>(block.dynamic_guest_ops)
				<< " expansion=" << std::fixed << std::setprecision(2) << block.host_expansion << "\n";
			if (block.unreadable_slots > 0)
				out << "  unreadable_slots=" << block.unreadable_slots << "\n";
			if (block.profile.type == 2 || block.profile.type == 3)
				out << "  state_fields=" << VuStateFieldsText(block.profile.state_hash) << "\n";
			for (const std::string& line : block.disassembly)
				out << line << "\n";
			if (block.disassembly.size() >= 48 && block.profile.size > 48)
				out << "  ... truncated, block has " << block.profile.size << " slots\n";
			out << "\n";
		}
	}

	void WriteReport()
	{
		std::vector<JitBlockProfile> profiles;
		EE_JitGetBlockProfiles(profiles);
		IOP_JitGetBlockProfiles(profiles);
		VU0_JitGetBlockProfiles(profiles);
		VU1_JitGetBlockProfiles(profiles);

		std::ostringstream report;
		AppendHeader(report, profiles);
		AppendCompilationHotspots(report);
		AppendSamplingHotspots(report);

		const std::string profile_dir = Path::Combine(EmuFolders::DataRoot, "jit_profiles");
		FileSystem::EnsureDirectoryExists(profile_dir.c_str(), false);

		const std::string serial = SanitizeFilePart(VMManager::GetDiscSerial());
		const std::string crc = SanitizeFilePart(Hex8(VMManager::GetCurrentCRC()));
		const std::string timestamped_path = Path::Combine(profile_dir, "jit_profile_" + serial + "_" + crc + "_" + TimestampForFile() + ".txt");
		const std::string latest_path = Path::Combine(EmuFolders::DataRoot, "jit_profile.txt");
		const std::string text = report.str();

		FileSystem::WriteStringToFile(timestamped_path.c_str(), text);
		FileSystem::WriteStringToFile(latest_path.c_str(), text);
	}
} // namespace

bool IsActive()
{
	return s_active.load(std::memory_order_relaxed);
}

void OpcodeRangeScope::Begin(int type, u32 guest_pc, u32 opcode, u32 paired_opcode)
{
	if (m_active || !IsActive() || !s_sampling_active.load(std::memory_order_acquire) ||
		s_opcode_range_limit_hit.load(std::memory_order_relaxed) || !oakAsm)
		return;

	m_active = true;
	m_type = type;
	m_guest_pc = guest_pc;
	m_opcode = opcode;
	m_paired_opcode = paired_opcode;
	m_host_begin = reinterpret_cast<uptr>(oakGetCurrentCodePointer());
}

OpcodeRangeScope::~OpcodeRangeScope()
{
	End();
}

void OpcodeRangeScope::End()
{
	if (!m_active || !oakAsm)
		return;

	m_active = false;
	RecordOpcodeRange(
		m_type,
		m_guest_pc,
		m_opcode,
		m_paired_opcode,
		m_host_begin,
		reinterpret_cast<uptr>(oakGetCurrentCodePointer()));
}

void RecordBlockCompile(int type, u32 startpc, u32 guest_size, u32 host_size)
{
	if (!IsActive())
		return;

	CompileEvent event;
	event.type = type;
	event.startpc = startpc;
	event.guest_size = guest_size;
	event.host_size = host_size;
	event.frame = PerformanceMetrics::GetFrameNumber() - s_start_frame;
	event.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_start_time).count();

	std::lock_guard<std::mutex> lock(s_compile_mutex);
	if (s_compile_events.size() >= COMPILE_EVENT_CAPACITY)
	{
		s_compile_events_dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	s_compile_events.push_back(event);
}

void RecordCodeCacheReset(int type, u64 discarded_host_bytes)
{
	if (!s_collecting_runtime.load(std::memory_order_acquire) || discarded_host_bytes == 0)
		return;

	CacheResetEvent event;
	event.type = type;
	event.discarded_host_bytes = discarded_host_bytes;
	event.frame = PerformanceMetrics::GetFrameNumber() - s_start_frame;

	std::lock_guard<std::mutex> lock(s_compile_mutex);
	s_cache_reset_events.push_back(event);
}

void EmitBlockIncrement(void* counter_ptr)
{
	// Statistical sampling must not perturb short blocks with runtime counter instructions.
	(void)counter_ptr;
}

void Start()
{
	if (s_active.exchange(true))
		return;

	{
		std::lock_guard<std::mutex> lock(s_compile_mutex);
		s_compile_events.clear();
		s_cache_reset_events.clear();
	}
	{
		std::lock_guard<std::mutex> lock(s_opcode_range_mutex);
		s_opcode_ranges.clear();
	}
	s_compile_events_dropped.store(0, std::memory_order_relaxed);
	s_opcode_range_limit_hit.store(false, std::memory_order_relaxed);
	s_collecting_runtime.store(false, std::memory_order_release);

	Host::RunOnCPUThread([]() {
		if (THREAD_VU1)
			vu1Thread.WaitVU();
#if defined(__ANDROID__) && defined(__aarch64__)
		s_cpu_sample_handle = Threading::ThreadHandle::GetForCallingThread();
		s_vu_sample_handle = (THREAD_VU1 && vu1Thread.IsOpen()) ? vu1Thread.GetThreadHandle() : Threading::ThreadHandle();
		s_cpu_sample_target = static_cast<pthread_t>(reinterpret_cast<uintptr_t>(static_cast<void*>(s_cpu_sample_handle)));
		const void* const vu_handle = static_cast<void*>(s_vu_sample_handle);
		s_vu_sample_target = vu_handle ? static_cast<pthread_t>(reinterpret_cast<uintptr_t>(vu_handle)) : pthread_t{};
#endif
		recCpu.Reset();
		psxRec.Reset();
		CpuMicroVU0.Reset();
		CpuMicroVU1.Reset();
	}, true);

	s_start_time = std::chrono::steady_clock::now();
	s_start_frame = PerformanceMetrics::GetFrameNumber();
	s_collecting_runtime.store(true, std::memory_order_release);
	StartPcSampler();
}

void Stop()
{
	if (!s_active.exchange(false))
		return;
	s_collecting_runtime.store(false, std::memory_order_release);
	StopPcSampler();

	Host::RunOnCPUThread([]() {
		if (THREAD_VU1)
			vu1Thread.WaitVU();
		WriteReport();

		recCpu.Reset();
		psxRec.Reset();
		CpuMicroVU0.Reset();
		CpuMicroVU1.Reset();
	}, true);

	s_raw_samples = nullptr;
	s_sample_buffer.reset();
}
} // namespace JitProfiler
