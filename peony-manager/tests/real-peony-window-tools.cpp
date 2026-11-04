// Runs only in an isolated Xvfb and D-Bus session. No user windows are touched.
#include <QApplication>
#include <QContextMenuEvent>
#include <QMainWindow>
#include <QDockWidget>
#include <QTabWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPluginLoader>
#include <QPointer>
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <dlfcn.h>
#include "menu-plugin-iface.h"
#include "../plugins/peony-window-tools/src/menu-order.h"
static void check(bool yes, const char *message) {
    fprintf(stderr, "%s: %s\n", yes ? "PASS" : "FAIL", message);
    if (!yes) ::exit(1);
}
static QList<QMainWindow *> windows() {
    QList<QMainWindow *> result;
    for (auto *w : QApplication::topLevelWidgets())
        if (w->inherits("MainWindow") && w->isVisible()) result << qobject_cast<QMainWindow *>(w);
    return result;
}
static QTabBar *bar(QMainWindow *window) {
    for (auto *b : window->findChildren<QTabBar *>()) if (b->inherits("NavigationTabBar")) return b;
    return nullptr;
}
static QWidget *tabs(QMainWindow *window) {
    for (auto *w : window->findChildren<QWidget *>()) if (w->inherits("TabWidget")) return w;
    return nullptr;
}
static Peony::MenuPluginInterface *plugin;
static QPointer<QMainWindow> target;
static QStringList expected;
static QStringList desiredMenuOrder;
static QString mode;
static QString uri(int i) { return QUrl::fromLocalFile(qEnvironmentVariable("PEONY_TEST_DIRECTORY") + "/" + QString::number(i)).toString(); }
static void merge() {
    check(windows().size() == 3, "three real Peony windows created");
    auto *targetBar = bar(target);
    check(targetBar && targetBar->count() == 1, "target starts with one native file tab");
    expected << targetBar->tabData(0).toString();
    auto sources = windows(); sources.removeOne(target);
    std::sort(sources.begin(), sources.end(), [](QMainWindow *a, QMainWindow *b) { return a->winId() < b->winId(); });
    for (auto *window : sources) {
        check(bar(window) && bar(window)->count() == 2, "source contains two tabs including duplicate URI");
        for (int i = 0; i < bar(window)->count(); ++i) expected << bar(window)->tabData(i).toString();
    }
    if (mode == "terminal") {
        auto *dock = new QDockWidget(sources.first()); dock->setObjectName("peonyCliDock");
        auto *sessions = new QTabWidget(dock); sessions->addTab(new QWidget, "running-session");
        dock->setWidget(sessions); sources.first()->addDockWidget(Qt::RightDockWidgetArea, dock);
    }
    if (mode == "invalid") bar(sources.first())->setTabData(0, QString());
    QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(20,20));
    QApplication::sendEvent(target, &context);
    auto actions = plugin->menuActions(Peony::MenuPluginInterface::DirectoryView, uri(0), {});
    check(actions.size() == 2 && actions[0]->isEnabled(), "merge and menu-order actions available");
    // Dismiss informational dialogs only inside this isolated test process.
    auto *dismiss = new QTimer(qApp);
    QObject::connect(dismiss, &QTimer::timeout, qApp, []() {
        for (auto *w : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(w)) box->accept();
    });
    dismiss->start(50);
    actions[0]->trigger();
    QTimer::singleShot(2200, qApp, []() {
        if (mode == "invalid") {
            check(windows().size() == 3 && bar(target)->count() == 1, "unreadable source URI leaves all windows and target tabs untouched");
            qApp->quit(); return;
        }
        check(target && windows().size() == (mode == "terminal" ? 2 : 1), "merge retains the clicked window and closes safe sources");
        if (mode == "terminal") {
            check(target->findChild<QDockWidget *>("peonyCliDock") == nullptr, "terminal remains attached to original source window");
            bool found = false;
            for (auto *w : windows()) if (w != target) found = w->findChild<QDockWidget *>("peonyCliDock") != nullptr;
            check(found, "source with running terminal is preserved");
        }
        check(bar(target)->count() == expected.size(), "all five tabs retained, including duplicates");
        for (int i = 0; i < expected.size(); ++i)
            check(bar(target)->tabData(i).toString() == expected[i], "native tab URI and order preserved");
        check(bar(target)->currentIndex() == 0, "active target tab preserved");
        // Native navigation continues to work after merge.
        check(QMetaObject::invokeMethod(tabs(target), "setCurrentIndex", Q_ARG(int, 4)), "merged tab remains selectable");
        check(bar(target)->currentIndex() == 4, "native current tab updated");
        auto actions = plugin->menuActions(Peony::MenuPluginInterface::DirectoryView, uri(0), {});
        if (mode == "terminal") { qApp->quit(); return; }
        check(!actions[0]->isEnabled(), "merge disabled with one remaining window");
        // Exercise the real DirectoryViewMenu and its actual Show event.
        QTimer::singleShot(250, qApp, []() {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            check(menu && menu->inherits("Peony::DirectoryViewMenu"), "real native directory context menu opened");
            check(!MenuOrder::order("background:file").isEmpty(), "background menu preference persisted");
            const auto actions = menu->actions();
            check(actions == MenuOrder::sorted(actions, MenuOrder::order("background:file")), "native menu obeys saved order");
            check(MenuOrder::key(actions.first()) == desiredMenuOrder.first(), "last native action really moved to first position");
            for (auto *a : actions) check(!a->isSeparator(), "custom order removes obsolete group separators");
            menu->close();
            QTimer::singleShot(100, qApp, []() { qApp->quit(); });
        });
        check(QMetaObject::invokeMethod(tabs(target), "menuRequest", Q_ARG(QPoint, QPoint(100,100))), "native context-menu signal invoked");
    });
}
// Redirect one extension only inside this isolated process. The installed
// PluginManager performs its own metadata validation and registration.
extern "C" void testPluginLoaderConstructor(QPluginLoader *, const QString &, QObject *)
    asm("_ZN13QPluginLoaderC1ERK7QStringP7QObject");
