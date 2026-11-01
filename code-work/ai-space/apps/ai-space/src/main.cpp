#include "AiSpaceController.h"
#include "AppConfig.h"
#include "SettingsBridge.h"
#include "BackgroundTask.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QCommandLineParser>
#include <QIcon>
#include <QSettings>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    BackgroundTask::ApplicationScope workers;
    app.setApplicationName("ai-space");
    app.setApplicationDisplayName(QStringLiteral("AI空间"));
    app.setOrganizationName("xikario");
    app.setApplicationVersion(QStringLiteral(AI_SPACE_VERSION));
    app.setDesktopFileName("ai-space");
    // Share one explicit format with Python readers, independent of Qt/OEM defaults.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    app.setWindowIcon(QIcon(":/app/icon.png"));
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AI空间 · 本地文件、OCR与知识库检索"));
    parser.addHelpOption();
    parser.addVersionOption();
    for (const auto &name : {"show", "hide", "toggle", "settings", "quit", "status", "autostart", "settings-service"})
        parser.addOption(QCommandLineOption(QStringList{name}, name));
    parser.process(app);
    if(parser.isSet("settings")&&LquiSettings::open("space:general"))return 0;
    if(parser.isSet("settings-service"))qputenv("LQUI_SETTINGS_SERVICE","1");
    const QString method = parser.isSet("quit")       ? "quit"
                           : parser.isSet("status")   ? "status"
                           : parser.isSet("settings") ? "showSettings"
                           : parser.isSet("hide")     ? "hideWindow"
                           : parser.isSet("toggle")   ? "toggleWindow"
                                                      : "showWindow";
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService("io.github.xikario.AiSpace")) {
        // A duplicate session-autostart must never surface an existing window.
        if (parser.isSet("autostart") || parser.isSet("settings-service"))
            return 0;
        QDBusInterface existing("io.github.xikario.AiSpace", "/aiSpace",
                                "io.github.xikario.AiSpace", bus);
        existing.setTimeout(3000);
        const auto reply = existing.call(method);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            std::fprintf(stderr, "%s\n", qPrintable(reply.errorMessage()));
            return 1;
        }
        if (method == "status" && !reply.arguments().isEmpty())
            std::printf("%s\n", qPrintable(reply.arguments().first().toString()));
        return 0;
    }
    if (parser.isSet("quit") || parser.isSet("status"))
        return 0;
    if (!parser.isSet("autostart") && !parser.isSet("settings-service"))
        qputenv("AI_SPACE_EXPLICIT_SHOW", "1");
    AiSpaceController controller;
    LquiSettings::Bridge settingsBridge("space",[&](const QString &){return controller.settingsBridgeRoot();},[]{return QJsonArray{QJsonObject{{"id","general"},{"title","AI 空间"}}};});
    settingsBridge.registerOnSession();
    QDBusServiceWatcher settingsWatcher("org.kylin.Lqui.Settings",bus,QDBusServiceWatcher::WatchForUnregistration,&app);
    QObject::connect(&settingsWatcher,&QDBusServiceWatcher::serviceUnregistered,&controller,[&]{controller.quitSettingsService();});
    if (!bus.registerObject("/aiSpace", &controller,
                            QDBusConnection::ExportScriptableSlots |
                                QDBusConnection::ExportAllSignals))
        return 1;
    controller.initialize(!parser.isSet("hide") && !parser.isSet("autostart") && !parser.isSet("settings-service"),
                          parser.isSet("settings"));
    return app.exec();
}
