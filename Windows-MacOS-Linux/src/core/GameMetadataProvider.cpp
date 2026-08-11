#include "GameMetadataProvider.h"
#include "CoreRuntime.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace {
QString normalizeSerial(const QString& value)
{
    static const QRegularExpression separated(QStringLiteral("\\b([A-Za-z]{4})[-_. ]?([0-9]{3})[-_. ]?([0-9]{2})\\b"));
    static const QRegularExpression compact(QStringLiteral("\\b([A-Za-z]{4})[-_. ]?([0-9]{5})\\b"));
    const QString upper = value.toUpper();
    auto match = separated.match(upper);
    if (match.hasMatch())
        return match.captured(1) + '-' + match.captured(2) + match.captured(3);
    match = compact.match(upper);
    return match.hasMatch() ? match.captured(1) + '-' + match.captured(2) : QString{};
}
}

GameImageMetadata GameMetadataProvider::inspect(const QString& path) const
{
    GameImageMetadata metadata = fallbackMetadata(path);
    EmuCoreXGameMetadata coreMetadata{};
    if (m_core && m_core->inspectGame(path, &coreMetadata)) {
        const QString coreTitle = QString::fromUtf8(coreMetadata.title).trimmed();
        const QString coreSerial = QString::fromLatin1(coreMetadata.serial).trimmed();
        const QString coreRegion = QString::fromUtf8(coreMetadata.region).trimmed();
        if (!coreTitle.isEmpty())
            metadata.title = coreTitle;
        if (!coreSerial.isEmpty())
            metadata.serial = coreSerial;
        if (!coreRegion.isEmpty())
            metadata.region = coreRegion;
        metadata.totalSize = static_cast<qint64>(coreMetadata.total_size);
        metadata.authoritative = true;
    }
    if (metadata.region.isEmpty())
        metadata.region = regionForSerial(metadata.serial);
    return metadata;
}

GameImageMetadata GameMetadataProvider::fallbackMetadata(const QString& path)
{
    QString title = QFileInfo(path).completeBaseName().trimmed();
    const QString serial = normalizeSerial(title);
    title.remove(QRegularExpression(QStringLiteral("(?i)\\b[A-Z]{4}[-_. ]?[0-9]{3}[-_. ]?[0-9]{2}\\b")));
    title.remove(QRegularExpression(QStringLiteral("(?i)\\b[A-Z]{4}[-_. ]?[0-9]{5}\\b")));
    title.remove(QRegularExpression(QStringLiteral("\\[[^]]*\\]|\\([^)]*\\)")));
    title.remove(QRegularExpression(QStringLiteral("(?i)\\b(disc|disk|cd|dvd)\\s*[0-9]+\\b")));
    title.replace('_', ' ');
    title.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    title = title.trimmed();
    if (title.isEmpty())
        title = QFileInfo(path).completeBaseName();
    return {title, serial, regionForSerial(serial), 0, false};
}

QString GameMetadataProvider::regionForSerial(const QString& serial)
{
    const QString prefix = serial.left(4).toUpper();
    if (prefix == QLatin1String("SLUS") || prefix == QLatin1String("SCUS"))
        return QStringLiteral("NTSC-U");
    if (prefix == QLatin1String("SLPS") || prefix == QLatin1String("SCPS") || prefix == QLatin1String("SLPM"))
        return QStringLiteral("NTSC-J");
    if (prefix == QLatin1String("SLKA") || prefix == QLatin1String("SCKA"))
        return QStringLiteral("NTSC-K");
    if (prefix == QLatin1String("SLES") || prefix == QLatin1String("SCES"))
        return QStringLiteral("PAL");
    if (prefix == QLatin1String("SLAJ") || prefix == QLatin1String("SCAJ"))
        return QStringLiteral("NTSC-C");
    return {};
}
