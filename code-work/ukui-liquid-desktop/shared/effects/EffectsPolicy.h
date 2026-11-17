#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <mutex>
#include <vector>

// Global liquid-effects switchboard shared by every Kylin-LQUI process
// (taskbar, Fences, start menu, control center, calendar, material workers).
//
// Stored in ~/.config/kylin-lqui/appearance.ini, group [effects]:
//   level         full | balanced | lite | off   tier; a cap on everything below
//                 full      today's behaviour (default)
//                 balanced  GPU glass + capture, no per-frame decoration, 70% motion
//                 lite      CPU glass, no screen capture, no animation
//                 off       no glass material at all, no animation
//   motion        system | on | off   reduce animations; "system" keeps each
//                                     module's previous behaviour (UKUI
//                                     reduceAnimation OR the module's toggle)
//   gpu           auto | off          off forces the CPU glass path everywhere
//   pointerLight  true | false        master cap for pointer-following light
//
//   castingLite   true | false        drop to lite while a casting app runs
//
// Session health (phase 3), $XDG_RUNTIME_DIR/kylin-lqui/effects-health-<DISPLAY>-<X server>.json,
// cleared at logout/reboot: renderers and helpers report failures; one severe
// failure (GPU init) or two within 10 minutes "trip" the session to lite. The
// taskbar records whether a casting app runs. The effective tier is the
// stricter of the user's tier and these caps; the user's choice is kept.
//
// Defaults change nothing. The input method keeps its own settings on purpose.
//
// Reads are thread-safe and cheap (re-read at most every RecheckMs), so
// renderers on worker threads and short-lived helper processes can call them.
// GUI processes may register onChanged() to react immediately to edits made
// by the settings app or another process.
namespace LquiEffects {

enum class Motion { System, On, Off };
enum class Level { Full, Balanced, Lite, Off };

namespace detail {
constexpr int RecheckMs=500;

inline QString motionKey(Motion value){
    return value==Motion::On?QStringLiteral("on"):value==Motion::Off?QStringLiteral("off"):QStringLiteral("system");
}
inline QString levelKey(Level value){
    switch(value){
    case Level::Balanced:return QStringLiteral("balanced");
    case Level::Lite:return QStringLiteral("lite");
    case Level::Off:return QStringLiteral("off");
    default:return QStringLiteral("full");
    }
}
inline Level parseLevel(const QString &value){
    if(value==QLatin1String("balanced"))return Level::Balanced;
    if(value==QLatin1String("lite"))return Level::Lite;
    if(value==QLatin1String("off"))return Level::Off;
    return Level::Full; // unknown values keep today's behaviour
}
inline Motion parseMotion(const QString &value){
    if(value==QLatin1String("on"))return Motion::On;
    if(value==QLatin1String("off"))return Motion::Off;
    return Motion::System; // unknown values must not change behaviour
}

struct Snapshot {
    Level level=Level::Full;      // effective (after health/casting caps)
    Level userLevel=Level::Full;  // what the user chose
    bool tripped=false;
    QString tripReason;
    bool casting=false;
    QString castingApp;
    bool castingLite=true;
    Motion motion=Motion::System;
    bool systemReduce=false;
    bool gpu=true;
    bool pointerLight=true;
    bool operator==(const Snapshot &o) const {
        return level==o.level&&userLevel==o.userLevel&&tripped==o.tripped&&tripReason==o.tripReason
            &&casting==o.casting&&castingApp==o.castingApp&&castingLite==o.castingLite&&motion==o.motion&&systemReduce==o.systemReduce&&gpu==o.gpu&&pointerLight==o.pointerLight;
    }
};

inline QString globalPath(){
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+QStringLiteral("/kylin-lqui/appearance.ini");
}

constexpr qint64 FailureWindowSecs=600;
constexpr int FailuresToTrip=2;

inline QString healthPath(){
    QString runtime=QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if(runtime.isEmpty())runtime=QDir::tempPath()+QStringLiteral("/kylin-lqui-")+QString::number(qulonglong(QCoreApplication::applicationPid()));
    // One file per X display: test runs on Xvfb/offscreen never degrade the
    // user's real desktop session (:0), and vice versa.
    QString display=QString::fromLocal8Bit(qgetenv("DISPLAY"));
    if(display.isEmpty())display=QStringLiteral("nodisplay");
    // ...and per X server instance: a later server reusing the same number
    // (e.g. successive xvfb-run test servers on :99) starts with clean health.
    QString instance;
    const int colon=display.lastIndexOf(QLatin1Char(':'));
    if(colon>=0){
        const QString number=display.mid(colon+1).section(QLatin1Char('.'),0,0);
        const QFileInfo socket(QStringLiteral("/tmp/.X11-unix/X")+number);
        if(!number.isEmpty()&&socket.exists())
            instance=QStringLiteral("-")+QString::number(socket.metadataChangeTime().toSecsSinceEpoch(),16);
    }
    for(QChar &c:display)if(!c.isLetterOrNumber()&&c!=QLatin1Char('.')&&c!=QLatin1Char(':'))c=QLatin1Char('_');
    return runtime+QStringLiteral("/kylin-lqui/effects-health-")+display+instance+QStringLiteral(".json");
}
// Unreadable or corrupt health never disables anything.
inline QJsonObject readHealth(){
    QFile file(healthPath());
    if(!file.open(QIODevice::ReadOnly))return {};
    const auto document=QJsonDocument::fromJson(file.read(64*1024));
    return document.isObject()?document.object():QJsonObject{};
}
inline bool writeHealth(const QJsonObject &health){
    QDir().mkpath(QFileInfo(healthPath()).absolutePath());
    QSaveFile file(healthPath());
    if(!file.open(QIODevice::WriteOnly))return false;
    file.write(QJsonDocument(health).toJson(QJsonDocument::Compact));
    return file.commit();
}
// Read-modify-write across processes and threads.
template<typename Edit> bool editHealth(Edit edit){
    QDir().mkpath(QFileInfo(healthPath()).absolutePath());
    static std::mutex local; std::lock_guard<std::mutex> threads(local);
    QLockFile lock(healthPath()+QStringLiteral(".lock"));lock.setStaleLockTime(5000);
    if(!lock.tryLock(2000))return false;
    QJsonObject health=readHealth();
    const QJsonObject before=health;
    edit(health);
    return health==before||writeHealth(health);
}
inline Level stricter(Level a,Level b){return int(a)>int(b)?a:b;}

inline Snapshot load(){
    Snapshot result;
    QSettings global(globalPath(),QSettings::IniFormat);
    result.userLevel=parseLevel(global.value(QStringLiteral("effects/level"),QStringLiteral("full")).toString());
    result.castingLite=global.value(QStringLiteral("effects/castingLite"),true).toBool();
    const QJsonObject health=readHealth();
    result.tripped=health.value(QStringLiteral("tripped")).toBool();
    result.tripReason=health.value(QStringLiteral("reason")).toString();
    result.casting=health.value(QStringLiteral("casting")).toBool();
    result.castingApp=health.value(QStringLiteral("castingApp")).toString();
    result.level=result.userLevel;
    if(result.tripped||(result.casting&&result.castingLite))result.level=stricter(result.level,Level::Lite);
    result.motion=parseMotion(global.value(QStringLiteral("effects/motion"),QStringLiteral("system")).toString());
    result.gpu=global.value(QStringLiteral("effects/gpu"),QStringLiteral("auto")).toString()!=QLatin1String("off");
    result.pointerLight=global.value(QStringLiteral("effects/pointerLight"),true).toBool();
    result.systemReduce=QSettings(QStringLiteral("org.ukui"),QStringLiteral("style"))
        .value(QStringLiteral("reduceAnimation"),false).toBool();
    return result;
}

struct Cache {
    std::mutex lock;
    QElapsedTimer checked;
    Snapshot snapshot;
};
inline Cache &cache(){static Cache value;return value;}

inline Snapshot current(){
    auto &c=cache();
    std::lock_guard<std::mutex> guard(c.lock);
    if(!c.checked.isValid()||c.checked.elapsed()>=RecheckMs){c.snapshot=load();c.checked.start();}
    return c.snapshot;
}

inline bool write(const QString &key,const QVariant &value){
    QDir().mkpath(QFileInfo(globalPath()).absolutePath());
    QSettings settings(globalPath(),QSettings::IniFormat);
    settings.setValue(key,value);settings.sync();
    const bool ok=settings.status()==QSettings::NoError;
    std::lock_guard<std::mutex> guard(cache().lock);cache().checked.invalidate();
    return ok;
}

// GUI-thread watcher. Plain QObject + lambdas: no moc needed in consumers.
class Notifier final : public QObject {
public:
    explicit Notifier(QObject *parent):QObject(parent){
        debounce.setSingleShot(true);debounce.setInterval(60);
        connect(&watcher,&QFileSystemWatcher::fileChanged,this,[this]{debounce.start();});
        connect(&watcher,&QFileSystemWatcher::directoryChanged,this,[this]{debounce.start();});
        connect(&debounce,&QTimer::timeout,this,[this]{reload();});
        last=current();rewatch();
    }
    void add(QObject *context,std::function<void()> callback){listeners.push_back({context,std::move(callback)});}
private:
    struct Listener {QPointer<QObject> context;std::function<void()> callback;};
    QFileSystemWatcher watcher;
    QTimer debounce;
    Snapshot last;
    std::vector<Listener> listeners;
    void reload(){
        {std::lock_guard<std::mutex> guard(cache().lock);cache().checked.invalidate();}
        const Snapshot next=current();
        rewatch();
        if(next==last)return;
        last=next;
        auto copy=listeners;
        for(const auto &listener:copy)if(listener.context)listener.callback();
        listeners.erase(std::remove_if(listeners.begin(),listeners.end(),[](const Listener &l){return !l.context;}),listeners.end());
    }
    void rewatch(){
        if(!watcher.files().isEmpty())watcher.removePaths(watcher.files());
        if(!watcher.directories().isEmpty())watcher.removePaths(watcher.directories());
        // Watch the files and their directories: QSettings saves by atomic rename,
        // and either file may be created later.
        const QStringList files{globalPath(),QSettings(QStringLiteral("org.ukui"),QStringLiteral("style")).fileName(),healthPath()};
        for(const QString &file:files){
            if(QFileInfo::exists(file))watcher.addPath(file);
            QString directory=QFileInfo(file).absolutePath();
            while(!QDir(directory).exists()){
                const QString up=QFileInfo(directory).absolutePath();if(up==directory)break;directory=up;
            }
            if(QDir(directory).exists()&&!watcher.directories().contains(directory))watcher.addPath(directory);
        }
    }
};
inline Notifier &notifier(){
    static QPointer<Notifier> instance;
    if(!instance)instance=new Notifier(QCoreApplication::instance());
    return *instance;
}
}

inline QString path(){return detail::globalPath();}
// Forget the cached values (tests, or right after this process wrote them).
inline void invalidate(){std::lock_guard<std::mutex> guard(detail::cache().lock);detail::cache().checked.invalidate();}

inline QString healthPath(){return detail::healthPath();}
// Effective tier: the stricter of the user's tier and the session caps.
inline Level level(){return detail::current().level;}
inline Level userLevel(){return detail::current().userLevel;}
inline bool tripped(){return detail::current().tripped;}
inline QString tripReason(){return detail::current().tripReason;}
inline bool casting(){return detail::current().casting;}
inline QString castingApp(){return detail::current().castingApp;}
inline bool castingLite(){return detail::current().castingLite;}

// Record a rendering failure. Returns true when this call tripped the session
// to lite. Thread-safe; callable from renderer threads and helper processes.
inline bool reportFailure(const QString &source,const QString &kind,bool severe=false){
    bool trippedNow=false;
    detail::editHealth([&](QJsonObject &health){
        const qint64 now=QDateTime::currentSecsSinceEpoch();
        QJsonArray recent;int inWindow=0;
        for(const auto &value:health.value(QStringLiteral("failures")).toArray()){
            const auto entry=value.toObject();
            if(now-entry.value(QStringLiteral("t")).toVariant().toLongLong()<=detail::FailureWindowSecs){recent.append(entry);++inWindow;}
        }
        recent.append(QJsonObject{{"t",now},{"source",source},{"kind",kind}});++inWindow;
        while(recent.size()>20)recent.removeFirst();
        health.insert(QStringLiteral("failures"),recent);
        if(!health.value(QStringLiteral("tripped")).toBool()&&(severe||inWindow>=detail::FailuresToTrip)){
            health.insert(QStringLiteral("tripped"),true);
            health.insert(QStringLiteral("reason"),source+QStringLiteral(": ")+kind);
            health.insert(QStringLiteral("since"),now);
            trippedNow=true;
        }
    });
    invalidate();
    return trippedNow;
}
// "Restore" in the settings card: forget failures for this session.
inline bool clearHealth(){
    const bool ok=detail::editHealth([](QJsonObject &health){
        health.remove(QStringLiteral("tripped"));health.remove(QStringLiteral("reason"));
        health.remove(QStringLiteral("since"));health.remove(QStringLiteral("failures"));
    });
    invalidate();return ok;
}
// Written by the taskbar's casting watcher only.
inline bool setCasting(bool active,const QString &app){
    const bool ok=detail::editHealth([&](QJsonObject &health){
        if(active){health.insert(QStringLiteral("casting"),true);health.insert(QStringLiteral("castingApp"),app);}
        else{health.remove(QStringLiteral("casting"));health.remove(QStringLiteral("castingApp"));}
    });
    invalidate();return ok;
}
inline Motion motion(){return detail::current().motion;}

// moduleOwn: the module's existing "reduce motion" toggle, honoured while the
// global choice is "system". Lite/Off tiers always reduce.
inline bool reducedMotion(bool moduleOwn=false){
    const auto s=detail::current();
    if(s.level==Level::Lite||s.level==Level::Off)return true;
    if(s.motion==Motion::On)return true;
    if(s.motion==Motion::Off)return false;
    return s.systemReduce||moduleOwn;
}
// Multiply animation durations by this (0 means: jump to the end state).
inline double motionScale(bool moduleOwn=false){
    if(reducedMotion(moduleOwn))return 0.0;
    return level()==Level::Balanced?0.7:1.0;
}
// Glass material may be rendered at all (Off draws plain tinted surfaces).
inline bool glassMaterial(){return level()!=Level::Off;}
// The UKUI_LIQUID_GLASS_NO_GL test switch is handled by the renderer itself.
inline bool gpuAllowed(){const auto s=detail::current();return s.gpu&&(s.level==Level::Full||s.level==Level::Balanced);}
// Screen grabs for backdrops (menus, popups, calendar, control center).
inline bool liveCapture(){const auto l=level();return l==Level::Full||l==Level::Balanced;}
// Per-frame decorative timers: pointer light, hover magnification, click ripples.
inline bool decorativeFrames(){return level()==Level::Full;}
// Master cap: the module's own setting still has to allow it.
inline bool pointerLight(bool moduleOwn=true){return moduleOwn&&detail::current().pointerLight&&decorativeFrames();}

inline bool setLevel(Level value){return detail::write(QStringLiteral("effects/level"),detail::levelKey(value));}
inline bool setMotion(Motion value){return detail::write(QStringLiteral("effects/motion"),detail::motionKey(value));}
inline bool setGpuAllowed(bool value){return detail::write(QStringLiteral("effects/gpu"),value?QStringLiteral("auto"):QStringLiteral("off"));}
inline bool setPointerLight(bool value){return detail::write(QStringLiteral("effects/pointerLight"),value);}
inline bool setCastingLite(bool value){return detail::write(QStringLiteral("effects/castingLite"),value);}

// GUI thread only. Called after another process (or this one) changes the policy.
// The callback is dropped when `context` is destroyed.
inline void onChanged(QObject *context,std::function<void()> callback){detail::notifier().add(context,std::move(callback));}
}
