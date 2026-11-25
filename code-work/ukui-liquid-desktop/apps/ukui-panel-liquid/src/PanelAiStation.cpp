#include "PanelAiStation.h"
#include "CalendarPlacement.h"
#include "CalendarLauncher.h"
#include "LiquidPopup.h"
#include "../../../shared/icon-style/IconPolicy.h"
#include "../../../shared/effects/EffectsPolicy.h"
#include "ControlCenterLauncher.h"
#include <QKeyEvent>
#include <QBuffer>
#include "StationMetrics.h"
#include "StartQilin.h"
#include "../../../shared/desktop-launch/StartIconPreference.h"
#include <QFileSystemWatcher>
#include "StationDrop.h"
#include "PetEntry.h"
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
#include <QWindow>
#include <QVariantAnimation>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <array>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDate>
#include <QPointer>
#include <QToolTip>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#undef FontChange
#undef None
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#include <algorithm>
#include <memory>
#include <QDynamicPropertyChangeEvent>
#include <QSet>
#include <QVector>
#include <dlfcn.h>

namespace {
const QString service="org.ukui.AiStationPanel";
QString launcher(){return qEnvironmentVariable("UKUI_AI_STATION_LAUNCHER",QDir::homePath()+"/.local/bin/liquid-cli");}
void *calendarSymbol(QWidget *widget,const char *name) {
    Dl_info info{};if(!widget||!dladdr(widget->metaObject(),&info)||!info.dli_fname)return nullptr;
    void *library=dlopen(info.dli_fname,RTLD_LAZY|RTLD_NOLOAD);if(!library)return nullptr;
    void *symbol=dlsym(library,name);dlclose(library);return symbol;
}
QWidget *scheduleCalendar(QWidget *item) {
    for(auto *p=item;p;p=p->parentWidget())if(p->inherits("LunarCalendarWidget"))return p;
    return nullptr;
}
QRect calendarTarget(QWidget *calendar) {
    return calendarDockTarget(calendar->size());
}
void positionCalendar(QWidget *calendar,const QPoint &position) {
    if(calendar->property("lquiCalendarCorrecting").toBool()||(calendar->pos()-position).manhattanLength()<=2)return;
    calendar->setProperty("lquiCalendarCorrecting",true);calendar->move(position);calendar->setProperty("lquiCalendarCorrecting",false);
}
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
        if(button->property("petFaceActive").toBool()){
            if(motion.state()==QAbstractAnimation::Running){motion.stop();hide();setProperty("animating",false);}
            return false;
        }
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
        if(LquiEffects::reducedMotion(appearance.value("appearance/reducedMotion",false).toBool())||!LquiEffects::decorativeFrames())return;
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
class StartOrbFace final : public QObject {
public:
    explicit StartOrbFace(QWidget *widget):QObject(widget),button(widget) {
        setObjectName("lquiStartOrbFace");
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoFillBackground(false);
        button->setProperty("doNotAnimate",true);
        button->setProperty("useIconHighlightEffect",false);
        glyph.addPolygon(art.silhouette());glyph.closeSubpath();
        frameTimer.setParent(this);frameTimer.setObjectName("qilinGestureFrame");
        waitTimer.setParent(this);waitTimer.setObjectName("qilinIdleWait");
        frameTimer.setSingleShot(true);waitTimer.setSingleShot(true);
        waitTimer.setTimerType(Qt::VeryCoarseTimer);
        connect(&frameTimer,&QTimer::timeout,this,[this]{
            const auto timing=StartQilin::timing(active);
            if(++frame>=timing.frames){rest();return;}
            paintFrame();
        });
        connect(&waitTimer,&QTimer::timeout,this,[this]{play(false);});
        const QString path=StartIconPreference::path();
        for(const auto &file:{path,panelPath()}){
            const auto dir=QFileInfo(file).absolutePath();
            if(QDir().mkpath(dir))watch.addPath(dir);
        }
        connect(&watch,&QFileSystemWatcher::directoryChanged,this,[this]{reload();});
        connect(&watch,&QFileSystemWatcher::fileChanged,this,[this]{reload();});
        LquiEffects::onChanged(this,[this]{reload();});
        button->installEventFilter(this);
        reload();
    }
protected:
    bool eventFilter(QObject *,QEvent *e) override {
        switch(e->type()){
        case QEvent::Enter:play(true);button->update();break;
        case QEvent::Leave:if(greeting)rest();button->update();break;
        case QEvent::Show:schedule();break;
        case QEvent::Hide:rest(false);break;
        case QEvent::MouseButtonPress:rest(false);break;
        case QEvent::MouseButtonRelease:schedule();break;
        case QEvent::DynamicPropertyChange:
            if(static_cast<QDynamicPropertyChangeEvent*>(e)->propertyName()=="liquidOrbPressFeedback")rest();
            break;
        case QEvent::FocusIn:case QEvent::FocusOut:button->update();break;
        case QEvent::Paint:paint();return true;
        default:break;
        }
        return false;
    }
private:
    static QString panelPath(){return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui/liquid-panel.ini";}
    void reload(){
        const auto path=StartIconPreference::path();
        for(const auto &file:{path,panelPath()})
            if(QFileInfo::exists(file)&&!watch.files().contains(file))watch.addPath(file);
        const bool pet=StartIconPreference::pet(QSettings(path,QSettings::IniFormat));
        const bool reduced=LquiEffects::reducedMotion(QSettings(panelPath(),QSettings::IniFormat).value("appearance/reducedMotion",false).toBool());
        if(initialized&&pet==petMode&&reduced==reduceMotion)return;
        initialized=true;petMode=pet;reduceMotion=reduced;
        button->setProperty("petFaceActive",petMode);
        if(petMode&&idle.isNull())idle=art.render(StartQilin::Rest,0);
        rest();
    }
    void schedule(){
        if(petMode&&!reduceMotion&&button->isVisible()&&active==StartQilin::Rest&&!waitTimer.isActive())
            waitTimer.start(QRandomGenerator::global()->bounded(18000,38001));
    }
    void rest(bool rearm=true){
        frameTimer.stop();waitTimer.stop();active=StartQilin::Rest;greeting=false;
        button->update();
        if(rearm)schedule();
    }
    StartQilin::Move choose(){
        if(next==order.size()){
            auto &random=*QRandomGenerator::global();
            std::shuffle(order.begin(),order.end(),random);
            if(order.front()==last)std::swap(order.front(),order.back());
            next=0;
        }
        last=order[next++];return last;
    }
    void play(bool hover){
        if(!petMode||reduceMotion||!button->isVisible())return;
        if(QApplication::mouseButtons()!=Qt::NoButton||QApplication::activePopupWidget()){schedule();return;}
        if(active!=StartQilin::Rest&&(!hover||greeting))return;
        waitTimer.stop();active=choose();greeting=hover;frame=0;
        paintFrame();
    }
    void paintFrame(){
        const auto timing=StartQilin::timing(active);
        current=art.render(active,(frame+1.)/(timing.frames+1.));
        frameTimer.start(timing.ms);button->update();
    }
    void paint() const {
        QPainter p(button);
        const QPointF center=QRectF(button->rect()).center();
        if(petMode){
            // 40 logical pixels including motion margins gives a ~34 px animal,
            // matching the visible area of the taskbar's 35 px pet portrait.
            const qreal side=qMax(1,qMin(40,qMin(button->width(),button->height())-2));
            QRectF target(0,0,side,side);target.moveCenter(center);
            const QPixmap &sprite=active==StartQilin::Rest?idle:current;
            p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawPixmap(target,sprite,QRectF(sprite.rect()));
            return;
        }
        const qreal diameter=qMin(30,button->height()-8);
        QRectF face(0,0,diameter,diameter);face.moveCenter(center);
        paintOrbSurface(p,face,button->underMouse()||button->hasFocus());
        if(!art.silhouette().isEmpty()){
            const QRectF bounds=glyph.boundingRect();const qreal scale=diameter*.68/qMax(bounds.width(),bounds.height());
            p.translate(center);p.scale(scale,scale);p.translate(-bounds.center());
            p.fillPath(glyph,QColor("#effcff"));
        } else {
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawPixmap(face.adjusted(3,3,-3,-3),QIcon::fromTheme("kylin-startmenu").pixmap(128,128),QRectF(0,0,128,128));
        }
    }
    QWidget *button;
    StartQilin art;
    QPainterPath glyph;
    QPixmap idle,current;
    QTimer frameTimer,waitTimer;
    QFileSystemWatcher watch;
    bool initialized=false,petMode=false,reduceMotion=false,greeting=false;
    StartQilin::Move active=StartQilin::Rest,last=StartQilin::Rest;
    std::array<StartQilin::Move,StartQilin::Count-1> order{{
        StartQilin::Roar,StartQilin::Tail,StartQilin::Leap,StartQilin::Bow,StartQilin::Listen,
        StartQilin::Stamp,StartQilin::Stretch,StartQilin::Trot,StartQilin::Blink,
        StartQilin::CloudRide,StartQilin::MistStep,StartQilin::Star,StartQilin::Ripple,StartQilin::Flame
    }};
    size_t next=order.size();
    int frame=0;
};
class Orb final : public QAbstractButton, public QAbstractNativeEventFilter {
public:
    explicit Orb(QWidget *parent):QAbstractButton(parent) {
        setObjectName("aiStationOrb");setAccessibleName("AI小站");setToolTip("AI小站 · 右键截图与设置");
        setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);setFixedSize(44,44);
        setAcceptDrops(true);
        if(QGuiApplication::platformName()=="xcb") {
            dropDisplay=XOpenDisplay(nullptr);
            if(dropDisplay){positionAtom=XInternAtom(dropDisplay,"XdndPosition",False);copyAtom=XInternAtom(dropDisplay,"XdndActionCopy",False);qApp->installNativeEventFilter(this);}
        }
        new OrbAccent(this);
        refreshPet();
        greetingEnd.setParent(this);greetingEnd.setObjectName("petGreetingEnd");
        greetingEnd.setSingleShot(true);
        connect(&greetingEnd,&QTimer::timeout,this,[this]{showGreetingFrame(greetingFrame+1);});
        singleClick.setSingleShot(true);
        connect(&singleClick,&QTimer::timeout,this,[this]{
            refreshPet();if(petId.isEmpty())return;
            auto call=QDBusMessage::createMethodCall("org.ukui.AiPet","/Pet","org.ukui.AiPet","Toggle");
            call.setAutoStartService(false);
            auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call,1000),this);
            connect(watcher,&QDBusPendingCallWatcher::finished,this,[this](QDBusPendingCallWatcher *w){
                const QDBusPendingReply<> reply=*w;w->deleteLater();
                if(!reply.isError())return;
                const auto error=reply.error().name();
                if(error=="org.freedesktop.DBus.Error.ServiceUnknown"||error=="org.freedesktop.DBus.Error.NameHasNoOwner"||error=="org.freedesktop.DBus.Error.UnknownObject")
                    if(!QProcess::startDetached("gtk-launch",{"kylin-ai-pet"}))notify("无法启动 AI 宠物");
            });
        });
        connect(this,&QAbstractButton::clicked,this,[this]{
            refreshPet();
            if(petId.isEmpty()){launch(false);return;}
            singleClick.start(QApplication::doubleClickInterval());
        });
    }
    void refreshPet() {
        QSettings cfg(PetEntry::path(),QSettings::IniFormat);
        QString next=cfg.value("entry/mode").toString()=="pet"?cfg.value("entry/character","claude").toString():QString();
        if(PetEntry::label(next).isEmpty())next.clear();
        if(next==petId)return;
        petId=next;
        const QString avatar=PetEntry::icon(next);
        petFace=avatar.isEmpty()?QPixmap():QPixmap(avatar);
        loadGreeting(avatar);
        setProperty("petFaceActive",!petFace.isNull());
        const QString name=next.isEmpty()?"AI小站":PetEntry::label(next);
        setAccessibleName(name);setToolTip(next.isEmpty()?QStringLiteral("AI小站 · 右键截图与设置"):QString());update();
    }
    void petChat(const QString &id) {
        const QRect anchor(mapToGlobal(QPoint()),size());
        auto call=QDBusMessage::createMethodCall("org.ukui.LiquidCli","/Cli","org.ukui.LiquidCli","PetChatAt");
        call.setArguments({id,anchor.x(),anchor.y(),anchor.width(),anchor.height()});
        auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call,1200),this);
        connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,id,anchor](QDBusPendingCallWatcher *w){
            const QDBusPendingReply<> reply=*w;w->deleteLater();if(!reply.isError())return;
            if(reply.error().name()=="org.freedesktop.DBus.Error.ServiceUnknown"||reply.error().name()=="org.freedesktop.DBus.Error.NameHasNoOwner"){
                const QString a=QString("%1,%2,%3,%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height());
                QProcess::startDetached(launcher(),{"--pet-chat",id,"--anchor",a});
            }else notify("无法打开宠物对话");
        });
    }
    void petDropMenu(const QString &manifest,const QPoint &point) { showDropMenu(manifest,QRect(point,QSize(1,1))); }
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
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        refreshPet();
        if(e->button()==Qt::LeftButton&&!petId.isEmpty()){
            singleClick.stop();skipRelease=true;setDown(false);petChat(petId);e->accept();return;
        }
        QAbstractButton::mouseDoubleClickEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if(skipRelease&&e->button()==Qt::LeftButton){skipRelease=false;setDown(false);e->accept();return;}
        QAbstractButton::mouseReleaseEvent(e);
    }
    void dragEnterEvent(QDragEnterEvent *event) override {if((event->possibleActions()&Qt::CopyAction)&&StationDrop::accepts(event->mimeData())){setDragOver(true);event->setDropAction(Qt::CopyAction);event->accept();}}
    void dragMoveEvent(QDragMoveEvent *event) override {if((event->possibleActions()&Qt::CopyAction)&&StationDrop::accepts(event->mimeData())){event->setDropAction(Qt::CopyAction);event->accept();}}
    void dragLeaveEvent(QDragLeaveEvent *event) override {setDragOver(false);event->accept();}
    void dropEvent(QDropEvent *event) override {
        if(!(event->possibleActions()&Qt::CopyAction)){event->ignore();return;}
        setDragOver(false);QString error;const auto manifest=StationDrop::prepare(event->mimeData(),&error);
        if(manifest.isEmpty()){if(!error.isEmpty())notify(error);event->ignore();return;}
        event->setDropAction(Qt::CopyAction);event->accept();
        // The drag source is still waiting for this event to return.
        QTimer::singleShot(0,this,[this,manifest]{showDropMenu(manifest);});
    }
    void enterEvent(QEvent *e) override {
        if(!greetingHolds.isEmpty()&&QApplication::mouseButtons()==Qt::NoButton&&!LquiEffects::reducedMotion())
            showGreetingFrame(0);
        else update();
        QAbstractButton::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override {
        showGreetingFrame(-1);QAbstractButton::leaveEvent(e);
    }
    void hideEvent(QHideEvent *e) override {
        if(greetingFrame>=0)showGreetingFrame(-1);
        QAbstractButton::hideEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent *e) override {
        auto *m=new QMenu(this);m->setAttribute(Qt::WA_DeleteOnClose);
        m->addAction("打开 AI小站",this,[this]{launch(false);});
        m->addAction("AI小站设置…",this,[this]{launch(true);});
        m->addSeparator();
        auto addCapture=[this,m](const QString &label,const QString &name,int delay){
            auto *action=m->addAction(label,m,[this,m,delay]{
                m->close();
                // Give the compositor time to remove the menu in the quick path.
                QTimer::singleShot(delay,Qt::PreciseTimer,this,&Orb::launchCapture);
            });
            action->setObjectName(name);
        };
        addCapture("现在截图","aiStationCaptureNow",250);
        addCapture("延时 3 秒截图","aiStationCaptureDelayed",3000);
        m->popup(e->globalPos());
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const qreal size=qMin(petId.isEmpty()||petId=="claude"?30:35,height()-6);const QPointF c(width()/2.,height()/2.);QRectF r(c.x()-size/2,c.y()-size/2,size,size);
        if(!petFace.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            if(greetingFrame>=0) {
                const int side=greetingStrip.height();
                p.drawPixmap(r,greetingStrip,QRectF(greetingFrame*side,0,side,side));
            } else p.drawPixmap(r,petFace,QRectF(petFace.rect()));
        } else paintOrbSurface(p,r,underMouse()||hasFocus());
        if(dragOver){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#a0f3db"),1.6));p.drawEllipse(r.adjusted(-3,-3,3,3));}
        QFont font=p.font();font.setPixelSize(10);font.setBold(true);p.setFont(font);p.setPen(isDown()?QColor("#dcf5ff"):Qt::white);if(petFace.isNull())p.drawText(r,Qt::AlignCenter,"AI");
        if(dragOver) {
            // "+" badge: the orb will take what is being dragged.
            const qreal badge=7.;const QPointF at(r.right()-1,r.bottom()-1);
            p.setPen(QPen(QColor(10,60,45,170),1.));p.setBrush(QColor("#38d39f"));p.drawEllipse(at,badge,badge);
            p.setPen(QPen(Qt::white,1.8,Qt::SolidLine,Qt::RoundCap));
            p.drawLine(at-QPointF(3.6,0),at+QPointF(3.6,0));p.drawLine(at-QPointF(0,3.6),at+QPointF(0,3.6));
        }
    }
private:
    QTimer singleClick;bool skipRelease=false;
    QTimer greetingEnd;
    QString petId;
    QPixmap petFace,greetingStrip;
    QVector<int> greetingHolds;
    int greetingFrame=-1;

    // Decode just the selected role's tiny strip when switching pets. The
    // one-shot timer is only armed for a visible gesture, never at rest.
    void loadGreeting(const QString &avatar) {
        greetingEnd.stop();greetingFrame=-1;
        greetingHolds.clear();greetingStrip=QPixmap();
        if(petFace.isNull())return;
        const QDir dir=QFileInfo(avatar).dir();
        QFile file(dir.filePath("../greetings.json"));
        if(!file.open(QIODevice::ReadOnly))return;
        const auto role=QJsonDocument::fromJson(file.readAll()).object().value(petId).toObject();
        const QString image=role.value("file").toString();
        const auto holds=role.value("holdMs").toArray();
        if(image.isEmpty()||QFileInfo(image).fileName()!=image||holds.isEmpty())return;
        QVector<int> delays;
        for(const auto &value:holds) {
            const int ms=value.toInt();
            if(ms<=0)return;
            delays.append(ms);
        }
        const QPixmap strip(dir.filePath(image));
        if(strip.isNull()||strip.width()!=qint64(strip.height())*delays.size())return;
        greetingStrip=strip;greetingHolds=delays;
    }
    void showGreetingFrame(int index) {
        greetingEnd.stop();greetingFrame=-1;
        if(index>=0&&index<greetingHolds.size()){
            greetingFrame=index;greetingEnd.start(greetingHolds.at(index));
        }
        update();
    }
    void setDragOver(bool on){if(dragOver==on)return;dragOver=on;setProperty("dropHint",on);update();}
    void notify(const QString &text) {
        setToolTip(text);QToolTip::showText(mapToGlobal(QPoint(width()/2,0)),text,this);
        QTimer::singleShot(4000,this,[this]{setToolTip(petId.isEmpty()?QStringLiteral("AI小站 · 右键截图与设置"):QString());});
    }
    // Quick actions for what was just dropped, shown at the orb.
    void showDropMenu(const QString &manifest,const QRect &petAnchor={}) {
        for(auto *old:findChildren<QMenu*>("aiStationDropMenu"))old->close();
        const auto payload=StationDrop::read(manifest);if(payload.isEmpty())return;
        auto *menu=new QMenu(this);menu->setObjectName("aiStationDropMenu");menu->setAttribute(Qt::WA_DeleteOnClose);
        auto chosen=std::make_shared<bool>(false);
        for(const auto &action:StationDrop::actions(payload)) {
            if(action.id=="insert")menu->addSeparator();
            auto *item=menu->addAction(action.label);item->setObjectName("aiStationDrop_"+action.id);
            connect(item,&QAction::triggered,this,[this,menu,chosen,manifest,payload,id=action.id]{
                *chosen=true;menu->close();runDropAction(id,manifest,payload);
            });
        }
        connect(menu,&QMenu::aboutToHide,this,[chosen,manifest]{
            // A click on an item hides the menu before it triggers; decide after.
            QTimer::singleShot(0,[chosen,manifest]{if(!*chosen)StationDrop::discard(manifest);});
        });
        menu->ensurePolished();const QSize size=menu->sizeHint();
        const QRect orb=petAnchor.isValid()?petAnchor:QRect(mapToGlobal(QPoint()),this->size());
        const QScreen *screen=QGuiApplication::screenAt(orb.center());const QRect area=screen?screen->availableGeometry():QRect(orb.center()-QPoint(2000,2000),QSize(4000,4000));
        int y=orb.top()-size.height()-8;if(y<area.top())y=orb.bottom()+8;
        const int x=qBound(area.left(),orb.center().x()-size.width()/2,qMax(area.left(),area.right()-size.width()));
        menu->popup(QPoint(x,y));
    }
    void runDropAction(const QString &id,const QString &manifest,const QJsonObject &payload) {
        const QRect anchor(mapToGlobal(QPoint()),size());
        if(id=="todo"){addTodo(payload);StationDrop::discard(manifest);return;}
        sendDrop(manifest,anchor,0,id=="insert"?QString():id);
    }
    // Personal todos live in the calendar's own store; reuse its validated writer.
    void addTodo(QJsonObject payload) {
        const QString title=StationDrop::todoTitle(payload);
        if(title.isEmpty()){notify("没有可加入待办的文字");return;}
        const QString dir=qEnvironmentVariable("LQUI_CALENDAR_DIR",QDir::homePath()+"/.local/lib/ukui-panel-liquid/calendar");
        const QString helper=dir+"/drop_todo.py";
        if(!QFileInfo::exists(helper)){notify("未找到待办日期组件");return;}
        payload.insert("title",title);
        auto *process=new QProcess(this);
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process](int code,QProcess::ExitStatus status){
            process->deleteLater();
            if(status!=QProcess::NormalExit||code!=0){notify("加入待办失败，请重试");return;}
            const auto result=QJsonDocument::fromJson(process->readAllStandardOutput()).object();
            if(result.contains("date")&&result.value("date").isNull())return;  // User cancelled the date picker.
            const QDate date=QDate::fromString(result.value("date").toString(),Qt::ISODate);
            if(!date.isValid()){notify("无法确认待办保存结果，请查看日历");return;}
            notify("已加入 "+date.toString("yyyy年M月d日")+" 的待办");
        });
        connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError error){
            if(error==QProcess::FailedToStart){process->deleteLater();notify("无法启动待办组件");}
        });
        process->start("/usr/bin/python3",{"-I",helper});
        process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        process->closeWriteChannel();
    }
    Display *dropDisplay=nullptr;Atom positionAtom=0,copyAtom=0;
    bool launching=false,dragOver=false;
    void launchCapture(){
        const QRect anchor(mapToGlobal(QPoint()),size());
        auto call=QDBusMessage::createMethodCall("org.ukui.LiquidCli","/Cli","org.ukui.LiquidCli","CaptureAt");
        call.setArguments({anchor.x(),anchor.y(),anchor.width(),anchor.height()});
        auto *request=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call,1200),this);
        connect(request,&QDBusPendingCallWatcher::finished,this,[this,anchor](QDBusPendingCallWatcher *w){
            const QDBusPendingReply<> reply=*w;w->deleteLater();
            if(!reply.isError())return;
            const auto name=reply.error().name();
            if(name!="org.freedesktop.DBus.Error.ServiceUnknown"&&name!="org.freedesktop.DBus.Error.NameHasNoOwner")return;
            const QString a=QString("%1,%2,%3,%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height());
            if(!QProcess::startDetached(launcher(),{"--capture","--anchor",a}))setToolTip("无法打开 AI小站截图");
        });
    }
    void sendDrop(const QString &manifest,const QRect &anchor,int attempt=0,const QString &action=QString()) {
        if(!QFileInfo::exists(manifest))return;
        auto call=QDBusMessage::createMethodCall("org.ukui.LiquidCli","/Cli","org.ukui.LiquidCli",action.isEmpty()?"ImportDropAt":"ImportDropActionAt");
        if(action.isEmpty())call.setArguments({manifest,anchor.x(),anchor.y(),anchor.width(),anchor.height()});
        else call.setArguments({manifest,action,anchor.x(),anchor.y(),anchor.width(),anchor.height()});
        auto *watcher=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call,1500),this);
        connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,manifest,anchor,attempt,action](QDBusPendingCallWatcher *w){
            const QDBusPendingReply<bool> reply=*w;w->deleteLater();if(!reply.isError()){if(!reply.value())setToolTip("AI小站未能接收内容，请重新拖入");return;}
            const auto name=reply.error().name();
            if(name=="org.freedesktop.DBus.Error.ServiceUnknown"||name=="org.freedesktop.DBus.Error.NameHasNoOwner") {
                const QString a=QString("%1,%2,%3,%4").arg(anchor.x()).arg(anchor.y()).arg(anchor.width()).arg(anchor.height());
                QStringList arguments{"--drop",manifest,"--anchor",a};if(!action.isEmpty())arguments<<"--drop-action"<<action;
                if(!QProcess::startDetached(launcher(),arguments))setToolTip("无法启动 AI小站接收拖放");
            } else if(!action.isEmpty()&&name=="org.freedesktop.DBus.Error.UnknownMethod")sendDrop(manifest,anchor,attempt); // older AI小站: just insert
            else if(attempt<12&&(name=="org.freedesktop.DBus.Error.UnknownObject"||name=="org.freedesktop.DBus.Error.NoReply"))QTimer::singleShot(150,this,[this,manifest,anchor,attempt,action]{sendDrop(manifest,anchor,attempt+1,action);});
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
        button->setFixedSize(width,36);date->setFixedSize(width,36);button->setProperty("stationMinimalClock",true);button->installEventFilter(this);button->setToolTip("日历 · 待办 · 农历与节假日");refresh();
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
        if(!button)return false;
        // Consume the complete activation sequence before the OEM button's
        // handlers. The native calendar must never be opened as a fallback.
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::MouseButtonDblClick){
            auto *mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton){
                if(event->type()==QEvent::MouseButtonRelease && button->rect().contains(mouse->pos()))openCalendar();
                event->accept();return true;
            }
        }
        if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){
            auto *key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Space||key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){
                if(event->type()==QEvent::KeyRelease&&!key->isAutoRepeat())openCalendar();
                event->accept();return true;
            }
        }
        if(event->type()!=QEvent::Paint||!button)return false;
        QPainter p(button);p.setRenderHint(QPainter::TextAntialiasing);const int top=(button->height()-28)/2;
        p.setFont(metricFont(12,true));p.setPen(QColor("#e2e8f0"));p.drawText(QRect(0,top,button->width(),15),Qt::AlignRight|Qt::AlignVCenter,time);
        p.setFont(metricFont(10));p.setPen(QColor("#94a3b8"));p.drawText(QRect(0,top+15,button->width(),13),Qt::AlignRight|Qt::AlignVCenter,dateText);
        return true;
    }
