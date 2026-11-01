#pragma once
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFileInfo>

namespace ResidentStartup {
inline QString path(){return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/autostart/kylin-lqui-ai-station.desktop";}
inline bool enabled(){QFile f(path());return f.open(QIODevice::ReadOnly)&&f.readAll().contains("--resident") ;}
inline bool save(bool enabled,QString *error=nullptr) {
    const QString file=path();
    if(!enabled){if(!QFileInfo::exists(file)||QFile::remove(file))return true;if(error)*error="无法删除 AI 小站登录启动项";return false;}
    QDir().mkpath(QFileInfo(file).absolutePath());
    // Use the stable user launcher so release switching does not pin old code.
    QString launcher=QDir::homePath()+"/.local/bin/liquid-cli";
    if(!QFileInfo(launcher).isExecutable())launcher=QCoreApplication::applicationFilePath();
    launcher.replace("%","%%");for(const auto c:{'\\','"','`','$'})launcher.replace(QChar(c),QString("\\")+QChar(c));launcher.replace("\\","\\\\");
    QSaveFile f(file);if(!f.open(QIODevice::WriteOnly)){if(error)*error=f.errorString();return false;}
    const QByteArray data=("[Desktop Entry]\nType=Application\nName=Kylin-LQUI AI小站\nComment=保持界面常驻，模型后台按需连接\nExec=\""+launcher+"\" --resident\nIcon=liquid-cli\nTerminal=false\nOnlyShowIn=UKUI;\nX-GNOME-Autostart-enabled=true\nX-GNOME-Autostart-Delay=1\n").toUtf8();
    if(f.write(data)!=data.size()||!f.commit()){if(error)*error=f.errorString();return false;}return true;
}
}
