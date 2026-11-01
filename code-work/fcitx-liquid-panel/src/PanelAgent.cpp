#include "PanelAgent.h"
#include "SettingsBridge.h"
#include "PinyinTones.h"
#include "SettingsDialog.h"
#include "LiquidPopup.h"
#include <QSettings>
#include <QDBusInterface>
#include <QApplication>
#include <QDBusMessage>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPainter>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QHash>
#include <algorithm>

static void initializeTrayIcons() { Q_INIT_RESOURCE(tray_icons); }
static QIcon statusIcon(const QString &source) {
    // Decode each supplied PNG once, then keep bounded tray-size bitmaps.
    // Original artwork and transparency remain intact in the embedded resource.
    static QHash<QString,QIcon> icons;
    auto cached=icons.constFind(source);
    if(cached!=icons.cend()) return cached.value();
    const QPixmap original(source);
    QIcon icon;
    if(!original.isNull()) for(int size:{22,24,32,48,64,128})
        icon.addPixmap(original.scaled(size,size,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    icons.insert(source,icon);
    return icon;
}

namespace { const char *backend="org.fcitx.Fcitx"; const char *path="/org/kde/impanel"; }
PanelAgent::PanelAgent(CandidateWindow &window,QObject *parent)
    :QObject(parent),m_window(window),m_bus(QDBusConnection::sessionBus()),m_keyboardLocks(this),m_trayMenu(m_menu,this) {
    initializeTrayIcons();
    connect(&window,&CandidateWindow::selected,this,[this](int i){send("SelectCandidate",{i});});
    connect(&window,&CandidateWindow::pageUp,this,[this]{send("LookupTablePageUp");});
    connect(&window,&CandidateWindow::pageDown,this,[this]{send("LookupTablePageDown");});
    connect(&window,&CandidateWindow::rimeSettingsChanged,this,[this]{send("TriggerProperty",{QStringLiteral("/Fcitx/rime-deploy")});});
    connect(&m_keyboardLocks,&CapsLockMonitor::changed,this,[this]{refreshTray();});
    refreshTray();
    // Own popup timing; exporting a native context menu lets the tray host
    // open it on press and dismiss it on the matching release.
    m_menu.setObjectName("liquidTrayMenu");
    m_submenu.setObjectName("liquidInputMethodMenu");
    // Only supply readable foreground colors. Material, corners, rim,
    // spacing and motion belong to the shared LiquidPopup module.
    QPalette palette=m_menu.palette();
    palette.setColor(QPalette::WindowText,QColor(241,245,252));
    palette.setColor(QPalette::Text,QColor(241,245,252));
    palette.setColor(QPalette::ButtonText,QColor(241,245,252));
    palette.setColor(QPalette::HighlightedText,Qt::white);
    m_menu.setPalette(palette); m_submenu.setPalette(palette);
    const auto followPanelTheme=[] {
        // Use the same saved menu preferences as the desktop panel host.
        QSettings settings(QDir::homePath()+"/.config/ukui/liquid-panel.ini",QSettings::IniFormat);
        LiquidPopup::theme().highlight=qBound(0.,settings.value("appearance/highlight",.55).toDouble(),1.);
        LiquidPopup::theme().reducedMotion=settings.value("appearance/reducedMotion",false).toBool();
    };
    connect(&m_menu,&QMenu::aboutToShow,this,followPanelTheme);
    connect(&m_submenu,&QMenu::aboutToShow,this,followPanelTheme);
    m_menu.setSeparatorsCollapsible(false);
    m_submenu.setSeparatorsCollapsible(false);
    m_menuPopupTimer.setSingleShot(true);
    connect(&m_menuPopupTimer,&QTimer::timeout,this,[this]{
        if (TrayMenu::pointerButtonDown()) {if(m_submenuWait.elapsed()<5000) m_menuPopupTimer.start(40);return;}
        m_menuPopupQueued=false;
        if (m_submenu.isVisible()) return;
        m_submenu.clear();
        for (const auto &property:m_pendingMenuProperties) addProperty(&m_submenu,property);
        if (!m_submenu.isEmpty()) m_submenu.popup(QCursor::pos());
    });
    auto rebuildAfterMenuHide=[this]{
        if (!m_menuRebuildPending) return;
        QTimer::singleShot(0,this,[this]{ rebuildMenu(); });
    };
    connect(&m_menu,&QMenu::aboutToHide,this,rebuildAfterMenuHide);
    connect(&m_submenu,&QMenu::aboutToHide,this,rebuildAfterMenuHide);
    connect(&m_tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
        if (reason==QSystemTrayIcon::Context) {m_window.hide();m_trayMenu.request(QCursor::pos());}
        if (reason==QSystemTrayIcon::Trigger) send("TriggerProperty",{QString("/Fcitx/im")});
    });
    rebuildMenu();
}

bool PanelAgent::start() {
    if (!m_bus.isConnected()) return false;
    const QString iface="org.kde.kimpanel.inputmethod";
#define CONNECT_SIGNAL(name,signature) if (!m_bus.connect(backend,"/kimpanel",iface,#name,this,SLOT(signature))) return false;
    CONNECT_SIGNAL(Enable,Enable(bool))
    CONNECT_SIGNAL(ShowPreedit,ShowPreedit(bool))
    CONNECT_SIGNAL(ShowAux,ShowAux(bool))
    CONNECT_SIGNAL(ShowLookupTable,ShowLookupTable(bool))
    CONNECT_SIGNAL(UpdatePreeditText,UpdatePreeditText(QString,QString))
    CONNECT_SIGNAL(UpdatePreeditCaret,UpdatePreeditCaret(int))
    CONNECT_SIGNAL(UpdateAux,UpdateAux(QString,QString))
    CONNECT_SIGNAL(UpdateLookupTable,UpdateLookupTable(QStringList,QStringList,QStringList,bool,bool))
    CONNECT_SIGNAL(UpdateLookupTableCursor,UpdateLookupTableCursor(int))
    CONNECT_SIGNAL(UpdateSpotLocation,UpdateSpotLocation(int,int))
    CONNECT_SIGNAL(RegisterProperties,RegisterProperties(QStringList))
    CONNECT_SIGNAL(UpdateProperty,UpdateProperty(QString))
    CONNECT_SIGNAL(ExecMenu,ExecMenu(QStringList))
#undef CONNECT_SIGNAL
    // Register methods before claiming the name, so introspection always works.
    if (!m_bus.registerObject(path,this,QDBusConnection::ExportScriptableSlots)) return false;
    m_backendOwner=m_bus.interface()->serviceOwner(backend).value();
    if (!m_bus.registerService("org.kde.impanel")) return false;
    auto *watcher=new QDBusServiceWatcher(backend,m_bus,QDBusServiceWatcher::WatchForOwnerChange,this);
    connect(watcher,&QDBusServiceWatcher::serviceOwnerChanged,this,[this](const QString &,const QString &,const QString &owner){
        m_backendOwner=owner;
        ++m_stateRevision; m_enabled=false;
        m_window.clear(); m_properties.clear(); refreshTray(); rebuildMenu();
        if (!owner.isEmpty()) QTimer::singleShot(200,this,[this]{announce();requestInitialState();});
    });
    if(!m_keyboardLocks.start()) qWarning()<<"[liquid-candidate] Caps Lock indicator is unavailable";
    refreshTray();
    m_tray.show();
    QTimer::singleShot(0,this,[this]{announce();requestInitialState();});
    return true;
}
bool PanelAgent::validCaller() const {
    // Exported methods only accept our Fcitx instance. Local calls are used by
    // legacy signal adapters and tests, and need no blocking bus round trip.
    if (!calledFromDBus()) return true;
    return message().service()==m_backendOwner;
}
void PanelAgent::send(const QString &member,const QVariantList &args,const QString &iface) {
    QDBusMessage signal=QDBusMessage::createSignal(path,iface,member);
    signal.setArguments(args); m_bus.send(signal);
}
void PanelAgent::announce() {
    send("PanelCreated"); send("PanelCreated2",{},"org.kde.impanel2");
}
void PanelAgent::requestInitialState() {
    if(m_backendOwner.isEmpty()) return;
    const QString owner=m_backendOwner;
    const quint64 revision=m_stateRevision;
    const auto request=QDBusMessage::createMethodCall(owner,"/inputmethod","org.fcitx.Fcitx.InputMethod","GetCurrentState");
    auto *watcher=new QDBusPendingCallWatcher(m_bus.asyncCall(request),this);
    connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,owner,revision](QDBusPendingCallWatcher *call){
        QDBusPendingReply<int> reply=*call;
        call->deleteLater();
        // Focus or Ctrl+Space can change state while the startup reply is in flight.
        if(!reply.isError() && owner==m_backendOwner && revision==m_stateRevision) Enable(reply.value()==2);
    });
}
void PanelAgent::SetSpotRect(int x,int y,int w,int h) {
    if (!validCaller()) return;
    m_window.state.spot=QRect(x,y,qMax(w,0),qMax(h,0));
    m_window.state.hasSpot=true; m_window.schedule();
}
void PanelAgent::SetLookupTable(const QStringList &labels,const QStringList &texts,const QStringList &,bool previous,bool next,int cursor,int layout) {
    if (!validCaller()) return;
    auto &s=m_window.state; s.labels=labels.mid(0,33); s.texts=texts.mid(0,33);
    // Fcitx4 Kimpanel concatenates word + comment into each text, rather
    // than transporting the Rime comment in its formatting attributes.
    s.pinyins.clear();
    for(QString &text:s.texts) {
        const QString trimmed=text.trimmed();
        const int bracket=trimmed.lastIndexOf(QChar(0xff3b));
        const QString reading=bracket>=0 && trimmed.endsWith(QChar(0xff3d))
            ?trimmed.mid(bracket+1,trimmed.size()-bracket-2).trimmed():QString();
        // Avoid the old ARM64 Qt/PCRE JIT's Unicode-class crash. Validate
        // pronunciation characters directly; ordinary Chinese comments stay intact.
        const bool pronunciation=!reading.isEmpty() && std::all_of(reading.cbegin(),reading.cend(),[](QChar c) {
            return c.script()==QChar::Script_Latin || c.isSpace() || c.category()==QChar::Mark_NonSpacing
                || QStringLiteral("12345'’:-").contains(c);
        });
        if(pronunciation) {
            text=trimmed.left(bracket).trimmed();
            s.pinyins<<PinyinTones::annotate(text,reading);
        } else s.pinyins<<QString();
    }
    s.previous=previous; s.next=next; s.cursor=cursor; s.layout=layout;
    m_window.schedule();
}
void PanelAgent::Enable(bool enabled) {
    ++m_stateRevision; m_enabled=enabled; refreshTray();
    if (!enabled) {
        // Fcitx suppresses duplicate cursor rectangles across off/on toggles.
        const QRect spot=m_window.state.spot; const bool known=m_window.state.hasSpot;
        m_window.clear(); m_window.state.spot=spot; m_window.state.hasSpot=known;
    }
}
void PanelAgent::ShowPreedit(bool visible) { m_window.state.preeditVisible=visible; m_window.schedule(); }
void PanelAgent::ShowAux(bool visible) { m_window.state.auxiliaryVisible=visible; m_window.schedule(); }
void PanelAgent::ShowLookupTable(bool visible) { m_window.state.lookupVisible=visible; m_window.schedule(); }
void PanelAgent::UpdatePreeditText(const QString &text,const QString &) { m_window.state.preedit=text; m_window.schedule(); }
void PanelAgent::UpdatePreeditCaret(int caret) { m_window.state.caret=caret; m_window.schedule(); }
void PanelAgent::UpdateAux(const QString &text,const QString &) { m_window.state.auxiliary=text; m_window.schedule(); }
void PanelAgent::UpdateLookupTable(const QStringList &labels,const QStringList &texts,const QStringList &attrs,bool previous,bool next) {
    SetLookupTable(labels,texts,attrs,previous,next,0,0);
}
void PanelAgent::UpdateLookupTableCursor(int cursor) { m_window.state.cursor=cursor; m_window.schedule(); }
void PanelAgent::UpdateSpotLocation(int x,int y) { SetSpotRect(x,y,0,0); }

