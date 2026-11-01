#include "Conversation.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

Conversation::Conversation(QString folder):base(folder.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/liquid-cli":std::move(folder)),root(base){}
void Conversation::useBackend(const QString &id) {
    root=id=="qwen"||id=="deepseek"?base+"/backends/"+id:base;
    items.clear();threadId.clear();contextTokens=0;contextWindow=0;
}

bool Conversation::load() {
    QFile f(root+"/conversation.json");
    if(!f.open(QIODevice::ReadOnly))return false;
    const auto o=QJsonDocument::fromJson(f.readAll()).object();
    if(o.isEmpty())return false;
    threadId=o.value("threadId").toString();
    contextTokens=qint64(o.value("contextTokens").toDouble());contextWindow=qint64(o.value("contextWindow").toDouble());
    items.clear();
    for(const auto v:o.value("messages").toArray()) {
        const auto m=v.toObject();ChatMessage c;
        c.role=m.value("role").toString();c.text=m.value("text").toString();c.meta=m.value("meta").toString();
        c.time=qint64(m.value("time").toDouble());
        for(const auto i:m.value("images").toArray())c.images<<i.toString();
        if(c.role=="user"||c.role=="assistant"||c.role=="note")items<<c;
    }
    return true;
}
bool Conversation::save(QString *error) const {
    QDir().mkpath(root);QJsonArray list;
    for(const auto &m:items) {
        QJsonObject o{{"role",m.role},{"text",m.text},{"time",double(m.time)}};
        if(!m.meta.isEmpty())o["meta"]=m.meta;
        if(!m.images.isEmpty())o["images"]=QJsonArray::fromStringList(m.images);
        list<<o;
    }
    const QJsonObject doc{{"version",1},{"threadId",threadId},{"contextTokens",double(contextTokens)},
        {"contextWindow",double(contextWindow)},{"messages",list}};
    QSaveFile f(root+"/conversation.json");
    if(!f.open(QIODevice::WriteOnly)){if(error)*error=f.errorString();return false;}
    f.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    if(f.write(QJsonDocument(doc).toJson(QJsonDocument::Compact))<0||!f.commit()){if(error)*error=f.errorString();return false;}
    return true;
}
void Conversation::clear() {
    items.clear();threadId.clear();contextTokens=0;contextWindow=0;
    QDir(root+"/images").removeRecursively();
}
int Conversation::append(const ChatMessage &message) {
    ChatMessage m=message;if(!m.time)m.time=QDateTime::currentMSecsSinceEpoch();
    items<<m;return items.size()-1;
}
void Conversation::replace(int index,const ChatMessage &message){if(index>=0&&index<items.size())items[index]=message;}
QString Conversation::storeImage(const QImage &image,QString *error) const {
    const QString dir=root+"/images";
    if(!QDir().mkpath(dir)){if(error)*error="无法创建图片目录";return {};}
    QFile::setPermissions(dir,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    const QString path=dir+"/"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-")+QUuid::createUuid().toString(QUuid::Id128).left(8)+".png";
    if(!image.save(path,"PNG")){if(error)*error="图片保存失败";return {};}
    QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner);return path;
}
