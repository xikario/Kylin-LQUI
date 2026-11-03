#include "PanelAiStation.h"
#include "../../../shared/icon-style/IconPolicy.h"
#include "ControlCenterLauncher.h"
#include <QKeyEvent>
#include "StationMetrics.h"
#include "StationDrop.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <xcb/xcb.h>
#include <QAbstractButton>
#include <QBoxLayout>
#include <QGridLayout>
#include <QFontMetrics>
#include <QDateTime>
#include <QContextMenuEvent>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QShowEvent>
#include <QHideEvent>
#include <QScreen>
#include <QVariantAnimation>
#include <QMouseEvent>
#include <QSettings>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#undef FontChange
#undef None
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#include <algorithm>
#include <QDynamicPropertyChangeEvent>

namespace {
const QString service="org.ukui.AiStationPanel";
QString launcher(){return qEnvironmentVariable("UKUI_AI_STATION_LAUNCHER",QDir::homePath()+"/.local/bin/liquid-cli");}
class OrbAccent final : public QWidget {
public:
    explicit OrbAccent(QWidget *button):QWidget(button),button(button) {
        setObjectName("aiStationOrbAccent");setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_TranslucentBackground);setFocusPolicy(Qt::NoFocus);hide();
        button->installEventFilter(this);motion.setDuration(420);motion.setStartValue(0.);motion.setEndValue(1.);
        connect(&motion,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){
            progress=value.toReal();if(frame.elapsed()>=32||progress>=1){frame.restart();update();}
        });
        connect(&motion,&QVariantAnimation::finished,this,[this]{hide();setProperty("animating",false);});setProperty("animating",false);
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Enter)play(false);
        if(event->type()==QEvent::MouseButtonPress&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton)play(true);
        if(event->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName()=="liquidOrbPressFeedback")play(true);
        if(event->type()==QEvent::Resize)setGeometry(button->rect());
        if(event->type()==QEvent::Hide){motion.stop();hide();setProperty("animating",false);}
        return false;
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QPointF c=QRectF(rect()).center();const qreal radius=qMin(15.,height()/2.-6);
        const qreal alpha=(clicked?110:70)*(1-progress);
        p.setPen(QPen(QColor(166,234,255,qRound(alpha)),1.2));p.setBrush(Qt::NoBrush);p.drawEllipse(c,radius+2+progress*3,radius+2+progress*3);
        QPainterPath clip;clip.addEllipse(c,radius,radius);p.setClipPath(clip);
        const qreal x=c.x()-radius-12+progress*(radius*2+24);
        QLinearGradient sheen(QPointF(x-9,c.y()-16),QPointF(x+9,c.y()+16));
        sheen.setColorAt(0,Qt::transparent);sheen.setColorAt(.5,QColor(224,253,255,clicked?95:60));sheen.setColorAt(1,Qt::transparent);p.fillPath(clip,sheen);
    }
private:
    void play(bool click) {
        QSettings appearance(QDir::homePath()+"/.config/ukui/liquid-panel.ini",QSettings::IniFormat);
        if(QSettings("org.ukui","style").value("reduceAnimation",false).toBool()||appearance.value("appearance/reducedMotion",false).toBool())return;
        clicked=click;motion.stop();progress=0;setGeometry(button->rect());show();raise();frame.start();setProperty("animating",true);motion.start();
    }
    QWidget *button;QVariantAnimation motion;QElapsedTimer frame;qreal progress=0;bool clicked=false;
};

