#include "window-tools-plugin.h"
#include "menu-order.h"
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QElapsedTimer>
#include <QMimeDatabase>

namespace {
QMainWindow *host(QWidget *widget) {
    while (widget) {
        if (widget->inherits("MainWindow")) return qobject_cast<QMainWindow *>(widget);
        widget = widget->parentWidget();
    }
    return nullptr;
}
QWidget *tabHost(QMainWindow *window) {
    if (!window) return nullptr;
    for (auto *widget : window->findChildren<QWidget *>())
        if (widget->inherits("TabWidget")) return widget;
    return nullptr;
}
QTabBar *tabBar(QMainWindow *window) {
    auto *tabs = tabHost(window);
    if (!tabs) return nullptr;
    for (auto *bar : tabs->findChildren<QTabBar *>())
        if (bar->inherits("NavigationTabBar")) return bar;
    return nullptr;
}
QStringList locations(QMainWindow *window) {
    QStringList result;
    auto *bar = tabBar(window);
    if (!bar) return result;
    for (int i = 0; i < bar->count(); ++i) result << bar->tabData(i).toString();
    return result;
}
QList<QMainWindow *> windows() {
    QList<QMainWindow *> result;
    for (auto *widget : QApplication::topLevelWidgets()) {
        auto *window = qobject_cast<QMainWindow *>(widget);
        if (window && window->inherits("MainWindow") && window->isVisible()) result << window;
    }
    std::sort(result.begin(), result.end(), [](QMainWindow *a, QMainWindow *b) { return a->winId() < b->winId(); });
    return result;
}
bool hasTerminal(QMainWindow *window) {
    for (auto *dock : window->findChildren<QDockWidget *>("peonyCliDock"))
        for (auto *tabs : dock->findChildren<QTabWidget *>())
            if (tabs->count() > 0) return true;
    return false;
}
// Peony exposes addPage as a Qt slot; wait for each native tab to appear before
// opening the next one. Never infer success from the invokeMethod return value.
class MergeJob : public QObject {
    struct Source { QPointer<QMainWindow> window; QStringList uris; };
    QPointer<QMainWindow> target;
    QList<Source> sources;
    QStringList expected;
    QStringList pending;
    QTimer timer;
    QElapsedTimer deadline;
    int sourceIndex = 0, uriIndex = 0, retained = 0;
    bool waiting = false;
public:
    explicit MergeJob(QMainWindow *window) : QObject(qApp), target(window), expected(locations(window)) {
        for (auto *other : windows())
            if (other != window) sources.append({other, locations(other)});
        timer.setInterval(80);
        connect(&timer, &QTimer::timeout, this, [this]() { step(); });
    }
    void start() {
        if (qApp->property("peonyMergeInProgress").toBool()) { deleteLater(); return; }
        if (!target || !tabHost(target) || expected.isEmpty()) { finish(QStringLiteral("当前 Peony 版本未提供可用的标签接口。")); return; }
        for (const auto &source : sources) {
            if (source.uris.isEmpty()) { finish(QStringLiteral("无法读取某个窗口的标签，已保留所有来源窗口。")); return; }
            for (const auto &uri : source.uris)
                if (uri.isEmpty() || !QUrl(uri).isValid() || QUrl(uri).scheme().isEmpty()) {
                    finish(QStringLiteral("某个标签的地址无法读取，已保留所有来源窗口。")); return;
                }
        }
        qApp->setProperty("peonyMergeInProgress", true);
        timer.start(); step();
    }
private:
    void finish(const QString &message = {}) {
        timer.stop(); qApp->setProperty("peonyMergeInProgress", false);
        if (target) { if (target->isMinimized()) target->showNormal(); target->raise(); target->activateWindow(); }
        if (!message.isEmpty()) QMessageBox::information(target, QStringLiteral("合并所有标签"), message);
        deleteLater();
    }
    void step() {
        if (!target) { finish(); return; }
        if (waiting) {
            if (locations(target) == pending) { expected = pending; waiting = false; ++uriIndex; }
            else if (deadline.elapsed() > 15000 || locations(target) != expected) {
                finish(QStringLiteral("标签未能完整打开，或合并期间标签发生了变化。已保留尚未合并完成的来源窗口，请检查后重试。"));
                return;
            } else return;
        }
        if (sourceIndex >= sources.size()) {
            finish(retained ? QStringLiteral("标签已合并。有 %1 个来源窗口因终端会话仍在运行、标签发生变化或窗口拒绝关闭而保留。请处理后手动关闭。 ").arg(retained) : QString());
            return;
        }
        auto &source = sources[sourceIndex];
        if (uriIndex == source.uris.size()) {
            if (source.window) {
                if (locations(source.window) != source.uris || hasTerminal(source.window)) ++retained;
                else if (!source.window->close()) ++retained;
            }
            ++sourceIndex; uriIndex = 0; return;
        }
        if (locations(target) != expected) {
            finish(QStringLiteral("合并期间目标窗口的标签发生变化，已停止并保留来源窗口。")); return;
        }
        pending = expected; pending << source.uris[uriIndex];
        waiting = true; deadline.start();
        if (!QMetaObject::invokeMethod(tabHost(target), "addPage", Qt::DirectConnection,
                Q_ARG(QString, source.uris[uriIndex]), Q_ARG(bool, false)))
            finish(QStringLiteral("当前 Peony 版本不支持打开标签，来源窗口已保留。"));
    }
};
}

