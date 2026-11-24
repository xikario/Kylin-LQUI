#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPixmap>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>
#include <QDateTime>
#include <QImageReader>
#include <QList>
#include <unistd.h>

namespace StationDrop {
constexpr int kManifestBytes=4*1024*1024;
inline QString root(){return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)+"/ai-station/drops";}
inline bool owns(const QString &path) {
    const QFileInfo file(path);return file.exists()&&file.isFile()&&!file.isSymLink()&&file.ownerId()==getuid()&&file.absolutePath()==QFileInfo(root()).absoluteFilePath();
}
inline bool accepts(const QMimeData *mime){return mime&&(mime->hasUrls()||mime->hasImage()||mime->hasText());}
inline QString prepare(const QMimeData *mime,QString *error=nullptr) {
    if(!accepts(mime))return {};
    if(!QDir().mkpath(root())){if(error)*error="无法暂存拖入内容";return {};}
    QFile::setPermissions(root(),QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    for(const auto &file:QDir(root()).entryInfoList(QDir::Files))if(file.lastModified().secsTo(QDateTime::currentDateTime())>86400&&!file.isSymLink())QFile::remove(file.absoluteFilePath());
    const QString stem=root()+"/"+QUuid::createUuid().toString(QUuid::Id128);QJsonObject payload;QJsonArray files;QStringList links,seen;
    for(const auto &url:mime->urls()) {
        if(url.isLocalFile()){const QString path=QDir::cleanPath(QFileInfo(url.toLocalFile()).absoluteFilePath());if(!seen.contains(path)){files.append(path);seen<<path;}}
        else if(url.isValid())links<<url.toString();
    }
    payload["files"]=files;
    if(files.isEmpty()&&mime->hasImage()) {
        QImage image=qvariant_cast<QImage>(mime->imageData());if(image.isNull())image=qvariant_cast<QPixmap>(mime->imageData()).toImage();
        const QString path=stem+".png";
        if(image.isNull()||!image.save(path,"PNG")){if(error)*error="无法读取拖入图片";return {};}
        QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner);payload["image"]=path;
    } else if(!links.isEmpty())payload["text"]=links.join('\n');
    else if(files.isEmpty()&&mime->hasText())payload["text"]=mime->text();
    if(files.isEmpty()&&!payload.contains("image")&&payload.value("text").toString().isEmpty())return {};
    const QByteArray encoded=QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if(encoded.size()>kManifestBytes){if(payload.contains("image"))QFile::remove(payload["image"].toString());if(error)*error="拖入内容超过 4 MiB，请缩小内容后重试。";return {};}
    QSaveFile file(stem+".json");
    if(!file.open(QIODevice::WriteOnly)){if(error)*error="无法暂存拖入内容";return {};}
    file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    if(file.write(encoded)!=encoded.size()||!file.commit()){if(error)*error="无法保存拖入内容";return {};}
    return file.fileName();
}
inline QJsonObject read(const QString &path) {
    if(!owns(path)||!path.endsWith(".json")||QFileInfo(path).size()>kManifestBytes)return {};
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};const auto bytes=file.read(kManifestBytes+1);if(bytes.size()>kManifestBytes)return {};return QJsonDocument::fromJson(bytes).object();
}
inline void discard(const QString &path) {
    const auto payload=read(path);const QString image=payload.value("image").toString();if(owns(image))QFile::remove(image);if(owns(path))QFile::remove(path);
}
// What a drop contains decides which quick actions the orb offers after release.
// The ids are shared with AI小站 (Drawer::ImportDropActionAt), which owns prompts.
inline bool isImageFile(const QString &path){return !QImageReader::imageFormat(path).isEmpty();}
inline QString kind(const QJsonObject &payload) {
    const auto files=payload.value("files").toArray();
    if(payload.contains("image"))return "image";
    if(!files.isEmpty()) {
        bool images=true;for(const auto &f:files)images&=isImageFile(f.toString());
        return images?"image":"files";
    }
    return "text";
}
struct DropAction {QString id,label;};
inline QList<DropAction> actions(const QJsonObject &payload) {
    const QString type=kind(payload);
    if(type=="image")return {{"ocr","图片识别"},{"translate","翻译图中文字"},{"summary","总结图片内容"},{"explain","解释图片"},{"insert","放入输入框"}};
    if(type=="files")return {{"summary","总结文件"},{"organize","梳理要点"},{"translate","翻译"},{"todo","加入待办"},{"insert","放入输入框"}};
    return {{"todo","加入待办"},{"organize","文字梳理"},{"translate","文本翻译"},{"summary","总结要点"},{"explain","解释说明"},{"polish","润色改写"},{"insert","放入输入框"}};
}
// One-line todo title from the dropped text or file names (no AI involved).
inline QString todoTitle(const QJsonObject &payload) {
    QString text=payload.value("text").toString();
    if(text.trimmed().isEmpty()) {
        QStringList names;for(const auto &f:payload.value("files").toArray())names<<QFileInfo(f.toString()).fileName();
        text=names.join(QStringLiteral("、"));
    }
    text=text.simplified();
    return text.size()>200?text.left(199)+QChar(0x2026):text;
}
}
