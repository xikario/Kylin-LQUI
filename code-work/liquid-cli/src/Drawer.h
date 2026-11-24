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
class QBoxLayout;
class QPushButton;
class QVBoxLayout;
class QDialog;
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
    bool hasMaterial() const{return !material.isNull();}
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
    Q_SCRIPTABLE void PetChatAt(const QString &character,int x,int y,int width,int height);
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Hide();
    Q_SCRIPTABLE void Toggle();
    Q_SCRIPTABLE void ToggleAt(int x,int y,int width,int height);
    Q_SCRIPTABLE void CaptureAt(int x,int y,int width,int height);
    Q_SCRIPTABLE void Preferences();
    Q_SCRIPTABLE void PreferencesAt(int x,int y,int width,int height);
    Q_SCRIPTABLE bool ImportDropAt(const QString &manifest,int x,int y,int width,int height);
    // A quick action chosen at the orb after a drop: send it with a matching prompt.
    Q_SCRIPTABLE bool ImportDropActionAt(const QString &manifest,const QString &action,int x,int y,int width,int height);
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
    QString chatCharacter,turnCharacter;
    void setChatCharacter(const QString &id);
    void reactPet(const QString &phase,const QString &text={});
    friend class Edge;
    void buildInterface();
    void connectClient();
    void sendMessage(const QString &,const QStringList &,bool clearComposer);
    void reveal(bool activate);
    void animate(bool show);
    void place(qreal value);
    void positionEdge();
    void syncPanelEntry();
    void captureBackdrop();
    void rebuildGlass();
    void startGlass(const QImage &capture,const QByteArray &signature,const QSize &physical,qreal dpr);
    void adoptGlass(const QByteArray &signature);
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
    QBoxLayout *identityLayout=nullptr;
    QLabel *petPortrait=nullptr;
    QPushButton *petTools=nullptr;
    QLabel *statusLabel=nullptr,*contextLabel=nullptr,*replyModeLabel=nullptr;
    QPushButton *pin=nullptr,*contextButton=nullptr,*quickReplyButton=nullptr,*newConversationButton=nullptr;
    QWidget *replyModeRow=nullptr;
    QVBoxLayout *notices=nullptr;
    QPointer<QWidget> compactNotice;
    QTimer hideTimer;
    // Keeps the hidden drawer's glass matched to the orb, so opening never
    // has to build it synchronously (290-420 ms on the FTG340).
    QTimer warmTimer;int warmAttempts=0;bool warmedOnce=false,gpuWarmed=false;
    QVariantAnimation motion;
    QPointer<QDialog> preferences;
    QImage material,previousMaterial,lastCapture;
    QVariantAnimation fadeAnimation;qreal fade=1;
    QByteArray lastSignature;QSize materialSize;quint64 openSerial=0;
    std::unique_ptr<NextKdeGlassView> launcherMaterial;
    QRect panelAnchor;
    bool panelHosted=false,lightSurface=false;
    int answerIndex=-1,builds=0;
    qint64 compactDismissedAt=0;
    qreal shown=0;
    bool opened=false,pinned=false,engaged=false,capturing=false,quitting=false,unread=false;
    bool restoreCaptureDrawer=false;
    // Temporary switch is independent of the configured startup default.
    bool quickReply=false;
};
