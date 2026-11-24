#pragma once
#include "AgentClient.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>

// Command Code over its HTTP provider API (https://commandcode.ai/docs/provider).
// Claude models are served only by the Anthropic-style /messages endpoint, all
// others by OpenAI-style /chat/completions; the model catalog says which.
// This is a plain chat backend: no tools, approvals or compaction. The
// conversation context is kept per thread id under the user's data directory.
class CommandCodeClient:public AgentClient {
    Q_OBJECT
public:
    explicit CommandCodeClient(QObject *parent=nullptr);
    ~CommandCodeClient() override;
    void configure(const Settings &value) override;
    void setIdleTimeout(int) override {}
    void setThreadId(const QString &id) override {thread=id;loaded=false;}
    QString threadId() const override {return thread;}
    bool running() const override {return false;}
    bool busy() const override {return reply!=nullptr||waitingForCatalog||retrying;}
    bool compacting() const override {return false;}
    QString unitName() const override {return {};}
    bool supportsCompaction() const override {return false;}
    QList<QPair<QString,QString>> approvalOptions(const QJsonValue &) const override {return {};}
    void send(const QString &text,const QStringList &images) override;
    void compact() override {emit compactionFinished(false,QStringLiteral("Command Code 后台不支持压缩上下文"));}
    void discoverModels() override;
    void interrupt() override;
    void answerApproval(const QJsonValue &,const QString &) override {}
    void stop() override;

    static QString defaultModel();
    static QString baseUrl();

private:
    QString apiKey() const;
    QString selectedModel() const;
    bool usesMessages(const QString &model) const;
    void fetchCatalog(bool announce);
    void startTurn();
    void finish(const QString &state,const QString &error={});
    void consume(const QByteArray &chunk);
    void handleEvent(const QJsonObject &event);
    QJsonObject requestBody(const QString &model,bool messages) const;
    QString contextFile() const;
    void loadContext();
    void saveContext() const;

    Settings settings;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> reply,catalogReply;
    QJsonArray catalog;          // raw provider entries
    bool haveCatalog=false,waitingForCatalog=false,announceCatalog=false;
    QString thread;
    bool loaded=false,cancelled=false;
    int attempt=0;               // network-level retries used in this turn
    bool retrying=false;         // waiting for that delayed retry
    QJsonArray turns;            // {role,text,images[]}
    QString output;
    QByteArray pending;          // undecoded SSE bytes
    qint64 inputTokens=0,outputTokens=0,totalTokens=0;
};
