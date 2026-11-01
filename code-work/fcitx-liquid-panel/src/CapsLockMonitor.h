#pragma once
#include <QObject>
#include <memory>

// XKB indicator events only: no key capture, grabs or idle polling.
class CapsLockMonitor final : public QObject {
    Q_OBJECT
public:
    explicit CapsLockMonitor(QObject *parent=nullptr);
    ~CapsLockMonitor() override;
    bool start();
    bool capsLock() const { return m_capsLock; }
signals:
    void changed(bool enabled);
private:
    void refresh();
    class Native;
    std::unique_ptr<Native> m_native;
    bool m_capsLock=false;
};
