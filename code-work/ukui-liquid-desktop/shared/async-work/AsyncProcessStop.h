#pragma once
#include <QCoreApplication>
#include <QProcess>
#include <QTimer>

// Transfer a heap process away from an interface being closed/reconfigured.
// Its late output cannot reach the old owner, and the GUI never waits for exit.
inline void retireProcess(QProcess *process, QObject *owner, int graceMs=1500, bool terminateNow=true) {
    if(!process)return;
    process->disconnect(owner);
    process->setParent(QCoreApplication::instance());
    process->closeWriteChannel();
    if(process->state()==QProcess::NotRunning){process->deleteLater();return;}
    QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
    QObject::connect(process,&QProcess::errorOccurred,process,[process](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)process->deleteLater();
    });
    if(terminateNow)process->terminate();
    QTimer::singleShot(graceMs,process,[process,terminateNow]{
        if(process->state()==QProcess::NotRunning)return;
        if(terminateNow)process->kill();
        else {
            process->terminate();
            QTimer::singleShot(500,process,[process]{if(process->state()!=QProcess::NotRunning)process->kill();});
        }
    });
}
