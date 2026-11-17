// The global effects switch "gpu=off" must force the CPU glass path in every
// process that links this renderer, even where OpenGL works.
#include "LiquidOpticsRenderer.h"
#include "NextKdeGlassView.h"
#include "../../effects/EffectsPolicy.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc,char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME",home.path().toUtf8());
    QApplication app(argc,argv);
    QImage wallpaper(320,220,QImage::Format_RGB32);wallpaper.fill(QColor(40,90,160));

    LquiEffects::setGpuAllowed(false);
    LiquidOpticsRenderer off;off.setWallpaper(wallpaper);
    const QImage cpu=off.renderPanel(QRect(0,0,320,220),16);
    if(cpu.isNull())return 1;
    if(off.usedGpu()){qCritical()<<"gpu=off still used the GPU";return 2;}
    if(!LiquidOpticsRenderer::gpuDisabled())return 3;

    LquiEffects::setGpuAllowed(true);
    if(LiquidOpticsRenderer::gpuDisabled()!=qEnvironmentVariableIsSet("UKUI_LIQUID_GLASS_NO_GL"))return 4;
    LiquidOpticsRenderer on;on.setWallpaper(wallpaper);
    if(on.renderPanel(QRect(0,0,320,220),16).isNull())return 5;
    if(app.arguments().contains("--gpu")&&!on.usedGpu())return 77; // no GL here: skip GPU half
    qInfo()<<"PASS: global gpu=off forces CPU; auto restores GPU:"<<on.usedGpu();
    return 0;
}
