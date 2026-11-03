#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QHash>
#include <QCache>
#include <functional>
class QWidget;

namespace LquiSettings {
// Old menu entries route to the standalone application after installation.
bool open(const QString &page);
bool buildingForm();
class FormScope {public:FormScope();~FormScope();};

class Bridge final:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.kylin.Lqui.SettingsModule")
public:
    using Factory=std::function<QWidget*(const QString&)>;
    using CatalogFactory=std::function<QJsonArray()>;
    Bridge(QString module,Factory factory,CatalogFactory catalog,QObject *parent=nullptr);
    bool registerOnSession();
    ~Bridge() override;
public slots:
    Q_SCRIPTABLE QString Catalog();
    Q_SCRIPTABLE QString Page(const QString &page);
    Q_SCRIPTABLE QString Discard(const QString &page);
    Q_SCRIPTABLE QString Set(const QString &page,const QString &control,const QString &json);
    Q_SCRIPTABLE QString Activate(const QString &page,const QString &control);
private:
    QJsonObject snapshot(const QString &page);
    QString label(QWidget *,QWidget *) const;
    QString controlId(QWidget *);
    QString module;
    Factory factory;
    CatalogFactory catalog;
    QHash<QString,QPointer<QWidget>> roots;
    QHash<QString,QPointer<QWidget>> controls;
    // At most four encoded previews and 8 MiB; never a wallpaper/frame history.
    QCache<QWidget*,QPair<qint64,QString>> previews{8192};
    quint64 nextId=1;
    bool registered=false;
};
}
