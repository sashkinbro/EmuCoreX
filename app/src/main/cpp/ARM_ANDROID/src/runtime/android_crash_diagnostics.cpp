#include "common/AndroidLogging.h"
#include "emucorex/android_crash_diagnostics.h"
#include "emucorex/android_signal_chain.h"

#include "pcsx2/Gif.h"
#include "pcsx2/GS.h"
#include "pcsx2/Hw.h"
#include "pcsx2/Dmac.h"
#include "pcsx2/Memory.h"
#include "pcsx2/R3000A.h"
#include "pcsx2/R5900.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/VU.h"
#include "pcsx2/VifDef.h"
#include "pcsx2/VUmicro.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/arm64/cpuRegistersPack-arm64.h"

#include <android/log.h>
#include <dlfcn.h>

#if EMUCOREX_HAS_CAPSTONE
#include <capstone/capstone.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>

namespace emucorex::android
{
namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";

std::atomic<u64> s_launch_id{0};
std::atomic<u64> s_execute_generation{0};
std::atomic<int> s_probe_steps{0};
std::atomic_bool s_boot_elf{false};
char s_launch_path[384] = {};
char s_execute_phase[32] = "not-started";
char s_game_title[160] = {};
char s_game_serial[64] = {};
std::atomic<u32> s_disc_crc{0};
std::atomic<u32> s_current_crc{0};
// The TFX selector currently being compiled. If the PowerVR driver aborts inside
// vkCreateGraphicsPipelines, this identifies the exact pipeline variant.
std::atomic_bool s_tfx_selector_valid{false};
std::atomic<u32> s_tfx_vs_key{0};
std::atomic<u64> s_tfx_ps_key_hi{0};
std::atomic<u64> s_tfx_ps_key_lo{0};

// ─── Native crash log file ─────────────────────────────────────────────────
char s_log_file_path[512] = {};
struct sigaction s_prev_sigsegv = {};
struct sigaction s_prev_sigabrt = {};
struct sigaction s_prev_sigbus  = {};
struct sigaction s_prev_sigfpe  = {};
std::atomic_bool s_signal_handler_installed{false};
std::atomic_bool s_in_signal_handler{false};

// Async-signal-safe timestamp
void FormatTimestampSafe(char* buf, size_t len)
{
	time_t now = time(nullptr);
	struct tm tm_info = {};
	gmtime_r(&now, &tm_info);
	strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
}

// Async-signal-safe append to log file using only write()
void WriteToLogFile(const char* level, const char* msg)
{
	if (s_log_file_path[0] == '\0')
		return;

	int fd = open(s_log_file_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (fd < 0)
		return;

	char ts[32];
	FormatTimestampSafe(ts, sizeof(ts));

	write(fd, "[", 1);
	write(fd, ts, strlen(ts));
	write(fd, "] ", 2);
	write(fd, level, strlen(level));
	write(fd, ": ", 2);
	write(fd, msg, strlen(msg));
	write(fd, "\n", 1);
	close(fd);
}

void WriteToLogFileFmt(const char* level, const char* fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	WriteToLogFile(level, buf);
}

void NativeCrashSignalHandler(int sig, siginfo_t* info, void* ctx)
{
    const int saved_errno = errno;
    if (s_in_signal_handler.exchange(true, std::memory_order_acq_rel))
    {
        _exit(128 + sig);
        return;
    }

    // Keep the fatal path independent of malloc, loader locks and stack unwinders.
    // debuggerd receives the original ucontext below and owns the full tombstone.
    if (s_log_file_path[0] != '\0')
    {
        const int fd = open(s_log_file_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0)
        {
            const SignalMetadata metadata{
                s_launch_id.load(std::memory_order_relaxed), s_execute_generation.load(std::memory_order_relaxed),
                s_tfx_selector_valid.load(std::memory_order_acquire) ? 1u : 0u,
                s_tfx_vs_key.load(std::memory_order_relaxed), s_tfx_ps_key_hi.load(std::memory_order_relaxed),
                s_tfx_ps_key_lo.load(std::memory_order_relaxed)};
            WriteSignalContext(fd, sig, info, ctx, &metadata);
            const int maps = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
            if (maps >= 0)
            {
                static constexpr char header[] = "NATIVE_CRASH_MAPS\n";
                (void)write(fd, header, sizeof(header) - 1);
                char chunk[4096];
                size_t remaining = 256 * 1024;
                while (remaining)
                {
                    const ssize_t count = read(maps, chunk, remaining < sizeof(chunk) ? remaining : sizeof(chunk));
                    if (count < 0 && errno == EINTR) continue;
                    if (count <= 0) break;
                    size_t sent = 0;
                    while (sent < static_cast<size_t>(count))
                    {
                        const ssize_t result = write(fd, chunk + sent, static_cast<size_t>(count) - sent);
                        if (result < 0 && errno == EINTR) continue;
                        if (result <= 0) break;
                        sent += static_cast<size_t>(result);
                    }
                    remaining -= static_cast<size_t>(count);
                    if (sent < static_cast<size_t>(count)) break;
                }
                close(maps);
            }
            close(fd);
        }
    }

    struct sigaction* prev = nullptr;
    switch (sig)
    {
        case SIGSEGV: prev = &s_prev_sigsegv; break;
        case SIGABRT: prev = &s_prev_sigabrt; break;
        case SIGBUS: prev = &s_prev_sigbus; break;
        case SIGFPE: prev = &s_prev_sigfpe; break;
        default: break;
    }
    errno = saved_errno;
    if (prev)
        HandOffFatalSignal(sig, info, ctx, *prev);
    s_in_signal_handler.store(false, std::memory_order_release);
}

void CopyBreadcrumb(char* dest, size_t size, const char* source)
{
	if (size == 0)
		return;

	if (!source)
		source = "";

	std::snprintf(dest, size, "%s", source);
}

void CopyBreadcrumb(char* dest, size_t size, const std::string& source)
{
	CopyBreadcrumb(dest, size, source.c_str());
}

bool AddressInRange(const void* address, const u8* start, const u8* end)
{
	const auto value = reinterpret_cast<std::uintptr_t>(address);
	return value >= reinterpret_cast<std::uintptr_t>(start) && value < reinterpret_cast<std::uintptr_t>(end);
}

const char* ClassifyNativeAddress(const void* address)
{
	if (!address)
		return "null";

	if (AddressInRange(address, SysMemory::GetEERec(), SysMemory::GetEERecEnd()))
		return "EE-rec";
	if (AddressInRange(address, SysMemory::GetIOPRec(), SysMemory::GetIOPRecEnd()))
		return "IOP-rec";
	if (AddressInRange(address, SysMemory::GetVU0Rec(), SysMemory::GetVU0RecEnd()))
		return "VU0-rec";
	if (AddressInRange(address, SysMemory::GetVU1Rec(), SysMemory::GetVU1RecEnd()))
		return "VU1-rec";
	if (AddressInRange(address, SysMemory::GetVIFUnpackRec(), SysMemory::GetVIFUnpackRecEnd()))
		return "VIF-unpack-rec";
	if (AddressInRange(address, SysMemory::GetSWRec(), SysMemory::GetSWRecEnd()))
		return "GS-SW-rec";
	if (AddressInRange(address, SysMemory::GetEEMem(), SysMemory::GetEEMemEnd()))
		return "EE-memory";
	if (AddressInRange(address, SysMemory::GetIOPMem(), SysMemory::GetIOPMemEnd()))
		return "IOP-memory";
	if (AddressInRange(address, SysMemory::GetVUMem(), SysMemory::GetVUMemEnd()))
		return "VU-memory";

	return "native";
}

void LogModuleForAddress(const char* label, const void* address)
{
	Dl_info info = {};
	if (address && dladdr(address, &info) != 0)
	{
		const auto offset = reinterpret_cast<std::uintptr_t>(address) -
			reinterpret_cast<std::uintptr_t>(info.dli_fbase);
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
			"Crash diag %s=%p class=%s module=%s+0x%zx symbol=%s",
			label, address, ClassifyNativeAddress(address),
			info.dli_fname ? info.dli_fname : "<unknown>",
			static_cast<size_t>(offset),
			info.dli_sname ? info.dli_sname : "<unknown>");
	}
	else
	{
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
			"Crash diag %s=%p class=%s module=<unmapped>",
			label, address, ClassifyNativeAddress(address));
	}
}

