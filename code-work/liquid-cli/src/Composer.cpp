#include "Composer.h"
#include "UiColors.h"
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QAbstractTextDocumentLayout>
#include <QTimer>

namespace {
constexpr int kMaxLines=8;
constexpr int kChip=46;
bool imageFile(const QString &path){return !QImageReader::imageFormat(path).isEmpty();}

// Paste and drop route images to the attachment strip instead of inserting
// file paths or nothing into the text.
class Input:public QPlainTextEdit {
public:
    explicit Input(Composer *owner):QPlainTextEdit(owner),composer(owner){}
protected:
    void paintEvent(QPaintEvent *event)override {
        QPlainTextEdit::paintEvent(event);
        // Qt 5.12's OEM stylesheet can reset PlaceholderText to black while
        // preserving Text. Paint this one hint with the shared surface ink.
        if(!document()->isEmpty()||composing)return;
        QPainter p(viewport());p.setFont(font());p.setPen(property("placeholderInk").value<QColor>());
        const auto cursor=cursorRect();p.drawText(QPoint(cursor.left(),cursor.top()+fontMetrics().ascent()),property("backendPlaceholder").toString());
    }
    void inputMethodEvent(QInputMethodEvent *event)override {
        composing=!event->preeditString().isEmpty();QPlainTextEdit::inputMethodEvent(event);viewport()->update();
    }
    bool canInsertFromMimeData(const QMimeData *source)const override{return source->hasImage()||source->hasUrls()||QPlainTextEdit::canInsertFromMimeData(source);}
    void insertFromMimeData(const QMimeData *source)override {
        if(source->hasImage()){emit composer->imagePasted(qvariant_cast<QImage>(source->imageData()));return;}
        if(source->hasUrls()) {
            bool used=false;
            for(const auto &url:source->urls())if(url.isLocalFile()&&imageFile(url.toLocalFile())){emit composer->imageFileDropped(url.toLocalFile());used=true;}
            if(used)return;
        }
        QPlainTextEdit::insertFromMimeData(source);
    }
private: Composer *composer;bool composing=false;
};
}

