#include "SingleInstanceGuard.h"

#include <QLocalServer>
#include <QLocalSocket>

SingleInstanceGuard::SingleInstanceGuard(QString instanceKey, QObject* parent)
    : QObject(parent)
    , m_instanceKey(std::move(instanceKey))
{
    QLocalSocket probe;
    probe.connectToServer(m_instanceKey, QIODevice::WriteOnly);
    if (probe.waitForConnected(180)) {
        probe.write("activate");
        probe.flush();
        probe.waitForBytesWritten(180);
        return;
    }

    QLocalServer::removeServer(m_instanceKey);
    m_server = new QLocalServer(this);
    m_primary = m_server->listen(m_instanceKey);
    if (m_primary) {
        connect(m_server, &QLocalServer::newConnection, this, &SingleInstanceGuard::acceptConnection);
    }
}

void SingleInstanceGuard::acceptConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            if (socket->readAll().contains("activate"))
                emit activationRequested();
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