void DumpCodeAroundAddress(const void* address)
{
	if (!address)
		return;

	if (std::strcmp(ClassifyNativeAddress(address), "native") == 0)
		return;

	const auto pc = reinterpret_cast<std::uintptr_t>(address);
	const auto start = reinterpret_cast<const u8*>(pc - 32);
	constexpr size_t code_size = 96;

#if EMUCOREX_HAS_CAPSTONE
	csh handle = 0;
	if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle) != CS_ERR_OK)
		return;

	cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);

	cs_insn* insn = nullptr;
	const size_t count = cs_disasm(handle, start, code_size, pc - 32, 0, &insn);
	if (count == 0)
	{
		cs_close(&handle);
		return;
	}

	for (size_t i = 0; i < count; i++)
	{
		const bool is_pc = insn[i].address == pc;
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
			"Crash diag disasm %c %016llx: %-8s %s",
			is_pc ? '>' : ' ',
			static_cast<unsigned long long>(insn[i].address),
			insn[i].mnemonic,
			insn[i].op_str);
	}

	cs_free(insn, count);
	cs_close(&handle);
#else
	for (size_t i = 0; i < code_size; i += sizeof(u32))
	{
		u32 bits = 0;
		std::memcpy(&bits, start + i, sizeof(bits));
		__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
			"Crash diag code %c %016llx: %08x",
			(reinterpret_cast<std::uintptr_t>(start + i) == pc) ? '>' : ' ',
			static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(start + i)),
			bits);
	}
