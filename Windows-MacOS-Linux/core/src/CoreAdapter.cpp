#include "EmuCoreXCoreApi.h"
#include "DesktopCoreSession.h"

#include "pcsx2/GameList.h"
#include "pcsx2/Config.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace {
template <size_t Size>
void copyString(char (&destination)[Size], const std::string& source)
{
    const size_t length = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}
}

uint32_t emucorex_core_abi_version(void)
{
    return EMUCOREX_CORE_ABI_VERSION;
}

const char* emucorex_core_architecture(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x64";
#else
    return "unknown";
#endif
}

int emucorex_core_initialize(const char* utf8_resources_path)
{
    if (!utf8_resources_path || *utf8_resources_path == '\0')
        return 0;
    EmuFolders::Resources = utf8_resources_path;
    EmuFolders::UserResources = utf8_resources_path;
    return 1;
}

int emucorex_core_inspect_game(const char* utf8_path, EmuCoreXGameMetadata* metadata)
{
    if (!utf8_path || !metadata || *utf8_path == '\0')
        return 0;

    *metadata = {};
    GameList::Entry entry;
    if (!GameList::PopulateEntryFromPath(utf8_path, &entry))
        return 0;

    const std::string preferredTitle = entry.GetTitle(true);
    copyString(metadata->title, preferredTitle.empty() ? entry.title : preferredTitle);
    copyString(metadata->serial, entry.serial);
    copyString(metadata->region, GameList::RegionToString(entry.region, false));
    metadata->total_size = entry.total_size;
    return 1;
}

int emucorex_core_configure(const EmuCoreXCoreConfiguration* configuration)
{
    return configuration && emucorex::desktop::Configure(*configuration) ? 1 : 0;
}

void emucorex_core_set_render_surface(const EmuCoreXRenderSurface* surface)
{
    emucorex::desktop::SetRenderSurface(surface);
}

int emucorex_core_start_game(const char* utf8_path)
{
    return emucorex::desktop::Start(utf8_path, false) ? 1 : 0;
}

int emucorex_core_start_bios(void)
{
    return emucorex::desktop::Start(nullptr, true) ? 1 : 0;
}

uint32_t emucorex_core_state(void)
{
    return static_cast<uint32_t>(emucorex::desktop::GetState());
}

void emucorex_core_set_paused(int paused)
{
    emucorex::desktop::SetPaused(paused != 0);
}

void emucorex_core_shutdown(void)
{
    emucorex::desktop::Shutdown();
}

int emucorex_core_save_state(int slot)
{
    return emucorex::desktop::SaveState(slot) ? 1 : 0;
}

int emucorex_core_load_state(int slot)
{
    return emucorex::desktop::LoadState(slot) ? 1 : 0;
}

uint32_t emucorex_core_last_error(char* destination, uint32_t capacity)
{
    return emucorex::desktop::CopyLastError(destination, capacity);
}
