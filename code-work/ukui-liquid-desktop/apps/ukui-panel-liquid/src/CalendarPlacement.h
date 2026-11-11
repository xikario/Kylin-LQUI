#pragma once
#include <QApplication>
#include <QScreen>
#include <QWidget>

inline QRect calendarDockTarget(const QSize &size) {
    QWidget *panel=nullptr;
    for(auto *w:QApplication::topLevelWidgets())if(w->inherits("UKUIPanel")&&w->isVisible()){panel=w;break;}
    if(!panel||!size.isValid())return {};
    auto *surface=panel->findChild<QWidget*>("BackgroundWidget");if(!surface)surface=panel;
    const QRect anchor(surface->mapToGlobal(QPoint()),surface->size());auto *screen=QGuiApplication::screenAt(anchor.center());if(!screen)return {};
    const QRect bounds=screen->availableGeometry().adjusted(6,6,-6,-6);
    return QRect(QPoint(qBound(bounds.left(),anchor.right()-size.width()+1,qMax(bounds.left(),bounds.right()-size.width()+1)),qMax(bounds.top(),anchor.top()-size.height()-8)),size);
}
