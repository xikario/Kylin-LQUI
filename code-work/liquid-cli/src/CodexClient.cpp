#include "CodexClient.h"
#include "CodexOptions.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kGuardTimeoutMs=5000;
constexpr int kStderrLines=8;
QString hex(const QString &text){return QString::fromLatin1(text.toUtf8().toHex());}
QString errorText(const QJsonObject &error){return error.value("message").toString(QStringLiteral("Codex 返回错误"));}
}

CodexClient::CodexClient(QObject *parent):AgentClient(parent) {
    idle.setSingleShot(true);
    connect(&idle,&QTimer::timeout,this,[this]{
        if(phase==Stopped||busy()||compactActive||!approvals.isEmpty())return;
        // The thread is persisted by Codex; the next send resumes it.
        stop();emit status(QStringLiteral("已休眠释放内存 · 发消息自动接上"));
    });
}
void CodexClient::setIdleTimeout(int ms){idle.setInterval(ms);if(ms<=0)idle.stop();else if(idle.isActive())idle.start();}
void CodexClient::armIdle(){if(idle.interval()>0&&phase==Ready&&!busy()&&!compactActive)idle.start();}
CodexClient::~CodexClient(){blockSignals(true);stop();}

void CodexClient::send(const QString &text,const QStringList &images) {
    if(busy())return;
    idle.stop();queued=Send;queuedText=text;queuedImages=images;queuedSettings=settings;
    if(phase==Stopped)launch();else runQueued();
}
void CodexClient::compact() {
    if(compacting()||queued==Send)return;
    idle.stop();queued=Compact; // runs after an active turn finishes
    if(phase==Stopped)launch();else runQueued();
}
void CodexClient::discoverModels() {
    if(running())return;
    discoveryOnly=true;models={};launch();
    QTimer::singleShot(15000,this,[this,attempt=unit]{
        if(discoveryOnly&&unit==attempt){discoveryOnly=false;emit modelsFailed("读取模型超时，请重试");stop();}
    });
}
void CodexClient::interrupt() {
    if(queued==Send){queued=None;queuedText.clear();queuedImages.clear();stop();emit turnFinished("interrupted",{},{});emit status("已停止");return;}
    if(turnActive&&turn.isEmpty()){
        // The server may already be starting the turn before its id arrives.
        // Closing the guarded scope cancels that work and invalidates replies.
        replies.clear();endTurn("interrupted",{});stop();emit status("已停止");return;
    }
    if(turnActive&&!turn.isEmpty()){
        const QString interrupted=turn,attempt=unit;
        request("turn/interrupt",{{"threadId",thread},{"turnId",turn}});
        QTimer::singleShot(10000,this,[this,interrupted,attempt]{
            if(unit==attempt&&turnActive&&turn==interrupted){endTurn("interrupted",{});stop();emit status("已停止");}
        });
    }
}
void CodexClient::answerApproval(const QJsonValue &id,const QString &decision) {
    const QString token=id.toString();
    if(!approvals.contains(token)||(decision!="accept"&&decision!="acceptForSession"&&decision!="decline"))return;
    const auto requestId=approvals.take(token);
    write({{"id",requestId},{"result",QJsonObject{{"decision",decision}}}});
    emit approvalClosed(id);armIdle();
}
void CodexClient::closeApprovals() {
    const auto pending=approvals;approvals.clear();
    for(auto it=pending.cbegin();it!=pending.cend();++it)emit approvalClosed(it.key());
}

