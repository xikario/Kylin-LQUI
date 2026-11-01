#pragma once
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QTimer>
class QWidget;

// One event-driven hover timer for the settings window, including its chrome.
class SettingsHelp final : public QObject {
public:
    explicit SettingsHelp(QWidget *dialog);
    ~SettingsHelp() override;
protected:
    bool eventFilter(QObject *,QEvent *) override;
private:
    QWidget *helpOwner(QWidget *) const;
    void track(QWidget *widget);
    void cancel();
    QPointer<QWidget> dialog,owner;
    QPoint origin;
    QTimer delay;
    bool shown=false;
};