#endif
}

void DumpEeState()
{
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag EE pc=%08x code=%08x cycle=%u next=%u branch=%d interrupt=%08x opmode=%d",
		cpuRegs.pc, cpuRegs.code, cpuRegs.cycle, cpuRegs.nextEventCycle,
		cpuRegs.branch, cpuRegs.interrupt, cpuRegs.opmode);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag EE CP0 status=%08x cause=%08x epc=%08x badvaddr=%08x entryhi=%08x count=%08x",
		cpuRegs.CP0.n.Status.val, cpuRegs.CP0.n.Cause, cpuRegs.CP0.n.EPC,
		cpuRegs.CP0.n.BadVAddr, cpuRegs.CP0.n.EntryHi, cpuRegs.CP0.n.Count);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag EE GPR a0=%016llx a1=%016llx a2=%016llx a3=%016llx sp=%016llx ra=%016llx",
		static_cast<unsigned long long>(cpuRegs.GPR.n.a0.UD[0]),
		static_cast<unsigned long long>(cpuRegs.GPR.n.a1.UD[0]),
		static_cast<unsigned long long>(cpuRegs.GPR.n.a2.UD[0]),
		static_cast<unsigned long long>(cpuRegs.GPR.n.a3.UD[0]),
		static_cast<unsigned long long>(cpuRegs.GPR.n.sp.UD[0]),
		static_cast<unsigned long long>(cpuRegs.GPR.n.ra.UD[0]));
}

void DumpIopState()
{
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag IOP pc=%08x code=%08x cycle=%u next=%u interrupt=%08x break=%d cycleEE=%d carry=%u",
		psxRegs.pc, psxRegs.code, psxRegs.cycle, psxRegs.iopNextEventCycle,
		psxRegs.interrupt, psxRegs.iopBreak, psxRegs.iopCycleEE, psxRegs.iopCycleEECarry);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag IOP CP0 status=%08x cause=%08x epc=%08x badvaddr=%08x",
		psxRegs.CP0.n.Status, psxRegs.CP0.n.Cause, psxRegs.CP0.n.EPC, psxRegs.CP0.n.BadVAddr);
}

