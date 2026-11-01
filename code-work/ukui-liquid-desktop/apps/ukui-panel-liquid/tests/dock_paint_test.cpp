#include <QApplication>
#include <QToolButton>
#include <QStyleOptionToolButton>
#include <QPainter>
#include <QDebug>
#include <QDir>
#include <cstdlib>

static void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QToolButton button;button.resize(44,48);button.setProperty("liquidDockIconSize",32);
    QPixmap source(96,96);source.fill(Qt::transparent);
    {QPainter p(&source);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(QColor("#49c886"));p.drawRoundedRect(QRectF(8,8,80,80),16,16);}
    QIcon icon(source);
    // Icon themes may supply visually different artwork at different sizes.
    QPixmap small(32,32);small.fill(Qt::red);icon.addPixmap(small);
    QStyleOptionToolButton option;option.initFrom(&button);option.rect=button.rect();option.icon=icon;
    option.iconSize=QSize(32,32);option.toolButtonStyle=Qt::ToolButtonIconOnly;option.state=QStyle::State_Enabled;
    auto render=[&](qreal scale){
        button.setProperty("liquidDockScale",scale);button.setProperty("liquidDockLift",6.4*(scale-1));
        const qreal dpr=button.devicePixelRatioF();QImage image(QSize(qRound(44*dpr),qRound(48*dpr)),QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);image.fill(Qt::transparent);
        QPainter p(&image);app.style()->drawControl(QStyle::CE_ToolButtonLabel,&option,&p,&button);
        check(!p.testRenderHint(QPainter::SmoothPixmapTransform),"Dock painting restores caller painter state");
        return image;
    };
    const QImage start=render(1.),a=render(1.010),b=render(1.012),end=render(1.2);
    check(a!=b,"fractional scale increments change pixels instead of integer-size stair steps");
    const QColor rest=start.pixelColor(start.width()/2,start.height()/2),hover=end.pixelColor(end.width()/2,end.height()/2);
    check(rest==hover&&rest.green()>rest.red(),"one icon raster variant is retained throughout magnification");
    option.state|=QStyle::State_MouseOver;
    check(render(1.2)==end,"native MouseOver does not replace the raster during the animation");
    auto plate=[&](QStyle::State state){option.state=state;QImage image(44,48,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);button.setProperty("liquidDockScale",1.);button.setProperty("liquidDockLift",0.);QPainter p(&image);app.style()->drawComplexControl(QStyle::CC_ToolButton,&option,&p,&button);return image;};
    check(plate(QStyle::State_Enabled)==plate(QStyle::State_Enabled|QStyle::State_MouseOver),"hover does not add a native task-button background plate");
    check(render(.90)!=render(1.),"pressed scale can shrink the clicked icon");
    QDir().mkpath("artifacts/dock-paint");start.save("artifacts/dock-paint/rest.png");end.save("artifacts/dock-paint/hover.png");
    qInfo()<<"PASS Dock fractional painting and stable icon raster at DPR"<<button.devicePixelRatioF();
}
