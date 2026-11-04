#include <QApplication>
#include <QMainWindow>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTimer>
#include <QPluginLoader>
#include <QUrl>
#include <dlfcn.h>
#include "../plugins/peony-window-tools/src/menu-order.h"
static void check(bool ok,const char *message) {fprintf(stderr,"%s: %s\n",ok?"PASS":"FAIL",message);if(!ok)::exit(1);}
static QMainWindow *window;
static QWidget *tabs;
static int contextIndex=0;
static bool verify=false;
static QStringList expected;
static const QStringList names{"空白处右键菜单","文件夹右键菜单","文件右键菜单","文件右键菜单","多选文件夹右键菜单","多选右键菜单"};
static const QStringList scopes{"background:file","folder","file:text/plain","file:image/png","folders","selection:text/plain"};
static void nextContext();
static void inspect() {
    auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
    check(menu && menu->inherits("Peony::DirectoryViewMenu"),"actual Peony context menu opened");
    const auto snapshot=MenuOrder::snapshot(menu->actions());
    QStringList keys; for(const auto &value:snapshot)keys<<value.toMap()["id"].toString();
    QAction *editor=nullptr;
    for(auto *a:menu->actions())if(a->property("peonyMenuOrderId").toString()=="peonyCustomizeMenuOrder")editor=a;
    check(editor,"actual context-menu editor entry exists");
    fprintf(stderr,"context %d: scope %s expected %s\n",contextIndex,qPrintable(editor->property("peonyMenuScope").toString()),qPrintable(scopes[contextIndex]));
    check(editor->property("peonyMenuScope").toString()==scopes[contextIndex],"file, folder and background classified independently");
    if(verify) {
        check(keys==expected,"saved order is applied to the exact real menu");
        menu->close(); verify=false; ++contextIndex;
        QTimer::singleShot(100,qApp,nextContext);return;
    }
    check(MenuOrder::order(scopes[contextIndex]).isEmpty(),"previous context does not change new context");
    expected=keys;expected.prepend(expected.takeLast());
    menu->close();
    QTimer::singleShot(100,qApp,[snapshot]() {
        auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->windowTitle().startsWith(names[contextIndex]),"editor identifies the right-clicked context");
        auto *list=dialog->findChild<QListWidget*>("menuOrderList");
        check(list && list->count()==snapshot.size(),"editor count exactly matches visible native actions");
        for(int i=0;i<list->count();++i) {
            auto entry=snapshot[i].toMap();
            check(list->item(i)->data(Qt::UserRole).toString()==entry["id"].toString(),"editor row corresponds to the same native action");
            QString label=entry["label"].toString();if(entry["submenu"].toBool())label+=QStringLiteral("   ›");
            check(list->item(i)->text()==label,"editor uses current menu label, not catalog label");
        }
        const auto capture=qEnvironmentVariable("PEONY_CONTEXT_CAPTURE");
        if(!capture.isEmpty())dialog->grab().save(capture+"/context-"+QString::number(contextIndex)+".png");
        auto *last=list->takeItem(list->count()-1);list->insertItem(0,last);list->setCurrentItem(last);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    editor->trigger();
    check(MenuOrder::order(scopes[contextIndex])==expected,"editor saves current menu order to its own scope");
    verify=true;QTimer::singleShot(100,qApp,nextContext);
}
static void nextContext() {
    if(contextIndex==scopes.size()) {
        for(const auto &scope:scopes)check(!MenuOrder::order(scope).isEmpty(),"all menu orders persist independently");
        qApp->quit();return;
    }
    QStringList selection;
    const QString base=QUrl::fromLocalFile(qEnvironmentVariable("PEONY_TEST_DIRECTORY")+"/0").toString();
    if(contextIndex==1)selection<<base+"/folder";
    if(contextIndex==2)selection<<base+"/sample.txt";
    if(contextIndex==3)selection<<base+"/picture.png";
    if(contextIndex==4)selection<<base+"/folder"<<base+"/folder2";
    if(contextIndex==5)selection<<base+"/sample.txt"<<base+"/second.txt";
    check(QMetaObject::invokeMethod(window,"setCurrentSelectionUris",Q_ARG(QStringList,selection)),"native selection updated");
    QTimer::singleShot(180,qApp,[](){
        QContextMenuEvent context(QContextMenuEvent::Mouse,QPoint(20,20));QApplication::sendEvent(window,&context);
        QTimer::singleShot(150,qApp,inspect);
        QMetaObject::invokeMethod(tabs,"menuRequest",Q_ARG(QPoint,QPoint(100,100)));
    });
}
extern "C" void testLoader(QPluginLoader*,const QString&,QObject*) asm("_ZN13QPluginLoaderC1ERK7QStringP7QObject");
extern "C" void testLoader(QPluginLoader *self,const QString &file,QObject *parent) {
    using Ctor=void(*)(QPluginLoader*,const QString&,QObject*);
    auto original=reinterpret_cast<Ctor>(dlsym(RTLD_NEXT,"_ZN13QPluginLoaderC1ERK7QStringP7QObject"));
    const bool replace=file.endsWith("/libpeony-window-tools.so")||(!QFile::exists("/usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-window-tools.so")&&file.endsWith("/libpeony-copy-path.so"));
    original(self,qEnvironmentVariableIsEmpty("PEONY_TEST_INSTALLED")&&replace?qEnvironmentVariable("PEONY_TEST_PLUGIN"):file,parent);
}
int QApplication::exec() {
    QTimer::singleShot(1800,qApp,[](){
        for(auto *w:QApplication::topLevelWidgets())if(w->inherits("MainWindow"))window=qobject_cast<QMainWindow*>(w);
        check(window,"isolated native Peony window found");
        for(auto *w:window->findChildren<QWidget*>())if(w->inherits("TabWidget"))tabs=w;
        check(tabs,"native file tab host found");nextContext();
    });
    QTimer::singleShot(20000,qApp,[](){check(false,"context probe timeout");});
    return reinterpret_cast<int(*)()>(dlsym(RTLD_NEXT,"_ZN12QApplication4execEv"))();
}
