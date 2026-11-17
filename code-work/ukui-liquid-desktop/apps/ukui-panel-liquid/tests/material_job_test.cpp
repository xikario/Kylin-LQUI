#include "PanelMaterialJob.h"
#include "../../../shared/effects/EffectsPolicy.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QDebug>
int main(int argc,char **argv){
    QTemporaryDir runtime;qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8()); // isolated session health
    QApplication app(argc,argv);QTemporaryDir root;
    const QString worker=app.arguments().value(1);
    QFile fake(root.path()+"/stall");if(!fake.open(QIODevice::WriteOnly))return 1;
    fake.write("#!/usr/bin/python3\nimport sys,time\nsys.stdin.buffer.read()\ntime.sleep(20)\n");fake.close();fake.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    QImage source(180,72,QImage::Format_RGB32);source.setDevicePixelRatio(1.5);source.fill(QColor("#285080"));
    QJsonObject settings{{"radius",12},{"finish","soft"},{"refraction",3.5},{"opacity",.5},{"highlight",.4},{"chroma",.3},{"clarity",.2},{"strength",1.}};
    int ticks=0;QTimer heartbeat;QObject::connect(&heartbeat,&QTimer::timeout,[&]{++ticks;});heartbeat.start(5);
    bool completed=false,stale=false;
    qputenv("LQUI_PANEL_MATERIAL_WORKER",fake.fileName().toUtf8());
    {
        PanelMaterialJob request(&app);QEventLoop loop;
        request.request(source,settings,[&](const QImage &,bool){stale=true;});
        QTimer::singleShot(40,&request,[&]{request.request(source,settings,[&](const QImage &image,bool){completed=image.isNull();loop.quit();});});
        QTimer::singleShot(4000,&loop,&QEventLoop::quit);loop.exec();
    }
    if(!completed||stale||ticks<100){qCritical()<<"worker timeout blocked or delivered obsolete image"<<ticks;return 2;}
    {
        // A stuck worker is reported to the session health (one timeout: not yet tripped).
        QFile health(LquiEffects::healthPath());
        if(!health.open(QIODevice::ReadOnly)||!health.readAll().contains("panel-material-worker")){qCritical()<<"worker timeout not reported";return 6;}
        LquiEffects::invalidate();if(LquiEffects::tripped()){qCritical()<<"one timeout must not trip";return 7;}
        LquiEffects::clearHealth();
    }
    qputenv("LQUI_PANEL_MATERIAL_WORKER",worker.toUtf8());completed=false;
    {
        PanelMaterialJob request(&app);QEventLoop loop;
        request.request(source,settings,[&](const QImage &image,bool){completed=image.size()==source.size()&&image.devicePixelRatio()==1.5;loop.quit();});
        QTimer::singleShot(4000,&loop,&QEventLoop::quit);loop.exec();
    }
    if(!completed)return 3;
    qInfo()<<"PASS: stuck render process cannot block GUI; stale work discarded; worker frame delivered";
    return 0;
}
