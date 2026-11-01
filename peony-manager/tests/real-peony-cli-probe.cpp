// LD_PRELOAD probe for an isolated Peony instance on Xvfb. Never contacts Codex.
#include <QApplication>
#include <QMainWindow>
#include <QToolBar>
#include <QToolButton>
#include <QTabBar>
#include <QTabWidget>
#include <QDockWidget>
#include <QTimer>
#include <QJsonObject>
#include <QPointer>
#include <QDebug>
#include <QHeaderView>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>

static void check(bool value, const char *message) {
    if (!value) { fprintf(stderr, "FAIL: %s\n", message); ::exit(1); }
    fprintf(stderr, "PASS: %s\n", message);
}
static void later(std::function<void()> fn) { QTimer::singleShot(200, qApp, fn); }
static QMainWindow *host;
static QDockWidget *dock;
static QToolButton *expand;
static QTabWidget *sessions;
static QWidget *session;
static QRect expandedRect;
static void checkPosition(bool expanded = false) {
    auto *preview = host->findChild<QToolBar *>("previewButtons");
    check(preview, "real Peony preview toolbar exists");
    check(expand->isVisible() && !expand->visibleRegion().isEmpty(), "collapsed restore button is actually visible in real Peony");
    check(expand->parentWidget() == host, "shared toggle belongs to outer window");
    check(expand->parentWidget()->rect().contains(expand->geometry()), "restore button is not clipped");
    check(expand->text() == (expanded ? QStringLiteral("收起 CLI") : QStringLiteral("展开 CLI")), "shared toggle text reflects panel visibility");
    const QPoint previewPosition = preview->mapTo(host, QPoint());
    check(qAbs(expand->geometry().center().y() - (previewPosition.y() + preview->height() / 2)) <= 1, "shared toggle stays aligned with file-tab row");
    if (!expanded) check(expand->geometry().right() < previewPosition.x(), "collapsed toggle sits immediately before preview icon");
    auto *tabs = preview->parentWidget()->findChild<QTabBar *>();
    check(tabs && (expanded || tabs->mapTo(host, QPoint(tabs->width(), 0)).x() <= expand->x()), "native file tabs leave room for restore button");
    check(preview->width() == 26, "native fixed-width preview icon stays unchanged");
    if (expanded) {
        QHeaderView *header = nullptr;
        for (auto *candidate : host->centralWidget()->findChildren<QHeaderView *>())
            if (candidate->isVisible() && candidate->orientation() == Qt::Horizontal) { header = candidate; break; }
        check(header, "real details view has a visible column header");
        auto *cliTabs = sessions->findChild<QTabBar *>();
        const int fileTop = header->mapTo(host, QPoint()).y();
        const int cliTop = cliTabs->mapTo(host, QPoint()).y();
        fprintf(stderr, "alignment: file header y=%d h=%d, CLI tabs y=%d h=%d\n", fileTop, header->height(), cliTop, cliTabs->height());
        check(cliTop == fileTop && cliTabs->height() == header->height(), "CLI tabs match file column header top and height exactly");
        fprintf(stderr, "page: y=%d, tab sizehint=%d, tabs widget y=%d\n", sessions->widget(0)->mapTo(host, QPoint()).y(), cliTabs->sizeHint().height(), sessions->mapTo(host, QPoint()).y());
        check(sessions->widget(0)->mapTo(host, QPoint()).y() == fileTop + header->height(), "CLI content starts directly below the aligned row");
    }
}
int QApplication::exec() {
    QTimer::singleShot(1800, qApp, []() {
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
            fprintf(stderr, "%s\n", message.toUtf8().constData());
        });
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->inherits("MainWindow") && widget->isVisible()) host = qobject_cast<QMainWindow *>(widget);
        check(host, "test uses installed Peony MainWindow");
        auto *fileHost = host->findChild<QWidget *>("peony_TabWidget_initUI_m_tab");
        check(fileHost && QMetaObject::invokeMethod(fileHost, "switchViewType", Qt::DirectConnection,
              Q_ARG(QString, QStringLiteral("List View"))), "switch actual Peony to details view");
        // Peony itself uses the isolated bus; child scopes use the actual user manager.
        qputenv("DBUS_SESSION_BUS_ADDRESS", qgetenv("PEONY_TEST_USER_BUS"));
        void *runtime = dlopen(qgetenv("PEONY_TEST_RUNTIME").constData(), RTLD_NOW | RTLD_LOCAL);
        check(runtime, "updated runtime loads in real Peony");
        auto open = reinterpret_cast<void (*)(QMainWindow *, const QString &, const QJsonObject &)>(dlsym(runtime, "peony_cli_open"));
        check(open, "open panel entrypoint resolves");
        host->resize(1280, 800);
        open(host, qEnvironmentVariable("PEONY_TEST_DIRECTORY"), QJsonObject{{"command", "/bin/sleep 90"}, {"name", "CLI test"}});
        later([]() {
            dock = host->findChild<QDockWidget *>("peonyCliDock");
            sessions = dock ? dock->findChild<QTabWidget *>() : nullptr;
            check(sessions && sessions->count() == 1, "real Peony opens one mock CLI session");
            session = sessions->widget(0);
            expand = host->findChild<QToolButton *>("peonyCliExpandButton");
            check(expand, "shared toggle exists when panel is expanded");
            checkPosition(true);
            expandedRect = expand->geometry();
            if (!qEnvironmentVariable("PEONY_TEST_EXPANDED_CAPTURE").isEmpty())
                check(host->grab().save(qEnvironmentVariable("PEONY_TEST_EXPANDED_CAPTURE")), "expanded window screenshot saved");
            expand->click();
            later([]() {
                expand = host->findChild<QToolButton *>("peonyCliExpandButton");
                check(expand, "restore button exists");
                checkPosition();
                check(expand->geometry() == expandedRect, "collapse and expand use exactly the same window position");
                check(host->grab().save(qEnvironmentVariable("PEONY_TEST_CAPTURE")), "real collapsed window screenshot saved");
                expand->click();
                later([]() {
                    check(dock->isVisible() && expand->isVisible(), "shared toggle restores original panel");
                    checkPosition(true);
                    check(expand->geometry() == expandedRect, "restoring panel does not move the toggle");
                    check(sessions->count() == 1 && sessions->widget(0) == session, "expand reuses same session");
                    expand->click();
                    host->resize(1600, 900);
                    later([]() {
                        checkPosition();
                        host->resize(900, 700);
                        later([]() {
                            checkPosition();
                            check(QMetaObject::invokeMethod(sessions, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)), "close mock CLI tab");
                            later([]() {
                                check(!expand->isVisible(), "last session close removes restore button");
                                host->close();
                                qApp->quit();
                            });
                        });
                    });
                });
            });
        });
    });
    auto next = reinterpret_cast<int (*)()>(dlsym(RTLD_NEXT, "_ZN12QApplication4execEv"));
    return next();
}
