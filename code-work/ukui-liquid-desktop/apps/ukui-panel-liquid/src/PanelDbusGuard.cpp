#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusAbstractInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <QHash>
#include <dlfcn.h>

static bool applicationLookup(const QString &service,const QString &path,const QString &interface,const QString &method) {
    const bool appdb=service=="com.ukui.search.appdb.service" && path=="/org/ukui/search/appDataBase/dbManager";
    const bool daemon=service=="org.ukui.panel.daemon" && path=="/convert/desktopwid";
    return ((appdb||daemon) && interface=="org.freedesktop.DBus.Introspectable" && method=="Introspect")
        || (appdb && interface=="org.ukui.search.appDBManager" && method=="tranWinIdToDesktopFilePath")
        || (daemon && interface=="org.ukui.panel.daemon" && method=="WIDToDesktop");
}

// Bound the OEM taskbar's synchronous application-name lookups. A busy search
// daemon otherwise stops ALL panel input, including the unstyled fallback.
// Interpose only a public Qt ABI entry; never patch OEM code or object memory.
extern "C" QDBusMessage lquiPanelDbusCall(const QDBusConnection *,const QDBusMessage &,QDBus::CallMode,int)
    __asm__("_ZNK15QDBusConnection4callERK12QDBusMessageN5QDBus8CallModeEi");
extern "C" QDBusMessage lquiPanelDbusCall(const QDBusConnection *connection,const QDBusMessage &message,QDBus::CallMode mode,int timeout) {
    using Call=QDBusMessage (*)(const QDBusConnection *,const QDBusMessage &,QDBus::CallMode,int);
    static const auto original=reinterpret_cast<Call>(dlsym(RTLD_NEXT,"_ZNK15QDBusConnection4callERK12QDBusMessageN5QDBus8CallModeEi"));
    if(!original)return message.createErrorReply("org.freedesktop.DBus.Error.Failed","Qt D-Bus entry unavailable");
    const bool panel=QCoreApplication::instance() && QCoreApplication::applicationFilePath()=="/usr/bin/ukui-panel"
        && QThread::currentThread()==QCoreApplication::instance()->thread();
    const bool lookup=applicationLookup(message.service(),message.path(),message.interface(),message.member());
    if(!panel || !lookup || mode==QDBus::NoBlock)return original(connection,message,mode,timeout);
    static QElapsedTimer clock;
    static QHash<QString,qint64> retryAfter;
    if(!clock.isValid())clock.start();
    if(clock.elapsed()<retryAfter.value(message.service()))
        return message.createErrorReply("org.freedesktop.DBus.Error.NoReply","Application lookup is cooling down; use native window identity");
    const auto reply=original(connection,message,mode,timeout>=0?qMin(timeout,120):120);
    if(reply.type()==QDBusMessage::ErrorMessage &&
       (reply.errorName()=="org.freedesktop.DBus.Error.NoReply" || reply.errorName()=="org.freedesktop.DBus.Error.Timeout"
        || reply.errorName()=="org.freedesktop.DBus.Error.TimedOut")) {
        retryAfter[message.service()]=clock.elapsed()+5000;
        qWarning()<<"[LquiPanelDbusGuard] application lookup timed out; native identity retained; retry in 5s"<<message.service();
    }
    return reply;
}

// This Qt build binds its internal Connection::call directly. The OEM plugin
// imports the public eight-argument interface overload, so guard that entry as
// well; unrelated callers still execute Qt's original implementation verbatim.
extern "C" QDBusMessage lquiPanelInterfaceCall(QDBusAbstractInterface *,const QString &,
    const QVariant &,const QVariant &,const QVariant &,const QVariant &,const QVariant &,const QVariant &,const QVariant &,const QVariant &)
    __asm__("_ZN22QDBusAbstractInterface4callERK7QStringRK8QVariantS5_S5_S5_S5_S5_S5_S5_");
