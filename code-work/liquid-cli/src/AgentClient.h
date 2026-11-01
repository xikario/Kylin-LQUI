#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include "Settings.h"

// UI-facing contract. Provider protocols and permission choices stay in the
// backend; the drawer never interprets an opaque ACP option or session id.
class AgentClient:public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void configure(const Settings &value)=0;
    virtual void setIdleTimeout(int ms)=0;
    virtual void setThreadId(const QString &id)=0;
    virtual QString threadId() const=0;
    virtual bool running() const=0;
    virtual bool busy() const=0;
    virtual bool compacting() const=0;
    virtual QString unitName() const=0;
    virtual bool supportsCompaction() const{return true;}
    virtual QList<QPair<QString,QString>> approvalOptions(const QJsonValue &) const {
        return {{"拒绝","decline"},{"本次对话都允许","acceptForSession"},{"允许","accept"}};
    }
    virtual void send(const QString &text,const QStringList &images)=0;
    virtual void compact()=0;
    virtual void discoverModels()=0;
    virtual void interrupt()=0;
    virtual void answerApproval(const QJsonValue &id,const QString &decision)=0;
    virtual void stop()=0;
signals:
    void status(QString text);
    void threadReady(QString id,bool resumed);
    void answer(QString text);
    void turnFinished(QString status,QString error,QString summary);
    void contextUsage(qint64 tokens,qint64 window);
    void compactionFinished(bool ok,QString error);
    void approvalRequested(QJsonValue id,QString title,QString detail);
    void approvalClosed(QJsonValue id);
    void modelsReady(QJsonArray models,QString configuredModel,QString configuredEffort);
    void modelsFailed(QString error);
    void modesReady(QJsonArray modes,QString current);
    void capabilitiesChanged();
};

AgentClient *createAgentClient(const Settings &settings,QObject *parent=nullptr);
