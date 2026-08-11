#include "DesktopCoreSession.h"

#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Config.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/R5900.h"
#include "pcsx2/VMManager.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/Threading.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace emucorex::desktop
{
namespace
{
struct Configuration
{
    std::string resources;
    std::string data;
    std::string bios;
};

std::mutex s_sessionMutex;
Configuration s_configuration;
EmuCoreXRenderSurface s_surface = {};
std::thread s_vmThread;
std::thread::id s_cpuThreadId;
std::atomic<EmuCoreXCoreState> s_state {EMUCOREX_CORE_STATE_STOPPED};
std::atomic<bool> s_stopRequested {false};
std::string s_lastError;

std::mutex s_taskMutex;
std::deque<std::function<void()>> s_tasks;

MemorySettingsInterface s_baseSettings;
MemorySettingsInterface s_secretsSettings;
std::vector<u8> s_standardFont;
std::vector<u8> s_emojiFont;

void SetError(std::string message)
{
    std::fprintf(stderr, "EmuCoreXCore [error] %s\n", message.c_str());
    std::fflush(stderr);
    std::lock_guard lock(s_sessionMutex);
    s_lastError = std::move(message);
    s_state.store(EMUCOREX_CORE_STATE_ERROR, std::memory_order_release);
}

void SetLastError(std::string message)
{
    std::lock_guard lock(s_sessionMutex);
    s_lastError = std::move(message);
}

void ConfigureFonts()
{
    if (s_standardFont.empty())
    {
        const auto data = FileSystem::ReadBinaryFile(
            Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf").c_str());
        if (data.has_value())
            s_standardFont = data.value();
    }
    if (s_emojiFont.empty())
    {
        const auto data = FileSystem::ReadBinaryFile(
            Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Twemoji.Mozilla.ttf").c_str());
        if (data.has_value())
            s_emojiFont = data.value();
    }

    std::vector<ImGuiManager::FontInfo> fonts;
    if (!s_standardFont.empty())
        fonts.push_back({s_standardFont, {}, nullptr, false});
    if (!s_emojiFont.empty())
        fonts.push_back({s_emojiFont, {}, nullptr, true});
    if (!fonts.empty())
        ImGuiManager::SetFonts(std::move(fonts));
}

bool InstallSettings(const Configuration& configuration, std::string* error)
{
    EmuFolders::AppRoot = configuration.data;
    EmuFolders::DataRoot = configuration.data;
    EmuFolders::Resources = configuration.resources;
    EmuFolders::UserResources = configuration.resources;
    EmuFolders::Settings = Path::Combine(configuration.data, "inis");
    EmuFolders::Cache = Path::Combine(configuration.data, "cache");

    {
        std::unique_lock settingsLock = Host::GetSettingsLock();
        if (!Host::Internal::GetBaseSettingsLayer())
            Host::Internal::SetBaseSettingsLayer(&s_baseSettings);
        s_baseSettings.Clear();
        VMManager::SetDefaultSettings(s_baseSettings, true, true, true, true, true);

        if (!configuration.bios.empty())
        {
            const std::string biosDirectory(Path::GetDirectory(configuration.bios));
            const std::string biosFile(Path::GetFileName(configuration.bios));
            s_baseSettings.SetStringValue("Folders", "Bios", biosDirectory.c_str());
            s_baseSettings.SetStringValue("EmuCore", "BIOS", biosFile.c_str());
        }
        s_baseSettings.SetBoolValue("Achievements", "Enabled", false);
        s_baseSettings.SetBoolValue("InputSources", "SDL", true);
        s_baseSettings.SetBoolValue("InputSources", "PadVibration", true);
        s_baseSettings.SetStringValue("Pad1", "Type", "DualShock2");
        s_baseSettings.SetStringValue("Pad2", "Type", "DualShock2");
        EmuFolders::LoadConfig(s_baseSettings);
        if (!EmuFolders::EnsureFoldersExist())
        {
            if (error)
                *error = "Could not create the PCSX2 data directories.";
            return false;
        }
    }

    {
        std::unique_lock secretsLock = Host::GetSecretsSettingsLock();
        if (!Host::Internal::GetSecretsSettingsLayer())
            Host::Internal::SetSecretsSettingsLayer(&s_secretsSettings);
        s_secretsSettings.Clear();
    }

    ConfigureFonts();
    return true;
}

void VMThreadMain(Configuration configuration, std::string path, bool bootBios)
{
    s_cpuThreadId = std::this_thread::get_id();
    if (s_stopRequested.load(std::memory_order_acquire))
    {
        s_cpuThreadId = {};
        s_state.store(EMUCOREX_CORE_STATE_STOPPED, std::memory_order_release);
        return;
    }
    std::string setupError;
    if (!InstallSettings(configuration, &setupError))
    {
        SetError(std::move(setupError));
        s_cpuThreadId = {};
        return;
    }

    const char* hardwareError = nullptr;
    if (!VMManager::PerformEarlyHardwareChecks(&hardwareError))
    {
        SetError(hardwareError ? hardwareError : "This CPU cannot run the selected PCSX2 core.");
        s_cpuThreadId = {};
        return;
    }

    if (!VMManager::Internal::CPUThreadInitialize())
    {
        SetError("PCSX2 CPU-thread initialization failed.");
        s_cpuThreadId = {};
        return;
    }

    PerformanceMetrics::SetCPUThread(Threading::ThreadHandle::GetForCallingThread());
    PerformanceMetrics::SetGSSWThreadCount(0);
    VMManager::ApplySettings();

    VMBootParameters parameters;
    parameters.fast_boot = !bootBios;
    parameters.fullscreen = false;
    parameters.start_turbo = false;
    parameters.start_unlimited = false;
    parameters.disable_achievements_hardcore_mode = true;
    if (bootBios)
    {
        parameters.source_type = CDVD_SourceType::NoDisc;
    }
    else
    {
        parameters.filename = std::move(path);
        parameters.source_type = CDVD_SourceType::Iso;
    }

    Error bootError;
    if (VMManager::Initialize(parameters, &bootError) != VMBootResult::StartupSuccess)
    {
        SetError(bootError.GetDescription());
        VMManager::Internal::CPUThreadShutdown();
        PerformanceMetrics::SetCPUThread(Threading::ThreadHandle());
        s_cpuThreadId = {};
        return;
    }

    if (s_stopRequested.load(std::memory_order_acquire))
        VMManager::SetState(VMState::Stopping);

    s_state.store(EMUCOREX_CORE_STATE_RUNNING, std::memory_order_release);
    VMManager::SetState(VMState::Running);
    for (;;)
    {
        PumpCPUThreadTasks();
        const VMState state = VMManager::GetState();
        if (state == VMState::Stopping || state == VMState::Shutdown)
            break;
        if (state == VMState::Paused)
        {
            s_state.store(EMUCOREX_CORE_STATE_PAUSED, std::memory_order_release);
            VMManager::IdlePollUpdate();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }
        s_state.store(EMUCOREX_CORE_STATE_RUNNING, std::memory_order_release);
        VMManager::Execute();
    }

    s_state.store(EMUCOREX_CORE_STATE_STOPPING, std::memory_order_release);
    VMManager::Shutdown(false);
    VMManager::Internal::CPUThreadShutdown();
    PerformanceMetrics::SetCPUThread(Threading::ThreadHandle());
    PerformanceMetrics::SetGSSWThreadCount(0);
    PumpCPUThreadTasks();
    s_cpuThreadId = {};
    s_state.store(EMUCOREX_CORE_STATE_STOPPED, std::memory_order_release);
}
}

bool Configure(const EmuCoreXCoreConfiguration& configuration)
{
    if (!configuration.resources_path || !configuration.data_path)
        return false;
    std::lock_guard lock(s_sessionMutex);
    if (s_state.load(std::memory_order_acquire) != EMUCOREX_CORE_STATE_STOPPED)
        return false;
    s_configuration.resources = configuration.resources_path;
    s_configuration.data = configuration.data_path;
    s_configuration.bios = configuration.bios_path ? configuration.bios_path : "";
    s_lastError.clear();
    return true;
}

void SetRenderSurface(const EmuCoreXRenderSurface* surface)
{
    std::lock_guard lock(s_sessionMutex);
    s_surface = surface ? *surface : EmuCoreXRenderSurface{};
}

bool Start(const char* path, bool bootBios)
{
    std::unique_lock lock(s_sessionMutex);
    if (s_state.load(std::memory_order_acquire) != EMUCOREX_CORE_STATE_STOPPED || s_configuration.data.empty())
        return false;
    if (!bootBios && (!path || !*path))
        return false;
    if (s_vmThread.joinable())
    {
        std::thread previous = std::move(s_vmThread);
        lock.unlock();
        previous.join();
        lock.lock();
        if (s_state.load(std::memory_order_acquire) != EMUCOREX_CORE_STATE_STOPPED)
            return false;
    }
    s_lastError.clear();
    s_stopRequested.store(false, std::memory_order_release);
    s_state.store(EMUCOREX_CORE_STATE_STARTING, std::memory_order_release);
    s_vmThread = std::thread(VMThreadMain, s_configuration, path ? path : "", bootBios);
    return true;
}

EmuCoreXCoreState GetState()
{
    return s_state.load(std::memory_order_acquire);
}

void RunOnCPUThread(std::function<void()> function, bool block)
{
    if (!function)
        return;
    if (std::this_thread::get_id() == s_cpuThreadId)
    {
        function();
        return;
    }

    if (!block)
    {
        std::lock_guard lock(s_taskMutex);
        s_tasks.emplace_back(std::move(function));
        if (Cpu)
            Cpu->ExitExecution();
        return;
    }

    auto completion = std::make_shared<std::promise<void>>();
    std::future<void> future = completion->get_future();
    {
        std::lock_guard lock(s_taskMutex);
        s_tasks.emplace_back([function = std::move(function), completion] {
            function();
            completion->set_value();
        });
    }
    if (Cpu)
        Cpu->ExitExecution();
    future.wait();
}

void PumpCPUThreadTasks()
{
    std::deque<std::function<void()>> tasks;
    {
        std::lock_guard lock(s_taskMutex);
        tasks.swap(s_tasks);
    }
    for (auto& task : tasks)
        task();
}

void SetPaused(bool paused)
{
    if (GetState() != EMUCOREX_CORE_STATE_RUNNING && GetState() != EMUCOREX_CORE_STATE_PAUSED)
        return;
    if (Cpu)
        Cpu->ExitExecution();
    RunOnCPUThread([paused] { VMManager::SetPaused(paused); }, false);
}

void Shutdown()
{
    const EmuCoreXCoreState current = GetState();
    if (current == EMUCOREX_CORE_STATE_STOPPED)
        return;
    s_stopRequested.store(true, std::memory_order_release);
    s_state.store(EMUCOREX_CORE_STATE_STOPPING, std::memory_order_release);
    if (Cpu)
        Cpu->ExitExecution();
    if (VMManager::HasValidVM())
        VMManager::SetState(VMState::Stopping);
    if (s_vmThread.joinable() && s_vmThread.get_id() != std::this_thread::get_id())
        s_vmThread.join();
}

bool SaveState(int slot)
{
    if (slot < 0 || slot >= VMManager::NUM_SAVE_STATE_SLOTS || !VMManager::HasValidVM())
        return false;
    RunOnCPUThread([slot] { VMManager::SaveStateToSlot(slot, true, [](const std::string&) {}); }, false);
    return true;
}

bool LoadState(int slot)
{
    if (slot < 0 || slot >= VMManager::NUM_SAVE_STATE_SLOTS || !VMManager::HasValidVM())
        return false;
    RunOnCPUThread([slot] {
        Error error;
        if (!VMManager::LoadStateFromSlot(slot, false, &error))
            SetLastError(error.GetDescription());
    }, false);
    return true;
}

WindowInfo GetWindowInfo()
{
    std::lock_guard lock(s_sessionMutex);
    WindowInfo info;
    switch (s_surface.type)
    {
        case EMUCOREX_SURFACE_WIN32: info.type = WindowInfo::Type::Win32; break;
        case EMUCOREX_SURFACE_X11: info.type = WindowInfo::Type::X11; break;
        case EMUCOREX_SURFACE_WAYLAND: info.type = WindowInfo::Type::Wayland; break;
        case EMUCOREX_SURFACE_MACOS: info.type = WindowInfo::Type::MacOS; break;
        default: info.type = WindowInfo::Type::Surfaceless; break;
    }
    info.display_connection = s_surface.display_connection;
    info.window_handle = s_surface.window_handle;
    info.surface_handle = s_surface.surface_handle;
    info.surface_width = s_surface.width;
    info.surface_height = s_surface.height;
    info.surface_scale = std::max(s_surface.scale, 1.0f);
    info.surface_refresh_rate = s_surface.refresh_rate;
    return info;
}

uint32_t CopyLastError(char* destination, uint32_t capacity)
{
    std::lock_guard lock(s_sessionMutex);
    const uint32_t required = static_cast<uint32_t>(s_lastError.size() + 1);
    if (destination && capacity > 0)
    {
        const size_t count = std::min<size_t>(s_lastError.size(), capacity - 1);
        std::memcpy(destination, s_lastError.data(), count);
        destination[count] = '\0';
    }
    return required;
}
}
