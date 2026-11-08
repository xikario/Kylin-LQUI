#include "Drawer.h"
#include "DockPopupMotion.h"
#include "LiquidPopup.h"
#include "StationDrop.h"
#include "UiColors.h"
#include "ChatView.h"
#include "Composer.h"
#include "RegionCapture.h"
#include "CaptureEditor.h"
#include "NextKdeGlassView.h"
#include "LiquidOpticsRenderer.h"
#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QImageReader>
#include <QStandardPaths>
#include <QMenu>
#include <QtMath>
#include <QDateTime>
#include <QDialog>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QSignalBlocker>
#include "ResidentStartup.h"

namespace {
constexpr int kFloatDistance=34;     // "float" glides in from just off its resting place
constexpr int kEdgeWidth=36,kEdgeHeight=36;
constexpr int kDismissStepTokens=32000;
QString configRoot(){return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);}
QScreen *currentScreen(const QWidget *w){return w->windowHandle()&&w->windowHandle()->screen()?w->windowHandle()->screen():QGuiApplication::primaryScreen();}
QIcon icon(const QString &name){return QIcon(":/icons/"+name+".svg");}
QPushButton *toolButton(const QString &iconName,const QString &tip,QWidget *parent) {
    auto *b=new QPushButton(parent);b->setIcon(icon(iconName));b->setToolTip(tip);b->setFixedSize(32,32);
    b->setCursor(Qt::PointingHandCursor);b->setFocusPolicy(Qt::TabFocus);return b;
}
QString wallpaperPath() {
    QFile f(configRoot()+"/kyfences/layout.json");
    if(f.open(QIODevice::ReadOnly)) {
        const auto path=QJsonDocument::fromJson(f.readAll()).object().value("wallpaperPath").toString();
        if(QFileInfo(path).isFile())return path;
    }
    return {};
}
QString kilo(qint64 tokens){return tokens<=0?QStringLiteral("—"):QString::number(qMax<qint64>(1,qRound64(tokens/1000.)))+"K";}
}
static QImage wallpaperTile(const QString &path,const QRect &area,const QRect &screen,qreal dpr) {
    const QSize pixels(qRound(area.width()*dpr),qRound(area.height()*dpr));
    QImage result(pixels,QImage::Format_ARGB32_Premultiplied);result.fill(QColor("#17392f"));
    QImageReader reader(path);reader.setDecideFormatFromContent(true);reader.setAutoTransform(true);const QSize source=reader.size();
    if(!source.isEmpty()) {
        const qreal factor=qMax(screen.width()*dpr/source.width(),screen.height()*dpr/source.height());
        const QSize scaled(qCeil(source.width()*factor),qCeil(source.height()*factor));
        const QPoint offset((scaled.width()-qRound(screen.width()*dpr))/2,(scaled.height()-qRound(screen.height()*dpr))/2);
        const QRect clip(QPoint(qRound((area.x()-screen.x())*dpr),qRound((area.y()-screen.y())*dpr))+offset,pixels);
        reader.setScaledSize(scaled);reader.setScaledClipRect(clip);
        const auto tile=reader.read();if(!tile.isNull()){QPainter p(&result);p.drawImage(result.rect(),tile);}
    }
    result.setDevicePixelRatio(dpr);return result;
}
QImage drawerWallpaper(const QRect &area,const QRect &screen,qreal dpr){return wallpaperTile(wallpaperPath(),area,screen,dpr);}

// The small handle at the screen edge. It shows a pulsing dot while Codex is
// replying in the background and a steady one for an unread reply.
class Edge:public QWidget {
public:
    explicit Edge(Drawer *drawer):QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint),owner(drawer) {
        setObjectName("cliEdge");setWindowTitle("AI小站");setWindowIcon(icon("app"));
        setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);
        setCursor(Qt::PointingHandCursor);setToolTip("AI小站 · 右键设置");
        pulse.setParent(this);pulse.setObjectName("edgeBusyPulse");pulse.setInterval(520);QObject::connect(&pulse,&QTimer::timeout,this,[this]{dim=!dim;update();});
    }
    void refresh(){if(isVisible()&&owner->backend->busy()){if(!pulse.isActive())pulse.start();}else{pulse.stop();dim=false;}update();}
protected:
    void showEvent(QShowEvent *event)override{QWidget::showEvent(event);refresh();}
    void hideEvent(QHideEvent *event)override{pulse.stop();dim=false;QWidget::hideEvent(event);}
    void mousePressEvent(QMouseEvent *e)override {
        if(e->button()==Qt::LeftButton)owner->Toggle();
        if(e->button()==Qt::RightButton) {
            auto *menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->addAction("打开对话",owner,&Drawer::Show);menu->addAction("设置…",owner,&Drawer::Preferences);
            menu->addSeparator();menu->addAction("退出",owner,&Drawer::Quit);menu->popup(e->globalPos());
        }
    }
    void paintEvent(QPaintEvent *)override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(rect(),icon("app").pixmap(size()*devicePixelRatioF()));
        const int cx=width()/2,cy=height()/2;
        if(owner->backend->busy()||owner->unread) {
            p.setPen(Qt::NoPen);p.setBrush(QColor(150,240,196,owner->backend->busy()&&dim?90:240));
            p.drawEllipse(QPointF(cx,cy+13),3,3);
        }
    }
private: Drawer *owner;QTimer pulse;bool dim=false;
};