void DumpBusState()
{
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag DMAC ctrl=%08x stat=%08x pcr=%08x sqwc=%08x stadr=%08x queue=%08x",
		dmacRegs.ctrl._u32, dmacRegs.stat._u32, dmacRegs.pcr._u32,
		dmacRegs.sqwc._u32, dmacRegs.stadr._u32, cpuRegs.dmastall);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag DMA VIF0 chcr=%08x madr=%08x qwc=%08x tadr=%08x VIF1 chcr=%08x madr=%08x qwc=%08x tadr=%08x",
		vif0ch.chcr._u32, vif0ch.madr, vif0ch.qwc, vif0ch.tadr,
		vif1ch.chcr._u32, vif1ch.madr, vif1ch.qwc, vif1ch.tadr);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag DMA GIF chcr=%08x madr=%08x qwc=%08x tadr=%08x IPU0 chcr=%08x qwc=%08x IPU1 chcr=%08x qwc=%08x",
		gifch.chcr._u32, gifch.madr, gifch.qwc, gifch.tadr,
		ipu0ch.chcr._u32, ipu0ch.qwc, ipu1ch.chcr._u32, ipu1ch.qwc);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag VIF0 stat=%08x code=%08x num=%u addr=%08x VIF1 stat=%08x code=%08x num=%u addr=%08x",
		vif0Regs.stat._u32, vif0Regs.code, vif0Regs.num, vif0Regs.addr,
		vif1Regs.stat._u32, vif1Regs.code, vif1Regs.num, vif1Regs.addr);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag GIF stat=%08x ctrl=%08x mode=%08x tag0=%08x tag1=%08x cnt=%08x fifo=%u state=%d",
		gifRegs.stat._u32, gifRegs.ctrl._u32, gifRegs.mode._u32,
		gifRegs.tag0._u32, gifRegs.tag1._u32, gifRegs.cnt._u32,
		gif_fifo.fifoSize, gif.gifstate);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag GS csr=%08x imr=%08x field=%u fifo=%u frame=%llu fps=%.2f speed=%.2f",
		CSRreg._u32, GSIMR._u32, CSRreg.FIELD, CSRreg.FIFO,
		static_cast<unsigned long long>(PerformanceMetrics::GetFrameNumber()),
		PerformanceMetrics::GetFPS(), PerformanceMetrics::GetSpeed());
}

void DumpVuState()
{
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag VU0 tpc=%08x cpc=%08x cycle=%u code=%08x VU1 tpc=%08x cpc=%08x cycle=%u code=%08x",
		VU0.VI[REG_TPC].UL, VU0.VI[REG_CMSAR0].UL, VU0.cycle, VU0.code,
		VU1.VI[REG_TPC].UL, VU1.VI[REG_CMSAR1].UL, VU1.cycle, VU1.code);
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
		"Crash diag VU ptrs vu0_mem=%p vu0_micro=%p vu1_mem=%p vu1_micro=%p vu_mem_range=%p-%p",
		VU0.Mem, VU0.Micro, VU1.Mem, VU1.Micro, SysMemory::GetVUMem(), SysMemory::GetVUMemEnd());
}
} // namespace

void RecordVmLaunchForCrashDiagnostics(const std::string& path, bool boot_elf, int probe_steps)
{
	CopyBreadcrumb(s_launch_path, sizeof(s_launch_path), path.empty() ? "<bios>" : path);
	s_boot_elf.store(boot_elf, std::memory_order_relaxed);
	s_probe_steps.store(probe_steps, std::memory_order_relaxed);
	s_launch_id.fetch_add(1, std::memory_order_relaxed);
	WriteToLogFileFmt("VM_LAUNCH", "launch=%llu path=%s bootElf=%d probeSteps=%d",
		static_cast<unsigned long long>(s_launch_id.load(std::memory_order_relaxed)),
		s_launch_path, boot_elf ? 1 : 0, probe_steps);
	RecordVmExecutePhaseForCrashDiagnostics("prepared");
}

void RecordVmExecutePhaseForCrashDiagnostics(const char* phase)
{
	CopyBreadcrumb(s_execute_phase, sizeof(s_execute_phase), phase);
	s_execute_generation.fetch_add(1, std::memory_order_relaxed);
}

void RecordGameForCrashDiagnostics(const std::string& title, const std::string& serial, u32 disc_crc, u32 current_crc)
{
	CopyBreadcrumb(s_game_title, sizeof(s_game_title), title);
	CopyBreadcrumb(s_game_serial, sizeof(s_game_serial), serial);
	s_disc_crc.store(disc_crc, std::memory_order_relaxed);
	s_current_crc.store(current_crc, std::memory_order_relaxed);
	WriteToLogFileFmt("VM_GAME", "launch=%llu title=%s serial=%s disc_crc=%08x current_crc=%08x",
		static_cast<unsigned long long>(s_launch_id.load(std::memory_order_relaxed)),
		s_game_title, s_game_serial, disc_crc, current_crc);
}

