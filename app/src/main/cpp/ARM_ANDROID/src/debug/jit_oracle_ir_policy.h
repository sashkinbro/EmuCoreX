// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstring>

// Pure lifter/lowerer diagnostics remain callable. These suites also require
// production IR dispatch, and must not report a legacy fallback as IR coverage.
inline bool OracleSuiteRequiresExperimentalIr(const char* suite)
{
    constexpr const char* suites[] = {
        "ee", "iop", "vu", "vu-ftoi", "vu-maxmin", "vu-maxmin-route",
        "vu-maxmin-broadcast", "vu-maxmin-broadcast-route", "vu-clip",
        "vu-clip-route", "vu-maxmin-i", "ee-cop1-mov", "ee-cop1-memory",
        "ee-cop1-compare", "ee-vu-clip", "ee-vu-abs", "ee-stores", "ee-bc1",
        "ee-hot-loads", "ee-fastmem", "ee-cop1-unary", "ee-cop1-maxmin",
        "ee-fastmem-exit", "ee-fastmem-cold", "ee-vu-maxmin-i",
        "ee-vu-maxmin-i-flags", "ee-vu-maxmin-broadcast", "ee-vu-maxmin",
        "ee-vu-itof", "ee-vu-ftoi", "iop-cop2", "iop-cop2-memory",
        "iop-nclip", "iop-avsz",
    };
    for (const char* candidate : suites)
    {
        if (std::strcmp(suite, candidate) == 0)
            return true;
    }
    return false;
}

inline constexpr bool OracleExperimentalIrAvailable =
#if defined(__ANDROID__) && defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS) && \
    defined(EMUCOREX_ENABLE_EXPERIMENTAL_IR) && EMUCOREX_ENABLE_EXPERIMENTAL_IR
    true;
#else
    false;
#endif
