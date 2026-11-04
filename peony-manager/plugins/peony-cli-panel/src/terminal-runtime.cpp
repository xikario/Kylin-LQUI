#include <qtermwidget.h>
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QTabWidget>
#include <QTabBar>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>
#include <QShortcut>
#include <QStandardPaths>
#include <QUuid>
#include <QMessageBox>
#include <QFontDatabase>
#include <QTemporaryFile>
#include <QEvent>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QClipboard>
#include <QMimeData>
#include <QToolBar>
#include <QToolButton>
#include <memory>
#include "terminal-appearance.h"
#include "codex-profile.h"
#include <dlfcn.h>
#include <unistd.h>
#include <sys/socket.h>

static QString runtimeDir() {
    Dl_info info{};
    dladdr(reinterpret_cast<void *>(&runtimeDir), &info);
    return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath();
}

static QStringList managedLaunchArguments(const QJsonObject &config, bool *valid) {
    *valid = true;
    const QString command = config.value("command").toString("codex");
    const QString managedCommand = config.value("managedCommand").toString();
    if (managedCommand.isEmpty() || command != managedCommand) return {};

    const QString model = config.value("model").toString();
    const QString effort = config.value("reasoningEffort").toString();
    static const QRegularExpression modelId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:/+-]{0,127}$"));
    static const QStringList efforts = {"none", "minimal", "low", "medium", "high", "xhigh", "max", "ultra"};
    QStringList expected;
    if (!model.isEmpty()) {
        if (!modelId.match(model).hasMatch()) { *valid = false; return {}; }
        expected << "--model" << model;
    }
    if (!effort.isEmpty()) {
        if (!efforts.contains(effort)) { *valid = false; return {}; }
        expected << "--config" << QString("model_reasoning_effort=\"%1\"").arg(effort);
    }

    if (config.contains("modelArgs")) {
        const QJsonValue value = config.value("modelArgs");
        if (!value.isArray()) { *valid = false; return {}; }
        const QJsonArray supplied = value.toArray();
        if (supplied.size() != expected.size()) { *valid = false; return {}; }
        for (int i = 0; i < supplied.size(); ++i) {
            if (!supplied.at(i).isString() || supplied.at(i).toString() != expected.at(i)) {
                *valid = false;
                return {};
            }
        }
    }
    return {"--profile", "peony-cli"};
}

static QByteArray hexArgument(const QString &value) {
    return value.toUtf8().toHex();
}

