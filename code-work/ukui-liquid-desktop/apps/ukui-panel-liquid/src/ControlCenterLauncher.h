#pragma once
#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRect>
#include "AsyncServiceLaunch.h"
namespace LquiControl {
inline QString launcher(){return qEnvironmentVariable("LQUI_CONTROL_CENTER_LAUNCHER",QDir::homePath()+"/.local/bin/kylin-lqui-control-center");}
inline bool enabled(){return qApp&&qApp->property("liquidSystemControlsEnabled").toBool()&&QFileInfo(launcher()).isExecutable();}
inline void prepare(const QString &page,const QRect &anchor){
    if(!enabled())return;
    auto bus=QDBusConnection::sessionBus();
    auto message=QDBusMessage::createMethodCall("org.kylin.Lqui.ControlCenter","/ControlCenter","org.kylin.Lqui.ControlCenter","Prepare");
    message.setAutoStartService(false);message.setArguments({page,anchor.x(),anchor.y(),anchor.width(),anchor.height()});bus.asyncCall(message,500);
}
inline bool open(const QString &page,const QRect &anchor){
    if(!enabled())return false;
    auto message=QDBusMessage::createMethodCall("org.kylin.Lqui.ControlCenter","/ControlCenter","org.kylin.Lqui.ControlCenter","Open");
    message.setArguments({page,anchor.x(),anchor.y(),anchor.width(),anchor.height()});
    return callOrLaunch(message,launcher(),{"--page",page,"--anchor",QString("%1,%2,%3,%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height())});
}
}
