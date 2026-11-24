"""Calendar presentation using the control center's actual cached glass renderer."""
from pathlib import Path
import sys,json
from PyQt5 import QtCore as C, QtGui as G, QtWidgets as W

HERE = Path(__file__).resolve().parent
if not (HERE/'glass_widgets.py').exists():
    sys.path.insert(0, str(HERE.parent.parent/'kylin-lqui-settings'))
from glass_widgets import GlassCard, diffuse_backdrop, glyph
import effects_policy
import popup_style

STYLE = '''
QWidget {font-family:"Noto Sans CJK SC";font-size:12px;color:#f0f4fa;}
QWidget#calendarRoot,QWidget#content,QScrollArea,QScrollArea>QWidget>QWidget {background:transparent;border:0;}
QLabel {background:transparent;border:0;color:#eef3fa;}
QLabel#muted {color:#b9c9d8;font-size:11px;}
QLabel#centerTitle {font-size:17px;font-weight:600;}
QLabel#selectedDate {font-size:14px;font-weight:600;}
QPushButton,QToolButton {background:rgba(220,235,255,20);color:#eef4ff;border:0;border-radius:10px;padding:6px 9px;}
QToolButton {padding:0;}
QPushButton:hover,QToolButton:hover {background:rgba(220,235,255,45);}
QPushButton:pressed,QToolButton:pressed {background:rgba(220,235,255,70);}
QPushButton:disabled,QToolButton:disabled {color:#778a99;background:rgba(180,205,230,8);}
QPushButton[quiet="true"],QToolButton[quiet="true"] {background:transparent;}
QPushButton[quiet="true"]:hover,QToolButton[quiet="true"]:hover {background:rgba(220,235,255,30);}
QPushButton:checked {background:rgba(126,210,194,50);color:#c9f2e9;}
QPushButton#monthTitle {font-size:16px;font-weight:600;text-align:left;background:transparent;padding:4px 0;}
QFrame#monthCard {background:rgba(7,17,26,78);border:1px solid rgba(210,230,245,35);border-radius:20px;}
QFrame#segment {background:rgba(5,14,22,110);border:1px solid rgba(200,220,240,28);border-radius:12px;}
QCalendarWidget,QCalendarWidget QWidget,QCalendarWidget QAbstractItemView {background:transparent;alternate-background-color:transparent;border:0;selection-background-color:transparent;outline:0;}
QCalendarWidget QToolButton {background:transparent;}
QListWidget {background:rgba(5,14,22,110);border:1px solid rgba(210,230,245,52);border-radius:18px;outline:0;padding:5px;}
QScrollBar:vertical {width:7px;background:transparent;margin:8px 0;}
QScrollBar::handle:vertical {background:rgba(178,204,221,85);min-height:24px;border-radius:3px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;border:0;background:transparent;}
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical {background:transparent;}
QDialog,QMessageBox,QMenu {background:#172632;border:1px solid #536573;border-radius:12px;}
QMenu::item {padding:6px 14px;} QMenu::item:selected {background:#33534f;}
QLineEdit,QDateEdit,QTimeEdit,QSpinBox,QPlainTextEdit {background:rgba(10,18,28,125);color:#f1f5ff;border:1px solid rgba(215,234,255,65);border-radius:8px;padding:6px;}
QToolTip {background:#eef4fa;color:#27394d;border:1px solid #a7bdcf;padding:6px;}
'''


# The parent calendar uses light text on glass. Own the whole modal surface as
# well: the OEM message box can otherwise supply a white background.
DIALOG_STYLE = """
QDialog#agendaDialog, QFrame#agendaSurface {background-color:#1c2c38;color:#f5f8fb;border:0;}
QFrame#agendaSurface {border:1px solid #516572;border-radius:13px;}
QLabel {background:transparent;color:#f5f8fb;border:0;font-size:14px;}
QLabel#agendaHeading {font-size:18px;font-weight:600;}
QLabel#agendaMeta {color:#b3e8de;font-size:13px;}
QPlainTextEdit, QLineEdit, QDateEdit, QTimeEdit {background:#11232e;color:#f5f8fb;
    border:1px solid #546b79;border-radius:7px;padding:6px;font-size:14px;
    selection-background-color:#396a66;selection-color:white;}
QPlainTextEdit#agendaBody {border:0;border-radius:8px;padding:4px;}
QPushButton {background:#344a57;color:#f5f8fb;border:1px solid #657985;
    border-radius:8px;padding:5px 15px;min-height:22px;font-size:13px;}
QPushButton:hover {background:#405b69;} QPushButton:pressed {background:#283e4b;}
QPushButton[primary="true"] {background:#a9e4d4;color:#112b28;border-color:#a9e4d4;font-weight:600;}
QPushButton[primary="true"]:hover {background:#c4f3e6;}
QPushButton:focus {border:2px solid #d2e8ff;}
QCalendarWidget, QCalendarWidget QWidget, QCalendarWidget QAbstractItemView {
    background:#223440;color:#f5f8fb;selection-background-color:#396a66;selection-color:white;}
"""


