#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QImage>

struct ChatMessage {
    QString role;        // user, assistant, note
    QString text;
    QStringList images;  // persisted PNG paths inside the conversation folder
    QString meta;        // muted footnote: activity summary, interruption, error
    qint64 time=0;
};

// One conversation per backend. Codex retains the original storage location.
class Conversation {
public:
    explicit Conversation(QString folder={});
    void useBackend(const QString &id);
    bool load();
    bool save(QString *error=nullptr) const;
    void clear();
    const QVector<ChatMessage> &messages() const{return items;}
    int append(const ChatMessage &message);
    void replace(int index,const ChatMessage &message);
    QString storeImage(const QImage &image,QString *error=nullptr) const;
    QString folder() const{return root;}
    QString threadId;
    qint64 contextTokens=0,contextWindow=0;
private:
    QString base,root;
    QVector<ChatMessage> items;
};
