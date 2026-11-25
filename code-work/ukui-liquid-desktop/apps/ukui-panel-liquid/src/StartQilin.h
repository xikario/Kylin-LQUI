#pragma once
#include <QColor>
#include <QPixmap>
#include <QPolygonF>
#include <QVector>
#include <array>

class StartQilin {
public:
    enum Move { Rest, Roar, Tail, Leap, Bow, Listen, Stamp, Stretch, Trot, Blink,
                CloudRide, MistStep, Star, Ripple, Flame, Rain, Ball,
                Flip, RibbonSpin, DoubleBounce, Kickflip, Count };
    struct Timing {
        int frames, ms;
        int holdMs=0;
        int delayAfter(int frame) const { return ms + (frame==frames/2 ? holdMs : 0); }
    };
    static Timing timing(Move move);
    static QRectF targetRect(const QRectF &button);
    StartQilin();
    const QPolygonF &silhouette() const { return outline; }
    // A lifted frame adds one transparent tile above the button tile.
    QPixmap render(Move move, qreal time, qreal extraLift=0) const;
private:
    struct Facet { QPolygonF points; QColor color; };
    QPolygonF outline;
    QVector<Facet> facets;
    QPixmap fallback;
};
