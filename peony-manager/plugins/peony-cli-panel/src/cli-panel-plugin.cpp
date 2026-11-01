#include "cli-panel-plugin.h"
#include <QApplication>
#include <QAction>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>
#include <QMouseEvent>
#include <dlfcn.h>

static QString moduleDirectory() {
    Dl_info info{};
    dladdr(reinterpret_cast<void *>(&moduleDirectory), &info);
    return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath();
}

Peony::CliPanelPlugin::CliPanelPlugin(QObject *parent) : QObject(parent) {
    qApp->installEventFilter(this);
    // No terminal library, PTY, timer or child process exists until a menu click.
}

QMainWindow *Peony::CliPanelPlugin::host(QWidget *widget) const {
    while (widget) {
        if (auto *window = qobject_cast<QMainWindow *>(widget)) {
            // Exclude property windows and the separate Peony desktop process.
            if (window->inherits("MainWindow") || window->inherits("Peony::FMWindow") || window->property("peonyCliTestHost").toBool())
                return window;
            return nullptr;
        }
        widget = widget->parentWidget();
    }
    return nullptr;
}

bool Peony::CliPanelPlugin::eventFilter(QObject *watched, QEvent *event) {
    bool context = event->type() == QEvent::ContextMenu;
    if (event->type() == QEvent::MouseButtonPress)
        context = static_cast<QMouseEvent *>(event)->button() == Qt::RightButton;
    if (context) {
        if (auto *window = host(qobject_cast<QWidget *>(watched))) m_contextWindow = window;
        else m_contextWindow.clear();
    }
    return false;
}

QList<QAction *> Peony::CliPanelPlugin::menuActions(Types types, const QString &uri, const QStringList &selection) {
    if (!m_enabled || !types.testFlag(DirectoryView) || selection.size() > 1) return {};
    const QUrl target(selection.isEmpty() ? uri : selection.first());
    if (!target.isLocalFile()) return {};
    const QFileInfo folder(target.toLocalFile());
    if (!folder.isDir()) return {};
    QFile file(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/peony-manager/cli-profiles.json");
    QJsonObject config;
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) return {};
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
        config = document.object();
    }
    if (!config.value("enabled").toBool(true)) return {};
    QPointer<QMainWindow> window = m_contextWindow;
    if (!window) window = host(qApp->activeWindow());
    if (!window) return {}; // Never guess among multiple windows by directory.
    const QString directory = folder.absoluteFilePath();
    auto *action = new QAction(icon(), config.value("menuLabel").toString("在此打开 Codex CLI"), this);
    connect(action, &QAction::triggered, this, [this, window, directory, config]() {
        if (!window) return;
        if (!m_runtime.isLoaded()) {
            m_runtime.setFileName(moduleDirectory() + "/peony-cli-panel/libpeony-cli-terminal.so");
            m_runtime.setLoadHints(QLibrary::PreventUnloadHint);
        }
        using OpenPanel = void (*)(QMainWindow *, const QString &, const QJsonObject &);
        auto open = reinterpret_cast<OpenPanel>(m_runtime.resolve("peony_cli_open"));
        if (!open) {
            QMessageBox::warning(window, "右侧 CLI 终端", "终端组件未就绪，请在 Peony 插件管理器中安装完整插件。\n" + m_runtime.errorString());
            return;
        }
        open(window, directory, config);
    });
    return {action};
}
