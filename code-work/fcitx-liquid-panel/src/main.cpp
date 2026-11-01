#include "PanelAgent.h"
#include "LiquidPopup.h"
#include <QDBusInterface>
#include "SettingsDialog.h"
#include "SettingsBridge.h"
#include <QJsonObject>
#include <QApplication>
#include <QCommandLineParser>
#include <QPainter>
#include <QDir>
#include <QDebug>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#include <QSocketNotifier>

static int signalFds[2];
static void signalHandler(int) { const char byte=1; const auto ignored=::write(signalFds[0],&byte,1); (void)ignored; }
int main(int argc,char **argv) {
    // One physical coordinate space for Fcitx/X11, including 150% desktops.
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR","0"); qputenv("QT_SCALE_FACTOR","1");
    qunsetenv("QT_SCREEN_SCALE_FACTORS");
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
    Q_INIT_RESOURCE(tray_icons);
    app.setApplicationDisplayName(QStringLiteral("RIME液态输入法"));
    app.setWindowIcon(QIcon(":/fcitx-liquid/tray/settings.png"));
    LiquidPopup::install(app);
    app.setApplicationName("fcitx-liquid-panel"); app.setApplicationVersion("0.2.0");
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"render-check","Render synthetic material without opening a window or sampling the desktop.","output"});
    parser.addOption({"contrast-check","Validate light/dark GPU material using synthetic pixels only.","output"});
    parser.addOption({"settings","Open graphical appearance settings."});
    parser.process(app);
    if(parser.isSet("contrast-check")) {
        LiquidOpticsRenderer optics;QImage input(640,100,QImage::Format_RGB32);input.fill(Qt::white);
        optics.setWallpaper(input);const QImage before=optics.renderPanel(input.rect(),16);
        optics.setLightSurface(1);const QImage after=optics.renderPanel(input.rect(),16);
        const bool gpu=optics.usedGpu();
        optics.setLightSurface(0);const bool reversible=optics.renderPanel(input.rect(),16)==before;
        input.fill(QColor(35,35,35));optics.setWallpaper(input);
        const QImage dark=optics.renderPanel(input.rect(),16);
        QImage sheet(680,400,QImage::Format_RGB32);sheet.fill(QColor(230,230,230));QPainter p(&sheet);
        p.drawImage(20,15,before);p.drawImage(20,140,after);p.drawImage(20,265,dark);p.end();
        const bool saved=sheet.save(parser.value("contrast-check"));
        const int oldValue=before.pixelColor(320,50).lightness(),newValue=after.pixelColor(320,50).lightness();
        qInfo()<<"Contrast GPU check"<<gpu<<"body old/new"<<oldValue<<newValue<<"zero-mode unchanged"<<reversible;
        return gpu && reversible && saved && newValue>200 && newValue>oldValue+30 && after.pixelColor(0,0).alpha()==0?0:1;
    }
    if(parser.isSet("settings")) {
        if(LquiSettings::open("input:general"))return 0;
        QDBusInterface remote("org.kde.impanel","/org/ukui/FcitxLiquid","org.ukui.FcitxLiquid");
        if(remote.isValid()) {auto reply=remote.call("ShowSettings");return reply.type()==QDBusMessage::ErrorMessage?1:0;}
        CandidateWindow previewHost; SettingsDialog dialog(previewHost);dialog.exec();return 0;
    }
    if (parser.isSet("render-check")) {
        QImage input(680,120,QImage::Format_RGB32);
        QPainter p(&input);
        p.fillRect(input.rect(),QColor(60,80,100));
        for (int x=0;x<input.width();x+=24) p.fillRect(x,0,12,input.height(),QColor(150,180,210));
        p.end();
        LiquidOpticsRenderer optics; optics.setWallpaper(input);
        QImage result=optics.renderPanel(input.rect(),24);
        bool saved=result.save(parser.value("render-check"));
        qInfo()<<"synthetic render: GPU="<<optics.usedGpu()<<"saved="<<saved;
        return saved && optics.usedGpu()?0:2;
    }
    if (QGuiApplication::platformName()!="xcb") { qCritical()<<"This adapter requires the X11 desktop."; return 1; }
    CandidateWindow window;
    window.warmUp();
    PanelAgent agent(window);
    PanelControl control(window,agent);
    auto *settingsBridge=new LquiSettings::Bridge("input",[&control](const QString &){return control.settingsBridgeRoot();},[]{return QJsonArray{QJsonObject{{"id","general"},{"title","液态输入法"},{"group","输入法"}}};},&control);settingsBridge->registerOnSession();
    QDBusConnection::sessionBus().registerObject("/org/ukui/FcitxLiquid",&control,QDBusConnection::ExportScriptableSlots);
    if (!agent.start()) { qCritical()<<"Cannot claim org.kde.impanel; another panel may already own it."; return 1; }
    if (::socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK,0,signalFds)==0) {
        auto *notifier=new QSocketNotifier(signalFds[1],QSocketNotifier::Read,&app);
        QObject::connect(notifier,&QSocketNotifier::activated,&app,[&app](int){app.quit();});
        std::signal(SIGTERM,signalHandler); std::signal(SIGINT,signalHandler);
    }
    qInfo()<<"[liquid-candidate] ready; no input text is logged; exit restores Fcitx fallback UI";
    return app.exec();
}
