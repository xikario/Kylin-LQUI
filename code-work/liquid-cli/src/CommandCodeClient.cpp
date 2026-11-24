#include "CommandCodeClient.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <memory>

namespace {
constexpr int MaxOutputTokens=8192;
constexpr int StallTimeoutMs=120000;   // no bytes at all for this long ends the turn
constexpr int RetryDelayMs=1500;       // one more try when the connection drops before any reply
const char *const DefaultBase="https://api.commandcode.ai/provider/v1";

QString errorText(const QByteArray &body,const QString &fallback) {
    const auto o=QJsonDocument::fromJson(body).object();
    const auto e=o.value("error");
    QString text=e.isObject()?e.toObject().value("message").toString():e.toString();
    if(text.isEmpty())text=o.value("message").toString();
    if(text.isEmpty())text=QString::fromUtf8(body).trimmed().left(300);
    return text.isEmpty()?fallback:text;
}
QString mimeFor(const QString &path) {
    const auto suffix=QFileInfo(path).suffix().toLower();
    if(suffix=="jpg"||suffix=="jpeg")return "image/jpeg";
    if(suffix=="webp")return "image/webp";
    if(suffix=="gif")return "image/gif";
    return "image/png";
}
}

CommandCodeClient::CommandCodeClient(QObject *parent):AgentClient(parent) {}
CommandCodeClient::~CommandCodeClient() {
    blockSignals(true);
    if(reply)reply->abort();
    if(catalogReply)catalogReply->abort();
}
void CommandCodeClient::configure(const Settings &value){settings=value;}

QString CommandCodeClient::defaultModel(){return QStringLiteral("deepseek/deepseek-v4-flash");}
QString CommandCodeClient::baseUrl() {
    const auto custom=qEnvironmentVariable("LIQUID_CLI_COMMANDCODE_BASE").trimmed();
    QString base=custom.isEmpty()?QString::fromLatin1(DefaultBase):custom;
    return base.remove(QRegularExpression("/+$"));
}
QString CommandCodeClient::apiKey() const {
    if(!settings.commandCodeApiKey.isEmpty())return settings.commandCodeApiKey;
    return qEnvironmentVariable("COMMAND_CODE_API_KEY").trimmed();
}
QString CommandCodeClient::selectedModel() const {return settings.model.isEmpty()?defaultModel():settings.model;}
bool CommandCodeClient::usesMessages(const QString &model) const {
    for(const auto &v:catalog) {
        const auto o=v.toObject();if(o.value("id").toString()!=model)continue;
        const auto endpoints=o.value("supported_endpoints").toArray();
        return endpoints.contains(QJsonValue("/messages"))&&!endpoints.contains(QJsonValue("/chat/completions"));
    }
    return model.startsWith("claude");
}

void CommandCodeClient::discoverModels() {
    if(apiKey().isEmpty()){emit modelsFailed(QStringLiteral("请先填写 Command Code API Key，再点刷新"));return;}
    emit status(QStringLiteral("正在读取 Command Code 模型列表…"));
    fetchCatalog(true);
}

void CommandCodeClient::fetchCatalog(bool announce) {
    announceCatalog|=announce;
    if(catalogReply)return;
    QNetworkRequest request(QUrl(baseUrl()+"/models"));
    request.setRawHeader("Authorization","Bearer "+apiKey().toUtf8());
    request.setRawHeader("Accept","application/json");
    auto *current=network.get(request);catalogReply=current;
    connect(current,&QNetworkReply::finished,this,[this,current]{
        current->deleteLater();if(catalogReply==current)catalogReply=nullptr;
        const int code=current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body=current->readAll();
        QString error;
        if(current->error()!=QNetworkReply::NoError||code>=400)
            error=errorText(body,current->errorString());
        else {
            catalog=QJsonDocument::fromJson(body).object().value("data").toArray();haveCatalog=true;
        }
        if(announceCatalog) {
            announceCatalog=false;
            if(!error.isEmpty())emit modelsFailed(error);
            else {
                QJsonArray models;
                for(const auto &v:catalog) {
                    const auto o=v.toObject();const auto id=o.value("id").toString();if(id.isEmpty())continue;
                    models<<QJsonObject{{"model",id},{"displayName",o.value("name").toString(id)},
                        {"contextLength",o.value("context_length")},{"supportedEndpoints",o.value("supported_endpoints")}};
                }
                emit modelsReady(models,defaultModel(),QString());
                emit status(QStringLiteral("已读取 %1 个模型").arg(models.size()));
            }
        }
        if(waitingForCatalog) {
            // Without a catalog the endpoint falls back to the model-name rule.
            waitingForCatalog=false;
            if(cancelled)finish(QStringLiteral("interrupted"));else startTurn();
        }
    });
}

