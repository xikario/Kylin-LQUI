#pragma once
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmapCache>
#include <QPainterPath>
#include "IconPolicy.h"
#include <QtMath>
#include <cmath>

// Shared automatic adaptation for application artwork. Source assets stay intact.
// Classification/resampling only happens on a cache miss, never on an animation tick.
namespace LquiIcon {
inline QPixmap render(const QIcon &icon,int pixels,bool unified,QIcon::Mode mode=QIcon::Normal,QIcon::State state=QIcon::Off,Theme theme=Theme::Color) {
    pixels=qBound(8,pixels,512);
    const QString key=QString("lqui-optical-v3:%1:%2:%3:%4:%5:%6").arg(icon.cacheKey()).arg(pixels).arg(int(mode)).arg(int(state)).arg(unified).arg(int(theme));
    QPixmap result;if(QPixmapCache::find(key,&result))return result;
    const int sample=qBound(96,pixels*2,1024);
    const QImage source=icon.pixmap(QSize(sample,sample),mode,state).toImage().convertToFormat(QImage::Format_ARGB32);
    if(source.isNull())return {};
    int left=source.width(),top=source.height(),right=-1,bottom=-1;double ink=0,luma=0;
    for(int y=0;y<source.height();++y) {
        const auto *row=reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for(int x=0;x<source.width();++x)if(qAlpha(row[x])>=32) {
            left=qMin(left,x);right=qMax(right,x);top=qMin(top,y);bottom=qMax(bottom,y);
            const double a=qAlpha(row[x])/255.;ink+=a;luma+=a*(.2126*qRed(row[x])+.7152*qGreen(row[x])+.0722*qBlue(row[x]));
        }
    }
    if(right<left||bottom<top)return {};
    const QRect core(QPoint(left,top),QPoint(right,bottom));
    const double occupancy=ink/qMax(1,core.width()*core.height());
    const double balance=qBound(.92,std::sqrt(.80/qMax(.10,occupancy)),1.03);
    // Opaque square artwork already has a designed tile. Circles, narrow logos
    // and freeform artwork get a neutral adaptive canvas instead. Corner probes
    // distinguish an opaque round icon from a rounded square without app lists.
    const double aspect=double(qMin(core.width(),core.height()))/qMax(core.width(),core.height());
    int corners=0;
    for(double y:{.1,.9})for(double x:{.1,.9})
        if(qAlpha(source.pixel(core.left()+qRound((core.width()-1)*x),core.top()+qRound((core.height()-1)*y)))>160)++corners;
    const bool tiled=aspect>.98 && occupancy>.86 && corners==4;
    const double side=pixels*.92*(unified?(tiled?1.:.76*balance):balance);
    const QSizeF extent=QSizeF(core.size()).scaled(QSizeF(side,side),Qt::KeepAspectRatio);
    const QRectF target(QPointF((pixels-extent.width())/2,(pixels-extent.height())/2),extent);
    result=QPixmap(pixels,pixels);result.fill(Qt::transparent);
    QPainter painter(&result);painter.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
    if(unified) {
        const QRectF tile(pixels*.04,pixels*.04,pixels*.92,pixels*.92);
        QPainterPath silhouette;silhouette.addRoundedRect(tile,tile.width()*.23,tile.height()*.23);
        painter.setClipPath(silhouette);
        if(tiled) {
            // A themed pixmap may have a fractional transparent fringe. Extend
            // its own edge colour underneath so the canvas stays identical at
            // fractional DPRs, regardless of the source asset's outer padding.
            QColor edge=source.pixelColor(core.center().x(),core.top()+qRound(core.height()*.06));edge.setAlpha(255);
            painter.fillPath(silhouette,edge);
        } else {
            // Keep white logos readable as well as dark or coloured artwork.
            const bool light=luma/qMax(1.,ink)>205;
            QLinearGradient background(tile.topLeft(),tile.bottomRight());
            const auto palette=colors(theme);
            background.setColorAt(0,light?palette.darkTop:palette.lightTop);
            background.setColorAt(1,light?palette.darkBottom:palette.lightBottom);
            if(mode==QIcon::Disabled)painter.setOpacity(.55);
            painter.fillPath(silhouette,background);painter.setOpacity(1.);
        }
    }
    painter.drawImage(target,source,core);
    if(unified&&theme!=Theme::Color) {
        const QRectF tile(pixels*.04,pixels*.04,pixels*.92,pixels*.92);
        if(theme==Theme::Mist)painter.fillRect(tile,QColor(221,225,225,32));
        else {
            QLinearGradient reflection(tile.topLeft(),tile.bottomLeft());
            reflection.setColorAt(0,QColor(255,255,255,54));reflection.setColorAt(.46,QColor(255,255,255,0));reflection.setColorAt(1,QColor(70,115,155,20));
            painter.fillRect(tile,reflection);painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor(235,250,255,100),qMax(.7,pixels/90.)));
            painter.drawRoundedRect(tile.adjusted(.6,.6,-.6,-.6),tile.width()*.23,tile.height()*.23);
        }
    }
    painter.end();
    QPixmapCache::insert(key,result);return result;
}
inline QPixmap raster(const QIcon &icon,int pixels,QIcon::Mode mode=QIcon::Normal,QIcon::State state=QIcon::Off) {
    return render(icon,pixels,uniform(),mode,state,theme());
}
}
