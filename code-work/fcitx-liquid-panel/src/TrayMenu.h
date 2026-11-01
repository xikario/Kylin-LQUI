#pragma once
#include <QObject>
#include <QMenu>
#include <QTimer>
#include <QElapsedTimer>
class TrayMenu : public QObject {
    Q_OBJECT
public:
    explicit TrayMenu(QMenu &menu,QObject *parent=nullptr);
    void request(const QPoint &position);
    bool pending() const { return m_timer.isActive(); }
    static bool pointerButtonDown();
private:
    QMenu &m_menu;
    QTimer m_timer;
    QElapsedTimer m_wait;
    QPoint m_position;
};
