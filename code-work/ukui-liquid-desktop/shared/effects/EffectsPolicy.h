#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFileSystemWatcher>
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
    Level level=Level::Full;
    Motion motion=Motion::System;
    bool systemReduce=false;
    bool gpu=true;
    bool pointerLight=true;
    bool operator==(const Snapshot &o) const {
        return level==o.level&&motion==o.motion&&systemReduce==o.systemReduce&&gpu==o.gpu&&pointerLight==o.pointerLight;
    }
};

inline QString globalPath(){
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+QStringLiteral("/kylin-lqui/appearance.ini");
}

inline Snapshot load(){
    Snapshot result;
    QSettings global(globalPath(),QSettings::IniFormat);
    result.level=parseLevel(global.value(QStringLiteral("effects/level"),QStringLiteral("full")).toString());
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
        const QStringList files{globalPath(),QSettings(QStringLiteral("org.ukui"),QStringLiteral("style")).fileName()};
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

inline Level level(){return detail::current().level;}
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

// GUI thread only. Called after another process (or this one) changes the policy.
// The callback is dropped when `context` is destroyed.
inline void onChanged(QObject *context,std::function<void()> callback){detail::notifier().add(context,std::move(callback));}
}
