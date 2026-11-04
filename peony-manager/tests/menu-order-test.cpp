#include <QApplication>
#include <QTimer>
#include "../plugins/peony-window-tools/src/menu-order.h"
static void check(bool ok, const char *text) {
    fprintf(stderr, "%s: %s\n", ok ? "PASS" : "FAIL", text);
    if (!ok) ::exit(1);
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QAction copy(QStringLiteral("复制"), &app), paste(QStringLiteral("粘贴"), &app), hidden("hidden", &app), separator(&app);
    copy.setObjectName("copy-action"); paste.setObjectName("paste-action");
    paste.setEnabled(false); hidden.setVisible(false); separator.setSeparator(true);
    auto entries = MenuOrder::snapshot({&copy,&separator,&paste,&hidden});
    check(entries.size() == 2, "snapshot excludes hidden actions and separators, retains disabled actions");
    check(MenuOrder::order("folder").isEmpty(), "no fallback to old global order");
    MenuOrder::saveOrder("folder", {"id:copy-action"});
    MenuOrder::saveOrder("file:text/plain", {"id:paste-action"});
    QTimer::singleShot(100, &app, []() {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        auto *list = dialog->findChild<QListWidget *>("menuOrderList");
        check(list && list->count() == 2, "editor contains exactly supplied menu, not global defaults");
        check(list->item(0)->text() == QStringLiteral("复制") && list->item(1)->text() == QStringLiteral("粘贴"), "editor matches actual labels and order");
        check(list->item(1)->flags().testFlag(Qt::ItemIsEnabled), "unavailable menu action remains movable in editor");
        list->setCurrentRow(1);
        QPushButton *up=nullptr,*down=nullptr;
        for(auto *button:dialog->findChildren<QPushButton*>()) {
            if(button->text()==QStringLiteral("上移"))up=button;
            if(button->text()==QStringLiteral("下移"))down=button;
        }
        check(up && up->isEnabled(), "up enabled for last row"); up->click(); QApplication::processEvents();
        check(list->currentRow()==0 && !up->isEnabled() && down->isEnabled(), "move controls update after reordering");
        down->click(); QApplication::processEvents();
        check(list->currentRow()==1 && up->isEnabled() && !down->isEnabled(), "moving down updates both controls");
        up->click();
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    MenuOrder::edit("background:file",QStringLiteral("空白处右键菜单"),entries);
    check(MenuOrder::order("background:file")==QStringList{"id:paste-action","id:copy-action"}, "save affects current context only");
    check(MenuOrder::order("folder")==QStringList{"id:copy-action"} && MenuOrder::order("file:text/plain")==QStringList{"id:paste-action"}, "file and folder settings preserved independently");
    QTimer::singleShot(100,&app,[](){qobject_cast<QDialog*>(QApplication::activeModalWidget())->reject();});
    MenuOrder::edit("folder",QStringLiteral("文件夹右键菜单"),entries);
    check(MenuOrder::order("folder")==QStringList{"id:copy-action"}, "cancel leaves context unchanged");
    QTimer::singleShot(100,&app,[](){QApplication::activeModalWidget()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::RestoreDefaults)->click();});
    MenuOrder::edit("background:file",QStringLiteral("空白处右键菜单"),entries);
    check(MenuOrder::order("background:file").isEmpty() && !MenuOrder::order("folder").isEmpty(), "restore defaults resets only current menu");
    QFile broken(MenuOrder::path("menu-order-contexts.json"));broken.open(QIODevice::WriteOnly);broken.write("broken");broken.close();
    check(!MenuOrder::saveOrder("folder",{"x"}), "damaged preferences are not silently overwritten");
}
