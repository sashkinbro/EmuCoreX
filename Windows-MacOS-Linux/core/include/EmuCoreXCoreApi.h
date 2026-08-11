#pragma once

#include <stdint.h>

#if defined(_WIN32)
#  if defined(EMUCOREX_CORE_BUILD)
#    define EMUCOREX_CORE_API __declspec(dllexport)
#  else
#    define EMUCOREX_CORE_API __declspec(dllimport)
#  endif
#else
#  define EMUCOREX_CORE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { EMUCOREX_CORE_ABI_VERSION = 1 };

typedef struct EmuCoreXGameMetadata {
    char title[512];
    char serial[64];
    char region[64];
    uint64_t total_size;
} EmuCoreXGameMetadata;

EMUCOREX_CORE_API uint32_t emucorex_core_abi_version(void);
EMUCOREX_CORE_API const char* emucorex_core_architecture(void);
EMUCOREX_CORE_API int emucorex_core_initialize(const char* utf8_resources_path);
EMUCOREX_CORE_API int emucorex_core_inspect_game(const char* utf8_path, EmuCoreXGameMetadata* metadata);

#ifdef __cplusplus
}
#endif
