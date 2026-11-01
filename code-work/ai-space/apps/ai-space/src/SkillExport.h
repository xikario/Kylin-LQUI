#pragma once
#include "SpacePresentation.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <memory>
namespace SkillExport {
inline bool copyTree(const QString &source,const QString &target,QString *error,int &files,qint64 &bytes,const std::shared_ptr<std::atomic_bool> &cancel) {
    if(cancel->load()||QThread::currentThread()->isInterruptionRequested()){*error=QStringLiteral("导出已取消");return false;}
    if(!QDir().mkpath(target)){*error=QStringLiteral("无法创建暂存目录");return false;}
    for(const auto &entry:QDir(source).entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDir::DirsFirst|QDir::Name)) {
        if(cancel->load()||QThread::currentThread()->isInterruptionRequested()){*error=QStringLiteral("导出已取消");return false;}
        if(--files<0||entry.isSymLink()){*error=QStringLiteral("导出超过文件预算或包含符号链接");return false;}
        const QString output=QDir(target).filePath(entry.fileName());
        if(entry.isDir()){if(!copyTree(entry.absoluteFilePath(),output,error,files,bytes,cancel))return false;}
        else {
            bytes-=entry.size();if(bytes<0){*error=QStringLiteral("Skill 超过 128 MiB 导出预算");return false;}
            QFile::remove(output);
            if(!QFile::copy(entry.absoluteFilePath(),output)){*error=QStringLiteral("无法复制：%1").arg(entry.fileName());return false;}
        }
    }
    return true;
}
// Runs on a worker. Only complete payloads replace the target; failed/cancelled
// copies leave the previous export untouched and remove temporary files.
inline QString publish(const QString &source,const QString &target,const QString &runtime,const std::shared_ptr<std::atomic_bool> &cancel) {
    const QString src=QFileInfo(source).canonicalFilePath();const QFileInfo output(target);
    const QString parent=output.absoluteDir().canonicalPath();
    const QString dst=output.exists()?output.canonicalFilePath():QDir(parent).filePath(output.fileName());
    if(src.isEmpty()||!QFileInfo(src).isDir()||parent.isEmpty()||(output.exists()&&!output.isDir())||output.isSymLink()||AiSpaceUi::pathIsInside(dst,src))return QStringLiteral("目标不能位于源目录内部或指向符号链接");
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".ai-space-export-XXXXXX")));
    if(!staging.isValid())return QStringLiteral("无法创建导出暂存目录");
    const QString prepared=staging.filePath("prepared"),backup=staging.filePath("previous");
    QString error;int files=10000;qint64 bytes=128ll*1024*1024;
    if(output.exists()&&!copyTree(dst,prepared,&error,files,bytes,cancel))return error;
    if(!copyTree(src,prepared,&error,files,bytes,cancel))return error;
    const QString worker=QDir(prepared).filePath("scripts/knowledge_library.py");
    QDir().mkpath(QFileInfo(worker).absolutePath());QFile::remove(worker);
    if(!QFile::copy(runtime,worker))return QStringLiteral("无法导出独立检索运行时");
    if(cancel->load())return QStringLiteral("导出已取消");
    const bool existed=output.exists();
    if(existed&&!QDir().rename(target,backup))return QStringLiteral("无法保留旧导出");
    if(!QDir().rename(prepared,target)) {
        if(existed&&!QDir().rename(backup,target)){staging.setAutoRemove(false);return QStringLiteral("导出发布失败；原目录保留在：%1").arg(backup);}
        return QStringLiteral("导出发布失败，原目录已恢复");
    }
    return {};
}
}
