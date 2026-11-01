#include "PanelNotificationHistory.h"
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDateTime>
#include <QApplication>
PanelNotificationHistory::PanelNotificationHistory(QObject *parent):QObject(parent){
    path=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/kylin-lqui/notifications.json";
    QFile file(path);if(file.open(QIODevice::ReadOnly)&&file.size()<2*1024*1024)entries=QJsonDocument::fromJson(file.readAll()).array();
    while(entries.size()>200)entries.removeFirst();
    pending.setSingleShot(true);pending.setInterval(250);connect(&pending,&QTimer::timeout,this,&PanelNotificationHistory::save);
    auto bus=QDBusConnection::sessionBus();bus.registerObject("/org/kylin/Lqui/Notifications",this,QDBusConnection::ExportScriptableSlots);
    bus.connect("org.ukui.Sidebar","/org/ukui/Sidebar/notification","org.ukui.Sidebar.notification","Sig_Notify",this,SLOT(received(QDBusMessage)));
}
PanelNotificationHistory::~PanelNotificationHistory(){if(pending.isActive())save();QDBusConnection::sessionBus().unregisterObject("/org/kylin/Lqui/Notifications");}
QString PanelNotificationHistory::List()const{return QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact));}
bool PanelNotificationHistory::Clear(){
    auto bus=QDBusConnection::sessionBus();for(const auto &value:entries){bool ok=false;const uint id=value.toObject()["id"].toString().toUInt(&ok);if(ok){auto call=QDBusMessage::createMethodCall("org.ukui.Sidebar","/org/ukui/Sidebar/notification","org.ukui.Sidebar.notification","closeNotification");call<<id;bus.asyncCall(call);}}
    entries={};save();return true;
}
void PanelNotificationHistory::received(const QDBusMessage &message){
    if(!qApp->property("liquidNotificationHistory").toBool())return;
    const auto args=message.arguments();if(args.size()<6)return;
    const QString id=args[0].toString().left(80),replace=args[1].toString().left(80);if(id.isEmpty())return;
    for(int i=entries.size()-1;i>=0;--i){const auto old=entries[i].toObject()["id"].toString();if(old==id||(!replace.isEmpty()&&replace!="0"&&old==replace))entries.removeAt(i);}
    entries.append(QJsonObject{{"id",id},{"app",args[2].toString().left(160)},{"title",args[4].toString().left(1024)},{"body",args[5].toString().left(8192)},{"time",QDateTime::currentMSecsSinceEpoch()}});
    while(entries.size()>200)entries.removeFirst();pending.start();
}
void PanelNotificationHistory::save(){
    QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return;file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);file.write(QJsonDocument(entries).toJson(QJsonDocument::Compact));file.commit();
}
