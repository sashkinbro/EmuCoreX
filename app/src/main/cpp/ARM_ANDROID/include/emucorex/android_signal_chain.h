// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cerrno>
#include <csignal>
#include <cstring>
#include <ucontext.h>
#include <unistd.h>

namespace emucorex::android
{
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