class AgendaDialog(W.QDialog):
    def __init__(self, parent, title):
        super().__init__(parent, C.Qt.Dialog | C.Qt.FramelessWindowHint)
        self.setWindowTitle(title)
        self.setObjectName('agendaDialog')
        self.setAttribute(C.Qt.WA_TranslucentBackground)
        palette = self.palette()
        for group in (G.QPalette.Active, G.QPalette.Inactive):
            for role, ink in ((G.QPalette.Window, '#1c2c38'), (G.QPalette.Base, '#11232e'),
                              (G.QPalette.WindowText, '#f5f8fb'), (G.QPalette.Text, '#f5f8fb'),
                              (G.QPalette.Button, '#344a57'), (G.QPalette.ButtonText, '#f5f8fb'),
                              (G.QPalette.Highlight, '#396a66'), (G.QPalette.HighlightedText, '#ffffff')):
                palette.setColor(group, role, G.QColor(ink))
        self.setPalette(palette)
        self.setStyleSheet(DIALOG_STYLE)
        outer = W.QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        surface = W.QFrame(self)
        surface.setObjectName('agendaSurface')
        surface.setAttribute(C.Qt.WA_StyledBackground)
        outer.addWidget(surface)
        self.content = W.QVBoxLayout(surface)
        self.content.setContentsMargins(18, 16, 18, 16)
        self.content.setSpacing(12)
        heading = W.QLabel(title)
        heading.setObjectName('agendaHeading')
        self.content.addWidget(heading)

    def run(self):
        try:
            return self.exec_()
        finally:
            self.deleteLater()

    def add_buttons(self, accept='关闭', cancel=False, cancel_default=False):
        buttons = W.QDialogButtonBox(self)
        ok = buttons.addButton(accept, W.QDialogButtonBox.AcceptRole)
        primary = ok
        if cancel:
            back = buttons.addButton('取消', W.QDialogButtonBox.RejectRole)
            if cancel_default:
                primary = back
                ok.setAutoDefault(False)
        primary.setDefault(True)
        primary.setProperty('primary', True)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        self.content.addWidget(buttons)
        return ok


# Each shape in its own design box: (box size, pen width, draw(painter)).
def _polyline(*points):
    return lambda p:p.drawPolyline(G.QPolygonF([C.QPointF(x,y) for x,y in points]))
def _refresh(p):
    p.drawArc(C.QRectF(4,4,12,12),35*16,290*16);_polyline((16,3),(16,7),(12,7))(p)
def _close(p):
    p.drawLine(C.QPointF(6,6),C.QPointF(18,18));p.drawLine(C.QPointF(18,6),C.QPointF(6,18))
ICON_SHAPES={
    'back':(20,1.5,_polyline((12,5),(7,10),(12,15))),
    'refresh':(20,1.5,_refresh),
    'chevron':(24,1.7,_polyline((9,6),(15,12),(9,18))),
    'close':(24,1.7,_close),
}
ICON_COLOR='#c5d7e5'


class VectorIconEngine(G.QIconEngine):
    """Draws at the exact requested device size, so 125/150% stay sharp.

    Fixed bitmaps were drawn at 2x and then resampled to 27 physical pixels.
    """
    def __init__(self,name):
        super().__init__();self.name=name
    def clone(self):
        return VectorIconEngine(self.name)
    def paint(self,painter,rect,mode,state):
        box,width,draw=ICON_SHAPES[self.name]
        painter.save();painter.setRenderHint(G.QPainter.Antialiasing)
        if mode==G.QIcon.Disabled:painter.setOpacity(.45)
        painter.translate(rect.topLeft());painter.scale(rect.width()/box,rect.height()/box)
        painter.setPen(G.QPen(G.QColor(ICON_COLOR),width,C.Qt.SolidLine,C.Qt.RoundCap,C.Qt.RoundJoin));painter.setBrush(C.Qt.NoBrush)
        draw(painter);painter.restore()
    def pixmap(self,size,mode,state):
        image=G.QPixmap(size);image.fill(C.Qt.transparent)
        painter=G.QPainter(image);self.paint(painter,C.QRect(C.QPoint(0,0),size),mode,state);painter.end()
        return image