void CodexClient::launch() {
    phase=Launching;emit status(QStringLiteral("正在连接 Codex…"));
    const QString helper=QCoreApplication::applicationDirPath()+"/liquid-cli-session";
    unit="liquid-cli-"+QUuid::createUuid().toString(QUuid::Id128)+".scope";
    socketPath=QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/lc-"+QUuid::createUuid().toString(QUuid::Id128)+".sock";
    server=new QLocalServer(this);server->setSocketOptions(QLocalServer::UserAccessOption);
    if(!QFileInfo(helper).isExecutable()||!server->listen(socketPath)){processGone(QStringLiteral("缺少会话清理组件，未启动 Codex"));return;}
    connect(server,&QLocalServer::newConnection,this,[this,helper]{
        while(server&&server->hasPendingConnections()) {
            auto *connection=server->nextPendingConnection();struct ucred peer{};socklen_t n=sizeof(peer);
            if(getsockopt(int(connection->socketDescriptor()),SOL_SOCKET,SO_PEERCRED,&peer,&n)||peer.uid!=getuid()||peer.pid!=guardPid){connection->deleteLater();continue;}
            guard=connection;connection->setParent(this);server->close();
            process=new QProcess(this);
            auto env=QProcessEnvironment::systemEnvironment();
            env.insert("PATH",QDir::homePath()+"/.local/bin:"+env.value("PATH"));
            env.insert("RUST_LOG","error");env.insert("NO_COLOR","1");
            process->setProcessEnvironment(env);
            connect(process,&QProcess::readyReadStandardOutput,this,&CodexClient::readLines);
            connect(process,&QProcess::readyReadStandardError,this,[this]{
                for(const auto &line:QString::fromUtf8(process->readAllStandardError().right(8192)).split('\n',QString::SkipEmptyParts))stderrTail<<line.trimmed().left(1024);
                while(stderrTail.size()>kStderrLines)stderrTail.removeFirst();
            });
            connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this]{
                processGone(stderrTail.isEmpty()?QStringLiteral("Codex 进程已退出"):"Codex 进程已退出："+stderrTail.last().left(240));
            });
            connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)processGone(QStringLiteral("无法启动 Codex"));});
            process->start(helper,{"--run",unit,hex(settings.directory),hex("exec "+settings.command+" app-server")});
            phase=Initializing;
            request("initialize",{{"clientInfo",QJsonObject{{"name","liquid_cli"},{"title","AI小站"},{"version",QCoreApplication::applicationVersion()}}},
                {"capabilities",QJsonObject{{"experimentalApi",false},{"requestAttestation",false}}}},
                [this](const QJsonObject &,const QJsonObject &error){
                    if(!error.isEmpty()){processGone("Codex 初始化失败："+errorText(error));return;}
                    write({{"method","initialized"}});phase=Opening;models={};loadModelPage();
                });
        }
    });
    if(!QProcess::startDetached(helper,{"--guard",socketPath,QString::number(getpid()),unit},{},&guardPid)){processGone(QStringLiteral("无法启动会话清理组件"));return;}
    QTimer::singleShot(kGuardTimeoutMs,this,[this,attempt=unit]{if(unit==attempt&&phase==Launching)processGone(QStringLiteral("会话组件连接超时，未启动 Codex"));});
}

void CodexClient::loadModelPage(const QString &cursor,int page) {
    QJsonObject params{{"limit",100},{"includeHidden",false}};
    if(!cursor.isEmpty())params["cursor"]=cursor;
    request("model/list",params,[this,page,cursor](const QJsonObject &result,const QJsonObject &error){
        if(!error.isEmpty()){catalogLoaded(errorText(error));return;}
        for(const auto &model:result.value("data").toArray())models.append(model);
        const QString next=result.value("nextCursor").toString();
        if(!next.isEmpty()) {
            if(next==cursor||page>=49){catalogLoaded("模型列表分页异常，请重试");return;}
            loadModelPage(next,page+1);return;
        }
        catalogLoaded();
    });
}
void CodexClient::catalogLoaded(const QString &error) {
    if(!discoveryOnly){openThread();return;}
    if(!error.isEmpty()){discoveryOnly=false;emit modelsFailed(error);stop();return;}
    request("config/read",{{"includeLayers",false},{"cwd",settings.directory}},[this](const QJsonObject &result,const QJsonObject &error){
        if(!error.isEmpty())emit modelsFailed(errorText(error));
        else {
            const auto config=result.value("config").toObject();
            emit modelsReady(models,config.value("model").toString(),config.value("model_reasoning_effort").toString());
        }
        // Discovery owns no conversation and releases its entire guarded scope.
        discoveryOnly=false;stop();
    });
}

