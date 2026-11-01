#include "copy-path-plugin.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QIcon>
#include <QUrl>

using namespace Peony;

CopyPathPlugin::CopyPathPlugin(QObject *parent)
    : QObject(parent)
{
    qDebug() << "Peony::CopyPathPlugin loaded";
}

Peony::PluginInterface::PluginType CopyPathPlugin::pluginType()
{
    return Peony::PluginInterface::MenuPlugin;
}

const QString CopyPathPlugin::name()
{
    return QStringLiteral("复制完整路径");
}

const QString CopyPathPlugin::description()
{
    return QStringLiteral("复制选中文件、文件夹或当前目录的完整本地路径。");
}

const QIcon CopyPathPlugin::icon()
{
    return QIcon::fromTheme(QStringLiteral("edit-copy"));
}

void CopyPathPlugin::setEnable(bool enable)
{
    m_enabled = enable;
}

bool CopyPathPlugin::isEnable()
{
    return m_enabled;
}

QString CopyPathPlugin::testPlugin()
{
    return QStringLiteral("peony-copy-path");
}

QList<QAction *> CopyPathPlugin::menuActions(Peony::MenuPluginInterface::Types types,
                                             const QString &uri,
                                             const QStringList &selectionUris)
{
    QList<QAction *> actions;
    qDebug() << "Peony::CopyPathPlugin::menuActions" << types << uri << selectionUris;
    if (!m_enabled) {
        return actions;
    }

    const bool supportedMenu = types.testFlag(Peony::MenuPluginInterface::DirectoryView)
        || types.testFlag(Peony::MenuPluginInterface::DesktopWindow);
    if (!supportedMenu) {
        return actions;
    }

    const QStringList paths = pathsFromUris(uri, selectionUris);
    if (paths.isEmpty()) {
        return actions;
    }

    const QString label = paths.size() > 1
        ? QStringLiteral("复制完整路径 (%1项)").arg(paths.size())
        : QStringLiteral("复制完整路径");

    auto *action = new QAction(icon(), label, this);
    connect(action, &QAction::triggered, this, [paths]() {
        QClipboard *clipboard = QApplication::clipboard();
        if (clipboard) {
            clipboard->setText(paths.join(QStringLiteral("\n")));
        }
    });
    actions << action;
    return actions;
}

QStringList CopyPathPlugin::pathsFromUris(const QString &currentUri, const QStringList &selectionUris) const
{
    QStringList sourceUris = selectionUris;
    if (sourceUris.isEmpty() && !currentUri.isEmpty()) {
        sourceUris << currentUri;
    }

    QStringList paths;
    for (const QString &itemUri : sourceUris) {
        const QString path = pathFromUri(itemUri);
        if (!path.isEmpty() && !paths.contains(path)) {
            paths << path;
        }
    }
    return paths;
}

QString CopyPathPlugin::pathFromUri(const QString &uri) const
{
    if (uri.isEmpty()) {
        return QString();
    }

    const QUrl url(uri);
    if (url.isValid() && url.isLocalFile()) {
        return url.toLocalFile();
    }
    if (uri.startsWith(QStringLiteral("file://"))) {
        return QUrl::fromPercentEncoding(uri.mid(7).toUtf8());
    }
    return uri;
}
