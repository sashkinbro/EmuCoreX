#include "app/AppController.h"
#include "app/SingleInstanceGuard.h"
#include "core/EmulatorController.h"
#include "core/CoreRuntime.h"
#include "data/DesktopDataService.h"
#include "i18n/TranslationManager.h"
#include "input/InputBindingService.h"
#include "library/GameLibraryModel.h"
#include "library/GameCatalogModel.h"
#include "library/CoverArtService.h"
#include "settings/SettingsStore.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName("SBRO");
    QCoreApplication::setOrganizationDomain("emucorex.com");
    QCoreApplication::setApplicationName("EmuCoreX");
    QCoreApplication::setApplicationVersion(EMUCOREX_VERSION);
    QQuickStyle::setStyle("Basic");

    QGuiApplication app(argc, argv);
    QPixmap iconPixmap(256, 256);
    iconPixmap.fill(Qt::transparent);
    {
        QPainter painter(&iconPixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(iconPixmap.width() / 108.0, iconPixmap.height() / 108.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#0D0D0F")));
        painter.drawRoundedRect(QRectF(0, 0, 108, 108), 24, 24);
        const auto polygon = [&painter](const QColor& color, std::initializer_list<QPointF> points) {
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF(points));
        };
        polygon(QColor(QStringLiteral("#4D9EFF")), {{30, 28}, {50, 48}, {42, 48}, {22, 28}});
        polygon(QColor(QStringLiteral("#4D9EFF")), {{78, 28}, {58, 48}, {66, 48}, {86, 28}});
        polygon(QColor(QStringLiteral("#3A7FD5")), {{30, 80}, {50, 60}, {42, 60}, {22, 80}});
        polygon(QColor(QStringLiteral("#3A7FD5")), {{78, 80}, {58, 60}, {66, 60}, {86, 80}});
        polygon(QColor(QStringLiteral("#70B4FF")), {{49, 50}, {54, 44}, {59, 50}, {54, 56}});
    }
    app.setWindowIcon(QIcon(iconPixmap));
    SingleInstanceGuard singleInstance("com.sbro.emucorex.desktop");
    if (!singleInstance.isPrimary())
        return 0;

    const QFont systemFont = app.font();
    const int rubikFontId = QFontDatabase::addApplicationFont(":/fonts/Rubik.ttf");
    const int exoFontId = QFontDatabase::addApplicationFont(":/fonts/Exo2.ttf");
    const QString rubikFamily = QFontDatabase::applicationFontFamilies(rubikFontId).value(0, QStringLiteral("Rubik"));
    const QString exoFamily = QFontDatabase::applicationFontFamilies(exoFontId).value(0, QStringLiteral("Exo 2"));

    SettingsStore settings;
    const auto applyApplicationFont = [&] {
        const QString selected = settings.value(QStringLiteral("appearance/font"), QStringLiteral("rubik")).toString();
        QFont font = systemFont;
        if (selected == QLatin1String("exo2"))
            font.setFamily(exoFamily);
        else if (selected != QLatin1String("system"))
            font.setFamily(rubikFamily);
        app.setFont(font);
    };
    applyApplicationFont();
    QObject::connect(&settings, &SettingsStore::valueChanged, &app,
        [&](const QString& key, const QVariant&) {
            if (key == QLatin1String("appearance/font"))
                applyApplicationFont();
        });
    TranslationManager translations;
    translations.setCurrentLanguage(settings.language());
    QObject::connect(&settings, &SettingsStore::languageChanged, &translations, [&] {
        translations.setCurrentLanguage(settings.language());
    });

    AppController appController(&settings);
    GameCatalogModel catalog;
    CoreRuntime coreRuntime;
    GameLibraryModel library(&catalog, &coreRuntime);
    CoverArtService coverArt(&settings);
    EmulatorController emulator;
    DesktopDataService desktopData(&settings);
    InputBindingService inputBindings;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("App", &appController);
    engine.rootContext()->setContextProperty("Preferences", &settings);
    engine.rootContext()->setContextProperty("I18n", &translations);
    engine.rootContext()->setContextProperty("GameLibrary", &library);
    engine.rootContext()->setContextProperty("GameCatalog", &catalog);
    engine.rootContext()->setContextProperty("CoverArtProvider", &coverArt);
    engine.rootContext()->setContextProperty("Emulator", &emulator);
    engine.rootContext()->setContextProperty("DesktopData", &desktopData);
    engine.rootContext()->setContextProperty("InputBindings", &inputBindings);

    const QUrl mainUrl("qrc:/qml/Main.qml");
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [mainUrl](QObject* object, const QUrl& url) {
            if (!object && url == mainUrl)
                QCoreApplication::exit(EXIT_FAILURE);
        }, Qt::QueuedConnection);
    engine.load(mainUrl);

    if (!engine.rootObjects().isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        QObject::connect(&singleInstance, &SingleInstanceGuard::activationRequested, window, [window] {
            if (!window)
                return;
            window->show();
            window->raise();
            window->requestActivate();
        });
    }

    return app.exec();
}
