#pragma once
#include "CandidateWindow.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QSystemTrayIcon>
#include <QMenu>
#include "TrayMenu.h"
#include "CapsLockMonitor.h"
#include <QPointer>
class SettingsDialog;

class PanelAgent : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.impanel2")
public:
    explicit PanelAgent(CandidateWindow &window, QObject *parent=nullptr);
    bool start();
    void send(const QString &member, const QVariantList &args={}, const QString &interface="org.kde.impanel");
    void announce();
    QVariantMap trayStatus() const;
public slots:
    Q_SCRIPTABLE void SetSpotRect(int x,int y,int w,int h);
    Q_SCRIPTABLE void SetLookupTable(const QStringList &labels,const QStringList &texts,
                                    const QStringList &attributes,bool previous,bool next,int cursor,int layout);
    void Enable(bool enabled);
    void ShowPreedit(bool visible);
    void ShowAux(bool visible);
    void ShowLookupTable(bool visible);
    void UpdatePreeditText(const QString &text,const QString &attributes);
    void UpdatePreeditCaret(int caret);
    void UpdateAux(const QString &text,const QString &attributes);
    void UpdateLookupTable(const QStringList &labels,const QStringList &texts,const QStringList &attributes,bool previous,bool next);
    void UpdateLookupTableCursor(int cursor);
    void UpdateSpotLocation(int x,int y);
    void RegisterProperties(const QStringList &properties);
    void UpdateProperty(const QString &property);
    void ExecMenu(const QStringList &properties);
private:
    bool validCaller() const;
    void rebuildMenu();
    void refreshTray();
    void requestInitialState();
    void addProperty(QMenu *menu,const QString &property);
    CandidateWindow &m_window;
    QDBusConnection m_bus;
    QSystemTrayIcon m_tray;
    CapsLockMonitor m_keyboardLocks;
    QString m_trayLabel, m_trayVisual, m_trayIconSource;
    bool m_enabled=false;
    quint64 m_stateRevision=0;
    QMenu m_menu, m_submenu;
    QStringList m_properties;
    QString m_backendOwner;
    QTimer m_menuPopupTimer;
    QElapsedTimer m_submenuWait;
    QStringList m_pendingMenuProperties;
    bool m_menuPopupQueued=false;
    bool m_menuRebuildPending=false;
    TrayMenu m_trayMenu;
};

class PanelControl : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ukui.FcitxLiquid")
public:
    explicit PanelControl(CandidateWindow &window, const PanelAgent &agent):m_window(window),m_agent(agent){}
public:
    QWidget *settingsBridgeRoot();
public slots:
    Q_SCRIPTABLE void Quit();
    Q_SCRIPTABLE void ShowSettings();
    Q_SCRIPTABLE void ReloadSettings();
    Q_SCRIPTABLE void ReloadRime();
    Q_SCRIPTABLE QVariantMap Status();
private:
    CandidateWindow &m_window;
    const PanelAgent &m_agent;
    QPointer<SettingsDialog> m_settings;
};
