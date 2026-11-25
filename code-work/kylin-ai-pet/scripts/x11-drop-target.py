"""Expose a native Qt drop target over the pet only during external X11 drags.

Chromium does not offer Qt's image selection formats to HTML. XFixes tells
us when a drag starts, so the regular pet takes all ordinary mouse input.
"""
import ctypes as C
import json
import os
import signal
import sys
from PyQt5.QtCore import Q_CLASSINFO, pyqtSlot, QAbstractNativeEventFilter, QByteArray, QBuffer, QIODevice, Qt, QSocketNotifier, QTimer
from PyQt5.QtDBus import QDBusAbstractAdaptor, QDBusConnection, QDBusMessage
from PyQt5.QtGui import QImage, QRegion
from PyQt5.QtWidgets import QApplication, QWidget

UL = C.c_ulong
I = C.c_int
P = C.c_void_p
class Rectangle(C.Structure):
    _fields_ = [('x',C.c_short),('y',C.c_short),('w',C.c_ushort),('h',C.c_ushort)]
class SelectionNotify(C.Structure):
    _fields_ = [('type',I),('serial',UL),('send_event',I),('display',P),('window',UL),
                ('subtype',I),('owner',UL),('selection',UL),('timestamp',UL),('selection_timestamp',UL)]

def bind(lib, name, restype, *args):
    fn = getattr(lib,name)
    fn.restype,fn.argtypes = restype,args
    return fn

x = C.CDLL('libX11.so.6')
fix = C.CDLL('libXfixes.so.3')
ext = C.CDLL('libXext.so.6')
open_display = bind(x,'XOpenDisplay',P,C.c_char_p)
display = open_display(None)
if not display: sys.exit(1)
root = bind(x,'XDefaultRootWindow',UL,P)(display)
atom = bind(x,'XInternAtom',UL,P,C.c_char_p,I)
sel = atom(display,b'XdndSelection',0)
subscribe = bind(fix,'XFixesSelectSelectionInput',None,P,UL,UL,UL)
geometry = bind(x,'XGetGeometry',I,P,UL,C.POINTER(UL),C.POINTER(I),C.POINTER(I),*[C.POINTER(C.c_uint)]*4)
translate = bind(x,'XTranslateCoordinates',I,P,UL,UL,I,I,C.POINTER(I),C.POINTER(I),C.POINTER(UL))
shape = bind(ext,'XShapeGetRectangles',C.POINTER(Rectangle),P,UL,I,C.POINTER(I),C.POINTER(I))
free = bind(x,'XFree',I,P)
pending = bind(x,'XPending',I,P)
next_event = bind(x,'XNextEvent',I,P,P)
pet = int(sys.argv[1])

# Keep the pet managed so KWin can toggle its stacking, but leave keyboard
# input with the current app even when the user pats the floating character.
class WMHints(C.Structure):
    _fields_ = [('flags',C.c_long),('input',I),('initial_state',I),('icon_pixmap',UL),
                ('icon_window',UL),('icon_x',I),('icon_y',I),('icon_mask',UL),('window_group',UL)]

hints_type = C.POINTER(WMHints)
hints = bind(x,'XGetWMHints',hints_type,P,UL)(display,pet)
if not hints:
    hints = bind(x,'XAllocWMHints',hints_type)()
if hints:
    hints.contents.flags |= 1  # ICCCM InputHint
    hints.contents.input = 0
    bind(x,'XSetWMHints',I,P,UL,hints_type)(display,pet,hints)
    free(hints)
    bind(x,'XFlush',I,P)(display)

app = QApplication([])
app.setQuitOnLastWindowClosed(False)

class Target(QWidget):
    def __init__(self):
        super().__init__(None,Qt.ToolTip|Qt.FramelessWindowHint|Qt.X11BypassWindowManagerHint|Qt.WindowDoesNotAcceptFocus)
        self.setAttribute(Qt.WA_ShowWithoutActivating)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setAcceptDrops(True)
        self.setWindowTitle('AI 宠物拖放')
        self.end_timer = QTimer(self)
        self.end_timer.setSingleShot(True)
        self.end_timer.timeout.connect(self.hide)

    def arm(self):
        self.end_timer.stop()
        rx,ry,child = I(),I(),UL()
        width,height,border,depth = [C.c_uint() for _ in range(4)]
        if not geometry(display,pet,C.byref(child),C.byref(rx),C.byref(ry),C.byref(width),C.byref(height),C.byref(border),C.byref(depth)):return
        translate(display,pet,root,0,0,C.byref(rx),C.byref(ry),C.byref(child))
        count,order = I(),I()
        rects = shape(display,pet,0,C.byref(count),C.byref(order))
        region = QRegion()
        for rect in rects[:count.value]:
            region |= QRegion(rect.x,rect.y,rect.w,rect.h)
        if rects: free(rects)
        if region.isEmpty():return
        self.setGeometry(rx.value,ry.value,width.value,height.value)
        self.setMask(region)
        self.show()
        self.raise_()

    def dragEnterEvent(self,event):
        mime = event.mimeData()
        if event.possibleActions() & Qt.CopyAction and (mime.hasUrls() or mime.hasImage() or mime.hasText()):
            event.setDropAction(Qt.CopyAction)
            event.accept()

    dragMoveEvent = dragEnterEvent

    def dropEvent(self,event):
        if not event.possibleActions() & Qt.CopyAction:return
        mime = event.mimeData()
        payload = {'files':[url.toLocalFile() for url in mime.urls() if url.isLocalFile()],
                   'links':[url.toString() for url in mime.urls() if not url.isLocalFile()]}
        if not payload['files'] and mime.hasImage():
            image = mime.imageData()
            if hasattr(image,'toImage'): image = image.toImage()
            if not isinstance(image,QImage) or image.isNull() or image.width()*image.height()>32000000:return
            data = QByteArray()
            buf = QBuffer(data)
            if not buf.open(QIODevice.WriteOnly) or not image.save(buf,'PNG') or data.size()>16*1024*1024:return
            payload['image'] = bytes(data.toBase64()).decode('ascii')
        elif mime.hasText():
            payload['text'] = mime.text()
        event.setDropAction(Qt.CopyAction)
        event.accept()
        self.hide()
        def deliver():
            call = QDBusMessage.createMethodCall('org.ukui.AiStationPanel','/AiStation','org.ukui.AiStationPanel','PetDrop')
            call.setArguments([json.dumps(payload,ensure_ascii=False)])
            QDBusConnection.sessionBus().asyncCall(call,10000)
        QTimer.singleShot(0,deliver)

