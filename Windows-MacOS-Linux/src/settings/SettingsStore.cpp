#include "SettingsStore.h"

#include <QCoreApplication>
#include <QLocale>
#include <QStandardPaths>

SettingsStore::SettingsStore(QObject* parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
          QCoreApplication::organizationName(), QCoreApplication::applicationName())
{
    const QString legacyAccent = m_settings.value("appearance/accent").toString().toUpper();
    if (legacyAccent == QLatin1String("#8B5CF6") || legacyAccent == QLatin1String("#7C3AED"))
        m_settings.setValue("appearance/accent", QStringLiteral("#C4203A"));
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

template <typename T>
bool SettingsStore::update(const QString& key, const T& value, void (SettingsStore::*changedSignal)())
{
    if (m_settings.value(key) == QVariant::fromValue(value))
        return false;
    m_settings.setValue(key, QVariant::fromValue(value));
    emit (this->*changedSignal)();
    emit valueChanged(key, QVariant::fromValue(value));
    return true;
}

void SettingsStore::setThemeMode(const QString& value) { update("appearance/theme", value, &SettingsStore::themeModeChanged); }
void SettingsStore::setAccentColor(const QString& value) { update("appearance/accent", value, &SettingsStore::accentColorChanged); }
void SettingsStore::setLanguage(const QString& value) { update("general/language", value, &SettingsStore::languageChanged); }
void SettingsStore::setOnboardingCompleted(bool value) { update("setup/completed", value, &SettingsStore::onboardingCompletedChanged); }
void SettingsStore::setBiosPath(const QString& value) { update("paths/bios", value, &SettingsStore::biosPathChanged); }
void SettingsStore::setEmulatorDataPath(const QString& value) { update("paths/data", value, &SettingsStore::emulatorDataPathChanged); }
void SettingsStore::setCompactSidebar(bool value) { update("appearance/compactSidebar", value, &SettingsStore::compactSidebarChanged); }
void SettingsStore::setCoverArtStyle(int value) { update("library/coverArtStyle", qBound(0, value, 2), &SettingsStore::coverArtStyleChanged); }
void SettingsStore::setPerformanceProfile(int value) { update("emulation/performanceProfile", qBound(0, value, 1), &SettingsStore::performanceProfileChanged); }
void SettingsStore::setGridScale(double value) { update("appearance/gridScale", qBound(0.65, value, 1.55), &SettingsStore::gridScaleChanged); }
void SettingsStore::setFontScale(double value) { update("appearance/fontScale", qBound(0.85, value, 1.30), &SettingsStore::fontScaleChanged); }
void SettingsStore::setBackgroundPath(const QString& value) { update("appearance/backgroundPath", value, &SettingsStore::backgroundPathChanged); }
void SettingsStore::setBackgroundDim(int value) { update("appearance/backgroundDim", qBound(0, value, 85), &SettingsStore::backgroundDimChanged); }

QVariant SettingsStore::value(const QString& key, const QVariant& fallback) const
{
    return m_settings.value(key, fallback);
}

void SettingsStore::setValue(const QString& key, const QVariant& value)
{
    if (m_settings.value(key) == value)
        return;
    m_settings.setValue(key, value);
    emit valueChanged(key, value);
}

void SettingsStore::resetDesktopPreferences()
{
    const QString bios = biosPath();
    const QString data = emulatorDataPath();
    const bool completed = onboardingCompleted();
    m_settings.clear();
    m_settings.setValue("paths/bios", bios);
    m_settings.setValue("paths/data", data);
    m_settings.setValue("setup/completed", completed);
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
}
