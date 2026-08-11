#pragma once

#include <QObject>
#include <QStringList>

class SettingsStore;

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentRoute READ currentRoute NOTIFY currentRouteChanged)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(QString hostArchitecture READ hostArchitecture CONSTANT)
    Q_PROPERTY(QString buildDescription READ buildDescription CONSTANT)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentRouteChanged)

public:
    explicit AppController(SettingsStore* settings, QObject* parent = nullptr);

    QString currentRoute() const { return m_routeStack.constLast(); }
    QString platformName() const;
    QString hostArchitecture() const;
    QString buildDescription() const;
    bool canGoBack() const { return m_routeStack.size() > 1; }

    Q_INVOKABLE void navigate(const QString& route);
    Q_INVOKABLE void replaceRoute(const QString& route);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void finishOnboarding();
    Q_INVOKABLE void openExternalUrl(const QString& url) const;

signals:
    void currentRouteChanged();
    void onboardingFinished();

private:
    static bool isKnownRoute(const QString& route);

    SettingsStore* m_settings;
    QStringList m_routeStack {"library"};
};