class CopyOffer(QAbstractNativeEventFilter):
    # Qt 5.12 Peony drags can advertise Move without publishing XdndActionList.
    # As in the station orb, the target accepts a reference to the source file.
    def nativeEventFilter(self, event_type, message):
        if event_type == b'xcb_generic_event_t':
            buf = (C.c_uint32 * 8).from_address(int(message))
            if buf[0] & 127 == 33 and buf[1] == window_id and buf[2] == xdnd_position:
                buf[7] = copy_action
        return False,0

class PetControl(QDBusAbstractAdaptor):
    Q_CLASSINFO('D-Bus Interface','org.ukui.AiPet')

    @pyqtSlot()
    def Toggle(self):
        print(json.dumps({'event':'toggle'}),flush=True)

    @pyqtSlot(str,str,str)
    def React(self,character,phase,text):
        if visible and phase in ('thinking','reply','error'):
            print(json.dumps({'event':phase,'character':character,'text':text[:36]}),flush=True)

target = Target()
visible = False
control = PetControl(target)
bus = QDBusConnection.sessionBus()
bus.registerObject('/Pet',target,QDBusConnection.ExportAdaptors)
bus.registerService('org.ukui.AiPet')
window_id = int(target.winId())
xdnd_position = atom(display,b'XdndPosition',0)
copy_action = atom(display,b'XdndActionCopy',0)
copy_filter = CopyOffer()
app.installNativeEventFilter(copy_filter)
# Some sources keep the selection after a cancelled drag. Retire the overlay
# after release even when there is no XFixes owner-change event.
release_timer = QTimer(target)
release_timer.setInterval(100)
def released():
    if not target.isVisible():
        release_timer.stop()
    elif not pointer_down():
        target.end_timer.start(100)
        release_timer.stop()
def pointer_down():
    root_id,child = UL(),UL()
    coords=[I() for _ in range(4)]
    mask=C.c_uint()
    query_pointer(display,root,C.byref(root_id),C.byref(child),*(C.byref(n) for n in coords),C.byref(mask))
    return bool(mask.value & (1 << 8))
query_pointer = bind(x,'XQueryPointer',I,P,UL,C.POINTER(UL),C.POINTER(UL),*[C.POINTER(I)]*4,C.POINTER(C.c_uint))
release_timer.timeout.connect(released)
event_base,error_base = I(),I()
query = bind(fix,'XFixesQueryExtension',I,P,C.POINTER(I),C.POINTER(I))
if not query(display,C.byref(event_base),C.byref(error_base)):sys.exit(1)
subscribe(display,root,sel,7)
bind(x,'XSync',I,P,I)(display,0)
def selection_changed():
    event = (C.c_long*24)()
    while pending(display):
        next_event(display,event)
        change = C.cast(event,C.POINTER(SelectionNotify)).contents
        if change.type == event_base.value and change.selection == sel:
            if visible and change.owner and pointer_down():
                target.arm();release_timer.start()
            else:target.end_timer.start(100)
events = QSocketNotifier(bind(x,'XConnectionNumber',I,P)(display),QSocketNotifier.Read)
events.activated.connect(selection_changed)
# Electron closes this pipe on hide, exit or crash.
parent = QSocketNotifier(sys.stdin.fileno(),QSocketNotifier.Read)
pending_input = b''
def process_visibility():
    global visible,pending_input
    data = os.read(sys.stdin.fileno(),4096)
    if not data:
        app.quit()
        return
    pending_input += data
    while b'\n' in pending_input:
        line,pending_input = pending_input.split(b'\n',1)
        visible = line == b'show'
        if not visible: target.hide()
parent.activated.connect(process_visibility)
signal.signal(signal.SIGTERM,signal.SIG_DFL)
app.exec_()
