#pragma once
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

// One look for every taskbar popup: start menu, quick settings, calendar and
// AI station. Python twin: apps/kylin-lqui-settings/popup_style.py (keep the
// two in sync). There are no per-popup skins; the global effects tier
// (shared/effects) decides glass vs. the flat fallback and the motion scale.
namespace PopupStyle {
constexpr qreal Radius=24.;          // outer corner radius of every popup
constexpr int Gap=8;                 // distance to the taskbar / anchor
// Motion: an in-place fade. Moving or reshaping X11 windows per frame stalls
// the compositor on the FTG340, so no popup slides or rises.
constexpr int OpenMs=150,CloseMs=120;
// Thin light edge drawn over the glass (and over the flat fallback).
inline QColor edgeColor(){return QColor(255,255,255,64);}
constexpr qreal EdgeWidth=1.;
// Lite / off tier, or no capture available: same shape, flat body.
inline QColor fallbackFill(){return QColor(23,33,44,242);}
// Text on the (always dark-toned) popup surface.
inline QColor text(){return QColor(QStringLiteral("#eef3fa"));}
inline QColor muted(){return QColor(QStringLiteral("#b9c9d8"));}
inline QColor accent(){return QColor(QStringLiteral("#a0e7ce"));}

inline QPainterPath shape(const QRectF &rect,qreal radius=Radius){
    QPainterPath path;path.addRoundedRect(rect,radius,radius);return path;
}
inline void paintEdge(QPainter &p,const QRectF &rect,qreal radius=Radius){
    p.save();p.setRenderHint(QPainter::Antialiasing);p.setBrush(Qt::NoBrush);
    p.setPen(QPen(edgeColor(),EdgeWidth));
    const qreal inset=EdgeWidth/2;
    p.drawRoundedRect(rect.adjusted(inset,inset,-inset,-inset),radius-inset,radius-inset);
    p.restore();
}
inline void paintFallback(QPainter &p,const QRectF &rect,qreal radius=Radius){
    p.save();p.setRenderHint(QPainter::Antialiasing);
    p.fillPath(shape(rect,radius),fallbackFill());p.restore();
    paintEdge(p,rect,radius);
}

// Brightness the GPU popup glass gives a backdrop of a given brightness,
// measured on the FTG340 (dark desktop 8->4, mid grey 128->64, white 250->81).
inline qreal glassBrightness(qreal lum){
    static const qreal xs[]={0,8,128,250,255},ys[]={2,4,64,81,82};
    for(int i=0;i<4;++i)if(lum<=xs[i+1])return ys[i]+(ys[i+1]-ys[i])*(lum-xs[i])/qMax<qreal>(xs[i+1]-xs[i],1);
    return ys[4];
}
// Instant glass-like surface from the captured backdrop (CPU blur + the
// curve above, ~15 ms): shown while the real glass renders, so opening has
// the right brightness from the first frame and the glass fades in calmly.
inline QImage standIn(const QImage &capture,const QSize &physical,qreal dpr,qreal radius=Radius){
    if(capture.isNull()||physical.isEmpty())return {};
    QImage small=capture.scaled(qMax(1,physical.width()/12),qMax(1,physical.height()/12),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    for(int y=0;y<small.height();++y){
        auto *line=reinterpret_cast<QRgb*>(small.scanLine(y));
        for(int x=0;x<small.width();++x){
            const QRgb c=line[x];const qreal lum=.2126*qRed(c)+.7152*qGreen(c)+.0722*qBlue(c);
            const qreal scale=glassBrightness(lum)/qMax<qreal>(lum,1);
            line[x]=qRgb(qMin(255,qRound(qRed(c)*scale)),qMin(255,qRound(qGreen(c)*scale)),qMin(255,qRound(qBlue(c)*scale)));
        }
    }
    const QImage blurred=small.scaled(physical,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    QImage out(physical,QImage::Format_ARGB32_Premultiplied);out.fill(Qt::transparent);
    {QPainter p(&out);p.setRenderHint(QPainter::Antialiasing);p.setClipPath(shape(QRectF(QPointF(),QSizeF(physical)),radius*dpr));p.drawImage(0,0,blurred);}
    out.setDevicePixelRatio(dpr);return out;
}
// 8x8 brightness fingerprint of a backdrop; two captures with close
// fingerprints can share one rendered glass.
inline QByteArray backdropSignature(const QImage &capture){
    if(capture.isNull())return {};
    const QImage small=capture.scaled(8,8,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
    QByteArray out;for(int y=0;y<8;++y)out.append(reinterpret_cast<const char*>(small.constScanLine(y)),8);return out;
}
inline bool signatureClose(const QByteArray &a,const QByteArray &b,int limit=14){
    if(a.size()!=64||b.size()!=64)return false;
    int sum=0;for(int i=0;i<64;++i)sum+=qAbs(int(uchar(a[i]))-int(uchar(b[i])));
    return sum/64<=limit;
}
}