def icon(name):
    if name not in ICON_SHAPES:return glyph(name,ICON_COLOR,20)
    return G.QIcon(VectorIconEngine(name))


class AgendaDelegate(W.QStyledItemDelegate):
    def sizeHint(self, option, index):return C.QSize(250,57)
    def paint(self,painter,option,index):
        item=index.data(C.Qt.UserRole) or {}
        painter.save();painter.setRenderHint(G.QPainter.Antialiasing)
        rect=option.rect.adjusted(4,2,-4,-2)
        if not item:
            painter.setPen(G.QColor('#aebfcd'));painter.drawText(rect,C.Qt.AlignCenter,index.data() or '');painter.restore();return
        if option.state & (W.QStyle.State_Selected|W.QStyle.State_MouseOver):
            painter.setPen(C.Qt.NoPen);painter.setBrush(G.QColor(126,210,194,35));painter.drawRoundedRect(rect,11,11)
        done=bool(item.get('completed'));painter.setPen(C.Qt.NoPen)
        painter.setBrush(G.QColor('#718a94' if done else '#e5bb7e' if item.get('workTaskId') else '#9cdbcc'))
        painter.drawEllipse(C.QPointF(rect.left()+10,rect.top()+17),2.5,2.5)
        font=option.font;font.setPixelSize(12);font.setWeight(G.QFont.Medium);font.setStrikeOut(done);painter.setFont(font)
        painter.setPen(G.QColor('#93a6b5' if done else '#edf4fa'))
        line=C.QRect(rect.left()+23,rect.top()+4,rect.width()-34,23)
        title=' '.join(item.get('title','').splitlines())
        painter.drawText(line,C.Qt.AlignVCenter,painter.fontMetrics().elidedText(title,C.Qt.ElideRight,line.width()))
        font.setPixelSize(10);font.setWeight(G.QFont.Normal);font.setStrikeOut(False);painter.setFont(font);painter.setPen(G.QColor('#aebfcd'))
        detail=item['date']+'  '+item.get('time','')+' · '+item.get('state','')
        painter.drawText(line.translated(0,22),C.Qt.AlignVCenter,painter.fontMetrics().elidedText(detail,C.Qt.ElideRight,line.width()))
        painter.restore()


def report_worker_trouble(kind):
    """Stuck/crashing material helper counts towards the session's automatic
    switch to the lite tier (two within 10 minutes). Real desktops only."""
    if G.QGuiApplication.platformName() == 'xcb':
        effects_policy.report_failure('calendar-material-worker', kind)


