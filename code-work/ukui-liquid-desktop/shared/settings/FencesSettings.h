#pragma once
#include <QSettings>
#include <QStandardPaths>
#include <QString>
// The OEM theme may change Qt's default to IniFormat. Migrate .ini once and
// preserve newer .conf keys before choosing the explicit NativeFormat contract.
inline bool initializeFencesSettings(QString *error=nullptr) {
    QSettings target(QSettings::NativeFormat,QSettings::UserScope,"kylin","ukui-fences");
    const QString marker=QStringLiteral("migration/legacyIniImported");
    if(!target.value(marker,false).toBool()) {
        QSettings previous(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/kylin/ukui-fences.ini",QSettings::IniFormat);
        const auto keys=previous.allKeys();
        if(previous.status()!=QSettings::NoError||target.status()!=QSettings::NoError){if(error)*error=QStringLiteral("无法读取旧配置，保留原配置格式");return false;}
        for(const auto &key:keys)if(!target.contains(key))target.setValue(key,previous.value(key));
        target.setValue(marker,true);target.sync();
        if(target.status()!=QSettings::NoError){if(error)*error=QStringLiteral("无法保存迁移配置，保留原配置格式");return false;}
    }
    QSettings::setDefaultFormat(QSettings::NativeFormat);return true;
}
