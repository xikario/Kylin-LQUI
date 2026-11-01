#include "StationMetrics.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>

namespace {
QString read(const QString &path) { QFile f(path);return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()).trimmed():QString(); }
int sensorRank(const QString &name) {
    const auto s=name.toLower();
    if(s.contains("cpu")||s.contains("coretemp")||s.contains("k10temp")||s.contains("zenpower")||s.contains("phytium")||s.contains("soc_thermal"))return 2;
    if(s=="acpitz")return 1;
    return 0; // Never present GPU, disk or battery temperature as CPU temperature.
}
}
StationMetrics::Network StationMetrics::network() const {
    Network result;
    const auto lines=read(m_proc+"/net/dev").split('\n');
    for(const auto &line:lines) {
        const int colon=line.indexOf(':');if(colon<0)continue;
        const auto name=line.left(colon).trimmed();const auto path=m_sys+"/class/net/"+name;
        // Physical NICs are counted once. TUN/bridge/lo/veth have no device.
        if(name=="lo"||!QFileInfo::exists(path+"/device")||read(path+"/operstate")!="up")continue;
        const auto values=line.mid(colon+1).simplified().split(' ');
        if(values.size()<16)continue;
        bool rx=false,tx=false;const auto received=values[0].toULongLong(&rx),sent=values[8].toULongLong(&tx);
        if(rx&&tx)result.insert(name,{received,sent});
    }
    return result;
}
StationMetrics::Rates StationMetrics::updateNetwork(const Network &now,qint64 ms) {
    Rates rates;
    if(m_sampleTime>=0 && ms>m_sampleTime && ms-m_sampleTime<60000) {
        for(auto it=now.cbegin();it!=now.cend();++it) {
            const auto old=m_previous.constFind(it.key());
            if(old==m_previous.cend()||it->received<old->received||it->sent<old->sent)continue;
            rates.down+=double(it->received-old->received)*1000/(ms-m_sampleTime);
            rates.up+=double(it->sent-old->sent)*1000/(ms-m_sampleTime);rates.valid=true;
        }
    }
    m_previous=now;m_sampleTime=ms;return rates;
}
StationMetrics::Thermal StationMetrics::temperature(qint64 ms) {
    struct Sensor { QString file,name;int rank; };
    QVector<Sensor> sensors;
    const QDir hw(m_sys+"/class/hwmon");
    for(const auto &d:hw.entryList({"hwmon*"},QDir::Dirs|QDir::NoDotAndDotDot)) {
        const auto base=hw.filePath(d),name=read(base+"/name");const int rank=sensorRank(name);if(!rank)continue;
        for(const auto &f:QDir(base).entryList({"temp*_input"},QDir::Files))sensors.append({base+"/"+f,name,rank});
    }
    // Some ARM machines expose CPU sensors only through thermal zones.
    const bool haveCpu=std::any_of(sensors.cbegin(),sensors.cend(),[](const Sensor &s){return s.rank==2;});
    if(!haveCpu) {
        const QDir zones(m_sys+"/class/thermal");
        for(const auto &d:zones.entryList({"thermal_zone*"},QDir::Dirs|QDir::NoDotAndDotDot)) {
            const auto base=zones.filePath(d),name=read(base+"/type");const int rank=sensorRank(name);
            if(rank&&(rank==2||sensors.isEmpty()))sensors.append({base+"/temp",name,rank});
        }
    }
    Thermal result;int selected=0;double sum=0;int count=0;QStringList identity;
    for(const auto &s:sensors) {
        bool ok=false;const double value=read(s.file).toDouble(&ok)/1000.;if(!ok||value<0||value>150)continue;
        if(s.rank<selected)continue;
        if(s.rank>selected){selected=s.rank;sum=0;count=0;identity.clear();}
        sum+=value;++count;identity.append(s.file);result.source=s.name;
    }
    const QString key=identity.join('|');
    if(key!=m_sensorIdentity){m_sensorIdentity=key;m_thermalHistory.clear();}
    if(!count){m_thermalHistory.clear();return result;}
    result.current=sum/count;result.label=selected==2?"均温":"机温";
    m_thermalHistory.append({ms,result.current});
    while(!m_thermalHistory.isEmpty()&&ms-m_thermalHistory.first().first>300000)m_thermalHistory.removeFirst();
    for(const auto &v:m_thermalHistory)result.peak=qMax(result.peak,v.second);
    return result;
}
QString StationMetrics::rate(double bytes) {
    if(!qIsFinite(bytes)||bytes<0)return "—";
    const QStringList units{"B/s","KB/s","MB/s","GB/s"};int i=0;
    while(bytes>=1024&&i<3){bytes/=1024;++i;}
    return QString::number(qMin(9999.,bytes),'f',i&&bytes<100?1:0)+" "+units[i];
}
