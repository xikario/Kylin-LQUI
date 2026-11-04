#pragma once
#include <QObject>
#include <QPointer>
#include <QMainWindow>
#include "menu-plugin-iface.h"

class WindowToolsPlugin : public QObject, public Peony::MenuPluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID MenuPluginInterface_iid FILE "peony-window-tools.json")
    Q_INTERFACES(Peony::MenuPluginInterface)
public:
    explicit WindowToolsPlugin(QObject *parent = nullptr);
    Peony::PluginInterface::PluginType pluginType() override { return Peony::PluginInterface::MenuPlugin; }
    const QString name() override { return QStringLiteral("菜单排序与标签合并"); }
    const QString description() override { return QStringLiteral("自定义右键菜单顺序，将多个文件管理窗口的标签合并到当前窗口。"); }
    const QIcon icon() override { return QIcon::fromTheme("tab-new"); }
    void setEnable(bool enable) override { m_enabled = enable; }
    bool isEnable() override { return m_enabled; }
    QString testPlugin() override { return "peony-window-tools"; }
    QList<QAction *> menuActions(Types types, const QString &, const QStringList &) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    bool m_enabled = true;
    QPointer<QMainWindow> m_context;
};