Drawer::Drawer(Settings config,QString conversationFolder):QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint),
    settings(std::move(config)),history(std::move(conversationFolder)) {
    Q_INIT_RESOURCE(resources);
    qApp->setWindowIcon(icon("app"));
    LiquidPopup::install(*qApp);
    setObjectName("liquidCliDrawer");setWindowTitle("AI小站");setWindowIcon(icon("app"));
    setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);
    setFont(QFont("Noto Sans CJK SC",10));
    quickReply=settings.backend=="codex"&&settings.defaultReplyMode=="quick";
    history.useBackend(settings.backend,quickReply);history.load();backend=createAgentClient(effectiveSettings(),this);backend->setThreadId(history.threadId);
    buildInterface();connectClient();
    loadDraft();input->setBackendName(settings.cliName);
    view->setTextPoint(settings.fontSize);view->reset(history.messages());
    QFont inputFont=input->editor()->font();inputFont.setPointSize(settings.fontSize);input->editor()->setFont(inputFont);updateContext();
    hideTimer.setSingleShot(true);connect(&hideTimer,&QTimer::timeout,this,[this]{
        if(pinned||capturing||(preferences&&preferences->isVisible())||QApplication::activePopupWidget()||QApplication::activeModalWidget())return;
        if(geometry().contains(QCursor::pos()))return;
        if(engaged&&isActiveWindow())return;
        Hide();
    });
    connect(&motion,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){place(v.toReal());});
    connect(&motion,&QVariantAnimation::finished,this,[this]{
        if(!opened){QWidget::hide();setWindowOpacity(1);}
        else {
            // Removing the native animation mask does not invalidate all of
            // Qt's translucent backing store (including child backgrounds).
            place(1);repaint();
        }
    });
    edge=new Edge(this);positionEdge();
    auto *panelWatcher=new QDBusServiceWatcher("org.ukui.AiStationPanel",QDBusConnection::sessionBus(),QDBusServiceWatcher::WatchForOwnerChange,this);
    connect(panelWatcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this]{syncPanelEntry();if(panelHosted&&!opened)QTimer::singleShot(150,this,&Drawer::prepareResident);});
    syncPanelEntry();
    for(auto *screen:QGuiApplication::screens())
        connect(screen,&QScreen::availableGeometryChanged,this,[this]{positionEdge();if(opened){motion.stop();place(1);materialKey.clear();refreshGlass();}});
    connect(qApp,&QGuiApplication::screenAdded,this,[this]{positionEdge();});
    connect(qApp,&QGuiApplication::screenRemoved,this,[this]{positionEdge();if(opened){motion.stop();place(1);refreshGlass();}});
    watcher=new QFileSystemWatcher(this);
    for(const auto &dir:QStringList{configRoot()+"/kyfences",configRoot()+"/kylin"})if(QDir(dir).exists())watcher->addPath(dir);
    wallpaperTimer.setSingleShot(true);wallpaperTimer.setInterval(400);
    connect(watcher,&QFileSystemWatcher::directoryChanged,this,[this]{wallpaperTimer.start();});
    connect(&wallpaperTimer,&QTimer::timeout,this,[this]{if(opened)refreshGlass();else materialKey.clear();});
    qApp->installEventFilter(this);
}
Drawer::~Drawer(){quitting=true;delete preferences.data();backend->disconnect(this);backend->stop();persist();delete edge;}

void Drawer::buildInterface() {
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(18,14,16,14);outer->setSpacing(8);
    auto *header=new QHBoxLayout;header->setSpacing(4);
    auto *title=new QLabel("AI小站",this);title->setObjectName("drawerTitle");
    statusLabel=new QLabel(this);statusLabel->setObjectName("cliStatus");
    header->addWidget(title);header->addSpacing(6);header->addWidget(statusLabel,1);
    contextButton=new QPushButton(this);contextButton->setObjectName("contextButton");contextButton->setCursor(Qt::PointingHandCursor);
    contextButton->setToolTip("当前上下文用量 · 点击压缩");header->addWidget(contextButton);
    connect(contextButton,&QPushButton::clicked,this,[this]{
        QMenu menu(this);auto *now=menu.addAction("压缩上下文",[this]{backend->setThreadId(history.threadId);backend->compact();showCompactNotice();});
        now->setEnabled(!history.messages().isEmpty()&&!backend->compacting()&&backend->supportsCompaction());
        menu.exec(contextButton->mapToGlobal(QPoint(0,contextButton->height())));
    });
    pin=toolButton("pin","固定展开",this);pin->setObjectName("pinDrawer");pin->setCheckable(true);
    auto *gear=toolButton("settings","设置",this);gear->setObjectName("preferences");
    auto *collapse=toolButton("collapse","收起（Esc）",this);collapse->setObjectName("collapseDrawer");
    header->addWidget(pin);header->addWidget(gear);header->addWidget(collapse);outer->addLayout(header);
    connect(pin,&QPushButton::toggled,this,[this](bool on){pinned=on;if(on)hideTimer.stop();else scheduleHide();});
    connect(gear,&QPushButton::clicked,this,&Drawer::Preferences);connect(collapse,&QPushButton::clicked,this,&Drawer::Hide);
    replyModeRow=new QWidget(this);
    auto *modeLayout=new QHBoxLayout(replyModeRow);modeLayout->setContentsMargins(0,0,0,0);
    replyModeLabel=new QLabel(replyModeRow);replyModeLabel->setObjectName("replyModeLabel");
    quickReplyButton=new QPushButton("快问快答",replyModeRow);quickReplyButton->setObjectName("quickReplyMode");
    quickReplyButton->setCheckable(true);quickReplyButton->setCursor(Qt::PointingHandCursor);
    quickReplyButton->setToolTip("快问快答使用 Luna low 和独立对话；普通思考使用原模型设置。启动时的默认模式可在设置中选择。");
    modeLayout->addWidget(replyModeLabel,1);modeLayout->addWidget(quickReplyButton);outer->addWidget(replyModeRow);
    connect(quickReplyButton,&QPushButton::clicked,this,[this](bool checked){setQuickReply(checked);});
    updateReplyMode();
    view=new ChatView(this);outer->addWidget(view,1);
    notices=new QVBoxLayout;notices->setSpacing(6);outer->addLayout(notices);
    input=new Composer(this);outer->addWidget(input);
    connect(input,&Composer::submit,this,&Drawer::submit);
    connect(input,&Composer::stopRequested,this,[this]{backend->interrupt();});
    connect(input,&Composer::captureRequested,this,&Drawer::beginCapture);
    connect(input,&Composer::imagePasted,this,&Drawer::attachImage);
    connect(input,&Composer::imageFileDropped,this,[this](const QString &path){QImageReader r(path);r.setAutoTransform(true);attachImage(r.read());});
    setStatus(history.messages().isEmpty()?QStringLiteral("随时开始"):QStringLiteral("对话已保留"));refreshColors();
}

