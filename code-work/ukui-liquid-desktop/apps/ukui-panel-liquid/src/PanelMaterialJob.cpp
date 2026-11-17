#include "PanelMaterialJob.h"
#include "../../../shared/async-work/AsyncProcessStop.h"
#include "../../../shared/effects/EffectsPolicy.h"
#include <QGuiApplication>
#include <QDataStream>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
#include <QDebug>
#include <dlfcn.h>

namespace {
// A stuck or crashing worker is a rendering failure for the whole session:
// two within 10 minutes switch every module to the lite tier (EffectsPolicy.h).
void reportWorkerTrouble(const QString &kind){
    if(QGuiApplication::platformName()!=QLatin1String("xcb"))return;
    LquiEffects::reportFailure(QStringLiteral("panel-material-worker"),kind);
}
QString helperPath(){
    if(qEnvironmentVariableIsSet("LQUI_PANEL_MATERIAL_WORKER"))return qEnvironmentVariable("LQUI_PANEL_MATERIAL_WORKER");
    Dl_info info{};
    if(!dladdr(reinterpret_cast<void *>(&helperPath),&info)||!info.dli_fname)return {};
    // Build and installed layouts both use plugins/styles/<style>.so.
    return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath()+"/../../lqui-panel-material-worker";
}
}
PanelMaterialJob::~PanelMaterialJob(){if(process)retireProcess(process,this,0);}
void PanelMaterialJob::invalidate(){++revision;source={};done={};}
void PanelMaterialJob::request(const QImage &image,const QJsonObject &settings,Done callback){
    ++revision;source=image;options=settings;done=std::move(callback);start();
}
void PanelMaterialJob::start(){
    if(process || !done)return;
    if((failed.isValid()&&failed.elapsed()<10000)||(retiring&&retiring->state()!=QProcess::NotRunning)
       || source.isNull() || !QFileInfo(helperPath()).isExecutable()){
        auto callback=std::move(done);source={};callback({},false);return;
    }
    const auto token=revision;const auto expected=source.size();const auto dpr=source.devicePixelRatio();
    QByteArray input;QDataStream writer(&input,QIODevice::WriteOnly);writer.setVersion(QDataStream::Qt_5_12);
    writer<<quint32(0x4c514d31)<<QJsonDocument(options).toJson(QJsonDocument::Compact)<<double(dpr)<<source;
    source={};auto callback=std::move(done);
    if(input.size()>32*1024*1024){callback({},false);return;}
    auto *child=new QProcess(this);process=child;
    auto *deadline=new QTimer(child);deadline->setSingleShot(true);
    connect(child,&QProcess::started,this,[child,input]{child->write(input);child->closeWriteChannel();});
    auto finish=[this,child,deadline,token,expected,dpr,callback](bool ok){
        if(process!=child)return;
        deadline->stop();process=nullptr;
        const auto output=child->readAllStandardOutput();QImage result;bool gpu=false;
        if(ok&&output.size()<=32*1024*1024){
            QDataStream reader(output);reader.setVersion(QDataStream::Qt_5_12);quint32 magic=0;
            reader>>magic>>gpu>>result;
            if(magic!=0x4c514d31||reader.status()!=QDataStream::Ok||result.size()!=expected)result={};
            else result.setDevicePixelRatio(dpr);
        }
        if(result.isNull())failed.start();
        if(child->state()!=QProcess::NotRunning){retiring=child;child->kill();retireProcess(child,this,0);}
        else child->deleteLater();
        if(token==revision)callback(result,gpu);
        start();
    };
    connect(child,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,child,finish](int code,QProcess::ExitStatus status){
        // Our own kill after a timeout is not a second failure: process!=child by then.
        if(process==child&&(status!=QProcess::NormalExit||code!=0))
            reportWorkerTrouble(status==QProcess::NormalExit?QStringLiteral("exit-")+QString::number(code):QStringLiteral("crash"));
        finish(code==0&&status==QProcess::NormalExit);});
    connect(child,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
    connect(deadline,&QTimer::timeout,this,[finish]{qWarning()<<"[LquiPanel] material worker timed out; keeping cached surface";reportWorkerTrouble(QStringLiteral("timeout"));finish(false);});
    child->start(helperPath(),QStringList{});deadline->start(2500);
}
