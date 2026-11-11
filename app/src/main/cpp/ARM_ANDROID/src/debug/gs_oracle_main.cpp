// SPDX-License-Identifier: GPL-3.0+
// Isolated debug GS process. Never starts an Activity or touches emulator preferences.
#include <cstdio>
#include <dlfcn.h>
#include <string>
#include <unistd.h>
#include "OracleProcessGuard.h"

static int RunGSOraclePayload(int argc, char** argv)
{
    if (argc != 11)
    {
        std::fprintf(stderr, "usage: %s /absolute/core.so sw|vk|gl builtin|dump.gs output capture|benchmark|profile warmup repeats cases seed export-case\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string library_path(argv[1]);
    const auto slash = library_path.rfind('/');
    if (library_path.empty() || library_path.front() != '/' || slash == std::string::npos ||
        chdir(library_path.substr(0, slash).c_str()) != 0)
        return 2;
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        std::fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    auto run = reinterpret_cast<int (*)(int, const char* const*)>(dlsym(library, "EmuCoreXRunGSOracle"));
    if (!run)
    {
        std::fprintf(stderr, "GS oracle is unavailable in this build: %s\n", dlerror());
        return 2;
    }
    const rlimit descriptors{256, 256};
    if (setrlimit(RLIMIT_NOFILE, &descriptors) != 0)
    { std::perror("GS oracle descriptor limit"); return 2; }
    const int result = run(argc - 2, argv + 2);
    std::fflush(nullptr);
    _exit(result);
}

int main(int argc, char** argv)
{
    struct Arguments { int argc; char** argv; } arguments{argc, argv};
    const auto run = +[](void* value) -> int {
        auto& args = *static_cast<Arguments*>(value);
        return RunGSOraclePayload(args.argc, args.argv);
    };
    // GPU drivers (notably Mali) map several hundred MiB of resident memory at
    // context creation; Debug benchmark replays on slower SoCs need minutes.
    return OracleGuard::Run(run, &arguments, {300000, 1024 * 1024, 1024, 128, 240}).code;
}
