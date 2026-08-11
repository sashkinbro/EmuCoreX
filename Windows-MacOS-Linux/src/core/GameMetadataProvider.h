#pragma once

#include <QString>

struct GameImageMetadata
{
    QString title;
    QString serial;
    QString region;
    qint64 totalSize = 0;
    bool authoritative = false;
};

class CoreRuntime;

class GameMetadataProvider final
{
public:
    explicit GameMetadataProvider(const CoreRuntime* core = nullptr) : m_core(core) {}
    GameImageMetadata inspect(const QString& path) const;

private:
    static GameImageMetadata fallbackMetadata(const QString& path);
    static QString regionForSerial(const QString& serial);
    const CoreRuntime* m_core = nullptr;
};
