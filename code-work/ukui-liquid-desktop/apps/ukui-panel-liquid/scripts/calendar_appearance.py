"""Calendar presentation using the control center's actual cached glass renderer."""
from pathlib import Path
import sys,json
from PyQt5 import QtCore as C, QtGui as G, QtWidgets as W

HERE = Path(__file__).resolve().parent
if not (HERE/'glass_widgets.py').exists():
    sys.path.insert(0, str(HERE.parent.parent/'kylin-lqui-settings'))
from glass_widgets import GlassCard, diffuse_backdrop, glyph

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


class Material:
    def init_material(self):
        self.setAttribute(C.Qt.WA_TranslucentBackground)
        self.owner=self;self.radius=28;self.native_glass=None;self.backdrop=G.QImage();self.surface=G.QImage()
        self.material_builds=0;self.material_ready=False

    def cancel_material(self):
        self.material_revision=getattr(self,'material_revision',0)+1
        worker=getattr(self,'material_worker',None)
        if worker is not None:worker.kill()

    def prepare_material(self):
        self.cancel_material();revision=self.material_revision;self.material_ready=False
        screen=W.QApplication.screenAt(self.geometry().center()) or W.QApplication.primaryScreen()
        area=screen.geometry();region=self.geometry().intersected(area);offset=region.topLeft()-area.topLeft()
        captured=screen.grabWindow(0,offset.x(),offset.y(),region.width(),region.height())
        self.material_builds+=1
        dpr=self.devicePixelRatioF();expected=C.QSize(round(self.width()*dpr),round(self.height()*dpr))
        if self.surface.isNull() or self.surface.size()!=expected:
            self.surface=G.QImage(expected,G.QImage.Format_ARGB32_Premultiplied);self.surface.setDevicePixelRatio(dpr);self.surface.fill(C.Qt.transparent)
            painter=G.QPainter(self.surface);painter.setRenderHint(G.QPainter.Antialiasing);painter.setBrush(G.QColor('#16232e'));painter.setPen(G.QColor('#536573'))
            painter.drawRoundedRect(C.QRectF(self.rect()).adjusted(.5,.5,-.5,-.5),28,28);painter.end()
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
        deadline.timeout.connect(worker.kill)
        def complete(code=1,status=None):
            if getattr(worker,'settled',False):return
            worker.settled=True;deadline.stop()
            if self.material_worker is worker:self.material_worker=None
            if code==0 and revision==self.material_revision:
                data=worker.readAllStandardOutput()
                if len(data)<=32*1024*1024:
                    reader=C.QDataStream(data);reader.setVersion(C.QDataStream.Qt_5_12)
                    magic=reader.readUInt32();reader.readBool();image=G.QImage();reader >> image
                    if magic==0x4c514d31 and reader.status()==C.QDataStream.Ok and image.size()==expected:
                        image.setDevicePixelRatio(dpr);self.surface=image;self.material_ready=True;self.update()
            worker.deleteLater()
        worker.finished.connect(complete)
        worker.errorOccurred.connect(lambda error:complete() if error==C.QProcess.FailedToStart else None)
        worker.started.connect(lambda:(worker.write(packet),worker.closeWriteChannel()))
        worker.start(str(helper),[]);deadline.start(2500)

    def paintEvent(self,event):
        painter=G.QPainter(self);painter.setCompositionMode(G.QPainter.CompositionMode_Source)
        painter.fillRect(self.rect(),C.Qt.transparent)
        if not self.surface.isNull():painter.drawImage(self.rect(),self.surface)
        else:
            painter.setRenderHint(G.QPainter.Antialiasing);painter.setBrush(G.QColor('#16232e'));painter.setPen(G.QColor('#536573'))
            painter.drawRoundedRect(C.QRectF(self.rect()).adjusted(.5,.5,-.5,-.5),28,28)
        painter.end()
