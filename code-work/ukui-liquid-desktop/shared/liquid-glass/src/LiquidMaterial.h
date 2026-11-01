#pragma once
#include <QImage>
#include <QSize>
#include <QPainter>
#include <QPainterPath>

namespace LiquidMaterial {
// A uniform reading veil in the body, smoothly removed at the optical rim.
// Unlike a per-pixel brightness cap this preserves colour and local detail;
// alpha coverage and the Snell highlights remain owned by the renderer.
inline QImage bodyTone(const QImage &source,qreal amount,const QPainterPath &outline,qreal rim=8)
{
    if(source.isNull() || amount<=0)return source;
    rim=qMin(rim,qMin(source.width(),source.height())/source.devicePixelRatio()*.08);
    QImage tone(source.size(),QImage::Format_ARGB32_Premultiplied);tone.setDevicePixelRatio(source.devicePixelRatio());tone.fill(Qt::transparent);
    {
        QPainter painter(&tone);painter.setRenderHint(QPainter::Antialiasing);painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillPath(outline,QColor(0,0,0,qRound(255*qBound(0.,amount,1.))));painter.setBrush(Qt::NoBrush);
        for(int step=12;step>=0;--step){const qreal t=step/12.;
            painter.setPen(QPen(QColor(0,0,0,qRound(255*amount*t*t*(3-2*t))),qMax(.5,2*rim*t)));
            painter.drawPath(outline);
        }
    }
    QImage image=source.convertToFormat(QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(source.devicePixelRatio());
    QPainter painter(&image);painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);painter.drawImage(QPoint(),tone);
    return image;
}
// GUI-independent, bounded-resolution six-pass diffusion. Shared by all
// optical adapters; integer rounding and edge extension match the old output.
inline QImage diffuse(const QImage &source, QSize logical, int radius=2, int divisor=3)
{
    if (source.isNull()) return {};
    QImage a = source.scaled(qMax(1, logical.width()/divisor), qMax(1, logical.height()/divisor),
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QImage b(a.size(), a.format());
    for (int pass = 0; pass < 6; ++pass) {
        const bool horizontal = pass % 2 == 0;
        const int lines = horizontal ? a.height() : a.width();
        const int length = horizontal ? a.width() : a.height();
        for (int line = 0; line < lines; ++line) {
            auto pixel = [&](int position) {
                position = qBound(0, position, length-1);
                return reinterpret_cast<const QRgb *>(a.constScanLine(horizontal ? line : position))[horizontal ? position : line];
            };
            int r=0, g=0, blue=0;
            for (int k=-radius; k<=radius; ++k) { const QRgb c=pixel(k); r+=qRed(c); g+=qGreen(c); blue+=qBlue(c); }
            for (int position=0; position<length; ++position) {
                reinterpret_cast<QRgb *>(b.scanLine(horizontal ? line : position))[horizontal ? position : line]
                    = qRgb(r/(radius*2+1), g/(radius*2+1), blue/(radius*2+1));
                const QRgb old=pixel(position-radius), next=pixel(position+radius+1);
                r+=qRed(next)-qRed(old); g+=qGreen(next)-qGreen(old); blue+=qBlue(next)-qBlue(old);
            }
        }
        a.swap(b);
    }
    return a.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
struct Prepared {
    QImage source, body, clear;
};
inline Prepared prepare(const QImage &source)
{
    Prepared material; material.source=source;
    if (source.isNull()) return material;
    const qreal dpr=source.devicePixelRatio();
    const QSize logical(qMax(1,qRound(source.width()/dpr)),qMax(1,qRound(source.height()/dpr)));
    material.body=diffuse(source,logical); material.clear=diffuse(source,logical,1,2);
    material.body.setDevicePixelRatio(1); material.clear.setDevicePixelRatio(1);
    return material;
}
}
