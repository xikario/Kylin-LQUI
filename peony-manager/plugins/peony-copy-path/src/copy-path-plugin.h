#ifndef PEONY_COPY_PATH_PLUGIN_H
#define PEONY_COPY_PATH_PLUGIN_H

#include <QObject>
#include <QStringList>

#include "menu-plugin-iface.h"

namespace Peony {

class CopyPathPlugin : public QObject, public MenuPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID MenuPluginInterface_iid FILE "peony-copy-path.json")
    Q_INTERFACES(Peony::MenuPluginInterface)

public:
    explicit CopyPathPlugin(QObject *parent = nullptr);

    Peony::PluginInterface::PluginType pluginType() override;
    const QString name() override;
    const QString description() override;
    const QIcon icon() override;
    void setEnable(bool enable) override;
    bool isEnable() override;
    QString testPlugin() override;
    QList<QAction *> menuActions(Peony::MenuPluginInterface::Types types,
                                  const QString &uri,
                                  const QStringList &selectionUris) override;

private:
    QStringList pathsFromUris(const QString &currentUri, const QStringList &selectionUris) const;
    QString pathFromUri(const QString &uri) const;

    bool m_enabled = true;
};

}

#endif