void paintOrbSurface(QPainter &p,const QRectF &r,bool hovered) {
    const QPointF c=r.center();const qreal size=r.width();p.setRenderHint(QPainter::Antialiasing);
    const auto theme=LquiIcon::uniform()?LquiIcon::theme():LquiIcon::Theme::Color;const auto palette=LquiIcon::colors(theme);
    if(hovered){p.setPen(Qt::NoPen);p.setBrush(QColor(145,208,255,40));p.drawEllipse(r.adjusted(-3,-3,3,3));}
    QRadialGradient base(c-QPointF(5,6),size*.9);base.setColorAt(0,palette.orbTop);base.setColorAt(.32,palette.orbMiddle);base.setColorAt(.65,theme==LquiIcon::Theme::Color?QColor("#7c6ceb"):palette.orbMiddle.darker(115));base.setColorAt(1,palette.orbBottom);
    p.setPen(QPen(QColor(219,246,255,180),.8));p.setBrush(base);p.drawEllipse(r);
    p.save();QPainterPath clip;clip.addEllipse(r);p.setClipPath(clip);
    QRadialGradient glow(c+QPointF(7,5),size*.65);QColor light=palette.orbGlow;light.setAlpha(theme==LquiIcon::Theme::Mist?70:190);glow.setColorAt(0,light);light.setAlpha(0);glow.setColorAt(1,light);p.fillRect(r,glow);
    p.setPen(QPen(QColor(235,255,255,theme==LquiIcon::Theme::Mist?55:150),1.1));p.setBrush(Qt::NoBrush);p.drawArc(r.adjusted(4,3,-4,-3),35*16,120*16);p.restore();
}
class StartOrbFace final:public QObject {
public:
    explicit StartOrbFace(QWidget *button):QObject(button),button(button) {
        setObjectName("lquiStartOrbFace");button->setCursor(Qt::PointingHandCursor);button->setAutoFillBackground(false);
        button->setProperty("doNotAnimate",true);button->setProperty("useIconHighlightEffect",false);button->installEventFilter(this);
        // Read the installed vector mark, not a low-resolution theme circle.
        // No theme asset is rewritten or bundled into this project.
        for(const auto &theme:{QIcon::themeName(),QString("ukui-icon-theme-fashion"),QString("ukui-icon-theme-classical")}) {
            QFile file("/usr/share/icons/"+theme+"/scalable/apps/kylin-startmenu.svg");if(!file.open(QIODevice::ReadOnly))continue;
            QXmlStreamReader xml(&file);
            while(!xml.atEnd()) {
                xml.readNext();if(!xml.isStartElement()||xml.name()!="polygon")continue;
                const auto values=xml.attributes().value("points").toString().split(QRegularExpression("[ ,\\s]+"),QString::SkipEmptyParts);
                if(values.size()<30)continue;QPolygonF polygon;
                for(int i=0;i+1<values.size();i+=2)polygon<<QPointF(values[i].toDouble(),values[i+1].toDouble());
                glyph.addPolygon(polygon);glyph.closeSubpath();break;
            }
            if(!glyph.isEmpty())break;
        }
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Enter||event->type()==QEvent::Leave||event->type()==QEvent::FocusIn||event->type()==QEvent::FocusOut)button->update();
        if(event->type()!=QEvent::Paint)return false;
        QPainter p(button);const qreal diameter=qMin(30,button->height()-8);const QPointF center(button->width()/2.,button->height()/2.);
        const QRectF face(center-QPointF(diameter/2,diameter/2),QSizeF(diameter,diameter));
        paintOrbSurface(p,face,button->underMouse()||button->hasFocus());
        if(!glyph.isEmpty()) {
            const QRectF bounds=glyph.boundingRect();const qreal scale=diameter*.68/qMax(bounds.width(),bounds.height());
            p.translate(center);p.scale(scale,scale);p.translate(-bounds.center());p.setPen(Qt::NoPen);p.setBrush(QColor("#effcff"));p.drawPath(glyph);
        } else {
            p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawPixmap(face.adjusted(3,3,-3,-3),QIcon::fromTheme("kylin-startmenu").pixmap(128,128),QRectF(0,0,128,128));
        }
        return true;
    }
private:QWidget *button;QPainterPath glyph;
};
class Orb final : public QAbstractButton, public QAbstractNativeEventFilter {
public:
    explicit Orb(QWidget *parent):QAbstractButton(parent) {
        setObjectName("aiStationOrb");setAccessibleName("AI小站");setToolTip("AI小站 · 右键设置");
        setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);setFixedSize(44,44);
        setAcceptDrops(true);
        if(QGuiApplication::platformName()=="xcb") {
            dropDisplay=XOpenDisplay(nullptr);
            if(dropDisplay){positionAtom=XInternAtom(dropDisplay,"XdndPosition",False);copyAtom=XInternAtom(dropDisplay,"XdndActionCopy",False);qApp->installNativeEventFilter(this);}
        }
        new OrbAccent(this);
        connect(this,&QAbstractButton::clicked,this,[this]{launch(false);});
    }
    ~Orb() override {if(dropDisplay){qApp->removeNativeEventFilter(this);XCloseDisplay(dropDisplay);}}
    bool nativeEventFilter(const QByteArray &type,void *message,long *) override {
        if(!dropDisplay||type!="xcb_generic_event_t"||!isVisible())return false;
        auto *generic=static_cast<xcb_generic_event_t*>(message);
        if((generic->response_type&127)!=XCB_CLIENT_MESSAGE)return false;
        auto *event=reinterpret_cast<xcb_client_message_event_t*>(generic);
        if(event->format!=32||event->type!=positionAtom||event->window!=effectiveWinId())return false;
        if(!QRect(mapToGlobal(QPoint()),size()).contains(QCursor::pos()))return false;
        // Qt 5.12.8 sources omit XdndActionList and receivers only expose the
        // proposed Move. This reference-only target requests Copy, as its
        // XdndStatus response must never authorize source-side deletion.
        // Scope is the orb's hit area; normal panel/file-manager DnD is intact.
        event->data.data32[4]=copyAtom;
        return false;
    }
