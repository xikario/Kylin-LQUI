#pragma once
#include <QFile>
#include <QSaveFile>
#include <QHash>
#include <QVariant>

// Desktop entries are not Qt INI files: commas and semicolons are literal,
// localized keys are preserved and group names must never be percent-encoded.
namespace DesktopEntryFile {
inline QString decode(const QString &raw) {
    QString value;
    for(int i=0;i<raw.size();++i) {
        if(raw[i]=='\\' && i+1<raw.size()) {
            const QChar next=raw[++i];
            if(next=='s')value+=' ';else if(next=='n')value+='\n';
            else if(next=='t')value+='\t';else if(next=='r')value+='\r';
            else if(next=='\\')value+='\\';else {value+='\\';value+=next;}
        } else value+=raw[i];
    }
    return value;
}
class Reader {
public:
    explicit Reader(const QString &path) {
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return;
        bool entry=false;
        for(const QString &line:QString::fromUtf8(file.readAll()).split('\n')) {
            const QString trimmed=line.trimmed();
            if(trimmed.startsWith('[')){entry=trimmed=="[Desktop Entry]";continue;}
            if(!entry||trimmed.startsWith('#'))continue;
            const int equals=line.indexOf('=');if(equals<=0)continue;
            values.insert(line.left(equals).trimmed(),decode(line.mid(equals+1).trimmed()));
        }
    }
    QVariant value(const QString &key,const QVariant &fallback={}) const {
        return values.contains(key)?QVariant(values.value(key)):fallback;
    }
private:QHash<QString,QString> values;
};
inline bool setIcon(const QString &path,QString icon) {
    QFile input(path);if(!input.open(QIODevice::ReadOnly))return false;
    QByteArray bytes=input.readAll();input.close();
    QList<QByteArray> lines=bytes.split('\n');bool entry=false,found=false,group=false;
    icon.replace("\\","\\\\");icon.replace("\n","\\n");icon.replace("\r","\\r");icon.replace("\t","\\t");
    const QByteArray replacement="Icon="+icon.toUtf8();
    for(int i=0;i<lines.size();++i) {
        const QByteArray trimmed=lines[i].trimmed();
        if(trimmed.startsWith('[')) {
            if(entry&&!found){lines.insert(i++,replacement);found=true;}
            entry=trimmed=="[Desktop Entry]";group|=entry;
        } else if(entry&&trimmed.left(trimmed.indexOf('=')).trimmed()=="Icon") {
            if(!found){lines[i]=replacement;found=true;}else lines.removeAt(i--);
        }
    }
    if(!group)return false;
    if(!found)lines.append(replacement);
    bytes=lines.join('\n');QSaveFile output(path);
    return output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.commit();
}
}
