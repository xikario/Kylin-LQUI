#pragma once
#include "AgentClient.h"
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>
class QProcess;
class QLocalServer;
class QLocalSocket;

// ACP v1 over stdio: Qwen Code (`qwen --acp`) and the official DeepSeek
// Harness (`dsh --profile acp`). No terminal scraping or provider API keys.
class AcpClient:public AgentClient {
    Q_OBJECT
public:
    explicit AcpClient(QObject *parent=nullptr);
    ~AcpClient() override;
    void configure(const Settings &value) override;
    void setIdleTimeout(int ms) override;
    void setThreadId(const QString &id) override{session=id;}
    QString threadId() const override{return session;}
    bool running() const override{return phase!=Stopped;}
    bool busy() const override{return promptActive||queued;}
    bool compacting() const override{return compactActive;}
    QString unitName() const override{return unit;}
    bool supportsCompaction() const override{return !compactCommand.isEmpty();}
    QList<QPair<QString,QString>> approvalOptions(const QJsonValue &id) const override;
    void send(const QString &text,const QStringList &images) override;
    void compact() override;
    void discoverModels() override;
    void interrupt() override;
    void answerApproval(const QJsonValue &id,const QString &decision) override;
    void stop() override;
private:
    using Reply=std::function<void(const QJsonObject &,const QJsonObject &)>;
    struct Permission{QJsonValue id;QJsonArray options;};
    enum Phase{Stopped,Launching,Initializing,Opening,Ready};
    void launch();
    void initialize();
    void openSession();
    void sessionOpened(const QJsonObject &result,bool resumed);
    void consumeConfiguration(const QJsonObject &result);
    QJsonObject option(const QString &category) const;
    void configureSession(const Settings &selection,std::function<void(QString)> done);
    void emitCatalog();
    void runQueued();
    void finishTurn(const QString &state,const QString &error={});
    void request(const QString &method,const QJsonObject &params,Reply done,bool bounded=true);
    void write(QJsonObject message);
    void readLines();
    void notification(const QString &method,const QJsonObject &params);
    void serverRequest(const QJsonValue &id,const QString &method,const QJsonObject &params);
    void cancelPermissions();
    void processGone(const QString &reason);
    void armIdle();
    QString failure(const QJsonObject &error) const;
    QString summary() const;
    Settings settings;
    QProcess *process=nullptr;
    QLocalServer *server=nullptr;
    QPointer<QLocalSocket> guard;
    qint64 guardPid=0,nextId=1,turnEpoch=0,approvalSerial=0;
    Phase phase=Stopped;
    QString session,unit,socketPath,queuedText,output,compactCommand,baselineModel,baselineEffort;
    QStringList queuedImages,stderrTail;
    QByteArray buffer;
    QJsonObject capabilities,legacyModels,legacyModes;
    QJsonArray configOptions;
    QHash<qint64,Reply> replies;
    QHash<QString,Permission> permissions;
    QHash<QString,QString> toolKinds;
    QSet<QString> finishedTools;
    bool queued=false,promptActive=false,compactActive=false,discoveryOnly=false,cancelled=false,separateOutput=false;
    int commands=0,edits=0,searches=0,tools=0;
    QTimer idle;
    QElapsedTimer clock;
};
