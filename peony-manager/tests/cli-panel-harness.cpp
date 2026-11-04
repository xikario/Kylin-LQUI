#include <QApplication>
#include <QMainWindow>
#include <QDockWidget>
#include <QTabWidget>
#include <QPluginLoader>
#include <QContextMenuEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QTextStream>
#include <QAction>
#include <QPointer>
#include <QRegularExpression>
#include <QLabel>
#include <QLayout>
#include <QFontMetricsF>
#include <QFontInfo>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QClipboard>
#include <QToolBar>
#include <QToolButton>
#include <QTabBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <unistd.h>
#include <signal.h>
#include "menu-plugin-iface.h"

// Match the installed Peony: hidden toolbars precede an unmanaged, fixed-width
// preview toolbar. Adding a QWidgetAction to any of these must not be required.
class FileArea : public QWidget {
public:
    FileArea() {
        for (const auto &name : {"fileNavigation", "hiddenTrashControls"}) {
            auto *hidden = new QToolBar(this);
            hidden->setObjectName(name);
            hidden->addAction("隐藏操作");
            hidden->hide();
        }
        fileTabs = new QTabBar(this);
        fileTabs->setObjectName("fileTabs");
        fileTabs->addTab("文件夹");
        controls = new QToolBar(this);
        controls->setObjectName("previewButtons");
        controls->setFixedSize(26, 30);
        controls->addAction("预览");
    }
protected:
    void resizeEvent(QResizeEvent *) override {
        controls->move(width() - 40, 6);
        fileTabs->setGeometry(2, 2, qMax(0, width() - 80), 40);
    }
private:
    QToolBar *controls;
    QTabBar *fileTabs;
};

