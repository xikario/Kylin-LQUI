#include "RegionCapture.h"
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <memory>

RegionCapture::RegionCapture(const QRect &screen,QPixmap screenshot):QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint),image(std::move(screenshot)) {
    setObjectName("regionCapture");setWindowTitle("AI小站 — 框选截图");setGeometry(screen);
    setAttribute(Qt::WA_DeleteOnClose);setMouseTracking(true);setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
}
QRect RegionCapture::selection() const {return QRect(first,last).normalized().intersected(rect());}
QList<RegionCapture*> RegionCapture::begin(std::function<void(QImage)> complete) {
    return beginWithGeometry([complete=std::move(complete)](QImage image,QRect){complete(image);});
}
QList<RegionCapture*> RegionCapture::beginWithGeometry(std::function<void(QImage,QRect)> complete) {
    struct Group { QList<QPointer<RegionCapture>> overlays;bool done=false;std::function<void(QImage,QRect)> callback; };
    auto group=std::make_shared<Group>();group->callback=std::move(complete);
    QList<RegionCapture*> result;
    // Take all snapshots before mapping any overlay, so another monitor never captures this UI.
    QList<QPair<QRect,QPixmap>> snapshots;
    for(auto *screen:QGuiApplication::screens()) {
        auto shot=screen->grabWindow(0);
        if(!shot.isNull())snapshots.append({screen->geometry(),std::move(shot)});
    }
    if(snapshots.isEmpty()){QTimer::singleShot(0,[group]{group->callback({},{});});return {};}
    for(auto &snapshot:snapshots) {
        auto *overlay=new RegionCapture(snapshot.first,std::move(snapshot.second));
        group->overlays.append(overlay);result.append(overlay);
        QObject::connect(overlay,&RegionCapture::selected,overlay,[group](QImage result,QRect selectedArea) {
            if(group->done)return;group->done=true;
            for(const auto &window:group->overlays)if(window){if(QWidget::mouseGrabber()==window)window->releaseMouse();window->close();}
            // Delete snapshots before resuming the CLI and release the capture's implicit mouse grab.
            QTimer::singleShot(0,[group,result=std::move(result),selectedArea]{group->callback(result,selectedArea);});
        });
        overlay->show();overlay->raise();
    }
    const auto cursor=QCursor::pos();
    for(auto *overlay:result)if(overlay->geometry().contains(cursor)){overlay->activateWindow();overlay->setFocus();}
    return result;
}
void RegionCapture::paintEvent(QPaintEvent *) {
    QPainter p(this);p.drawPixmap(rect(),image);p.fillRect(rect(),QColor(0,0,0,100));
    const QRect area=selection();
    if(dragging&&!area.isEmpty()) {
        p.save();p.setClipRect(area);p.drawPixmap(rect(),image);p.restore();
        p.setPen(QPen(QColor("#a6f6d7"),2));p.drawRect(area.adjusted(0,0,-1,-1));
        p.fillRect(QRect(area.topLeft(),QSize(148,30)),QColor(12,36,29,220));p.setPen(Qt::white);
        p.drawText(QRect(area.topLeft()+QPoint(8,0),QSize(135,30)),Qt::AlignVCenter,QString("%1 × %2").arg(area.width()).arg(area.height()));
    }
    p.setPen(Qt::white);p.drawText(rect().adjusted(25,24,-25,-24),Qt::AlignTop|Qt::AlignHCenter,
        QStringLiteral("拖动框选，松开后打开截图工具 · Esc / 右键取消"));
}
void RegionCapture::mousePressEvent(QMouseEvent *e) {
    if(e->button()==Qt::RightButton){emit selected({},{});return;}
    if(e->button()==Qt::LeftButton){first=last=e->pos();dragging=true;update();}
}
void RegionCapture::mouseMoveEvent(QMouseEvent *e){if(dragging){last=e->pos();update();}}
void RegionCapture::mouseReleaseEvent(QMouseEvent *e) {
    if(e->button()!=Qt::LeftButton||!dragging)return;
    last=e->pos();const QRect area=selection();dragging=false;
    if(area.width()<8||area.height()<8){update();return;}
    const qreal dx=qreal(image.width())/width(),dy=qreal(image.height())/height();
    const QRect physical(qRound(area.x()*dx),qRound(area.y()*dy),qRound(area.width()*dx),qRound(area.height()*dy));
    auto cropped=image.copy(physical).toImage();cropped.setDevicePixelRatio(1);emit selected(cropped,QRect(mapToGlobal(area.topLeft()),area.size()));
}
void RegionCapture::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Escape)emit selected({},{});else QWidget::keyPressEvent(e);}
