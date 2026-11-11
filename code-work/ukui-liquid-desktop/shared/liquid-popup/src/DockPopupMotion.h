#pragma once
#include <QWidget>
#include <QtMath>

// Fade the cached surface at its final position. Repeated X11 move/shape
// updates stall the compositor on the target GPU, even without repainting.
namespace DockPopupMotion {
constexpr int OpenMs=150,CloseMs=120;
inline void apply(QWidget *window,const QRect &target,int edge,qreal progress) {
    Q_UNUSED(edge);
    // Tolerate one logical pixel of fractional-DPI window-manager rounding.
    if((window->pos()-target.topLeft()).manhattanLength()>2)window->move(target.topLeft());
    if(!window->mask().isEmpty())window->clearMask();
    window->setWindowOpacity(qBound(0.,progress,1.));
}
}
