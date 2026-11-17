#pragma once
#include <QRect>
#include <QList>
#include <QWidget>
#include <QVariant>
#include <limits>
#include <algorithm>

namespace DesktopScreenLayout {
inline QRect nearestArea(const QList<QRect> &areas,const QPoint &point) {
    QRect best;qint64 distance=std::numeric_limits<qint64>::max();
    for(const auto &area:areas){
        if(area.contains(point))return area;
        const qint64 dx=point.x()-qBound(area.left(),point.x(),area.right());
        const qint64 dy=point.y()-qBound(area.top(),point.y(),area.bottom());
        if(dx*dx+dy*dy<distance){distance=dx*dx+dy*dy;best=area;}
    }
    return best;
}
inline QRect preferred(QWidget *widget) {
    const auto applied=widget->property("lquiScreenApplied").toRect();
    const auto original=widget->property("lquiScreenPreferred").toRect();
    return original.isValid()&&applied==widget->geometry()?original:widget->geometry();
}
inline void apply(QWidget *widget,const QRect &original,const QRect &target) {
    widget->setProperty("lquiScreenPreferred",original!=target?QVariant(original):QVariant());
    widget->setProperty("lquiScreenApplied",original!=target?QVariant(target):QVariant());
    if(widget->geometry()!=target)widget->setGeometry(target);
}
inline QRect fit(const QRect &preferred,const QRect &area,const QList<QRect> &occupied={}) {
    if(!area.isValid())return preferred;
    const QSize size(qMin(preferred.width(),area.width()),qMin(preferred.height(),area.height()));
    const int right=area.right()-size.width()+1,bottom=area.bottom()-size.height()+1;
    auto clamp=[&](QPoint point){return QPoint(qBound(area.left(),point.x(),right),qBound(area.top(),point.y(),bottom));};
    const auto bounded=clamp(preferred.topLeft());
    QList<int> xs{bounded.x(),area.left(),right},ys{bounded.y(),area.top(),bottom};
    for(const auto &rect:occupied){xs<<rect.left()-size.width()-8<<rect.right()+9;ys<<rect.top()-size.height()-8<<rect.bottom()+9;}
    QRect best(bounded,size);qint64 bestScore=std::numeric_limits<qint64>::max();
    for(int x:xs)for(int y:ys){
        const QRect candidate(clamp({x,y}),size);qint64 overlap=0;
        for(const auto &other:occupied){const auto intersection=candidate.intersected(other);overlap+=qint64(intersection.width())*intersection.height();}
        const auto delta=candidate.topLeft()-preferred.topLeft();
        const qint64 score=overlap*1000000000LL+qint64(delta.x())*delta.x()+qint64(delta.y())*delta.y();
        if(score<bestScore){bestScore=score;best=candidate;}
    }
    return best;
}
}