private:
    void openCalendar(){
        if(lastCalendarOpen.isValid()&&lastCalendarOpen.elapsed()<250)return;
        lastCalendarOpen.start();
        if(!LquiCalendar::open(QRect(button->mapToGlobal(QPoint()),button->size())))
            button->setToolTip("独立日历尚未安装或无法启动，请重新安装 LQ UI 日历组件。");
    }
    QElapsedTimer lastCalendarOpen;
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
        if(property("liquidUnifiedStatus").toBool())return;
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QRectF r(.5,(height()-36)/2.,qMax(1,orb->x()-8),36);
        const bool hover=r.contains(mapFromGlobal(QCursor::pos()));p.setBrush(QColor(0,0,0,102));
        p.setPen(QPen(hover?QColor("#475569"):QColor(51,65,85,180),1));p.drawRoundedRect(r,10,10);
    }
private:
    void sample() {
        orb->refreshPet();
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
    qApp->setProperty("lquiCalendarGeometryEnabled",true);
    if(auto detach=reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT,"lqui_calendar_detach_preload")))detach();
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
    if(qApp){qApp->setProperty("lquiCalendarGeometryEnabled",false);qApp->removeEventFilter(this);}
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
void PanelAiStation::wireCalendarSchedule(QWidget *item) {
    auto *calendar=scheduleCalendar(item);if(!calendar)return;
    // The OEM double-click and menu slots both dereference an unrelated null
    // editor member. Keep its native row and replace only these edit routes.
    QObject::disconnect(item,SIGNAL(ScheduleClick(QString)),calendar,nullptr);
    QObject::disconnect(item,SIGNAL(EditSchedule(QString)),calendar,nullptr);
    QObject::connect(item,SIGNAL(ScheduleClick(QString)),this,SLOT(onCalendarScheduleEdit(QString)),Qt::UniqueConnection);
    QObject::connect(item,SIGNAL(EditSchedule(QString)),this,SLOT(onCalendarScheduleEdit(QString)),Qt::UniqueConnection);
}
void PanelAiStation::onCalendarScheduleEdit(QString id) {
    auto *item=qobject_cast<QWidget*>(sender());
    using Id=QString(*)(QWidget *);const auto getId=reinterpret_cast<Id>(calendarSymbol(item,"_ZN13schedule_item5getIdEv"));
    if(getId){const auto current=getId(item);if(!current.isEmpty())id=current;}
    if(auto *calendar=scheduleCalendar(item))queueCalendarEdit(calendar,id);
}
void PanelAiStation::queueCalendarEdit(QWidget *calendar,const QString &id) {
    if(!calendar||calendar->property("lquiCalendarEditPending").toBool())return;
    calendar->setProperty("lquiCalendarEditPending",true);QPointer<QWidget> owner(calendar);
    QTimer::singleShot(0,this,[owner,id]{
        if(!owner)return;owner->setProperty("lquiCalendarEditPending",false);
        auto fail=[owner]{if(owner)LiquidPopup::showText(owner->mapToGlobal(QPoint(20,50)),"系统日历编辑入口不可用，已阻止可能导致任务栏退出的操作。",owner,owner->rect());};
        using Factory=void(*)(QWidget *);using Mode=void(*)(QWidget *,int,bool);using Text=void(*)(QWidget *,QString);
        const auto factory=reinterpret_cast<Factory>(calendarSymbol(owner,"_ZN19LunarCalendarWidget15slotAddScheduleEv"));
        const auto mode=reinterpret_cast<Mode>(calendarSymbol(owner,"_ZN13CSchceduleDlg11setPropertyEib"));
        const auto setId=reinterpret_cast<Text>(calendarSymbol(owner,"_ZN13CSchceduleDlg5setIdE7QString"));
        const auto load=reinterpret_cast<Text>(calendarSymbol(owner,"_ZN13CSchceduleDlg15setStatusFromIdE7QString"));
        if(id.isEmpty()||!factory||!mode||!setId||!load){fail();return;}
        QSet<QWidget*> before;
        for(auto *w:QApplication::topLevelWidgets())if(w->inherits("CSchceduleDlg")){
            if(w->property("lquiScheduleEditId").toString()==id){w->show();w->raise();w->activateWindow();return;}
            before.insert(w);
        }
        // Use the OEM's safe dialog factory and ownership/cleanup connections,
        // then apply the same public edit-mode/id loaders used by its edit slot.
        factory(owner);QPointer<QWidget> editor;
        for(auto *w:QApplication::topLevelWidgets())if(w->inherits("CSchceduleDlg")&&!before.contains(w)){editor=w;break;}
        if(!editor){fail();return;}
        mode(editor,0,true);setId(editor,id);load(editor,id);
        editor->setProperty("lquiScheduleEditId",id);editor->show();editor->raise();editor->activateWindow();
    });
}
bool PanelAiStation::eventFilter(QObject *o,QEvent *e) {
    auto *calendar=qobject_cast<QWidget*>(o);
    if(calendar&&calendar->inherits("schedule_item")){
        if(e->type()==QEvent::Show||e->type()==QEvent::Polish||e->type()==QEvent::MouseButtonPress){
            wireCalendarSchedule(calendar);QPointer<QWidget> item(calendar);QTimer::singleShot(0,this,[this,item]{if(item)wireCalendarSchedule(item);});
        }
        if(e->type()==QEvent::MouseButtonDblClick){
            wireCalendarSchedule(calendar);
            using Id=QString(*)(QWidget *);const auto getId=reinterpret_cast<Id>(calendarSymbol(calendar,"_ZN13schedule_item5getIdEv"));
            queueCalendarEdit(scheduleCalendar(calendar),getId?getId(calendar):QString());e->accept();return true;
        }
    }
    if(calendar&&calendar->inherits("LunarCalendarWidget")&&e->type()==QEvent::Move&&calendar->isVisible()
        &&!calendar->property("lquiCalendarOpening").toBool()&&!calendar->property("lquiCalendarCorrecting").toBool()){
        const auto target=calendarTarget(calendar);if(target.isValid())positionCalendar(calendar,target.topLeft());
    }
    if(calendar&&calendar->inherits("LunarCalendarWidget")&&e->type()==QEvent::Hide){
        if(auto *motion=calendar->findChild<QVariantAnimation*>("lquiCalendarMotion"))motion->stop();
        calendar->setProperty("lquiCalendarOpening",false);calendar->setWindowOpacity(1);
    }
    if(calendar&&calendar->inherits("LunarCalendarWidget")&&(e->type()==QEvent::Show||e->type()==QEvent::Resize)){
        if(e->type()==QEvent::Show){calendar->setProperty("lquiCalendarOpening",true);calendar->setWindowOpacity(0);}
        if(calendar->property("lquiCalendarPositionQueued").toBool())return false;
        calendar->setProperty("lquiCalendarPositionQueued",true);
        QPointer<QWidget> guard(calendar);QTimer::singleShot(0,this,[guard]{
            if(guard)guard->setProperty("lquiCalendarPositionQueued",false);
            if(!guard||!guard->isVisible())return;
            const auto target=calendarTarget(guard);if(!target.isValid())return;const auto position=target.topLeft();
            const bool opening=guard->property("lquiCalendarOpening").toBool();guard->setProperty("lquiCalendarOpening",false);
            if(!opening){positionCalendar(guard,position);return;}
            // Hide the OEM screen-right placement until its show handler has
            // returned. Keep geometry fixed throughout the opacity transition.
            auto *motion=guard->findChild<QVariantAnimation*>("lquiCalendarMotion");
            if(!motion){motion=new QVariantAnimation(guard);motion->setObjectName("lquiCalendarMotion");}
            motion->stop();motion->disconnect();
            if(LquiEffects::reducedMotion(QSettings(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui/liquid-panel.ini",QSettings::IniFormat).value("appearance/reducedMotion",false).toBool())){
                positionCalendar(guard,position);guard->setWindowOpacity(1);return;
            }
            positionCalendar(guard,position);
            motion->setDuration(150);motion->setStartValue(0.);motion->setEndValue(1.);motion->setEasingCurve(QEasingCurve::OutCubic);
            connect(motion,&QVariantAnimation::valueChanged,guard,[guard](const QVariant &value){
                if(!guard||!guard->isVisible())return;const qreal t=value.toReal();
                guard->setWindowOpacity(t);
            });
            connect(motion,&QVariantAnimation::finished,guard,[guard]{if(guard){const auto target=calendarTarget(guard);if(target.isValid())positionCalendar(guard,target.topLeft());guard->setWindowOpacity(1);}});
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
    QJsonArray calendars;
    for(auto *w:QApplication::topLevelWidgets())if(w->inherits("LunarCalendarWidget")&&w->isVisible()&&w->windowHandle()){
        const auto r=w->windowHandle()->geometry();calendars.append(QJsonObject{{"x",r.x()},{"y",r.y()},{"width",r.width()},{"height",r.height()},{"dpr",w->devicePixelRatioF()},{"windowId",QString::number(w->winId())}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"clusters",array},{"orbs",orbs},{"desktopCorners",edge},{"systemEntries",systemEntries},{"calendars",calendars},{"calendarGeometryRedirects",qApp->property("lquiCalendarGeometryRedirects").toInt()},{"calendarPreloadInherited",qEnvironmentVariable("LD_PRELOAD").contains("liblqui-calendar-geometry-bridge.so")}}).toJson(QJsonDocument::Compact));
}

bool PanelAiStation::OpenPetChat(const QString &character) {
    if(PetEntry::label(character).isEmpty())return false;
    for(auto cluster:widgets)if(cluster&&cluster->isVisible()){
        auto *orb=cluster->findChild<QAbstractButton*>("aiStationOrb");
        if(orb){static_cast<Orb*>(orb)->petChat(character);return true;}
    }
    return false;
}

bool PanelAiStation::PetDrop(const QString &description) {
    if(description.size()>24*1024*1024)return false;
    const auto data=QJsonDocument::fromJson(description.toUtf8()).object();
    QMimeData mime;QList<QUrl> urls;
    for(const auto &value:data.value("files").toArray()) {
        const QString file=value.toString();
        if(QDir::isAbsolutePath(file))urls<<QUrl::fromLocalFile(file);
    }
    for(const auto &value:data.value("links").toArray()) {
        const QUrl url(value.toString());
        if(url.isValid()&&!url.scheme().isEmpty()&&!urls.contains(url))urls<<url;
    }
    if(!urls.isEmpty())mime.setUrls(urls);
    if(data.contains("text"))mime.setText(data.value("text").toString());
    if(data.contains("image")) {
        const QByteArray bytes=QByteArray::fromBase64(data.value("image").toString().toLatin1());
        QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);const QSize size=reader.size();
        if(!size.isValid()||qint64(size.width())*size.height()>32000000)return false;
        const QImage image=reader.read();if(image.isNull())return false;mime.setImageData(image);
    }
    for(auto cluster:widgets)if(cluster) {
        auto *orb=cluster->findChild<QAbstractButton*>("aiStationOrb");if(!orb)continue;
        const QString manifest=StationDrop::prepare(&mime);
        if(manifest.isEmpty())return false;
        static_cast<Orb*>(orb)->petDropMenu(manifest,QCursor::pos());return true;
    }
    return false;
}

bool PanelAiStation::OpenCalendar(){
    for(auto *widget:QApplication::allWidgets())if(widget->property("stationMinimalClock").toBool())
        return LquiCalendar::open(QRect(widget->mapToGlobal(QPoint()),widget->size()));
    return false;
}
