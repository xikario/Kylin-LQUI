// The taskbar's casting watcher: a running casting app caps the session to
// lite; when it exits the cap is lifted. "sleep" stands in for ExceedShare.
#include "../CastingWatch.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

static int failures=0;
#define CHECK(cond) do{if(!(cond)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#cond);++failures;}}while(0)

int main(int argc,char **argv){
    QTemporaryDir home,runtime;
    qputenv("XDG_CONFIG_HOME",home.path().toUtf8());qputenv("XDG_RUNTIME_DIR",runtime.path().toUtf8());
    QCoreApplication app(argc,argv);
    {QSettings s(LquiEffects::path(),QSettings::IniFormat);s.setValue("effects/castingApps",QStringList{"NoSuchCaster","sleep"});s.sync();}
    {
        LquiEffects::CastingWatch watch(&app);
        watch.scanNow();
        CHECK(!LquiEffects::casting());
        QProcess caster;caster.start("sleep",{"30"});caster.waitForStarted();
        watch.scanNow();
        CHECK(LquiEffects::casting()&&LquiEffects::castingApp()=="sleep");
        CHECK(LquiEffects::level()==LquiEffects::Level::Lite);
        caster.kill();caster.waitForFinished();
        watch.scanNow();
        CHECK(!LquiEffects::casting()&&LquiEffects::level()==LquiEffects::Level::Full);
        caster.start("sleep",{"30"});caster.waitForStarted();watch.scanNow();
        CHECK(LquiEffects::casting());
        // Leaving the destructor must not leave a stale cap behind.
        caster.kill();caster.waitForFinished();
    }
    LquiEffects::invalidate();
    CHECK(!LquiEffects::casting());
    // Default list targets the MAXHUB client.
    {QSettings s(LquiEffects::path(),QSettings::IniFormat);s.remove("effects/castingApps");s.sync();}
    CHECK(LquiEffects::castingApps()==QStringList{"ExceedShare"});
    if(failures){std::fprintf(stderr,"%d failure(s)\n",failures);return 1;}
    std::puts("casting watch: ok");return 0;
}