class SessionPage : public QWidget {
public:
    SessionPage(const QString &cwd, const QJsonObject &config, QWidget *parent) : QWidget(parent), settings(config) {
        setProperty("cliDirectory", cwd);
        const QString command = config.value("command").toString("codex");
        const QString managedCommand = config.value("managedCommand").toString();
        const QString codexExecutable = config.value("codexExecutable").toString();
        bool modelArgumentsValid = false;
        const QStringList modelArguments = managedLaunchArguments(config, &modelArgumentsValid);
        const bool directCodex = !managedCommand.isEmpty() && command == managedCommand
            && QFileInfo(codexExecutable).isAbsolute() && modelArgumentsValid && !modelArguments.isEmpty();
        QString launchKey = command;
        if (directCodex) {
            launchKey += QChar(0x1f);
            launchKey += modelArguments.join(QChar(0x1f)) + QChar(0x1f)
                + config.value("model").toString() + QChar(0x1f) + config.value("reasoningEffort").toString();
        }
        setProperty("cliCommand", launchKey);
        if (!modelArgumentsValid) setProperty("cliModelPreferencesIgnored", true);
        auto *layout = new QVBoxLayout(this);
        const int padding = qBound(0, config.value("contentPadding").toInt(10), 24);
        layout->setContentsMargins(padding, padding, padding, padding);
        layout->setSpacing(0);
        setAutoFillBackground(true);
        setBackgroundRole(QPalette::Base);
        status = new QLabel("正在准备终端…", this);
        status->setObjectName("peonyCliStatus");
        status->setWordWrap(true);
        status->setMargin(6);
        layout->addWidget(status);
        if (directCodex) {
            QString error;
            if (!ensurePeonyCodexProfile(config, &error)) {
                status->setText(error);
                setProperty("cliFinished", true);
                return;
            }
        }
        terminal = new QTermWidget(0, this);
        terminal->setObjectName("peonyCliTerminal");
        terminal->setHistorySize(qBound(100, config.value("historyLines").toInt(2000), 10000));
        terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
        terminal->setFlowControlEnabled(false);
        terminal->setAutoClose(false);
        terminal->setTerminalSizeHint(false);
        terminal->setMargin(0);
        terminal->setKeyboardCursorShape(QTermWidget::KeyboardCursorShape::IBeamCursor);
        terminal->setBlinkingCursor(false);
        applyAppearance();
        connect(terminal, &QTermWidget::receivedData, this, [this](const QString &chunk) {
            const QPalette current = palette();
            const auto replies = oscColorQueries.consume(
                chunk.toLatin1(),
                current.color(QPalette::Active, QPalette::Text),
                current.color(QPalette::Active, QPalette::Base));
            for (const auto &reply : replies)
                terminal->sendText(QString::fromLatin1(reply.constData(), reply.size()));
        });
        if (QDir("/usr/share/qtermwidget5/kb-layouts").exists()) terminal->setKeyBindings("default");
        else terminal->setKeyBindings(runtimeDir() + "/data/kb-layouts/default");
        layout->addWidget(terminal, 1);
        setFocusProxy(terminal);
        auto shortcut = [this](const QKeySequence &key, void (QTermWidget::*slot)()) {
            auto *action = new QShortcut(key, terminal);
            action->setContext(Qt::WidgetWithChildrenShortcut);
            connect(action, &QShortcut::activated, terminal, slot);
        };
        shortcut(QKeySequence("Ctrl+Shift+C"), &QTermWidget::copyClipboard);
        shortcut(QKeySequence("Ctrl+Shift+V"), &QTermWidget::pasteClipboard);
        terminal->installEventFilter(this);
        for (auto *widget : terminal->findChildren<QWidget *>())
            if (QString::fromLatin1(widget->metaObject()->className()).contains("TerminalDisplay")) widget->installEventFilter(this);
        connect(terminal, &QTermWidget::finished, this, [this]() {
            setProperty("cliFinished", true);
            status->setText("会话已结束；可关闭此页，再从目录右键重新打开。");
            status->show();
            releaseGuard();
        });
        const QString helper = runtimeDir() + "/peony-cli-session";
        if (!QFileInfo(helper).isExecutable() || !QFileInfo("/usr/bin/systemd-run").isExecutable()) {
            status->setText("缺少会话组件或 systemd-run，请重新安装终端插件。");
            setProperty("cliFinished", true);
            return;
        }
        unit = "peony-cli-" + QUuid::createUuid().toString(QUuid::Id128) + ".scope";
        setProperty("cliUnit", unit);
        QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (base.isEmpty()) {
            status->setText("用户运行目录不可用，无法建立终端会话。");
            setProperty("cliFinished", true);
            return;
        }
        server = new QLocalServer(this);
        server->setSocketOptions(QLocalServer::UserAccessOption);
        socketPath = base + "/" + unit.left(43) + ".sock";
        if (!server->listen(socketPath)) {
            status->setText("会话连接失败：" + server->errorString());
            setProperty("cliFinished", true);
            return;
        }
        connect(server, &QLocalServer::newConnection, this, [this, helper, cwd, config, directCodex, codexExecutable, modelArguments]() {
            while (server->hasPendingConnections()) {
                auto *connection = server->nextPendingConnection();
                struct ucred credentials{};
                socklen_t length = sizeof(credentials);
                const bool valid = getsockopt(connection->socketDescriptor(), SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0
                    && credentials.uid == getuid() && credentials.pid == guardPid;
                if (!valid) { connection->deleteLater(); continue; }
                guard = connection;
                guard->setParent(this);
                server->close();
                auto environment = QProcessEnvironment::systemEnvironment();
                QString path = environment.value("PATH");
                environment.insert("PATH", QDir::homePath() + "/.local/bin:" + path);
                environment.insert("TERM", "xterm-256color");
                environment.insert("COLORTERM", "truecolor");
                environment.insert("COLORFGBG", palette().color(QPalette::Base).lightness() < 128 ? "15;0" : "0;15");
                terminal->setEnvironment(environment.toStringList());
                terminal->setShellProgram(helper);
                // QTermWidget 0.14 expands shell-like arguments itself. Hex-encode
                // both user strings so filenames containing '$' remain literal.
                QStringList helperArguments;
                if (directCodex) {
                    helperArguments << "--run-argv" << unit << QString::fromLatin1(hexArgument(cwd))
                                    << QString::fromLatin1(hexArgument(codexExecutable));
                    for (const auto &argument : modelArguments)
                        helperArguments << QString::fromLatin1(hexArgument(argument));
                } else {
                    helperArguments << "--run" << unit << QString::fromLatin1(hexArgument(cwd))
                                    << QString::fromLatin1(config.value("command").toString("codex").toUtf8().toHex());
                }
                terminal->setArgs(helperArguments);
                terminal->setWorkingDirectory(QDir::homePath());
                prepared = true;
                status->hide();
                terminal->startShellProgram();
                terminal->setFocus();
            }
        });
        if (!QProcess::startDetached(helper, {"--guard", socketPath, QString::number(getpid()), unit}, QString(), &guardPid)) {
            status->setText("无法启动会话清理组件。");
            server->close();
            setProperty("cliFinished", true);
        }
        QTimer::singleShot(4000, this, [this]() {
            if (!prepared && !property("cliFinished").toBool()) {
                status->setText("会话清理组件连接超时，终端未启动。");
                setProperty("cliFinished", true);
                if (server) server->close();
            }
        });
    }
    ~SessionPage() override { releaseGuard(); }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && modifiers == Qt::ShiftModifier) {
                // The old keytab emits keypad Enter for Shift+Return. Send the
                // same LF byte as Ctrl+J so Codex inserts a newline; plain Enter
                // keeps its usual CR/submit mapping. Accept the override first
                // to keep file-manager shortcuts from taking this keystroke.
                if (event->type() == QEvent::KeyPress && prepared && !property("cliFinished").toBool())
                    terminal->sendText(QStringLiteral("\n"));
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::RightButton) {
                if (event->type() == QEvent::MouseButtonPress) showContextMenu(mouse->globalPos());
                return true; // Keep selected text and prevent clicks reaching CLI mouse reporting.
            }
        } else if (event->type() == QEvent::ContextMenu) {
            showContextMenu(static_cast<QContextMenuEvent *>(event)->globalPos());
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }
    bool event(QEvent *event) override {
        const bool appearance = event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
            || event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange;
        const bool result = QWidget::event(event);
        if (appearance && terminal) applyAppearance();
        return result;
    }
