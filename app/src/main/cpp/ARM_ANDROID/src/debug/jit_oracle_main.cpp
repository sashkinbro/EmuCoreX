// SPDX-License-Identifier: GPL-3.0+
// A separate shell process: no Activity, Java VM, BIOS, game or rendering surface.
#include <cstdio>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    const bool fallback = argc == 4 && std::strcmp(argv[2], "replay-vu1-fallback") == 0;
    const bool replay = argc == 4 && (fallback || std::strcmp(argv[2], "replay-vu1") == 0);
    if (argc != 3 && !replay)
    {
        std::fprintf(stderr, "usage: %s /absolute/path/libemucore_4k.so classifier|vu|vu-itof-codegen|vu-ftoi|vectors|sqrt-flags|div-edges|ee|ee-cop1-mov|ee-cop1-memory|ee-vu-clip|ee-vu-abs|ee-stores|ee-vu-itof|ee-vu-ftoi|ee-bc1|ee-hot-loads|ee-fastmem|ee-cop1-unary|ee-cop1-maxmin|ee-vu-maxmin|ir-immediates|iop-nclip|iop-avsz|ee-fastmem-exit|ee-fastmem-cold|vu-maxmin-i|vu-clip|vu-clip-route|ee-vu-maxmin-i|ee-vu-maxmin-i-flags|vu-maxmin|vu-maxmin-route|vu-maxmin-broadcast|vu-maxmin-broadcast-route|ee-vu-maxmin-broadcast|iop|iop-cop2|iop-cop2-memory|gs-prepare|gs-job|gs-job-bridge|gs-job-runtime|gs-job-snapshot|replay-vu1 [capture.vuo]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string path(argv[1]);
    const auto slash = path.rfind('/');
    if (slash == std::string::npos || path.front() != '/' || chdir(path.substr(0, slash).c_str()) != 0)
    {
        std::fprintf(stderr, "An accessible absolute library path is required.\n");
        return 2;
    }
    mkdir("artifacts", 0700);
    // A broken guest branch must not leave a device process running forever.
    // The expanded CPU corpora need more than the original minute.
    alarm(300);
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    const char* symbol = std::strcmp(argv[2], "gs-job-snapshot") == 0 ? "EmuCoreXRunTFXPreparationSnapshotTests" : std::strcmp(argv[2], "gs-job-runtime") == 0 ? "EmuCoreXRunTFXPreparationRuntimeTests" : std::strcmp(argv[2], "gs-job-bridge") == 0 ? "EmuCoreXRunTFXPreparationBridgeTests" : std::strcmp(argv[2], "gs-job") == 0 ? "EmuCoreXRunTFXPreparationJobTests" : fallback ? "EmuCoreXReplayVU1Fallback" : (replay ? "EmuCoreXReplayVU1" : "EmuCoreXRunJitOracle");
    auto run = reinterpret_cast<int (*)(const char*)>(dlsym(library, symbol));
    if (!run)
    {
        std::fprintf(stderr, "debug oracle unavailable: %s\n", dlerror());
        return 2;
    }
    // A data export with this platform function's name can redirect the
    // Android SPHAL loader into non-executable storage before Vulkan starts.
    Dl_info core_info{};
    if (!dladdr(reinterpret_cast<void*>(run), &core_info))
    {
        std::fprintf(stderr, "Cannot inspect Android API exports in the emulator library.\n");
        return 2;
    }
    for (const char* api : {"android_get_exported_namespace", "android_link_namespaces", "android_create_namespace"})
    {
        Dl_info api_info{};
        void* api_symbol = dlsym(library, api);
        if (api_symbol && dladdr(api_symbol, &api_info) && core_info.dli_fbase == api_info.dli_fbase)
        {
            std::fprintf(stderr, "Invalid Android API export: %s belongs to the emulator library.\n", api);
            return 2;
        }
    }
    const int result = run(replay ? argv[3] : argv[2]);
    std::fflush(nullptr);
    // Process owns all emulator globals; avoid unrelated application teardown.
    _exit(result);
}
