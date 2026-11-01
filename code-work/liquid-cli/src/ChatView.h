#pragma once
#include <QScrollArea>
#include <QTimer>
#include <QVector>
#include "Conversation.h"
class QVBoxLayout;
class QLabel;
class QPushButton;

class ChatView:public QScrollArea {
    Q_OBJECT
public:
    explicit ChatView(QWidget *parent=nullptr);
    void setTextPoint(int point);
    void setColors(bool light);
    void reset(const QVector<ChatMessage> &messages);
    void append(const ChatMessage &message);
    void updateLast(const ChatMessage &message);
    void setTyping(bool on);
    bool typing() const;
    int rowCount() const;
    void followBottom();
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    QWidget *buildRow(const ChatMessage &message);
    void fitUserBubbles();
    void showEarlier();
    void refreshEmpty();
    void flushLast();
    QVector<ChatMessage> history;
    int shownFrom=0;
    QWidget *content=nullptr,*typingRow=nullptr,*emptyState=nullptr;
    QVBoxLayout *rows=nullptr;
    QLabel *dots=nullptr;
    QPushButton *earlier=nullptr;
    QTimer dotTimer,lastTimer;
    ChatMessage pendingLast;
    bool follow=true;
    int point=11;
    bool lightSurface=false;
};
