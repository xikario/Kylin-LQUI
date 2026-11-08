// A scripted stand-in for `codex app-server`: same JSON-lines protocol, no
// network, no model. It records every request for the test to inspect and
// starts two sleeping children (one setsid) to prove scope cleanup.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTimer>
#include <unistd.h>

namespace {
QString record;
QString thread;
QString turn;
int turns=0;
bool waitingApproval=false,slow=false;
QList<QJsonObject> script;
QTimer pump;

void out(const QJsonObject &o){const QByteArray line=QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n';const auto n=write(1,line.data(),size_t(line.size()));Q_UNUSED(n);}
void note(const QString &method,QJsonObject params){params["threadId"]=thread;out({{"method",method},{"params",params}});}
void log(const QJsonObject &o){QFile f(record+".requests");if(f.open(QIODevice::Append))f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n');}
QJsonObject item(const QString &type,const QString &id,QJsonObject extra={}){extra["type"]=type;extra["id"]=id;return extra;}
void queue(const QString &method,const QJsonObject &params){script<<QJsonObject{{"method",method},{"params",params}};}
void finishTurn(const QString &status){script.clear();note("turn/completed",{{"turn",QJsonObject{{"id",turn},{"status",status},{"error",QJsonValue()}}}});turn.clear();}

void startTurn(const QJsonObject &params) {
    QString text;int images=0;
    for(const auto v:params.value("input").toArray()){const auto o=v.toObject();if(o.value("type")=="text")text=o.value("text").toString();if(o.value("type")=="localImage")++images;}
    turn=QString("turn-%1").arg(++turns);slow=text.contains("SLOW");
    queue("turn/started",{{"turn",QJsonObject{{"id",turn}}}});
    queue("item/started",{{"item",item("reasoning","r1")}});
    queue("item/started",{{"item",item("agentMessage","c1",{{"text",""},{"phase","commentary"}})}});
    queue("item/agentMessage/delta",{{"itemId","c1"},{"delta","我先看看目录"}});
    queue("item/completed",{{"item",item("agentMessage","c1",{{"text","我先看看目录"},{"phase","commentary"}})}});
    queue("item/completed",{{"item",item("commandExecution","x1",{{"command","ls"}})}});
    if(text.contains("APPROVE"))script<<QJsonObject{{"approval",true}};
    queue("item/started",{{"item",item("agentMessage","a1",{{"text",""},{"phase","final_answer"}})}});
    const QStringList pieces{"收到：",text,QString(" · 图片 %1 张").arg(images)};
    for(const auto &p:pieces)queue("item/agentMessage/delta",{{"itemId","a1"},{"delta",p}});
    if(slow)for(int i=0;i<40;++i)queue("item/agentMessage/delta",{{"itemId","a1"},{"delta","…"}});
    queue("item/completed",{{"item",item("agentMessage","a1",{{"text",pieces.join("")},{"phase","final_answer"}})}});
    const double tokens=text.contains("BIG")?270000:1200;
    queue("thread/tokenUsage/updated",{{"turnId",turn},{"tokenUsage",QJsonObject{{"last",QJsonObject{{"totalTokens",tokens}}},{"total",QJsonObject{{"totalTokens",tokens}}},{"modelContextWindow",400000}}}});
    script<<QJsonObject{{"complete",true}};
    pump.start(slow?120:15);
}
void step() {
    if(waitingApproval)return;
    if(script.isEmpty()){pump.stop();return;}
    const auto next=script.takeFirst();
    if(next.value("approval").toBool()) {
        waitingApproval=true;
        out({{"id","approval-1"},{"method","item/commandExecution/requestApproval"},{"params",QJsonObject{{"threadId",thread},{"turnId",turn},{"itemId","x2"},{"command","rm -rf build"},{"reason","清理构建目录"}}}});
        return;
    }
    if(next.value("complete").toBool()){finishTurn("completed");return;}
    note(next.value("method").toString(),next.value("params").toObject());
}
void handle(const QJsonObject &m) {
    log(m);
    const QString method=m.value("method").toString();const QJsonValue id=m.value("id");const auto params=m.value("params").toObject();
    if(method.isEmpty()&&id==QJsonValue("approval-1")) {
        waitingApproval=false;
        const QString decision=m.value("result").toObject().value("decision").toString();
        out({{"method","serverRequest/resolved"},{"params",QJsonObject{{"threadId",thread},{"requestId","approval-1"}}}});
        queue("item/agentMessage/delta",{{"itemId","a0"},{"delta","审批结果："+decision}});
        script.prepend(script.takeLast());return;
    }
    if(method=="initialize"){out({{"id",id},{"result",QJsonObject{{"userAgent","fixture"}}}});return;}
    if(method=="initialized")return;
    if(method=="config/read") {
        if(params.value("cwd").toString().endsWith("/stall-config"))return;
        out({{"id",id},{"result",QJsonObject{{"config",QJsonObject{{"model","fixture-fast"},{"model_reasoning_effort","high"}}}}}});return;
    }
    if(method=="model/list") {
        const bool second=!params.value("cursor").toString().isEmpty();
        QJsonArray efforts;for(const auto &v:second?QStringList{"medium","high","future-effort"}:QStringList{"low","high","xhigh"})efforts<<QJsonObject{{"reasoningEffort",v},{"description",v}};
        QJsonObject model{{"model",second?"fixture-deep":"fixture-fast"},{"displayName",second?"Deep":"Fast"},{"isDefault",!second},{"defaultReasoningEffort",second?"medium":"high"},{"supportedReasoningEfforts",efforts}};
        if(!second)model["serviceTiers"]=QJsonArray{QJsonObject{{"id","priority"},{"name","Fast"},{"description","Faster processing"}}};
        out({{"id",id},{"result",QJsonObject{{"data",QJsonArray{model}},{"nextCursor",second?QJsonValue():QJsonValue("next-page")}}}});return;
    }
    if(method=="thread/start"){thread=QString("fixture-thread-%1").arg(getpid());out({{"id",id},{"result",QJsonObject{{"thread",QJsonObject{{"id",thread}}}}}});return;}
    if(method=="thread/resume") {
        if(params.value("threadId").toString()=="gone"){out({{"id",id},{"error",QJsonObject{{"code",-32600},{"message","thread not found"}}}});return;}
        thread=params.value("threadId").toString();out({{"id",id},{"result",QJsonObject{{"thread",QJsonObject{{"id",thread}}}}}});return;
    }
    if(method=="turn/start"){startTurn(params);out({{"id",id},{"result",QJsonObject{{"turn",QJsonObject{{"id",turn}}}}}});return;}
    if(method=="turn/interrupt"){out({{"id",id},{"result",QJsonObject()}});pump.stop();finishTurn("interrupted");return;}
    if(method=="thread/compact/start") {
        out({{"id",id},{"result",QJsonObject()}});turn=QString("turn-%1").arg(++turns);
        queue("turn/started",{{"turn",QJsonObject{{"id",turn}}}});
        queue("item/completed",{{"item",item("contextCompaction","k1")}});
        script<<QJsonObject{{"complete",true}};pump.start(15);return;
    }
    if(!id.isUndefined())out({{"id",id},{"error",QJsonObject{{"code",-32601},{"message","fixture: unsupported"}}}});
}
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if(argc<3||QString(argv[2])!="app-server")return 2;record=QString::fromLocal8Bit(argv[1]);
    QJsonArray pids{double(getpid())};
    for(int i=0;i<2;i++){pid_t child=fork();if(child==0){if(i)setsid();execl("/bin/sleep","sleep","90",nullptr);_exit(1);}pids.append(double(child));}
    QFile f(record);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"pids",pids},{"cwd",QDir::currentPath()}}).toJson());f.close();
    QObject::connect(&pump,&QTimer::timeout,&app,step);
    QByteArray buffer;auto *notifier=new QSocketNotifier(0,QSocketNotifier::Read,&app);
    QObject::connect(notifier,&QSocketNotifier::activated,&app,[&](int){
        char bytes[8192];const auto count=read(0,bytes,sizeof(bytes));if(count<=0){app.quit();return;}
        buffer.append(bytes,int(count));int nl;
        while((nl=buffer.indexOf('\n'))>=0){const auto line=buffer.left(nl);buffer.remove(0,nl+1);const auto o=QJsonDocument::fromJson(line).object();if(!o.isEmpty())handle(o);}
    });
    return app.exec();
}
