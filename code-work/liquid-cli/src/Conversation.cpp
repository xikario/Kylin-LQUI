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
    writable=true;
}

bool Conversation::load() {
    QFile f(root+"/conversation.json");
    writable=!QFileInfo::exists(f.fileName())&&!QFileInfo(f.fileName()).isSymLink();
    if(!f.open(QIODevice::ReadOnly))return false;
    writable=false;
    const auto o=QJsonDocument::fromJson(f.readAll()).object();
    if(f.error()!=QFile::NoError||!o.value("messages").isArray())return false;
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
    writable=true;return true;
}
bool Conversation::save(QString *error) const {
    if(!writable){if(error)*error="原聊天记录读取失败，已保护原件；请恢复文件或在设置中明确开始新对话。";return false;}
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
bool Conversation::clear() {
    if(!writable) {
        const QString original=root+"/conversation.json";
        if(!QFile::rename(original,original+".recovery-"+QUuid::createUuid().toString(QUuid::Id128)))return false;
        writable=true;
    }
    items.clear();threadId.clear();contextTokens=0;contextWindow=0;
    QDir(root+"/images").removeRecursively();
    return true;
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
