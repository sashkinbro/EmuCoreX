#include "CoverArtService.h"

#include "../settings/SettingsStore.h"

#include <QRegularExpression>
#include <QUrl>

namespace {
constexpr auto kFlatCoverBase = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default";
constexpr auto kThreeDimensionalCoverBase = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/3d";
}

CoverArtService::CoverArtService(SettingsStore* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    connect(settings, &SettingsStore::coverArtStyleChanged, this, &CoverArtService::invalidate);
    connect(settings, &SettingsStore::valueChanged, this, [this](const QString& key, const QVariant&) {
        if (key == QLatin1String("library/coverDownloadBaseUrl"))
            invalidate();
    });
}

QString CoverArtService::urlForSerial(const QString& serial, int style) const
{
    if (style <= 0)
        return {};
    const QString normalized = normalizedSerial(serial);
    if (normalized.isEmpty())
        return {};

    const bool is3d = style == 2;
    QString base = QString::fromLatin1(is3d ? kThreeDimensionalCoverBase : kFlatCoverBase);
    const QString configured = m_settings->value(QStringLiteral("library/coverDownloadBaseUrl")).toString().trimmed();
    if (!configured.isEmpty()) {
        const QStringList urls = configured.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (!urls.isEmpty())
            base = is3d && urls.size() > 1 ? urls.at(1) : urls.constFirst();
    }
    base = base.trimmed();
    while (base.endsWith('/'))
        base.chop(1);
    const QUrl baseUrl(base);
    if (!baseUrl.isValid() || (baseUrl.scheme() != QLatin1String("https") && baseUrl.scheme() != QLatin1String("http")))
        return {};

    return QStringLiteral("%1/%2.%3?v=%4")
        .arg(base, normalized, is3d ? QStringLiteral("png") : QStringLiteral("jpg"))
        .arg(m_revision);
}

void CoverArtService::invalidate()
{
    ++m_revision;
    emit revisionChanged();
}

QString CoverArtService::normalizedSerial(const QString& serial)
{
    static const QRegularExpression split(QStringLiteral("([A-Za-z]{4})[^A-Za-z0-9]*([0-9]{3})[^A-Za-z0-9]*([0-9]{2})"));
    static const QRegularExpression compact(QStringLiteral("([A-Za-z]{4})[^A-Za-z0-9]*([0-9]{5})"));
    const QString upper = serial.trimmed().toUpper();
    auto match = split.match(upper);
    if (match.hasMatch())
        return match.captured(1) + '-' + match.captured(2) + match.captured(3);
    match = compact.match(upper);
    if (match.hasMatch())
        return match.captured(1) + '-' + match.captured(2);
    QString clean = upper;
    clean.remove(QRegularExpression(QStringLiteral("[^A-Z0-9_-]")));
    return clean;
}
