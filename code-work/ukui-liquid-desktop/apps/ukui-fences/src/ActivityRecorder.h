#pragma once
#include "DeskletModels.h"
#include "ProcessMetrics.h"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QPointer>
#include <memory>
class QWidget;
class QDialog;
class ActivityRecorder final : public QObject {
    Q_OBJECT
public:
    explicit ActivityRecorder(QObject *parent = nullptr);
    ~ActivityRecorder() override;
    const ActivityLedger &ledger() const { return m_ledger; }
    bool isRecording() const { return m_recording; }
    bool saveOk() const { return m_saveOk; }
    void setRecording(bool enabled);
    QString dataPath() const;
    QString metricsDirectory() const;
    bool metricsEnabled() const{return m_metricsEnabled;}
    int metricsIntervalSeconds() const{return m_metricsInterval;}
    int logRetentionDays() const{return m_logRetention;}
    void configureMetrics(bool enabled,int intervalSeconds,int retentionDays);
    const QMap<QString,ProcessMetrics::Group> &liveResources() const{return m_liveResources;}
    QString csvForDay(const QDate &day) const;
    void showDetails(QWidget *parent=nullptr,const QString &appId={});
public slots:
    void sample();
    void flush();
    void setLocked(bool locked);
    void sessionLocked() { setLocked(true); }
    void sessionUnlocked() { setLocked(false); }
    void sampleResources();
signals:
    void changed();
private:
    friend struct ActivityMetricsTestAccess;
    void syncResourceSampling();
    void flushMetricLogs();
    void pruneMetricLogs();
    QHash<int,QString> windowProcessRoots();
    void acceptResources(const ProcessMetrics::Snapshot &,const QMap<QString,ProcessMetrics::Group> &,const QMap<QString,QString> &names);
    struct Native;
    std::unique_ptr<Native> m_native;
    ActivityLedger m_ledger;
    QElapsedTimer m_elapsed;
    QDateTime m_lastWall;
    QTimer m_sampleTimer, m_saveTimer;
    QString m_appId, m_appName;
    bool m_recording = true, m_locked = false, m_saveOk = true;
    bool m_metricsEnabled=true,m_metricsPending=false;
    int m_metricsInterval=10,m_logRetention=7;quint64 m_metricsRevision=0;
    QTimer m_metricsTimer;
    ProcessMetrics::Snapshot m_previousResources;
    QMap<QString,ProcessMetrics::Group> m_liveResources;
    struct LogBucket {QString id,name;QDateTime from,to;ResourceStats stats;};
    QMap<QString,LogBucket> m_pendingLogs;
    QPointer<QDialog> m_details;
};
