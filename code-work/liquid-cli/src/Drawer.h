#pragma once
#include <QWidget>
#include <memory>
#include <QTimer>
#include <QPointer>
#include <QVariantAnimation>
#include <QJsonValue>
#include "Settings.h"
#include "Conversation.h"
#include "AgentClient.h"
class QLabel;
class QPushButton;
class QVBoxLayout;
class QDialog;
class QFileSystemWatcher;
class ChatView;
class Composer;
class Edge;
class NextKdeGlassView;

class Drawer:public QWidget {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.ukui.LiquidCli")
public:
    explicit Drawer(Settings settings,QString conversationFolder={});
    ~Drawer() override;
    Settings configuration() const{return settings;}
    QRect targetGeometry() const;
    QWidget *edgeWidget() const;
    AgentClient *client(){return backend;}
    ChatView *chat() const{return view;}
    Composer *composer() const{return input;}
    const Conversation &conversation() const{return history;}
    int glassBuilds() const{return builds;}
    bool expanded() const{return opened;}
    qreal progress() const{return shown;}
    void applySettings(Settings value);
    bool setQuickReply(bool enabled);
    void beginCapture();
    void acceptCapture(const QImage &image,const QRect &selection=QRect());
    void submit(const QString &text,const QStringList &images);
    void processCapture(const QImage &image,const QString &action);
    void clearConversation();
    bool startNewConversation();
    void prepareResident();
    QWidget *settingsBridgeRoot();
public slots:
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Hide();
    Q_SCRIPTABLE void Toggle();
    Q_SCRIPTABLE void ToggleAt(int x,int y,int width,int height);
    Q_SCRIPTABLE void Preferences();
    Q_SCRIPTABLE void PreferencesAt(int x,int y,int width,int height);
    Q_SCRIPTABLE bool ImportDropAt(const QString &manifest,int x,int y,int width,int height);
    Q_SCRIPTABLE void Reload();
    Q_SCRIPTABLE QString State() const;
    Q_SCRIPTABLE void Quit();
protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEvent *) override;
    void leaveEvent(QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *,QEvent *) override;
private:
    friend class Edge;
    void buildInterface();
    void connectClient();
    void sendMessage(const QString &,const QStringList &,bool clearComposer);
    void reveal(bool activate);
    void animate(bool show);
    void place(qreal value);
    void positionEdge();
    void syncPanelEntry();
    void refreshGlass();
    void refreshColors();
    QRect preferencesGeometry(QSize size) const;
    void scheduleHide();
    void setStatus(const QString &text);
    void updateContext();
    void showCompactNotice();
    void addApproval(const QJsonValue &id,const QString &title,const QString &detail);
    void attachImage(const QImage &image);
    bool persist();
    void loadDraft();
    Settings effectiveSettings() const;
    void updateReplyMode();
    Settings settings;
    Conversation history;
    AgentClient *backend=nullptr;
    Edge *edge=nullptr;
    ChatView *view=nullptr;
    Composer *input=nullptr;
    QLabel *statusLabel=nullptr,*contextLabel=nullptr,*replyModeLabel=nullptr;
    QPushButton *pin=nullptr,*contextButton=nullptr,*quickReplyButton=nullptr,*newConversationButton=nullptr;
    QWidget *replyModeRow=nullptr;
    QVBoxLayout *notices=nullptr;
    QPointer<QWidget> compactNotice;
    QTimer hideTimer,wallpaperTimer;
    QVariantAnimation motion;
    QPointer<QDialog> preferences;
    QImage material;
    std::unique_ptr<NextKdeGlassView> launcherMaterial;
    QString materialKey;
    QRect panelAnchor;
    bool panelHosted=false,lightSurface=false;
    QFileSystemWatcher *watcher=nullptr;
    int answerIndex=-1,builds=0;
    qint64 compactDismissedAt=0;
    qreal shown=0;
    bool opened=false,pinned=false,engaged=false,capturing=false,quitting=false,unread=false;
    // Temporary switch is independent of the configured startup default.
    bool quickReply=false;
};
