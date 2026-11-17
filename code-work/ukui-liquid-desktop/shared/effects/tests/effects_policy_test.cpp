// Global liquid-effects policy: one switchboard for every Kylin-LQUI process.
#include "../EffectsPolicy.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QFile>
#include <atomic>
#include <cstdio>

static int failures=0;
#define CHECK(cond) do{if(!(cond)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#cond);++failures;}}while(0)

static void writeGlobal(const QString &key,const QVariant &value){
    QSettings s(LquiEffects::path(),QSettings::IniFormat);s.setValue(key,value);s.sync();
}
static void writeSystemReduce(bool on){
    QSettings s("org.ukui","style");s.setValue("reduceAnimation",on);s.sync();
}

int main(int argc,char **argv){
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME",home.path().toUtf8());
    QTemporaryDir runtime; // health state lives in the per-login runtime dir
    qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());
    QCoreApplication app(argc,argv);

    // Defaults change nothing: motion follows system or the module's own toggle,
    // GPU stays available, and pointer light is only a master cap.
    CHECK(LquiEffects::motion()==LquiEffects::Motion::System);
    CHECK(!LquiEffects::reducedMotion());
    CHECK(LquiEffects::reducedMotion(true));          // module's own toggle still counts
    CHECK(LquiEffects::gpuAllowed());
    CHECK(LquiEffects::pointerLight());
    CHECK(!LquiEffects::pointerLight(false));          // module off stays off

    writeSystemReduce(true);LquiEffects::invalidate();
    CHECK(LquiEffects::reducedMotion());               // follows UKUI system setting

    // An explicit global choice overrides both the system and the module.
    writeGlobal("effects/motion","off");LquiEffects::invalidate();
    CHECK(!LquiEffects::reducedMotion(true));
    writeGlobal("effects/motion","on");writeSystemReduce(false);LquiEffects::invalidate();
    CHECK(LquiEffects::reducedMotion(false));

    writeGlobal("effects/gpu","off");writeGlobal("effects/pointerLight",false);LquiEffects::invalidate();
    CHECK(!LquiEffects::gpuAllowed());
    CHECK(!LquiEffects::pointerLight(true));

    // Unknown values fall back to safe defaults instead of disabling things.
    writeGlobal("effects/motion","sideways");writeGlobal("effects/gpu","maybe");LquiEffects::invalidate();
    CHECK(LquiEffects::motion()==LquiEffects::Motion::System);
    CHECK(LquiEffects::gpuAllowed());

    // Setters used by the settings UI round-trip through the same file.
    CHECK(LquiEffects::setMotion(LquiEffects::Motion::On));
    CHECK(LquiEffects::setGpuAllowed(false));
    CHECK(LquiEffects::setPointerLight(true));
    CHECK(LquiEffects::motion()==LquiEffects::Motion::On && !LquiEffects::gpuAllowed() && LquiEffects::pointerLight());
    // Icon settings that share appearance.ini are left untouched.
    {QSettings s(LquiEffects::path(),QSettings::IniFormat);s.setValue("icons/uniform",false);s.sync();}
    LquiEffects::setGpuAllowed(true);
    {QSettings s(LquiEffects::path(),QSettings::IniFormat);CHECK(s.value("icons/uniform",true).toBool()==false);}

    // Without invalidate(), another process' edit is seen within the cache window.
    writeGlobal("effects/gpu","off");
    QThread::msleep(LquiEffects::detail::RecheckMs+50);
    CHECK(!LquiEffects::gpuAllowed());
    writeGlobal("effects/gpu","auto");LquiEffects::invalidate();

    // Reads are safe from worker threads (renderers run off the GUI thread).
    std::atomic<int> allowed{0};
    QThread *worker=QThread::create([&]{for(int i=0;i<2000;++i)if(LquiEffects::gpuAllowed())++allowed;});
    worker->start();for(int i=0;i<2000;++i)LquiEffects::gpuAllowed();worker->wait();delete worker;
    CHECK(allowed==2000);

    // Effect tiers. Default "full" is today's behaviour; each lower tier is a cap.
    writeGlobal("effects/motion","system");writeGlobal("effects/gpu","auto");writeGlobal("effects/pointerLight",true);
    writeSystemReduce(false);LquiEffects::invalidate();
    CHECK(LquiEffects::level()==LquiEffects::Level::Full);
    CHECK(LquiEffects::gpuAllowed()&&LquiEffects::liveCapture()&&LquiEffects::decorativeFrames()&&LquiEffects::glassMaterial());
    CHECK(qFuzzyCompare(LquiEffects::motionScale(),1.0));
    CHECK(LquiEffects::setLevel(LquiEffects::Level::Balanced));
    CHECK(LquiEffects::gpuAllowed()&&LquiEffects::liveCapture()&&LquiEffects::glassMaterial());
    CHECK(!LquiEffects::decorativeFrames()&&!LquiEffects::pointerLight(true));
    CHECK(qFuzzyCompare(LquiEffects::motionScale(),0.7)&&!LquiEffects::reducedMotion());
    CHECK(LquiEffects::setLevel(LquiEffects::Level::Lite));
    CHECK(!LquiEffects::gpuAllowed()&&!LquiEffects::liveCapture()&&LquiEffects::glassMaterial());
    writeGlobal("effects/motion","off");LquiEffects::invalidate();
    CHECK(LquiEffects::reducedMotion()&&LquiEffects::motionScale()==0.0); // tier caps the explicit choice
    CHECK(LquiEffects::setLevel(LquiEffects::Level::Off));
    CHECK(!LquiEffects::glassMaterial()&&!LquiEffects::gpuAllowed()&&!LquiEffects::liveCapture());
    writeGlobal("effects/level","turbo");LquiEffects::invalidate();
    CHECK(LquiEffects::level()==LquiEffects::Level::Full);              // unknown -> today's behaviour
    // Full tier still honours the fine-tune switches.
    writeGlobal("effects/level","full");writeGlobal("effects/gpu","off");writeGlobal("effects/motion","on");LquiEffects::invalidate();
    CHECK(!LquiEffects::gpuAllowed()&&LquiEffects::reducedMotion()&&LquiEffects::motionScale()==0.0);
    writeGlobal("effects/gpu","auto");writeGlobal("effects/motion","system");writeGlobal("effects/pointerLight",true);LquiEffects::invalidate();

    // Phase 3: automatic degradation. Effective tier = the stricter of the
    // user's tier and the session health cap; the user's choice is kept.
    LquiEffects::setLevel(LquiEffects::Level::Full);LquiEffects::clearHealth();
    CHECK(!LquiEffects::tripped()&&LquiEffects::level()==LquiEffects::Level::Full);
    CHECK(!LquiEffects::reportFailure("material-worker","timeout"));        // one soft failure: no trip
    CHECK(LquiEffects::level()==LquiEffects::Level::Full);
    CHECK(LquiEffects::reportFailure("material-worker","timeout"));         // second within 10 min trips
    CHECK(LquiEffects::tripped()&&LquiEffects::level()==LquiEffects::Level::Lite);
    CHECK(LquiEffects::userLevel()==LquiEffects::Level::Full);              // choice untouched
    CHECK(LquiEffects::tripReason().contains("material-worker"));
    CHECK(!LquiEffects::gpuAllowed()&&!LquiEffects::liveCapture());
    LquiEffects::setLevel(LquiEffects::Level::Off);
    CHECK(LquiEffects::level()==LquiEffects::Level::Off);                   // stricter user choice wins
    LquiEffects::setLevel(LquiEffects::Level::Full);
    CHECK(LquiEffects::clearHealth()&&!LquiEffects::tripped()&&LquiEffects::level()==LquiEffects::Level::Full);
    CHECK(LquiEffects::reportFailure("renderer","gpu-init-failed",true));   // severe: trips at once
    CHECK(LquiEffects::level()==LquiEffects::Level::Lite);
    LquiEffects::clearHealth();
    // Casting: temporary cap while a casting app runs (switchable, default on).
    CHECK(LquiEffects::castingLite());
    CHECK(LquiEffects::setCasting(true,"ExceedShare"));
    CHECK(LquiEffects::casting()&&LquiEffects::level()==LquiEffects::Level::Lite);
    CHECK(LquiEffects::setCastingLite(false)&&LquiEffects::level()==LquiEffects::Level::Full);
    CHECK(LquiEffects::setCastingLite(true)&&LquiEffects::level()==LquiEffects::Level::Lite);
    CHECK(LquiEffects::setCasting(false,QString())&&LquiEffects::level()==LquiEffects::Level::Full);
    // A corrupt health file must never disable effects or crash.
    {QFile f(LquiEffects::healthPath());f.open(QIODevice::WriteOnly);f.write("{not json");}
    LquiEffects::invalidate();
    CHECK(!LquiEffects::tripped()&&LquiEffects::level()==LquiEffects::Level::Full);
    // Concurrent reporters (renderer threads) never lose the trip.
    LquiEffects::clearHealth();
    QThread *a=QThread::create([]{for(int i=0;i<5;++i)LquiEffects::reportFailure("thread-a","timeout");});
    QThread *b=QThread::create([]{for(int i=0;i<5;++i)LquiEffects::reportFailure("thread-b","timeout");});
    a->start();b->start();a->wait();b->wait();delete a;delete b;
    CHECK(LquiEffects::tripped());
    LquiEffects::clearHealth();

    // GUI processes get a callback when another process changes the policy.
    int notified=0;
    LquiEffects::onChanged(&app,[&]{++notified;});
    QTimer::singleShot(50,&app,[]{QSettings s(LquiEffects::path(),QSettings::IniFormat);s.setValue("effects/pointerLight",false);s.sync();});
    QTimer::singleShot(700,&app,[]{LquiEffects::setCasting(true,"ExceedShare");});
    QTimer::singleShot(1500,&app,&QCoreApplication::quit);
    app.exec();
    CHECK(notified>=2);                                                    // settings edit + casting change
    CHECK(!LquiEffects::pointerLight());
    CHECK(LquiEffects::casting());

    if(failures){std::fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    std::puts("effects policy: ok");return 0;
}
