#include "PanelLauncherBridge.h"
#include <QApplication>
#include <QAbstractButton>
#include <QContextMenuEvent>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QWidget>
#include <QMouseEvent>

namespace {
const QString service=QStringLiteral("org.ukui.LiquidPanelLauncher");
const QString contextService=QStringLiteral("org.ukui.LiquidLauncherContext");
bool nativeButton(QWidget *widget){return widget&&widget->objectName()=="StartMenuButton"&&widget->window()->inherits("UKUIPanel");}
}
PanelLauncherBridge::PanelLauncherBridge(QObject *parent):QObject(parent) {
    pending.setSingleShot(true);connect(&pending,&QTimer::timeout,this,&PanelLauncherBridge::scan);
    auto bus=QDBusConnection::sessionBus();auto *watch=new QDBusServiceWatcher(contextService,bus,QDBusServiceWatcher::WatchForOwnerChange,this);
    connect(watch,&QDBusServiceWatcher::serviceOwnerChanged,this,[this](const QString &,const QString &,const QString &owner){contextAvailable=!owner.isEmpty();});
    if(bus.interface())contextAvailable=bus.interface()->isServiceRegistered(contextService);
    qApp->installEventFilter(this);pending.start(0);
}
PanelLauncherBridge::~PanelLauncherBridge(){if(qApp)qApp->removeEventFilter(this);if(serviceOwned){auto bus=QDBusConnection::sessionBus();bus.unregisterService(service);bus.unregisterObject("/Launcher");}}
bool PanelLauncherBridge::Available() const{return bool(button);}
void PanelLauncherBridge::scan() {
    if(button)return;
    for(auto *widget:QApplication::allWidgets())if(nativeButton(widget)){button=widget;connect(widget,&QObject::destroyed,this,[this]{button.clear();if(serviceOwned){auto bus=QDBusConnection::sessionBus();bus.unregisterService(service);bus.unregisterObject("/Launcher");serviceOwned=false;}pending.start(0);});break;}
    if(button&&!serviceOwned){auto bus=QDBusConnection::sessionBus();if(bus.registerObject("/Launcher",this,QDBusConnection::ExportScriptableSlots)){serviceOwned=bus.registerService(service);if(!serviceOwned)bus.unregisterObject("/Launcher");}}
}
bool PanelLauncherBridge::eventFilter(QObject *object,QEvent *event) {
    auto *widget=qobject_cast<QWidget *>(object);
    if(!button&&(event->type()==QEvent::Show||event->type()==QEvent::Polish)&&!pending.isActive())pending.start(0);
    // QWindow receives the native release before QWidget receives its copy.
    // Clear the press only at the widget stage or the OEM handler can also
    // deliver WinKeyResponse for that same physical click.
    if(widget&&pressed&&event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent *>(event)->button()==pressedButton) {
        const bool own=widget==pressed;if(auto *native=qobject_cast<QAbstractButton *>(pressed.data()))native->setDown(false);pressed.clear();pressedButton=Qt::NoButton;if(own)return true;
    }
    if(contextAvailable&&nativeButton(widget)&&event->type()==QEvent::MouseButtonRelease) {
        const auto released=static_cast<QMouseEvent *>(event)->button();
        if(released==Qt::LeftButton||released==Qt::RightButton)return true;
    }
    // Keep the real button and its geometry. The OEM routes mouse clicks to
    // WinKeyResponse too, whose Super-key duplicate guard would swallow fast
    // repeated clicks; use a dedicated pointer command instead.
    if(contextAvailable&&nativeButton(widget)&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonDblClick)) {
        auto *mouse=static_cast<QMouseEvent *>(event);
        if(mouse->button()==Qt::LeftButton||mouse->button()==Qt::RightButton) {
            if(event->type()==QEvent::MouseButtonDblClick&&mouse->timestamp()!=0&&mouse->timestamp()==lastPressStamp)return true;
            lastPressStamp=mouse->timestamp();const bool left=mouse->button()==Qt::LeftButton;
            if(left){pressed=widget;pressedButton=Qt::LeftButton;}
            if(left){if(auto *native=qobject_cast<QAbstractButton *>(widget))native->setDown(true);widget->setProperty("liquidOrbPressFeedback",!widget->property("liquidOrbPressFeedback").toBool());}
            auto message=QDBusMessage::createMethodCall(contextService,"/StartButton",contextService,left?"Toggle":"Open");
            if(!left){contextTap.start();message<<mouse->globalPos().x()<<mouse->globalPos().y();}
            QDBusConnection::sessionBus().asyncCall(message);return true;
        }
    }
    if(contextAvailable&&nativeButton(widget)&&event->type()==QEvent::ContextMenu){
        auto *context=static_cast<QContextMenuEvent *>(event);
        if(context->reason()==QContextMenuEvent::Mouse&&contextTap.isValid()&&contextTap.elapsed()<300)return true;
        const QPoint position=context->reason()==QContextMenuEvent::Keyboard?widget->mapToGlobal(widget->rect().center()):context->globalPos();
        contextTap.start();
        auto message=QDBusMessage::createMethodCall(contextService,"/StartButton",contextService,"Open");message<<position.x()<<position.y();QDBusConnection::sessionBus().asyncCall(message);return true;
    }
    return false;
}
