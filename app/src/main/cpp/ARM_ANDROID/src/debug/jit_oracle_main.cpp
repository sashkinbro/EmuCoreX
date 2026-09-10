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
        std::fprintf(stderr, "usage: %s /absolute/path/libemucore_4k.so classifier|vu|vectors|ee|iop|replay-vu1 [capture.vuo]\n", argv[0]);
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
    alarm(60);
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    const char* symbol = fallback ? "EmuCoreXReplayVU1Fallback" : (replay ? "EmuCoreXReplayVU1" : "EmuCoreXRunJitOracle");
    auto run = reinterpret_cast<int (*)(const char*)>(dlsym(library, symbol));
    if (!run)
    {
        std::fprintf(stderr, "debug oracle unavailable: %s\n", dlerror());
        return 2;
    }
    const int result = run(replay ? argv[3] : argv[2]);
    std::fflush(nullptr);
    // Process owns all emulator globals; avoid unrelated application teardown.
    _exit(result);
}