void PanelAgent::addProperty(QMenu *menu,const QString &property) {
    const auto fields=property.split(':');
    if (fields.size()<4 || fields[0].isEmpty()) return;
    QString label=fields[1]; if (label.isEmpty()) label=fields[3];
    if (label.isEmpty()) return;
    if(fields[0]=="/Fcitx/im" && fields[2].contains("rime",Qt::CaseInsensitive))label=QStringLiteral("RIME液态输入法");
    const QIcon icon=fields[2].contains("rime",Qt::CaseInsensitive)?QIcon(":/fcitx-liquid/tray/settings.png"):QIcon::fromTheme(fields[2]);
    QAction *action=menu->addAction(icon,label);
    action->setToolTip(fields[3]);
    connect(action,&QAction::triggered,this,[this,key=fields[0]]{send("TriggerProperty",{key});});
}
void PanelAgent::rebuildMenu() {
    if (m_menu.isVisible() || m_submenu.isVisible() || m_trayMenu.pending()) {
        m_menuRebuildPending=true;
        return;
    }
    m_menuRebuildPending=false;
    m_menu.clear();
    connect(m_menu.addAction(QIcon(":/fcitx-liquid/tray/settings.png"),QStringLiteral("RIME液态输入法设置…")),&QAction::triggered,this,[]{
        QDBusInterface control("org.kde.impanel","/org/ukui/FcitxLiquid","org.ukui.FcitxLiquid");
        control.asyncCall("ShowSettings");
    });
    m_menu.addSeparator();
    for (const auto &property:m_properties) addProperty(&m_menu,property);
    if (!m_properties.isEmpty()) m_menu.addSeparator();
    connect(m_menu.addAction(QStringLiteral("Fcitx 输入法管理")),&QAction::triggered,this,[this]{send("Configure");});
    connect(m_menu.addAction(QStringLiteral("退出液态输入法（恢复系统候选窗）")),&QAction::triggered,this,[]{
        // Only remove the desktop entry authored by this project.
        const QString file=QDir::homePath()+"/.config/autostart/fcitx-liquid-panel.desktop";
        QFile desktop(file);
        if (desktop.open(QIODevice::ReadOnly) && desktop.readAll().contains("X-UKUI-FcitxLiquid=true")) { desktop.close(); QFile::remove(file); }
        QCoreApplication::quit();
    });
}
void PanelAgent::RegisterProperties(const QStringList &properties) { m_properties=properties; refreshTray(); rebuildMenu(); }
void PanelAgent::UpdateProperty(const QString &property) {
    const QString key=property.section(':',0,0);
    bool found=false;
    for (QString &old:m_properties) if (old.section(':',0,0)==key) { old=property; found=true; break; }
    if (!found) m_properties.append(property);
    refreshTray();
    rebuildMenu();
}
void PanelAgent::refreshTray() {
    QStringList im,rime;
    for(const auto &property:m_properties) {
        const auto fields=property.split(':');
        if(fields.size()<4) continue;
        if(fields[0]=="/Fcitx/im") im=fields;
        if(fields[0]=="/Fcitx/rime-enzh") rime=fields;
    }
    QString name=im.isEmpty()?QStringLiteral("RIME液态输入法"):im[1];
    const QString iconName=im.isEmpty()?QString():im[2];
    const bool isRime=iconName.contains("rime",Qt::CaseInsensitive);
    if(isRime)name=QStringLiteral("RIME液态输入法");
    const bool direct=!m_enabled || iconName=="fcitx-kbd" || iconName=="kbd";
    const bool keyboard=iconName.isEmpty() && !im.isEmpty();
    const bool latin=isRime && (!rime.isEmpty()?rime[1]=="A" || rime[2].contains("rime-latin"):iconName.contains("rime-latin"));
    const bool preparing=isRime && !rime.isEmpty() && (rime[1]==QStringLiteral("⌛") || rime[2].contains("rime-disable"));
    const bool caps=m_keyboardLocks.capsLock();
    QString label,mode,themeIcon,asset;
    QColor color(48,117,174);
    if(im.isEmpty()) {
        label="?"; mode=QStringLiteral("等待输入法状态"); color=QColor(91,105,120);
    } else if(caps) {
        label="A"; mode=QStringLiteral("英文大写 · Caps Lock 已开启"); color=QColor(169,105,35);
        asset="uppercase";
    } else if(preparing && !direct) {
        label="?"; mode=QStringLiteral("输入法正在准备"); color=QColor(91,105,120);
    } else if(direct || keyboard || latin) {
        label="a"; mode=QStringLiteral("英文小写"); color=direct || keyboard?QColor(91,105,120):QColor(38,126,134);
        asset=direct || keyboard?"direct":"latin";
    } else if(isRime) {
        label=QStringLiteral("中"); mode=QStringLiteral("中文输入");
        asset="chinese";
    } else {
        label=name.left(1); if(label.isEmpty()) label=QStringLiteral("文");
        mode=QStringLiteral("输入法已启用"); themeIcon=iconName;
    }
    if(!im.isEmpty() && direct) mode+=QStringLiteral("（输入法未启用）");
    if(keyboard && !direct) mode+=QStringLiteral(" · 键盘直输");
    if(!im.isEmpty() && isRime && caps && m_enabled)
        mode+=latin?QStringLiteral(" · Rime 英文模式"):QStringLiteral(" · Rime 中文模式");
    m_trayLabel=label;
    m_tray.setToolTip(name+QStringLiteral(" · ")+mode+QStringLiteral("\nRIME液态输入法"));
    const QString resource=asset.isEmpty()?QString():QString(":/fcitx-liquid/tray/%1.png").arg(asset);
    const QString visual=label+":"+color.name()+":"+themeIcon+":"+resource;
    if(visual==m_trayVisual) return;
    m_trayVisual=visual;
    QIcon icon;
    m_trayIconSource.clear();
    if(!resource.isEmpty()) {
        icon=statusIcon(resource);
        if(!icon.isNull()) m_trayIconSource=resource;
    }
    if(!themeIcon.isEmpty()) icon=themeIcon.startsWith('/')?QIcon(themeIcon):QIcon::fromTheme(themeIcon);
    if(icon.isNull()) {
        QPixmap pixmap(48,48); pixmap.fill(Qt::transparent);
        QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen);
        p.setBrush(color); p.drawRoundedRect(QRectF(2,2,44,44),13,13);
        p.setPen(Qt::white); QFont font("Noto Sans CJK SC");font.setPixelSize(34);font.setBold(true);p.setFont(font);
        p.drawText(pixmap.rect(),Qt::AlignCenter,label);
        icon=QIcon(pixmap);
    }
    m_tray.setIcon(icon);
}
QVariantMap PanelAgent::trayStatus() const {
    return {{"trayLabel",m_trayLabel},{"trayTooltip",m_tray.toolTip()},{"trayIcon",m_trayIconSource},
            {"inputEnabled",m_enabled},{"capsLock",m_keyboardLocks.capsLock()}};
}
void PanelAgent::ExecMenu(const QStringList &properties) {
    m_pendingMenuProperties=properties;
    if (m_submenu.isVisible() || m_menuPopupTimer.isActive()) return;
    m_menuPopupQueued=true;
    m_submenuWait.start();
    m_menuPopupTimer.start(120);
}
void PanelControl::ShowSettings() {
    if(LquiSettings::open("input:general"))return;
    if(!m_settings) {m_settings=new SettingsDialog(m_window);m_settings->setAttribute(Qt::WA_DeleteOnClose);}
    if(!LquiSettings::buildingForm()){m_settings->show();m_settings->raise();m_settings->activateWindow();}
}
void PanelControl::ReloadSettings() {m_window.reloadConfiguration();}
void PanelControl::ReloadRime() {emit m_window.rimeSettingsChanged();}
void PanelControl::Quit() { QTimer::singleShot(0,qApp,&QCoreApplication::quit); }
QVariantMap PanelControl::Status() {
    const QRect previous=m_window.previousRect();
    const QRect next=m_window.nextRect();
    const QVector<QRect> items=m_window.candidateRects();
    const QRect first=items.isEmpty()?QRect():items.first();
    QVariantMap status={{"version","0.2.0"},{"product",QStringLiteral("RIME液态输入法")},{"pid",QCoreApplication::applicationPid()},
            {"pageSize",m_window.config().pageSize},{"layoutDirection",m_window.config().layoutDirection},
            {"candidateCount",items.size()},
            {"showPreedit",m_window.config().showPreedit},{"showLabels",m_window.config().showLabels},
            {"showCandidatePinyin",m_window.config().showCandidatePinyin},
            {"candidatePinyinCount",int(std::count_if(m_window.state.pinyins.cbegin(),m_window.state.pinyins.cend(),[](const QString &s){return !s.isEmpty();}))},
            {"visible",m_window.isVisible()},{"captures",m_window.captureCount()},
            {"renders",m_window.renderCount()},{"gpu",m_window.usedGpu()},
            {"geometry",QString("%1,%2 %3x%4").arg(m_window.x()).arg(m_window.y()).arg(m_window.width()).arg(m_window.height())},
            {"first",QString("%1,%2 %3x%4").arg(first.x()).arg(first.y()).arg(first.width()).arg(first.height())},
            {"previous",QString("%1,%2 %3x%4").arg(previous.x()).arg(previous.y()).arg(previous.width()).arg(previous.height())},
            {"next",QString("%1,%2 %3x%4").arg(next.x()).arg(next.y()).arg(next.width()).arg(next.height())}};
    const auto tray=m_agent.trayStatus();
    for(auto i=tray.cbegin();i!=tray.cend();++i) status.insert(i.key(),i.value());
    return status;
}

QWidget *PanelControl::settingsBridgeRoot(){ShowSettings();return m_settings.data();}
