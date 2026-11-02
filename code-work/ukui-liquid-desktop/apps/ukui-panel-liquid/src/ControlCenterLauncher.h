#pragma once
#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRect>
namespace LquiControl {
inline QString launcher(){return qEnvironmentVariable("LQUI_CONTROL_CENTER_LAUNCHER",QDir::homePath()+"/.local/bin/kylin-lqui-control-center");}
inline bool enabled(){return qApp&&qApp->property("liquidSystemControlsEnabled").toBool()&&QFileInfo(launcher()).isExecutable();}
inline void prepare(const QString &page,const QRect &anchor){
    if(!enabled())return;
    auto bus=QDBusConnection::sessionBus();
    if(!bus.interface()||!bus.interface()->isServiceRegistered("org.kylin.Lqui.ControlCenter").value())return;
    auto message=QDBusMessage::createMethodCall("org.kylin.Lqui.ControlCenter","/ControlCenter","org.kylin.Lqui.ControlCenter","Prepare");
    message.setArguments({page,anchor.x(),anchor.y(),anchor.width(),anchor.height()});bus.asyncCall(message);
}
inline bool open(const QString &page,const QRect &anchor){
    if(!enabled())return false;
    auto bus=QDBusConnection::sessionBus();
    if(bus.interface()&&bus.interface()->isServiceRegistered("org.kylin.Lqui.ControlCenter").value()){
        auto message=QDBusMessage::createMethodCall("org.kylin.Lqui.ControlCenter","/ControlCenter","org.kylin.Lqui.ControlCenter","Open");
        message.setArguments({page,anchor.x(),anchor.y(),anchor.width(),anchor.height()});bus.asyncCall(message);return true;
    }
    return QProcess::startDetached(launcher(),{"--page",page,"--anchor",QString("%1,%2,%3,%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height())});
}
}
