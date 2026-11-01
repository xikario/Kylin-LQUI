#pragma once
#include <QObject>
#include <QJsonArray>
#include <QTimer>
#include <QDBusMessage>
class PanelNotificationHistory final:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.kylin.Lqui.Notifications")
public:
    explicit PanelNotificationHistory(QObject *parent=nullptr);
    ~PanelNotificationHistory() override;
public slots:
    Q_SCRIPTABLE QString List() const;
    Q_SCRIPTABLE bool Clear();
private slots:
    void received(const QDBusMessage &message);
private:
    void save();
    QString path;
    QJsonArray entries;
    QTimer pending;
};