void CommandCodeClient::send(const QString &text,const QStringList &images) {
    if(busy())return;
    if(apiKey().isEmpty()){emit turnFinished(QStringLiteral("failed"),QStringLiteral("请先在设置里填写 Command Code API Key"),QString());return;}
    if(!loaded) {
        loaded=true;loadContext();
    }
    turns.append(QJsonObject{{"role","user"},{"text",text},{"images",QJsonArray::fromStringList(images)}});
    output.clear();pending.clear();inputTokens=outputTokens=totalTokens=0;cancelled=false;attempt=0;
    if(!haveCatalog){waitingForCatalog=true;emit status(QStringLiteral("正在连接 Command Code…"));fetchCatalog(false);return;}
    startTurn();
}

QJsonObject CommandCodeClient::requestBody(const QString &model,bool messages) const {
    QJsonArray list;
    for(const auto &v:turns) {
        const auto turn=v.toObject();const auto role=turn.value("role").toString();const auto text=turn.value("text").toString();
        const auto images=turn.value("images").toArray();
        if(images.isEmpty()){list<<QJsonObject{{"role",role},{"content",text}};continue;}
        QJsonArray parts;
        for(const auto &image:images) {
            QFile file(image.toString());if(!file.open(QIODevice::ReadOnly))continue;
            const auto data=QString::fromLatin1(file.readAll().toBase64());const auto mime=mimeFor(image.toString());
            if(messages)parts<<QJsonObject{{"type","image"},{"source",QJsonObject{{"type","base64"},{"media_type",mime},{"data",data}}}};
            else parts<<QJsonObject{{"type","image_url"},{"image_url",QJsonObject{{"url","data:"+mime+";base64,"+data}}}};
        }
        parts<<QJsonObject{{"type","text"},{"text",text}};
        list<<QJsonObject{{"role",role},{"content",parts}};
    }
    QJsonObject body{{"model",model},{"messages",list},{"stream",true}};
    if(messages)body.insert("max_tokens",MaxOutputTokens);
    else body.insert("stream_options",QJsonObject{{"include_usage",true}});
    return body;
}

