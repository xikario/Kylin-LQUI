#pragma once
#include <QPainterPath>
#include <QRectF>

namespace TaskbarPetLayout {
// The original resting mark occupies 150 units of its 176-unit motion tile.
constexpr qreal tileSide=176, restSide=150, restTop=15;
inline QRectF canvas(const QRectF &button,Qt::Edge end){
    const qreal outer=qMin(8.,button.width()/6.),inner=2.;
    const bool left=end==Qt::LeftEdge;
    const QRectF inset=button.adjusted(left?outer:inner,1,left?-inner:-outer,-1);
    if(inset.isEmpty())return {};
    const qreal side=qMin(40.,qMin(inset.width(),inset.height()));
    QRectF target(0,0,side,side);target.moveCenter(inset.center());
    return target;
}
inline QRectF portrait(const QRectF &button){
    const QRectF tile=canvas(button,Qt::RightEdge);
    const qreal unit=tile.width()/tileSide,side=restSide*unit;
    return {tile.center().x()-side/2,tile.top()+restTop*unit,side,side};
}
inline QPainterPath bust(const QRectF &r){
    // Keep the horns and hair at the top, and round off the portrait's cut shoulders.
    const qreal x=r.left(),y=r.top(),w=r.width(),h=r.height();
    QPainterPath shape;shape.moveTo(r.topLeft());shape.lineTo(r.topRight());
    shape.lineTo(x+w,y+.55*h);
    shape.cubicTo(x+w,y+.84*h,x+.82*w,y+h,x+.5*w,y+h);
    shape.cubicTo(x+.18*w,y+h,x,y+.84*h,x,y+.55*h);
    shape.closeSubpath();return shape;
}
}
