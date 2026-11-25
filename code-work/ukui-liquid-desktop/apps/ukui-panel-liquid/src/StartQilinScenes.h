#pragma once
#include "StartQilin.h"
#include <QTransform>

class QPainter;
namespace StartQilinScenes {
inline qreal ramp(qreal value,qreal lo,qreal hi) {
    const qreal t=qBound<qreal>(0,(value-lo)/(hi-lo),1);
    return t*t*(3-2*t);
}
QTransform bodyTransform(StartQilin::Move move,qreal t);
qreal flightLift(StartQilin::Move move,qreal t);
void paintBehind(QPainter &p,StartQilin::Move move,qreal t,qreal extraLift);
void paintInFront(QPainter &p,StartQilin::Move move,qreal t,QPointF snout,QPointF horn);
}