Composer::Composer(QWidget *parent):QFrame(parent) {
    setObjectName("composer");setAttribute(Qt::WA_TranslucentBackground);
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(12,8,8,8);outer->setSpacing(6);
    stripHost=new QWidget(this);strip=new QHBoxLayout(stripHost);strip->setContentsMargins(0,2,0,0);strip->setSpacing(6);
    stripHost->hide();outer->addWidget(stripHost);
    auto *row=new QHBoxLayout;row->setSpacing(6);
    input=new Input(this);input->setObjectName("composerInput");input->setFrameShape(QFrame::NoFrame);
    setBackendName("Codex");input->setTabChangesFocus(true);
    input->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    input->viewport()->setAutoFillBackground(false);
    input->installEventFilter(this);
    connect(input,&QPlainTextEdit::textChanged,this,&Composer::scheduleHeight);
    connect(input->document()->documentLayout(),&QAbstractTextDocumentLayout::documentSizeChanged,this,[this]{scheduleHeight();});
    row->addWidget(input,1);
    capture=new QPushButton(this);capture->setObjectName("captureButton");capture->setIcon(QIcon(":/icons/capture.svg"));
    capture->setToolTip("框选截图");capture->setFixedSize(34,34);capture->setCursor(Qt::PointingHandCursor);
    connect(capture,&QPushButton::clicked,this,&Composer::captureRequested);
    action=new QPushButton(this);action->setObjectName("sendButton");action->setFixedSize(34,34);action->setCursor(Qt::PointingHandCursor);
    connect(action,&QPushButton::clicked,this,[this]{if(working)emit stopRequested();else trySubmit();});
    row->addWidget(capture,0,Qt::AlignBottom);row->addWidget(action,0,Qt::AlignBottom);outer->addLayout(row);
    hint=new QLabel(this);hint->setObjectName("composerHint");hint->hide();outer->addWidget(hint);
    setColors(false);
    setBusy(false);fitHeight();
}
void Composer::setBackendName(const QString &name) {
    input->setAccessibleDescription("给 "+name+" 发消息");input->setProperty("backendPlaceholder","给 "+name+" 发消息…");input->viewport()->update();
}
void Composer::setColors(bool light) {
    lightSurface=light;const auto c=UiColors::forSurface(light);
    setPalette(c.palette(palette()));
    input->setStyleSheet(QString("QPlainTextEdit{background:transparent;color:%1;selection-background-color:#397d73;selection-color:white;}").arg(c.text));
    // An explicit placeholder role is required by the OEM UKUI editor style.
    input->ensurePolished();input->setPalette(c.palette(input->palette()));
    input->setProperty("placeholderInk",QColor(c.muted));input->viewport()->update();
    input->viewport()->setPalette(input->palette());
    hint->setStyleSheet(QString("color:%1;font-size:11px;").arg(c.muted));
    setStyleSheet(QString("QPushButton#captureButton,QPushButton#sendButton{border-radius:17px;border:0;background:transparent;color:%1;font-size:16px;}"
        "QPushButton#captureButton:hover{background:rgba(120,185,165,35);}"
        "QPushButton#sendButton{background:rgba(120,185,165,80);}QPushButton#sendButton:hover{background:rgba(120,185,165,120);}"
        "QPushButton#sendButton:disabled{background:rgba(120,185,165,22);color:%2;}QPushButton:focus{border:1px solid %3;}").arg(c.text,c.muted,c.accent));
    QPixmap pixmap=QIcon(":/icons/capture.svg").pixmap(QSize(20,20)*devicePixelRatioF());
    QPainter painter(&pixmap);painter.setCompositionMode(QPainter::CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),QColor(c.text));painter.end();
    capture->setIcon(QIcon(pixmap));update();
}
void Composer::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),18,18);
    const auto colors=UiColors::forSurface(lightSurface);
    p.fillPath(path,UiColors::color(colors.field));
    p.setPen(QPen(UiColors::color(input->hasFocus()?colors.accent:colors.border),1));p.drawPath(path);
}
bool Composer::eventFilter(QObject *watched,QEvent *event) {
    if(watched==input&&(event->type()==QEvent::FontChange||event->type()==QEvent::Resize))scheduleHeight();
    if(watched==input&&(event->type()==QEvent::FocusIn||event->type()==QEvent::FocusOut))update();
    if(watched==input&&event->type()==QEvent::KeyPress) {
        auto *key=static_cast<QKeyEvent*>(event);
        const bool enter=key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter;
        if(enter&&!(key->modifiers()&Qt::ShiftModifier)) {
            // An input method composing text owns Enter; QPlainTextEdit gets
            // the commit separately, so this only sees real submissions.
            trySubmit();return true;
        }
    }
    return QFrame::eventFilter(watched,event);
}
void Composer::trySubmit() {
    if(working){setHint("正在回复，可以先写好，完成后再按 Enter 发送");return;}
    const QString message=text();
    if(message.isEmpty()&&attached.isEmpty())return;
    emit submit(message,attached);
}
QString Composer::text() const{return input->toPlainText().trimmed();}
void Composer::clear(){input->clear();attached.clear();rebuildStrip();setHint({});}
void Composer::focusInput(){input->setFocus(Qt::OtherFocusReason);}
void Composer::setHint(const QString &value){hint->setText(value);hint->setVisible(!value.isEmpty());}
void Composer::setBusy(bool busy) {
    working=busy;action->setText(busy?QString(QChar(0x25A0)):QString(QChar(0x2191)));
    action->setToolTip(busy?"停止回复":"发送（Enter）");
    if(!busy&&hint->text().startsWith("正在回复"))setHint({});
}
void Composer::addImage(const QString &path) {
    if(path.isEmpty()||attached.contains(path))return;
    attached<<path;rebuildStrip();focusInput();
}
void Composer::appendText(const QString &text) {
    if(text.isEmpty())return;
    auto cursor=input->textCursor();cursor.movePosition(QTextCursor::End);cursor.beginEditBlock();
    if(!input->toPlainText().isEmpty()&&!input->toPlainText().endsWith('\n'))cursor.insertText("\n");
    cursor.insertText(text);cursor.endEditBlock();input->setTextCursor(cursor);input->ensureCursorVisible();focusInput();
}
void Composer::scheduleHeight(){if(heightQueued)return;heightQueued=true;QTimer::singleShot(0,this,[this]{heightQueued=false;fitHeight();});}
void Composer::rebuildStrip() {
    while(auto *item=strip->takeAt(0)){delete item->widget();delete item;}
    for(const auto &path:attached) {
        auto *chip=new QToolButton(stripHost);chip->setObjectName("attachmentChip");chip->setFixedSize(kChip,kChip);
        QImageReader reader(path);QSize size=reader.size();if(size.isValid()){size.scale(kChip-4,kChip-4,Qt::KeepAspectRatioByExpanding);reader.setScaledSize(size);}
        chip->setIcon(QPixmap::fromImage(reader.read()));chip->setIconSize(QSize(kChip-4,kChip-4));
        chip->setToolTip("点击移除这张图片");chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet("QToolButton{border:1px solid rgba(214,245,228,70);border-radius:9px;background:rgba(0,0,0,40);}QToolButton:hover{border-color:#f0a3a3;}");
        connect(chip,&QToolButton::clicked,this,[this,path]{attached.removeAll(path);rebuildStrip();});
        strip->addWidget(chip);
    }
    strip->addStretch();stripHost->setVisible(!attached.isEmpty());
}
void Composer::fitHeight() {
    const QFontMetrics metrics(input->font());
    int lines=0;for(auto block=input->document()->begin();block.isValid();block=block.next())lines+=qMax(1,block.layout()->lineCount());
    input->setFixedHeight(qBound(3,lines,kMaxLines)*metrics.lineSpacing()+16);
}