private:
    void showContextMenu(const QPoint &position) {
        if (contextMenu && contextMenu->isVisible()) return;
        auto *menu = new QMenu(this);
        contextMenu = menu;
        menu->setObjectName("peonyCliContextMenu");
        menu->setAttribute(Qt::WA_DeleteOnClose);
        auto *copy = menu->addAction("复制");
        copy->setObjectName("peonyCliCopy");
        copy->setShortcut(QKeySequence("Ctrl+Shift+C"));
        copy->setShortcutVisibleInContextMenu(true);
        copy->setEnabled(!terminal->selectedText(false).isEmpty());
        connect(copy, &QAction::triggered, terminal, &QTermWidget::copyClipboard);
        auto *paste = menu->addAction("粘贴");
        paste->setObjectName("peonyCliPaste");
        paste->setShortcut(QKeySequence("Ctrl+Shift+V"));
        paste->setShortcutVisibleInContextMenu(true);
        const auto *mime = QApplication::clipboard()->mimeData();
        paste->setEnabled(!property("cliFinished").toBool() && mime && mime->hasText());
        connect(paste, &QAction::triggered, this, [this]() {
            terminal->setFocus();
            terminal->pasteClipboard();
        });
        menu->popup(position);
    }
    void applyAppearance() {
        const QFont desiredFont = PeonyCliAppearance::terminalFont(window()->font(), settings);
        if (!fontApplied || appliedFont != desiredFont) {
            terminal->setTerminalFont(desiredFont);
            appliedFont = desiredFont;
            fontApplied = true;
        }
        const QPalette colors = palette();
        terminal->setPalette(colors);
        terminal->setAutoFillBackground(true);
        terminal->setBackgroundRole(QPalette::Base);
        const QColor background = colors.color(QPalette::Base), foreground = colors.color(QPalette::Text);
        // The old QTermWidget API consumes a colorscheme file. A uniquely named
        // temporary file avoids its global cache reusing stale theme colors.
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        std::unique_ptr<QTemporaryFile> next(new QTemporaryFile(directory + "/peony-cli-theme-XXXXXX.colorscheme"));
        if (next->open() && PeonyCliAppearance::writeScheme(*next, colors) && next->flush()) {
            terminal->setColorScheme(next->fileName());
            scheme = std::move(next);
        } else {
            terminal->setColorScheme(runtimeDir() + "/data/color-schemes/" +
                (background.lightness() < 128 ? "Linux.colorscheme" : "SolarizedLight.colorscheme"));
        }
        // Old QTermWidget leaves padding and partial character cells unpainted.
        // Fill those pixels with the same base color instead of its startup gray.
        for (auto *widget : terminal->findChildren<QWidget *>()) {
            if (!QString::fromLatin1(widget->metaObject()->className()).contains("TerminalDisplay")) continue;
            QPalette canvas = widget->palette();
            canvas.setColor(QPalette::Window, background);
            canvas.setColor(QPalette::Base, background);
            widget->setPalette(canvas);
            widget->setBackgroundRole(QPalette::Base);
            widget->setAutoFillBackground(true);
            widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
        }
        setProperty("cliThemeBackground", background);
        setProperty("cliThemeForeground", foreground);
    }
    void releaseGuard() {
        if (guard) {
            guard->abort();
            guard.clear();
        }
    }
    QLabel *status = nullptr;
    QTermWidget *terminal = nullptr;
    QLocalServer *server = nullptr;
    QPointer<QLocalSocket> guard;
    QPointer<QMenu> contextMenu;
    QString unit, socketPath;
    qint64 guardPid = -1;
    bool prepared = false;
    bool fontApplied = false;
    QFont appliedFont;
    QJsonObject settings;
    PeonyCliAppearance::OscColorQueryResponder oscColorQueries;
    std::unique_ptr<QTemporaryFile> scheme;
};

