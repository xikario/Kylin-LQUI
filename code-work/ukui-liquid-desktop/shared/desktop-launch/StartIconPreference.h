#pragma once
#include <QSettings>
#include <QStandardPaths>

namespace StartIconPreference {
inline QString path() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + QStringLiteral("/ukui-kaishicaidan-v2/settings.conf");
}
inline bool pet(const QSettings &settings) {
    return settings.value(QStringLiteral("startButtonIcon")).toString() == QStringLiteral("pet");
}
inline void save(QSettings &settings, bool petIcon) {
    if (pet(settings) == petIcon) return;
    settings.setValue(QStringLiteral("startButtonIcon"), petIcon ? QStringLiteral("pet") : QStringLiteral("classic"));
    settings.sync();
}
}
