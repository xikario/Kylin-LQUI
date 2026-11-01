#include "Drawer.h"
#include "SettingsBridge.h"
#include <QJsonObject>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QCommandLineParser>
#include <QEventLoop>
#include <QTimer>
#include "StationDrop.h"
#include <cstdio>
#include <malloc.h>

int main(int argc,char **argv) {
    // glibc raises its mmap threshold after a large block is freed, then keeps
    // later full-screen images (screenshots, glass tiles) in the heap for good.
    // A fixed threshold returns them to the system: about 66 -> 15 MiB measured.
    mallopt(M_MMAP_THRESHOLD,128*1024);mallopt(M_TRIM_THRESHOLD,128*1024);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);app.setApplicationName("liquid-cli");app.setApplicationDisplayName("AI小站");app.setApplicationVersion("0.2.5");app.setDesktopFileName("liquid-cli");
    QCommandLineParser parser;parser.setApplicationDescription("AI小站 · Codex / 千问 / DeepSeek");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"resident","登录时保持界面常驻，不展开、不连接模型"});
    parser.addOption({"show","展开并开始输入"});parser.addOption({"hide","收起"});parser.addOption({"toggle","展开或收起（适合绑定快捷键）"});
    parser.addOption({"settings","打开设置"});parser.addOption({"reload","重新读取设置"});parser.addOption({"quit","退出当前实例"});parser.addOption({"state","读取当前实例状态"});
    parser.addOption({"anchor","任务栏入口的逻辑坐标 x,y,w,h","rect"});
    parser.addOption({"drop","接收拖放内容","manifest"});
    parser.process(app);
    const auto values=parser.value("anchor").split(',');QList<int> anchor;
    if(parser.isSet("anchor")) {
        if(values.size()!=4)return 2;
        for(const auto &v:values){bool ok=false;const int n=v.toInt(&ok);if(!ok)return 2;anchor.append(n);}
    }
    const QString service="org.ukui.LiquidCli";auto bus=QDBusConnection::sessionBus();
    // Read-only/control invocations must never temporarily own the service name:
    // --state racing a cold launch used to make the real UI instance exit.
    if(parser.isSet("state")||parser.isSet("quit")||parser.isSet("hide")||parser.isSet("reload")){
        QDBusInterface existing(service,"/Cli","org.ukui.LiquidCli",bus);
        if(!existing.isValid())return parser.isSet("state")?1:0;
        const auto reply=existing.call(parser.isSet("state")?"State":parser.isSet("quit")?"Quit":parser.isSet("reload")?"Reload":"Hide");
        if(reply.type()==QDBusMessage::ErrorMessage)return 1;
        if(parser.isSet("state")&&!reply.arguments().isEmpty())std::puts(reply.arguments().first().toString().toUtf8().constData());
        return 0;
    }
    if(!bus.registerService(service)) {
        if(parser.isSet("resident"))return 0;
        if(parser.isSet("drop")) {
            const auto a=anchor.size()==4?anchor:QList<int>{0,0,0,0};
            for(int attempt=0;attempt<30;++attempt) {
                QDBusInterface existing(service,"/Cli","org.ukui.LiquidCli",bus);
                if(existing.isValid()) {
                    const auto reply=existing.call("ImportDropAt",parser.value("drop"),a[0],a[1],a[2],a[3]);
                    if(reply.type()!=QDBusMessage::ErrorMessage)return !StationDrop::owns(parser.value("drop"))?0:1;
                    if(reply.errorName()!="org.freedesktop.DBus.Error.UnknownObject")return 1;
                }
                QEventLoop settle;QTimer::singleShot(100,&settle,&QEventLoop::quit);settle.exec();
            }
            return 1;
        }
        QDBusInterface existing(service,"/Cli","org.ukui.LiquidCli",bus);
        if(anchor.size()==4)existing.call(parser.isSet("settings")?"PreferencesAt":"ToggleAt",anchor[0],anchor[1],anchor[2],anchor[3]);
        else existing.call(parser.isSet("settings")?"Preferences":parser.isSet("toggle")?"Toggle":"Show");
        return existing.isValid()?0:1;
    }
    Drawer drawer(Settings::load());
    auto *settingsBridge=new LquiSettings::Bridge("ai",[&drawer](const QString &){return drawer.settingsBridgeRoot();},[]{return QJsonArray{QJsonObject{{"id","general"},{"title","AI 小站"},{"group","AI"}}};},&drawer);settingsBridge->registerOnSession();
    bus.registerObject("/Cli",&drawer,QDBusConnection::ExportScriptableSlots);
    if(parser.isSet("drop")){const auto a=anchor.size()==4?anchor:QList<int>{0,0,0,0};if(!drawer.ImportDropAt(parser.value("drop"),a[0],a[1],a[2],a[3]))return 1;}
    else if(anchor.size()==4){if(parser.isSet("settings"))drawer.PreferencesAt(anchor[0],anchor[1],anchor[2],anchor[3]);else drawer.ToggleAt(anchor[0],anchor[1],anchor[2],anchor[3]);}
    else if(parser.isSet("settings"))drawer.Preferences();else if(parser.isSet("show")||parser.isSet("toggle"))drawer.Show();
    if(parser.isSet("resident"))QTimer::singleShot(900,&drawer,&Drawer::prepareResident);
    return app.exec();
}
