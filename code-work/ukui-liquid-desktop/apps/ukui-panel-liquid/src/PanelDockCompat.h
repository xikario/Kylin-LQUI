#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QVariant>
#include <QSize>
#include <QPoint>
#include <QVector>
#include "DockLayoutMath.h"
class QWidget;
class QScrollArea;
class QAbstractButton;
class QStyle;
class QLayout;

class PanelDockCompat final:public QObject {
    Q_OBJECT
public:
    explicit PanelDockCompat(QWidget *panel);
    ~PanelDockCompat() override;
    Q_INVOKABLE void schedule();
protected:
    bool eventFilter(QObject *,QEvent *) override;
private slots:
    void nativePanelRealigned();
    void updateActiveGroup();
private:
    struct Saved {QSize minimum,maximum,icon;QPointer<QStyle> style;QVariant animation,highlight;};
    struct Group {QPointer<QWidget> widget;QVector<QPointer<QAbstractButton>> buttons;};
    void attach();
    void apply();
    void restore();
    void remember(QWidget *);
    void updatePointer(const QPoint &global,bool allowed=true);
    void frame();
    void feedback(QWidget *button,bool pressed);
    void resetWave();
    bool enabled() const;
    QPointer<QWidget> m_panel,m_container,m_pages,m_content;
    QPointer<QScrollArea> m_bar;
    QPointer<QWidget> m_nativeOwner;
    void (*m_nativeRealign)(QWidget *)=nullptr;
    QSize m_nativePanelSize,m_nativeContainerSize;
    qreal m_nativeDpr=0;
    int m_nativeDirection=-1;
    QVariant m_nativePosition;
    bool m_nativeForwarding=false;
    QVector<Group> m_groups;
    QHash<QWidget*,Saved> m_saved;
    QTimer m_pending,m_frame;
    Qt::ScrollBarPolicy m_horizontalPolicy=Qt::ScrollBarAsNeeded;
    Qt::ScrollBarPolicy m_verticalPolicy=Qt::ScrollBarAsNeeded;
    QElapsedTimer m_clock,m_feedbackClock;
    QPointer<QWidget> m_clicked;
    bool m_pressed=false,m_hoverActive=false;
    DockLayout::Metrics m_metrics;
    QSize m_contentMinimum,m_contentMaximum,m_pagesMinimum,m_pagesMaximum;
    bool m_pagesOffscreen=false,m_pagesEnabled=true;
    int m_containerSpacing=-1;
    int m_accessoryWidth=-1;
    int m_lastWidth=-1,m_lastHeight=-1;
    qreal m_target=0;
    QPoint m_pointer;
    quint64 m_batches=0,m_frames=0;
    bool m_applying=false,m_managed=false;
};
