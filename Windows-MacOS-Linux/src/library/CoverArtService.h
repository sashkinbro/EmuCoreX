#pragma once

#include <QObject>
#include <QString>

class SettingsStore;

class CoverArtService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit CoverArtService(SettingsStore* settings, QObject* parent = nullptr);

    int revision() const { return m_revision; }
    Q_INVOKABLE QString urlForSerial(const QString& serial, int style) const;
    Q_INVOKABLE void invalidate();

signals:
    void revisionChanged();

private:
    static QString normalizedSerial(const QString& serial);

    SettingsStore* m_settings = nullptr;
    int m_revision = 0;
};
