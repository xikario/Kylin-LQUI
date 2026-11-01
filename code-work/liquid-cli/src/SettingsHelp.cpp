#include "SettingsHelp.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QCursor>
#include <QWidget>

SettingsHelp::SettingsHelp(QWidget *window):QObject(window),dialog(window) {
    setObjectName("cliSettingsHelpController");setProperty("delayMs",1000);
    delay.setSingleShot(true);delay.setInterval(1000);
    for(auto *widget:window->findChildren<QWidget*>())widget->setMouseTracking(true);
    window->setMouseTracking(true);qApp->installEventFilter(this);
    connect(&delay,&QTimer::timeout,this,[this]{
        if(!dialog||!dialog->isVisible()||!owner||QApplication::activePopupWidget())return;
        if(helpOwner(QApplication::widgetAt(QCursor::pos()))!=owner)return;
        const QString text=owner->property("settingsHelp").toString();if(text.isEmpty())return;
        LiquidPopup::showText(QCursor::pos()+QPoint(10,12),text,owner,owner->rect(),5000);shown=true;
    });
}
SettingsHelp::~SettingsHelp(){if(qApp)qApp->removeEventFilter(this);cancel();}
QWidget *SettingsHelp::helpOwner(QWidget *widget) const {
    if(!dialog||!widget||(widget!=dialog&&!dialog->isAncestorOf(widget)))return nullptr;
    for(auto *w=widget;w;w=w->parentWidget()) {
        if(!w->property("settingsHelp").toString().isEmpty())return w;
        if(w==dialog)break;
    }
    return nullptr;
}
void SettingsHelp::cancel(){delay.stop();owner=nullptr;if(shown){LiquidPopup::hideText();shown=false;}}
void SettingsHelp::track(QWidget *widget) {
    auto *next=helpOwner(widget);
    if(!next||!dialog->isVisible()||QApplication::activePopupWidget()){cancel();return;}
    const QPoint cursor=QCursor::pos();
    if(next==owner&&(cursor-origin).manhattanLength()<=4)return;
    cancel();owner=next;origin=cursor;delay.start();
}
bool SettingsHelp::eventFilter(QObject *object,QEvent *event) {
    auto *widget=qobject_cast<QWidget*>(object);if(!widget||!dialog)return false;
    const bool inside=widget==dialog||dialog->isAncestorOf(widget);
    if(inside&&event->type()==QEvent::ToolTip){event->accept();return true;}
    if(inside&&dialog->isVisible()&&(event->type()==QEvent::Enter||event->type()==QEvent::MouseMove))track(QApplication::widgetAt(QCursor::pos()));
    else if(inside&&event->type()==QEvent::Leave)QTimer::singleShot(0,this,[this]{track(QApplication::widgetAt(QCursor::pos()));});
    else if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::KeyPress||
            (widget==dialog&&(event->type()==QEvent::Hide||event->type()==QEvent::Close||event->type()==QEvent::WindowDeactivate)))cancel();
    return false;
}
