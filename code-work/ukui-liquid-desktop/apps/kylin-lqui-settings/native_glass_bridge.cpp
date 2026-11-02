// Narrow C ABI: background image preparation, cached one-shot Snell renders.
// No Qt objects cross Python's boundary and no continuous GPU loop is created.
#include "LiquidOpticsRenderer.h"
#include "LiquidMaterial.h"
#include "CohesiveGlass.h"
#include "WallpaperBackdrop.h"
#include <QApplication>
#include <QThread>
#include <QPointer>
#include <cstring>
#include <memory>
#include <cmath>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
struct MaterialContext {
    LiquidMaterial::Prepared material;QRect region;
    bool usedGpu=false,adaptiveSmoke=false;
};
// Keep one program/context warm for the process' 60-second reuse window.
// Closing releases the captured scene and all card textures; no render timer.
struct MaterialSession final:QObject {
    explicit MaterialSession(QObject *parent):QObject(parent){CohesiveGlass::configure(renderer);}
    LiquidOpticsRenderer renderer;qreal dpr=0;
};
static QPointer<MaterialSession> session;
static MaterialContext *activeContext=nullptr;
static bool guiThread(){return qApp&&QThread::currentThread()==qApp->thread();}
extern "C" {
void *lqui_glass_create_region(const unsigned char *pixels,int width,int height,int stride,double dpr,int sx,int sy,int sw,int sh,int rx,int ry,int rw,int rh){
    // QImage/QSettings preparation is safe in the background worker.
    if(!qApp||!pixels||width<1||height<1||width>16384||height>16384||stride<width*4||!std::isfinite(dpr)||dpr<.5||dpr>4)return nullptr;
    try{
        if(sw<1||sh<1||rw<1||rh<1||rw>4096||rh>4096)return nullptr;
        auto context=std::make_unique<MaterialContext>();context->region=QRect(rx,ry,rw,rh);
        // A popup captures before mapping. Use the actual windows/wallpaper
        // behind it, rather than replacing that scene with the wallpaper file.
        QImage backdrop;
        if(QSize(width,height)==QSize(qRound(rw*dpr),qRound(rh*dpr))) {
            backdrop=QImage(pixels,width,height,stride,QImage::Format_ARGB32_Premultiplied).copy();backdrop.setDevicePixelRatio(dpr);
        } else {
            WallpaperBackdrop wallpaper;wallpaper.reload(QSize(1920,1200));
            backdrop=wallpaper.sample(context->region,QRect(sx,sy,sw,sh),dpr);
        }
        if(backdrop.isNull())return nullptr;
        context->material=LiquidMaterial::prepare(backdrop);
        #if defined(__GLIBC__)
        malloc_trim(0); // return the decoder's temporary buffers, including worker arenas
        #endif
        return context.release();
    }catch(...){return nullptr;}
}
void *lqui_glass_create(const unsigned char *pixels,int width,int height,int stride,double dpr,int sx,int sy,int sw,int sh){
    return lqui_glass_create_region(pixels,width,height,stride,dpr,sx,sy,sw,sh,sx,sy,sw,sh);
}
void lqui_glass_set_adaptive_smoke(void *handle,int enabled){
    if(handle)static_cast<MaterialContext*>(handle)->adaptiveSmoke=enabled!=0;
}
int lqui_glass_render(void *handle,int x,int y,int width,int height,double dpr,double radius,unsigned char *out,int outWidth,int outHeight,int stride){
    if(!guiThread()||!handle||!out||width<1||height<1||width>4096||height>4096||!std::isfinite(dpr)||!std::isfinite(radius)||dpr<.5||dpr>4||outWidth<1||outHeight<1||stride<outWidth*4)return 0;
    const QSize expected(qRound(width*dpr),qRound(height*dpr));if(expected!=QSize(outWidth,outHeight))return 0;
    try{
        auto *context=static_cast<MaterialContext*>(handle);const QRect rect(x,y,width,height);
        if(!context->region.contains(rect))return 0;
        if(!session) {
            session=new MaterialSession(qApp);
            QObject::connect(qApp,&QCoreApplication::aboutToQuit,qApp,[]{
                // Release GL while the platform integration still exists.
                activeContext=nullptr;delete session.data();session=nullptr;
            });
        }
        if(activeContext!=context || session->dpr!=dpr) {
            if(context->material.source.devicePixelRatio()==dpr)session->renderer.setPreparedWallpaper(context->material);
            else {
                QImage scaled=context->material.source.scaled(QSize(qRound(context->region.width()*dpr),qRound(context->region.height()*dpr)),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                scaled.setDevicePixelRatio(dpr);session->renderer.setWallpaper(scaled);
            }
            activeContext=context;session->dpr=dpr;
        }
        session->renderer.setAdaptiveSmoke(context->adaptiveSmoke);
        QImage image=session->renderer.renderPanel(rect.translated(-context->region.topLeft()),radius);
        context->usedGpu=session->renderer.usedGpu();
        if(image.isNull()||image.size()!=expected)return 0;
        QPainterPath shape;shape.addRoundedRect(QRectF(0,0,width,height),radius,radius);
        // Adaptive smoke already includes its reading veil. Applying the old
        // wide veil again would recreate a bright embossed band on white.
        image=LiquidMaterial::bodyTone(image,.34*(1-session->renderer.adaptiveSmokeAmount()),shape).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for(int row=0;row<image.height();++row){
            std::memcpy(out+row*stride,image.constScanLine(row),outWidth*4);
        }
        return 1;
    }catch(...){return 0;}
}
int lqui_glass_used_gpu(void *handle){auto *context=static_cast<MaterialContext*>(handle);return context&&context->usedGpu;}
void lqui_glass_destroy(void *handle){
    if(activeContext==handle && guiThread()) {if(session)session->renderer.setWallpaper({});activeContext=nullptr;}
    delete static_cast<MaterialContext*>(handle);
    #if defined(__GLIBC__)
    malloc_trim(0);
    #endif
}
}
