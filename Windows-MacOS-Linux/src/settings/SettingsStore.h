#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

class SettingsStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool onboardingCompleted READ onboardingCompleted WRITE setOnboardingCompleted NOTIFY onboardingCompletedChanged)
    Q_PROPERTY(QString biosPath READ biosPath WRITE setBiosPath NOTIFY biosPathChanged)
    Q_PROPERTY(QString emulatorDataPath READ emulatorDataPath WRITE setEmulatorDataPath NOTIFY emulatorDataPathChanged)
    Q_PROPERTY(bool compactSidebar READ compactSidebar WRITE setCompactSidebar NOTIFY compactSidebarChanged)
    Q_PROPERTY(int coverArtStyle READ coverArtStyle WRITE setCoverArtStyle NOTIFY coverArtStyleChanged)
    Q_PROPERTY(int performanceProfile READ performanceProfile WRITE setPerformanceProfile NOTIFY performanceProfileChanged)

public:
    explicit SettingsStore(QObject* parent = nullptr);

    QString themeMode() const;
    QString accentColor() const;
    QString language() const;
    bool onboardingCompleted() const;
    QString biosPath() const;
    QString emulatorDataPath() const;
    bool compactSidebar() const;
    int coverArtStyle() const;
    int performanceProfile() const;

    void setThemeMode(const QString& value);
    void setAccentColor(const QString& value);
    void setLanguage(const QString& value);
    void setOnboardingCompleted(bool value);
    void setBiosPath(const QString& value);
    void setEmulatorDataPath(const QString& value);
    void setCompactSidebar(bool value);
    void setCoverArtStyle(int value);
    void setPerformanceProfile(int value);

    Q_INVOKABLE QVariant value(const QString& key, const QVariant& fallback = {}) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void resetDesktopPreferences();

signals:
    void themeModeChanged();
    void accentColorChanged();
    void languageChanged();
    void onboardingCompletedChanged();
    void biosPathChanged();
    void emulatorDataPathChanged();
    void compactSidebarChanged();
    void coverArtStyleChanged();
    void performanceProfileChanged();
    void valueChanged(const QString& key, const QVariant& value);

private:
    template <typename T>
    bool update(const QString& key, const T& value, void (SettingsStore::*signal)());

    QSettings m_settings;
};
