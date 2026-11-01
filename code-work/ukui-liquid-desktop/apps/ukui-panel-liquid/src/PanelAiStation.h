#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
class QWidget;

// Adds the compact status/calendar/orb group and a small, non-activating
// show-desktop dock at the screen corner, using the window manager's EWMH API.
class PanelAiStation final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.AiStationPanel")
public:
    explicit PanelAiStation(QObject *parent);
    ~PanelAiStation() override;
public slots:
    Q_SCRIPTABLE QString State() const;
    Q_SCRIPTABLE bool OpenCalendar();
protected:
    bool eventFilter(QObject *,QEvent *) override;
private:
    void shutdown();
    void schedule();
    void attach();
    QTimer pending;
    QList<QPointer<QWidget>> widgets;
    QList<QPointer<QWidget>> corners;
    bool serviceOwned=false;
    bool stopping=false;
};
