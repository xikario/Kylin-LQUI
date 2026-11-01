// Offline peer for the two official ACP surfaces. Qwen uses loadSession and
// modes; DSH uses session/resume, grouped model choices and no modes/commands.
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
QString record,session,model="fixture/fast",effort="high",mode="ask";
bool deepseek=false,approval=false;
QJsonValue promptId;
QList<QJsonObject> events;
QTimer pump;
void out(QJsonObject o){o["jsonrpc"]="2.0";const auto line=QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n';const auto n=write(1,line.data(),size_t(line.size()));Q_UNUSED(n);}
void reply(QJsonValue id,QJsonObject result){out({{"id",id},{"result",result}});}
void update(QJsonObject u){out({{"method","session/update"},{"params",QJsonObject{{"sessionId",session},{"update",u}}}});}
QJsonObject config() {
    QJsonArray choices{QJsonObject{{"value","fixture/fast"},{"name","Fast"}},QJsonObject{{"value","fixture/deep"},{"name","Deep"}}};
    if(deepseek)choices=QJsonArray{QJsonObject{{"group","fixture"},{"name","Fixture"},{"options",choices}}};
    QJsonArray strengths;
    for(const auto &v:model=="fixture/deep"?QStringList{"medium","future-effort"}:QStringList{"low","high"})strengths<<QJsonObject{{"value",v},{"name",v}};
    QJsonArray options{
        QJsonObject{{"id","model"},{"name","Model"},{"category","model"},{"type","select"},{"currentValue",model},{"options",choices}},
        QJsonObject{{"id","reasoning_effort"},{"name","Reasoning"},{"category","thought_level"},{"type","select"},{"currentValue",effort},{"options",strengths}}
    };
    if(!deepseek)options<<QJsonObject{{"id","mode"},{"name","Mode"},{"category","mode"},{"type","select"},{"currentValue",mode},
        {"options",QJsonArray{QJsonObject{{"value","ask"},{"name","Ask"}},QJsonObject{{"value","edit"},{"name","Edit"}}}}};
    return {{"configOptions",options}};
}
void step() {
    if(approval)return;
    if(events.isEmpty()){pump.stop();reply(promptId,{{"stopReason","end_turn"}});return;}
    auto e=events.takeFirst();
    if(e.contains("approval")) {
        approval=true;out({{"id","opaque-permission"},{"method","session/request_permission"},{"params",QJsonObject{{"sessionId",session},
            {"toolCall",QJsonObject{{"toolCallId","x1"},{"title","清理构建目录"},{"rawInput",QJsonObject{{"command","rm -rf build"}}}}},
            {"options",QJsonArray{QJsonObject{{"optionId","yes-once"},{"name","允许本次"},{"kind","allow_once"}},QJsonObject{{"optionId","no-once"},{"name","拒绝"},{"kind","reject_once"}}}}}}});return;
    }
    update(e);
}
void handle(const QJsonObject &m) {
    QFile log(record+".requests");if(log.open(QIODevice::Append))log.write(QJsonDocument(m).toJson(QJsonDocument::Compact)+'\n');
    const auto id=m.value("id");const QString method=m.value("method").toString();const auto p=m.value("params").toObject();
    if(method.isEmpty()&&id==QJsonValue("opaque-permission")){approval=false;return;}
    if(method=="initialize") {
        QJsonObject caps{{"promptCapabilities",QJsonObject{{"image",true}}}};
        if(deepseek)caps["sessionCapabilities"]=QJsonObject{{"resume",QJsonObject()},{"close",QJsonObject()}};else caps["loadSession"]=true;
        reply(id,{{"protocolVersion",1},{"agentCapabilities",caps},{"authMethods",QJsonArray()}});return;
    }
    if(method=="session/new"||method=="session/load"||method=="session/resume") {
        session=method=="session/new"?QString("fixture-acp-%1").arg(getpid()):p.value("sessionId").toString();
        // Replayed history must not become a second assistant reply.
        if(method=="session/load")update({{"sessionUpdate","agent_message_chunk"},{"content",QJsonObject{{"type","text"},{"text","REPLAY"}}}});
        auto result=config();if(method=="session/new")result["sessionId"]=session;reply(id,result);
        if(!deepseek)update({{"sessionUpdate","available_commands_update"},{"availableCommands",QJsonArray{QJsonObject{{"name","compress"},{"description","压缩"}}}}});return;
    }
    if(method=="session/set_config_option") {
        const QString field=p.value("configId").toString(),v=p.value("value").toString();
        if(field=="model"){model=v;effort=model=="fixture/deep"?"medium":"high";}else if(field=="reasoning_effort")effort=v;else if(field=="mode"&&!deepseek)mode=v;
        else {out({{"id",id},{"error",QJsonObject{{"code",-32602},{"message","unsupported config"}}}});return;}
        reply(id,config());return;
    }
    if(method=="session/close"){reply(id,{});return;}
    if(method=="session/cancel"){events.clear();pump.stop();approval=false;reply(promptId,{{"stopReason","cancelled"}});return;}
    if(method=="session/prompt") {
        promptId=id;QString text;int images=0;
        for(const auto &v:p.value("prompt").toArray()){const auto o=v.toObject();if(o.value("type")=="text")text+=o.value("text").toString();else if(o.value("type")=="image")++images;}
        events<<QJsonObject{{"sessionUpdate","agent_thought_chunk"},{"content",QJsonObject{{"type","text"},{"text","SECRET_THOUGHT"}}}};
        events<<QJsonObject{{"sessionUpdate","tool_call"},{"toolCallId","x1"},{"kind","execute"},{"status","in_progress"}};
        if(text.contains("APPROVE"))events<<QJsonObject{{"approval",true}};
        events<<QJsonObject{{"sessionUpdate","tool_call_update"},{"toolCallId","x1"},{"status","completed"}};
        for(const auto &part:QStringList{"收到：",text,QString(" · 图片 %1 张").arg(images)})events<<QJsonObject{{"sessionUpdate","agent_message_chunk"},{"content",QJsonObject{{"type","text"},{"text",part}}}};
        if(text.contains("SLOW"))for(int i=0;i<50;++i)events<<QJsonObject{{"sessionUpdate","agent_message_chunk"},{"content",QJsonObject{{"type","text"},{"text","…"}}}};
        events<<QJsonObject{{"sessionUpdate","usage_update"},{"used",1200},{"size",400000}};
        pump.start(text.contains("SLOW")?100:10);return;
    }
    if(!id.isUndefined())out({{"id",id},{"error",QJsonObject{{"code",-32601},{"message","unsupported"}}}});
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if(argc<3)return 2;record=QString::fromLocal8Bit(argv[1]);deepseek=QString(argv[2])=="--profile";
    if(deepseek?(argc!=4||QString(argv[3])!="acp"):QString(argv[2])!="--acp")return 2;
    QFile f(record);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"cwd",QDir::currentPath()},{"deepseek",deepseek},{"arguments",QJsonArray::fromStringList(app.arguments().mid(2))}}).toJson());f.close();
    QObject::connect(&pump,&QTimer::timeout,&app,step);QByteArray buffer;auto *notifier=new QSocketNotifier(0,QSocketNotifier::Read,&app);
    QObject::connect(notifier,&QSocketNotifier::activated,&app,[&](int){char bytes[8192];const auto n=read(0,bytes,sizeof(bytes));if(n<=0){app.quit();return;}buffer.append(bytes,int(n));int nl;
        while((nl=buffer.indexOf('\n'))>=0){const auto line=buffer.left(nl);buffer.remove(0,nl+1);handle(QJsonDocument::fromJson(line).object());}});
    return app.exec();
}
