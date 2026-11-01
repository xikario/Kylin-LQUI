#pragma once
#include <QHash>
#include <QMap>
#include <QStringList>
#include <QDateTime>

namespace ProcessMetrics {
struct Process {
    int pid=0,parentPid=0;quint64 startTicks=0,ticks=0;
    double rssMb=0;QString name,executable;
};
struct Snapshot {
    QHash<int,Process> processes;
    qint64 monotonicMs=0;quint64 bootTicks=0;
    QDateTime at;
    int cores=1;long ticksPerSecond=100;
};
struct Group {
    double cpuPercent=-1,rssMb=0;int processes=0;
};
// Pure /proc reads on a worker; no command arguments, file contents or GUI.
Snapshot read(const QString &procRoot=QStringLiteral("/proc"));
// Nearest window-owning ancestor wins, so a terminal and its launched GUI
// application cannot both own the same process. PID reuse resets its baseline.
QMap<QString,Group> aggregate(const Snapshot &current,const Snapshot &previous,
    const QHash<int,QString> &windowRoots,const QStringList &selected,qint64 maxIntervalMs);
}