void CommandCodeClient::startTurn() {
    const auto model=selectedModel();const bool messages=usesMessages(model);
    QNetworkRequest request(QUrl(baseUrl()+(messages?"/messages":"/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setRawHeader("Authorization","Bearer "+apiKey().toUtf8());
    request.setRawHeader("Accept","text/event-stream");
    if(messages)request.setRawHeader("anthropic-version","2023-06-01");
    emit status(QStringLiteral("Command Code · ")+model);
    auto *current=network.post(request,QJsonDocument(requestBody(model,messages)).toJson(QJsonDocument::Compact));reply=current;
    auto *stall=new QTimer(current);stall->setSingleShot(true);stall->setInterval(StallTimeoutMs);stall->start();
    auto failure=std::make_shared<QByteArray>();
    auto timedOut=std::make_shared<bool>(false);
    connect(stall,&QTimer::timeout,current,[current,timedOut]{*timedOut=true;current->abort();});
    connect(current,&QNetworkReply::readyRead,this,[this,current,stall,failure]{
        stall->start();
        const int code=current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(code>=400)*failure+=current->readAll();else consume(current->readAll());
    });
    connect(current,&QNetworkReply::finished,this,[this,current,failure,timedOut]{
        current->deleteLater();if(reply!=current)return;
        const int code=current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(code>=400)*failure+=current->readAll();else consume(current->readAll()+"\n");
        if(cancelled&&!*timedOut){finish(QStringLiteral("interrupted"));return;}
        if(*timedOut){finish(QStringLiteral("failed"),QStringLiteral("网络长时间没有响应"));return;}
        if(code==0&&current->error()!=QNetworkReply::NoError&&output.isEmpty()) {
            // Never reached the provider (proxy node down, TLS reset, DNS). Nothing
            // was answered yet, so one delayed retry cannot duplicate a reply.
            if(attempt==0) {
                ++attempt;emit status(QStringLiteral("连接中断，正在重试…"));
                QTimer::singleShot(RetryDelayMs,this,[this]{
                    retrying=false;
                    if(cancelled){finish(QStringLiteral("interrupted"));return;}
                    pending.clear();startTurn();
                });
                reply=nullptr;retrying=true;   // still busy until the retry
                return;
            }
            finish(QStringLiteral("failed"),QStringLiteral("网络连接中断（%1）。可能是代理节点不通，请稍后重试，或检查代理对 api.commandcode.ai 的分流。").arg(current->errorString()));
            return;
        }
        if(code>=400||current->error()!=QNetworkReply::NoError){
            finish(QStringLiteral("failed"),errorText(*failure,current->errorString()));return;
        }
        finish(output.isEmpty()?QStringLiteral("failed"):QStringLiteral("completed"),output.isEmpty()?QStringLiteral("模型没有返回内容"):QString());
    });
}

void CommandCodeClient::consume(const QByteArray &chunk) {
    pending+=chunk;
    int end;
    while((end=pending.indexOf('\n'))>=0) {
        const auto line=pending.left(end).trimmed();pending.remove(0,end+1);
        if(!line.startsWith("data:"))continue;
        const auto payload=line.mid(5).trimmed();
        if(payload.isEmpty()||payload=="[DONE]")continue;
        const auto doc=QJsonDocument::fromJson(payload);
        if(doc.isObject())handleEvent(doc.object());
    }
}

void CommandCodeClient::handleEvent(const QJsonObject &event) {
    const auto type=event.value("type").toString();
    QString delta;
    if(event.contains("choices")) {                        // OpenAI chat completions
        const auto choice=event.value("choices").toArray().first().toObject();
        delta=choice.value("delta").toObject().value("content").toString();
        const auto usage=event.value("usage").toObject();
        if(!usage.isEmpty()){inputTokens=qint64(usage.value("prompt_tokens").toDouble());outputTokens=qint64(usage.value("completion_tokens").toDouble());totalTokens=qint64(usage.value("total_tokens").toDouble());}
    } else if(type=="content_block_delta") {               // Anthropic messages
        const auto d=event.value("delta").toObject();
        if(d.value("type").toString()=="text_delta")delta=d.value("text").toString();
    } else if(type=="message_start") {
        const auto usage=event.value("message").toObject().value("usage").toObject();
        inputTokens=qint64(usage.value("input_tokens").toDouble());outputTokens=qint64(usage.value("output_tokens").toDouble());
    } else if(type=="message_delta") {
        const auto usage=event.value("usage").toObject();
        if(usage.contains("output_tokens"))outputTokens=qint64(usage.value("output_tokens").toDouble());
    } else if(type=="error") {
        emit status(event.value("error").toObject().value("message").toString());
    }
    if(!delta.isEmpty()){output+=delta;emit answer(output);}
}

void CommandCodeClient::finish(const QString &state,const QString &error) {
    reply=nullptr;waitingForCatalog=false;retrying=false;
    const auto model=selectedModel();
    if(state=="completed"||(state=="interrupted"&&!output.isEmpty())) {
        turns.append(QJsonObject{{"role","assistant"},{"text",output}});
    } else if(!turns.isEmpty()&&turns.last().toObject().value("role").toString()=="user") {
        turns.removeLast();   // keep the context consistent; the drawer still shows the message
    }
    saveContext();
    if(state=="completed") {
        const qint64 used=totalTokens>0?totalTokens:inputTokens+outputTokens;
        qint64 window=0;
        for(const auto &v:catalog)if(v.toObject().value("id").toString()==model){window=qint64(v.toObject().value("context_length").toDouble());break;}
        if(used>0)emit contextUsage(used,window);
    }
    QString name=model;
    for(const auto &v:catalog)if(v.toObject().value("id").toString()==model){name=v.toObject().value("name").toString(model);break;}
    emit status(state=="completed"?QStringLiteral("就绪"):state=="interrupted"?QStringLiteral("已停止"):QStringLiteral("未完成"));
    emit turnFinished(state,error,name);
}

void CommandCodeClient::interrupt() {
    if(!busy())return;
    cancelled=true;
    if(reply)reply->abort();
    else if(catalogReply&&waitingForCatalog)catalogReply->abort();
}
void CommandCodeClient::stop(){interrupt();}

QString CommandCodeClient::contextFile() const {
    static const QRegularExpression safe("^[A-Za-z0-9-]{8,64}$");
    if(!safe.match(thread).hasMatch())return {};
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/liquid-cli/commandcode/"+thread+".json";
}
void CommandCodeClient::loadContext() {
    bool resumed=false;
    const auto path=contextFile();
    if(!path.isEmpty()) {
        QFile file(path);
        if(file.open(QIODevice::ReadOnly)) {
            const auto doc=QJsonDocument::fromJson(file.readAll());
            if(doc.isObject()){turns=doc.object().value("turns").toArray();resumed=true;}
        }
    }
    if(!resumed){thread=QUuid::createUuid().toString(QUuid::WithoutBraces);turns={};}
    emit threadReady(thread,resumed);
}
void CommandCodeClient::saveContext() const {
    const auto path=contextFile();if(path.isEmpty())return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return;
    file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    file.write(QJsonDocument(QJsonObject{{"version",1},{"turns",turns}}).toJson(QJsonDocument::Compact));
    file.commit();
}
