// SPDX-License-Identifier: GPL-3.0+

#include "common/Assertions.h"
#include "common/BitUtils.h"
#include "common/CrashHandler.h"
#include "common/Error.h"
#include "common/HostSys.h"

#include <android/log.h>
#include <android/sharedmem.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/mman.h>
#include <unistd.h>
#include <ucontext.h>

#include "fmt/format.h"

extern "C" void EmuCoreXDumpNativeCrashDiagnostics(
	int signal, void* exception_pc, void* exception_address, bool is_write, int handler_result) __attribute__((weak));

static __ri uint AndroidProt(const PageProtectionMode& mode)
{
	uint prot = 0;
	if (mode.CanRead())
		prot |= PROT_READ;
	if (mode.CanWrite())
		prot |= PROT_WRITE;
	if (mode.CanExecute())
		prot |= PROT_EXEC | PROT_READ;
	return prot;
}

void HostSys::MemProtect(void* baseaddr, size_t size, const PageProtectionMode& mode)
{
	pxAssertMsg((size & (__pagesize - 1)) == 0, "Size is page aligned");
	if (mprotect(baseaddr, size, AndroidProt(mode)) != 0)
		pxFail("mprotect() failed");
}

std::string HostSys::GetFileMappingName(const char* prefix)
{
	return fmt::format("{}_{}", prefix, static_cast<unsigned>(getpid()));
}

void* HostSys::CreateSharedMemory(const char* name, size_t size)
{
	const int fd = ASharedMemory_create(name, size);
	if (fd < 0)
	{
		std::fprintf(stderr, "ASharedMemory_create(%s, %zu) failed: %d\n", name, size, errno);
		return nullptr;
	}

	return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
}

void HostSys::DestroySharedMemory(void* ptr)
{
	close(static_cast<int>(reinterpret_cast<intptr_t>(ptr)));
}

size_t HostSys::GetRuntimePageSize()
{
	const long res = sysconf(_SC_PAGESIZE);
	return (res > 0) ? static_cast<size_t>(res) : 0;
}

size_t HostSys::GetRuntimeCacheLineSize()
{
	for (int index = 0; index < 16; index++)
	{
		char path[128];
		std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/coherency_line_size", index);
		FILE* fp = std::fopen(path, "rb");
		if (!fp)
			continue;

		char buf[64] = {};
		const size_t read = std::fread(buf, 1, sizeof(buf) - 1, fp);
		std::fclose(fp);
		if (read == 0)
			continue;

		const int value = std::atoi(buf);
		if (value > 0)
			return static_cast<size_t>(value);
	}

	return 64;
}

SharedMemoryMappingArea::SharedMemoryMappingArea(u8* base_ptr, size_t size, size_t num_pages)
	: m_base_ptr(base_ptr)
	, m_size(size)
	, m_num_pages(num_pages)
{
}

SharedMemoryMappingArea::~SharedMemoryMappingArea()
{
	pxAssertRel(m_num_mappings == 0, "No mappings left");
	if (munmap(m_base_ptr, m_size) != 0)
		pxFailRel("Failed to release shared memory area");
}

