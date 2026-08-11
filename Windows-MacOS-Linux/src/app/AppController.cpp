#include "AppController.h"

#include "../settings/SettingsStore.h"

#include <QDesktopServices>
#include <QSysInfo>
#include <QUrl>

AppController::AppController(SettingsStore* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QString AppController::platformName() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("Linux");
#else
    return QSysInfo::productType();
#endif
}

QString AppController::hostArchitecture() const
{
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    return (arch == "aarch64" || arch == "arm64") ? QStringLiteral("ARM64") : QStringLiteral("x64");
}

QString AppController::buildDescription() const
{
    return QStringLiteral("%1 · %2 · %3").arg(platformName(), hostArchitecture(), QStringLiteral(EMUCOREX_VERSION));
}

bool AppController::isKnownRoute(const QString& route)
{
    static const QStringList routes = {
        "library", "catalog", "hub", "achievements", "profile", "game-manager",
        "launch-game", "launch-bios", "save-manager", "memory-cards", "textures",
        "cheats", "settings", "formats", "feedback", "emulation", "game-detail",
        "gamepad-mapping"
    };
    return routes.contains(route);
}

void AppController::navigate(const QString& route)
{
    if (!isKnownRoute(route) || currentRoute() == route)
        return;
    m_routeStack.append(route);
    emit currentRouteChanged();
}

void AppController::replaceRoute(const QString& route)
{
    if (!isKnownRoute(route) || currentRoute() == route)
        return;
    m_routeStack.last() = route;
    emit currentRouteChanged();
}

void AppController::goBack()
{
    if (!canGoBack())
        return;
    m_routeStack.removeLast();
    emit currentRouteChanged();
}

void AppController::finishOnboarding()
{
    m_settings->setOnboardingCompleted(true);
    emit onboardingFinished();
}

void AppController::openGameDetails(qint64 catalogGameId)
{
    if (catalogGameId <= 0)
        return;
    if (m_selectedCatalogGameId != catalogGameId) {
        m_selectedCatalogGameId = catalogGameId;
        emit selectedGameChanged();
    }
    navigate(QStringLiteral("game-detail"));
}

void AppController::openExternalUrl(const QString& url) const
{
    QDesktopServices::openUrl(QUrl(url));
}
