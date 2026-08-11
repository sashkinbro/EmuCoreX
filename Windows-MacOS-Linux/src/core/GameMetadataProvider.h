#pragma once

#include <QString>

struct GameImageMetadata
{
    QString title;
    QString serial;
    QString region;
};

class GameMetadataProvider final
{
public:
    GameImageMetadata inspect(const QString& path) const;

private:
    static GameImageMetadata fallbackMetadata(const QString& path);
    static QString regionForSerial(const QString& serial);
};
