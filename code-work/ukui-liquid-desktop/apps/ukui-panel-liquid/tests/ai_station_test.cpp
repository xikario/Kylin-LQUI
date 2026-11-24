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
#include <QElapsedTimer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include "StationDrop.h"
#include "PetEntry.h"

class UKUIPanel:public QWidget { Q_OBJECT public:using QWidget::QWidget; };
class LunarCalendarWidget:public QWidget { Q_OBJECT public:using QWidget::QWidget; };
class FakeCalendar:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.kylin.Lqui.Calendar")
public:int calls=0;QRect anchor;
public slots:
    Q_SCRIPTABLE void Toggle(int x,int y,int w,int h){++calls;anchor=QRect(x,y,w,h);}
};
class FakePet:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.AiPet")
public: int toggles=0;
public slots:
    Q_SCRIPTABLE void Toggle(){++toggles;}
};
class FakeStation:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.LiquidCli")
public:QString chatPet;int petOpens=0;int calls=0,preferences=0,drops=0,captures=0,actionDrops=0;QRect anchor,settingsAnchor,dropAnchor,captureAnchor;QJsonObject payload;QString action;bool capturePopup=false;
public slots:
    Q_SCRIPTABLE void PetChatAt(const QString &id,int x,int y,int w,int h){++petOpens;chatPet=id;anchor=QRect(x,y,w,h);}
    Q_SCRIPTABLE void ToggleAt(int x,int y,int w,int h){++calls;anchor=QRect(x,y,w,h);}
    Q_SCRIPTABLE void PreferencesAt(int x,int y,int w,int h){++preferences;settingsAnchor=QRect(x,y,w,h);}
    Q_SCRIPTABLE void CaptureAt(int x,int y,int w,int h){++captures;captureAnchor=QRect(x,y,w,h);capturePopup=QApplication::activePopupWidget()!=nullptr;}
    Q_SCRIPTABLE bool ImportDropActionAt(const QString &manifest,const QString &name,int x,int y,int w,int h){payload=StationDrop::read(manifest);action=name;dropAnchor=QRect(x,y,w,h);++actionDrops;StationDrop::discard(manifest);return !payload.isEmpty();}
    Q_SCRIPTABLE bool ImportDropAt(const QString &manifest,int x,int y,int w,int h){payload=StationDrop::read(manifest);dropAnchor=QRect(x,y,w,h);++drops;StationDrop::discard(manifest);return !payload.isEmpty();}
};
class StationTest:public QObject {
    Q_OBJECT
    static void write(const QString &path,const QByteArray &data){QDir().mkpath(QFileInfo(path).path());QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(data);}
private slots:
    void clockUsesIndependentCalendarWithoutNativeActivation(){
        auto bus=QDBusConnection::sessionBus();FakeCalendar service;
        QVERIFY(bus.registerService("org.kylin.Lqui.Calendar"));
        QVERIFY(bus.registerObject("/Calendar",&service,QDBusConnection::ExportAllSlots));
        UKUIPanel panel;panel.resize(660,48);auto *row=new QHBoxLayout(&panel);
        auto *wrapper=new QWidget(&panel);wrapper->setObjectName("ukui-panel_plugin_QFrame_calendar");
        auto *layout=new QHBoxLayout(wrapper);auto *calendar=new QWidget(wrapper);auto *inside=new QHBoxLayout(calendar);
        auto *button=new QPushButton("clock",calendar);inside->addWidget(button);layout->addWidget(calendar);row->addWidget(wrapper);
        int nativeCalls=0;connect(button,&QPushButton::clicked,[&]{++nativeCalls;});
        panel.show();PanelAiStation bridge(&panel);
        QTRY_VERIFY(button->property("stationMinimalClock").toBool());
        QTest::mouseClick(button,Qt::LeftButton);QTRY_COMPARE(service.calls,1);QCOMPARE(nativeCalls,0);
        QVERIFY(service.anchor.width()>0);QTest::qWait(270);
        QTest::keyClick(button,Qt::Key_Space);QTRY_COMPARE(service.calls,2);QCOMPARE(nativeCalls,0);
        QVERIFY(bridge.OpenCalendar());QTRY_COMPARE(service.calls,3);
        bus.unregisterObject("/Calendar");bus.unregisterService("org.kylin.Lqui.Calendar");
    }
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
    void qilinGreetsOncePerVisit(){
        UKUIPanel panel;panel.resize(180,48);
        QPushButton button(&panel);button.setObjectName("StartMenuButton");button.setFixedSize(44,44);
        PanelAiStation adapter(&panel);
        QTRY_VERIFY(button.findChild<QTimer*>("qilinGestureFrame"));
        auto *timer=button.findChild<QTimer*>("qilinGestureFrame");
        QVERIFY(timer->isSingleShot());QVERIFY(!timer->isActive());
        QEvent enter(QEvent::Enter),leave(QEvent::Leave);
        QSet<int> all;int last=-1;
        for(int round=0;round<3;++round){
            QSet<int> seen;
            for(int i=0;i<4;++i){
                QApplication::sendEvent(&button,&enter);QVERIFY(timer->isActive());
                // Each gesture has its own cadence. A full bag visits all four.
                const int beat=timer->interval();QVERIFY(beat!=last);QVERIFY(!seen.contains(beat));
                seen.insert(beat);last=beat;
                QApplication::sendEvent(&button,&leave);QVERIFY(!timer->isActive());
            }
            if(round==0)all=seen;else QCOMPARE(seen,all);
        }
        QSignalSpy frames(timer,&QTimer::timeout);
        QApplication::sendEvent(&button,&enter);QTRY_VERIFY(!timer->isActive());
        const int played=frames.count();QVERIFY(played>=12&&played<=14);
        QTest::qWait(180);QCOMPARE(frames.count(),played);
        QApplication::sendEvent(&button,&enter);QVERIFY(timer->isActive());
        QSignalSpy clicks(&button,&QPushButton::clicked);
        QTest::mouseClick(&button,Qt::LeftButton);QCOMPARE(clicks.count(),1);QVERIFY(!timer->isActive());
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
        auto *leftAccent=start->findChild<QWidget*>("aiStationOrbAccent");QVERIFY(leftAccent);
        QApplication::sendEvent(start,&enter);QVERIFY(!leftAccent->property("animating").toBool());
        QVERIFY(start->findChild<QTimer*>("qilinGestureFrame")->isActive());
        QEvent leaveStart(QEvent::Leave);QApplication::sendEvent(start,&leaveStart);
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
        QVERIFY(orb->property("dropHint").toBool()); // "+" feedback while dragging over the orb
        // Release opens quick actions at the orb instead of importing at once.
        auto dropAndMenu=[&](QMimeData &data)->QMenu*{
            QDragEnterEvent enter(orb->rect().center(),Qt::CopyAction,&data,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(orb,&enter);
            QDropEvent drop(orb->rect().center(),Qt::CopyAction,&data,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(orb,&drop);
            if(!drop.isAccepted())return nullptr;
            QElapsedTimer wait;wait.start();
            while(wait.elapsed()<3000){for(auto *m:orb->findChildren<QMenu*>("aiStationDropMenu"))if(m->isVisible())return m;QTest::qWait(20);}
            return nullptr;
        };
        auto *dropMenu=dropAndMenu(mime);QVERIFY(dropMenu);QVERIFY(!orb->property("dropHint").toBool());
        for(const auto &id:QStringList{"todo","organize","translate","summary","explain","polish","insert"})QVERIFY2(dropMenu->findChild<QAction*>("aiStationDrop_"+id),qPrintable(id));
        QVERIFY(dropMenu->geometry().bottom()<=orb->mapToGlobal(QPoint()).y()+2||dropMenu->geometry().top()>=orb->mapToGlobal(QPoint(0,orb->height())).y()-2);
        dropMenu->findChild<QAction*>("aiStationDrop_summary")->trigger();
        QTRY_COMPARE(fake.actionDrops,1);QCOMPARE(fake.action,QString("summary"));QCOMPARE(fake.payload.value("text").toString(),QString("拖入的说明"));
        QCOMPARE(fake.dropAnchor,QRect(orb->mapToGlobal(QPoint()),orb->size()));
        dropMenu=dropAndMenu(mime);QVERIFY(dropMenu);dropMenu->findChild<QAction*>("aiStationDrop_insert")->trigger();
        QTRY_COMPARE(fake.drops,1);QCOMPARE(fake.payload.value("text").toString(),QString("拖入的说明"));
        // Todo goes straight to the calendar's task store, without AI.
        QTemporaryDir calendarDir;QFile fakeData(calendarDir.path()+"/calendar_data.py");QVERIFY(fakeData.open(QIODevice::WriteOnly));
        fakeData.write("import json,os\ndef task_path(): return os.path.join(os.path.dirname(__file__),'tasks.json')\ndef mutate_task(path,request):\n    open(path,'w').write(json.dumps(request))\n");fakeData.close();
        const QString resolver=QFINDTESTDATA("../scripts/drop_todo.py");
        QVERIFY(!resolver.isEmpty());QVERIFY(QFile::copy(resolver,calendarDir.path()+"/drop_todo.py"));
        qputenv("LQUI_CALENDAR_DIR",calendarDir.path().toUtf8());
        QMimeData todo;todo.setText("  项目例会通知\n时间：2026年11月25日 10:00  ");dropMenu=dropAndMenu(todo);QVERIFY(dropMenu);dropMenu->findChild<QAction*>("aiStationDrop_todo")->trigger();
        QTRY_VERIFY(QFileInfo::exists(calendarDir.path()+"/tasks.json"));
        {QFile out(calendarDir.path()+"/tasks.json");QVERIFY(out.open(QIODevice::ReadOnly));const auto request=QJsonDocument::fromJson(out.readAll()).object();
         QCOMPARE(request.value("action").toString(),QString("add"));QCOMPARE(request.value("title").toString(),QString("项目例会通知 时间：2026年11月25日 10:00"));
         QCOMPARE(request.value("date").toString(),QString("2026-11-25"));}
        QTRY_VERIFY(orb->toolTip().contains("待办"));
        qunsetenv("LQUI_CALENDAR_DIR");
        // Dismissing the menu discards the staged drop.
        const int staged=QDir(StationDrop::root()).entryList(QDir::Files).size();
        dropMenu=dropAndMenu(mime);QVERIFY(dropMenu);QCOMPARE(QDir(StationDrop::root()).entryList(QDir::Files).size(),staged+1);
        dropMenu->close();QTRY_COMPARE(QDir(StationDrop::root()).entryList(QDir::Files).size(),staged);
        QCOMPARE(fake.actionDrops,1);QCOMPARE(fake.drops,1);
        // Images get image actions.
        QMimeData picture;QImage image(40,30,QImage::Format_RGB32);image.fill(Qt::white);picture.setImageData(image);
        dropMenu=dropAndMenu(picture);QVERIFY(dropMenu);QVERIFY(dropMenu->findChild<QAction*>("aiStationDrop_ocr"));QVERIFY(!dropMenu->findChild<QAction*>("aiStationDrop_todo"));
        dropMenu->findChild<QAction*>("aiStationDrop_ocr")->trigger();QTRY_COMPARE(fake.actionDrops,2);QCOMPARE(fake.action,QString("ocr"));
        QDragEnterEvent moveOnly(orb->rect().center(),Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(orb,&moveOnly);QVERIFY(!moveOnly.isAccepted());
        auto count=[&]{return QJsonDocument::fromJson(adapter.State().toUtf8()).object()["clusters"].toArray()[0].toObject()["samples"].toInt();};
        // One click toggles once. The second press of a double click must not
        // also hide a pet or launch the station's ordinary toggle action.
        QTemporaryDir petPrefs;QVERIFY(petPrefs.isValid());
        const auto configHome=qgetenv("XDG_CONFIG_HOME");qputenv("XDG_CONFIG_HOME",petPrefs.path().toUtf8());
        const auto dataHome=qgetenv("XDG_DATA_HOME");qputenv("XDG_DATA_HOME",petPrefs.path().toUtf8());
        const QString petDir=petPrefs.path()+"/kylin-ai-pets/gemini";QDir().mkpath(petDir);
        QImage portrait(36,36,QImage::Format_ARGB32);portrait.fill(Qt::magenta);QVERIFY(portrait.save(petDir+"/avatar.png"));
        QImage strip(108,36,QImage::Format_ARGB32);
        for(int x=0;x<108;++x)for(int y=0;y<36;++y)
            strip.setPixelColor(x,y,x<36?Qt::green:(x<72?Qt::yellow:Qt::cyan));
        QVERIFY(strip.save(petDir+"/greeting.png"));
        write(petPrefs.path()+"/kylin-ai-pets/greetings.json",R"({"gemini":{"file":"greeting.png","holdMs":[60,60,60]}})");
        QSettings entry(PetEntry::path(),QSettings::IniFormat);
        entry.setValue("entry/mode","pet");entry.setValue("entry/character","gemini");entry.sync();
        FakePet pet;QVERIFY(bus.registerObject("/Pet",&pet,QDBusConnection::ExportScriptableSlots));
        QVERIFY(bus.registerService("org.ukui.AiPet"));
        const int normalCalls=fake.calls;
        auto *greeting=orb->findChild<QTimer*>("petGreetingEnd");QVERIFY(greeting);QVERIFY(greeting->isSingleShot());
        QTest::mouseClick(orb,Qt::LeftButton);QTRY_COMPARE(pet.toggles,1);
        const auto showsFace=[orb](const QColor &expected) {
            const QImage image=orb->grab().toImage();const QColor pixel=image.pixelColor(image.rect().center());
            // Fractional-scale captures can include a small tint from the native style.
            return qAbs(pixel.red()-expected.red())<32 && qAbs(pixel.green()-expected.green())<32
                && qAbs(pixel.blue()-expected.blue())<32;
        };
        QSignalSpy ticks(greeting,&QTimer::timeout);
        QEvent hover(QEvent::Enter);QApplication::sendEvent(orb,&hover);QVERIFY(greeting->isActive());QVERIFY(orb->toolTip().isEmpty());
        QVERIFY(showsFace(Qt::green));QTRY_COMPARE(ticks.count(),3);
        QVERIFY(!greeting->isActive());QVERIFY(showsFace(Qt::magenta));
        QTest::qWait(200);QCOMPARE(ticks.count(),3);
        QApplication::sendEvent(orb,&hover);QEvent leave(QEvent::Leave);QApplication::sendEvent(orb,&leave);
        QVERIFY(!greeting->isActive());QVERIFY(showsFace(Qt::magenta));
        QTest::mouseClick(orb,Qt::LeftButton);QTRY_COMPARE(pet.toggles,2);
        QTest::mouseClick(orb,Qt::LeftButton);
        QTest::mouseDClick(orb,Qt::LeftButton);
        QTest::mouseRelease(orb,Qt::LeftButton);
        QTRY_COMPARE(fake.petOpens,1);QTest::qWait(QApplication::doubleClickInterval()+80);
        QCOMPARE(pet.toggles,2);QCOMPARE(fake.calls,normalCalls);QCOMPARE(fake.chatPet,QString("gemini"));
        QCOMPARE(fake.anchor,QRect(orb->mapToGlobal(QPoint()),orb->size()));
        bus.unregisterService("org.ukui.AiPet");bus.unregisterObject("/Pet");
        if(configHome.isNull())qunsetenv("XDG_CONFIG_HOME");else qputenv("XDG_CONFIG_HOME",configHome);
        if(dataHome.isNull())qunsetenv("XDG_DATA_HOME");else qputenv("XDG_DATA_HOME",dataHome);
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