QJsonObject CodexClient::permissionOverrides() const {
    if(settings.permissions=="ask")return {{"approvalPolicy","on-request"},{"sandbox","workspace-write"}};
    if(settings.permissions=="workspace")return {{"approvalPolicy","never"},{"sandbox","workspace-write"}};
    if(settings.permissions=="full")return {{"approvalPolicy","never"},{"sandbox","danger-full-access"}};
    return {};
}
void CodexClient::openThread() {
    QJsonObject params=permissionOverrides();params["cwd"]=settings.directory;
    auto ready=[this](const QString &id,bool resumed){
        thread=id;phase=Ready;emit threadReady(id,resumed);emit status(QStringLiteral("就绪"));runQueued();
    };
    auto fresh=[this,params,ready]{
        request("thread/start",params,[this,ready](const QJsonObject &result,const QJsonObject &error){
            const QString id=result.value("thread").toObject().value("id").toString();
            if(!error.isEmpty()||id.isEmpty()){processGone("无法开启 Codex 会话："+errorText(error));return;}
            ready(id,false);
        });
    };
    if(thread.isEmpty()){fresh();return;}
    QJsonObject resume=params;resume["threadId"]=thread;resume["excludeTurns"]=true;
    request("thread/resume",resume,[ready,this](const QJsonObject &result,const QJsonObject &error){
        const QString id=result.value("thread").toObject().value("id").toString();
        if(error.isEmpty()&&!id.isEmpty())ready(id,true);
        else processGone("恢复会话失败，原会话已保留，可重试或在设置中开始新对话："+errorText(error));
    });
}
void CodexClient::runQueued() {
    if(phase!=Ready||turnActive||compactActive)return;
    if(queued==Send){startTurn();return;}
    if(queued==Compact) {
        queued=None;compactActive=true;emit status(QStringLiteral("正在压缩上下文…"));
        request("thread/compact/start",{{"threadId",thread}},[this](const QJsonObject &,const QJsonObject &error){
            if(error.isEmpty())return;compactActive=false;emit compactionFinished(false,errorText(error));
        });
    }
}
void CodexClient::startTurn() {
    queued=None;turnActive=true;const quint64 epoch=++turnEpoch;turn.clear();order.clear();texts.clear();phases.clear();
    commands=edits=searches=tools=0;clock.start();
    QJsonArray input;
    if(!queuedText.isEmpty())input<<QJsonObject{{"type","text"},{"text",queuedText},{"text_elements",QJsonArray()}};
    for(const auto &path:queuedImages)input<<QJsonObject{{"type","localImage"},{"path",path}};
    emit status(QStringLiteral("输入中…"));
    const Settings selection=queuedSettings;
    // Read effective defaults for every send: returning to "follow CLI" must
    // also replace overrides previously stored on the resumed thread.
    request("config/read",{{"includeLayers",false},{"cwd",selection.directory}},[this,input,epoch,selection](const QJsonObject &result,const QJsonObject &error){
        if(!turnActive||epoch!=turnEpoch)return;
        if(!error.isEmpty()){endTurn("failed","无法读取 Codex 模型配置："+errorText(error));return;}
        const auto config=result.value("config").toObject();
        QString model=selection.model.isEmpty()?config.value("model").toString():selection.model;
        if(model.isEmpty())for(const auto &v:models)if(v.toObject().value("isDefault").toBool()){model=v.toObject().value("model").toString();break;}
        QString effort=selection.reasoningEffort;
        if(effort.isEmpty()&&selection.model.isEmpty())effort=config.value("model_reasoning_effort").toString();
        if(effort.isEmpty())for(const auto &v:models)if(v.toObject().value("model").toString()==model){effort=v.toObject().value("defaultReasoningEffort").toString();break;}
        QJsonObject params{{"threadId",thread},{"input",input}};
        if(!model.isEmpty())params["model"]=model;if(!effort.isEmpty())params["effort"]=effort;
        if(!selection.serviceTierForTurn.isEmpty()){
            const auto entry=CodexOptions::model(models,model);
            if(!entry.isEmpty()&&!effort.isEmpty()){
                bool supported=false;for(const auto &v:entry.value("supportedReasoningEfforts").toArray())supported|=v.toObject().value("reasoningEffort").toString()==effort;
                if(!supported){endTurn("failed","所选模型不支持思考强度 "+effort+"，请在对应模式的设置中调整。");return;}
            }
            QString tier=selection.serviceTierForTurn;
            if(tier=="fast"){
                tier=CodexOptions::fastTier(entry);
                if(tier.isEmpty()){endTurn("failed","当前模型目录未提供 Fast，请刷新模型列表或关闭对应模式的 Fast 后重试。");return;}
            }
            params["serviceTierForTurn"]=tier;
        }
        request("turn/start",params,[this,epoch](const QJsonObject &result,const QJsonObject &error){
            if(!turnActive||epoch!=turnEpoch)return;
            if(!error.isEmpty()){endTurn("failed",errorText(error));return;}
            const QString id=result.value("turn").toObject().value("id").toString();
            if(turn.isEmpty())turn=id;
        });
    });
}

