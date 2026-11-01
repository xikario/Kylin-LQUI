#pragma once
#include <QMap>
#include <QPair>
#include <QString>
#include <QVector>

// Reads kernel counters only; no child processes, sockets or model backend.
class StationMetrics {
public:
    struct Counters { quint64 received=0, sent=0; };
    using Network = QMap<QString, Counters>;
    struct Rates { double down=0, up=0; bool valid=false; };
    struct Thermal { double current=-1, peak=-1; QString source, label="温度"; };
    explicit StationMetrics(QString sys="/sys", QString proc="/proc"):m_sys(sys),m_proc(proc){}
    Network network() const;
    Rates updateNetwork(const Network &, qint64 monotonicMs);
    Thermal temperature(qint64 monotonicMs);
    static QString rate(double bytesPerSecond);
private:
    QString m_sys,m_proc;
    Network m_previous;
    qint64 m_sampleTime=-1;
    QString m_sensorIdentity;
    QVector<QPair<qint64,double>> m_thermalHistory;
};