void Drawer::connectClient() {
    connect(backend,&AgentClient::status,this,&Drawer::setStatus);
    connect(backend,&AgentClient::threadReady,this,[this](const QString &id,bool resumed){
        if(!resumed&&!history.threadId.isEmpty()&&history.threadId!=id) {
            ChatMessage note{"note","之前的模型上下文无法恢复，已从新上下文继续；聊天记录仍保留在这里。",{},{},0};
            history.append(note);view->append(note);
        }
        history.threadId=id;persist();
    });
    connect(backend,&AgentClient::answer,this,[this](const QString &text){
        if(answerIndex<0) {
            ChatMessage m{"assistant",text,{},{},0};answerIndex=history.append(m);view->append(history.messages().at(answerIndex));
        } else {
            auto m=history.messages().at(answerIndex);m.text=text;history.replace(answerIndex,m);view->updateLast(m);
        }
    });
    connect(backend,&AgentClient::turnFinished,this,[this](const QString &state,const QString &error,const QString &summary){
        QStringList meta;if(state=="interrupted")meta<<"已停止";if(state=="failed")meta<<"未完成";if(!summary.isEmpty())meta<<summary;
        if(answerIndex>=0) {
            auto m=history.messages().at(answerIndex);m.meta=meta.join(QStringLiteral(" · "));history.replace(answerIndex,m);view->updateLast(m);
        }
        if(!error.isEmpty()||(answerIndex<0&&state!="completed")) {
            ChatMessage note{"note",error.isEmpty()?QStringLiteral("已停止，没有收到回复"):"没有完成："+error,{},{},0};
            history.append(note);view->setTyping(false);view->append(note);
        }
        answerIndex=-1;view->setTyping(false);input->setBusy(backend->busy());persist();
        if(!opened)unread=true;edge->refresh();
    });
    connect(backend,&AgentClient::contextUsage,this,[this](qint64 tokens,qint64 window){
        history.contextTokens=tokens;history.contextWindow=window;updateContext();
    });
    connect(backend,&AgentClient::compactionFinished,this,[this](bool ok,const QString &error){
        if(compactNotice)compactNotice->deleteLater();
        if(ok){history.contextTokens=0;compactDismissedAt=0;}
        ChatMessage note{"note",ok?QStringLiteral("已压缩上下文：较早的内容整理成了摘要，聊天记录不变。"):"压缩没有完成："+error,{},{},0};
        history.append(note);view->append(note);updateContext();persist();
        view->setTyping(backend->busy()&&answerIndex>=0);input->setBusy(backend->busy());edge->refresh();
    });
    connect(backend,&AgentClient::approvalRequested,this,&Drawer::addApproval);
    connect(backend,&AgentClient::approvalClosed,this,[this](const QJsonValue &id){
        for(auto *card:findChildren<QFrame*>("notice"))if(card->property("requestId").toJsonValue()==id)card->deleteLater();
    });
}

