// SPDX-License-Identifier: GPL-3.0+

#include "pcsx2/Achievements.h"
#include "pcsx2/GS.h"
#include "pcsx2/GameList.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/VMManager.h"

#include "common/ProgressCallback.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
void logMessage(const char* level, const std::string_view title, const std::string_view message)
{
    if (!title.empty())
        std::fprintf(stderr, "EmuCoreXCore [%s] %.*s: %.*s\n", level,
            static_cast<int>(title.size()), title.data(), static_cast<int>(message.size()), message.data());
    else
        std::fprintf(stderr, "EmuCoreXCore [%s] %.*s\n", level,
            static_cast<int>(message.size()), message.data());
}
}

void Host::CommitBaseSettingChanges()
{
}

void Host::LoadSettings(SettingsInterface&, std::unique_lock<std::mutex>&)
{
}

void Host::CheckForSettingsChanges(const Pcsx2Config&)
{
}

bool Host::RequestResetSettings(bool, bool, bool, bool, bool)
{
    return false;
}

void Host::SetDefaultUISettings(SettingsInterface&)
{
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
    return ProgressCallback::CreateNullProgressCallback();
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
    logMessage("info", title, message);
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
    logMessage("error", title, message);
}

void Host::OpenURL(const std::string_view)
{
}

bool Host::InBatchMode()
{
    return false;
}

bool Host::InNoGUIMode()
{
    return false;
}

bool Host::CopyTextToClipboard(const std::string_view)
{
    return false;
}

void Host::BeginTextInput()
{
}

void Host::EndTextInput()
{
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
    return std::nullopt;
}

void Host::OnInputDeviceConnected(const std::string_view, const std::string_view)
{
}

void Host::OnInputDeviceDisconnected(const InputBindingKey, const std::string_view)
{
}

void Host::SetMouseMode(bool, bool)
{
}

void Host::SetMouseLock(bool)
{
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool)
{
    return std::nullopt;
}

void Host::ReleaseRenderWindow()
{
}

void Host::BeginPresentFrame()
{
}

void Host::RequestResizeHostDisplay(s32, s32)
{
}

void Host::OnVMStarting()
{
}

void Host::OnVMStarted()
{
}

void Host::OnVMDestroyed()
{
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
}

void Host::OnGameChanged(const std::string&, const std::string&, const std::string&, const std::string&, u32, u32)
{
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnSaveStateLoading(const std::string_view)
{
}

void Host::OnSaveStateLoaded(const std::string_view, bool)
{
}

void Host::OnSaveStateSaved(const std::string_view)
{
}

void Host::RunOnCPUThread(std::function<void()> function, bool)
{
    if (function)
        function();
}

void Host::RunOnGSThread(std::function<void()> function)
{
    if (function)
        function();
}

void Host::RefreshGameListAsync(bool)
{
}

void Host::CancelGameListRefresh()
{
}

bool Host::IsFullscreen()
{
    return false;
}

void Host::SetFullscreen(bool)
{
}

void Host::OnCaptureStarted(const std::string&)
{
}

void Host::OnCaptureStopped()
{
}

void Host::RequestExitApplication(bool)
{
}

void Host::RequestExitBigPicture()
{
}

void Host::RequestVMShutdown(bool, bool, bool)
{
    if (VMManager::HasValidVM())
        VMManager::SetState(VMState::Stopping);
}

void Host::PumpMessagesOnCPUThread()
{
}

s32 Host::Internal::GetTranslatedStringImpl(
    const std::string_view, const std::string_view message, char* buffer, size_t bufferSpace)
{
    if (message.size() > bufferSpace)
        return -1;
    if (message.empty())
        return 0;

    std::memcpy(buffer, message.data(), message.size());
    return static_cast<s32>(message.size());
}

std::string Host::TranslatePluralToString(const char*, const char* message, const char*, int count)
{
    std::string result(message ? message : "");
    const std::string countString = std::to_string(count);
    for (size_t position = result.find("%n"); position != std::string::npos; position = result.find("%n", position))
        result.replace(position, 2, countString);
    return result;
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason)
{
}

void Host::OnAchievementsLoginSuccess(const char*, u32, u32, u32)
{
}

void Host::OnAchievementsRefreshed()
{
}

void Host::OnAchievementsHardcoreModeChanged(bool)
{
}

void Host::OnCoverDownloaderOpenRequested()
{
}

void Host::OnCreateMemoryCardOpenRequested()
{
}

bool Host::LocaleCircleConfirm()
{
    return false;
}

bool Host::ShouldPreferHostFileSelector()
{
    return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view, bool, FileSelectorCallback callback,
    FileSelectorFilters, std::string_view)
{
    if (callback)
        callback(std::string());
}

int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs)
{
    const size_t commonLength = std::min(lhs.size(), rhs.size());
    const int result = std::strncmp(lhs.data(), rhs.data(), commonLength);
    if (result != 0)
        return result;
    return (lhs.size() > rhs.size()) ? 1 : ((lhs.size() < rhs.size()) ? -1 : 0);
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view)
{
    return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32)
{
    return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32)
{
    return nullptr;
}

BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()
