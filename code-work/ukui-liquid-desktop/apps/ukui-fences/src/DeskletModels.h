#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>

// Qt5 adaptation of NextKde DeskCenter behaviour. See vendor/nextkde-desklets.
struct CountdownState {
    enum State { Ready, Running, Paused, Finished } state = Ready;
    qint64 durationMs = 25 * 60000, deadlineMs = 0, pausedMs = 0;
    qint64 remaining(qint64 now) const;
    void start(qint64 duration, qint64 now);
    void toggle(qint64 now);
    void cancel();
    bool expire(qint64 now); // True once, including restoration after a restart.
    QJsonObject toJson() const;
    static CountdownState fromJson(const QJsonObject &json);
};
struct ResourceStats {
    qint64 observedMs=0,samples=0;
    double cpuWeighted=0,rssWeighted=0,cpuPeak=-1,rssPeakMb=-1,lastCpu=-1,lastRssMb=-1;
    int processCount=0;QDateTime lastAt;
    void observe(double cpu,double rssMb,qint64 durationMs,int processes,const QDateTime &at);
    double cpuMean() const {return observedMs>0?cpuWeighted/observedMs:-1;}
    double rssMeanMb() const {return observedMs>0?rssWeighted/observedMs:-1;}
    QJsonObject toJson() const;
    static ResourceStats fromJson(const QJsonObject &);
};
struct AppDuration { QString name; qint64 ms = 0; ResourceStats resources; };
struct ActivityDay { qint64 uptimeMs = 0; QMap<QString, AppDuration> apps; };
class ActivityLedger {
public:
    QMap<QDate, ActivityDay> days;
    QString bootId;
    qint64 lastUptimeMs = 0;
    QDateTime startedAt;
    void addInterval(const QDateTime &end, qint64 duration, const QString &appId = {}, const QString &name = {});
    void observeBoot(const QString &id, qint64 uptime, const QDateTime &now);
    void addResources(const QDateTime &end,qint64 durationMs,const QString &id,const QString &name,double cpu,double rssMb,int processes);
    void prune(const QDate &today);
    QJsonObject toJson() const;
    static ActivityLedger fromJson(const QJsonObject &json);
};
