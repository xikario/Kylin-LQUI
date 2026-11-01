#pragma once
#include "AgentClient.h"
#include <QPointer>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include "Settings.h"
class QProcess;
class QLocalServer;
class QLocalSocket;

// Drives `codex app-server` (JSON lines over stdio). The process starts on the
// first send/compaction and runs inside a guarded systemd scope, so commands
// Codex launches are reclaimed together with it.
class CodexClient:public AgentClient {
    Q_OBJECT
public:
    explicit CodexClient(QObject *parent=nullptr);
    ~CodexClient() override;
    void configure(const Settings &value){settings=value;setIdleTimeout(value.idleStopMinutes*60000);}
    void setIdleTimeout(int ms);
    void setThreadId(const QString &id){thread=id;}
    QString threadId() const{return thread;}
    bool running() const{return phase!=Stopped;}
    bool busy() const{return turnActive||queued!=None;}
    bool compacting() const{return compactActive||queued==Compact;}
    QString unitName() const{return unit;}
    void send(const QString &text,const QStringList &images);
    void compact();
    void discoverModels(); // Catalog/config only; never creates a thread or inference turn.
    void interrupt();
    void answerApproval(const QJsonValue &id,const QString &decision);
    void stop();
private:
    enum Phase{Stopped,Launching,Initializing,Opening,Ready};
    enum Action{None,Send,Compact};
    using Reply=std::function<void(const QJsonObject &result,const QJsonObject &error)>;
    void launch();
    void request(const QString &method,const QJsonObject &params,Reply done={});
    void write(const QJsonObject &message);
    void readLines();
    void handleNotification(const QString &method,const QJsonObject &params);
    void handleServerRequest(const QJsonValue &id,const QString &method,const QJsonObject &params);
    void openThread();
    void loadModelPage(const QString &cursor={},int page=0);
    void catalogLoaded(const QString &error={});
    void runQueued();
    void startTurn();
    void noteItem(const QJsonObject &item,bool completed);
    void emitAnswer();
    void endTurn(const QString &state,const QString &error);
    void processGone(const QString &reason);
    void armIdle();
    QJsonObject permissionOverrides() const;
    QString summary() const;
    Settings settings;
    QProcess *process=nullptr;
    QLocalServer *server=nullptr;
    QPointer<QLocalSocket> guard;
    qint64 guardPid=0,nextId=1;
    QString unit,socketPath,thread,turn;
    QByteArray buffer;
    QStringList stderrTail;
    QHash<qint64,Reply> replies;
    Phase phase=Stopped;
    Action queued=None;
    QString queuedText;QStringList queuedImages;
    bool turnActive=false,compactActive=false,resumedOnce=false;
    QStringList order;QHash<QString,QString> texts,phases;
    int commands=0,edits=0,searches=0,tools=0;
    QElapsedTimer clock;
    QTimer idle;
    int pendingApprovals=0;
    bool discoveryOnly=false;
    QJsonArray models;
};
