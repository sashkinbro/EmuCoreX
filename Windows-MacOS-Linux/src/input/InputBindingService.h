#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

class InputBindingService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(int listeningPlayer READ listeningPlayer NOTIFY listeningChanged)
    Q_PROPERTY(QString listeningAction READ listeningAction NOTIFY listeningChanged)
    Q_PROPERTY(QStringList connectedDevices READ connectedDevices NOTIFY devicesChanged)
    Q_PROPERTY(int revision READ revision NOTIFY mappingsChanged)

public:
    explicit InputBindingService(QObject* parent = nullptr);

    bool listening() const { return m_listening; }
    int listeningPlayer() const { return m_listeningPlayer; }
    QString listeningAction() const { return m_listeningAction; }
    QStringList connectedDevices() const { return m_connectedDevices; }
    int revision() const { return m_revision; }

    Q_INVOKABLE QString binding(int player, const QString& action) const;
    Q_INVOKABLE QString bindingDisplay(int player, const QString& action) const;
    Q_INVOKABLE void beginListening(int player, const QString& action);
    Q_INVOKABLE void cancelListening();
    Q_INVOKABLE void clearBinding(int player, const QString& action);
    Q_INVOKABLE void resetPlayer(int player);

signals:
    void listeningChanged();
    void mappingsChanged();
    void devicesChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static QString settingsKey(int player, const QString& action);
    static QString defaultBinding(int player, const QString& action);
    void finishListening(const QString& binding);

    QSettings m_settings;
    QStringList m_connectedDevices;
    bool m_listening = false;
    int m_listeningPlayer = 1;
    QString m_listeningAction;
    int m_revision = 0;
};