void Drawer::submit(const QString &text,const QStringList &images) {sendMessage(text,images,true);}
void Drawer::sendMessage(const QString &text,const QStringList &images,bool clearComposer) {
    if(!history.canSave()){setStatus("原聊天记录读取失败，已保留原件；请恢复文件或在设置中开始新对话。");return;}
    if(backend->busy()||(text.isEmpty()&&images.isEmpty()))return;
    ChatMessage m{"user",text,images,{},0};history.append(m);view->append(history.messages().last());
    view->followBottom();if(clearComposer)input->clear();answerIndex=-1;view->setTyping(true);input->setBusy(true);
    backend->setThreadId(history.threadId);backend->send(text,images);
    persist();edge->refresh();
}
void Drawer::attachImage(const QImage &image) {
    if(image.isNull()){setStatus("无法读取这张图片");return;}
    QString error;const QString path=history.storeImage(image,&error);
    if(path.isEmpty()){setStatus(error);return;}
    input->addImage(path);input->setHint("图片已附上 · 补充说明后按 Enter 发送，不会自动发送");
}
void Drawer::updateContext() {
    const int limitK=effectiveSettings().contextLimitK;
    const qint64 limit=qint64(limitK)*1000;const qint64 tokens=history.contextTokens;
    contextButton->setText("上下文 "+kilo(tokens)+" / "+QString::number(limitK)+"K");
    contextButton->setStyleSheet(tokens>=limit?"color:#ffd59a;":"");
    if(tokens>=limit&&tokens>=compactDismissedAt+kDismissStepTokens&&!backend->compacting())showCompactNotice();
}
void Drawer::showCompactNotice() {
    if(compactNotice){compactNotice->raise();return;}
    auto *card=new QFrame(this);card->setObjectName("notice");card->setProperty("kind","compact");compactNotice=card;
    auto *layout=new QVBoxLayout(card);layout->setContentsMargins(14,10,14,10);layout->setSpacing(8);
    auto *text=new QLabel(card);text->setWordWrap(true);layout->addWidget(text);
    auto *buttons=new QHBoxLayout;buttons->addStretch();
    auto *later=new QPushButton("稍后",card);auto *now=new QPushButton("立即压缩",card);now->setObjectName("primary");
    buttons->addWidget(later);buttons->addWidget(now);layout->addLayout(buttons);
    auto refresh=[this,text,now,later]{
        const bool running=backend->compacting();
        text->setText(running?QStringLiteral("正在压缩上下文…"):
            QString("上下文已用 %1，超过 %2K。可将较早内容整理成摘要，聊天记录仍保留。").arg(kilo(history.contextTokens)).arg(effectiveSettings().contextLimitK));
        now->setEnabled(!running&&backend->supportsCompaction());later->setVisible(!running);
        if(!running&&!backend->supportsCompaction())text->setText(QString("上下文已用 %1。当前后台未提供压缩入口，可在设置里开始新对话。").arg(kilo(history.contextTokens)));
    };
    refresh();
    connect(later,&QPushButton::clicked,card,[this,card]{compactDismissedAt=history.contextTokens;card->deleteLater();});
    connect(now,&QPushButton::clicked,card,[this,refresh]{
        backend->setThreadId(history.threadId);backend->compact();input->setBusy(backend->busy());refresh();
    });
    connect(backend,&AgentClient::status,card,[refresh]{refresh();});
    connect(backend,&AgentClient::capabilitiesChanged,card,[refresh]{refresh();});
    notices->addWidget(card);
}
void Drawer::addApproval(const QJsonValue &id,const QString &title,const QString &detail) {
    auto *card=new QFrame(this);card->setObjectName("notice");card->setProperty("requestId",QVariant::fromValue(id));
    auto *layout=new QVBoxLayout(card);layout->setContentsMargins(14,10,14,10);layout->setSpacing(6);
    auto *heading=new QLabel(title,card);heading->setStyleSheet("font-weight:600;");layout->addWidget(heading);
    auto *body=new QPlainTextEdit(card);body->setReadOnly(true);body->setPlainText(detail);
    body->setAccessibleName(QStringLiteral("完整审批内容"));
    body->setMinimumHeight(70);body->setMaximumHeight(180);
    body->setStyleSheet("font-family:'Sarasa Fixed SC',monospace;font-size:12px;");layout->addWidget(body);
    if(detail.size()>600){auto *hint=new QLabel(QStringLiteral("完整内容共 %1 字，可滚动查看或全选复制。请确认后再批准。").arg(detail.size()),card);hint->setWordWrap(true);layout->addWidget(hint);}
    auto *buttons=new QHBoxLayout;buttons->addStretch();
    const auto choices=backend->approvalOptions(id);
    for(const auto &choice:choices) {
        auto *b=new QPushButton(choice.first,card);if(choice.second=="accept")b->setObjectName("primary");
        connect(b,&QPushButton::clicked,card,[this,card,id,decision=choice.second]{backend->answerApproval(id,decision);card->deleteLater();});
        buttons->addWidget(b);
    }
    layout->addLayout(buttons);notices->addWidget(card);
    if(!opened)reveal(false);unread=!opened;edge->refresh();
}
void Drawer::setStatus(const QString &text){statusLabel->setText(text);if(edge)edge->refresh();}
void Drawer::loadDraft() {
    input->clear();
    QFile draft(history.folder()+"/draft.json");
    if(draft.open(QIODevice::ReadOnly)) {
        const auto saved=QJsonDocument::fromJson(draft.readAll()).object();input->editor()->setPlainText(saved.value("text").toString());
        for(const auto &value:saved.value("images").toArray()){const QString path=value.toString();if(QFileInfo(path).isFile()&&QFileInfo(path).absolutePath()==history.folder()+"/images")input->addImage(path);}
    }
}
bool Drawer::persist() {
    QString error;if(!history.save(&error)){if(statusLabel)statusLabel->setText("聊天记录保存失败："+error);return false;}
    if(!input)return true;
    const QString path=history.folder()+"/draft.json";
    if(input->editor()->toPlainText().isEmpty()&&input->images().isEmpty()){
        if(!QFileInfo::exists(path)||QFile::remove(path))return true;
        setStatus("草稿无法保存，未切换对话。");return false;
    }
    const auto data=QJsonDocument(QJsonObject{{"text",input->editor()->toPlainText()},{"images",QJsonArray::fromStringList(input->images())}}).toJson(QJsonDocument::Compact);
    QSaveFile draft(path);
    if(draft.open(QIODevice::WriteOnly)){
        draft.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
        if(draft.write(data)==data.size()&&draft.commit())return true;
    }
    setStatus("草稿无法保存："+draft.errorString());return false;
}
void Drawer::clearConversation() {
    backend->stop();if(!history.clear()){setStatus("无法保留原聊天记录，未开始新对话。");return;}input->clear();persist();backend->setThreadId({});answerIndex=-1;compactDismissedAt=0;
    for(auto *card:findChildren<QFrame*>("notice"))card->deleteLater();
    view->setTyping(false);view->reset({});input->clear();input->setBusy(false);updateContext();setStatus("已清空，下一条消息开始新对话");
}

