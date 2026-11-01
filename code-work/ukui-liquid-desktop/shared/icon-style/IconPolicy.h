#pragma once
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>
#include "IconTheme.h"

namespace LquiIcon {
// One preference shared by all application-icon surfaces. Watching the parent
// also handles QSettings' atomic rename and a configuration created later.
class Policy final : public QObject {
public:
    explicit Policy(QObject *parent):QObject(parent) {
        debounce.setSingleShot(true);debounce.setInterval(40);
        connect(&watcher,&QFileSystemWatcher::fileChanged,this,[this]{debounce.start();});
        connect(&watcher,&QFileSystemWatcher::directoryChanged,this,[this]{debounce.start();});
        connect(&debounce,&QTimer::timeout,this,[this]{reload();});
        reload();
    }
    static QString path(){return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/kylin-lqui/appearance.ini";}
    bool enabled=true;
    Theme theme=Theme::Color;
    quint64 generation=1;
    bool set(bool value) {
        return write("icons/uniform",value);
    }
    bool setTheme(Theme value){return write("icons/theme",themeKey(value));}
private:
    bool write(const QString &key,const QVariant &value) {
        QDir().mkpath(QFileInfo(path()).absolutePath());
        QSettings settings(path(),QSettings::IniFormat);settings.setValue(key,value);settings.sync();
        if(settings.status()!=QSettings::NoError)return false;
        reload();return true;
    }
    QFileSystemWatcher watcher;
    QTimer debounce;
    void reload() {
        QSettings settings(path(),QSettings::IniFormat);const bool next=settings.value("icons/uniform",true).toBool();
        const Theme nextTheme=parseTheme(settings.value("icons/theme","color").toString());
        if(next!=enabled||nextTheme!=theme) {
            enabled=next;theme=nextTheme;++generation;
            // The outer renderers include this generation in their cache keys.
            for(auto *widget:QApplication::allWidgets())if(widget->isVisible())widget->update();
        }
        if(!watcher.files().isEmpty())watcher.removePaths(watcher.files());
        if(!watcher.directories().isEmpty())watcher.removePaths(watcher.directories());
        if(QFileInfo::exists(path()))watcher.addPath(path());
        QString directory=QFileInfo(path()).absolutePath();
        while(!QDir(directory).exists()) {
            const QString up=QFileInfo(directory).absolutePath();if(up==directory)break;directory=up;
        }
        if(QDir(directory).exists())watcher.addPath(directory);
    }
};
inline Policy &policy(){static QPointer<Policy> instance;if(!instance)instance=new Policy(qApp);return *instance;}
inline bool uniform(){return policy().enabled;}
inline quint64 revision(){return policy().generation;}
inline bool setUniform(bool value){return policy().set(value);}
inline Theme theme(){return policy().theme;}
inline bool setTheme(Theme value){return policy().setTheme(value);}
}
