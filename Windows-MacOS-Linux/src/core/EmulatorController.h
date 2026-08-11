#pragma once

#include <QObject>

class EmulatorController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString backendName READ backendName CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString currentGame READ currentGame NOTIFY currentGameChanged)

public:
    explicit EmulatorController(QObject* parent = nullptr);

    bool running() const { return m_running; }
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

signals:
    void runningChanged();
    void statusTextChanged();
    void currentGameChanged();
    void errorOccurred(const QString& message);

private:
    void setStatus(QString status);

    bool m_running = false;
    QString m_statusText;
    QString m_currentGame;
};

