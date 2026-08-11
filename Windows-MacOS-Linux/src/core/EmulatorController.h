#pragma once

#include <QObject>
#include <QTimer>

#include "../../core/include/EmuCoreXCoreApi.h"

class CoreRuntime;
class SettingsStore;
class TranslationManager;

class EmulatorController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendNameChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString currentGame READ currentGame NOTIFY currentGameChanged)

public:
    explicit EmulatorController(CoreRuntime* runtime, SettingsStore* settings,
        TranslationManager* translations, QObject* parent = nullptr);

    bool running() const { return m_running; }
    bool paused() const { return m_paused; }
    bool available() const;
    QString backendName() const;
    QString statusText() const { return m_statusText; }
    QString currentGame() const { return m_currentGame; }

    Q_INVOKABLE bool bootGame(const QString& path);
    Q_INVOKABLE bool bootBios();
    Q_INVOKABLE void pause(bool paused);
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void saveState(int slot);
    Q_INVOKABLE void loadState(int slot);
    Q_INVOKABLE void setRenderSurface(quintptr windowHandle, int width, int height,
        qreal scale = 1.0, qreal refreshRate = 60.0);
    Q_INVOKABLE void clearRenderSurface();

signals:
    void runningChanged();
    void pausedChanged();
    void backendNameChanged();
    void statusTextChanged();
    void currentGameChanged();
    void errorOccurred(const QString& message);

private:
    QString text(const QString& key) const;
    bool prepareCore();
    void startPendingSession();
    void pollState();
    void setStatus(QString status);
    void reportError(const QString& fallbackKey);

    CoreRuntime* m_runtime = nullptr;
    SettingsStore* m_settings = nullptr;
    TranslationManager* m_translations = nullptr;
    QTimer m_stateTimer;
    EmuCoreXCoreState m_lastState = EMUCOREX_CORE_STATE_STOPPED;
    bool m_running = false;
    bool m_paused = false;
    bool m_surfaceReady = false;
    bool m_startPending = false;
    bool m_pendingBootBios = false;
    QString m_statusText;
    QString m_currentGame;
};
