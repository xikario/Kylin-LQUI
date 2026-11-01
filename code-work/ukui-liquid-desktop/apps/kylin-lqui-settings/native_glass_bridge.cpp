// Narrow C ABI for the PyQt control center. Same CPU optics as the launcher;
// no Qt objects cross the Python boundary and no GPU render loop is created.
#include "NextKdeGlassView.h"
#include "WallpaperBackdrop.h"
#include <QApplication>
#include <QThread>
#include <cstring>
#include <memory>
#include <cmath>
struct MaterialContext { WallpaperBackdrop wallpaper; QImage fallback; QRect screen; };
static bool guiThread(){return qApp&&QThread::currentThread()==qApp->thread();}
extern "C" {
void *lqui_glass_create(const unsigned char *pixels,int width,int height,int stride,double dpr,int sx,int sy,int sw,int sh){
    if(!guiThread()||!pixels||width<1||height<1||width>16384||height>16384||stride<width*4||!std::isfinite(dpr)||dpr<.5||dpr>4)return nullptr;
    try{
        auto context=std::make_unique<MaterialContext>();context->screen=QRect(sx,sy,sw,sh);
        context->fallback=QImage(pixels,width,height,stride,QImage::Format_ARGB32_Premultiplied).copy();context->fallback.setDevicePixelRatio(dpr);
        context->wallpaper.reload();return context.release();
    }catch(...){return nullptr;}
}
int lqui_glass_render(void *handle,int x,int y,int width,int height,double dpr,double radius,unsigned char *out,int outWidth,int outHeight,int stride){
    if(!guiThread()||!handle||!out||width<1||height<1||width>4096||height>4096||!std::isfinite(dpr)||!std::isfinite(radius)||dpr<.5||dpr>4||outWidth<1||outHeight<1||stride<outWidth*4)return 0;
    const QSize expected(qRound(width*dpr),qRound(height*dpr));if(expected!=QSize(outWidth,outHeight))return 0;
    try{
        auto *context=static_cast<MaterialContext*>(handle);const QRect rect(x,y,width,height);
        QImage source=context->wallpaper.sample(rect,context->screen,dpr);
        if(source.isNull()){
            const auto offset=rect.topLeft()-context->screen.topLeft();const qreal fd=context->fallback.devicePixelRatio();
            source=context->fallback.copy(QRect(qRound(offset.x()*fd),qRound(offset.y()*fd),qRound(width*fd),qRound(height*fd))).scaled(expected);
            source.setDevicePixelRatio(dpr);
        }
        NextKdeGlassView glass;glass.setRadius(radius);glass.setBackdropFast(source);
        QImage image=glass.image().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if(image.isNull()||image.size()!=expected)return 0;
        // Keep the common white text legible even on snow-white wallpaper.
        // Tone mapping preserves hue/detail, instead of an opaque colour plate.
        for(int row=0;row<image.height();++row){auto *line=reinterpret_cast<QRgb*>(image.scanLine(row));
            for(int col=0;col<image.width();++col){const QRgb c=line[col];const int peak=qMax(qRed(c),qMax(qGreen(c),qBlue(c)));const int ceiling=qRound(100*qAlpha(c)/255.);
                if(peak>ceiling&&peak>0)line[col]=qRgba(qRed(c)*ceiling/peak,qGreen(c)*ceiling/peak,qBlue(c)*ceiling/peak,qAlpha(c));
            }
            std::memcpy(out+row*stride,image.constScanLine(row),outWidth*4);
        }
        return 1;
    }catch(...){return 0;}
}
void lqui_glass_destroy(void *handle){delete static_cast<MaterialContext*>(handle);}
}