std::unique_ptr<SharedMemoryMappingArea> SharedMemoryMappingArea::Create(size_t size, bool /*jit*/)
{
	pxAssertRel(Common::IsAlignedPow2(size, __pagesize), "Size is page aligned");

	void* alloc = mmap(nullptr, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (alloc == MAP_FAILED)
		return nullptr;

	return std::unique_ptr<SharedMemoryMappingArea>(
		new SharedMemoryMappingArea(static_cast<u8*>(alloc), size, size / __pagesize));
}

u8* SharedMemoryMappingArea::Map(void* file_handle, size_t file_offset, void* map_base, size_t map_size, const PageProtectionMode& mode)
{
	pxAssert(static_cast<u8*>(map_base) >= m_base_ptr && static_cast<u8*>(map_base) < (m_base_ptr + m_size));

	if (file_handle)
	{
		const int fd = static_cast<int>(reinterpret_cast<intptr_t>(file_handle));
		void* const ptr = mmap(map_base, map_size, AndroidProt(mode), MAP_SHARED | MAP_FIXED, fd, static_cast<off_t>(file_offset));
		if (ptr == MAP_FAILED)
			return nullptr;
	}
	else if (mprotect(map_base, map_size, AndroidProt(mode)) < 0)
	{
		return nullptr;
	}

	m_num_mappings++;
	return static_cast<u8*>(map_base);
}

bool SharedMemoryMappingArea::Unmap(void* map_base, size_t map_size, bool /*is_file*/)
{
	pxAssert(static_cast<u8*>(map_base) >= m_base_ptr && static_cast<u8*>(map_base) < (m_base_ptr + m_size));

	if (mmap(map_base, map_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
		return false;

	m_num_mappings--;
	return true;
}

void HostSys::FlushInstructionCache(void* address, u32 size)
{
	__builtin___clear_cache(reinterpret_cast<char*>(address), reinterpret_cast<char*>(address) + size);
}

namespace PageFaultHandler
{
	static std::recursive_mutex s_exception_handler_mutex;
	static bool s_in_exception_handler = false;
	static bool s_installed = false;
	static void SignalHandler(int sig, siginfo_t* info, void* ctx);
} // namespace PageFaultHandler

[[maybe_unused]] static bool IsStoreInstruction(const void* ptr)
{
	u32 bits;
	std::memcpy(&bits, ptr, sizeof(bits));

	if ((bits & 0x0a000000) != 0x08000000)
		return false;

	if ((bits & 0x3a000000) == 0x28000000)
		return (bits & (1 << 22)) == 0;

	switch (bits & 0xC4C00000)
	{
		case 0x00000000:
		case 0x40000000:
		case 0x80000000:
		case 0xC0000000:
		case 0x04000000:
		case 0x44000000:
		case 0x84000000:
		case 0xC4000000:
		case 0x04800000:
			return true;

		default:
			return false;
	}
}

void PageFaultHandler::SignalHandler(int sig, siginfo_t* info, void* ctx)
{
	void* const exception_address = reinterpret_cast<void*>(info->si_addr);
	void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.pc);
	const bool is_write = exception_pc ? IsStoreInstruction(exception_pc) : false;

	s_exception_handler_mutex.lock();

	HandlerResult result = HandlerResult::ExecuteNextHandler;
	if (!s_in_exception_handler)
	{
		s_in_exception_handler = true;
		result = HandlePageFault(exception_pc, exception_address, is_write);
		s_in_exception_handler = false;
	}

	s_exception_handler_mutex.unlock();

	if (result == HandlerResult::ContinueExecution)
		return;

	__android_log_print(ANDROID_LOG_ERROR, "EmuCoreX",
		"Unhandled native signal sig=%d pc=%p addr=%p write=%d handler_result=%d",
		sig, exception_pc, exception_address, is_write ? 1 : 0, static_cast<int>(result));
	if (EmuCoreXDumpNativeCrashDiagnostics)
		EmuCoreXDumpNativeCrashDiagnostics(sig, exception_pc, exception_address, is_write, static_cast<int>(result));

	const ucontext_t* const uctx = static_cast<const ucontext_t*>(ctx);
	__android_log_print(ANDROID_LOG_ERROR, "EmuCoreX",
		"Unhandled native signal regs X0=%016llx X1=%016llx X2=%016llx X3=%016llx",
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[0]),
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[1]),
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[2]),
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[3]));
	__android_log_print(ANDROID_LOG_ERROR, "EmuCoreX",
		"Unhandled native signal regs X4=%016llx X5=%016llx X6=%016llx X7=%016llx",
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[4]),
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[5]),
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[6]),
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[7]));
	__android_log_print(ANDROID_LOG_ERROR, "EmuCoreX",
		"Unhandled native signal regs X8=%016llx X16=%016llx X30=%016llx SP=%016llx",
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[8]),
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[16]),
		static_cast<unsigned long long>(uctx->uc_mcontext.regs[30]),
		static_cast<unsigned long long>(uctx->uc_mcontext.sp));

	CrashHandler::CrashSignalHandler(sig, info, ctx);
}

bool PageFaultHandler::Install(Error* error)
{
	std::unique_lock lock(s_exception_handler_mutex);
	pxAssertRel(!s_installed, "Page fault handler has already been installed.");

	struct sigaction sa = {};
	sigemptyset(&sa.sa_mask);
	// Keep both synchronous memory-fault signals blocked while this handler is
	// running. SA_NODEFER allowed a fault in HandlePageFault() (including a
	// SIGBUS raised while handling SIGSEGV, or vice versa) to recursively enter
	// code which mutates the same fastmem/JIT state. The first-level fault path
	// is unchanged; a fault in the handler itself is not recoverable here.
	sigaddset(&sa.sa_mask, SIGSEGV);
	sigaddset(&sa.sa_mask, SIGBUS);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = SignalHandler;

	struct sigaction old_sigsegv = {};
	if (sigaction(SIGSEGV, &sa, &old_sigsegv) != 0)
	{
		Error::SetErrno(error, "sigaction() for SIGSEGV failed: ", errno);
		return false;
	}

	if (sigaction(SIGBUS, &sa, nullptr) != 0)
	{
		const int install_errno = errno;
		// Do not leave a half-installed page-fault handler behind. In particular,
		// a later Install() attempt must not unexpectedly chain through this
		// handler while s_installed is still false.
		(void)sigaction(SIGSEGV, &old_sigsegv, nullptr);
		Error::SetErrno(error, "sigaction() for SIGBUS failed: ", install_errno);
		return false;
	}

	s_installed = true;
	return true;
}

bool PageFaultHandler::InstallSecondaryThread()
{
	return true;
}
