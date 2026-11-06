#pragma once
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QTimer>
#include <QSize>
#include <QLayout>
class QWidget;
class QAbstractButton;
class PanelSystemControls final:public QObject {
    Q_OBJECT
public:
    explicit PanelSystemControls(QObject *parent=nullptr);
    ~PanelSystemControls() override;
    Q_INVOKABLE void sync();
protected:bool eventFilter(QObject *,QEvent *) override;
private:
    void schedule();
    void restore();
    struct Saved {QPointer<QAbstractButton> widget;QSize minimum,maximum,icon;};
    QHash<QAbstractButton*,Saved> originals;
    struct Host {QPointer<QWidget> widget;int minimum,maximum;};
    QHash<QWidget*,Host> hosts;
    struct Bounds {QPointer<QWidget> widget;QSize minimum,maximum;};
    QHash<QWidget*,Bounds> bounds;
    QPointer<QWidget> home,capsule,cluster;
    QTimer pending;
    quint64 activationRevision=0;
    bool applying=false;
};
