#pragma once
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>

inline bool callOrLaunch(QDBusMessage message,const QString &program,const QStringList &arguments) {
    const auto bus=QDBusConnection::sessionBus();
    if(!QCoreApplication::instance() || !bus.isConnected())return false;
    message.setAutoStartService(false);
    auto *call=new QDBusPendingCallWatcher(bus.asyncCall(message,1000),QCoreApplication::instance());
    QObject::connect(call,&QDBusPendingCallWatcher::finished,call,[call,program,arguments]{
        QDBusPendingReply<> reply=*call;call->deleteLater();
        if(!reply.isError())return;
        const auto name=reply.error().name();
        if(name=="org.freedesktop.DBus.Error.ServiceUnknown" || name=="org.freedesktop.DBus.Error.NameHasNoOwner") {
            if(!QFileInfo(program).isExecutable() || !QProcess::startDetached(program,arguments))
                qWarning()<<"[LquiPanel] cannot launch"<<program;
        } // A timeout never starts a duplicate process or blocks the panel.
    });
    return true;
}
