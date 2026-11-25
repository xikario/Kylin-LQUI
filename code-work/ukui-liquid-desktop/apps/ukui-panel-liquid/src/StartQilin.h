#pragma once
#include <QColor>
#include <QPixmap>
#include <QPolygonF>
#include <QVector>
#include <array>

class StartQilin {
public:
    enum Move { Rest, Roar, Tail, Leap, Bow, Listen, Stamp, Stretch, Trot, Blink,
                CloudRide, MistStep, Star, Ripple, Flame, Count };
    struct Timing { int frames, ms; };
    static Timing timing(Move move);
    StartQilin();
    const QPolygonF &silhouette() const { return outline; }
    QPixmap render(Move move, qreal time) const;
private:
    struct Facet { QPolygonF points; QColor color; };
    QPolygonF outline;
    QVector<Facet> facets;
    QPixmap fallback;
};