protected:
    void dragEnterEvent(QDragEnterEvent *event) override {if((event->possibleActions()&Qt::CopyAction)&&StationDrop::accepts(event->mimeData())){dragOver=true;event->setDropAction(Qt::CopyAction);event->accept();update();}}
    void dragMoveEvent(QDragMoveEvent *event) override {if((event->possibleActions()&Qt::CopyAction)&&StationDrop::accepts(event->mimeData())){event->setDropAction(Qt::CopyAction);event->accept();}}
    void dragLeaveEvent(QDragLeaveEvent *event) override {dragOver=false;update();event->accept();}
    void dropEvent(QDropEvent *event) override {
        if(!(event->possibleActions()&Qt::CopyAction)){event->ignore();return;}
        dragOver=false;update();QString error;const auto manifest=StationDrop::prepare(event->mimeData(),&error);
        if(manifest.isEmpty()){if(!error.isEmpty())setToolTip(error);event->ignore();return;}
        event->setDropAction(Qt::CopyAction);event->accept();sendDrop(manifest,QRect(mapToGlobal(QPoint()),size()));
    }
    void enterEvent(QEvent *e) override{update();QAbstractButton::enterEvent(e);}
    void leaveEvent(QEvent *e) override{update();QAbstractButton::leaveEvent(e);}
    void contextMenuEvent(QContextMenuEvent *e) override {
        auto *m=new QMenu(this);m->setAttribute(Qt::WA_DeleteOnClose);
        m->addAction("打开 AI小站",this,[this]{launch(false);});
        m->addAction("AI小站设置…",this,[this]{launch(true);});m->popup(e->globalPos());
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const qreal size=qMin(30,height()-8);const QPointF c(width()/2.,height()/2.);QRectF r(c.x()-size/2,c.y()-size/2,size,size);
        paintOrbSurface(p,r,underMouse()||hasFocus());
        if(dragOver){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#a0f3db"),1.6));p.drawEllipse(r.adjusted(-3,-3,3,3));}
        QFont font=p.font();font.setPixelSize(10);font.setBold(true);p.setFont(font);p.setPen(isDown()?QColor("#dcf5ff"):Qt::white);p.drawText(r,Qt::AlignCenter,"AI");
    }
private:
    Display *dropDisplay=nullptr;Atom positionAtom=0,copyAtom=0;
    bool launching=false,dragOver=false;
    void sendDrop(const QString &manifest,const QRect &anchor,int attempt=0) {
        if(!QFileInfo::exists(manifest))return;
        auto call=QDBusMessage::createMethodCall("org.ukui.LiquidCli","/Cli","org.ukui.LiquidCli","ImportDropAt");
        call.setArguments({manifest,anchor.x(),anchor.y(),anchor.width(),anchor.height()});
        auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call,1500),this);
        connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,manifest,anchor,attempt](QDBusPendingCallWatcher *w){
            const QDBusPendingReply<bool> reply=*w;w->deleteLater();if(!reply.isError()){if(!reply.value())setToolTip("AI小站未能接收内容，请重新拖入");return;}
            const auto name=reply.error().name();
            if(name=="org.freedesktop.DBus.Error.ServiceUnknown"||name=="org.freedesktop.DBus.Error.NameHasNoOwner") {
                const QString a=QString("%1,%2,%3,%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height());
                if(!QProcess::startDetached(launcher(),{"--drop",manifest,"--anchor",a}))setToolTip("无法启动 AI小站接收拖放");
            } else if(attempt<12&&(name=="org.freedesktop.DBus.Error.UnknownObject"||name=="org.freedesktop.DBus.Error.NoReply"))QTimer::singleShot(150,this,[this,manifest,anchor,attempt]{sendDrop(manifest,anchor,attempt+1);});
            else setToolTip("AI小站未能接收内容，请重新拖入");
        });
    }
    void launch(bool settings) {
        if(launching)return;
        const QRect anchor(mapToGlobal(QPoint()),this->size());
        const auto method=settings?"PreferencesAt":"ToggleAt";
        auto call=QDBusMessage::createMethodCall("org.ukui.LiquidCli","/Cli","org.ukui.LiquidCli",method);
        call.setArguments({anchor.x(),anchor.y(),anchor.width(),anchor.height()});
        launching=true;auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call,1200),this);
        connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,settings,anchor](QDBusPendingCallWatcher *w){
            const QDBusPendingReply<> reply=*w;w->deleteLater();
            if(reply.isError()) {
                const auto name=reply.error().name();
                if(name=="org.freedesktop.DBus.Error.ServiceUnknown"||name=="org.freedesktop.DBus.Error.NameHasNoOwner") {
                    const QString path=launcher();
                    if(QFileInfo(path).isExecutable()) {
                        const QString a=QString("%1,%2,%3,%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height());
                        QProcess::startDetached(path,settings?QStringList{"--settings","--anchor",a}:QStringList{"--show","--anchor",a});
                    }else setToolTip("尚未安装 AI小站");
                }
                // NoReply must not spawn another instance: the live UI may be busy.
            }
            QTimer::singleShot(250,this,[this]{launching=false;});
        });
    }
};
QFont metricFont(int pixels,bool bold=false) {
    QFont font("DejaVu Sans Mono");font.setPixelSize(pixels);font.setBold(bold);return font;
}
class MinimalClock final : public QObject {
public:
    MinimalClock(QWidget *date,QObject *parent):QObject(parent),calendar(date) {
        button=date->findChild<QAbstractButton*>();if(!button)button=date;
        calendarMin=date->minimumSize();calendarMax=date->maximumSize();buttonMin=button->minimumSize();buttonMax=button->maximumSize();
        const QFontMetrics small(metricFont(10)),large(metricFont(12,true));
        const int width=qMax(small.horizontalAdvance("12/30 周四"),large.horizontalAdvance("23:59"))+4;
        button->setFixedSize(width,36);date->setFixedSize(width,36);button->setProperty("stationMinimalClock",true);button->installEventFilter(this);refresh();
    }
    void restore() {
        if(button){button->removeEventFilter(this);button->setProperty("stationMinimalClock",false);button->setMinimumSize(buttonMin);button->setMaximumSize(buttonMax);}
        if(calendar){calendar->setMinimumSize(calendarMin);calendar->setMaximumSize(calendarMax);}
    }
    void refresh() {
        const auto now=QDateTime::currentDateTime();const QStringList weeks{"周一","周二","周三","周四","周五","周六","周日"};
        const QString nextTime=now.toString("HH:mm"),nextDate=now.toString("MM/dd ")+weeks[now.date().dayOfWeek()-1];
        if(time!=nextTime||dateText!=nextDate){time=nextTime;dateText=nextDate;if(button)button->update();}
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()!=QEvent::Paint||!button)return false;
        QPainter p(button);p.setRenderHint(QPainter::TextAntialiasing);const int top=(button->height()-28)/2;
        p.setFont(metricFont(12,true));p.setPen(QColor("#e2e8f0"));p.drawText(QRect(0,top,button->width(),15),Qt::AlignRight|Qt::AlignVCenter,time);
        p.setFont(metricFont(10));p.setPen(QColor("#94a3b8"));p.drawText(QRect(0,top+15,button->width(),13),Qt::AlignRight|Qt::AlignVCenter,dateText);
        return true;
    }
