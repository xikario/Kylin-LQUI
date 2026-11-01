#include "LiquidDiffusion.h"
#include <QVector>

namespace {
struct RgbSum {
    int red=0, green=0, blue=0;

    void add(QRgb pixel) {
        red += qRed(pixel); green += qGreen(pixel); blue += qBlue(pixel);
    }
    void remove(QRgb pixel) {
        red -= qRed(pixel); green -= qGreen(pixel); blue -= qBlue(pixel);
    }
    QRgb average(int count) const {
        return qRgb(red/count, green/count, blue/count);
    }
};
}

QImage LiquidOpticsDetail::diffuse(const QImage &source, QSize logical, int radius, int divisor)
{
    Q_ASSERT(radius >= 0 && divisor > 0);
    // Keep both Qt scaling operations and the RGB conversion unchanged.
    QImage a = source.scaled(qMax(1, logical.width()/divisor), qMax(1, logical.height()/divisor),
                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGB32);
    if (a.isNull()) return a;
    QImage b(a.size(), a.format());
    const int width=a.width(), height=a.height(), count=radius*2+1;
    QVector<RgbSum> columns(width);
    // Preserve six alternating passes, replicated edges and integer division
    // after every pass. Sliding sums only remove redundant neighbourhood reads.
    for (int pass=0; pass<6; ++pass) {
        if (pass % 2 == 0) {
            for (int y=0; y<height; ++y) {
                const auto *in=reinterpret_cast<const QRgb *>(a.constScanLine(y));
                auto *out=reinterpret_cast<QRgb *>(b.scanLine(y));
                RgbSum sum;
                for (int k=-radius; k<=radius; ++k)
                    sum.add(in[qBound(0,k,width-1)]);
                out[0]=sum.average(count);
                for (int x=1; x<width; ++x) {
                    sum.remove(in[qBound(0,x-radius-1,width-1)]);
                    sum.add(in[qBound(0,x+radius,width-1)]);
                    out[x]=sum.average(count);
                }
            }
        } else {
            columns.fill(RgbSum{});
            for (int k=-radius; k<=radius; ++k) {
                const auto *in=reinterpret_cast<const QRgb *>(a.constScanLine(qBound(0,k,height-1)));
                for (int x=0; x<width; ++x) columns[x].add(in[x]);
            }
            // Process complete rows for contiguous access instead of repeatedly
            // fetching separate scan lines for every pixel in a column.
            auto *sums=columns.data();
            for (int y=0; y<height; ++y) {
                auto *out=reinterpret_cast<QRgb *>(b.scanLine(y));
                for (int x=0; x<width; ++x) out[x]=sums[x].average(count);
                if (y+1 == height) break;
                const auto *leaving=reinterpret_cast<const QRgb *>(a.constScanLine(qBound(0,y-radius,height-1)));
                const auto *entering=reinterpret_cast<const QRgb *>(a.constScanLine(qBound(0,y+radius+1,height-1)));
                for (int x=0; x<width; ++x) {
                    sums[x].remove(leaving[x]);
                    sums[x].add(entering[x]);
                }
            }
        }
        a.swap(b);
    }
    return a.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
