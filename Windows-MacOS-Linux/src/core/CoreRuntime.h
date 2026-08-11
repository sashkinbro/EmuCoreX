#pragma once

#include <QLibrary>
#include <QString>

#include "../../core/include/EmuCoreXCoreApi.h"

class CoreRuntime final
{
public:
    CoreRuntime();
    ~CoreRuntime();

    bool isAvailable() const { return m_startGame != nullptr; }
    QString architecture() const { return m_architecture; }
    QString errorString() const { return m_errorString; }
    QString lastCoreError() const;

    bool inspectGame(const QString& path, EmuCoreXGameMetadata* metadata) const;
    bool configure(const QString& dataPath, const QString& biosPath) const;
    void setRenderSurface(const EmuCoreXRenderSurface* surface) const;
    bool startGame(const QString& path) const;
    bool startBios() const;
    EmuCoreXCoreState state() const;
    void setPaused(bool paused) const;
    void shutdown() const;
    bool saveState(int slot) const;
    bool loadState(int slot) const;

private:
    using AbiVersionFunction = uint32_t (*)();
    using ArchitectureFunction = const char* (*)();
    using InitializeFunction = int (*)(const char*);
    using InspectGameFunction = int (*)(const char*, EmuCoreXGameMetadata*);
    using ConfigureFunction = int (*)(const EmuCoreXCoreConfiguration*);
    using SetRenderSurfaceFunction = void (*)(const EmuCoreXRenderSurface*);
    using StartGameFunction = int (*)(const char*);
    using StartBiosFunction = int (*)();
    using StateFunction = uint32_t (*)();
    using SetPausedFunction = void (*)(int);
    using ShutdownFunction = void (*)();
    using StateSlotFunction = int (*)(int);
    using LastErrorFunction = uint32_t (*)(char*, uint32_t);

    void load();
    QStringList candidatePaths() const;

    QLibrary m_library;
    QString m_coreDirectory;
    QString m_architecture;
    QString m_errorString;
    InspectGameFunction m_inspectGame = nullptr;
    ConfigureFunction m_configure = nullptr;
    SetRenderSurfaceFunction m_setRenderSurface = nullptr;
    StartGameFunction m_startGame = nullptr;
    StartBiosFunction m_startBios = nullptr;
    StateFunction m_state = nullptr;
    SetPausedFunction m_setPaused = nullptr;
    ShutdownFunction m_shutdown = nullptr;
    StateSlotFunction m_saveState = nullptr;
    StateSlotFunction m_loadState = nullptr;
    LastErrorFunction m_lastError = nullptr;
};
