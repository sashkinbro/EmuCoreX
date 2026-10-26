// SPDX-License-Identifier: GPL-3.0+
// Isolated debug GS process. Never starts an Activity or touches emulator preferences.
#include <cstdio>
#include <dlfcn.h>
#include <string>
#include <unistd.h>

int main(int argc, char** argv)
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
    alarm(900);
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
    const int result = run(argc - 2, argv + 2);
    std::fflush(nullptr);
    _exit(result);
}
