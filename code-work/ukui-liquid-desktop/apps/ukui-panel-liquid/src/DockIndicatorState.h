#pragma once
#include <QVector>
#include <QtGlobal>

namespace DockIndicator {
struct State { int windows=0; bool active=false; };
inline State state(const QVector<qulonglong> &ids,qulonglong activeWindow) {
    State result;
    for(const auto id:ids)if(id) {
        ++result.windows;
        if(id==activeWindow)result.active=true;
    }
    return result;
}
}
