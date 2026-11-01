#include "AcpClient.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeDatabase>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

namespace {
QString hex(const QString &s){return QString::fromLatin1(s.toUtf8().toHex());}
QString key(const QJsonValue &id){return QString::fromUtf8(QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact));}
QJsonArray values(const QJsonArray &options) {
    QJsonArray out;
    for(const auto &v:options){const auto o=v.toObject();if(o.contains("options"))for(const auto &child:values(o.value("options").toArray()))out<<child;else if(o.value("value").isString())out<<o;}
    return out;
}
bool containsValue(const QJsonArray &options,const QString &value){for(const auto &v:values(options))if(v.toObject().value("value").toString()==value)return true;return false;}
}

AcpClient::AcpClient(QObject *parent):AgentClient(parent) {
    idle.setSingleShot(true);
    connect(&idle,&QTimer::timeout,this,[this]{if(phase==Ready&&!busy()&&permissions.isEmpty()){stop();emit status("已休眠释放内存 · 发消息自动接上");}});
}
AcpClient::~AcpClient(){blockSignals(true);stop();}
void AcpClient::configure(const Settings &value){settings=value;setIdleTimeout(value.idleStopMinutes*60000);}
void AcpClient::setIdleTimeout(int ms){idle.setInterval(ms);if(ms<=0)idle.stop();else if(idle.isActive())idle.start();}
void AcpClient::armIdle(){if(phase==Ready&&!busy()&&permissions.isEmpty()&&idle.interval()>0)idle.start();}
void AcpClient::send(const QString &text,const QStringList &images) {
    if(busy()||discoveryOnly)return;
    idle.stop();cancelled=false;queued=true;queuedText=text;queuedImages=images;
    if(phase==Stopped)launch();else runQueued();
}
void AcpClient::compact() {
    if(busy())return;
    if(!supportsCompaction()){emit compactionFinished(false,"当前后台没有提供可调用的压缩命令");return;}
    compactActive=true;send("/"+compactCommand,{});
}
void AcpClient::discoverModels() {
    if(running())return;
    discoveryOnly=true;launch();
}
void AcpClient::launch() {
    phase=Launching;capabilities={};configOptions={};legacyModels={};legacyModes={};compactCommand.clear();baselineModel.clear();baselineEffort.clear();
    emit status("正在连接 "+settings.cliName+"…");
    const QString helper=QCoreApplication::applicationDirPath()+"/liquid-cli-session";
    unit="liquid-cli-"+QUuid::createUuid().toString(QUuid::Id128)+".scope";
    socketPath=QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/lc-"+QUuid::createUuid().toString(QUuid::Id128)+".sock";
    server=new QLocalServer(this);server->setSocketOptions(QLocalServer::UserAccessOption);
    if(!QFileInfo(helper).isExecutable()||!server->listen(socketPath)){processGone("缺少会话清理组件，未启动后台");return;}
    connect(server,&QLocalServer::newConnection,this,[this,helper]{
        while(server&&server->hasPendingConnections()) {
            auto *connection=server->nextPendingConnection();struct ucred peer{};socklen_t n=sizeof(peer);
            if(getsockopt(int(connection->socketDescriptor()),SOL_SOCKET,SO_PEERCRED,&peer,&n)||peer.uid!=getuid()||peer.pid!=guardPid){connection->deleteLater();continue;}
            guard=connection;connection->setParent(this);server->close();process=new QProcess(this);
            auto env=QProcessEnvironment::systemEnvironment();env.insert("PATH",QDir::homePath()+"/.local/bin:"+env.value("PATH"));env.insert("NO_COLOR","1");
            process->setProcessEnvironment(env);
            connect(process,&QProcess::readyReadStandardOutput,this,&AcpClient::readLines);
            connect(process,&QProcess::readyReadStandardError,this,[this]{
                for(const auto &line:QString::fromUtf8(process->readAllStandardError().right(8192)).split('\n',QString::SkipEmptyParts))stderrTail<<line.trimmed().left(1024);
                while(stderrTail.size()>8)stderrTail.removeFirst();
            });
            connect(process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this]{processGone(settings.cliName+" 已退出；请检查 CLI 安装和登录配置");});
            connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)processGone("无法启动 "+settings.cliName);});
            const QString suffix=settings.backend=="qwen"?" --acp":" --profile acp";
            process->start(helper,{"--run",unit,hex(settings.directory),hex("exec "+settings.command+suffix)});
            phase=Initializing;initialize();
        }
    });
    if(!QProcess::startDetached(helper,{"--guard",socketPath,QString::number(getpid()),unit},{},&guardPid)){processGone("无法启动会话清理组件");return;}
    QTimer::singleShot(5000,this,[this,attempt=unit]{if(unit==attempt&&phase==Launching)processGone("会话组件连接超时");});
}
void AcpClient::initialize() {
    // Both agents execute their own filesystem and shell tools. Do not claim
    // client filesystem/terminal/auth capabilities that the drawer cannot serve.
    request("initialize",{{"protocolVersion",1},{"clientCapabilities",QJsonObject()},
        {"clientInfo",QJsonObject{{"name","liquid-cli"},{"title","AI小站"},{"version",QCoreApplication::applicationVersion()}}}},
        [this](const QJsonObject &result,const QJsonObject &error){
            if(!error.isEmpty()){processGone(failure(error));return;}
            if(result.value("protocolVersion").toInt()!=1){processGone("该 CLI 未协商 ACP v1，请检查版本");return;}
            capabilities=result.value("agentCapabilities").toObject();phase=Opening;openSession();
        });
}
QString AcpClient::failure(const QJsonObject &error) const {
    QString text=error.value("message").toString("CLI 返回错误");
    if(error.value("code").toInt()==-32000||text.contains("auth",Qt::CaseInsensitive)||text.contains("API key",Qt::CaseInsensitive))
        text+=settings.backend=="qwen"?"；请先在终端运行 qwen 完成登录或配置模型提供商":"；请先配置 DeepSeek Harness 的模型提供商和凭据";
    return text.left(700);
}
void AcpClient::openSession() {
    const QJsonObject params{{"cwd",QFileInfo(settings.directory).absoluteFilePath()},{"mcpServers",QJsonArray()}};
    auto fresh=[this,params]{request("session/new",params,[this](const QJsonObject &result,const QJsonObject &error){
        if(!error.isEmpty()){processGone(failure(error));return;}
        const QString id=result.value("sessionId").toString();if(id.isEmpty()){processGone("CLI 未返回会话编号");return;}
        session=id;sessionOpened(result,false);
    });};
    const auto sessionCaps=capabilities.value("sessionCapabilities").toObject();
    if(discoveryOnly||session.isEmpty()){fresh();return;}
    QString method;
    if(sessionCaps.value("resume").isObject())method="session/resume";
    else if(capabilities.value("loadSession").toBool())method="session/load";
    else {processGone("当前 CLI 不支持恢复旧会话；请在设置中明确开始新对话。");return;}
    auto resume=params;resume["sessionId"]=session;
    request(method,resume,[this](const QJsonObject &result,const QJsonObject &error){
        if(error.isEmpty()){sessionOpened(result,true);return;}
        processGone("恢复会话失败，原会话已保留："+failure(error));
    });
}
QJsonObject AcpClient::option(const QString &category) const {
    for(const auto &v:configOptions){const auto o=v.toObject();const QString id=o.value("id").toString(),c=o.value("category").toString();
        if(o.value("type").toString()!="select")continue;
        if(c==category||id==category||(category=="thought_level"&&(id=="reasoning_effort"||c=="reasoning_effort")))return o;
    }
    return {};
}
void AcpClient::consumeConfiguration(const QJsonObject &result) {
    if(result.contains("configOptions"))configOptions=result.value("configOptions").toArray();
    if(result.contains("models"))legacyModels=result.value("models").toObject();
    if(result.contains("modes"))legacyModes=result.value("modes").toObject();
}
void AcpClient::sessionOpened(const QJsonObject &result,bool resumed) {
    consumeConfiguration(result);
    baselineModel=option("model").value("currentValue").toString(legacyModels.value("currentModelId").toString());
    baselineEffort=option("thought_level").value("currentValue").toString();
    if(discoveryOnly) {
        auto selected=settings;selected.reasoningEffort.clear();selected.sessionMode.clear();
        const auto o=option("model");
        if(!o.isEmpty()&&!containsValue(o.value("options").toArray(),selected.model))selected.model.clear();
        if(o.isEmpty()&&!legacyModels.isEmpty()) {
            bool found=false;for(const auto &v:legacyModels.value("availableModels").toArray())if(v.toObject().value("modelId").toString()==selected.model)found=true;
            if(!found)selected.model.clear();
        }
        configureSession(selected,[this](const QString &error){
            if(!error.isEmpty()){processGone(error);return;}
            emitCatalog();discoveryOnly=false;
            if(capabilities.value("sessionCapabilities").toObject().value("close").isObject())request("session/close",{{"sessionId",session}},[this](const QJsonObject &,const QJsonObject &){stop();});
            else stop();
        });return;
    }
    phase=Ready;emit threadReady(session,resumed);emit capabilitiesChanged();emit status("就绪");runQueued();armIdle();
}
void AcpClient::configureSession(const Settings &selection,std::function<void(QString)> done) {
    // Apply model first, then use its newly returned effort options. Both IDs
    // and values are opaque and must come from the peer's advertised state.
    auto step=std::make_shared<std::function<void(int)>>();
    *step=[this,selection,done,weak=std::weak_ptr<std::function<void(int)>>(step)](int index){
        if(index>=3){done({});return;}
        const QString category=index==0?"model":index==1?"thought_level":"mode";
        const QString wanted=index==0?selection.model:index==1?selection.reasoningEffort:selection.sessionMode;
        auto next=weak.lock();if(!next)return;
        if(wanted.isEmpty()){(*next)(index+1);return;}
        const auto o=option(category);QString method;QJsonObject params{{"sessionId",session}};
        if(!o.isEmpty()) {
            if(!containsValue(o.value("options").toArray(),wanted)){done("当前 CLI 不支持所选的"+QString(index==0?"模型":index==1?"思考强度":"会话模式")+"，请刷新设置后选择");return;}
            if(o.value("currentValue").toString()==wanted){(*next)(index+1);return;}
            method="session/set_config_option";params["configId"]=o.value("id");params["value"]=wanted;
        } else if(index==0&&!legacyModels.isEmpty()) {
            bool found=false;for(const auto &v:legacyModels.value("availableModels").toArray())if(v.toObject().value("modelId").toString()==wanted)found=true;
            if(!found){done("所选模型已不可用，请刷新模型列表");return;}
            if(legacyModels.value("currentModelId").toString()==wanted){(*next)(index+1);return;}
            method="session/set_model";params["modelId"]=wanted;
        } else if(index==2&&!legacyModes.isEmpty()) {
            bool found=false;for(const auto &v:legacyModes.value("availableModes").toArray())if(v.toObject().value("id").toString()==wanted)found=true;
            if(!found){done("所选会话模式已不可用，请刷新设置");return;}
            if(legacyModes.value("currentModeId").toString()==wanted){(*next)(index+1);return;}
            method="session/set_mode";params["modeId"]=wanted;
        } else {done("当前 CLI 未提供该配置项，请改为跟随会话配置");return;}
        request(method,params,[this,next,index,wanted,done](const QJsonObject &result,const QJsonObject &error){
            if(!error.isEmpty()){done(failure(error));return;}
            consumeConfiguration(result);
            if(index==0&&!legacyModels.isEmpty())legacyModels["currentModelId"]=wanted;
            if(index==2&&!legacyModes.isEmpty())legacyModes["currentModeId"]=wanted;
            (*next)(index+1);
        });
    };
    (*step)(0);
}
void AcpClient::emitCatalog() {
    QJsonArray models,modes;const auto modelOption=option("model"),effortOption=option("thought_level"),modeOption=option("mode");
    const QString current=modelOption.value("currentValue").toString(legacyModels.value("currentModelId").toString());
    QJsonArray efforts;for(const auto &v:values(effortOption.value("options").toArray())){const auto o=v.toObject();efforts<<QJsonObject{{"reasoningEffort",o.value("value")},{"description",o.value("name")}};}
    auto addModel=[&](QString id,QString name){if(id.isEmpty())return;QJsonObject m{{"model",id},{"displayName",name.isEmpty()?id:name},{"isDefault",id==baselineModel}};
        if(id==current){m["supportedReasoningEfforts"]=efforts;m["defaultReasoningEffort"]=effortOption.value("currentValue");}models<<m;};
    if(!modelOption.isEmpty())for(const auto &v:values(modelOption.value("options").toArray())){const auto o=v.toObject();addModel(o.value("value").toString(),o.value("name").toString());}
    else for(const auto &v:legacyModels.value("availableModels").toArray()){const auto o=v.toObject();addModel(o.value("modelId").toString(),o.value("name").toString());}
    if(models.isEmpty()&&!current.isEmpty())addModel(current,current);
    if(!modeOption.isEmpty())for(const auto &v:values(modeOption.value("options").toArray())){const auto o=v.toObject();modes<<QJsonObject{{"id",o.value("value")},{"name",o.value("name")}};}
    else modes=legacyModes.value("availableModes").toArray();
    emit modesReady(modes,modeOption.value("currentValue").toString(legacyModes.value("currentModeId").toString()));
    emit modelsReady(models,baselineModel,baselineEffort);
}
void AcpClient::runQueued() {
    if(phase!=Ready||!queued||promptActive)return;
    queued=false;promptActive=true;cancelled=false;++turnEpoch;output.clear();separateOutput=false;toolKinds.clear();finishedTools.clear();commands=edits=searches=tools=0;clock.start();
    emit status(compactActive?"正在压缩上下文…":"输入中…");
    configureSession(settings,[this](const QString &error){
        if(!promptActive)return;
        if(!error.isEmpty()){finishTurn("failed",error);return;}
        if(cancelled){finishTurn("interrupted");return;}
        QJsonArray prompt;if(!queuedText.isEmpty())prompt<<QJsonObject{{"type","text"},{"text",queuedText}};
        if(!queuedImages.isEmpty()&&!capabilities.value("promptCapabilities").toObject().value("image").toBool()){finishTurn("failed","当前 CLI / 模型未声明支持图片输入，请选择支持图片的后台或移除附件");return;}
        for(const auto &path:queuedImages) {
            QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>20*1024*1024){finishTurn("failed","无法读取图片，或图片超过 20 MiB");return;}
            const auto bytes=f.readAll();const QString mime=QMimeDatabase().mimeTypeForData(bytes).name();
            if(!mime.startsWith("image/")){finishTurn("failed","附件不是可识别的图片");return;}
            prompt<<QJsonObject{{"type","image"},{"mimeType",mime},{"data",QString::fromLatin1(bytes.toBase64())}};
        }
        request("session/prompt",{{"sessionId",session},{"prompt",prompt}},[this](const QJsonObject &result,const QJsonObject &error){
            if(cancelled){finishTurn("interrupted");return;}
            if(!error.isEmpty()){finishTurn("failed",failure(error));return;}
            const QString reason=result.value("stopReason").toString();
            if(reason=="end_turn")finishTurn("completed");
            else if(reason=="cancelled")finishTurn("interrupted");
            else finishTurn("failed",reason=="max_tokens"?"达到模型输出上限":reason=="refusal"?"模型拒绝继续此请求":reason=="max_turn_requests"?"达到后台的模型请求次数上限":"后台结束回复："+reason);
        },false);
    });
}
void AcpClient::request(const QString &method,const QJsonObject &params,Reply done,bool bounded) {
    const qint64 id=nextId++;replies.insert(id,std::move(done));write({{"id",double(id)},{"method",method},{"params",params}});
    if(bounded)QTimer::singleShot(20000,this,[this,id,attempt=unit]{
        if(attempt!=unit||!replies.contains(id))return;
        auto reply=replies.take(id);reply({},{{"code",-32099},{"message","读取 CLI 配置超时，请检查安装、登录及网络"}});
    });
}
void AcpClient::write(QJsonObject message){if(process&&process->state()!=QProcess::NotRunning){message["jsonrpc"]="2.0";process->write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n');}}
void AcpClient::readLines() {
    buffer+=process->readAllStandardOutput();
    if(buffer.size()>64*1024*1024){stop();emit status("CLI 协议数据过大，连接已关闭");return;}
    int nl;
    while(process&&(nl=buffer.indexOf('\n'))>=0) {
        const auto line=buffer.left(nl).trimmed();buffer.remove(0,nl+1);if(line.isEmpty())continue;
        const auto m=QJsonDocument::fromJson(line).object();
        if(m.value("jsonrpc").toString()!="2.0"){processGone("CLI 输出不是 ACP 数据，请检查启动命令及插件配置");return;}
        const auto id=m.value("id");const QString method=m.value("method").toString();
        if(!method.isEmpty()&&m.contains("id"))serverRequest(id,method,m.value("params").toObject());
        else if(!method.isEmpty())notification(method,m.value("params").toObject());
        else if(id.isDouble()){auto reply=replies.take(qint64(id.toDouble()));if(reply)reply(m.value("result").toObject(),m.value("error").toObject());}
    }
}
void AcpClient::notification(const QString &method,const QJsonObject &params) {
    if(method!="session/update")return;
    const QString id=params.value("sessionId").toString();if(!session.isEmpty()&&id!=session&&phase!=Opening)return;
    const auto update=params.value("update").toObject();const QString type=update.value("sessionUpdate").toString();
    if(type=="config_option_update"){configOptions=update.value("configOptions").toArray();return;}
    if(type=="current_mode_update"){legacyModes["currentModeId"]=update.value("currentModeId");return;}
    if(type=="available_commands_update") {
        compactCommand.clear();for(const auto &v:update.value("availableCommands").toArray()){const QString name=v.toObject().value("name").toString();if(name=="compress"||name=="compact")compactCommand=name;}
        emit capabilitiesChanged();return;
    }
    // session/load can replay old content. Local persisted history owns display.
    if(phase!=Ready||!promptActive)return;
    if(type=="usage_update"){emit contextUsage(qint64(update.value("used").toDouble()),qint64(update.value("size").toDouble()));return;}
    if(type=="agent_message_chunk"&&!compactActive) {
        const auto content=update.value("content").toObject();if(content.value("type")!="text")return;
        if(separateOutput&&!output.isEmpty()&&!output.endsWith('\n'))output+="\n\n";separateOutput=false;
        output+=content.value("text").toString();emit answer(output);return;
    }
    if(type=="agent_thought_chunk"){separateOutput=true;return;}
    if(type=="tool_call"||type=="tool_call_update") {
        separateOutput=true;const QString tool=update.value("toolCallId").toString(),kind=update.value("kind").toString();if(!kind.isEmpty())toolKinds[tool]=kind;
        if(update.value("status")=="completed"&&!finishedTools.contains(tool)) {
            finishedTools.insert(tool);const QString k=toolKinds.value(tool);
            if(k=="execute")++commands;else if(k=="edit"||k=="delete"||k=="move")++edits;else if(k=="search"||k=="fetch")++searches;else ++tools;
        }
    }
}
QList<QPair<QString,QString>> AcpClient::approvalOptions(const QJsonValue &id) const {
    QList<QPair<QString,QString>> out;
    for(const auto &v:permissions.value(id.toString()).options){const auto o=v.toObject();out.append({o.value("name").toString(),o.value("optionId").toString()});}
    return out;
}
void AcpClient::serverRequest(const QJsonValue &id,const QString &method,const QJsonObject &params) {
    if(method!="session/request_permission"){write({{"id",id},{"error",QJsonObject{{"code",-32601},{"message","AI小站未声明支持该客户端能力"}}}});return;}
    if(params.value("sessionId").toString()!=session||cancelled||!promptActive){write({{"id",id},{"result",QJsonObject{{"outcome",QJsonObject{{"outcome","cancelled"}}}}}});return;}
    const auto options=params.value("options").toArray();
    if(options.isEmpty()){write({{"id",id},{"result",QJsonObject{{"outcome",QJsonObject{{"outcome","cancelled"}}}}}});return;}
    const QString token=unit+":"+QString::number(++approvalSerial);
    permissions.insert(token,{id,options});
    const auto tool=params.value("toolCall").toObject();QString detail=tool.value("title").toString("运行工具");
    const auto raw=tool.value("rawInput");
    if(!raw.isUndefined()) {
        const auto encoded=QJsonDocument(QJsonArray{raw}).toJson(QJsonDocument::Compact);
        detail+="\n"+QString::fromUtf8(encoded.mid(1,encoded.size()-2));
    }
    emit approvalRequested(token,settings.cliName+" 请求授权",detail);
}
void AcpClient::answerApproval(const QJsonValue &id,const QString &decision) {
    const QString k=id.toString();if(!permissions.contains(k))return;
    bool valid=false;for(const auto &v:permissions.value(k).options)if(v.toObject().value("optionId").toString()==decision)valid=true;
    if(!valid)return;
    write({{"id",permissions.value(k).id},{"result",QJsonObject{{"outcome",QJsonObject{{"outcome","selected"},{"optionId",decision}}}}}});
    permissions.remove(k);emit approvalClosed(id);
}
void AcpClient::cancelPermissions() {
    const auto pending=permissions;permissions.clear();
    for(auto it=pending.cbegin();it!=pending.cend();++it){write({{"id",it.value().id},{"result",QJsonObject{{"outcome",QJsonObject{{"outcome","cancelled"}}}}}});emit approvalClosed(it.key());}
}
void AcpClient::interrupt() {
    if(!busy())return;cancelled=true;cancelPermissions();
    if(promptActive){write({{"method","session/cancel"},{"params",QJsonObject{{"sessionId",session}}}});
        QTimer::singleShot(5000,this,[this,attempt=unit,epoch=turnEpoch]{if(attempt==unit&&epoch==turnEpoch&&promptActive&&cancelled)stop();});
    } else stop();
}
QString AcpClient::summary() const {
    QStringList parts;if(commands)parts<<QString("运行 %1 条命令").arg(commands);if(edits)parts<<QString("修改 %1 个文件").arg(edits);
    if(searches)parts<<QString("搜索 %1 次").arg(searches);if(tools)parts<<QString("调用 %1 次工具").arg(tools);
    if(clock.isValid()&&clock.elapsed()>=3000)parts<<QString("用时 %1 秒").arg(clock.elapsed()/1000);return parts.join(" · ");
}
void AcpClient::finishTurn(const QString &state,const QString &error) {
    if(!promptActive)return;promptActive=false;cancelPermissions();
    if(compactActive){compactActive=false;emit compactionFinished(state=="completed",error);}
    else emit turnFinished(state,error,summary());
    emit status(state=="completed"?"就绪":state=="interrupted"?"已停止":"出错了");armIdle();
}
void AcpClient::processGone(const QString &reason) {
    const bool existed=phase!=Stopped;phase=Stopped;idle.stop();replies.clear();buffer.clear();cancelPermissions();
    if(process){process->disconnect(this);process->deleteLater();process=nullptr;}
    if(guard){guard->close();guard->deleteLater();guard=nullptr;}
    if(server){server->close();server->deleteLater();server=nullptr;}
    if(!socketPath.isEmpty()){QLocalServer::removeServer(socketPath);socketPath.clear();}
    if(discoveryOnly){discoveryOnly=false;emit modelsFailed(reason);}
    if(promptActive)finishTurn(cancelled?"interrupted":"failed",cancelled?QString():reason);
    if(queued){queued=false;if(compactActive){compactActive=false;emit compactionFinished(false,reason);}else emit turnFinished(cancelled?"interrupted":"failed",cancelled?QString():reason,{});}
    if(existed)emit status(reason);
}
void AcpClient::stop() {
    if(phase==Stopped)return;cancelPermissions();
    if(process){process->disconnect(this);process->closeWriteChannel();process->terminate();if(!process->waitForFinished(1500))process->kill();}
    processGone("会话已断开");
}