// Peony positions previewButtons itself and fixes its width to one icon. An
// independent window child avoids clipping and keeps both toggle states at the
// same window position even when the file area shrinks beside the CLI dock.
class CliRestoreButton : public QToolButton {
public:
    CliRestoreButton(QMainWindow *window, QWidget *anchor, QTabBar *fileTabs)
        : QToolButton(window), anchor(anchor), fileTabs(fileTabs),
          originalMaximum(fileTabs ? fileTabs->maximumWidth() : QWIDGETSIZE_MAX) {
        anchor->installEventFilter(this);
        anchor->parentWidget()->installEventFilter(this);
        parentWidget()->installEventFilter(this);
        if (fileTabs) fileTabs->installEventFilter(this);
        hide();
    }
    void setState(bool value, bool expanded) {
        active = value;
        this->expanded = expanded;
        setText(expanded ? QStringLiteral("收起 CLI") : QStringLiteral("展开 CLI"));
        setToolTip(expanded ? QStringLiteral("收起面板，保留会话；关闭标签页将结束会话")
                            : QStringLiteral("恢复已收起的 CLI，会话仍在运行"));
        setVisible(value);
        reposition();
        schedule();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        switch (event->type()) {
        case QEvent::Move: case QEvent::Resize: case QEvent::LayoutRequest:
        case QEvent::Show: case QEvent::Hide: case QEvent::FontChange:
        case QEvent::StyleChange:
            schedule(); break;
        default: break;
        }
        return false;
    }
private:
    void schedule() {
        if (pending) return;
        pending = true;
        QTimer::singleShot(0, this, [this]() { pending = false; reposition(); });
    }
    void reposition() {
        if (!anchor) return;
        if (!active) {
            if (fileTabs) fileTabs->setMaximumWidth(originalMaximum);
            return;
        }
        QPoint position = anchor->mapTo(parentWidget(), QPoint());
        const int rightInset = anchor == fileTabs ? 8 : anchor->parentWidget()->width() - anchor->x();
        position.setX(parentWidget()->width() - rightInset);
        const QSize size = sizeHint().expandedTo(QSize(0, anchor->height()));
        const int left = qMax(0, position.x() - size.width() - 4);
        setGeometry(left, position.y() + (anchor->height() - size.height()) / 2, size.width(), size.height());
        if (fileTabs) {
            const int tabLeft = fileTabs->mapTo(parentWidget(), QPoint()).x();
            fileTabs->setMaximumWidth(expanded ? originalMaximum : qMin(originalMaximum, qMax(0, left - tabLeft - 4)));
        }
        raise();
    }
    QPointer<QWidget> anchor;
    QPointer<QTabBar> fileTabs;
    int originalMaximum;
    bool active = false, expanded = false, pending = false;
};