class Material:
    def init_material(self):
        self.setAttribute(C.Qt.WA_TranslucentBackground)
        self.owner=self;self.radius=popup_style.RADIUS;self.native_glass=None;self.backdrop=G.QImage();self.surface=G.QImage()
        self.material_builds=0;self.material_ready=False
        self.fade_from=G.QImage();self.fade=C.QVariantAnimation(self);self.fade.setStartValue(0.0);self.fade.setEndValue(1.0)
        self.fade.valueChanged.connect(lambda _value:self.update());self.fade.finished.connect(self.end_fade)

    def end_fade(self):
        self.fade_from=G.QImage();self.update()

    def surface_cache(self,size,dpr):
        """Last finished glass of this size, kept for this login only. A fresh
        calendar process starts from it instead of a flat placeholder."""
        base=C.QStandardPaths.writableLocation(C.QStandardPaths.RuntimeLocation)
        if not base:return None
        return Path(base)/'kylin-lqui'/('calendar-surface-%dx%d@%d.png'%(size.width(),size.height(),round(dpr*100)))

    @staticmethod
    def backdrop_signature(captured):
        """8x8 brightness fingerprint of what is behind the calendar."""
        small=captured.toImage().scaled(8,8,C.Qt.IgnoreAspectRatio,C.Qt.SmoothTransformation).convertToFormat(G.QImage.Format_Grayscale8)
        return ''.join('%02x'%small.pixelColor(x,y).red() for y in range(8) for x in range(8))

    @staticmethod
    def signature_close(a,b,limit=14):
        if len(a)!=128 or len(b)!=128:return False
        diffs=[abs(int(a[i:i+2],16)-int(b[i:i+2],16)) for i in range(0,128,2)]
        return sum(diffs)/len(diffs)<=limit

    # Brightness the GPU glass gives a backdrop of a given brightness, measured
    # on the FTG340 (dark desktop 8->4, mid grey 128->64, white page 250->81).
    GLASS_CURVE=((0,2),(8,4),(128,64),(250,81),(255,82))

    @classmethod
    def glass_brightness(cls,lum):
        points=cls.GLASS_CURVE
        for (x0,y0),(x1,y1) in zip(points,points[1:]):
            if lum<=x1:return y0+(y1-y0)*(lum-x0)/max(x1-x0,1)
        return points[-1][1]

    def stand_in(self,captured,expected,dpr):
        """Instant glass-like surface from the current backdrop (CPU blur +
        brightness curve of the real glass), so the first frame already has the
        right brightness and the real glass fades in without a colour jump."""
        small=captured.toImage().scaled(max(1,expected.width()//12),max(1,expected.height()//12),C.Qt.IgnoreAspectRatio,C.Qt.SmoothTransformation).convertToFormat(G.QImage.Format_RGB32)
        for y in range(small.height()):
            for x in range(small.width()):
                c=small.pixelColor(x,y);lum=.2126*c.red()+.7152*c.green()+.0722*c.blue()
                scale=self.glass_brightness(lum)/max(lum,1.0)
                small.setPixelColor(x,y,G.QColor(min(255,round(c.red()*scale)),min(255,round(c.green()*scale)),min(255,round(c.blue()*scale))))
        image=small.scaled(expected,C.Qt.IgnoreAspectRatio,C.Qt.SmoothTransformation).convertToFormat(G.QImage.Format_ARGB32_Premultiplied)
        out=G.QImage(expected,G.QImage.Format_ARGB32_Premultiplied);out.fill(C.Qt.transparent);out.setDevicePixelRatio(dpr)
        path=G.QPainterPath();path.addRoundedRect(C.QRectF(self.rect()),self.radius,self.radius)
        painter=G.QPainter(out);painter.setRenderHint(G.QPainter.Antialiasing);painter.setClipPath(path)
        image.setDevicePixelRatio(dpr);painter.drawImage(C.QRectF(self.rect()),image)
        painter.end()
        return out

    def show_surface(self,image):
        """Crossfade a newly finished glass over the one on screen."""
        duration=round(180*effects_policy.motion_scale())
        if self.isVisible() and not self.surface.isNull() and duration>0:
            self.fade.stop();self.fade_from=self.surface;self.fade.setDuration(duration);self.surface=image;self.fade.start()
        else:
            self.fade.stop();self.fade_from=G.QImage();self.surface=image
        self.material_ready=True;self.update()

    def cancel_material(self):
        self.material_revision=getattr(self,'material_revision',0)+1
        worker=getattr(self,'material_worker',None)
        if worker is not None:worker.kill()

    def prepare_material(self):
        self.cancel_material();revision=self.material_revision;self.material_ready=False
        screen=W.QApplication.screenAt(self.geometry().center()) or W.QApplication.primaryScreen()
        area=screen.geometry();region=self.geometry().intersected(area);offset=region.topLeft()-area.topLeft()
        if not (effects_policy.live_capture() and effects_policy.glass_material()):
            self.surface=G.QImage();self.update();return  # lite/off tier: plain painted surface, no capture, no worker
        captured=screen.grabWindow(0,offset.x(),offset.y(),region.width(),region.height())
        self.material_builds+=1
        dpr=self.devicePixelRatioF();expected=C.QSize(round(self.width()*dpr),round(self.height()*dpr))
        # X11 rounds both ends of the screen region. At fractional device
        # ratios this can differ by one pixel from rounding the widget size.
        if (not captured.isNull() and captured.size()!=expected
                and abs(captured.width()-expected.width())<=1
                and abs(captured.height()-expected.height())<=1):
            captured=captured.scaled(expected, C.Qt.IgnoreAspectRatio, C.Qt.SmoothTransformation)
            captured.setDevicePixelRatio(dpr)
        signature=self.backdrop_signature(captured) if not captured.isNull() else ''
        # Start from the saved glass only when the backdrop looks the same as
        # when it was made; over a different backdrop (a white browser instead
        # of the dark desktop) it would visibly change colour a moment later.
        cache=self.surface_cache(expected,dpr);cached=G.QImage(str(cache)) if cache and cache.is_file() else G.QImage()
        if not cached.isNull() and cached.size()==expected and self.signature_close(cached.text('backdrop'),signature):
            cached=cached.convertToFormat(G.QImage.Format_ARGB32_Premultiplied);cached.setDevicePixelRatio(dpr);self.surface=cached
        elif signature:
            self.surface=self.stand_in(captured,expected,dpr)
        if self.surface.isNull() or self.surface.size()!=expected:
            self.surface=G.QImage(expected,G.QImage.Format_ARGB32_Premultiplied);self.surface.setDevicePixelRatio(dpr);self.surface.fill(C.Qt.transparent)
            painter=G.QPainter(self.surface);popup_style.paint_fallback(painter,self.rect(),self.radius);painter.end()
        if captured.isNull():return
        helper=HERE.parent/'lqui-panel-material-worker'
        if not helper.is_file():helper=HERE.parent/'build/lqui-panel-material-worker'
        import os
        helper=Path(os.environ.get('LQUI_PANEL_MATERIAL_WORKER',str(helper)))
        library=HERE/'liblqui-glass-bridge.so'
        if not library.is_file():library=HERE.parent/'build/liblqui-glass-bridge.so'
        if not helper.is_file() or not library.is_file():return
        options={'nativeBridge':True,'bridgeLibrary':str(library),'screen':list(area.getRect()),'region':list(region.getRect()),'radius':self.radius}
        packet=C.QByteArray();writer=C.QDataStream(packet,C.QIODevice.WriteOnly);writer.setVersion(C.QDataStream.Qt_5_12)
        writer.writeUInt32(0x4c514d31);writer << C.QByteArray(json.dumps(options).encode());writer.writeDouble(captured.devicePixelRatio());writer << captured.toImage()
        worker=C.QProcess(W.QApplication.instance());self.destroyed.connect(worker.kill);self.material_worker=worker;deadline=C.QTimer(worker);deadline.setSingleShot(True)
        def timed_out():
            worker.timed_out=True;report_worker_trouble('timeout');worker.kill()
        deadline.timeout.connect(timed_out)
        def complete(code=1,status=None):
            if getattr(worker,'settled',False):return
            worker.settled=True;deadline.stop()
            if self.material_worker is worker:self.material_worker=None
            # Cancelled (revision changed) or already counted as a timeout: not another failure.
            if code!=0 and revision==self.material_revision and not getattr(worker,'timed_out',False):
                report_worker_trouble('exit-%d' % code)
            if code==0 and revision==self.material_revision:
                data=worker.readAllStandardOutput()
                if len(data)<=32*1024*1024:
                    reader=C.QDataStream(data);reader.setVersion(C.QDataStream.Qt_5_12)
                    magic=reader.readUInt32();reader.readBool();image=G.QImage();reader >> image
                    if magic==0x4c514d31 and reader.status()==C.QDataStream.Ok and image.size()==expected:
                        image.setDevicePixelRatio(dpr);self.show_surface(image)
                        cache=self.surface_cache(expected,dpr)
                        if cache:
                            try:
                                cache.parent.mkdir(parents=True,exist_ok=True);saved=G.QImage(image);saved.setText('backdrop',signature);saved.save(str(cache),'PNG')
                            except OSError:pass
            worker.deleteLater()
        worker.finished.connect(complete)
        worker.errorOccurred.connect(lambda error:complete() if error==C.QProcess.FailedToStart else None)
        worker.started.connect(lambda:(worker.write(packet),worker.closeWriteChannel()))
        worker.start(str(helper),[]);deadline.start(2500)

    def paintEvent(self,event):
        painter=G.QPainter(self);painter.setCompositionMode(G.QPainter.CompositionMode_Source)
        painter.fillRect(self.rect(),C.Qt.transparent)
        if not self.surface.isNull():
            if not self.fade_from.isNull() and self.fade.state()==C.QAbstractAnimation.Running:
                progress=float(self.fade.currentValue() or 0.0)
                painter.drawImage(self.rect(),self.fade_from)
                painter.setCompositionMode(G.QPainter.CompositionMode_SourceOver);painter.setOpacity(progress)
            painter.drawImage(self.rect(),self.surface)
            painter.setOpacity(1.0);painter.setCompositionMode(G.QPainter.CompositionMode_SourceOver)
            popup_style.paint_edge(painter,self.rect(),self.radius)
        else:
            painter.setCompositionMode(G.QPainter.CompositionMode_SourceOver)
            popup_style.paint_fallback(painter,self.rect(),self.radius)
        painter.end()
