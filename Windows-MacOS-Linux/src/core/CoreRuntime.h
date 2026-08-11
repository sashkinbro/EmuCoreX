#pragma once

#include <QLibrary>
#include <QString>

#include "../../core/include/EmuCoreXCoreApi.h"

class CoreRuntime final
{
public:
    CoreRuntime();

    bool isAvailable() const { return m_inspectGame != nullptr; }
    QString architecture() const { return m_architecture; }
    QString errorString() const { return m_errorString; }
    bool inspectGame(const QString& path, EmuCoreXGameMetadata* metadata) const;

private:
    using AbiVersionFunction = uint32_t (*)();
    using ArchitectureFunction = const char* (*)();
    using InitializeFunction = int (*)(const char*);
    using InspectGameFunction = int (*)(const char*, EmuCoreXGameMetadata*);

    void load();
    QStringList candidatePaths() const;

    QLibrary m_library;
    QString m_architecture;
    QString m_errorString;
    InspectGameFunction m_inspectGame = nullptr;
};
