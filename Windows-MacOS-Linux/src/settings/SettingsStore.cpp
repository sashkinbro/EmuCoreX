#include "SettingsStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QUrl>

namespace {
QString normalizeLocalPath(const QString& value)
{
    QString path = value.trimmed();
    if (path.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        path = QUrl(path).toLocalFile();
    else if (path.contains(QLatin1Char('%'))) {
        const QString decoded = QUrl::fromPercentEncoding(path.toUtf8());
        // Migrate paths previously saved by stripping file:// in QML. Preserve a
        // legitimate percent sign when the encoded path itself actually exists.
        if (decoded != path && !QFileInfo::exists(path) && QFileInfo::exists(decoded))
            path = decoded;
    }
#if defined(Q_OS_WIN)
    if (path.size() >= 4 && path.at(0) == QLatin1Char('/') && path.at(2) == QLatin1Char(':')
        && path.at(1).isLetter())
        path.remove(0, 1);
#endif
    return path.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path));
}
}

SettingsStore::SettingsStore(QObject* parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
          QCoreApplication::organizationName(), QCoreApplication::applicationName())
{
    const QString legacyAccent = m_settings.value("appearance/accent").toString().toUpper();
    if (legacyAccent == QLatin1String("#8B5CF6") || legacyAccent == QLatin1String("#7C3AED"))
        m_settings.setValue("appearance/accent", QStringLiteral("#C4203A"));

    const QString storedBios = m_settings.value("paths/bios").toString();
    const QString normalizedBios = normalizeLocalPath(storedBios);
    if (storedBios != normalizedBios)
        m_settings.setValue("paths/bios", normalizedBios);
    const QString storedData = m_settings.value("paths/data").toString();
    const QString normalizedData = normalizeLocalPath(storedData);
    if (!storedData.isEmpty() && storedData != normalizedData)
        m_settings.setValue("paths/data", normalizedData);
    m_settings.sync();
}

QString SettingsStore::themeMode() const { return m_settings.value("appearance/theme", "dark").toString(); }
QString SettingsStore::accentColor() const { return m_settings.value("appearance/accent", "#C4203A").toString(); }
QString SettingsStore::language() const
{
    const QString systemLanguage = QLocale::system().name().left(2);
    return m_settings.value("general/language", systemLanguage).toString();
}
bool SettingsStore::onboardingCompleted() const { return m_settings.value("setup/completed", false).toBool(); }
QString SettingsStore::biosPath() const { return m_settings.value("paths/bios").toString(); }
QString SettingsStore::emulatorDataPath() const
{
    return m_settings.value("paths/data", QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).toString();
}
bool SettingsStore::compactSidebar() const { return m_settings.value("appearance/compactSidebar", false).toBool(); }
int SettingsStore::coverArtStyle() const { return m_settings.value("library/coverArtStyle", 1).toInt(); }
int SettingsStore::performanceProfile() const { return m_settings.value("emulation/performanceProfile", 0).toInt(); }
double SettingsStore::gridScale() const { return m_settings.value("appearance/gridScale", 1.0).toDouble(); }
double SettingsStore::fontScale() const { return m_settings.value("appearance/fontScale", 1.0).toDouble(); }
QString SettingsStore::backgroundPath() const { return m_settings.value("appearance/backgroundPath").toString(); }
int SettingsStore::backgroundDim() const { return m_settings.value("appearance/backgroundDim", 48).toInt(); }
double SettingsStore::cornerScale() const { return m_settings.value("appearance/cornerScale", 1.0).toDouble(); }
double SettingsStore::motionScale() const { return m_settings.value("appearance/motionScale", 1.0).toDouble(); }

template <typename T>
bool SettingsStore::update(const QString& key, const T& value, void (SettingsStore::*changedSignal)())
{
    if (m_settings.value(key) == QVariant::fromValue(value))
        return false;
    m_settings.setValue(key, QVariant::fromValue(value));
    m_settings.sync();
    emit (this->*changedSignal)();
    emit valueChanged(key, QVariant::fromValue(value));
    return true;
}

void SettingsStore::setThemeMode(const QString& value) { update("appearance/theme", value, &SettingsStore::themeModeChanged); }
void SettingsStore::setAccentColor(const QString& value) { update("appearance/accent", value, &SettingsStore::accentColorChanged); }
void SettingsStore::setLanguage(const QString& value) { update("general/language", value, &SettingsStore::languageChanged); }
void SettingsStore::setOnboardingCompleted(bool value) { update("setup/completed", value, &SettingsStore::onboardingCompletedChanged); }
void SettingsStore::setBiosPath(const QString& value) { update("paths/bios", normalizeLocalPath(value), &SettingsStore::biosPathChanged); }
void SettingsStore::setEmulatorDataPath(const QString& value) { update("paths/data", normalizeLocalPath(value), &SettingsStore::emulatorDataPathChanged); }
void SettingsStore::setCompactSidebar(bool value) { update("appearance/compactSidebar", value, &SettingsStore::compactSidebarChanged); }
void SettingsStore::setCoverArtStyle(int value) { update("library/coverArtStyle", qBound(0, value, 2), &SettingsStore::coverArtStyleChanged); }
void SettingsStore::setPerformanceProfile(int value) { update("emulation/performanceProfile", qBound(0, value, 1), &SettingsStore::performanceProfileChanged); }
void SettingsStore::setGridScale(double value) { update("appearance/gridScale", qBound(0.65, value, 1.55), &SettingsStore::gridScaleChanged); }
void SettingsStore::setFontScale(double value) { update("appearance/fontScale", qBound(0.85, value, 1.30), &SettingsStore::fontScaleChanged); }
void SettingsStore::setBackgroundPath(const QString& value) { update("appearance/backgroundPath", normalizeLocalPath(value), &SettingsStore::backgroundPathChanged); }
void SettingsStore::setBackgroundDim(int value) { update("appearance/backgroundDim", qBound(0, value, 85), &SettingsStore::backgroundDimChanged); }
void SettingsStore::setCornerScale(double value) { update("appearance/cornerScale", qBound(0.5, value, 1.5), &SettingsStore::cornerScaleChanged); }
void SettingsStore::setMotionScale(double value) { update("appearance/motionScale", qBound(0.5, value, 1.5), &SettingsStore::motionScaleChanged); }

QVariant SettingsStore::value(const QString& key, const QVariant& fallback) const
{
    return m_settings.value(key, fallback);
}

void SettingsStore::setValue(const QString& key, const QVariant& value)
{
    if (m_settings.value(key) == value)
        return;
    m_settings.setValue(key, value);
    m_settings.sync();
    emit valueChanged(key, value);
}

void SettingsStore::resetDesktopPreferences()
{
    // This action belongs to the Customization screen. Never erase the library,
    // favorites, BIOS, input bindings, profile, save slots, or emulator options.
    m_settings.remove(QStringLiteral("appearance"));
    m_settings.sync();
    emit themeModeChanged();
    emit accentColorChanged();
    emit languageChanged();
    emit compactSidebarChanged();
    emit coverArtStyleChanged();
    emit performanceProfileChanged();
    emit gridScaleChanged();
    emit fontScaleChanged();
    emit backgroundPathChanged();
    emit backgroundDimChanged();
    emit cornerScaleChanged();
    emit motionScaleChanged();
}
