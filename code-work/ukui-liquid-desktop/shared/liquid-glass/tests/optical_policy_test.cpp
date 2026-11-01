#include "CohesiveGlass.h"
#include "LiquidMaterial.h"
#include <QApplication>
#include <QDebug>
#include <QPainter>

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QImage source(600,400,QImage::Format_RGB32);
    {QPainter p(&source);QLinearGradient gradient(0,0,600,400);gradient.setColorAt(0,QColor("#cc2b87"));gradient.setColorAt(.4,QColor("#244ac0"));gradient.setColorAt(1,QColor("#12a89a"));p.fillRect(source.rect(),gradient);
        p.fillRect(80,60,70,280,QColor("#fa8a15"));p.fillRect(400,40,150,100,QColor("#b5eaff"));p.setPen(Qt::white);p.drawText(180,200,"Backdrop detail / Snell optical policy");}
    LiquidOpticsRenderer reference,common,dock;
    reference.setWallpaper(source);CohesiveGlass::configure(common);common.setWallpaper(source);
    dock.setFinish(LiquidOpticsRenderer::Finish::Liquid);dock.setOptics(2.6,.58,.28,.86);dock.setMaterial(0,.75);dock.setWallpaper(source);
    const QRect rect(0,0,600,400);const auto original=reference.renderPanel(rect,26),restored=common.renderPanel(rect,26),flat=dock.renderPanel(rect,26);
    if(app.arguments().contains("--gpu")&&!common.usedGpu())return 77;
    if(original!=restored || restored==flat){qCritical()<<"full surfaces must preserve the original Snell policy, independently of Dock presets";return 1;}
    QPainterPath outline;outline.addRoundedRect(QRectF(0,0,600,400),26,26);
    const auto toned=LiquidMaterial::bodyTone(restored,.35,outline);
    for(int y=0;y<400;++y)for(int x=0;x<600;++x)if(qAlpha(toned.pixel(x,y))!=qAlpha(restored.pixel(x,y)))return 2;
    if(qRed(toned.pixel(300,200))>=qRed(restored.pixel(300,200)))return 3;
    if(app.arguments().contains("--preview")) {
        QImage comparison(1800,440,QImage::Format_RGB32);comparison.fill(QColor("#121822"));QPainter p(&comparison);
        p.drawImage(0,40,source);p.drawImage(600,40,flat);p.drawImage(1200,40,toned);p.setPen(Qt::white);p.drawText(15,25,"Scene");p.drawText(615,25,"Dock policy (not for full panels)");p.drawText(1215,25,"Full Snell + body-only reading veil");p.end();comparison.save("/tmp/lqui-optical-policy.png");
    }
    qInfo()<<"PASS full-surface Snell policy, body contrast and preserved rim alpha; GPU"<<common.usedGpu();return 0;
}
