#pragma once
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace PetEntry {
inline QString path() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/kylin-lqui/ai-entry.ini";
}
inline QList<QPair<QString,QString>> characters() {
    return {{"克劳德大小姐","claude"},{"大肥鱼","fish"},{"GPT 白龙","gpt"},{"Gemini 星空猫","gemini"}};
}
inline QString label(const QString &id) {
    for(const auto &item:characters()) if(item.second==id) return item.first;
    return {};
}
inline QString icon(const QString &id) {
    if(label(id).isEmpty())return {};
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation,"kylin-ai-pets/"+id+"/avatar.png");
}
}
