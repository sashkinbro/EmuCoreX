// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include "OracleProcessGuard.h"
#include <fcntl.h>

inline int TestOracleProcessGuard()
{
    using namespace OracleGuard;
    // These children never load the emulator. Maximum allocation is 16 MiB.
    const Limits tiny{500, 32 * 1024, 32, 8, 1};
    bool passed = true;
    auto success = +[](void*) -> int { return 7; };
    auto result = Run(success, nullptr, tiny);
    passed &= result.code == 7 && result.stop == Stop::None;
    auto idle = +[](void*) -> int { for (;;) pause(); };
    auto deadline = tiny;
    deadline.milliseconds = 80;
    result = Run(idle, nullptr, deadline);
    passed &= result.code == 124 && result.stop == Stop::Time;
    auto allocate = +[](void*) -> int {
        volatile unsigned char* memory = static_cast<unsigned char*>(std::malloc(16 * 1024 * 1024));
        if (!memory) return 2;
        for (size_t i = 0; i < 16 * 1024 * 1024; i += 4096) memory[i] = 1;
        for (;;) pause();
    };
    auto resident = tiny;
    resident.rss_kib = 8 * 1024;
    result = Run(allocate, nullptr, resident);
    passed &= result.code == 137 && result.stop == Stop::Memory;
    auto descriptors = +[](void*) -> int {
        while (open("/dev/null", O_RDONLY) >= 0) {}
        for (;;) pause();
    };
    auto files = tiny;
    files.descriptors = 16;
    result = Run(descriptors, nullptr, files);
    passed &= result.code == 137 && result.stop == Stop::Descriptors;
    auto signal = +[](void*) -> int { raise(SIGTERM); return 2; };
    result = Run(signal, nullptr, tiny);
    passed &= result.code == 128 + SIGTERM && result.stop == Stop::None;
    std::printf("%s oracle process guard: exit status, timeout, RSS, descriptor limit and child signal\n",
        passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
