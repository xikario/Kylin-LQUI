#include "ProcessMetrics.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <chrono>
#include <unistd.h>
#include <cmath>

namespace ProcessMetrics {
static QByteArray bytes(const QString &path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
Snapshot read(const QString &root) {
    Snapshot result;result.ticksPerSecond=qMax(1L,sysconf(_SC_CLK_TCK));
    result.at=QDateTime::currentDateTime();
    result.monotonicMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    bool ok=false;const double uptime=bytes(root+"/uptime").split(' ').value(0).toDouble(&ok);
    if(ok&&std::isfinite(uptime)&&uptime>=0)result.bootTicks=quint64(uptime*result.ticksPerSecond);
    int cores=0;for(const auto &line:bytes(root+"/stat").split('\n'))if(line.startsWith("cpu")&&line.size()>3&&line[3]>='0'&&line[3]<='9')++cores;
    result.cores=qMax(1,cores);
    for(const auto &entry:QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
        const int pid=entry.toInt(&ok);if(!ok||pid<=0)continue;
        const QString stat=QString::fromLocal8Bit(bytes(root+"/"+entry+"/stat"));const int left=stat.indexOf('('),right=stat.lastIndexOf(')');
        if(left<0||right<=left)continue;
        const auto fields=stat.mid(right+2).simplified().split(' ');if(fields.size()<22)continue;
        Process process;process.pid=pid;process.name=stat.mid(left+1,right-left-1);process.parentPid=fields[1].toInt(&ok);if(!ok)continue;
        process.startTicks=fields[19].toULongLong(&ok);if(!ok)continue;
        const auto user=fields[11].toULongLong(&ok);if(!ok)continue;
        const auto system=fields[12].toULongLong(&ok);if(!ok)continue;process.ticks=user+system;
        const auto pages=fields[21].toLongLong(&ok);if(!ok||pages<0)continue;
        process.rssMb=double(pages)*sysconf(_SC_PAGESIZE)/1048576.;
        process.executable=QFileInfo(root+"/"+entry+"/exe").symLinkTarget();result.processes.insert(pid,process);
    }
    return result;
}
QMap<QString,Group> aggregate(const Snapshot &now,const Snapshot &before,const QHash<int,QString> &roots,const QStringList &selected,qint64 maxIntervalMs) {
    QMap<QString,Group> groups;const qint64 elapsed=now.monotonicMs-before.monotonicMs;
    const bool validTime=before.monotonicMs>0&&elapsed>0&&elapsed<=maxIntervalMs&&before.bootTicks>0&&now.bootTicks>=before.bootTicks
        &&std::abs((now.bootTicks-before.bootTicks)*1000./qMax(1L,now.ticksPerSecond)-elapsed)<2000.;
    QSet<QString> allowed;for(const auto &id:selected)allowed.insert(id);
    for(const auto &process:now.processes) {
        int ancestor=process.pid;QString owner;QSet<int> visited;
        while(ancestor>0&&!visited.contains(ancestor)&&visited.size()<64) {
            visited.insert(ancestor);
            if(roots.contains(ancestor)){owner=roots.value(ancestor);break;}
            const auto it=now.processes.constFind(ancestor);if(it==now.processes.cend())break;ancestor=it->parentPid;
        }
        if(owner.isEmpty()||!allowed.contains(owner))continue;
        auto &group=groups[owner];++group.processes;group.rssMb+=process.rssMb;
        if(!validTime)continue;
        const auto old=before.processes.constFind(process.pid);quint64 delta=0;bool known=false;
        if(old!=before.processes.cend()&&old->startTicks==process.startTicks&&process.ticks>=old->ticks){delta=process.ticks-old->ticks;known=true;}
        else if(process.startTicks>=before.bootTicks&&process.startTicks<=now.bootTicks){delta=process.ticks;known=true;}
        if(known)group.cpuPercent=qMax(0.,group.cpuPercent)+delta*100000./qMax(1L,now.ticksPerSecond)/elapsed;
    }
    for(auto &group:groups)if(group.cpuPercent>=0)group.cpuPercent=qMin(group.cpuPercent,now.cores*100.);
    return groups;
}
}