QWidget *Drawer::edgeWidget() const{return edge;}
QRect Drawer::targetGeometry() const {
    const auto *screen=panelAnchor.isValid()?QGuiApplication::screenAt(panelAnchor.center()):currentScreen(this);
    if(!screen)screen=QGuiApplication::primaryScreen();
    const QRect available=screen?screen->availableGeometry():QRect(0,0,1280,800);
    int height=available.height()-28;
    const bool below=panelAnchor.isValid()&&panelAnchor.center().y()<available.center().y();
    if(panelAnchor.isValid())height=qMin(height,below?available.bottom()-panelAnchor.bottom()-16:panelAnchor.top()-available.top()-16);
    const QSize size(qMin(settings.width,qMax(280,available.width()-20)),qMin(settings.height,qMax(240,height)));
    if(panelAnchor.isValid()) {
        const int x=qBound(available.left()+8,panelAnchor.right()-size.width()+1,available.right()-size.width()-7);
        const int y=qBound(available.top()+8,below?panelAnchor.bottom()+8:panelAnchor.top()-size.height()-8,available.bottom()-size.height()-7);
        return QRect(QPoint(x,y),size);
    }
    return QRect(QPoint(available.right()-size.width()-9,available.center().y()-size.height()/2),size);
}
void Drawer::syncPanelEntry() {
    auto *bus=QDBusConnection::sessionBus().interface();
    panelHosted=bus&&bus->isServiceRegistered("org.ukui.AiStationPanel").value();
    edge->setVisible(!panelHosted&&!capturing);
}
void Drawer::positionEdge() {
    const auto target=targetGeometry();const auto *screen=currentScreen(this);const QRect area=screen?screen->availableGeometry():QRect(0,0,1280,800);
    edge->setGeometry(area.right()-kEdgeWidth-8,area.bottom()-kEdgeHeight-8,kEdgeWidth,kEdgeHeight);
}
void Drawer::refreshGlass() {
    const auto area=targetGeometry();const auto *screen=currentScreen(this);const auto screenRect=screen?screen->geometry():QRect(0,0,1280,800);
    const qreal dpr=qMin(2.,devicePixelRatioF());const auto path=wallpaperPath();const QFileInfo f(path);
    const auto key=QStringList{path,QString::number(f.lastModified().toMSecsSinceEpoch()),QString::number(f.size()),QString::number(area.x()),QString::number(area.y()),
        QString::number(area.width()),QString::number(area.height()),QString::number(settings.liquid),QString::number(settings.darken),QString::number(settings.followLauncherMaterial),QString::number(dpr)}.join('/');
    if(key==materialKey&&!material.isNull())return;
    const auto tile=wallpaperTile(path,area,screenRect,dpr);
    if(settings.followLauncherMaterial) {
        if(!launcherMaterial)launcherMaterial=std::make_unique<NextKdeGlassView>(this);
        launcherMaterial->setRadius(26);
        if(qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_FAST"))launcherMaterial->setBackdropFast(tile);else launcherMaterial->setBackdrop(tile);
        material=launcherMaterial->image();
    } else {
        LiquidOpticsRenderer renderer;renderer.setWallpaper(tile);renderer.setMaterial(0,settings.liquid/100.);
        material=renderer.renderPanel(QRect(QPoint(),area.size()),24);
        if(material.isNull())material=tile;
        QPainter p(&material);p.setCompositionMode(QPainter::CompositionMode_SourceAtop);p.fillRect(QRect(QPoint(),area.size()),QColor(0,0,0,qRound(settings.darken*2.55)));
    }
    materialKey=key;++builds;refreshColors();update();
}
void Drawer::refreshColors() {
    if(!material.isNull()) {
        qreal sum=0;int count=0;
        for(int y=material.height()/8;y<material.height()*7/8;y+=qMax(1,material.height()/12))
            for(int x=material.width()/8;x<material.width()*7/8;x+=qMax(1,material.width()/12)) {
                const auto c=material.pixelColor(x,y);if(c.alpha()<128)continue;
                sum+=.2126*c.redF()+.7152*c.greenF()+.0722*c.blueF();++count;
            }
        lightSurface=count&&sum/count>.60;
    }
    const auto c=UiColors::forSurface(lightSurface);setPalette(c.palette(palette()));
    setStyleSheet(QString(
        "QWidget#liquidCliDrawer{color:%1;} QLabel{background:transparent;color:%1;}"
        "QLabel#drawerTitle{font-size:17px;font-weight:600;} QLabel#cliStatus{color:%2;font-size:11px;padding-top:3px;}"
        "QPushButton{background:transparent;border:0;border-radius:9px;padding:4px 8px;color:%1;}"
        "QPushButton:hover{background:rgba(130,190,170,35);} QPushButton:pressed,QPushButton:checked{background:rgba(130,190,170,65);}"
        "QPushButton:focus{border:1px solid %3;} QPushButton#earlierMessages{color:%2;font-size:12px;}"
        "QPushButton#contextButton{color:%2;font-size:11px;padding:3px 8px;border-radius:10px;}"
        "QFrame#notice{background:%4;border:1px solid %5;border-radius:14px;}"
        "QFrame#notice QPushButton{background:rgba(130,190,170,45);border-radius:9px;padding:5px 10px;}"
        "QScrollArea,QWidget#chatViewport,QWidget#chatContent{background:transparent;border:0;}"
        "QScrollBar:vertical{background:transparent;width:7px;} QScrollBar::handle:vertical{background:rgba(145,175,185,100);min-height:24px;border-radius:3px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}")
        .arg(c.text,c.muted,c.accent,c.field,c.border));
    view->setColors(lightSurface);input->setColors(lightSurface);
    // SVG strokes must follow the same foreground as the labels.
    for(const auto &pair:QList<QPair<QString,QString>>{{"pinDrawer","pin"},{"preferences","settings"},{"collapseDrawer","collapse"}}) {
        auto *button=findChild<QPushButton*>(pair.first);if(!button)continue;
        QPixmap pixmap=icon(pair.second).pixmap(QSize(20,20)*devicePixelRatioF());
        QPainter painter(&pixmap);painter.setCompositionMode(QPainter::CompositionMode_SourceIn);painter.fillRect(pixmap.rect(),QColor(c.text));painter.end();
        button->setIcon(QIcon(pixmap));
    }
}
void Drawer::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setCompositionMode(QPainter::CompositionMode_Source);p.fillRect(rect(),Qt::transparent);p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if(!material.isNull())p.drawImage(QPoint(),material);
    else{QPainterPath path;path.addRoundedRect(QRectF(rect()),24,24);p.fillPath(path,QColor("#27323a"));}
    p.setPen(QPen(QColor(255,255,255,65),1));p.setBrush(Qt::NoBrush);const qreal radius=settings.followLauncherMaterial?26:23;p.drawRoundedRect(QRectF(rect()).adjusted(.8,.8,-.8,-.8),radius,radius);
}