private:
    QPointer<QWidget> calendar,button;QSize calendarMin,calendarMax,buttonMin,buttonMax;QString time,dateText;
};
class Readout final : public QWidget {
public:
    explicit Readout(QWidget *parent):QWidget(parent) {
        setObjectName("aiStationReadout");setMinimumHeight(36);setFont(metricFont(11));setToolTip("当前温度与近 5 分钟峰值\n实时下行 / 上行速率");
        const QFontMetrics number(metricFont(11,true)),small(metricFont(10)),normal(metricFont(11));
        labelWidth=normal.horizontalAdvance("TEMP");valueWidth=number.horizontalAdvance("150°C");
        thermalWidth=qMax(labelWidth+4+valueWidth,small.horizontalAdvance("PEAK 150°C"));
        numberWidth=normal.horizontalAdvance("9999");unitWidth=normal.horizontalAdvance("MB/s");
        networkX=thermalWidth+17;setFixedWidth(networkX+11+numberWidth+3+unitWidth+16);
        setProperty("stationStyle","minimal");setMouseTracking(true);
    }
    void setValues(const StationMetrics::Thermal &t,const StationMetrics::Rates &r) {
        const QStringList next{t.current>=0?QString::number(qRound(t.current))+"°C":"—",
            "PEAK "+(t.peak>=0?QString::number(qRound(t.peak))+"°C":"—"),
            r.valid?StationMetrics::rate(r.down):"—",r.valid?StationMetrics::rate(r.up):"—"};
        if(next!=lines){lines=next;update();}
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if(lines.size()!=4)return;QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const int top=(height()-28)/2;
        p.setFont(metricFont(11));p.setPen(QColor("#94a3b8"));p.drawText(QRect(0,top,labelWidth,15),Qt::AlignLeft|Qt::AlignVCenter,"TEMP");
        p.setFont(metricFont(11,true));p.setPen(QColor("#fcd34d"));p.drawText(QRect(labelWidth+4,top,valueWidth,15),Qt::AlignRight|Qt::AlignVCenter,lines[0]);
        p.setFont(metricFont(10));p.setPen(QColor("#64748b"));p.drawText(QRect(0,top+15,thermalWidth,13),Qt::AlignLeft|Qt::AlignVCenter,lines[1]);
        p.setPen(QColor("#334155"));p.drawLine(thermalWidth+8,(height()-20)/2,thermalWidth+8,(height()+20)/2);
        for(int i=0;i<2;++i) {
            const QColor ink(i==0?"#22d3ee":"#94a3b8");const qreal cy=top+(i==0?7.5:21.5),cx=networkX+3,dir=i==0?1:-1;
            p.setPen(QPen(ink,1.15,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            p.drawLine(QPointF(cx,cy-dir*3),QPointF(cx,cy+dir*3));
            p.drawLine(QPointF(cx-2.2,cy+dir*.8),QPointF(cx,cy+dir*3));p.drawLine(QPointF(cx+2.2,cy+dir*.8),QPointF(cx,cy+dir*3));
            const auto cells=lines[i+2].split(' ');p.setFont(metricFont(11));p.setPen(ink);
            const QRect row(networkX+11,top+(i==0?0:15),numberWidth, i==0?15:13);
            p.drawText(row,Qt::AlignRight|Qt::AlignVCenter,cells.value(0));
            p.drawText(QRect(row.right()+4,row.top(),unitWidth,row.height()),Qt::AlignLeft|Qt::AlignVCenter,cells.value(1));
        }
        p.setPen(QColor("#334155"));p.drawLine(width()-8,(height()-20)/2,width()-8,(height()+20)/2);
    }
private:QStringList lines;int labelWidth=0,valueWidth=0,thermalWidth=0,numberWidth=0,unitWidth=0,networkX=0;
};
class Cluster final : public QWidget {
public:
    explicit Cluster(QWidget *date,QWidget *parent):QWidget(parent),calendar(date) {
        setObjectName("aiStationCluster");setProperty("stationStyle","minimal");setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);setMouseTracking(true);
        auto *layout=new QHBoxLayout(this);layout->setContentsMargins(10,0,2,0);layout->setSpacing(0);
        readout=new Readout(this);layout->addWidget(readout);face=new MinimalClock(date,this);
        layout->addWidget(date,0,Qt::AlignVCenter);date->show();layout->addSpacing(18);
        orb=new Orb(this);layout->addWidget(orb,0,Qt::AlignVCenter);
        readout->installEventFilter(this);date->installEventFilter(this);
        clock.start();timer.setTimerType(Qt::CoarseTimer);timer.setInterval(2000);connect(&timer,&QTimer::timeout,this,[this]{sample();});
    }
    void restoreCalendar() {
        face->restore();if(!calendar||!parentWidget()||!parentWidget()->layout())return;
        layout()->removeWidget(calendar);calendar->setParent(parentWidget());parentWidget()->layout()->addWidget(calendar);calendar->show();calendar=nullptr;
    }
    int samples=0;
protected:
    void showEvent(QShowEvent *) override {sample();timer.start();}
    void hideEvent(QHideEvent *) override {timer.stop();}
    void enterEvent(QEvent *event) override {update();QWidget::enterEvent(event);}
    void leaveEvent(QEvent *event) override {update();QWidget::leaveEvent(event);}
    bool eventFilter(QObject *,QEvent *event) override {if(event->type()==QEvent::Enter||event->type()==QEvent::Leave||event->type()==QEvent::MouseMove)update();return false;}
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QRectF r(.5,(height()-36)/2.,qMax(1,orb->x()-8),36);
        const bool hover=r.contains(mapFromGlobal(QCursor::pos()));p.setBrush(QColor(0,0,0,102));
        p.setPen(QPen(hover?QColor("#475569"):QColor(51,65,85,180),1));p.drawRoundedRect(r,10,10);
    }
private:
    void sample() {
        const qint64 ms=clock.elapsed();const auto net=metrics.network();const auto rates=metrics.updateNetwork(net,ms);
        if(lastThermal<0||ms-lastThermal>=10000){thermal=metrics.temperature(ms);lastThermal=ms;}
        readout->setValues(thermal,rates);face->refresh();++samples;
    }
    QPointer<QWidget> calendar;Orb *orb=nullptr;MinimalClock *face=nullptr;
    StationMetrics metrics;StationMetrics::Thermal thermal;Readout *readout;QTimer timer;QElapsedTimer clock;qint64 lastThermal=-1;
};
class DesktopCorner final : public QAbstractButton {
public:
    explicit DesktopCorner(QWidget *wrapper):QAbstractButton(wrapper?wrapper->window():nullptr),owner(wrapper) {
        setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus|Qt::X11BypassWindowManagerHint);
        setObjectName("aiStationDesktopCorner");setWindowTitle("显示桌面");
        setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);setAttribute(Qt::WA_X11NetWmWindowTypeDock);
        setFocusPolicy(Qt::NoFocus);setToolTip("显示桌面");setAccessibleName("显示桌面");setCursor(Qt::PointingHandCursor);
        connect(this,&QAbstractButton::clicked,this,[]{
            // EWMH is also what the desktop's native window-manager backend
            // uses. No shell process, desktop polling, or activation needed.
            Display *display=XOpenDisplay(nullptr);if(!display)return;
            const Window root=DefaultRootWindow(display);const Atom property=XInternAtom(display,"_NET_SHOWING_DESKTOP",False);
            Atom type;int format=0;unsigned long count=0,remaining=0;unsigned char *data=nullptr;bool showing=false;
            if(XGetWindowProperty(display,root,property,0,1,False,XA_CARDINAL,&type,&format,&count,&remaining,&data)==Success&&data&&format==32&&count)showing=*reinterpret_cast<unsigned long*>(data)!=0;
            if(data)XFree(data);
            XEvent event{};event.xclient.type=ClientMessage;event.xclient.window=root;event.xclient.message_type=property;event.xclient.format=32;event.xclient.data.l[0]=showing?0:1;
            XSendEvent(display,root,False,SubstructureNotifyMask|SubstructureRedirectMask,&event);XFlush(display);XCloseDisplay(display);
        });
        place();
        // KWin may round a dock's initial map rectangle on fractional scaling.
        // Reassert once after mapping so the hot corner reaches both edges.
        QTimer::singleShot(120,this,[this]{place();});
    }
    QWidget *wrapper() const{return owner;}
    void place() {
        if(!owner){hide();return;}
        auto *panel=owner->window();auto *screen=QGuiApplication::screenAt(panel->geometry().center());if(!screen)screen=QGuiApplication::primaryScreen();if(!screen)return;
        const auto area=screen->geometry();const int h=qBound(28,panel->height(),64);
        const QRect target(area.right()-11,area.bottom()-h+1,12,h);
        setGeometry(target);owner->hide();show();setGeometry(target);raise();snapOuterEdges();
    }
    void restore() {
        hide();if(owner)owner->show();
    }