extern "C" void testPluginLoaderConstructor(QPluginLoader *self, const QString &fileName, QObject *parent) {
    using Constructor = void (*)(QPluginLoader *, const QString &, QObject *);
    auto original = reinterpret_cast<Constructor>(dlsym(RTLD_NEXT, "_ZN13QPluginLoaderC1ERK7QStringP7QObject"));
    const bool replace = fileName.endsWith("/libpeony-window-tools.so")
        || (!QFile::exists("/usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-window-tools.so") && fileName.endsWith("/libpeony-copy-path.so"));
    const QString path = qEnvironmentVariableIsEmpty("PEONY_TEST_INSTALLED") && replace ? qEnvironmentVariable("PEONY_TEST_PLUGIN") : fileName;
    original(self, path, parent);
}
int QApplication::exec() {
    QTimer::singleShot(1800, qApp, []() {
        check(windows().size() == 1, "isolated installed Peony launched");
        target = windows().first();
        auto *loader = new QPluginLoader(qEnvironmentVariable("PEONY_TEST_PLUGIN"), qApp);
        auto *instance = loader->instance();
        if (!instance) fprintf(stderr, "%s\n", loader->errorString().toUtf8().constData());
        plugin = qobject_cast<Peony::MenuPluginInterface *>(instance);
        check(plugin, "new plugin loads through the real Peony plugin interface");
        check(loader->metaData()["MetaData"].toObject()["version"].toString() == "3.2.2", "plugin declares the installed Peony ABI version");
        for (int i : {1, 2}) {
            QByteArray cmd = ("peony -f " + uri(i) + " " + uri(i)).toUtf8();
            check(QMetaObject::invokeMethod(qApp, "parseCmd", Q_ARG(quint32, 123), Q_ARG(QByteArray, cmd)), "native command creates source window");
        }
        mode = qEnvironmentVariable("PEONY_TEST_CASE");
        QTimer::singleShot(1400, qApp, []() {
            QTimer::singleShot(150, qApp, []() {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                check(menu && menu->inherits("Peony::DirectoryViewMenu"), "original native menu captured");
                bool mergeFound = false, editorFound = false;
                for (auto *a : menu->actions()) {
                    mergeFound |= a->property("peonyMenuOrderId").toString() == "peonyMergeAllTabs";
                    editorFound |= a->property("peonyMenuOrderId").toString() == "peonyCustomizeMenuOrder";
                }
                check(mergeFound && editorFound, "real native menu contains both new actions");
                for (auto *a : menu->actions()) if (!a->isSeparator()) desiredMenuOrder.prepend(MenuOrder::key(a));
                QStringList ids = desiredMenuOrder;
                check(MenuOrder::saveOrder("background:file", ids), "reverse menu order saved atomically");
                menu->close();
                QTimer::singleShot(100, qApp, merge);
            });
            QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(20,20));
            QApplication::sendEvent(target, &context);
            QMetaObject::invokeMethod(tabs(target), "menuRequest", Q_ARG(QPoint, QPoint(100,100)));
        });
    });
    QTimer::singleShot(18000, qApp, []() { check(false, "probe timed out"); });
    return reinterpret_cast<int (*)()>(dlsym(RTLD_NEXT, "_ZN12QApplication4execEv"))();
}