void Drawer::reveal(bool activate) {
    if(capturing)return;hideTimer.stop();
    if(!opened){opened=true;refreshGlass();animate(true);}
    if(activate){engaged=true;raise();activateWindow();input->focusInput();}
    unread=false;edge->refresh();
}
void Drawer::prepareResident() {
    if(opened)return;
    auto call=QDBusMessage::createMethodCall("org.ukui.AiStationPanel","/AiStation","org.ukui.AiStationPanel","State");
    auto *request=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call,1200),this);
    connect(request,&QDBusPendingCallWatcher::finished,this,[this](QDBusPendingCallWatcher *w){
        const QDBusPendingReply<QString> reply=*w;w->deleteLater();if(opened)return;
        if(!reply.isError())for(const auto &v:QJsonDocument::fromJson(reply.value().toUtf8()).object().value("orbs").toArray()) {
            const auto orb=v.toObject();const QRect anchor(orb["x"].toInt(),orb["y"].toInt(),orb["width"].toInt(),orb["height"].toInt());
            if(orb["visible"].toBool()&&QGuiApplication::screenAt(anchor.center())){panelAnchor=anchor;break;}
        }
        refreshGlass();
    });
}
void Drawer::Show(){reveal(true);}
void Drawer::Hide(){if(capturing)return;hideTimer.stop();if(!opened)return;opened=false;engaged=false;animate(false);}
void Drawer::Toggle(){if(opened&&shown>.5)Hide();else Show();}
void Drawer::ToggleAt(int x,int y,int width,int height) {
    const QRect next(x,y,width,height);
    if(width>0&&height>0&&width<4096&&height<4096&&QGuiApplication::screenAt(next.center()))panelAnchor=next;
    Toggle();
}
bool Drawer::ImportDropAt(const QString &manifest,int x,int y,int width,int height) {
    const auto payload=StationDrop::read(manifest);if(payload.isEmpty())return false;
    const QRect anchor(x,y,width,height);if(width>0&&height>0&&QGuiApplication::screenAt(anchor.center()))panelAnchor=anchor;
    reveal(true);QStringList paths;
    for(const auto &value:payload.value("files").toArray()) {
        const QString path=value.toString();if(!QDir::isAbsolutePath(path))continue;
        paths<<path;
        if(!QImageReader::imageFormat(path).isEmpty()){QImageReader reader(path);reader.setAutoTransform(true);attachImage(reader.read());}
    }
    const QString image=payload.value("image").toString();
    if(StationDrop::owns(image)){QImageReader reader(image);reader.setAutoTransform(true);attachImage(reader.read());}
    const QString text=payload.value("text").toString();if(!text.isEmpty())paths<<text;
    input->appendText(paths.join('\n'));StationDrop::discard(manifest);input->setHint("已放入输入区 · 检查后按 Enter 发送");persist();return true;
}
void Drawer::place(qreal value) {
    shown=value;const QRect target=targetGeometry();
    const int distance=settings.animation=="slide"?target.height()+14:kFloatDistance;
    const auto *screen=QGuiApplication::screenAt(panelAnchor.center());
    const bool topAnchor=panelAnchor.isValid()&&screen&&panelAnchor.center().y()<screen->availableGeometry().center().y();
    if(size()!=target.size())resize(target.size());
    if(panelAnchor.isValid()) {DockPopupMotion::apply(this,target,topAnchor?2:3,value);return;}
    clearMask();
    move(target.topLeft()+QPoint(0,qRound((topAnchor?-1:1)*distance*(1-value))));
    setWindowOpacity(settings.animation=="float"?qBound(0.,value*1.2,1.):1.);
}
void Drawer::animate(bool show) {
    motion.stop();const qreal from=isVisible()?shown:0.;
    if(show&&!isVisible()){
        // Populate the whole backing store before the taskbar mask clips it.
        // Material-cache readiness alone does not mean these pixels were
        // painted. Keep this preparation invisible and outside the frame loop.
        setGeometry(targetGeometry());clearMask();setWindowOpacity(0);
        QWidget::show();layout()->activate();repaint();place(from);raise();
    }
    bool reduced=QSettings("org.ukui","style").value("reduceAnimation",false).toBool();
    if(settings.animation=="none"||reduced){place(show?1:0);if(show)repaint();else{QWidget::hide();setWindowOpacity(1);}return;}
    // Reversing mid-way continues from the current point with the remaining time.
    const int duration=panelAnchor.isValid()?(show?DockPopupMotion::OpenMs:DockPopupMotion::CloseMs):settings.animationMs;
    motion.setDuration(qMax(40,qRound(duration*qAbs((show?1.:0.)-from))));
    motion.setEasingCurve(show?QEasingCurve::OutCubic:QEasingCurve::InCubic);
    motion.setStartValue(from);motion.setEndValue(show?1.:0.);motion.start();
}
void Drawer::enterEvent(QEvent *){hideTimer.stop();}
void Drawer::scheduleHide(){if(opened&&!pinned&&!capturing&&!(preferences&&preferences->isVisible()))hideTimer.start(settings.hideDelay);}
void Drawer::leaveEvent(QEvent *){if(!engaged)scheduleHide();}
void Drawer::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Escape){Hide();return;}QWidget::keyPressEvent(e);}
bool Drawer::eventFilter(QObject *watched,QEvent *event) {
    auto *w=qobject_cast<QWidget*>(watched);const bool inside=w&&(w==this||isAncestorOf(w));
    if(inside&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::KeyPress)) {
        // A click or keystroke means the user is working here: stop treating
        // the drawer as a hover preview, and take keyboard focus if needed.
        if(!engaged){engaged=true;if(event->type()==QEvent::MouseButtonPress&&!isActiveWindow())activateWindow();}
        hideTimer.stop();
    }
    if(event->type()==QEvent::WindowDeactivate&&watched==this){engaged=false;scheduleHide();}
    if(preferences&&watched==preferences&&event->type()==QEvent::Hide)QTimer::singleShot(0,this,[this]{scheduleHide();});
    return QWidget::eventFilter(watched,event);
}
void Drawer::closeEvent(QCloseEvent *event){if(quitting)event->accept();else{event->ignore();Hide();}}

