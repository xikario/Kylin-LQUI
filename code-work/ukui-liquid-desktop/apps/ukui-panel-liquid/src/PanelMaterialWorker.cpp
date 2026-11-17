#include "LiquidOpticsRenderer.h"
#include <QApplication>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLibrary>
#include <cmath>

int main(int argc,char **argv){
    QApplication app(argc,argv);
    QFile input;if(!input.open(0,QIODevice::ReadOnly))return 1;
    QByteArray bytes;
    for(;;){const auto chunk=input.read(65536);if(chunk.isEmpty())break;bytes+=chunk;if(bytes.size()>32*1024*1024)return 2;}
    QDataStream reader(bytes);reader.setVersion(QDataStream::Qt_5_12);
    quint32 magic=0;QByteArray settings;double dpr=1;QImage source;
    reader>>magic>>settings>>dpr>>source;
    if(reader.status()!=QDataStream::Ok||magic!=0x4c514d31||source.isNull()||!std::isfinite(dpr)||dpr<=0||dpr>8)return 3;
    source.setDevicePixelRatio(dpr);
    const auto options=QJsonDocument::fromJson(settings).object();
    QImage image;bool gpu=false;
    if(options.value("nativeBridge").toBool()) {
        QLibrary library(options.value("bridgeLibrary").toString());
        library.setLoadHints(QLibrary::PreventUnloadHint);
        using Create=void *(*)(const void *,int,int,int,double,int,int,int,int,int,int,int,int);
        using Render=int (*)(void *,int,int,int,int,double,double,void *,int,int,int);
        using Destroy=void (*)(void *);using Adaptive=void (*)(void *,int);using Used=int (*)(void *);
        const auto create=reinterpret_cast<Create>(library.resolve("lqui_glass_create_region"));
        const auto render=reinterpret_cast<Render>(library.resolve("lqui_glass_render"));
        const auto destroy=reinterpret_cast<Destroy>(library.resolve("lqui_glass_destroy"));
        const auto adaptive=reinterpret_cast<Adaptive>(library.resolve("lqui_glass_set_adaptive_smoke"));
        const auto used=reinterpret_cast<Used>(library.resolve("lqui_glass_used_gpu"));
        if(!create||!render||!destroy)return 6;
        const auto screen=options["screen"].toArray(),region=options["region"].toArray();if(screen.size()!=4||region.size()!=4)return 7;
        source=source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        void *context=create(source.constBits(),source.width(),source.height(),source.bytesPerLine(),dpr,
            screen[0].toInt(),screen[1].toInt(),screen[2].toInt(),screen[3].toInt(),region[0].toInt(),region[1].toInt(),region[2].toInt(),region[3].toInt());
        if(!context)return 8;
        if(adaptive)adaptive(context,1);
        image=QImage(source.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        const int ok=render(context,region[0].toInt(),region[1].toInt(),region[2].toInt(),region[3].toInt(),dpr,options["radius"].toDouble(),image.bits(),image.width(),image.height(),image.bytesPerLine());
        gpu=used&&used(context);destroy(context);if(!ok)return 9;
    } else {
    LiquidOpticsRenderer renderer;
    const auto finish=options.value("finish").toString();
    renderer.setFinish(finish=="soft"?LiquidOpticsRenderer::Finish::Soft:finish=="frosted"?LiquidOpticsRenderer::Finish::Frosted:LiquidOpticsRenderer::Finish::Liquid);
    renderer.setOptics(options["refraction"].toDouble(),options["opacity"].toDouble(),options["highlight"].toDouble(),options["chroma"].toDouble());
    renderer.setMaterial(options["clarity"].toDouble(),options["strength"].toDouble());
    renderer.setWallpaper(source);
    image=renderer.renderPanel(QRect(QPoint(),QSize(qRound(source.width()/dpr),qRound(source.height()/dpr))),options["radius"].toDouble());
    gpu=renderer.usedGpu();
    }
    if(image.isNull())return 4;
    QByteArray payload;QDataStream writer(&payload,QIODevice::WriteOnly);writer.setVersion(QDataStream::Qt_5_12);
    writer<<quint32(0x4c514d31)<<gpu<<image;
    QFile output;if(!output.open(1,QIODevice::WriteOnly)||output.write(payload)!=payload.size()||!output.flush())return 5;
    return 0;
}