static void require(bool condition, const char *message) {
    if (!condition) { qCritical("FAIL: %s", message); ::exit(1); }
}
static QByteArray content(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
static long pss() {
    const auto lines = content("/proc/self/smaps_rollup").split('\n');
    for (const auto &line : lines) if (line.startsWith("Pss:")) return line.simplified().split(' ').value(1).toLong();
    return 0;
}
static bool alive(int pid) {
    auto stat = content(QString("/proc/%1/stat").arg(pid));
    return !stat.isEmpty() && stat.mid(stat.lastIndexOf(')') + 2, 1) != "Z";
}
static QJsonObject marker(const QString &path) { return QJsonDocument::fromJson(content(path)).object(); }
static bool allDead(const QJsonObject &record) {
    for (const auto pid : record.value("pids").toArray()) if (alive(pid.toInt())) return false;
    return true;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    require(argc >= 3, "arguments");
    const QString pluginPath = QString::fromLocal8Bit(argv[1]), root = QString::fromLocal8Bit(argv[2]);
    const QString mode = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();
    const bool crash = mode == "crash";
    const bool managedArgv = mode == "managed-argv";
    const QString configPath = qEnvironmentVariable("XDG_CONFIG_HOME") + "/peony-manager/cli-profiles.json";
    QDir().mkpath(QFileInfo(configPath).absolutePath());
    QPluginLoader loader(pluginPath);
    auto *plugin = dynamic_cast<Peony::MenuPluginInterface *>(loader.instance());
    if (!plugin) qCritical() << loader.errorString();
    require(plugin, "plugin loads with menu interface");
    require(!content("/proc/self/maps").contains("qtermwidget"), "terminal not loaded at plugin startup");
    const long cold = pss();
    auto createWindow = [&]() {
        auto *window = new QMainWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->setProperty("peonyCliTestHost", true);
        window->setCentralWidget(new FileArea);
        auto *toolbar = window->addToolBar("文件导航");
        toolbar->setObjectName("windowControls");
        toolbar->setMovable(false);
        toolbar->addAction("后退");
        window->resize(1200, 700); window->show();
        return window;
    };
    QPointer<QMainWindow> a = createWindow(), b;
    auto open = [&](QMainWindow *window, const QString &directory, const QString &name) {
        QJsonObject settings;
        if (managedArgv) {
            settings = QJsonDocument::fromJson(content(root + "/managed-settings.fixture")).object();
            require(!settings.isEmpty(), "managed Codex settings fixture loaded");
        } else {
            settings = QJsonObject{{"enabled", true}, {"name", "测试 CLI"}, {"menuLabel", "测试入口"},
                {"command", "/usr/bin/python3 '" + root + "/child.py' '" + root + "/" + name + ".json'"}};
        }
        QFile file(configPath); require(file.open(QIODevice::WriteOnly), "config write");
        file.write(QJsonDocument(settings).toJson()); file.close();
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(1,1));
        QApplication::sendEvent(window->centralWidget(), &event);
        const QString uri = QUrl::fromLocalFile(directory).toString();
        require(plugin->menuActions(Peony::MenuPluginInterface::DirectoryView, "trash:///", {}).isEmpty(), "virtual paths excluded");
        require(plugin->menuActions(Peony::MenuPluginInterface::DirectoryView, uri, {uri, uri}).isEmpty(), "multiple folders excluded");
        const auto actions = plugin->menuActions(Peony::MenuPluginInterface::DirectoryView, uri, {});
        const QString expectedLabel = managedArgv ? settings.value("menuLabel").toString() : QStringLiteral("测试入口");
        require(actions.size() == 1 && actions.first()->text() == expectedLabel, "custom context action");
        actions.first()->trigger();
        qDeleteAll(actions);
    };
    const QString directory = root + "/目录 空格 ' $HOME ; test";
    require(QDir().mkpath(directory), "special directory creation");
    open(a, directory, managedArgv ? "managed" : "a");
    int stage = 0;
    int managedStage = 0;
    bool newlineKeysSent = false;
    QJsonObject recordA, recordB, recordC;
    QJsonObject managedRecord;
    QElapsedTimer timeout; timeout.start();
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&]() {
        require(timeout.elapsed() < 20000, "integration timeout");
        if (managedArgv) {
            if (managedStage == 0) {
                managedRecord = marker(root + "/managed.json");
                if (managedRecord.isEmpty()) return;
                require(managedRecord.value("cwd").toString() == directory, "managed Codex cwd passed literally");
                const QJsonArray expectedArgs{"--profile", "peony-cli"};
                require(managedRecord.value("argv").toArray() == expectedArgs, "managed Codex uses its independent profile without model overrides");
                require(managedRecord.value("loginEnv").toString() == "from login profile", "managed Codex retains login shell environment");
                require(managedRecord.value("pids").toArray().size() == 3, "managed fake Codex child processes recorded");
                auto *tabs = a->findChild<QTabWidget *>();
                require(tabs && tabs->count() == 1, "managed Codex starts one session");
                open(a, directory, "managed-again");
                require(tabs->count() == 1, "same model settings reuse managed Codex session");
                require(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)), "close managed Codex tab");
                require(tabs->count() == 0 && a, "managed session closes without closing Peony window");
                managedStage = 1;
                return;
            }
            if (managedStage == 1 && allDead(managedRecord)) {
                qInfo("PASS: managed model argv is literal, login-shell environment and cwd are preserved, scope cleanup works");
                app.exit(0);
            }
            return;
        }
        if (stage == 0) {
            recordA = marker(root + "/a.json"); if (recordA.isEmpty()) return;
            require(recordA.value("cwd").toString() == directory, "special path passed literally");
            require(content("/proc/self/maps").contains("qtermwidget"), "terminal loads on first click");
            auto *tabs = a->findChild<QTabWidget *>(); require(tabs && tabs->count() == 1, "one session");
            auto *page = tabs->widget(0);
            auto *status = page->findChild<QLabel *>("peonyCliStatus");
            require(status && status->isHidden(), "successful session has no path/status row");
            require(page->property("cliThemeBackground").value<QColor>() == a->palette().color(QPalette::Base), "terminal follows host background");
            QWidget *display = nullptr;
            for (auto *widget : page->findChildren<QWidget *>())
                if (QString::fromLatin1(widget->metaObject()->className()).contains("TerminalDisplay")) display = widget;
            require(display, "terminal text display found");
            auto *terminal = page->findChild<QWidget *>("peonyCliTerminal");
            const auto rendered = page->grab().toImage();
            require(rendered.pixelColor(1,1) == a->palette().color(QPalette::Base), "no gray frame at top-left edge");
            require(rendered.pixelColor(1,rendered.height()-2) == a->palette().color(QPalette::Base), "no gray frame at bottom-left edge");
            qInfo() << "display_background=" << display->palette().color(display->backgroundRole()) << "expected=" << a->palette().color(QPalette::Base);
            require(display->palette().color(display->backgroundRole()) == a->palette().color(QPalette::Base), "actual terminal display background matches host");
            QFontMetricsF metrics(display->font());
            const auto cell = metrics.horizontalAdvance("M");
            qInfo() << "terminal_font=" << display->font() << "actual=" << QFontInfo(display->font()).family() << "cell=" << cell << "CJK=" << metrics.horizontalAdvance(QStringLiteral("中"));
            require(qAbs(metrics.horizontalAdvance("i") - cell) < .2, "Latin letters use equal cells");
            require(qAbs(metrics.horizontalAdvance(QStringLiteral("中")) - 2 * cell) < 1, "Chinese glyph spans exactly two cells");
            const qreal rowHeight = metrics.height();
            const QPoint start(1, int(rowHeight/2)), end(int(cell*18), int(rowHeight/2));
            QMouseEvent selectStart(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent selectMove(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent selectEnd(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(display, &selectStart);
            QApplication::sendEvent(display, &selectMove);
            QApplication::sendEvent(display, &selectEnd);
            auto rightClick = [&]() -> QMenu * {
                QMouseEvent press(QEvent::MouseButtonPress, start, display->mapToGlobal(start), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
                QApplication::sendEvent(display, &press);
                auto *menu = page->findChild<QMenu *>("peonyCliContextMenu");
                require(menu && menu->isVisible(), "right click opens terminal context menu");
                return menu;
            };
            auto *menu = rightClick();
            auto *copy = menu->findChild<QAction *>("peonyCliCopy");
            require(copy && copy->isEnabled(), "copy enabled with selected terminal text");
            copy->trigger();
            require(QApplication::clipboard()->text().contains(QStringLiteral("当前文件夹")), "context copy writes selected text to clipboard");
            menu->close();
            QApplication::clipboard()->setText(QStringLiteral("右键粘贴测试_abc123"));
            // Retire closed menu before finding the second popup by name.
            menu->setObjectName("closedMenu");
            menu = rightClick();
            auto *paste = menu->findChild<QAction *>("peonyCliPaste");
            require(paste && paste->isEnabled(), "paste enabled with text clipboard");
            paste->trigger(); menu->close();
            require(!QFileInfo::exists(root + "/a.json.input"), "pasting text without newline does not submit it");
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
            QApplication::sendEvent(display, &enter);
            auto originalPalette = a->palette();
            auto darkPalette = originalPalette;
            darkPalette.setColor(QPalette::Base, QColor("#23272f"));
            darkPalette.setColor(QPalette::Text, QColor("#ecedf0"));
            a->setPalette(darkPalette);
            require(page->property("cliThemeBackground").value<QColor>() == QColor("#23272f"), "live dark theme update");
            require(page->property("cliThemeForeground").value<QColor>() == QColor("#ecedf0"), "live text color update");
            a->setPalette(originalPalette);
            const QString capture = qEnvironmentVariable("PEONY_CLI_TEST_CAPTURE");
            if (!capture.isEmpty()) QTimer::singleShot(150, page, [page, capture]() {
                require(page->grab().save(capture), "test preview image saved");
            });
            open(a, directory, "a"); require(tabs->count() == 1, "repeat click reuses session");
            auto *dock = a->findChild<QDockWidget *>("peonyCliDock");
            dock->hide();
            QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            auto *expand = a->findChild<QToolButton *>("peonyCliExpandButton");
            require(expand && expand->isVisible() && expand->text() == QStringLiteral("展开 CLI"), "collapsed session retains visible expand button");
            auto *preview = a->findChild<QToolBar *>("previewButtons");
            require(expand->parentWidget() == a, "shared toggle is independent of dock and file-area width");
            require(expand->geometry().right() < preview->mapTo(a, QPoint()).x(), "expand button is beside file-tab preview controls");
            require(expand->parentWidget()->rect().contains(expand->geometry()), "expand button fits within file area");
            require(preview->width() == 26 && preview->actions().count() == 1, "native preview toolbar stays unchanged");
            require(a->findChild<QToolBar *>("windowControls")->actions().count() == 1, "window controls toolbar stays unchanged");
            require(a->findChild<QToolBar *>("fileNavigation")->actions().count() == 1, "path toolbar stays unchanged");
            const QString windowCapture = qEnvironmentVariable("PEONY_CLI_WINDOW_CAPTURE");
            if (!windowCapture.isEmpty()) require(a->grab().save(windowCapture), "collapsed window preview saved");
            for (const auto pid : recordA.value("pids").toArray()) require(alive(pid.toInt()), "hide retains processes");
            const QRect collapsedControl = expand->geometry();
            expand->click();
            QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            require(dock->isVisible() && expand->isVisible() && expand->text() == QStringLiteral("收起 CLI"), "same toggle becomes collapse control");
            require(expand->geometry() == collapsedControl, "collapse and expand control occupy exactly the same rectangle");
            require(tabs->count() == 1 && tabs->widget(0) == page, "expand restores existing session");
            for (const auto pid : recordA.value("pids").toArray()) require(alive(pid.toInt()), "expand preserves existing processes");
            expand->click();
            require(!dock->isVisible() && expand->text() == QStringLiteral("展开 CLI"), "shared toggle collapses panel");
            qInfo("terminal_delta_pss_kib=%ld", pss() - cold);
            if (crash) { qInfo("crash-test-ready"); _exit(0); }
            b = createWindow(); open(b, root, "b"); stage = 1;
        } else if (stage == 1) {
            recordB = marker(root + "/b.json"); if (recordB.isEmpty()) return;
            if (!QFileInfo::exists(root + "/a.json.input")) return;
            require(QString::fromUtf8(content(root + "/a.json.input")) == QStringLiteral("右键粘贴测试_abc123"), "context paste reaches PTY with Chinese text intact");
            if (!QFileInfo::exists(root + "/a.json.rawready")) return;
            if (!newlineKeysSent) {
                auto *page = a->findChild<QTabWidget *>()->widget(0);
                QWidget *display = nullptr;
                for (auto *widget : page->findChildren<QWidget *>())
                    if (QString::fromLatin1(widget->metaObject()->className()).contains("TerminalDisplay")) display = widget;
                require(display, "terminal display found for newline test");
                QKeyEvent shortcut(QEvent::ShortcutOverride, Qt::Key_Return, Qt::ShiftModifier);
                shortcut.ignore();
                QApplication::sendEvent(display, &shortcut);
                require(shortcut.isAccepted(), "Shift+Enter overrides host shortcuts inside terminal");
                QKeyEvent shiftEnter(QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier, "\r");
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
                QKeyEvent keypadEnter(QEvent::KeyPress, Qt::Key_Enter, Qt::ShiftModifier | Qt::KeypadModifier, "\r");
                QKeyEvent ctrlJ(QEvent::KeyPress, Qt::Key_J, Qt::ControlModifier, "\n");
                QApplication::sendEvent(display, &shiftEnter);
                QApplication::sendEvent(display, &enter);
                QApplication::sendEvent(display, &keypadEnter);
                QApplication::sendEvent(display, &ctrlJ);
                newlineKeysSent = true;
                return;
            }
            if (!QFileInfo::exists(root + "/a.json.keys")) return;
            require(content(root + "/a.json.keys") == QByteArray("\n\r\n\n", 4), "Shift+Enter and Ctrl+J send LF, plain Enter preserves CR through actual PTY");
            qInfo("PASS: Shift+Enter inserts newline; Enter retains submit byte; keypad and Ctrl+J also verified");
            qInfo("PASS: right-click copy and paste work through clipboard and PTY");
            auto *tabs = a->findChild<QTabWidget *>();
            require(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 0)), "close terminal tab signal");
            QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            require(tabs->count() == 0 && a, "closing terminal tab leaves file-manager window alive");
            require(!a->findChild<QToolButton *>("peonyCliExpandButton")->isVisible(), "closed last tab has no stale expand control");
            stage = 2;
        } else if (stage == 2) {
            if (!allDead(recordA)) return;
            require(a, "tab close did not close its file-manager window");
            for (const auto pid : recordB.value("pids").toArray()) require(alive(pid.toInt()), "other window retained");
            qInfo("PASS: terminal tab close ends CLI and both ordinary and detached descendants");
            open(a, directory, "a2"); stage = 3;
        } else if (stage == 3) {
            recordC = marker(root + "/a2.json"); if (recordC.isEmpty()) return;
            a->close(); stage = 4;
        } else if (stage == 4) {
            if (!allDead(recordC)) return;
            require(!a, "window deleted");
            for (const auto pid : recordB.value("pids").toArray()) require(alive(pid.toInt()), "other window retained after window close");
            b->close(); stage = 5;
        } else if (stage == 5 && allDead(recordB)) {
            qInfo("PASS: borderless rendering, lazy load, special path, reuse, hide, tab/window cleanup");
            app.exit(0);
        }
    });
    app.setQuitOnLastWindowClosed(false);
    timer.start(100);
    return app.exec();
}
