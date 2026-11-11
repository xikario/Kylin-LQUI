#include "PanelAiStation.h"
#include "StationMetrics.h"
#include <QtTest>
#include <QApplication>
#include <QAbstractButton>
#include <QDBusConnection>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDragEnterEvent>
#include <QDropEvent>
#include "StationDrop.h"

class UKUIPanel:public QWidget { Q_OBJECT public:using QWidget::QWidget; };
class LunarCalendarWidget:public QWidget { Q_OBJECT public:using QWidget::QWidget; };
class FakeStation:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.LiquidCli")
public:int calls=0,preferences=0,drops=0,captures=0;QRect anchor,settingsAnchor,dropAnchor,captureAnchor;QJsonObject payload;bool capturePopup=false;
public slots:
    Q_SCRIPTABLE void ToggleAt(int x,int y,int w,int h){++calls;anchor=QRect(x,y,w,h);}
    Q_SCRIPTABLE void PreferencesAt(int x,int y,int w,int h){++preferences;settingsAnchor=QRect(x,y,w,h);}
    Q_SCRIPTABLE void CaptureAt(int x,int y,int w,int h){++captures;captureAnchor=QRect(x,y,w,h);capturePopup=QApplication::activePopupWidget()!=nullptr;}
    Q_SCRIPTABLE bool ImportDropAt(const QString &manifest,int x,int y,int w,int h){payload=StationDrop::read(manifest);dropAnchor=QRect(x,y,w,h);++drops;StationDrop::discard(manifest);return !payload.isEmpty();}
};
class StationTest:public QObject {
    Q_OBJECT
    static void write(const QString &path,const QByteArray &data){QDir().mkpath(QFileInfo(path).path());QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(data);}
private slots:
    void calendarIgnoresLateOemRepositionWithoutDpiOscillation(){
        const auto screen=QGuiApplication::primaryScreen()->availableGeometry();
        UKUIPanel panel;panel.setGeometry(screen.left()+100,screen.bottom()-60,650,48);panel.show();PanelAiStation bridge(&panel);
        LunarCalendarWidget calendar;calendar.resize(300,400);calendar.move(900,200);calendar.show();
        QTest::qWait(250);QPoint expected(panel.geometry().right()-calendar.width()+1,panel.y()-calendar.height()-8);
        QVERIFY((calendar.pos()-expected).manhattanLength()<=2);QVERIFY(calendar.windowOpacity()>.99);
        for(int n=0;n<4;++n){calendar.move(950,200+n*10);QVERIFY((calendar.pos()-expected).manhattanLength()<=2);QTest::qWait(30);QVERIFY((calendar.pos()-expected).manhattanLength()<=2);}
        calendar.move(expected+QPoint(1,0));const auto rounded=calendar.pos();QTest::qWait(100);QCOMPARE(calendar.pos(),rounded);
        calendar.resize(330,430);QTest::qWait(80);expected=QPoint(panel.geometry().right()-calendar.width()+1,panel.y()-calendar.height()-8);QVERIFY((calendar.pos()-expected).manhattanLength()<=2);
        calendar.hide();calendar.move(950,200);calendar.show();QTest::qWait(220);QVERIFY((calendar.pos()-expected).manhattanLength()<=2);QVERIFY(calendar.windowOpacity()>.99);
    }
    void networkAccounting(){
        QTemporaryDir root;StationMetrics metrics(root.path()+"/sys",root.path()+"/proc");
        for(const QString &name:QStringList{"eth0","tun0","lo"}) {
            write(root.path()+"/sys/class/net/"+name+"/operstate","up");
            if(name=="eth0")QDir().mkpath(root.path()+"/sys/class/net/"+name+"/device");
        }
        write(root.path()+"/proc/net/dev","eth0: 1000 0 0 0 0 0 0 0 2000 0 0 0 0 0 0 0\ntun0: 9000 0 0 0 0 0 0 0 9000 0 0 0 0 0 0 0\nlo: 99999 0 0 0 0 0 0 0 9999 0 0 0 0 0 0 0\n");
        const auto n=metrics.network();QCOMPARE(n.keys(),QStringList{"eth0"});
        QVERIFY(!metrics.updateNetwork(n,0).valid);
        auto rates=metrics.updateNetwork({{"eth0",{3000,6000}}},2000);QVERIFY(rates.valid);QCOMPARE(rates.down,1000.);QCOMPARE(rates.up,2000.);
        QVERIFY(!metrics.updateNetwork({{"eth0",{10,20}}},4000).valid); // reset is not a huge spike
        QVERIFY(!metrics.updateNetwork({{"eth1",{9999,9999}}},6000).valid); // hotplug establishes its own baseline
        QVERIFY(!metrics.updateNetwork({},8000).valid);
        QCOMPARE(StationMetrics::rate(0),QString("0 B/s"));QCOMPARE(StationMetrics::rate(2048),QString("2.0 KB/s"));
    }
    void sensorIdentityAndPeak(){
        QTemporaryDir root;StationMetrics metrics(root.path(),"/nonexistent");
        write(root.path()+"/class/hwmon/hwmon0/name","acpitz");write(root.path()+"/class/hwmon/hwmon0/temp1_input","55000");
        write(root.path()+"/class/hwmon/hwmon1/name","nvme");write(root.path()+"/class/hwmon/hwmon1/temp1_input","95000");
        auto t=metrics.temperature(0);QCOMPARE(t.current,55.);QCOMPARE(t.label,QString("机温"));
        write(root.path()+"/class/hwmon/hwmon0/temp1_input","45000");t=metrics.temperature(10000);QCOMPARE(t.current,45.);QCOMPARE(t.peak,55.);
        t=metrics.temperature(310001);QCOMPARE(t.peak,45.);
        write(root.path()+"/class/hwmon/hwmon2/name","coretemp");write(root.path()+"/class/hwmon/hwmon2/temp1_input","65000");
        t=metrics.temperature(320000);QCOMPARE(t.current,65.);QCOMPARE(t.label,QString("均温"));
        write(root.path()+"/class/hwmon/hwmon2/temp1_input","200000");
        t=metrics.temperature(330000);QCOMPARE(t.current,45.);QCOMPARE(t.label,QString("机温"));QCOMPARE(t.peak,45.);
        QFile::remove(root.path()+"/class/hwmon/hwmon0/temp1_input");QVERIFY(metrics.temperature(340000).current<0);
    }
    void nativeLayoutClickAndIdle(){
        FakeStation fake;auto bus=QDBusConnection::sessionBus();
        QVERIFY(bus.registerObject("/Cli",&fake,QDBusConnection::ExportScriptableSlots));QVERIFY(bus.registerService("org.ukui.LiquidCli"));
        UKUIPanel panel;panel.resize(660,48);panel.move(20,100);panel.setStyleSheet("UKUIPanel{background:#132b2b;color:white;}");
        auto *row=new QHBoxLayout(&panel);row->setContentsMargins(0,0,0,0);auto *start=new QPushButton(&panel);start->setObjectName("StartMenuButton");start->setFixedSize(44,44);row->addWidget(start);row->addStretch();
        auto *dateWrapper=new QWidget(&panel);dateWrapper->setObjectName("ukui-panel_plugin_QFrame_calendar");
        auto *dates=new QHBoxLayout(dateWrapper);dates->setContentsMargins(0,0,0,0);
        auto *calendar=new QLabel("10:30 星期四\n2026/10/29",dateWrapper);calendar->setObjectName("Calendar");calendar->setFont(QFont("Noto Sans",10));dates->addWidget(calendar);row->addWidget(dateWrapper);
        auto *wrapper=new QWidget(&panel);wrapper->setObjectName("ukui-panel_plugin_QFrame_showdesktop");
        auto *inner=new QHBoxLayout(wrapper);inner->setContentsMargins(0,0,0,0);inner->setSpacing(0);
        auto *desktop=new QWidget(wrapper);desktop->setObjectName("ShowDesktop");desktop->setFixedWidth(12);inner->addWidget(desktop);row->addWidget(wrapper);
        PanelAiStation adapter(&panel);panel.show();
        QTRY_VERIFY(dateWrapper->findChild<QWidget*>("aiStationCluster"));
        auto *cluster=dateWrapper->findChild<QWidget*>("aiStationCluster");auto *orb=dateWrapper->findChild<QAbstractButton*>("aiStationOrb");QVERIFY(orb);
        QTRY_VERIFY(cluster->width()>150);
        QVERIFY(calendar->mapToGlobal(QPoint()).x()+calendar->width()<=orb->mapToGlobal(QPoint()).x());
        QCOMPARE(orb->size(),start->size());QVERIFY(!wrapper->isVisible());
        QVERIFY(start->findChild<QObject*>("lquiStartOrbFace"));QCOMPARE(start->cursor().shape(),Qt::PointingHandCursor);
        QCOMPARE(orb->cursor().shape(),Qt::PointingHandCursor);
        const auto startIdle=start->grab().toImage();QTest::mouseMove(start,start->rect().center());QTest::qWait(500);
        const auto startHover=start->grab().toImage();
        for(const auto point:{QPoint(2,2),QPoint(startIdle.width()-3,2),QPoint(2,startIdle.height()-3)})
            QCOMPARE(startIdle.pixelColor(point),startHover.pixelColor(point)); // no square hover background

        QWidget *corner=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="aiStationDesktopCorner")corner=w;QVERIFY(corner);
        QCOMPARE(corner->geometry().bottomRight(),QGuiApplication::primaryScreen()->geometry().bottomRight());
        auto *readout=cluster->findChild<QWidget*>("aiStationReadout");QVERIFY(readout);QCOMPARE(readout->property("stationStyle").toString(),QString("minimal"));
        QCOMPARE(calendar->height(),36);QVERIFY(readout->geometry().right()<calendar->geometry().left());QVERIFY(readout->width()<170);
        QEvent enter(QEvent::Enter);QApplication::sendEvent(orb,&enter);QTest::qWait(180);QCOMPARE(fake.calls,0);
        auto *accent=orb->findChild<QWidget*>("aiStationOrbAccent");QVERIFY(accent);QVERIFY(accent->property("animating").toBool());
        QTest::qWait(300);QVERIFY(!accent->property("animating").toBool());QVERIFY(!accent->isVisible());
        auto *leftAccent=start->findChild<QWidget*>("aiStationOrbAccent");QVERIFY(leftAccent);QApplication::sendEvent(start,&enter);QVERIFY(leftAccent->property("animating").toBool());
        QTest::qWait(500);QVERIFY(!leftAccent->property("animating").toBool());
        QTest::mouseClick(orb,Qt::LeftButton);QTRY_COMPARE(fake.calls,1);QCOMPARE(fake.anchor,QRect(orb->mapToGlobal(QPoint()),orb->size()));
        const int width=panel.width();QTest::qWait(2200);QCOMPARE(panel.width(),width);QCOMPARE(dateWrapper->findChildren<QWidget*>("aiStationCluster").size(),1);
        QContextMenuEvent context(QContextMenuEvent::Mouse,orb->rect().center(),orb->mapToGlobal(orb->rect().center()));QApplication::sendEvent(orb,&context);
        auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QVERIFY(menu);for(auto *action:menu->actions())if(action->text().contains("设置"))action->trigger();menu->close();
        QTRY_COMPARE(fake.preferences,1);QCOMPARE(fake.settingsAnchor,QRect(orb->mapToGlobal(QPoint()),orb->size()));
        auto chooseCapture=[&](const QString &name){
            QContextMenuEvent e(QContextMenuEvent::Mouse,orb->rect().center(),orb->mapToGlobal(orb->rect().center()));
            QApplication::sendEvent(orb,&e);
            auto *popup=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!popup)return false;
            auto *action=popup->findChild<QAction*>(name);
            if(!action){popup->close();return false;}
            action->trigger();
            return !QApplication::activePopupWidget();
        };
        QVERIFY(chooseCapture("aiStationCaptureNow"));
        QTRY_COMPARE(fake.captures,1);
        QCOMPARE(fake.captureAnchor,QRect(orb->mapToGlobal(QPoint()),orb->size()));
        QVERIFY(!fake.capturePopup);
        QElapsedTimer countdown;countdown.start();
        QVERIFY(chooseCapture("aiStationCaptureDelayed"));
        QTest::qWait(400);QCOMPARE(fake.captures,1);
        QTRY_COMPARE(fake.captures,2);
        QVERIFY(countdown.elapsed()>=3000);
        QVERIFY(!fake.capturePopup);
        QMimeData mime;mime.setText("拖入的说明");QDragEnterEvent drag(orb->rect().center(),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(orb,&drag);QVERIFY(drag.isAccepted());
        QDropEvent drop(orb->rect().center(),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(orb,&drop);QVERIFY(drop.isAccepted());
        QTRY_COMPARE(fake.drops,1);QCOMPARE(fake.payload.value("text").toString(),QString("拖入的说明"));QCOMPARE(fake.dropAnchor,QRect(orb->mapToGlobal(QPoint()),orb->size()));
        QDragEnterEvent moveOnly(orb->rect().center(),Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(orb,&moveOnly);QVERIFY(!moveOnly.isAccepted());
        auto count=[&]{return QJsonDocument::fromJson(adapter.State().toUtf8()).object()["clusters"].toArray()[0].toObject()["samples"].toInt();};
        panel.hide();const int samples=count();QTest::qWait(2200);QCOMPARE(count(),samples);
        panel.show();QTRY_VERIFY(count()>samples);
        const QString artifacts=qEnvironmentVariable("AI_STATION_ARTIFACT_DIR");if(!artifacts.isEmpty()){QDir().mkpath(artifacts);panel.grab().save(artifacts+"/station-panel.png");}
        panel.resize(48,660);QTRY_VERIFY(!dateWrapper->findChild<QWidget*>("aiStationCluster"));QVERIFY(calendar->isVisible());
        panel.resize(660,48);QTRY_VERIFY(dateWrapper->findChild<QWidget*>("aiStationCluster"));QVERIFY(calendar->isVisible());
        bus.unregisterService("org.ukui.LiquidCli");bus.unregisterObject("/Cli");
    }
};
QTEST_MAIN(StationTest)
#include "ai_station_test.moc"