void CodexClient::request(const QString &method,const QJsonObject &params,Reply done) {
    const qint64 id=nextId++;if(done)replies.insert(id,std::move(done));
    write({{"id",double(id)},{"method",method},{"params",params}});
    // This bounds RPC acknowledgements, not the duration of an AI turn. Once
    // the reply is received, long-running inference/tools remain unrestricted.
    if(replies.contains(id))QTimer::singleShot(60000,this,[this,id,attempt=unit,method]{
        if(unit==attempt&&replies.contains(id))processGone(QStringLiteral("Codex 请求超时（%1），会话已断开，可重试。").arg(method));
    });
}
void CodexClient::write(const QJsonObject &message) {
    if(!process||process->state()==QProcess::NotRunning)return;
    process->write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n');
}
void CodexClient::readLines() {
    buffer+=process->readAllStandardOutput();
    if(buffer.size()>64*1024*1024){stop();emit status("Codex 协议数据超过 64 MiB，连接已关闭，可重试。");return;}
    int newline;
    while(process&&(newline=buffer.indexOf('\n'))>=0) {
        const QByteArray line=buffer.left(newline).trimmed();buffer.remove(0,newline+1);
        const auto message=QJsonDocument::fromJson(line).object();
        if(message.isEmpty())continue;
        const QString method=message.value("method").toString();
        if(!method.isEmpty()&&message.contains("id"))handleServerRequest(message.value("id"),method,message.value("params").toObject());
        else if(!method.isEmpty())handleNotification(method,message.value("params").toObject());
        else if(message.contains("id")) {
            const auto reply=replies.take(qint64(message.value("id").toDouble()));
            if(reply)reply(message.value("result").toObject(),message.value("error").toObject());
        }
    }
}
void CodexClient::handleNotification(const QString &method,const QJsonObject &params) {
    if(!params.value("threadId").toString().isEmpty()&&params.value("threadId").toString()!=thread)return;
    if(method=="turn/started"){if(turn.isEmpty())turn=params.value("turn").toObject().value("id").toString();return;}
    if(method=="item/started"||method=="item/completed"){noteItem(params.value("item").toObject(),method=="item/completed");return;}
    if(method=="item/agentMessage/delta") {
        const QString id=params.value("itemId").toString();
        if(!order.contains(id))order<<id;texts[id]+=params.value("delta").toString();emitAnswer();return;
    }
    if(method=="thread/tokenUsage/updated") {
        const auto usage=params.value("tokenUsage").toObject();
        emit contextUsage(qint64(usage.value("last").toObject().value("totalTokens").toDouble()),qint64(usage.value("modelContextWindow").toDouble()));
        return;
    }
    if(method=="error") {
        const QString message=params.value("error").toObject().value("message").toString();
        if(params.value("willRetry").toBool())emit status("连接不稳定，正在重试…");
        else if(!message.isEmpty()){stderrTail<<message.left(1024);while(stderrTail.size()>kStderrLines)stderrTail.removeFirst();}
        return;
    }
    if(method=="serverRequest/resolved"){
        for(auto it=approvals.begin();it!=approvals.end();) {
            if(it.value()==params.value("requestId")){const QString token=it.key();it=approvals.erase(it);emit approvalClosed(token);}else ++it;
        }
        armIdle();return;
    }
    if(method=="turn/completed") {
        const auto t=params.value("turn").toObject();
        const QString state=t.value("status").toString();QString error=t.value("error").toObject().value("message").toString();
        if(compactActive) {
            compactActive=false;turn.clear();emit status(QStringLiteral("就绪"));
            emit compactionFinished(state=="completed",error);runQueued();armIdle();return;
        }
        if(turnActive)endTurn(state,error);
    }
}
void CodexClient::handleServerRequest(const QJsonValue &id,const QString &method,const QJsonObject &params) {
    if(!turnActive||(!params.value("threadId").toString().isEmpty()&&params.value("threadId").toString()!=thread)||
       (!turn.isEmpty()&&!params.value("turnId").toString().isEmpty()&&params.value("turnId").toString()!=turn)) {
        write({{"id",id},{"error",QJsonObject{{"code",-32600},{"message","请求已失效"}}}});return;
    }
    auto approve=[this,id](const QString &title,const QString &detail){
        const QString token=unit+":"+QString::number(++approvalSerial);
        approvals.insert(token,id);emit approvalRequested(token,title,detail);
    };
    const QString reason=params.value("reason").toString();
    if(method=="item/commandExecution/requestApproval") {
        QString detail=params.value("command").toString();if(!reason.isEmpty())detail+=(detail.isEmpty()?"":"\n")+reason;
        approve(QStringLiteral("Codex 想运行命令"),detail);return;
    }
    if(method=="item/fileChange/requestApproval") {
        QString detail=reason.isEmpty()?QStringLiteral("修改工作目录中的文件"):reason;
        const QString root=params.value("grantRoot").toString();if(!root.isEmpty())detail+="\n"+root;
        approve(QStringLiteral("Codex 想修改文件"),detail);return;
    }
    // Interactive forms, attestations and account flows are not part of this
    // chat. Reply instead of leaving the turn waiting forever.
    write({{"id",id},{"error",QJsonObject{{"code",-32601},{"message","AI小站不支持该请求"}}}});
}
void CodexClient::noteItem(const QJsonObject &item,bool completed) {
    const QString type=item.value("type").toString(),id=item.value("id").toString();
    if(type=="agentMessage") {
        if(!order.contains(id))order<<id;
        const QString phaseName=item.value("phase").toString();if(!phaseName.isEmpty())phases[id]=phaseName;
        if(completed&&item.contains("text"))texts[id]=item.value("text").toString();
        emitAnswer();return;
    }
    if(!completed)return;
    if(type=="commandExecution")++commands;
    else if(type=="fileChange")edits+=qMax(1,item.value("changes").toArray().size());
    else if(type=="webSearch")++searches;
    else if(type=="mcpToolCall"||type=="dynamicToolCall")++tools;
}
void CodexClient::emitAnswer() {
    // Commentary is the model narrating its process; the chat shows it only as
    // the typing indicator, never as answer text.
    QStringList visible;
    for(const auto &id:order)if(phases.value(id)!="commentary"&&!texts.value(id).trimmed().isEmpty())visible<<texts.value(id).trimmed();
    if(!visible.isEmpty())emit answer(visible.join("\n\n"));
}
void CodexClient::endTurn(const QString &state,const QString &error) {
    bool anyVisible=false;
    for(const auto &id:order)if(phases.value(id)!="commentary"&&!texts.value(id).trimmed().isEmpty())anyVisible=true;
    if(!anyVisible)for(int i=order.size()-1;i>=0;--i)if(!texts.value(order[i]).trimmed().isEmpty()){emit answer(texts.value(order[i]).trimmed());break;}
    turnActive=false;turn.clear();closeApprovals();
    const QString failure=error.isEmpty()&&state=="failed"&&!stderrTail.isEmpty()?stderrTail.last():error;
    emit status(state=="completed"?QStringLiteral("就绪"):state=="interrupted"?QStringLiteral("已停止"):QStringLiteral("出错了"));
    emit turnFinished(state,failure,summary());
    runQueued();armIdle();
}
QString CodexClient::summary() const {
    QStringList parts;
    if(commands)parts<<QString("运行 %1 条命令").arg(commands);
    if(edits)parts<<QString("修改 %1 个文件").arg(edits);
    if(searches)parts<<QString("搜索 %1 次").arg(searches);
    if(tools)parts<<QString("调用 %1 次工具").arg(tools);
    const qint64 seconds=clock.isValid()?clock.elapsed()/1000:0;
    if(seconds>=3)parts<<(seconds<60?QString("用时 %1 秒").arg(seconds):QString("用时 %1 分 %2 秒").arg(seconds/60).arg(seconds%60,2,10,QChar('0')));
    return parts.join(QStringLiteral(" · "));
}
void CodexClient::processGone(const QString &reason) {
    const bool wasRunning=phase!=Stopped;
    phase=Stopped;replies.clear();buffer.clear();idle.stop();closeApprovals();
    if(discoveryOnly){discoveryOnly=false;emit modelsFailed(reason);}
    if(process){process->disconnect(this);process->deleteLater();process=nullptr;}
    if(guard){guard->close();guard->deleteLater();guard=nullptr;}
    if(server){server->close();server->deleteLater();server=nullptr;}
    if(!socketPath.isEmpty()){QLocalServer::removeServer(socketPath);socketPath.clear();}
    if(turnActive)endTurn("failed",reason);
    if(compactActive){compactActive=false;emit compactionFinished(false,reason);}
    if(queued==Send){queued=None;emit turnFinished("failed",reason,{});}
    else if(queued==Compact){queued=None;emit compactionFinished(false,reason);}
    if(wasRunning)emit status(reason);
}
void CodexClient::stop() {
    if(phase==Stopped)return;
    if(process) {
        process->disconnect(this);process->closeWriteChannel();process->terminate();
        if(!process->waitForFinished(1500))process->kill();
    }
    // Closing the guard socket makes the helper stop the whole scope.
    processGone(QStringLiteral("会话已断开"));
}
