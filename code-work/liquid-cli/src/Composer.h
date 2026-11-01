#pragma once
#include <QFrame>
#include <QImage>
#include <QStringList>
class QPlainTextEdit;
class QPushButton;
class QHBoxLayout;
class QLabel;

class Composer:public QFrame {
    Q_OBJECT
public:
    explicit Composer(QWidget *parent=nullptr);
    void setBusy(bool busy);
    void setColors(bool light);
    void setBackendName(const QString &name);
    void addImage(const QString &path);
    void appendText(const QString &text);
    QStringList images() const{return attached;}
    QString text() const;
    void clear();
    void focusInput();
    QPlainTextEdit *editor() const{return input;}
    void setHint(const QString &text);
signals:
    void submit(QString text,QStringList images);
    void stopRequested();
    void captureRequested();
    void imagePasted(QImage image);
    void imageFileDropped(QString path);
protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    void fitHeight();
    void scheduleHeight();
    void trySubmit();
    void rebuildStrip();
    QPlainTextEdit *input=nullptr;
    QPushButton *capture=nullptr,*action=nullptr;
    QHBoxLayout *strip=nullptr;
    QWidget *stripHost=nullptr;
    QLabel *hint=nullptr;
    QStringList attached;
    bool working=false,lightSurface=false,heightQueued=false;
};
