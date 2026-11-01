#include "ChatView.h"
#include "UiColors.h"
#include "Markdown.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextBrowser>
#include <QAbstractTextDocumentLayout>
#include <QTextOption>
#include <QtMath>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kPageSize=120;
constexpr int kFollowSlack=28;
constexpr int kThumb=150;
constexpr qreal kUserWidth=.84;

// One rounded glass surface per message; user and assistant differ only in tint.
class Bubble:public QFrame {
public:
    Bubble(bool user,QWidget *parent):QFrame(parent),mine(user){setAttribute(Qt::WA_TranslucentBackground);}
protected:
    void paintEvent(QPaintEvent *)override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),mine?16:14,mine?16:14);
        const auto colors=UiColors::forSurface(property("lightSurface").toBool());
        p.fillPath(path,UiColors::color(mine?colors.userBubble:colors.bubble));
        p.setPen(QPen(UiColors::color(colors.border),1));p.drawPath(path);
    }
private: bool mine;
};
class Thumb:public QLabel {
public:
    Thumb(const QString &file,QWidget *parent):QLabel(parent),path(file) {
        QImageReader reader(file);reader.setAutoTransform(true);QSize size=reader.size();
        if(size.isValid()){size.scale(kThumb,kThumb,Qt::KeepAspectRatio);reader.setScaledSize(size);}
        const QImage image=reader.read();
        if(image.isNull())setText("▧ 图片已移除");else setPixmap(QPixmap::fromImage(image));
        setCursor(Qt::PointingHandCursor);setToolTip("打开图片");
    }
protected:
    void mousePressEvent(QMouseEvent *e)override{if(e->button()==Qt::LeftButton&&QFileInfo::exists(path))QDesktopServices::openUrl(QUrl::fromLocalFile(path));}
private: QString path;
};
// Bound rich text to its bubble. Tables may scroll inside the message, while
// prose and unbroken identifiers wrap without enlarging the whole drawer.
class MessageText final:public QTextBrowser {
public:
    explicit MessageText(QWidget *parent):QTextBrowser(parent) {
        setObjectName("messageText");setFrameShape(QFrame::NoFrame);
        setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);setMinimumWidth(0);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setOpenExternalLinks(true);setReadOnly(true);
        setStyleSheet("QTextBrowser{background:transparent;border:0;padding:0;}");
        viewport()->setAutoFillBackground(false);document()->setDocumentMargin(0);
        auto option=document()->defaultTextOption();option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);document()->setDefaultTextOption(option);
        connect(document()->documentLayout(),&QAbstractTextDocumentLayout::documentSizeChanged,this,[this]{schedule();});
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this,&QWidget::customContextMenuRequested,this,[this](const QPoint &at){
            QMenu menu(this);
            if(textCursor().hasSelection())menu.addAction("复制所选",this,&QTextEdit::copy);
            menu.addAction("复制全文",[this]{QApplication::clipboard()->setText(property("raw").toString());});
            menu.exec(mapToGlobal(at));
        });
    }
    void setSurface(bool light){
        const auto colors=UiColors::forSurface(light);ink=colors.text;
        setPalette(colors.palette(palette()));viewport()->setPalette(colors.palette(viewport()->palette()));
        setStyleSheet(QString("QTextBrowser{color:%1;background:transparent;border:0;padding:0;selection-background-color:#397d73;selection-color:white;}").arg(ink));
    }
    void render(const QString &raw,const QString &html){setProperty("raw",raw);setHtml("<div style=\"color:"+ink+"\">"+html+"</div>");schedule();}
protected:
    void resizeEvent(QResizeEvent *event)override{QTextBrowser::resizeEvent(event);schedule();}
    void changeEvent(QEvent *event)override{QTextBrowser::changeEvent(event);if(event->type()==QEvent::FontChange){document()->setDefaultFont(font());schedule();}}
    QVariant loadResource(int,const QUrl &)override{return {};}
private:
    QString ink="#edf5fc";
    bool pending=false;
    void schedule(){if(pending)return;pending=true;QTimer::singleShot(0,this,[this]{
        pending=false;
        const auto size=document()->documentLayout()->documentSize();
        const int extra=size.width()>viewport()->width()+1?horizontalScrollBar()->sizeHint().height():0;
        const int height=qMax(fontMetrics().height()+2,qCeil(size.height())+extra+2);
        if(height!=this->height())setFixedHeight(height);
    });}
};
MessageText *textLabel(const QString &raw,const QString &html,bool light,QWidget *parent) {
    auto *label=new MessageText(parent);label->setSurface(light);label->render(raw,html);return label;
}
QString userHtml(const QString &text){return Markdown::escape(text).replace('\n',"<br>");}
}

