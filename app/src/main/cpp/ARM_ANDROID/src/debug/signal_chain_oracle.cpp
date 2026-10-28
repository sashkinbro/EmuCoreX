// SPDX-License-Identifier: GPL-3.0+
#include "common/Error.h"
#include "common/HostSys.h"
#include "Memory.h"
#include "emucorex/android_crash_diagnostics.h"
#include "emucorex/android_signal_chain.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

namespace
{
int s_report_fd = -1;
struct SignalReport
{
    int signal;
    int code;
    uintptr_t address;
    uintptr_t pc;
};

void CaptureSignal(int signal, siginfo_t* info, void* context)
{
    const auto* registers = static_cast<const ucontext_t*>(context);
    const SignalReport report{signal, info ? info->si_code : 0,
        info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0,
        registers ? static_cast<uintptr_t>(registers->uc_mcontext.pc) : 0};
    const bool sent = write(s_report_fd, &report, sizeof(report)) == sizeof(report);
    _exit(sent ? 0 : 90);
}

__attribute__((noinline)) void FaultingLoad(void* address)
{
    asm volatile("ldr w9, [%0]" : : "r"(address) : "x9", "memory");
}

__attribute__((noinline)) void FaultingStore(void* address)
{
    asm volatile("str wzr, [%0]" : : "r"(address) : "memory");
}

void RunChild(int fault, bool native_logger, bool platform_handler, int report_fd)
{
    const bool bus = fault == 2;
    const bool store = fault == 1;
    alarm(15);
    s_report_fd = report_fd;
    struct sigaction capture{};
    sigemptyset(&capture.sa_mask);
    capture.sa_flags = SA_SIGINFO;
    capture.sa_sigaction = CaptureSignal;
    if (!platform_handler && (sigaction(SIGSEGV, &capture, nullptr) != 0 ||
        sigaction(SIGBUS, &capture, nullptr) != 0 ||
        sigaction(SIGABRT, &capture, nullptr) != 0))
        _exit(91);
    if (native_logger)
    {
        emucorex::android::SetNativeCrashLogFilePath("artifacts/signal-chain-crash.log");
        emucorex::android::InstallNativeCrashSignalHandler();
    }
    // Allocating the real core memory also installs its real page-fault handler.
    // The deliberately inaccessible mapping below is outside its recoverable ranges.
    if (!SysMemory::Allocate())
        _exit(92);

    const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    int fd = -1;
    if (bus)
    {
        fd = open("artifacts/signal-chain-empty.bin", O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0)
            _exit(93);
    }
    void* const mapping = mmap(nullptr, page_size, bus ? PROT_READ | PROT_WRITE : PROT_NONE,
        bus ? MAP_SHARED : MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
    if (fd >= 0)
        close(fd);
    if (mapping == MAP_FAILED)
        _exit(94);
    // Tell the parent which exact memory address must survive the handler chain.
    const uintptr_t expected_address = reinterpret_cast<uintptr_t>(mapping);
    if (write(report_fd, &expected_address, sizeof(expected_address)) != sizeof(expected_address))
        _exit(95);
    if (fault == 3)
        asm volatile("br %0" : : "r"(mapping) : "memory");
    else if (store)
        FaultingStore(mapping);
    else
        FaultingLoad(mapping);
    _exit(96);
}
} // namespace

extern "C" __attribute__((visibility("default"))) int EmuCoreXRunSignalChainTests(const char*)
{
    int checks = 0;
    int failures = 0;
    const auto check = [&](bool passed, const char* name) {
        ++checks;
        failures += !passed;
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    };
    ucontext_t context{};
    check(!emucorex::android::IsWriteSignalContext(nullptr) && !emucorex::android::IsWriteSignalContext(&context),
        "Missing ESR is safe and does not dereference the faulting instruction");
    esr_context esr{{ESR_MAGIC, sizeof(esr_context)}, (0x24ull << 26) | (1ull << 6)};
    std::memcpy(context.uc_mcontext.__reserved, &esr, sizeof(esr));
    check(emucorex::android::IsWriteSignalContext(&context), "ESR data abort reports a write");
    esr.esr |= 1ull << 32; // ISS2 occupies bits above the six-bit exception class.
    std::memcpy(context.uc_mcontext.__reserved, &esr, sizeof(esr));
    check(emucorex::android::IsWriteSignalContext(&context), "Extended ESR syndrome bits do not change the exception class");
    esr.esr &= ~(1ull << 6);
    std::memcpy(context.uc_mcontext.__reserved, &esr, sizeof(esr));
    check(!emucorex::android::IsWriteSignalContext(&context), "ESR data abort reports a read");
    esr.esr = (0x20ull << 26) | (1ull << 6);
    std::memcpy(context.uc_mcontext.__reserved, &esr, sizeof(esr));
    check(!emucorex::android::IsWriteSignalContext(&context), "Instruction abort is never classified as a write");
    esr.head.size = sizeof(context.uc_mcontext.__reserved) + 16;
    std::memcpy(context.uc_mcontext.__reserved, &esr, sizeof(esr));
    check(!emucorex::android::IsWriteSignalContext(&context), "Oversized ESR record is rejected within the signal frame");
    esr.head.size = 1;
    std::memcpy(context.uc_mcontext.__reserved, &esr, sizeof(esr));
    check(!emucorex::android::IsWriteSignalContext(&context), "Undersized ESR record cannot loop or read past the signal frame");
    context = {};
    const _aarch64_ctx preceding{FPSIMD_MAGIC, 16};
    std::memcpy(context.uc_mcontext.__reserved, &preceding, sizeof(preceding));
    esr = {{ESR_MAGIC, sizeof(esr_context)}, (0x24ull << 26) | (1ull << 6)};
    std::memcpy(context.uc_mcontext.__reserved + 16, &esr, sizeof(esr));
    check(emucorex::android::IsWriteSignalContext(&context), "ESR lookup skips other bounded context records");

    for (int mode = 0; mode < 4; ++mode)
    {
        const bool native_logger = (mode & 1) != 0;
        const bool platform_handler = (mode & 2) != 0;
        for (int fault = 0; fault < (platform_handler ? 1 : 4); ++fault)
        {
            int channel[2];
            if (pipe(channel) != 0)
                return 2;
            const pid_t child = fork();
            if (child == 0)
            {
                close(channel[0]);
                RunChild(fault, native_logger, platform_handler, channel[1]);
            }
            close(channel[1]);
            if (child < 0)
            {
                close(channel[0]);
                return 2;
            }
            uintptr_t expected_address = 0;
            SignalReport report{};
            const ssize_t address_bytes = read(channel[0], &expected_address, sizeof(expected_address));
            const ssize_t report_bytes = read(channel[0], &report, sizeof(report));
            close(channel[0]);
            int status = 0;
            pid_t waited;
            do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
            const int expected_signal = fault == 2 ? SIGBUS : SIGSEGV;
            const int expected_code = fault == 2 ? BUS_ADRERR : SEGV_ACCERR;
            const uintptr_t function = reinterpret_cast<uintptr_t>(fault == 1 ? FaultingStore : FaultingLoad);
            const bool captured = report_bytes == sizeof(report) &&
                waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
                report.signal == expected_signal && report.code == expected_code &&
                report.address == expected_address &&
                (fault == 3 ? report.pc == expected_address : (report.pc >= function && report.pc < function + 128));
            const bool platform_dumped = report_bytes == 0 && waited == child && WIFSIGNALED(status) && WTERMSIG(status) == expected_signal;
            const bool passed = address_bytes == sizeof(expected_address) && (platform_handler ? platform_dumped : captured);
            ++checks;
            failures += !passed;
            std::printf("%s native_logger=%u platform_handler=%u fault=%d child=%d preserves signal/code/address/PC: signal=%d code=%d address=%p expected=%p pc=%p status=%d\n",
                passed ? "PASS" : "FAIL", native_logger ? 1u : 0u, platform_handler ? 1u : 0u, fault, child,
                report.signal, report.code, reinterpret_cast<void*>(report.address),
                reinterpret_cast<void*>(expected_address), reinterpret_cast<void*>(report.pc), status);
        }
    }
    std::printf("RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
