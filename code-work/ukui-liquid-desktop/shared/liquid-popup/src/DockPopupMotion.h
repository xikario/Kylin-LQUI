#pragma once
#include <QWidget>
#include <QtMath>

// Move the already rendered window through its taskbar edge. Clip to the
// final popup rectangle so neither opening nor closing paints over the Dock.
// No resize, layout, blur, snapshot allocation or recurring timer per frame.
namespace DockPopupMotion {
constexpr int OpenMs=260,CloseMs=220;
inline void apply(QWidget *window,const QRect &target,int edge,qreal progress) {
    const qreal rest=1-qBound(0.,progress,1.);
    const QPoint offset=edge==0?QPoint(-qRound(target.width()*rest),0)
        :edge==1?QPoint(qRound(target.width()*rest),0)
        :edge==2?QPoint(0,-qRound(target.height()*rest))
        :QPoint(0,qRound(target.height()*rest));
    window->move(target.topLeft()+offset);
    if(progress>=1)window->clearMask();
    else {
        const QRect clip=target.translated(-window->pos()).intersected(window->rect());
        window->setMask(QRegion(clip.isEmpty()?QRect(-2,-2,1,1):clip));
    }
    window->setWindowOpacity(qBound(0.,progress*4,1.));
}
}
