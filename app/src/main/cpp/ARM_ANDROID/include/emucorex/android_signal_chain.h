// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdint>
#include <ucontext.h>
#include <unistd.h>

namespace emucorex::android
{
// Record the faulting thread, not a backtrace of the signal handler. No allocator,
// symbol lookup, unwinder or printf-family function is safe to call here.
struct SignalMetadata
{
    uint64_t launch_id, phase_generation, tfx_valid, tfx_vs, tfx_ps_hi, tfx_ps_lo;
};

inline void WriteSignalContext(int fd, int signal, const siginfo_t* info, const void* context,
    const SignalMetadata* metadata = nullptr)
{
    char buffer[2048];
    size_t used = 0;
    const auto append = [&](const char* text) {
        while (*text && used < sizeof(buffer)) buffer[used++] = *text++;
    };
    const auto hex = [&](uint64_t value) {
        append("0x");
        for (int shift = 60; shift >= 0 && used < sizeof(buffer); shift -= 4)
            buffer[used++] = "0123456789abcdef"[(value >> shift) & 15];
    };
    const auto field = [&](const char* name, uint64_t value) {
        append(name); append("="); hex(value); append("\n");
    };
    append("NATIVE_CRASH_CONTEXT v=1\n");
    field("signal", static_cast<uint64_t>(signal));
    field("code", info ? static_cast<uint64_t>(info->si_code) : 0);
    field("fault_addr", info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0);
    if (metadata)
    {
        field("launch_id", metadata->launch_id);
        field("phase_generation", metadata->phase_generation);
        if (metadata->tfx_valid)
        {
            field("tfx_vs", metadata->tfx_vs);
            field("tfx_ps_hi", metadata->tfx_ps_hi);
            field("tfx_ps_lo", metadata->tfx_ps_lo);
        }
    }
    if (context)
    {
        const auto& regs = static_cast<const ucontext_t*>(context)->uc_mcontext;
        field("PC", regs.pc); field("LR", regs.regs[30]); field("SP", regs.sp);
        field("FP", regs.regs[29]); field("PSTATE", regs.pstate);
        for (unsigned i = 0; i < 31; ++i)
        {
            char name[] = {'X', static_cast<char>('0' + i / 10), static_cast<char>('0' + i % 10), '\0'};
            field(name, regs.regs[i]);
        }
    }
    size_t sent = 0;
    while (sent < used)
    {
        const ssize_t result = write(fd, buffer + sent, used - sent);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) break;
        sent += static_cast<size_t>(result);
    }
}

// Never read the instruction at a faulting PC: instruction-fetch faults can point
// at unmapped/non-readable memory. The kernel's bounded ESR record identifies writes.
inline bool IsWriteSignalContext(const void* context)
{
    if (!context)
        return false;
    const auto& records = static_cast<const ucontext_t*>(context)->uc_mcontext.__reserved;
    for (size_t offset = 0; offset + sizeof(_aarch64_ctx) <= sizeof(records);)
    {
        _aarch64_ctx header;
        std::memcpy(&header, records + offset, sizeof(header));
        if (header.size < sizeof(header) || header.size > sizeof(records) - offset)
            break;
        if (header.magic == ESR_MAGIC && header.size >= sizeof(esr_context))
        {
            esr_context record;
            std::memcpy(&record, records + offset, sizeof(record));
            const auto exception_class = (record.esr >> 26) & 0x3f;
            return (exception_class == 0x24 || exception_class == 0x25) && (record.esr & (1u << 6));
        }
        offset += header.size;
    }
    return false;
}

// Terminal handoff after the emulator has rejected a fault. Restore the preceding
// action before calling it so debuggerd sees its own SA_SIGINFO flags. Passing the
// original arguments preserves the fault's PC, address and si_code in tombstones.
inline void HandOffFatalSignal(int signal, siginfo_t* info, void* context, const struct sigaction& previous)
{
    const int saved_errno = errno;
    struct sigaction action = previous;
    if (action.sa_handler == SIG_IGN && info && info->si_code > 0)
        action.sa_handler = SIG_DFL; // Ignoring a synchronous fault would just refault forever.
    if (sigaction(signal, &action, nullptr) != 0)
        _exit(128 + signal);

    if (action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN)
    {
        sigset_t saved_mask;
        sigprocmask(SIG_BLOCK, &action.sa_mask, &saved_mask);
        if (action.sa_flags & SA_SIGINFO)
            action.sa_sigaction(signal, info, context);
        else
            action.sa_handler(signal);
        sigprocmask(SIG_SETMASK, &saved_mask, nullptr);
    }
    else if (action.sa_handler == SIG_DFL && (!info || info->si_code <= 0))
    {
        // A software-generated signal will not recur when the handler returns.
        raise(signal);
    }
    // For a synchronous default action, returning retries the original instruction
    // and lets the kernel deliver the original hardware fault with that disposition.
    errno = saved_errno;
}
} // namespace emucorex::android
