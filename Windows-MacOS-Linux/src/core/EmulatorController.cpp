#include "EmulatorController.h"

#include "CoreRuntime.h"
#include "../i18n/TranslationManager.h"
#include "../settings/SettingsStore.h"

#include <QDir>
#include <QFileInfo>

EmulatorController::EmulatorController(CoreRuntime* runtime, SettingsStore* settings,
    TranslationManager* translations, QObject* parent)
    : QObject(parent)
    , m_runtime(runtime)
    , m_settings(settings)
    , m_translations(translations)
{
    m_stateTimer.setInterval(100);
    connect(&m_stateTimer, &QTimer::timeout, this, &EmulatorController::pollState);
    m_stateTimer.start();
    if (m_translations) {
        connect(m_translations, &TranslationManager::stringsChanged, this, [this] {
            emit backendNameChanged();
            pollState();
        });
    }
    setStatus(available() ? text(QStringLiteral("home_status_ready"))
                          : text(QStringLiteral("emulation_launch_failed")));
}

bool EmulatorController::available() const
{
    return m_runtime && m_runtime->isAvailable();
}

QString EmulatorController::backendName() const
{
    return text(QStringLiteral("settings_emulator_core_desc"));
}

QString EmulatorController::text(const QString& key) const
{
    return m_translations ? m_translations->get(key) : key;
}

bool EmulatorController::prepareCore()
{
    if (!available() || !m_settings) {
        reportError(QStringLiteral("emulation_launch_failed"));
        return false;
    }

    const QString biosPath = m_settings->biosPath();
    if (biosPath.isEmpty() || !QFileInfo::exists(biosPath)) {
        setStatus(text(QStringLiteral("emulation_bios_missing")));
        emit errorOccurred(m_statusText);
        return false;
    }

    const QString dataPath = m_settings->emulatorDataPath();
    if (dataPath.isEmpty() || !QDir().mkpath(dataPath) || !m_runtime->configure(dataPath, biosPath)) {
        reportError(QStringLiteral("emulation_launch_failed"));
        return false;
    }
    return true;
}

bool EmulatorController::bootGame(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        setStatus(text(QStringLiteral("emulation_launch_path_error")));
        emit errorOccurred(m_statusText);
        return false;
    }
    if (!prepareCore())
        return false;

    setStatus(text(QStringLiteral("emulation_status_starting_core")));
    m_currentGame = QFileInfo(path).absoluteFilePath();
    emit currentGameChanged();
    m_pendingBootBios = false;
    m_startPending = true;
    startPendingSession();
    return true;
}

bool EmulatorController::bootBios()
{
    if (!prepareCore())
        return false;

    setStatus(text(QStringLiteral("emulation_status_starting_core")));
    if (!m_currentGame.isEmpty()) {
        m_currentGame.clear();
        emit currentGameChanged();
    }
    m_pendingBootBios = true;
    m_startPending = true;
    startPendingSession();
    return true;
}

void EmulatorController::startPendingSession()
{
    if (!m_startPending || !m_surfaceReady || !m_runtime)
        return;

    m_startPending = false;
    const bool started = m_pendingBootBios ? m_runtime->startBios()
                                           : m_runtime->startGame(m_currentGame);
    if (!started)
        reportError(QStringLiteral("emulation_launch_failed"));
    pollState();
}

void EmulatorController::pause(bool paused)
{
    if (m_runtime && m_running)
        m_runtime->setPaused(paused);
}

void EmulatorController::shutdown()
{
    m_startPending = false;
    if (m_runtime)
        m_runtime->shutdown();
    pollState();
}

void EmulatorController::saveState(int slot)
{
    if (!m_runtime || !m_running || !m_runtime->saveState(slot)) {
        reportError(QStringLiteral("emulation_launch_failed"));
        return;
    }
    setStatus(text(QStringLiteral("emulation_status_saving")));
}

void EmulatorController::loadState(int slot)
{
    if (!m_runtime || !m_running || !m_runtime->loadState(slot)) {
        reportError(QStringLiteral("emulation_load_failed"));
        return;
    }
    setStatus(text(QStringLiteral("emulation_status_loading_state")));
}

void EmulatorController::setRenderSurface(quintptr windowHandle, int width, int height,
    qreal scale, qreal refreshRate)
{
    if (!m_runtime || windowHandle == 0)
        return;
    EmuCoreXRenderSurface surface {};
#if defined(Q_OS_WIN)
    surface.type = EMUCOREX_SURFACE_WIN32;
#elif defined(Q_OS_MACOS)
    surface.type = EMUCOREX_SURFACE_MACOS;
#else
    surface.type = EMUCOREX_SURFACE_X11;
#endif
    surface.window_handle = reinterpret_cast<void*>(windowHandle);
    surface.width = static_cast<uint32_t>(qMax(width, 1));
    surface.height = static_cast<uint32_t>(qMax(height, 1));
    surface.scale = static_cast<float>(qMax(scale, 1.0));
    surface.refresh_rate = static_cast<float>(qMax(refreshRate, 1.0));
    m_runtime->setRenderSurface(&surface);
    m_surfaceReady = true;
    startPendingSession();
}

void EmulatorController::clearRenderSurface()
{
    m_surfaceReady = false;
    if (m_runtime)
        m_runtime->setRenderSurface(nullptr);
}

void EmulatorController::pollState()
{
    if (!m_runtime)
        return;
    const EmuCoreXCoreState state = m_runtime->state();
    const bool running = state == EMUCOREX_CORE_STATE_STARTING || state == EMUCOREX_CORE_STATE_RUNNING
        || state == EMUCOREX_CORE_STATE_PAUSED || state == EMUCOREX_CORE_STATE_STOPPING;
    const bool paused = state == EMUCOREX_CORE_STATE_PAUSED;
    if (m_running != running) {
        m_running = running;
        emit runningChanged();
    }
    if (m_paused != paused) {
        m_paused = paused;
        emit pausedChanged();
    }

    if (state != m_lastState || state == EMUCOREX_CORE_STATE_STOPPED) {
        m_lastState = state;
        switch (state) {
        case EMUCOREX_CORE_STATE_STARTING:
            setStatus(text(QStringLiteral("emulation_status_starting_core")));
            break;
        case EMUCOREX_CORE_STATE_RUNNING:
            setStatus(text(QStringLiteral("emulation_status_running")));
            break;
        case EMUCOREX_CORE_STATE_PAUSED:
            setStatus(text(QStringLiteral("emulation_pause")));
            break;
        case EMUCOREX_CORE_STATE_STOPPING:
            setStatus(text(QStringLiteral("emulation_status_preparing")));
            break;
        case EMUCOREX_CORE_STATE_ERROR:
            reportError(QStringLiteral("emulation_launch_failed"));
            break;
        case EMUCOREX_CORE_STATE_STOPPED:
            setStatus(text(QStringLiteral("home_status_ready")));
            if (!m_currentGame.isEmpty()) {
                m_currentGame.clear();
                emit currentGameChanged();
            }
            break;
        }
    }
}

void EmulatorController::reportError(const QString& fallbackKey)
{
    const QString coreError = m_runtime ? m_runtime->lastCoreError() : QString();
    setStatus(coreError.isEmpty() ? text(fallbackKey) : coreError);
    emit errorOccurred(m_statusText);
}

void EmulatorController::setStatus(QString status)
{
    if (m_statusText == status)
        return;
    m_statusText = std::move(status);
    emit statusTextChanged();
}