WindowToolsPlugin::WindowToolsPlugin(QObject *parent) : QObject(parent) {
    qApp->installEventFilter(this);
}
bool WindowToolsPlugin::eventFilter(QObject *watched, QEvent *event) {
    if (!m_enabled) return false;
    bool context = event->type() == QEvent::ContextMenu;
    if (event->type() == QEvent::MouseButtonPress)
        context = static_cast<QMouseEvent *>(event)->button() == Qt::RightButton;
    if (context) m_context = host(qobject_cast<QWidget *>(watched));
    if (event->type() == QEvent::Show && watched->inherits("Peony::DirectoryViewMenu")) {
        auto *menu = qobject_cast<QMenu *>(watched);
        if (!menu) return false;
        auto actions = menu->actions();
        QAction *editor = nullptr;
        for (auto *action : actions) {
            if (action->parent() == this) action->setParent(menu);
            if (action->property("peonyMenuOrderId").toString() == "peonyCustomizeMenuOrder") editor = action;
        }
        if (!editor) return false; // No ordering outside the menu that owns our entry.
        const auto selection = editor->property("peonySelection").toStringList();
        const QUrl directory(editor->property("peonyDirectory").toString());
        QString scope, title;
        bool folder = false;
        for (auto *action : actions) if (action->objectName() == "open-in-new-tab-action") folder = true;
        if (!selection.isEmpty()) {
            const bool allLocal = std::all_of(selection.begin(), selection.end(), [](const QString &uri) { return QUrl(uri).isLocalFile(); });
            if (allLocal) folder = std::all_of(selection.begin(), selection.end(), [](const QString &uri) { return QFileInfo(QUrl(uri).toLocalFile()).isDir(); });
        }
        if (selection.isEmpty()) {
            scope = "background:" + directory.scheme(); title = QStringLiteral("空白处右键菜单");
        } else if (selection.size() > 1) {
            if (folder) { scope = "folders"; title = QStringLiteral("多选文件夹右键菜单"); }
            else {
                QStringList types;
                for (const auto &uri : selection) {
                    const auto mime = QMimeDatabase().mimeTypeForFile(QUrl(uri).path(), QMimeDatabase::MatchExtension).name();
                    if (!types.contains(mime)) types << mime;
                }
                types.sort(); scope = "selection:" + types.join("|"); title = QStringLiteral("多选右键菜单");
            }
        } else {
            if (folder) { scope = "folder"; title = QStringLiteral("文件夹右键菜单"); }
            else {
                const QString mime = QMimeDatabase().mimeTypeForFile(QUrl(selection.first()).path(), QMimeDatabase::MatchExtension).name();
                scope = "file:" + mime; title = QStringLiteral("文件右键菜单");
            }
        }
        const auto ids = MenuOrder::order(scope);
        if (!ids.isEmpty()) {
            // Keep original actions and their behavior. Separators have no rank.
            for (auto *action : actions) menu->removeAction(action);
            actions.erase(std::remove_if(actions.begin(), actions.end(), [](QAction *a) { return a->isSeparator(); }), actions.end());
            menu->addActions(MenuOrder::sorted(actions, ids));
        }
        // Associate this action with its own context, never a process-global menu.
        editor->setProperty("peonyMenuScope", scope);
        editor->setProperty("peonyMenuTitle", title);
    }
    return false;
}
QList<QAction *> WindowToolsPlugin::menuActions(Types types, const QString &uri, const QStringList &selection) {
    if (!m_enabled || !types.testFlag(DirectoryView)) return {};
    QPointer<QMainWindow> window = m_context;
    if (!window) window = host(qApp->activeWindow());
    if (!window) return {};
    auto *merge = new QAction(icon(), QStringLiteral("合并所有标签到此窗口"), this);
    merge->setObjectName("peonyMergeAllTabs");
    merge->setProperty("peonyMenuOrderId", "peonyMergeAllTabs");
    merge->setEnabled(windows().size() > 1 && !qApp->property("peonyMergeInProgress").toBool());
    connect(merge, &QAction::triggered, this, [window]() {
        if (window) (new MergeJob(window))->start();
    });
    auto *edit = new QAction(QIcon::fromTheme("preferences-system"), QStringLiteral("自定义菜单顺序…"), this);
    edit->setObjectName("peonyCustomizeMenuOrder");
    edit->setProperty("peonyMenuOrderId", "peonyCustomizeMenuOrder");
    edit->setProperty("peonyDirectory", uri);
    edit->setProperty("peonySelection", selection);
    connect(edit, &QAction::triggered, this, [window, edit]() {
        auto *menu = qobject_cast<QMenu *>(edit->parent());
        if (!menu) return;
        // Some native actions arrive asynchronously after Show. Snapshot at the
        // click itself, while this menu and its action state are still alive.
        MenuOrder::edit(edit->property("peonyMenuScope").toString(),
                        edit->property("peonyMenuTitle").toString(),
                        MenuOrder::snapshot(menu->actions()), window);
    });
    return {merge, edit};
}
