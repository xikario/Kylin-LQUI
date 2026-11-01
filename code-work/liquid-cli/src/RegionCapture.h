#pragma once
#include <QWidget>
#include <QPixmap>
#include <QPointer>
#include <functional>

// An on-demand overlay per monitor. No daemon, polling, or persistent full-screen input window.
class RegionCapture : public QWidget {
    Q_OBJECT
public:
    RegionCapture(const QRect &screen, QPixmap screenshot);
    static QList<RegionCapture*> begin(std::function<void(QImage)> complete);
    static QList<RegionCapture*> beginWithGeometry(std::function<void(QImage,QRect)> complete);
    QRect selection() const;
signals:
    void selected(QImage image,QRect globalSelection);
protected:
    void closeEvent(QCloseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QPixmap image;
    QPoint first, last;
    bool dragging=false;
};