extern "C" QDBusMessage lquiPanelInterfaceCall(QDBusAbstractInterface *iface,const QString &method,
    const QVariant &a,const QVariant &b,const QVariant &c,const QVariant &d,const QVariant &e,const QVariant &f,const QVariant &g,const QVariant &h) {
    using Call=QDBusMessage (*)(QDBusAbstractInterface *,const QString &,const QVariant &,const QVariant &,const QVariant &,const QVariant &,const QVariant &,const QVariant &,const QVariant &,const QVariant &);
    static const auto original=reinterpret_cast<Call>(dlsym(RTLD_NEXT,"_ZN22QDBusAbstractInterface4callERK7QStringRK8QVariantS5_S5_S5_S5_S5_S5_S5_"));
    if(QCoreApplication::instance() && QCoreApplication::applicationFilePath()=="/usr/bin/ukui-panel"
       && QThread::currentThread()==QCoreApplication::instance()->thread()
       && iface && applicationLookup(iface->service(),iface->path(),iface->interface(),method)) {
        auto message=QDBusMessage::createMethodCall(iface->service(),iface->path(),iface->interface(),method);
        QList<QVariant> arguments;
        for(const auto *value:{&a,&b,&c,&d,&e,&f,&g,&h}){if(!value->isValid())break;arguments.append(*value);}
        message.setArguments(arguments);
        const auto connection=iface->connection();
        return lquiPanelDbusCall(&connection,message,QDBus::Block,iface->timeout());
    }
    if(original)return original(iface,method,a,b,c,d,e,f,g,h);
    return QDBusMessage::createError("org.freedesktop.DBus.Error.Failed","Qt D-Bus entry unavailable");
}

// Dynamic interfaces introspect synchronously in their constructor, before
// call() can be bounded. Check these two services with a bounded request first.
// On failure construct a normal Qt disconnected interface; OEM isValid() then
// takes its existing window-ID fallback. No private Qt memory is touched.
using Construct=void (*)(QDBusInterface *,const QString &,const QString &,const QString &,const QDBusConnection &,QObject *);
static void constructLookup(Construct original,QDBusInterface *self,const QString &service,const QString &path,const QString &interface,const QDBusConnection &connection,QObject *parent) {
    if(QCoreApplication::instance() && QCoreApplication::applicationFilePath()=="/usr/bin/ukui-panel"
       && QThread::currentThread()==QCoreApplication::instance()->thread()
       && applicationLookup(service,path,"org.freedesktop.DBus.Introspectable","Introspect")) {
        const auto request=QDBusMessage::createMethodCall(service,path,"org.freedesktop.DBus.Introspectable","Introspect");
        const auto response=lquiPanelDbusCall(&connection,request,QDBus::Block,120);
        if(response.type()==QDBusMessage::ErrorMessage) {
            original(self,QString(),QString(),QString(),QDBusConnection(QString()),parent);
            return;
        }
    }
    original(self,service,path,interface,connection,parent);
}
extern "C" void lquiInterfaceConstruct(QDBusInterface *,const QString &,const QString &,const QString &,const QDBusConnection &,QObject *)
    __asm__("_ZN14QDBusInterfaceC1ERK7QStringS2_S2_RK15QDBusConnectionP7QObject");
extern "C" void lquiInterfaceConstruct(QDBusInterface *self,const QString &service,const QString &path,const QString &interface,const QDBusConnection &connection,QObject *parent) {
    static const auto original=reinterpret_cast<Construct>(dlsym(RTLD_NEXT,"_ZN14QDBusInterfaceC1ERK7QStringS2_S2_RK15QDBusConnectionP7QObject"));
    constructLookup(original,self,service,path,interface,connection,parent);
}
extern "C" void lquiInterfaceBaseConstruct(QDBusInterface *,const QString &,const QString &,const QString &,const QDBusConnection &,QObject *)
    __asm__("_ZN14QDBusInterfaceC2ERK7QStringS2_S2_RK15QDBusConnectionP7QObject");
extern "C" void lquiInterfaceBaseConstruct(QDBusInterface *self,const QString &service,const QString &path,const QString &interface,const QDBusConnection &connection,QObject *parent) {
    static const auto original=reinterpret_cast<Construct>(dlsym(RTLD_NEXT,"_ZN14QDBusInterfaceC2ERK7QStringS2_S2_RK15QDBusConnectionP7QObject"));
    constructLookup(original,self,service,path,interface,connection,parent);
}

// Keep the guard in this panel, not in programs started by its launch buttons.
__attribute__((constructor)) static void detachChildPreload() {
    Dl_info info{};
    if(!dladdr(reinterpret_cast<void *>(&lquiPanelDbusCall),&info)||!info.dli_fname)return;
    auto entries=qgetenv("LD_PRELOAD").split(':');
    if(!entries.removeAll(QByteArray(info.dli_fname)))return;
    entries.removeAll(QByteArray());
    if(entries.isEmpty())qunsetenv("LD_PRELOAD");else qputenv("LD_PRELOAD",entries.join(':'));
}