// QDockWidget lays out a custom title from sizeHint(), so fixing the widget's
// height alone does not move the content beneath it on Peony's Qt style.
class CliHeaderBar : public QWidget {
public:
    explicit CliHeaderBar(QWidget *parent) : QWidget(parent) {}
    QSize sizeHint() const override {
        QSize size = QWidget::sizeHint();
        if (rowHeight >= 0) size.setHeight(rowHeight);
        return size;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
    void setRowHeight(int height) {
        if (rowHeight == height) return;
        rowHeight = height;
        updateGeometry();
    }
private:
    int rowHeight = -1;
};

class CliDock : public QDockWidget {
public:
    explicit CliDock(QMainWindow *window) : QDockWidget("CLI 终端", window), owner(window) {
        setObjectName("peonyCliDock");
        setAllowedAreas(Qt::RightDockWidgetArea);
        setFeatures(QDockWidget::NoDockWidgetFeatures);
        auto *bar = new CliHeaderBar(this);
        headerBar = bar;
        auto *row = new QHBoxLayout(bar);
        row->setContentsMargins(10, 2, 8, 2);
        title = new QLabel("CLI 终端", bar);
        row->addWidget(title, 1);
        setTitleBarWidget(bar);
        tabs = new QTabWidget(this);
        tabs->setTabsClosable(true);
        tabs->setDocumentMode(true);
        connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
            auto *page = tabs->widget(index);
            tabs->removeTab(index);
            delete page;
            if (!tabs->count()) {
                if (expandButton) expandButton->setState(false, false);
                hide();
            }
        });
        setWidget(tabs);
        window->addDockWidget(Qt::RightDockWidgetArea, this);
        // Peony's file-tab controls live inside the central TabWidget, separate
        // from the outer toolbar containing the window controls. Anchor to the
        // preview icon on the tab row without changing either native toolbar.
        auto *files = window->centralWidget();
        fileArea = files;
        const auto toolbars = files ? files->findChildren<QToolBar *>() : QList<QToolBar *>();
        QToolBar *toolbar = files ? files->findChild<QToolBar *>("previewButtons") : nullptr;
        auto *fileTabs = files ? files->findChild<QTabBar *>() : nullptr;
        nativeTabs = fileTabs;
        if (!toolbar && fileTabs) {
            const int tabTop = fileTabs->mapTo(window, QPoint()).y();
            const int tabBottom = tabTop + fileTabs->height();
            for (auto *candidate : toolbars) {
                if (!candidate->isVisible()) continue;
                const int top = candidate->mapTo(window, QPoint()).y();
                if (top < tabBottom && top + candidate->height() > tabTop) {
                    toolbar = candidate;
                    break;
                }
            }
        }
        // The file tab bar is also a useful anchor on Peony variants without a
        // preview toolbar. Never parent the restore button to the hidden CLI dock.
        QWidget *anchor = toolbar ? static_cast<QWidget *>(toolbar) : fileTabs;
        if (anchor) {
            expandButton = new CliRestoreButton(window, anchor, fileTabs);
            expandButton->setObjectName("peonyCliExpandButton");
            expandButton->setIcon(QIcon::fromTheme("utilities-terminal"));
            expandButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            expandButton->setAutoRaise(true);
            expandButton->setState(false, false);
            // Leave header room for the shared control drawn at the outer
            // window's right edge, independent of the dock's visibility.
            row->addSpacing(expandButton->sizeHint().width() + 48);
            connect(expandButton, &QToolButton::clicked, this, [this]() {
                if (!tabs->count()) return;
                if (isVisible()) { hide(); return; }
                show(); raise();
                tabs->currentWidget()->setFocus();
            });
        }
        // Scoped to the panel: Ctrl+C and CLI input don't trigger window shortcuts.
        connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            if (expandButton) expandButton->setState(tabs->count() > 0, visible);
            if (visible && tabs->currentWidget()) tabs->currentWidget()->setFocus();
            scheduleAlignment();
        });
        qApp->installEventFilter(this);
    }
    void open(const QString &cwd, const QJsonObject &config) {
        title->setText(config.value("name").toString("Codex CLI"));
        for (int i = 0; i < tabs->count(); ++i) {
            auto *page = tabs->widget(i);
            QString launchKey = config.value("command").toString("codex");
            bool modelArgumentsValid = false;
            const QStringList modelArguments = managedLaunchArguments(config, &modelArgumentsValid);
            const QString managedCommand = config.value("managedCommand").toString();
            const bool directCodex = !managedCommand.isEmpty() && launchKey == managedCommand
                && QFileInfo(config.value("codexExecutable").toString()).isAbsolute()
                && modelArgumentsValid && !modelArguments.isEmpty();
            if (directCodex) {
                launchKey += QChar(0x1f);
                launchKey += modelArguments.join(QChar(0x1f)) + QChar(0x1f)
                + config.value("model").toString() + QChar(0x1f) + config.value("reasoningEffort").toString();
            }
            if (page->property("cliDirectory").toString() == cwd && page->property("cliCommand").toString() == launchKey
                && !page->property("cliFinished").toBool()) {
                tabs->setCurrentIndex(i); show();
                if (expandButton) expandButton->setState(true, true);
                page->setFocus(); return;
            }
        }
        if (tabs->count() >= 6) {
            QMessageBox::information(this, "CLI 终端", "此窗口已有 6 个会话，请先关闭不需要的终端页。");
            return;
        }
        auto *page = new SessionPage(cwd, config, tabs);
        const QString label = QFileInfo(cwd).fileName().isEmpty() ? cwd : QFileInfo(cwd).fileName();
        const int index = tabs->addTab(page, label);
        tabs->setTabToolTip(index, cwd);
        tabs->setCurrentIndex(index);
        show();
        if (expandButton) expandButton->setState(true, true);
        if (tabs->count() == 1) {
            auto *window = qobject_cast<QMainWindow *>(parentWidget());
            const int percent = qBound(25, config.value("widthPercent").toInt(40), 65);
            window->resizeDocks({this}, {qMax(280, window->width() * percent / 100)}, Qt::Horizontal);
        }
        page->setFocus();
        scheduleAlignment();
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        switch (event->type()) {
        case QEvent::Move: case QEvent::Resize: case QEvent::LayoutRequest:
        case QEvent::Show: case QEvent::Hide: case QEvent::FontChange:
        case QEvent::StyleChange: {
            auto *widget = qobject_cast<QWidget *>(watched);
            // Ancestor moves can change the header's window coordinates while
            // the header itself receives no move event (e.g. async view setup).
            if (widget && (widget == owner || widget == this || widget == headerBar || widget == fileArea
                || (fileArea && fileArea->isAncestorOf(widget))))
                scheduleAlignment();
            break;
        }
        default: break;
        }
        return QDockWidget::eventFilter(watched, event);
    }
