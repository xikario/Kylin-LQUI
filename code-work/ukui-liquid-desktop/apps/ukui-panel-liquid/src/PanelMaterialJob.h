#pragma once
#include <QObject>
#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QElapsedTimer>
#include <functional>

// Latest request wins. A driver wait is confined to a bounded child process.
class PanelMaterialJob final : public QObject {
public:
    using Done=std::function<void(const QImage &,bool)>;
    explicit PanelMaterialJob(QObject *parent):QObject(parent){}
    ~PanelMaterialJob() override;
    void request(const QImage &,const QJsonObject &,Done);
    void invalidate();
private:
    void start();
    QProcess *process=nullptr;
    QPointer<QProcess> retiring;
    QImage source;
    QJsonObject options;
    Done done;
    quint64 revision=0;
    QElapsedTimer failed;
};