void Drawer::beginCapture() {
    if(capturing)return;hideTimer.stop();motion.stop();capturing=true;opened=false;QWidget::hide();edge->hide();
    // Bounded settling wait lets KWin remove both surfaces before reading screen pixels.
    QTimer::singleShot(140,this,[this]{RegionCapture::beginWithGeometry([owner=QPointer<Drawer>(this)](QImage image,QRect selection){if(owner)owner->acceptCapture(image,selection);});});
}
void Drawer::acceptCapture(const QImage &image,const QRect &selection) {
    if(image.isNull()){capturing=false;syncPanelEntry();shown=0;reveal(true);setStatus("已取消截图");return;}
    capturing=true;QWidget::hide();edge->hide();
    auto *editor=new CaptureEditor(image,this,selection);editor->setAttribute(Qt::WA_DeleteOnClose);editor->setProcessingAvailable(!backend->busy());
    connect(backend,&AgentClient::turnFinished,editor,[this,editor]{editor->setProcessingAvailable(!backend->busy());});
    connect(editor,&QDialog::finished,this,[this,editor](int result){
        const auto image=editor->resultImage();const auto action=editor->selectedAction();
        QTimer::singleShot(0,this,[this,image,action,result]{
            capturing=false;syncPanelEntry();shown=0;reveal(true);
            if(result!=QDialog::Accepted){setStatus("已取消截图编辑");return;}
            processCapture(image,action);
        });
    });
    editor->show();editor->raise();editor->activateWindow();
}
void Drawer::processCapture(const QImage &image,const QString &action) {
    if(image.isNull())return;
    if(action=="insert"){attachImage(image);persist();return;}
    if(backend->busy()){attachImage(image);input->setHint("当前回复尚未结束；截图已放入输入框。");persist();return;}
    QString error;const QString path=history.storeImage(image,&error);if(path.isEmpty()){setStatus(error);return;}
    sendMessage(CaptureEditor::actionPrompt(action,settings.translationLanguage),{path},false);
}
Settings Drawer::effectiveSettings() const {
    Settings effective=settings;
    if(quickReply&&settings.backend=="codex"){
        effective.model="gpt-6-luna";effective.reasoningEffort="low";
        effective.contextLimitK=qMin(settings.contextLimitK,64);
    }
    return effective;
}
void Drawer::updateReplyMode() {
    if(!replyModeRow)return;
    replyModeRow->setVisible(settings.backend=="codex");
    const QSignalBlocker block(quickReplyButton);quickReplyButton->setChecked(quickReply);
    QString normal="普通思考";
    if(settings.model=="gpt-6-luna")normal+=" · Luna "+(settings.reasoningEffort.isEmpty()?QString("默认强度"):settings.reasoningEffort);
    replyModeLabel->setText(quickReply?QString("独立对话 · Luna low"):normal);
    quickReplyButton->setText(quickReply?QString("切回普通思考"):QString("快问快答"));
}
bool Drawer::setQuickReply(bool enabled) {
    if(enabled==quickReply){updateReplyMode();return true;}
    if(settings.backend!="codex"||backend->busy()||backend->compacting()||capturing){
        updateReplyMode();setStatus("请等待当前任务结束，再切换问答模式。");return false;
    }
    Conversation next=history;next.useBackend(settings.backend,enabled);next.load();
    if(!next.canSave()){updateReplyMode();setStatus("目标对话读取失败，已保留当前对话及原文件。");return false;}
    if(!persist()){updateReplyMode();return false;}
    backend->disconnect(this);backend->stop();delete backend;
    for(auto *card:findChildren<QFrame*>("notice"))delete card;
    compactNotice.clear();answerIndex=-1;compactDismissedAt=0;unread=false;
    quickReply=enabled;history=std::move(next);
    backend=createAgentClient(effectiveSettings(),this);backend->setThreadId(history.threadId);connectClient();
    view->setTyping(false);view->reset(history.messages());input->setBusy(false);loadDraft();
    updateReplyMode();updateContext();setStatus(quickReply?QString("快问快答 · 独立上下文"):QString("已返回普通对话"));
    return true;
}
void Drawer::applySettings(Settings value) {
    if(backend->busy()&&(settings.backend!=value.backend||settings.command!=value.command||settings.directory!=value.directory||settings.permissions!=value.permissions)) {
        setStatus("请先停止当前回复，再更改后台、命令、目录或权限。");return;
    }
    value.normalize();const bool geometryChanged=settings.width!=value.width||settings.height!=value.height;
    const bool materialChanged=geometryChanged||settings.liquid!=value.liquid||settings.darken!=value.darken||settings.followLauncherMaterial!=value.followLauncherMaterial;
    const bool switched=settings.backend!=value.backend;
    if(switched&&(backend->busy()||backend->compacting())){setStatus("请先停止当前回复再切换后台");return;}
    if(switched) {
        if(!persist())return;
        backend->disconnect(this);backend->stop();delete backend;
        for(auto *card:findChildren<QFrame*>("notice"))delete card;
        compactNotice.clear();answerIndex=-1;compactDismissedAt=0;unread=false;
        quickReply=false;settings=value;history.useBackend(settings.backend);history.load();backend=createAgentClient(effectiveSettings(),this);backend->setThreadId(history.threadId);connectClient();
        view->setTyping(false);view->reset(history.messages());input->setBusy(false);loadDraft();
        setStatus(settings.cliName+" · "+(history.messages().isEmpty()?QString("随时开始"):QString("对话已保留")));
    } else {
        if(!backend->busy()&&(settings.command!=value.command||settings.directory!=value.directory||settings.permissions!=value.permissions))backend->stop();
        settings=value;backend->configure(effectiveSettings());
    }
    updateReplyMode();
    input->setBackendName(settings.cliName);positionEdge();view->setTextPoint(settings.fontSize);QFont font=input->editor()->font();font.setPointSize(settings.fontSize);input->editor()->setFont(font);updateContext();
    if(materialChanged)materialKey.clear();
    if(opened){motion.stop();place(1);refreshGlass();}
}
void Drawer::Reload(){if(preferences&&!preferences->isVisible()&&!QApplication::activeModalWidget())delete preferences.data();applySettings(Settings::load(settings.file));}
QString Drawer::State() const {
    QJsonObject state{{"expanded",opened},{"progress",shown},{"capturing",capturing},{"busy",backend->busy()},{"sessionRunning",backend->running()},
        {"backend",settings.backend},{"backendName",settings.cliName},{"messages",history.messages().size()},{"hasThread",!history.threadId.isEmpty()},{"contextTokens",double(history.contextTokens)},
        {"residentOnLogin",ResidentStartup::enabled()},{"glassBuilds",builds},{"status",statusLabel->text()},{"pid",double(QCoreApplication::applicationPid())},{"unread",unread},
        {"x",x()},{"y",y()},{"width",width()},{"height",height()},{"edgeX",edge->x()},{"edgeY",edge->y()},
        {"panelHosted",panelHosted},{"edgeVisible",edge->isVisible()},{"mouseGrabbed",QWidget::mouseGrabber()!=nullptr},
        {"settingsVisible",preferences&&preferences->isVisible()},{"draftCharacters",input->editor()->toPlainText().size()},{"draftImages",input->images().size()}};
    state["replyMode"]=quickReply?"quick":"normal";
    state["defaultReplyMode"]=settings.defaultReplyMode;
    state["effectiveModel"]=effectiveSettings().model;state["effectiveEffort"]=effectiveSettings().reasoningEffort;
    if(preferences) {state["settingsX"]=preferences->x();state["settingsY"]=preferences->y();state["settingsWidth"]=preferences->width();state["settingsHeight"]=preferences->height();if(preferences->isVisible())state["settingsWindowId"]=QString::number(preferences->winId());}
    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}
void Drawer::Quit(){quitting=true;backend->stop();persist();qApp->quit();}
