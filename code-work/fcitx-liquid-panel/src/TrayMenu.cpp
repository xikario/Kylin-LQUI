#include "TrayMenu.h"
#include <QApplication>
#include <QLibrary>

bool TrayMenu::pointerButtonDown() {
    // Query the native pointer: a tray host's release may never reach Qt's
    // cached mouseButtons(). No timer runs outside a pending popup request.
    static QLibrary x11(QStringLiteral("libX11.so.6"));
    using Open=void *(*)(const char *);
    using Root=unsigned long (*)(void *);
    using Query=int (*)(void *,unsigned long,unsigned long *,unsigned long *,int *,int *,int *,int *,unsigned int *);
    using Close=int (*)(void *);
    const auto open=reinterpret_cast<Open>(x11.resolve("XOpenDisplay"));
    const auto root=reinterpret_cast<Root>(x11.resolve("XDefaultRootWindow"));
    const auto query=reinterpret_cast<Query>(x11.resolve("XQueryPointer"));
    const auto close=reinterpret_cast<Close>(x11.resolve("XCloseDisplay"));
    if(open && root && query && close) if(void *display=open(nullptr)) {
        unsigned long r=0,c=0; int rx=0,ry=0,x=0,y=0; unsigned int mask=0;
        const bool ok=query(display,root(display),&r,&c,&rx,&ry,&x,&y,&mask);
        close(display);
        if(ok) return (mask & (0x100|0x200|0x400))!=0;
    }
    return QApplication::mouseButtons()!=Qt::NoButton;
}
TrayMenu::TrayMenu(QMenu &menu,QObject *parent):QObject(parent),m_menu(menu) {
    m_timer.setSingleShot(true);
    connect(&m_timer,&QTimer::timeout,this,[this]{
        if(m_menu.isVisible()) return;
        if(pointerButtonDown()) {
            if(m_wait.elapsed()<5000) m_timer.start(40);
            return;
        }
        if(!m_menu.isEmpty()) m_menu.popup(m_position);
    });
}
void TrayMenu::request(const QPoint &position) {
    if(m_menu.isVisible() || m_timer.isActive()) return;
    m_position=position; m_wait.start(); m_timer.start(120);
}