private:
    void scheduleAlignment() {
        if (alignmentPending) return;
        alignmentPending = true;
        QTimer::singleShot(0, this, [this]() { alignmentPending = false; alignHeaders(); });
    }
    void alignHeaders() {
        if (!isVisible() || !owner || !fileArea || !nativeTabs) return;
        auto *cliTabs = tabs->findChild<QTabBar *>();
        if (!cliTabs) return;
        // The active details view's horizontal header is the source of truth.
        // Ignore hidden views and sidebar headers; a preview table is farther
        // right than the file list and must not determine this alignment.
        QHeaderView *fileHeader = nullptr;
        for (auto *candidate : fileArea->findChildren<QHeaderView *>()) {
            if (!candidate->isVisible() || candidate->orientation() != Qt::Horizontal || candidate->height() <= 0) continue;
            if (!fileHeader || candidate->mapTo(owner, QPoint()).x() < fileHeader->mapTo(owner, QPoint()).x())
                fileHeader = candidate;
        }
        const int targetTop = fileHeader ? fileHeader->mapTo(owner, QPoint()).y()
            : fileArea->mapTo(owner, QPoint()).y() + nativeTabs->height();
        const int targetHeight = fileHeader ? fileHeader->height() : cliTabs->sizeHint().height();
        const int titleHeight = qMax(0, targetTop - mapTo(owner, QPoint()).y());
        headerBar->setRowHeight(titleHeight);
        if (cliTabs->height() != targetHeight) cliTabs->setFixedHeight(targetHeight);
    }
