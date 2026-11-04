#pragma once
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>
#include <QStyle>
#include <QLockFile>
#include <QVariant>
#include <QTimer>

namespace MenuOrder {
inline QString path(const QString &name) {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/peony-manager/" + name;
}
inline QJsonObject preferences() {
    QFile file(path("menu-order-contexts.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
inline bool saveOrder(const QString &scope, const QStringList &ids) {
    if (scope.isEmpty()) return false;
    const auto filename = path("menu-order-contexts.json");
    QDir().mkpath(QFileInfo(filename).absolutePath());
    QLockFile lock(filename + ".lock");
    if (!lock.tryLock(1000)) return false;
    QJsonObject root;
    QFile existing(filename);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) return false;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(existing.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
        root = document.object();
    }
    auto contexts = root["contexts"].toObject();
    if (ids.isEmpty()) contexts.remove(scope);
    else contexts[scope] = QJsonArray::fromStringList(ids);
    root["version"] = 2; root["contexts"] = contexts;
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto data = QJsonDocument(root).toJson();
    return file.write(data) == data.size() && file.commit();
}
inline QString label(QAction *action) {
    QString text = action->text().section('\t', 0, 0);
    text.remove(QRegularExpression("\\(&[A-Za-z]\\)"));
    text.remove('&');
    return text.trimmed();
}
inline QString canonicalKey(const QString &id) {
    if (id.startsWith("action:")) {
        const auto name = id.section(':', 1, 1);
        if (name.endsWith("-action")) return "id:" + name;
    }
    return id;
}
inline QString key(QAction *action) {
    const QString stableId = action->property("peonyMenuOrderId").toString();
    if (!stableId.isEmpty()) return "id:" + stableId;
    QString text = label(action);
    text.remove(QRegularExpression("\\s*[（(]\\d+\\s*项[）)]$"));
    // Peony overwrites every extension action objectName with its plugin name.
    // Include the label to distinguish multiple actions supplied by one plugin.
    return canonicalKey("action:" + action->objectName() + ":" + text);
}
inline QStringList order(const QString &scope) {
    QStringList result;
    for (const auto &value : preferences()["contexts"].toObject()[scope].toArray())
        if (value.isString() && !result.contains(value.toString())) result << value.toString();
    return result;
}
// A copy of exactly this menu's visible actions, safe after QMenu is destroyed.
inline QVariantList snapshot(const QList<QAction *> &actions) {
    QVariantList entries;
    for (auto *action : actions) {
        if (action->isSeparator() || !action->isVisible() || label(action).isEmpty()) continue;
        entries.append(QVariantMap{{"id", key(action)}, {"label", label(action)},
            {"icon", action->icon()}, {"enabled", action->isEnabled()}, {"submenu", action->menu() != nullptr}});
    }
    return entries;
}
inline QList<QAction *> sorted(QList<QAction *> actions, const QStringList &ids) {
    std::stable_sort(actions.begin(), actions.end(), [&ids](QAction *a, QAction *b) {
        int x = ids.indexOf(key(a)), y = ids.indexOf(key(b));
        return (x < 0 ? ids.size() : x) < (y < 0 ? ids.size() : y);
    });
    return actions;
}
inline void edit(const QString &scope, const QString &titleText, const QVariantList &entries, QWidget *parent = nullptr) {
    if (scope.isEmpty() || entries.isEmpty()) return;
    QDialog dialog(parent);
    dialog.setWindowTitle(titleText + QStringLiteral(" · 调整顺序"));
    dialog.setObjectName("menuOrderDialog");
    dialog.resize(580, 680);
    dialog.setMinimumSize(500, 480);
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog#menuOrderDialog { background:#f5f6f9; color:#20232b; }"
        "QDialog#menuOrderDialog QLabel { color:#242934; background:transparent; font-size:13px; }"
        "QDialog#menuOrderDialog QLabel#orderTitle { font-size:23px; font-weight:600; }"
        "QDialog#menuOrderDialog QLabel#orderHint { color:#757d8b; font-size:13px; }"
        "QListWidget#menuOrderList { background:white; color:#242934; border:1px solid #dce1e9; border-radius:10px; padding:6px; outline:0; font-size:14px; }"
        "QListWidget#menuOrderList::item { border:0; border-radius:6px; padding-left:9px; }"
        "QListWidget#menuOrderList::item:hover { background:#f0f5fc; }"
        "QListWidget#menuOrderList::item:selected { background:#e0edff; color:#185ca5; }"
        "QDialog#menuOrderDialog QPushButton { background:white; color:#344259; border:1px solid #d8dee8; border-radius:7px; padding:7px 16px; min-height:20px; font-size:13px; }"
        "QDialog#menuOrderDialog QPushButton:hover { background:#edf4ff; border-color:#9ebee8; }"
        "QDialog#menuOrderDialog QPushButton:disabled { color:#a3aab5; background:#f1f3f6; border-color:#e1e5eb; }"
        "QDialog#menuOrderDialog QPushButton#saveOrder { background:#348ff6; color:white; border-color:#348ff6; }"
        "QDialog#menuOrderDialog QPushButton#saveOrder:hover { background:#2682e9; }"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 20); layout->setSpacing(14);
    auto *title = new QLabel(titleText);
    title->setObjectName("orderTitle"); layout->addWidget(title);
    auto *hint = new QLabel(QStringLiteral("下面就是刚才右键菜单中的功能，顺序与菜单一致。拖动即可调整。"));
    hint->setObjectName("orderHint"); hint->setWordWrap(true); layout->addWidget(hint);
    auto *list = new QListWidget;
    list->setObjectName("menuOrderList");
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setIconSize(QSize(20, 20)); list->setSpacing(2);
    layout->addWidget(list, 1);
    for (const auto &value : entries) {
        const auto entry = value.toMap();
        QString text = entry["label"].toString();
        if (entry["submenu"].toBool()) text += QStringLiteral("   ›");
        QIcon icon = qvariant_cast<QIcon>(entry["icon"]);
        if (!entry["enabled"].toBool()) icon = QIcon(icon.pixmap(QSize(20,20), QIcon::Disabled));
        auto *item = new QListWidgetItem(icon, text, list);
        if (!entry["enabled"].toBool()) item->setForeground(QColor("#9299a4"));
        item->setData(Qt::UserRole, entry["id"]); item->setSizeHint(QSize(0, 38));
    }
    list->setCurrentRow(0);
    auto *row = new QHBoxLayout; layout->addLayout(row);
    auto *count = new QLabel(QStringLiteral("本次菜单 · %1 项功能").arg(list->count()));
    count->setObjectName("orderHint"); row->addWidget(count); row->addStretch();
    for (int direction : {-1, 1}) {
        auto *button = new QPushButton(direction < 0 ? QStringLiteral("上移") : QStringLiteral("下移"));
        row->addWidget(button);
        auto updateEnabled = [list, button, direction]() {
            const int to = list->currentRow() + direction;
            button->setEnabled(list->currentRow() >= 0 && to >= 0 && to < list->count());
        };
        QObject::connect(list, &QListWidget::currentRowChanged, &dialog, [updateEnabled](int) { updateEnabled(); });
        QObject::connect(list->model(), &QAbstractItemModel::rowsMoved, &dialog, [updateEnabled]() { updateEnabled(); });
        QObject::connect(list->model(), &QAbstractItemModel::rowsInserted, &dialog, [updateEnabled, &dialog]() { QTimer::singleShot(0, &dialog, updateEnabled); });
        QObject::connect(list->model(), &QAbstractItemModel::rowsRemoved, &dialog, [updateEnabled, &dialog]() { QTimer::singleShot(0, &dialog, updateEnabled); });
        updateEnabled();
        QObject::connect(button, &QPushButton::clicked, &dialog, [list, direction]() {
            const int from = list->currentRow(), to = from + direction;
            if (from < 0 || to < 0 || to >= list->count()) return;
            auto *item = list->takeItem(from); list->insertItem(to, item); list->setCurrentItem(item);
        });
    }
    auto *note = new QLabel(QStringLiteral("只保存这类右键菜单的顺序，其他菜单不受影响。灰色功能也可调整位置。"));
    note->setObjectName("orderHint"); note->setWordWrap(true); layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    buttons->button(QDialogButtonBox::Save)->setObjectName("saveOrder");
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("恢复此菜单默认顺序"));
    buttons->button(QDialogButtonBox::Save)->setEnabled(list->count() > 0);
    for (auto *button : buttons->buttons()) button->setIcon(QIcon());
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        QStringList result;
        for (int i = 0; i < list->count(); ++i) result.append(list->item(i)->data(Qt::UserRole).toString());
        if (saveOrder(scope, result)) dialog.accept();
        else QMessageBox::warning(&dialog, QStringLiteral("保存失败"), QStringLiteral("无法写入菜单排序配置，请检查配置目录权限。"));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&]() {
        if (saveOrder(scope, {})) dialog.accept();
        else QMessageBox::warning(&dialog, QStringLiteral("保存失败"), QStringLiteral("无法恢复默认顺序。"));
    });
    dialog.exec();
}
}
