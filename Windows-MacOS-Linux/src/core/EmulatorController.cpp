#include "EmulatorController.h"

#include <QFileInfo>
#include <QSysInfo>

EmulatorController::EmulatorController(QObject* parent)
    : QObject(parent)
{
    setStatus(available() ? QStringLiteral("Core ready") : QStringLiteral("UI development build · core adapter is disabled"));
}

bool EmulatorController::available() const
{
#ifdef EMUCOREX_WITH_PCSX2
    return true;
#else
    return false;
#endif
}

QString EmulatorController::backendName() const
{
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    const bool arm64 = arch == "arm64" || arch == "aarch64";
    return arm64 ? QStringLiteral("EmuCoreX PCSX2 · ARM64 recompiler") : QStringLiteral("EmuCoreX PCSX2 · x64 recompiler");
}

bool EmulatorController::bootGame(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        const QString message = QStringLiteral("Game image was not found: %1").arg(path);
        setStatus(message);
        emit errorOccurred(message);
        return false;
    }
    if (!available()) {
        const QString message = QStringLiteral("The embedded PCSX2 adapter is not enabled in this build yet.");
        setStatus(message);
        emit errorOccurred(message);
        return false;
    }
    m_currentGame = path;
    m_running = true;
    emit currentGameChanged();
    emit runningChanged();
    setStatus(QStringLiteral("Running %1").arg(QFileInfo(path).completeBaseName()));
    return true;
}

bool EmulatorController::bootBios()
{
    if (!available()) {
        const QString message = QStringLiteral("The embedded PCSX2 adapter is not enabled in this build yet.");
        setStatus(message);
        emit errorOccurred(message);
        return false;
    }
    m_currentGame.clear();
    m_running = true;
    emit currentGameChanged();
    emit runningChanged();
    setStatus(QStringLiteral("Running BIOS"));
    return true;
}

void EmulatorController::pause(bool paused)
{
    if (m_running)
        setStatus(paused ? QStringLiteral("Paused") : QStringLiteral("Running"));
}

void EmulatorController::shutdown()
{
    if (!m_running)
        return;
    m_running = false;
    m_currentGame.clear();
    emit runningChanged();
    emit currentGameChanged();
    setStatus(QStringLiteral("Stopped"));
}

void EmulatorController::saveState(int slot)
{
    if (!m_running)
        emit errorOccurred(QStringLiteral("Start a game before saving state."));
    else
        setStatus(QStringLiteral("Saved state to slot %1").arg(slot));
}

void EmulatorController::loadState(int slot)
{
    if (!m_running)
        emit errorOccurred(QStringLiteral("Start a game before loading state."));
    else
        setStatus(QStringLiteral("Loaded state from slot %1").arg(slot));
}

void EmulatorController::setStatus(QString status)
{
    if (m_statusText == status)
        return;
    m_statusText = std::move(status);
    emit statusTextChanged();
}