ChatView::ChatView(QWidget *parent):QScrollArea(parent) {
    setObjectName("chatView");setFrameShape(QFrame::NoFrame);setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(false);viewport()->setObjectName("chatViewport");
    content=new QWidget(this);content->setObjectName("chatContent");content->setAutoFillBackground(false);
    rows=new QVBoxLayout(content);rows->setContentsMargins(2,4,6,6);rows->setSpacing(10);
    rows->addStretch(1); // short conversations sit next to the composer, like a chat
    earlier=new QPushButton("显示更早的消息",content);earlier->setObjectName("earlierMessages");earlier->setCursor(Qt::PointingHandCursor);earlier->hide();
    rows->addWidget(earlier,0,Qt::AlignHCenter);
    connect(earlier,&QPushButton::clicked,this,&ChatView::showEarlier);
    auto *empty=new QLabel("<div style='font-size:19px'>有什么可以帮你？</div>"
        "<div style='margin-top:10px;font-size:12px'>Enter 发送 · Shift+Enter 换行<br>截图或粘贴图片会附在下一条消息里</div>",content);
    empty->setObjectName("emptyState");empty->setAlignment(Qt::AlignCenter);empty->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding);
    emptyState=empty;rows->addWidget(emptyState,8); // outweighs the top stretch, so the prompt sits mid-height
    typingRow=new QWidget(content);typingRow->setObjectName("typingIndicator");
    auto *typingLayout=new QHBoxLayout(typingRow);typingLayout->setContentsMargins(6,0,0,0);typingLayout->setSpacing(2);
    auto *word=new QLabel("输入中",typingRow);word->setObjectName("typingWord");
    dots=new QLabel(typingRow);dots->setFixedWidth(24);dots->setObjectName("typingDots");
    typingLayout->addWidget(word);typingLayout->addWidget(dots);typingLayout->addStretch();
    rows->addWidget(typingRow);typingRow->hide();
    dotTimer.setInterval(380);
    connect(&dotTimer,&QTimer::timeout,this,[this]{dots->setText(QString(QChar(0x00B7)).repeated((dots->text().size()+1)%4));});
    lastTimer.setSingleShot(true);lastTimer.setInterval(45); // coalesce streaming deltas into ~20 repaints/s
    connect(&lastTimer,&QTimer::timeout,this,&ChatView::flushLast);
    setWidget(content);
    auto *bar=verticalScrollBar();
    connect(bar,&QScrollBar::valueChanged,this,[this,bar](int value){follow=value>=bar->maximum()-kFollowSlack;});
    connect(bar,&QScrollBar::rangeChanged,this,[this,bar](int,int max){if(follow)bar->setValue(max);});
}
void ChatView::setColors(bool light) {
    lightSurface=light;const auto c=UiColors::forSurface(light);setPalette(c.palette(palette()));
    setStyleSheet(QString("QLabel{color:%1;background:transparent;} QLabel#messageMeta,QLabel[note=true]{color:%2;font-size:11px;padding-left:6px;}"
        "QLabel#typingWord,QLabel#typingDots{color:%2;} QPushButton#earlierMessages{color:%2;}").arg(c.text,c.muted));
    for(auto *bubble:findChildren<QFrame*>())if(bubble->objectName()=="userBubble"||bubble->objectName()=="assistantBubble") {bubble->setProperty("lightSurface",light);bubble->update();}
    for(auto *label:findChildren<QTextBrowser*>("messageText")){auto *message=static_cast<MessageText*>(label);message->setSurface(light);const auto raw=label->property("raw").toString();message->render(raw,label->property("markdown").toBool()?Markdown::toHtml(raw,light):userHtml(raw));}
}
void ChatView::setTextPoint(int value) {
    point=value;QFont f=font();f.setPointSize(point);content->setFont(f);fitUserBubbles();
}
QWidget *ChatView::buildRow(const ChatMessage &m) {
    auto *row=new QWidget(content);row->setProperty("role",m.role);
    auto *line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);
    if(m.role=="note") {
        auto *note=new QLabel(Markdown::escape(m.text),row);note->setObjectName("messageText");note->setWordWrap(true);note->setAlignment(Qt::AlignCenter);
        note->setProperty("note",true);line->addWidget(note);return row;
    }
    const bool user=m.role=="user";
    auto *bubble=new Bubble(user,row);bubble->setObjectName(user?"userBubble":"assistantBubble");bubble->setProperty("lightSurface",lightSurface);
    auto *inside=new QVBoxLayout(bubble);inside->setContentsMargins(13,9,13,9);inside->setSpacing(7);
    if(!m.images.isEmpty()) {
        auto *gallery=new QScrollArea(bubble);gallery->setObjectName("messageImages");gallery->setFrameShape(QFrame::NoFrame);gallery->setMinimumWidth(0);
        gallery->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);gallery->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        gallery->setStyleSheet("QScrollArea{background:transparent;border:0;}");gallery->viewport()->setAutoFillBackground(false);
        auto *items=new QWidget;auto *strip=new QHBoxLayout(items);strip->setContentsMargins(0,0,0,0);strip->setSpacing(6);
        int height=0;for(const auto &path:m.images){auto *thumb=new Thumb(path,items);strip->addWidget(thumb);height=qMax(height,thumb->sizeHint().height());}
        items->adjustSize();gallery->setWidget(items);gallery->setFixedHeight(height+gallery->horizontalScrollBar()->sizeHint().height()+2);inside->addWidget(gallery);
    }
    if(!m.text.isEmpty()||!user){auto *label=textLabel(m.text,user?userHtml(m.text):Markdown::toHtml(m.text,lightSurface),lightSurface,bubble);label->setProperty("markdown",!user);inside->addWidget(label);}
    if(user){line->addStretch(1);line->addWidget(bubble);}
    else {
        auto *column=new QVBoxLayout;column->setSpacing(3);column->addWidget(bubble);
        auto *meta=new QLabel(m.meta,row);meta->setObjectName("messageMeta");
        meta->setVisible(!m.meta.isEmpty());column->addWidget(meta);line->addLayout(column,1);
    }
    return row;
}
void ChatView::fitUserBubbles() {
    // Short user messages hug their text; long ones wrap at a fixed share of
    // the drawer. QLabel's own word-wrap hint would make them oddly narrow.
    const int limit=qMax(160,int(viewport()->width()*kUserWidth));
    for(auto *bubble:content->findChildren<QFrame*>("userBubble")) {
        int ideal=0;
        if(auto *label=bubble->findChild<QTextBrowser*>("messageText")) {
            QTextDocument doc;doc.setDefaultFont(label->font());doc.setHtml(label->toHtml());doc.setDocumentMargin(0);
            ideal=int(doc.idealWidth())+4;
        }
        int strip=0;
        for(auto *thumb:bubble->findChildren<QLabel*>())if(thumb->objectName()!="messageText")strip+=thumb->sizeHint().width()+6;
        ideal=qMax(ideal,strip);
        bubble->setFixedWidth(qBound(60,ideal+26,limit));
    }
}
void ChatView::reset(const QVector<ChatMessage> &messages) {
    history=messages;
    for(int i=rows->count()-1;i>=0;--i){auto *w=rows->itemAt(i)->widget();if(w&&w!=earlier&&w!=typingRow&&w!=emptyState)delete w;}
    shownFrom=qMax(0,history.size()-kPageSize);
    const int base=rows->indexOf(emptyState)+1;
    for(int i=shownFrom;i<history.size();++i)rows->insertWidget(base+i-shownFrom,buildRow(history[i]));
    earlier->setVisible(shownFrom>0);refreshEmpty();fitUserBubbles();followBottom();
}
void ChatView::showEarlier() {
    auto *bar=verticalScrollBar();const int fromBottom=bar->maximum()-bar->value();
    const int next=qMax(0,shownFrom-kPageSize);const int base=rows->indexOf(emptyState)+1;
    for(int i=next;i<shownFrom;++i)rows->insertWidget(base+i-next,buildRow(history[i]));
    shownFrom=next;earlier->setVisible(shownFrom>0);fitUserBubbles();follow=false;
    QTimer::singleShot(0,this,[bar,fromBottom]{bar->setValue(bar->maximum()-fromBottom);});
}
void ChatView::append(const ChatMessage &message) {
    if(lastTimer.isActive()){lastTimer.stop();flushLast();}
    history<<message;
    rows->insertWidget(rows->indexOf(typingRow),buildRow(message));refreshEmpty();fitUserBubbles();
}
void ChatView::updateLast(const ChatMessage &message) {
    if(history.isEmpty())return;history.last()=message;pendingLast=message;
    if(!lastTimer.isActive())lastTimer.start();
}
void ChatView::setTyping(bool on) {
    typingRow->setVisible(on);if(on){dots->setText(QString(QChar(0x00B7)));dotTimer.start();}else dotTimer.stop();
    if(!on&&lastTimer.isActive()){lastTimer.stop();flushLast();}
}
bool ChatView::typing() const{return typingRow->isVisible();}
int ChatView::rowCount() const{return int(history.size());}
void ChatView::followBottom() {
    follow=true;QTimer::singleShot(0,this,[this]{verticalScrollBar()->setValue(verticalScrollBar()->maximum());});
}
void ChatView::flushLast() {
    const int index=rows->indexOf(typingRow)-1;auto *row=index>=0?rows->itemAt(index)->widget():nullptr;
    auto *label=row?row->findChild<QTextBrowser*>("messageText"):nullptr;if(!label)return;
    static_cast<MessageText*>(label)->render(pendingLast.text,Markdown::toHtml(pendingLast.text,lightSurface));
    if(auto *meta=row->findChild<QLabel*>("messageMeta")){meta->setText(pendingLast.meta);meta->setVisible(!pendingLast.meta.isEmpty());}
}
void ChatView::refreshEmpty(){emptyState->setVisible(history.isEmpty());}
void ChatView::resizeEvent(QResizeEvent *event){QScrollArea::resizeEvent(event);fitUserBubbles();}
