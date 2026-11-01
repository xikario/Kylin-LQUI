#pragma once
#include <QFile>
#include <sys/stat.h>
// Identity of the directory entry itself, including dangling links. Content
// edits preserve identity; an external replacement does not inherit undo rights.
inline QByteArray fileObjectIdentity(const QString &path) {
    struct stat value{};
    if(::lstat(QFile::encodeName(path).constData(),&value)!=0)return {};
    return QByteArray::number(quint64(value.st_dev))+':'+QByteArray::number(quint64(value.st_ino));
}
