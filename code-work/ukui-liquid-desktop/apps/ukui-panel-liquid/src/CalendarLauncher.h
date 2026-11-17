#pragma once
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRect>
#include "AsyncServiceLaunch.h"
namespace LquiCalendar {
inline bool open(const QRect &anchor) {
    const QString name="org.kylin.Lqui.Calendar";
    auto call=QDBusMessage::createMethodCall(name,"/Calendar",name,"Toggle");
    call.setArguments({anchor.x(),anchor.y(),anchor.width(),anchor.height()});
    const auto path=qEnvironmentVariable("LQUI_CALENDAR_LAUNCHER",QDir::homePath()+"/.local/bin/lqui-calendar");
    return callOrLaunch(call,path,{"--anchor",QString("%1,%2,%3,%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height())});
}
}
