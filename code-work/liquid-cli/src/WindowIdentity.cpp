#include "WindowIdentity.h"
#include <QGuiApplication>
#include <QWidget>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

void applyStationWindowIdentity(QWidget *window) {
    window->setWindowRole("ai-station-settings");
    if(QGuiApplication::platformName()!="xcb")return;
    Display *display=XOpenDisplay(nullptr);if(!display)return;
    const Window id=static_cast<Window>(window->winId());QByteArray identity("liquid-cli");
    XClassHint hint{identity.data(),identity.data()};XSetClassHint(display,id,&hint);
    XChangeProperty(display,id,XInternAtom(display,"_KDE_NET_WM_DESKTOP_FILE",False),
        XInternAtom(display,"UTF8_STRING",False),8,PropModeReplace,
        reinterpret_cast<const unsigned char *>(identity.constData()),identity.size());
    XSync(display,False);XCloseDisplay(display);
}
