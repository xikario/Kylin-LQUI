#include "../AsyncProcessStop.h"
#include <QPointer>
#include <QElapsedTimer>
#include <QDebug>
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    auto *owner=new QObject;auto *process=new QProcess(owner);QPointer<QProcess> guard(process);
    process->start("/usr/bin/python3",{"-c","import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); print('ready',flush=True); time.sleep(30)"});
    if(!process->waitForReadyRead(2000))return 1;
    int stale=0,ticks=0;
    QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),owner,[&]{++stale;});
    QElapsedTimer duration;duration.start();
    retireProcess(process,owner,100);delete owner;
    if(duration.elapsed()>80)return 2;
    QTimer heartbeat;QObject::connect(&heartbeat,&QTimer::timeout,[&]{++ticks;if(!guard)app.quit();});heartbeat.start(5);
    QTimer::singleShot(2500,&app,&QCoreApplication::quit);app.exec();
    if(guard){guard->kill();guard->waitForFinished(1000);return 3;}
    if(stale || ticks<5)return 4;
    qInfo()<<"PASS: stubborn process retired without blocking; old owner detached; ticks"<<ticks;
    return 0;
}
