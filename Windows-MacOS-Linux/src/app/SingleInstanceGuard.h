#pragma once

#include <QObject>

class QLocalServer;

class SingleInstanceGuard final : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceGuard(QString instanceKey, QObject* parent = nullptr);
    [[nodiscard]] bool isPrimary() const noexcept { return m_primary; }

signals:
    void activationRequested();

private:
    void acceptConnection();

    QString m_instanceKey;
    QLocalServer* m_server = nullptr;
    bool m_primary = false;
};

