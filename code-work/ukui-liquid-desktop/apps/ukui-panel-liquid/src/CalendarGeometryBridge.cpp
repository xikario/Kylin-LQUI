#include "CalendarPlacement.h"
#include <QThread>
#include <QWindow>
#include <dlfcn.h>

// Interpose the SDK's public geometry entry point in the managed panel only.
// Do not patch system files, machine instructions, or private object fields.
// All other windows and an unmanaged/original panel retain the SDK behavior.
extern "C" void lquiCalendarSdkGeometry(QWindow *,const QRect &)
    __asm__("_ZN3kdk13WindowManager11setGeometryEP7QWindowRK5QRect");
extern "C" void lquiCalendarSdkGeometry(QWindow *window,const QRect &requested) {
    using SetGeometry=void (*)(QWindow *,const QRect &);
    static const auto original=reinterpret_cast<SetGeometry>(dlsym(RTLD_NEXT,"_ZN3kdk13WindowManager11setGeometryEP7QWindowRK5QRect"));
    QRect target=requested;
    if(window&&qApp&&QThread::currentThread()==qApp->thread()&&qApp->property("lquiCalendarGeometryEnabled").toBool()){
        for(auto *widget:QApplication::topLevelWidgets())if(widget->inherits("LunarCalendarWidget")&&widget->windowHandle()==window){
            const auto anchored=calendarDockTarget(requested.size());
            if(anchored.isValid()){
                target=anchored;
                qApp->setProperty("lquiCalendarGeometryRedirects",qApp->property("lquiCalendarGeometryRedirects").toInt()+1);
            }
            break;
        }
    }
    if(original)original(window,target);
    else if(window)window->setGeometry(target);
}
