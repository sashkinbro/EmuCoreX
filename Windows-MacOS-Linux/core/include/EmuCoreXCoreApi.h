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

enum { EMUCOREX_CORE_ABI_VERSION = 2 };

typedef enum EmuCoreXCoreState {
    EMUCOREX_CORE_STATE_STOPPED = 0,
    EMUCOREX_CORE_STATE_STARTING = 1,
    EMUCOREX_CORE_STATE_RUNNING = 2,
    EMUCOREX_CORE_STATE_PAUSED = 3,
    EMUCOREX_CORE_STATE_STOPPING = 4,
    EMUCOREX_CORE_STATE_ERROR = 5
} EmuCoreXCoreState;

typedef enum EmuCoreXSurfaceType {
    EMUCOREX_SURFACE_NONE = 0,
    EMUCOREX_SURFACE_WIN32 = 1,
    EMUCOREX_SURFACE_X11 = 2,
    EMUCOREX_SURFACE_WAYLAND = 3,
    EMUCOREX_SURFACE_MACOS = 4
} EmuCoreXSurfaceType;

typedef struct EmuCoreXCoreConfiguration {
    const char* resources_path;
    const char* data_path;
    const char* bios_path;
} EmuCoreXCoreConfiguration;

typedef struct EmuCoreXRenderSurface {
    uint32_t type;
    void* display_connection;
    void* window_handle;
    void* surface_handle;
    uint32_t width;
    uint32_t height;
    float scale;
    float refresh_rate;
} EmuCoreXRenderSurface;

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
EMUCOREX_CORE_API int emucorex_core_configure(const EmuCoreXCoreConfiguration* configuration);
EMUCOREX_CORE_API void emucorex_core_set_render_surface(const EmuCoreXRenderSurface* surface);
EMUCOREX_CORE_API int emucorex_core_start_game(const char* utf8_path);
EMUCOREX_CORE_API int emucorex_core_start_bios(void);
EMUCOREX_CORE_API uint32_t emucorex_core_state(void);
EMUCOREX_CORE_API void emucorex_core_set_paused(int paused);
EMUCOREX_CORE_API void emucorex_core_shutdown(void);
EMUCOREX_CORE_API int emucorex_core_save_state(int slot);
EMUCOREX_CORE_API int emucorex_core_load_state(int slot);
EMUCOREX_CORE_API uint32_t emucorex_core_last_error(char* destination, uint32_t capacity);

#ifdef __cplusplus
}
#endif
