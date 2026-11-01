#include "../../../shared/desktop-launch/DesktopEntryFile.h"
#include "ActivityRecorder.h"
#include "../../../shared/async-work/BackgroundTask.h"
#include <QApplication>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

struct ActivityRecorder::Native {
    Display *display = nullptr; Atom active = 0, type = 0, desktop = 0, dock = 0, pid = 0, clients=0;
    QSocketNotifier *notifier = nullptr;
    QHash<QString, QString> names;
    ~Native() { delete notifier; if (display) XCloseDisplay(display); }
};
namespace {
int ignoreDestroyedWindow(Display *, XErrorEvent *) { return 0; }
QByteArray fileBytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed() : QByteArray(); }
unsigned long xProperty(Display *d, Window w, Atom a, Atom requested) {
    Atom actual; int format; unsigned long count, rest; unsigned char *bytes = nullptr;
    unsigned long value = 0;
    if (XGetWindowProperty(d,w,a,0,1,False,requested,&actual,&format,&count,&rest,&bytes)==Success && bytes && format==32 && count)
        value = *reinterpret_cast<unsigned long *>(bytes);
    if (bytes) XFree(bytes); return value;
}
}
QString ActivityRecorder::dataPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/ukui-fences/widgets/activity.json";
}
QString ActivityRecorder::metricsDirectory() const {return QFileInfo(dataPath()).absolutePath()+"/activity-metrics";}
ActivityRecorder::ActivityRecorder(QObject *parent) : QObject(parent), m_native(new Native) {
    QFile f(dataPath());
    const bool hadFile=QFileInfo::exists(dataPath())||QFileInfo(dataPath()).isSymLink();
    const bool opened=f.open(QIODevice::ReadOnly);
    if(hadFile&&!opened)m_saveOk=false;
    if (opened) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(f.readAll(), &error);
        const int version=doc.object().value("schemaVersion").toInt();
        if (error.error == QJsonParseError::NoError && doc.isObject() && (version==1||version==2)) {
            m_ledger = ActivityLedger::fromJson(doc.object());
            if(version==1){f.close();m_saveOk=QFile::copy(dataPath(),dataPath()+".v1-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+".bak");}
        } else {
            f.close();
            // Preserve an unreadable file before creating a replacement ledger.
            const QString backup = dataPath()+".invalid-"+QString::number(QDateTime::currentMSecsSinceEpoch());
            m_saveOk = QFile::copy(dataPath(), backup);
            if (!m_saveOk) qWarning() << "Activity ledger could not be backed up; writes disabled";
        }
    }
    if(opened&&f.error()!=QFile::NoError)m_saveOk=false;
    m_recording = QSettings().value("desklets/activity/recording", true).toBool();
    m_metricsEnabled=QSettings().value("desklets/activity/metricsEnabled",true).toBool();
    m_metricsInterval=qBound(5,QSettings().value("desklets/activity/metricsInterval",10).toInt(),60);
    m_logRetention=qBound(1,QSettings().value("desklets/activity/logRetentionDays",7).toInt(),30);
    for (const QString &root : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        for (const QString &file : QDir(root).entryList({"*.desktop"}, QDir::Files)) {
            DesktopEntryFile::Reader desktop(root+"/"+file);
            const QString key = desktop.value("StartupWMClass").toString().toLower();
            const QString name = desktop.value("Name[zh_CN]",desktop.value("Name")).toString();
            if (!name.isEmpty()) {
                if (!key.isEmpty()) m_native->names.insert(key,name);
                m_native->names.insert(file.left(file.size()-8).toLower(),name);
            }
        }
    }
    if (QGuiApplication::platformName() == "xcb") m_native->display = XOpenDisplay(nullptr);
    if (auto *d = m_native->display) {
        m_native->active = XInternAtom(d,"_NET_ACTIVE_WINDOW",False);
        m_native->type = XInternAtom(d,"_NET_WM_WINDOW_TYPE",False);
        m_native->desktop = XInternAtom(d,"_NET_WM_WINDOW_TYPE_DESKTOP",False);
        m_native->dock = XInternAtom(d,"_NET_WM_WINDOW_TYPE_DOCK",False);
        m_native->pid = XInternAtom(d,"_NET_WM_PID",False);
        m_native->clients = XInternAtom(d,"_NET_CLIENT_LIST",False);
        XSelectInput(d,DefaultRootWindow(d),PropertyChangeMask); XFlush(d);
        m_native->notifier = new QSocketNotifier(ConnectionNumber(d),QSocketNotifier::Read,this);
        connect(m_native->notifier,&QSocketNotifier::activated,this,[this,d] {
            bool changed = false;
            while (XPending(d)) { XEvent e; XNextEvent(d,&e); if (e.type==PropertyNotify && e.xproperty.atom==m_native->active) changed=true; }
            if (changed) sample();
        });
    }
    for (const QString &service : {QStringLiteral("org.freedesktop.ScreenSaver"),QStringLiteral("org.ukui.ScreenSaver"),QStringLiteral("org.gnome.ScreenSaver")}) {
        const QString path = service == "org.freedesktop.ScreenSaver" ? "/ScreenSaver" : "/org/"+service.section('.',1,1)+"/ScreenSaver";
        QDBusConnection::sessionBus().connect(service,path,service,"ActiveChanged",this,SLOT(setLocked(bool)));
        QDBusInterface iface(service,path,service,QDBusConnection::sessionBus());
        if (iface.isValid()) { QDBusReply<bool> active = iface.call("GetActive"); if (active.isValid() && active.value()) m_locked=true; }
    }
    // This UKUI release exports its lock service at / with parameterless
    // lock/unlock signals, not the freedesktop ActiveChanged interface.
    auto bus = QDBusConnection::sessionBus();
    for (const QString &signal : {QStringLiteral("lock"), QStringLiteral("prepareForLock"), QStringLiteral("ScreenSaverLock")})
        bus.connect("org.ukui.ScreenSaver", "/", "org.ukui.ScreenSaver", signal, this, SLOT(sessionLocked()));
    bus.connect("org.ukui.ScreenSaver", "/", "org.ukui.ScreenSaver", "unlock", this, SLOT(sessionUnlocked()));
    QDBusInterface ukuiLock("org.ukui.ScreenSaver", "/", "org.ukui.ScreenSaver", bus);
    if (ukuiLock.isValid()) {
        QDBusReply<bool> locked = ukuiLock.call("GetLockState");
        if (locked.isValid()) m_locked = locked.value();
    }
    m_elapsed.start(); m_lastWall = QDateTime::currentDateTime();
    connect(&m_sampleTimer,&QTimer::timeout,this,&ActivityRecorder::sample); m_sampleTimer.start(5000);
    connect(&m_saveTimer,&QTimer::timeout,this,&ActivityRecorder::flush); m_saveTimer.start(60000);
    m_metricsTimer.setTimerType(Qt::CoarseTimer);connect(&m_metricsTimer,&QTimer::timeout,this,&ActivityRecorder::sampleResources);syncResourceSampling();
    connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{sample();flush();});
    sample(); flush();
}
ActivityRecorder::~ActivityRecorder() { ++m_metricsRevision;m_metricsTimer.stop();sample();flush(); }
void ActivityRecorder::setRecording(bool enabled) {
    sample(); m_recording=enabled; QSettings().setValue("desklets/activity/recording",enabled);
    syncResourceSampling();sample(); flush(); emit changed();
}
void ActivityRecorder::setLocked(bool locked) { sample(); m_locked=locked;syncResourceSampling();sample();flush(); }
void ActivityRecorder::sample() {
    const auto now = QDateTime::currentDateTime();
    const qint64 elapsed = m_elapsed.restart();
    const qint64 wall = m_lastWall.msecsTo(now); m_lastWall=now;
    // Never attribute suspend, event-loop stalls, or a clock jump to an app.
    if (m_recording && !m_locked && !m_appId.isEmpty() && elapsed > 0 && elapsed <= 10000 && qAbs(wall-elapsed)<2000)
        m_ledger.addInterval(now,elapsed,m_appId,m_appName);
    const QByteArray uptimeText = fileBytes("/proc/uptime").split(' ').value(0);
    bool ok=false; const double uptime=uptimeText.toDouble(&ok);
    if(ok) m_ledger.observeBoot(QString::fromUtf8(fileBytes("/proc/sys/kernel/random/boot_id")),qint64(uptime*1000),now);
    m_appId.clear(); m_appName.clear();
    if (m_recording && !m_locked && m_native->display) {
        auto *d=m_native->display;
        XSync(d,False); auto oldHandler=XSetErrorHandler(ignoreDestroyedWindow);
        const Window w=xProperty(d,DefaultRootWindow(d),m_native->active,XA_WINDOW);
        if(w) {
            const Atom t=xProperty(d,w,m_native->type,XA_ATOM);
            const auto pid=xProperty(d,w,m_native->pid,XA_CARDINAL);
            if(t!=m_native->desktop && t!=m_native->dock && pid!=static_cast<unsigned long>(QCoreApplication::applicationPid())) {
                XClassHint cls{};
                if(XGetClassHint(d,w,&cls)) {
                    const QString name=QString::fromUtf8(cls.res_class ? cls.res_class : "");
                    const QString id=name.toLower();
                    if(!id.isEmpty() && id!="ukui-lockscreen" && id!="ukui-screensaver" && id!="ukui-fences") {
                        m_appId=id; m_appName=m_native->names.value(id,name);
                    }
                    if(cls.res_name)XFree(cls.res_name); if(cls.res_class)XFree(cls.res_class);
                }
            }
        }
        XSync(d,False); XSetErrorHandler(oldHandler);
    }
    emit changed();
}
void ActivityRecorder::flush() {
    if(!m_saveOk) return;
    QDir().mkpath(QFileInfo(dataPath()).absolutePath());
    QSaveFile f(dataPath());
    const auto data=QJsonDocument(m_ledger.toJson()).toJson(QJsonDocument::Compact);
    if(!f.open(QIODevice::WriteOnly) || f.write(data)!=data.size() || !f.commit()) {
        qWarning()<<"Could not save activity ledger"<<f.errorString();
        // Retry a transient failure on the next flush, while exposing the error.
        setProperty("saveError",true); emit changed();
    } else {setProperty("saveError",false);QFile::setPermissions(dataPath(),QFileDevice::ReadOwner|QFileDevice::WriteOwner);}
    flushMetricLogs();pruneMetricLogs();
}