protected:
    void enterEvent(QEvent *event) override{update();QAbstractButton::enterEvent(event);}
    void leaveEvent(QEvent *event) override{update();QAbstractButton::leaveEvent(event);}
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(QColor(210,236,245,underMouse()?180:85));painter.drawLine(1,5,1,height()-6);
    }
private:
    void snapOuterEdges() {
        // Qt 5 fractional scaling can round the client's far edge inward by
        // one physical pixel. Correct only tiny gaps at the X root boundary.
        Display *display=XOpenDisplay(nullptr);if(!display)return;
        const Window root=DefaultRootWindow(display),window=Window(winId());
        XWindowAttributes bounds{},client{};int x=0,y=0;Window child;
        if(XGetWindowAttributes(display,root,&bounds)&&XGetWindowAttributes(display,window,&client)&&XTranslateCoordinates(display,window,root,0,0,&x,&y,&child)) {
            const int dx=bounds.width-x-client.width,dy=bounds.height-y-client.height;
            const int nx=x+(dx>0&&dx<=3?dx:0),ny=y+(dy>0&&dy<=3?dy:0);
            if(nx!=x||ny!=y) {
                // The tiny corner surface is unmanaged, like a tooltip, so
                // this physical move cannot be rounded back by the WM.
                XMoveWindow(display,window,nx,ny);XFlush(display);
            }
        }
        XCloseDisplay(display);
    }
    QPointer<QWidget> owner;
};
}
PanelAiStation::PanelAiStation(QObject *parent):QObject(parent) {
    pending.setSingleShot(true);connect(&pending,&QTimer::timeout,this,&PanelAiStation::attach);
    qApp->installEventFilter(this);schedule();
    // Floating widgets must go away while QApplication's widget/platform
    // state still exists, before its QObject children are finally deleted.
    connect(qApp,&QCoreApplication::aboutToQuit,this,&PanelAiStation::shutdown);
    const auto watchScreen=[this](QScreen *screen){connect(screen,&QScreen::geometryChanged,this,[this]{schedule();});};
    for(auto *screen:QGuiApplication::screens())watchScreen(screen);
    connect(qApp,&QGuiApplication::screenAdded,this,[this,watchScreen](QScreen *screen){watchScreen(screen);schedule();});
    connect(qApp,&QGuiApplication::screenRemoved,this,[this]{schedule();});
}
PanelAiStation::~PanelAiStation() {
    shutdown();
}
void PanelAiStation::shutdown() {
    if(stopping)return;stopping=true;pending.stop();
    if(qApp)qApp->removeEventFilter(this);
    if(serviceOwned){auto bus=QDBusConnection::sessionBus();bus.unregisterObject("/AiStation");bus.unregisterService(service);}
    serviceOwned=false;
    for(auto w:corners)if(w){static_cast<DesktopCorner*>(w.data())->restore();delete w;}
    for(auto w:widgets)if(w){static_cast<Cluster*>(w.data())->restoreCalendar();delete w;}
    corners.clear();widgets.clear();
}
void PanelAiStation::schedule(){if(!stopping&&!pending.isActive())pending.start();}
void PanelAiStation::attach() {
    if(stopping)return;
    for(auto w:corners)if(w){auto *corner=static_cast<DesktopCorner*>(w.data());
        if(!corner->wrapper()||corner->wrapper()->window()->width()<corner->wrapper()->window()->height()){corner->restore();delete w;}else corner->place();}
    corners.erase(std::remove_if(corners.begin(),corners.end(),[](const QPointer<QWidget> &w){return !w;}),corners.end());
    for(auto w:widgets)if(w&&w->window()->width()<w->window()->height()) {
        static_cast<Cluster*>(w.data())->restoreCalendar();delete w;
    }
    widgets.erase(std::remove_if(widgets.begin(),widgets.end(),[](const QPointer<QWidget> &w){return !w;}),widgets.end());
    for(auto *w:QApplication::allWidgets()) {
        if(w->objectName()=="StartMenuButton"&&w->window()->inherits("UKUIPanel")) {
            if(!w->findChild<QWidget*>("aiStationOrbAccent"))new OrbAccent(w);
            if(!w->findChild<QObject*>("lquiStartOrbFace"))new StartOrbFace(w);
        }
        if(w->objectName()=="ukui-panel_plugin_QFrame_showdesktop"&&w->window()->inherits("UKUIPanel")&&w->window()->width()>=w->window()->height()&&QGuiApplication::platformName()=="xcb") {
            bool moved=false;for(auto corner:corners)if(corner&&static_cast<DesktopCorner*>(corner.data())->wrapper()==w)moved=true;
            if(!moved&&w->layout()&&w->layout()->count()==1) {
                if(w->layout()->itemAt(0)->widget()){auto *corner=new DesktopCorner(w);corners.append(corner);}
            }
        }
        if(w->objectName()!="ukui-panel_plugin_QFrame_calendar"||!w->window()->inherits("UKUIPanel"))continue;
        if(w->window()->width()<w->window()->height()||w->findChild<QWidget*>("aiStationCluster"))continue;
        auto *row=qobject_cast<QBoxLayout *>(w->layout());auto *grid=qobject_cast<QGridLayout *>(w->layout());
        if((!row&&!grid)||w->layout()->count()!=1)continue;
        auto *calendar=w->layout()->itemAt(0)->widget();if(!calendar)continue;
        w->layout()->removeWidget(calendar);
        auto *cluster=new Cluster(calendar,w);
        if(row)row->addWidget(cluster);else grid->addWidget(cluster,0,0);
        widgets.append(cluster);connect(cluster,&QObject::destroyed,this,[this]{schedule();});cluster->show();
    }
    const bool hosted=!widgets.isEmpty();
    if(serviceOwned&&!hosted){auto bus=QDBusConnection::sessionBus();bus.unregisterService(service);bus.unregisterObject("/AiStation");serviceOwned=false;}
    if(!serviceOwned&&hosted) {
        auto bus=QDBusConnection::sessionBus();
        if(bus.registerObject("/AiStation",this,QDBusConnection::ExportScriptableSlots)) {
            serviceOwned=bus.registerService(service);if(!serviceOwned)bus.unregisterObject("/AiStation");
        }
    }
}
bool PanelAiStation::eventFilter(QObject *o,QEvent *e) {
    auto *calendar=qobject_cast<QWidget*>(o);
    if(calendar&&calendar->inherits("LunarCalendarWidget")&&e->type()==QEvent::Hide){
        if(auto *motion=calendar->findChild<QVariantAnimation*>("lquiCalendarMotion"))motion->stop();
        calendar->setProperty("lquiCalendarOpening",false);calendar->setWindowOpacity(1);
    }
    if(calendar&&calendar->inherits("LunarCalendarWidget")&&(e->type()==QEvent::Show||e->type()==QEvent::Resize)){
        if(e->type()==QEvent::Show){calendar->setProperty("lquiCalendarOpening",true);calendar->setWindowOpacity(0);}
        else if(auto *motion=calendar->findChild<QVariantAnimation*>("lquiCalendarMotion"))if(motion->state()==QAbstractAnimation::Running)return false;
        // A Move is also generated by the window manager's fractional-DPI
        // rounding. Feeding it back into move() can oscillate forever.
        if(calendar->property("lquiCalendarPositionQueued").toBool())return false;
        calendar->setProperty("lquiCalendarPositionQueued",true);
        QPointer<QWidget> guard(calendar);QTimer::singleShot(0,this,[guard]{
            if(guard)guard->setProperty("lquiCalendarPositionQueued",false);
            if(!guard||!guard->isVisible())return;
            QWidget *panel=nullptr;for(auto *window:QApplication::topLevelWidgets())if(window->inherits("UKUIPanel")&&window->isVisible()){panel=window;break;}if(!panel)return;
            QWidget *surface=panel->findChild<QWidget*>("BackgroundWidget");if(!surface)surface=panel;
            const QRect anchor(surface->mapToGlobal(QPoint()),surface->size());auto *screen=QGuiApplication::screenAt(anchor.center());if(!screen)return;
            const QRect available=screen->availableGeometry().adjusted(6,6,-6,-6);
            const QPoint position(qBound(available.left(),anchor.right()-guard->width()+1,qMax(available.left(),available.right()-guard->width()+1)),qMax(available.top(),anchor.top()-guard->height()-8));
            const bool opening=guard->property("lquiCalendarOpening").toBool();guard->setProperty("lquiCalendarOpening",false);
            if(!opening){if((guard->pos()-position).manhattanLength()>1)guard->move(position);return;}
            // Hide the OEM screen-right placement until its show handler has
            // returned. Animate only Y from the Dock; never correct X visibly.
            auto *motion=guard->findChild<QVariantAnimation*>("lquiCalendarMotion");
            if(!motion){motion=new QVariantAnimation(guard);motion->setObjectName("lquiCalendarMotion");}
            motion->stop();motion->disconnect();
            if(QSettings("org.ukui","style").value("reduceAnimation",false).toBool()){
                guard->move(position);guard->setWindowOpacity(1);return;
            }
            guard->move(position+QPoint(0,24));
            motion->setDuration(180);motion->setStartValue(0.);motion->setEndValue(1.);motion->setEasingCurve(QEasingCurve::OutCubic);
            connect(motion,&QVariantAnimation::valueChanged,guard,[guard,position](const QVariant &value){
                if(!guard||!guard->isVisible())return;const qreal t=value.toReal();
                guard->move(position+QPoint(0,qRound(24*(1-t))));guard->setWindowOpacity(t);
            });
            connect(motion,&QVariantAnimation::finished,guard,[guard,position]{if(guard){guard->move(position);guard->setWindowOpacity(1);}});
            motion->start();
        });
    }

    if((e->type()==QEvent::Show||e->type()==QEvent::ChildPolished||e->type()==QEvent::Polish||e->type()==QEvent::Resize||e->type()==QEvent::Move)
        &&(o->objectName()=="StartMenuButton"||o->objectName()=="ukui-panel_plugin_QFrame_calendar"||o->objectName()=="ukui-panel_plugin_QFrame_showdesktop"||o->inherits("UKUIPanel")))schedule();
    return false;
}
QString PanelAiStation::State() const {
    QJsonArray array;
    for(auto w:widgets)if(w){const QPoint p=w->mapToGlobal(QPoint());auto *cluster=static_cast<Cluster*>(w.data());
        array.append(QJsonObject{{"x",p.x()},{"y",p.y()},{"width",w->width()},{"height",w->height()},{"visible",w->isVisible()},{"samples",cluster->samples}});}
    QJsonArray orbs;
    for(auto *w:QApplication::allWidgets())if(w->objectName()=="aiStationOrb") {
        const QPoint p=w->mapToGlobal(QPoint());orbs.append(QJsonObject{{"x",p.x()},{"y",p.y()},{"width",w->width()},{"height",w->height()},{"visible",w->isVisible()}});
    }
    QJsonArray systemEntries;
    // Read-only identities and coordinates: tray membership can change while
    // testing, so automation must not guess fixed screen pixels.
    const QList<QPair<QString,QString>> trayIds={{"ukui-sidebar","notifications"},{"kylin-nm","wifi"},{"ukui-volume-control-applet-qt","sound"},{"ukui-power-manager-tray","power"},{"ukui-bluetooth","bluetooth"}};
    for(auto *w:QApplication::allWidgets()) {
        if(!w->inherits("StatusNotifierButton")&&!w->inherits("StatusNotifierStorageArrow"))continue;
        QString entry;
        for(const auto &id:trayIds)if(w->accessibleName().endsWith("_"+id.first)){entry=id.second;break;}
        if(w->objectName()=="StatusNotifier_arrowBtn")entry="overflow";
        if(entry.isEmpty())continue;
        const QPoint p=w->mapToGlobal(QPoint());
        systemEntries.append(QJsonObject{{"entry",entry},{"visible",w->isVisible()},{"enabled",w->isEnabled()},
            {"x",p.x()},{"y",p.y()},{"width",w->width()},{"height",w->height()},{"dpr",w->devicePixelRatioF()}});
    }
    QJsonArray edge;
    for(auto w:corners)if(w)edge.append(QJsonObject{{"x",w->x()},{"y",w->y()},{"width",w->width()},{"height",w->height()},{"visible",w->isVisible()}});
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"clusters",array},{"orbs",orbs},{"desktopCorners",edge},{"systemEntries",systemEntries}}).toJson(QJsonDocument::Compact));
}

bool PanelAiStation::OpenCalendar(){
    for(auto *widget:QApplication::allWidgets())if(widget->inherits("CalendarButton"))return QMetaObject::invokeMethod(widget,"pressShowHideCalendar",Qt::QueuedConnection);
    return false;
}
