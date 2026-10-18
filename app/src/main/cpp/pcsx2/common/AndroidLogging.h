// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <android/log.h>

// Keep diagnostics in developer builds. A disabled call must not evaluate its
// arguments: formatting, property reads and JNI work can cost more than the sink.
#if !defined(NDEBUG) || defined(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
#define EMUCOREX_DIAGNOSTIC_LOG_PRINT(...) __android_log_print(__VA_ARGS__)
#define EMUCOREX_DIAGNOSTIC_LOG_WRITE(...) __android_log_write(__VA_ARGS__)
#else
#define EMUCOREX_DIAGNOSTIC_LOG_PRINT(...) ((void)0)
#define EMUCOREX_DIAGNOSTIC_LOG_WRITE(...) ((void)0)
#endif