public:
    QTabWidget *tabs;
    QLabel *title;
    QPointer<CliRestoreButton> expandButton;
private:
    QPointer<QMainWindow> owner;
    QPointer<QWidget> fileArea;
    QPointer<QTabBar> nativeTabs;
    CliHeaderBar *headerBar = nullptr;
    bool alignmentPending = false;
};

extern "C" void peony_cli_open(QMainWindow *window, const QString &cwd, const QJsonObject &config) {
    if (!window || !QFileInfo(cwd).isDir()) return;
    auto *dock = static_cast<CliDock *>(window->findChild<QDockWidget *>("peonyCliDock", Qt::FindDirectChildrenOnly));
    if (!dock) {
        dock = new CliDock(window);
        auto *shortcut = new QShortcut(QKeySequence("Ctrl+Alt+T"), window);
        QObject::connect(shortcut, &QShortcut::activated, dock, [dock]() {
            if (dock->tabs->count()) dock->setVisible(!dock->isVisible());
        });
    }
    QJsonObject effective(config);
    // A fresh installation may not have opened the settings page yet.
    if (effective.value("managedCommand").toString().isEmpty()) {
        const QString executable = QStandardPaths::findExecutable("codex",
            QStringList{QDir::homePath() + "/.local/bin"} + qEnvironmentVariable("PATH").split(':'));
        const QString command = effective.value("command").toString("codex");
        QString quotedExecutable = executable;
        quotedExecutable.replace("'", "'\"'\"'");
        quotedExecutable = "'" + quotedExecutable + "'";
        if (!executable.isEmpty() && (command == "codex" || command == executable || command == quotedExecutable)) {
            effective.insert("command", command);
            effective.insert("managedCommand", command);
            effective.insert("codexExecutable", executable);
        }
    }
    dock->open(cwd, effective);
}