void ActivityRecorder::syncResourceSampling() {
    ++m_metricsRevision;m_previousResources={};m_liveResources.clear();
    m_metricsTimer.stop();
    if(m_recording&&m_metricsEnabled&&!m_locked)m_metricsTimer.start(m_metricsInterval*1000);
}
void ActivityRecorder::configureMetrics(bool enabled,int intervalSeconds,int retentionDays) {
    m_metricsEnabled=enabled;m_metricsInterval=qBound(5,intervalSeconds,60);m_logRetention=qBound(1,retentionDays,30);
    QSettings settings;settings.setValue("desklets/activity/metricsEnabled",enabled);settings.setValue("desklets/activity/metricsInterval",m_metricsInterval);settings.setValue("desklets/activity/logRetentionDays",m_logRetention);settings.sync();
    syncResourceSampling();flush();emit changed();
}
QHash<int,QString> ActivityRecorder::windowProcessRoots() {
    QHash<int,QString> roots;auto *display=m_native->display;if(!display)return roots;
    XSync(display,False);const auto oldHandler=XSetErrorHandler(ignoreDestroyedWindow);
    Atom actual;int format=0;unsigned long count=0,rest=0;unsigned char *bytes=nullptr;
    if(XGetWindowProperty(display,DefaultRootWindow(display),m_native->clients,0,8192,False,XA_WINDOW,&actual,&format,&count,&rest,&bytes)==Success&&bytes&&format==32) {
        const auto *windows=reinterpret_cast<unsigned long *>(bytes);
        for(unsigned long i=0;i<count;++i) {
            const Window window=windows[i];const Atom type=xProperty(display,window,m_native->type,XA_ATOM);
            const int pid=int(xProperty(display,window,m_native->pid,XA_CARDINAL));
            if(pid<=0||pid==QCoreApplication::applicationPid()||type==m_native->desktop||type==m_native->dock)continue;
            XClassHint hint{};if(!XGetClassHint(display,window,&hint))continue;
            const QString id=QString::fromUtf8(hint.res_class?hint.res_class:"").toLower();
            if(!id.isEmpty()&&id!="ukui-screensaver"&&id!="ukui-lockscreen"&&id!="ukui-fences")roots.insert(pid,id);
            if(hint.res_name)XFree(hint.res_name);if(hint.res_class)XFree(hint.res_class);
        }
    }
    if(bytes)XFree(bytes);XSync(display,False);XSetErrorHandler(oldHandler);return roots;
}
void ActivityRecorder::sampleResources() {
    if(!m_metricsEnabled||!m_recording||m_locked||m_metricsPending)return;
    const auto apps=m_ledger.days.value(QDate::currentDate()).apps;
    QStringList selected=apps.keys();std::sort(selected.begin(),selected.end(),[&](const QString &a,const QString &b){return apps[a].ms==apps[b].ms?a<b:apps[a].ms>apps[b].ms;});
    while(selected.size()>5)selected.removeLast();
    const auto roots=windowProcessRoots();
    if(selected.isEmpty()||roots.isEmpty()){m_previousResources={};m_liveResources.clear();emit changed();return;}
    QMap<QString,QString> names;for(const auto &id:selected)names[id]=apps.value(id).name;
    const auto previous=m_previousResources;const auto revision=m_metricsRevision;const int maxInterval=m_metricsInterval*3000;
    m_metricsPending=true;
    if(!BackgroundTask::run(this,[previous,roots,selected,maxInterval]{
        auto snapshot=ProcessMetrics::read();return qMakePair(snapshot,ProcessMetrics::aggregate(snapshot,previous,roots,selected,maxInterval));
    },[this,revision,names](const QPair<ProcessMetrics::Snapshot,QMap<QString,ProcessMetrics::Group>> &result){
        m_metricsPending=false;if(revision!=m_metricsRevision||!m_recording||!m_metricsEnabled||m_locked)return;
        acceptResources(result.first,result.second,names);
    }))m_metricsPending=false;
}
void ActivityRecorder::acceptResources(const ProcessMetrics::Snapshot &snapshot,const QMap<QString,ProcessMetrics::Group> &groups,const QMap<QString,QString> &names) {
    const qint64 elapsed=m_previousResources.monotonicMs>0?snapshot.monotonicMs-m_previousResources.monotonicMs:0;
    m_liveResources=groups;
    for(auto it=groups.cbegin();it!=groups.cend();++it) {
        const auto &group=it.value();const qint64 duration=group.cpuPercent>=0&&elapsed>0&&elapsed<=m_metricsInterval*3000?elapsed:0;
        const QString name=names.value(it.key(),it.key());m_ledger.addResources(snapshot.at,duration,it.key(),name,group.cpuPercent,group.rssMb,group.processes);
        const QString key=snapshot.at.toString("yyyy-MM-dd/HH:mm/")+it.key();auto &bucket=m_pendingLogs[key];
        if(!bucket.from.isValid()){bucket.from=snapshot.at.addMSecs(-duration);bucket.id=it.key();bucket.name=name;}
        bucket.to=snapshot.at;bucket.stats.observe(group.cpuPercent,group.rssMb,duration,group.processes,snapshot.at);
    }
    m_previousResources=snapshot;emit changed();
}
void ActivityRecorder::flushMetricLogs() {
    if(m_pendingLogs.isEmpty())return;
    const QString directory=metricsDirectory();QDir().mkpath(directory);QFile::setPermissions(directory,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    QMap<QString,QByteArray> payloads;QMap<QString,QStringList> keys;
    for(auto it=m_pendingLogs.cbegin();it!=m_pendingLogs.cend();++it) {
        const auto &bucket=it.value();const QString day=bucket.to.date().toString(Qt::ISODate);auto record=bucket.stats.toJson();
        record.insert("schemaVersion",1);record.insert("appId",bucket.id);record.insert("name",bucket.name);record.insert("from",bucket.from.toString(Qt::ISODateWithMs));record.insert("to",bucket.to.toString(Qt::ISODateWithMs));
        record.insert("cpuBasis","single-core-100-percent");record.insert("memoryBasis","process-group-rss-mb");
        payloads[day]+=QJsonDocument(record).toJson(QJsonDocument::Compact)+'\n';keys[day]<<it.key();
    }
    bool failed=false;
    for(auto it=payloads.cbegin();it!=payloads.cend();++it) {
        QFile file(directory+"/"+it.key()+".jsonl");
        if(!file.open(QIODevice::ReadWrite|QIODevice::Append)){failed=true;continue;}
        const qint64 original=file.size();
        if(original+it.value().size()>16*1024*1024){failed=true;continue;}
        if(file.write(it.value())!=it.value().size()||!file.flush()){file.resize(original);failed=true;continue;}
        file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
        for(const auto &key:keys[it.key()])m_pendingLogs.remove(key);
    }
    setProperty("metricsSaveError",failed);
}
void ActivityRecorder::pruneMetricLogs() {
    const QDate oldest=QDate::currentDate().addDays(1-m_logRetention);QDir directory(metricsDirectory());
    for(const auto &name:directory.entryList({"*.jsonl"},QDir::Files)) {
        const QDate day=QDate::fromString(name.left(10),Qt::ISODate);
        if(day.isValid()&&name==day.toString(Qt::ISODate)+".jsonl"&&day<oldest)QFile::remove(directory.filePath(name));
    }
}
QString ActivityRecorder::csvForDay(const QDate &day) const {
    auto cell=[](QString value){if(!value.isEmpty()&&(QString("=+-@\t\r").contains(value.front())))value.prepend('\'');value.replace('"',"\"\"");return '"'+value+'"';};
    auto number=[](double value){return value<0?QString():QString::number(value,'f',2);};
    QStringList lines{QStringLiteral("日期,应用,前台停留秒,CPU采样均值_单核百分比,CPU采样峰值_单核百分比,RSS均值_MB,RSS峰值_MB,已采样运行秒,有效样本数,最近采样")};
    const auto apps=m_ledger.days.value(day).apps;
    for(const auto &app:apps){const auto &r=app.resources;lines<<QStringList{cell(day.toString(Qt::ISODate)),cell(app.name),QString::number(app.ms/1000.,'f',3),number(r.cpuMean()),number(r.cpuPeak),number(r.rssMeanMb()),number(r.rssPeakMb),QString::number(r.observedMs/1000.,'f',3),QString::number(r.samples),cell(r.lastAt.toString(Qt::ISODateWithMs))}.join(',');}
    return lines.join('\n')+'\n';
}
