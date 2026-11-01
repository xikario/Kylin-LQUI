#include "LiquidDialog.h"
#include <QApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QPushButton>
#include <QTimer>
#include <cstdlib>

static void check(bool ok,const char *message)
{
    if(!ok){qCritical()<<"FAIL:"<<message;std::exit(1);}
    qInfo()<<"PASS:"<<message;
}
static QPushButton *choice(QWidget *dialog,QMessageBox::StandardButton wanted)
{
    for(auto *button:dialog->findChildren<QPushButton *>())
        if(button->property("messageChoice").toInt()==wanted)return button;
    return nullptr;
}
static void key(QWidget *target,int code,Qt::KeyboardModifiers modifiers=Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress,code,modifiers),release(QEvent::KeyRelease,code,modifiers);
    QApplication::sendEvent(target,&press);
    QApplication::sendEvent(target,&release);
}
static void verify(int code,QMessageBox::StandardButtons buttons,
                   QMessageBox::StandardButton initial,QMessageBox::StandardButton focused,
                   QMessageBox::StandardButton expected,bool animated)
{
    LiquidPopup::theme().reducedMotion=!animated;
    auto *dialog=LiquidDialog::createMessage(nullptr,"移到回收站","确定要移动测试项目吗？",
                                            QMessageBox::Question,buttons,initial);
    int clicked=0,finished=0;
    for(auto *button:dialog->findChildren<QPushButton *>())
        QObject::connect(button,&QPushButton::clicked,dialog,[&]{++clicked;});
    QObject::connect(dialog,&QDialog::finished,dialog,[&]{++finished;});
    QTimer watchdog;watchdog.setSingleShot(true);
    bool timeout=false;
    QObject::connect(&watchdog,&QTimer::timeout,dialog,[&]{timeout=true;dialog->QDialog::done(QDialog::Rejected);});
    watchdog.start(1800);
    QTimer::singleShot(animated?240:30,dialog,[=]{
        dialog->activateWindow();
        QCoreApplication::processEvents();
        auto *initialButton=choice(dialog,initial);
        check(initialButton && initialButton->isDefault() && dialog->focusWidget()==initialButton,
              "opening preserves the caller's initial default and focus");
        auto *target=choice(dialog,focused);
        check(target,"requested message button exists");
        // Follow the real focus chain, including any title controls, rather
        // than invoking click() or assigning the dialog's result in the test.
        for(int i=0;i<12 && dialog->focusWidget()!=target;++i)key(dialog->focusWidget(),Qt::Key_Tab);
        check(dialog->focusWidget()==target,"Tab navigation reaches the requested button");
        key(dialog->focusWidget(),code);
    });
    const int result=dialog->exec();watchdog.stop();
    qInfo()<<"message key"<<code<<"initial"<<initial<<"focus"<<focused<<"result"<<result;
    check(!timeout && result==expected && finished==1,"keyboard returns the exact selected action once");
    check(clicked==(code==Qt::Key_Escape?0:1),"button activation emits exactly one native click");
    delete dialog;
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    check(app.setStyle(qEnvironmentVariable("UKUI_MESSAGE_TEST_STYLE","Fusion")),"requested style is available");
    LiquidPopup::install(app);
    for(bool animated:{false,true}) {
        for(int code:{Qt::Key_Return,Qt::Key_Enter,Qt::Key_Space}) {
            verify(code,QMessageBox::Yes|QMessageBox::No,QMessageBox::No,QMessageBox::Yes,QMessageBox::Yes,animated);
            verify(code,QMessageBox::Yes|QMessageBox::No,QMessageBox::No,QMessageBox::No,QMessageBox::No,animated);
            verify(code,QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes,QMessageBox::No,QMessageBox::No,animated);
            verify(code,QMessageBox::Ok|QMessageBox::Cancel,QMessageBox::Cancel,QMessageBox::Ok,QMessageBox::Ok,animated);
        }
        verify(Qt::Key_Escape,QMessageBox::Yes|QMessageBox::No,QMessageBox::No,QMessageBox::Yes,QMessageBox::No,animated);
    }
    return 0;
}
