#include "LiquidMaterial.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <sys/resource.h>

// Frozen pre-optimization implementation: independent pixel-by-pixel oracle.
static QImage reference(const QImage &source, QSize logical, int radius, int divisor)
{
    QImage a = source.scaled(qMax(1, logical.width()/divisor), qMax(1, logical.height()/divisor),
                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGB32);
    QImage b(a.size(), a.format());
    for (int pass=0; pass<6; ++pass) {
        const bool horizontal=pass%2==0;
        for (int y=0; y<a.height(); ++y) {
            auto *out=reinterpret_cast<QRgb *>(b.scanLine(y));
            for (int x=0; x<a.width(); ++x) {
                int r=0, g=0, blue=0;
                for (int k=-radius; k<=radius; ++k) {
                    const int sx=horizontal?qBound(0,x+k,a.width()-1):x;
                    const int sy=horizontal?y:qBound(0,y+k,a.height()-1);
                    const QRgb c=reinterpret_cast<const QRgb *>(a.constScanLine(sy))[sx];
                    r+=qRed(c); g+=qGreen(c); blue+=qBlue(c);
                }
                out[x]=qRgb(r/(radius*2+1),g/(radius*2+1),blue/(radius*2+1));
            }
        }
        a.swap(b);
    }
    return a.scaled(source.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}

static uint32_t randomState=0x173ab92;
static uint32_t nextRandom() {
    randomState^=randomState<<13; randomState^=randomState>>17; randomState^=randomState<<5;
    return randomState;
}
static QImage image(QSize size, int pattern, QImage::Format format=QImage::Format_RGB32, qreal dpr=1)
{
    QImage source(size,QImage::Format_ARGB32);
    for (int y=0; y<size.height(); ++y) {
        auto *out=reinterpret_cast<QRgb *>(source.scanLine(y));
        for (int x=0; x<size.width(); ++x) {
            switch (pattern) {
            case 0: out[x]=QRgb(nextRandom()); break;
            case 1: out[x]=qRgba(113,157,229,91); break;
            case 2: out[x]=(x+y)%2?qRgba(255,0,93,255):qRgba(0,231,255,0); break;
            case 3: out[x]=(x==0 || y==0 || x+1==size.width() || y+1==size.height())
                    ?qRgba(255,17,0,255):qRgba(0,0,255,63); break;
            default: out[x]=(x==size.width()/2 && y==size.height()/2)?qRgba(255,255,255,255):0; break;
            }
        }
    }
    source=source.convertToFormat(format); source.setDevicePixelRatio(dpr); return source;
}
static int cases=0;
static bool check(const QImage &source, QSize logical, int radius, int divisor)
{
    const qint64 key=source.cacheKey();
    const QImage expected=reference(source,logical,radius,divisor);
    const QImage actual=LiquidMaterial::diffuse(source,logical,radius,divisor);
    ++cases;
    if (actual==expected && actual.format()==expected.format()
        && actual.devicePixelRatio()==expected.devicePixelRatio() && source.cacheKey()==key) return true;
    fprintf(stderr,"Mismatch case %d: source %dx%d format %d dpr %.2f, logical %dx%d, radius %d divisor %d\n",
        cases,source.width(),source.height(),int(source.format()),source.devicePixelRatio(),
        logical.width(),logical.height(),radius,divisor);
    return false;
}
static double cpuSeconds() {
    rusage usage{}; getrusage(RUSAGE_SELF,&usage);
    return usage.ru_utime.tv_sec+usage.ru_utime.tv_usec/1e6+usage.ru_stime.tv_sec+usage.ru_stime.tv_usec/1e6;
}
static bool benchmark(const QString &path)
{
    const QImage source=image({1003,124},0);
    const QSize logical(669,83);
    using Diffuse=QImage (*)(const QImage &,QSize,int,int);
    volatile QRgb checksum=0;
    auto measure=[&](Diffuse function,int repeats) {
        const double start=cpuSeconds();
        for (int i=0; i<repeats; ++i) {
            const QImage body=function(source,logical,2,3);
            const QImage rim=function(source,logical,1,2);
            checksum^=body.pixel(i%body.width(),0)^rim.pixel(0,i%rim.height());
        }
        return (cpuSeconds()-start)*1000/repeats;
    };
    measure(reference,3); measure(LiquidMaterial::diffuse,3);
    QJsonArray before,after;
    QVector<double> oldTimes,newTimes;
    for (int round=0; round<7; ++round) {
        double oldTime,newTime;
        if (round%2==0) { oldTime=measure(reference,24); newTime=measure(LiquidMaterial::diffuse,24); }
        else { newTime=measure(LiquidMaterial::diffuse,24); oldTime=measure(reference,24); }
        before.append(oldTime); after.append(newTime); oldTimes.append(oldTime); newTimes.append(newTime);
    }
    std::sort(oldTimes.begin(),oldTimes.end()); std::sort(newTimes.begin(),newTimes.end());
    const double oldMedian=oldTimes[3],newMedian=newTimes[3];
    const QJsonObject result{{"equivalence_cases",cases},{"pixel_differences",0},
        {"source_width",source.width()},{"source_height",source.height()},
        {"logical_width",logical.width()},{"logical_height",logical.height()},
        {"repeats_per_round",24},{"rounds",7},{"before_cpu_ms_per_body_and_rim",before},
        {"after_cpu_ms_per_body_and_rim",after},{"before_median_cpu_ms",oldMedian},
        {"after_median_cpu_ms",newMedian},{"cpu_reduction_percent",(1-newMedian/oldMedian)*100}};
    QFile output(path); if (!output.open(QIODevice::WriteOnly)) return false;
    output.write(QJsonDocument(result).toJson());
    fprintf(stdout,"Diffusion body+rim CPU median: %.3f -> %.3f ms (%.1f%% less)\n",
        oldMedian,newMedian,(1-newMedian/oldMedian)*100);
    return true;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    // Exhaust narrow dimensions, including kernels larger than the image.
    for (int w=1; w<=17; ++w) for (int h=1; h<=17; ++h) {
        const QImage source=image({w,h},0);
        for (int radius : {0,1,2,4,8})
            if (!check(source,{w,h},radius,1)) return 1;
        if (!check(source,{w,h},2,3) || !check(source,{w,h},1,2)) return 1;
    }
    const QImage::Format formats[]{QImage::Format_RGB32,QImage::Format_ARGB32,
        QImage::Format_ARGB32_Premultiplied,QImage::Format_RGBA8888,QImage::Format_RGB888,
        QImage::Format_Grayscale8};
    for (auto format : formats) for (int pattern=0; pattern<5; ++pattern) {
        for (qreal dpr : {1.0,1.5,2.0,3.0}) {
            const QImage source=image({73,29},pattern,format,dpr);
            for (QSize logical : {QSize(1,1),QSize(0,0),QSize(73,29),QSize(49,19),QSize(111,47)})
                if (!check(source,logical,2,3) || !check(source,logical,1,2)) return 1;
        }
    }
    for (int i=0; i<300; ++i) {
        const QSize size(1+nextRandom()%181,1+nextRandom()%123);
        const QSize logical(1+nextRandom()%221,1+nextRandom()%161);
        const int radius=nextRandom()%9,divisor=1+nextRandom()%4;
        const QImage source=image(size,i%5,formats[i%6],1+(i%5)*0.5);
        if (!check(source,logical,radius,divisor)) return 1;
    }
    for (QSize size : {QSize(1003,124),QSize(2560,160),QSize(161,997)}) {
        const QImage source=image(size,0);
        if (!check(source,size,2,3) || !check(source,size/2,1,2)) return 1;
    }
    fprintf(stdout,"PASS: %d cases, exact pixel/format/DPR equality, source unchanged\n",cases);
    if (argc==3 && QString::fromLocal8Bit(argv[1])=="--benchmark")
        return benchmark(QString::fromLocal8Bit(argv[2]))?0:2;
    return 0;
}
