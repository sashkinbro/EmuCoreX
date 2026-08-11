#pragma once

#include "EmuCoreXCoreApi.h"

#include "common/WindowInfo.h"

#include <functional>

namespace emucorex::desktop
{
bool Configure(const EmuCoreXCoreConfiguration& configuration);
void SetRenderSurface(const EmuCoreXRenderSurface* surface);
bool Start(const char* path, bool boot_bios);
EmuCoreXCoreState GetState();
void SetPaused(bool paused);
void Shutdown();
bool SaveState(int slot);
bool LoadState(int slot);
uint32_t CopyLastError(char* destination, uint32_t capacity);

WindowInfo GetWindowInfo();
void RunOnCPUThread(std::function<void()> function, bool block);
void PumpCPUThreadTasks();
}
