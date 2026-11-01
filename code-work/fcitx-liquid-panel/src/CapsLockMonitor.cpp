#include "CapsLockMonitor.h"
#include <QLibrary>
#include <QSocketNotifier>
#include <X11/XKBlib.h>

class CapsLockMonitor::Native {
public:
    QLibrary library{QStringLiteral("libX11.so.6")};
    decltype(&XOpenDisplay) open=nullptr;
    decltype(&XCloseDisplay) close=nullptr;
    decltype(&XConnectionNumber) connection=nullptr;
    decltype(&XInternAtom) atom=nullptr;
    decltype(&XPending) pending=nullptr;
    decltype(&XNextEvent) next=nullptr;
    decltype(&XkbQueryExtension) query=nullptr;
    decltype(&XkbSelectEvents) select=nullptr;
    decltype(&XkbGetNamedIndicator) indicator=nullptr;
    Display *display=nullptr;
    Atom caps=None;
    std::unique_ptr<QSocketNotifier> notifier;
    ~Native() { notifier.reset(); if(display && close) close(display); }
    bool initialize() {
#define RESOLVE(field, symbol) field=reinterpret_cast<decltype(field)>(library.resolve(#symbol)); if(!field) return false;
        RESOLVE(open,XOpenDisplay)
        RESOLVE(close,XCloseDisplay)
        RESOLVE(connection,XConnectionNumber)
        RESOLVE(atom,XInternAtom)
        RESOLVE(pending,XPending)
        RESOLVE(next,XNextEvent)
        RESOLVE(query,XkbQueryExtension)
        RESOLVE(select,XkbSelectEvents)
        RESOLVE(indicator,XkbGetNamedIndicator)
#undef RESOLVE
        display=open(nullptr); if(!display) return false;
        int opcode=0,event=0,error=0,major=XkbMajorVersion,minor=XkbMinorVersion;
        if(!query(display,&opcode,&event,&error,&major,&minor)) return false;
        caps=atom(display,"Caps Lock",False);
        return select(display,XkbUseCoreKbd,XkbIndicatorStateNotifyMask,XkbIndicatorStateNotifyMask);
    }
};

CapsLockMonitor::CapsLockMonitor(QObject *parent):QObject(parent) {}
CapsLockMonitor::~CapsLockMonitor() = default;
bool CapsLockMonitor::start() {
    if(m_native) return true;
    auto native=std::make_unique<Native>();
    if(!native->initialize()) return false;
    m_native=std::move(native);
    m_native->notifier=std::make_unique<QSocketNotifier>(m_native->connection(m_native->display),QSocketNotifier::Read);
    connect(m_native->notifier.get(),&QSocketNotifier::activated,this,[this](int){
        do {
            while(m_native->pending(m_native->display)) {
                XEvent event; m_native->next(m_native->display,&event);
            }
            refresh();
            // A synchronous indicator query may have queued another event.
        } while(m_native->pending(m_native->display));
    });
    refresh();
    return true;
}
void CapsLockMonitor::refresh() {
    Bool enabled=False;
    if(!m_native->indicator(m_native->display,m_native->caps,nullptr,&enabled,nullptr,nullptr)) return;
    if(m_capsLock==bool(enabled)) return;
    m_capsLock=enabled;
    emit changed(m_capsLock);
}
