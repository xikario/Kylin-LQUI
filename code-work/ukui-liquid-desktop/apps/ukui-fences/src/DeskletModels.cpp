#include "DeskletModels.h"
#include <QtGlobal>
#include <cmath>

qint64 CountdownState::remaining(qint64 now) const {
    if (state == Running) return qMax<qint64>(0, deadlineMs - now);
    if (state == Paused) return pausedMs;
    return state == Finished ? 0 : durationMs;
}
void CountdownState::start(qint64 duration, qint64 now) {
    durationMs = qBound<qint64>(1000, duration, 24LL * 3600000);
    deadlineMs = now + durationMs; pausedMs = 0; state = Running;
}
void CountdownState::toggle(qint64 now) {
    if (state == Running) { pausedMs = remaining(now); state = Paused; }
    else if (state == Paused) { deadlineMs = now + pausedMs; state = Running; }
    else start(durationMs, now);
}
void CountdownState::cancel() { state = Ready; deadlineMs = pausedMs = 0; }
bool CountdownState::expire(qint64 now) {
    if (state != Running || remaining(now) > 0) return false;
    state = Finished; deadlineMs = pausedMs = 0; return true;
}
QJsonObject CountdownState::toJson() const {
    return {{"state", int(state)}, {"durationMs", double(durationMs)},
            {"deadlineMs", double(deadlineMs)}, {"pausedMs", double(pausedMs)}};
}
CountdownState CountdownState::fromJson(const QJsonObject &j) {
    CountdownState s;
    s.durationMs = qBound<qint64>(1000, qint64(j.value("durationMs").toDouble(s.durationMs)), 24LL * 3600000);
    s.state = State(qBound(0, j.value("state").toInt(), 3));
    s.deadlineMs = qint64(j.value("deadlineMs").toDouble());
    s.pausedMs = qBound<qint64>(0, qint64(j.value("pausedMs").toDouble()), s.durationMs);
    if (s.state == Running && s.deadlineMs <= 0) s.cancel();
    return s;
}
void ResourceStats::observe(double cpu,double rss,qint64 duration,int processes,const QDateTime &at) {
    if(!at.isValid()||!std::isfinite(rss)||rss<0)return;
    lastAt=at;lastRssMb=rss;rssPeakMb=qMax(rssPeakMb,rss);processCount=qMax(0,processes);
    lastCpu=std::isfinite(cpu)&&cpu>=0?cpu:-1;
    if(lastCpu<0||duration<=0||duration>300000)return;
    cpuPeak=qMax(cpuPeak,cpu);observedMs+=duration;++samples;cpuWeighted+=cpu*duration;rssWeighted+=rss*duration;
}
QJsonObject ResourceStats::toJson() const {
    return {{"observedMs",double(observedMs)},{"samples",double(samples)},{"cpuWeighted",cpuWeighted},{"rssWeighted",rssWeighted},
        {"cpuPeak",cpuPeak},{"rssPeakMb",rssPeakMb},{"lastCpu",lastCpu},{"lastRssMb",lastRssMb},{"processCount",processCount},{"lastAt",lastAt.toString(Qt::ISODateWithMs)}};
}
ResourceStats ResourceStats::fromJson(const QJsonObject &j) {
    ResourceStats r;r.observedMs=qBound<qint64>(0,qint64(j.value("observedMs").toDouble()),86400000);r.samples=qMax<qint64>(0,qint64(j.value("samples").toDouble()));
    r.cpuWeighted=qMax(0.,j.value("cpuWeighted").toDouble());r.rssWeighted=qMax(0.,j.value("rssWeighted").toDouble());
    r.cpuPeak=j.value("cpuPeak").toDouble(-1);r.rssPeakMb=j.value("rssPeakMb").toDouble(-1);r.lastCpu=j.value("lastCpu").toDouble(-1);r.lastRssMb=j.value("lastRssMb").toDouble(-1);
    r.processCount=qMax(0,j.value("processCount").toInt());r.lastAt=QDateTime::fromString(j.value("lastAt").toString(),Qt::ISODateWithMs);return r;
}
void ActivityLedger::addResources(const QDateTime &end,qint64 duration,const QString &id,const QString &name,double cpu,double rssMb,int processes) {
    if(!end.isValid()||id.isEmpty())return;
    if(duration<=0||duration>300000){auto &app=days[end.date()].apps[id];app.name=name;app.resources.observe(cpu,rssMb,0,processes,end);return;}
    auto cursor=end.addMSecs(-duration);
    while(cursor<end) {
        const QDateTime next(cursor.date().addDays(1),QTime(0,0),cursor.timeSpec());if(next<=cursor)break;
        const auto stop=qMin(next,end);auto &app=days[cursor.date()].apps[id];app.name=name;
        app.resources.observe(cpu,rssMb,cursor.msecsTo(stop),processes,stop);cursor=stop;
    }
}
void ActivityLedger::addInterval(const QDateTime &end, qint64 duration, const QString &id, const QString &name) {
    if (!end.isValid() || duration <= 0) return;
    // Cap to retained history; partition using local midnights (including DST).
    duration = qMin(duration, 61LL * 86400000);
    auto cursor = end.addMSecs(-duration);
    while (cursor < end) {
        QDateTime next(cursor.date().addDays(1), QTime(0, 0), cursor.timeSpec());
        if (next <= cursor) break;
        const auto stop = next < end ? next : end;
        const qint64 span = cursor.msecsTo(stop);
        auto &d = days[cursor.date()];
        if (id.isEmpty()) d.uptimeMs += span;
        else { auto &a = d.apps[id]; a.name = name; a.ms += span; }
        cursor = stop;
    }
}
void ActivityLedger::observeBoot(const QString &id, qint64 uptime, const QDateTime &now) {
    if (id.isEmpty() || uptime < 0) return;
    if (!startedAt.isValid()) startedAt = now;
    const qint64 elapsed = (id == bootId) ? qMax<qint64>(0, uptime - lastUptimeMs) : uptime;
    addInterval(now, elapsed);
    bootId = id; lastUptimeMs = uptime; prune(now.date());
}
void ActivityLedger::prune(const QDate &today) {
    for (auto i = days.begin(); i != days.end();) {
        if (i.key() < today.addDays(-59) || i.key() > today) i = days.erase(i); else ++i;
    }
}
QJsonObject ActivityLedger::toJson() const {
    QJsonObject result;
    for (auto i = days.cbegin(); i != days.cend(); ++i) {
        QJsonObject apps;
        for (auto a = i->apps.cbegin(); a != i->apps.cend(); ++a)
            apps.insert(a.key(), QJsonObject{{"name", a->name}, {"ms", double(a->ms)}, {"resources",a->resources.toJson()}});
        result.insert(i.key().toString(Qt::ISODate), QJsonObject{{"uptimeMs", double(i->uptimeMs)}, {"apps", apps}});
    }
    return {{"schemaVersion", 2}, {"bootId", bootId}, {"lastUptimeMs", double(lastUptimeMs)},
            {"startedAt", startedAt.toString(Qt::ISODate)}, {"days", result}};
}
ActivityLedger ActivityLedger::fromJson(const QJsonObject &j) {
    ActivityLedger l;
    if (j.value("schemaVersion").toInt()!=1&&j.value("schemaVersion").toInt()!=2) return l;
    l.bootId = j.value("bootId").toString(); l.lastUptimeMs = qMax<qint64>(0, qint64(j.value("lastUptimeMs").toDouble()));
    l.startedAt = QDateTime::fromString(j.value("startedAt").toString(), Qt::ISODate);
    const auto ds = j.value("days").toObject();
    for (auto i = ds.begin(); i != ds.end(); ++i) {
        const QDate date = QDate::fromString(i.key(), Qt::ISODate); if (!date.isValid()) continue;
        const auto d = i.value().toObject(); auto &day = l.days[date];
        day.uptimeMs = qMax<qint64>(0, qint64(d.value("uptimeMs").toDouble()));
        const auto apps = d.value("apps").toObject();
        for (auto a = apps.begin(); a != apps.end(); ++a) {
            const auto v = a.value().toObject();
            day.apps.insert(a.key(), {v.value("name").toString(a.key()), qMax<qint64>(0, qint64(v.value("ms").toDouble())),ResourceStats::fromJson(v.value("resources").toObject())});
        }
    }
    return l;
}