void RecordNativeErrorForCrashDiagnostics(std::string_view title, std::string_view message)
{
	WriteToLogFileFmt("NATIVE_ERROR", "%.*s: %.*s", static_cast<int>(title.size()), title.empty() ? "" : title.data(),
		static_cast<int>(message.size()), message.empty() ? "" : message.data());
}

void SetNativeCrashLogFilePath(const char* path)
{
	if (!path)
	{
		s_log_file_path[0] = '\0';
		return;
	}
	std::snprintf(s_log_file_path, sizeof(s_log_file_path), "%s", path);
	EMUCOREX_DIAGNOSTIC_LOG_PRINT(ANDROID_LOG_INFO, LOG_TAG, "Native crash log path set: %s", s_log_file_path);
}

void InstallNativeCrashSignalHandler()
{
	if (s_signal_handler_installed.exchange(true, std::memory_order_acq_rel))
		return;

	struct sigaction sa = {};
	sa.sa_sigaction = NativeCrashSignalHandler;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGSEGV, &sa, &s_prev_sigsegv);
	sigaction(SIGABRT, &sa, &s_prev_sigabrt);
	sigaction(SIGBUS,  &sa, &s_prev_sigbus);
	sigaction(SIGFPE,  &sa, &s_prev_sigfpe);

	EMUCOREX_DIAGNOSTIC_LOG_WRITE(ANDROID_LOG_INFO, LOG_TAG,
		"Native crash signal handler installed (SIGSEGV/SIGABRT/SIGBUS/SIGFPE)");
}
} // namespace emucorex::android

namespace emucorex::android
{
extern "C" void EmuCoreXRecordTFXCompileSelector(u32 vs, u64 ps_hi, u64 ps_lo)
{
	s_tfx_vs_key.store(vs, std::memory_order_relaxed);
	s_tfx_ps_key_hi.store(ps_hi, std::memory_order_relaxed);
	s_tfx_ps_key_lo.store(ps_lo, std::memory_order_relaxed);
	s_tfx_selector_valid.store(true, std::memory_order_release);
}

extern "C" void EmuCoreXClearTFXCompileSelector()
{
	s_tfx_selector_valid.store(false, std::memory_order_release);
}
} // namespace emucorex::android

extern "C" void EmuCoreXDumpNativeCrashDiagnostics(
	int signal, void* exception_pc, void* exception_address, bool is_write, int handler_result)
{
	__android_log_print(ANDROID_LOG_ERROR, emucorex::android::LOG_TAG,
		"Crash diag begin sig=%d write=%d handler_result=%d launch=%llu phase=%s phase_gen=%llu bootElf=%d probeSteps=%d",
		signal, is_write ? 1 : 0, handler_result,
		static_cast<unsigned long long>(emucorex::android::s_launch_id.load(std::memory_order_relaxed)),
		emucorex::android::s_execute_phase,
		static_cast<unsigned long long>(emucorex::android::s_execute_generation.load(std::memory_order_relaxed)),
		emucorex::android::s_boot_elf.load(std::memory_order_relaxed) ? 1 : 0,
		emucorex::android::s_probe_steps.load(std::memory_order_relaxed));
	__android_log_print(ANDROID_LOG_ERROR, emucorex::android::LOG_TAG,
		"Crash diag launch_path=%s game=\"%s\" serial=%s disc_crc=%08x current_crc=%08x vm_state=%d valid_vm=%d",
		emucorex::android::s_launch_path,
		emucorex::android::s_game_title[0] ? emucorex::android::s_game_title : "<unknown>",
		emucorex::android::s_game_serial[0] ? emucorex::android::s_game_serial : "<unknown>",
		emucorex::android::s_disc_crc.load(std::memory_order_relaxed),
		emucorex::android::s_current_crc.load(std::memory_order_relaxed),
		static_cast<int>(VMManager::GetState()),
		VMManager::HasValidVM() ? 1 : 0);

	emucorex::android::LogModuleForAddress("pc", exception_pc);
	emucorex::android::LogModuleForAddress("fault_addr", exception_address);
	emucorex::android::DumpCodeAroundAddress(exception_pc);
	emucorex::android::DumpEeState();
	emucorex::android::DumpIopState();
	emucorex::android::DumpBusState();
	emucorex::android::DumpVuState();
	__android_log_write(ANDROID_LOG_ERROR, emucorex::android::LOG_TAG, "Crash diag end");
}
