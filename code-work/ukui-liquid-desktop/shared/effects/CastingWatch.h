#pragma once
#include "EffectsPolicy.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <unistd.h>

// Casting detection for the global effects policy. Run it in ONE long-lived
// process per session (the taskbar). While a configured casting app of this
// user runs, the session health records casting=true and every module that
// follows EffectsPolicy drops to the lite tier (if castingLite is on).
//
// Cost: one coarse 5 s timer reading /proc/<pid>/comm of the user's processes
// (a few hundred small reads, no X11, no D-Bus). Configurable list:
//   ~/.config/kylin-lqui/appearance.ini  effects/castingApps (default ExceedShare)
namespace LquiEffects {

inline QStringList castingApps(){
    const QStringList configured=QSettings(path(),QSettings::IniFormat)
        .value(QStringLiteral("effects/castingApps"),QStringList{QStringLiteral("ExceedShare")}).toStringList();
    QStringList names;
    for(const QString &name:configured){
        const QString trimmed=name.trimmed();
        // /proc comm is truncated to 15 bytes by the kernel.
        if(!trimmed.isEmpty())names<<trimmed.left(15);
    }
    return names;
}

class CastingWatch final : public QObject {
public:
    explicit CastingWatch(QObject *parent,int intervalMs=5000):QObject(parent){
        timer.setInterval(intervalMs);timer.setTimerType(Qt::VeryCoarseTimer);
        connect(&timer,&QTimer::timeout,this,[this]{scanNow();});
        scanNow();          // correct a stale state left by a crashed previous taskbar
        timer.start();
    }
    ~CastingWatch() override {if(active)setCasting(false,QString());}

    // Public for tests; the timer calls it otherwise.
    void scanNow(){
        const QString found=runningCaster();
        const bool now=!found.isEmpty();
        if(now==active&&found==app&&now==casting())return;
        active=now;app=found;
        setCasting(now,found);
    }
private:
    QTimer timer;
    bool active=false;
    QString app;
    static QString runningCaster(){
        const QStringList names=castingApps();
        if(names.isEmpty())return {};
        const uint uid=getuid();
        const auto entries=QDir(QStringLiteral("/proc")).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
        for(const QString &pid:entries){
            bool numeric=false;pid.toULongLong(&numeric);if(!numeric)continue;
            if(QFileInfo(QStringLiteral("/proc/")+pid).ownerId()!=uid)continue;
            QFile comm(QStringLiteral("/proc/")+pid+QStringLiteral("/comm"));
            if(!comm.open(QIODevice::ReadOnly))continue;
            const QString name=QString::fromLocal8Bit(comm.readAll()).trimmed();
            if(names.contains(name))return name;
        }
        return {};
    }
};
}
