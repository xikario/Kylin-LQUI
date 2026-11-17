// A real GPU initialisation failure must trip the session to "lite" so every
// process stops retrying the GPU, while the frame itself still renders on CPU.
// Run under xvfb with QT_XCB_GL_INTEGRATION=none (Qt then cannot create a GL context).
#include "LiquidOpticsRenderer.h"
#include "../../effects/EffectsPolicy.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc,char **argv)
{
    QTemporaryDir home,runtime;
    qputenv("XDG_CONFIG_HOME",home.path().toUtf8());qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());
    QApplication app(argc,argv);
    if(QGuiApplication::platformName()!="xcb")return 77;
    QImage wallpaper(200,120,QImage::Format_RGB32);wallpaper.fill(QColor(60,90,140));
    LiquidOpticsRenderer renderer;renderer.setWallpaper(wallpaper);
    const QImage frame=renderer.renderPanel(QRect(0,0,200,120),12);
    if(frame.isNull()){qCritical()<<"no CPU fallback frame";return 1;}
    if(renderer.usedGpu())return 77; // GL worked here after all: nothing to test
    LquiEffects::invalidate();
    if(!LquiEffects::tripped()){qCritical()<<"GPU init failure was not reported";return 2;}
    if(!LquiEffects::tripReason().contains("gpu-init-failed")){qCritical()<<LquiEffects::tripReason();return 3;}
    if(LquiEffects::level()!=LquiEffects::Level::Lite)return 4;
    qInfo()<<"PASS: GPU init failure tripped the session:"<<LquiEffects::tripReason();
    return 0;
}
