#pragma once
#include <QObject>
#include <QPointer>
#include <QLibrary>
#include <QHash>
#include <QMainWindow>
#include "menu-plugin-iface.h"

namespace Peony {
class CliPanelPlugin : public QObject, public MenuPluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID MenuPluginInterface_iid FILE "peony-cli-panel.json")
    Q_INTERFACES(Peony::MenuPluginInterface)
public:
    explicit CliPanelPlugin(QObject *parent = nullptr);
    PluginType pluginType() override { return MenuPlugin; }
    const QString name() override { return QStringLiteral("右侧 CLI 终端"); }
    const QString description() override { return QStringLiteral("按需展开原生轻量终端，随窗口关闭"); }
    const QIcon icon() override { return QIcon::fromTheme("utilities-terminal"); }
    void setEnable(bool value) override { m_enabled = value; }
    bool isEnable() override { return m_enabled; }
    QString testPlugin() override { return "peony-cli-panel"; }
    QList<QAction *> menuActions(Types types, const QString &uri, const QStringList &selection) override;
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QMainWindow *host(QWidget *widget) const;
    bool m_enabled = true;
    QPointer<QMainWindow> m_contextWindow;
    QLibrary m_runtime;
};
}
