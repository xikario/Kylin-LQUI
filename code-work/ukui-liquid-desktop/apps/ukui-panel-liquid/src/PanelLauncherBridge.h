#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
class QWidget;
class PanelLauncherBridge final:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.LiquidPanelLauncher")
public:
    explicit PanelLauncherBridge(QObject *parent);
    ~PanelLauncherBridge() override;
public slots:
    Q_SCRIPTABLE bool Available() const;
protected:
    bool eventFilter(QObject *,QEvent *) override;
private:
    void scan();
    QTimer pending;
    QPointer<QWidget> button;
    QPointer<QWidget> pressed;
    Qt::MouseButton pressedButton=Qt::NoButton;
    QElapsedTimer contextTap;
    quint64 lastPressStamp=0;
    bool serviceOwned=false,contextAvailable=false;
};
