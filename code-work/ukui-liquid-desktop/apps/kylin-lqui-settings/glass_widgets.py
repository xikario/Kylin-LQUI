"""Cached backdrop cards: one capture per opening, no animated blur or timer."""
from PyQt5 import QtCore,QtGui,QtWidgets

def glyph(name,color='#e6edf7',size=22):
    pix=QtGui.QPixmap(size*2,size*2);pix.fill(QtCore.Qt.transparent);p=QtGui.QPainter(pix);p.setRenderHint(QtGui.QPainter.Antialiasing);p.scale(size/24*2,size/24*2);p.setPen(QtGui.QPen(QtGui.QColor(color),1.7,QtCore.Qt.SolidLine,QtCore.Qt.RoundCap,QtCore.Qt.RoundJoin));p.setBrush(QtCore.Qt.NoBrush)
    if name=='wifi':
        for radius in (10,6.8,3.6):p.drawArc(QtCore.QRectF(12-radius,20-radius,radius*2,radius*2),45*16,90*16)
        p.setBrush(QtGui.QColor(color));p.drawEllipse(QtCore.QPointF(12,19),.8,.8)
    elif name=='bluetooth':
        p.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(11,2),QtCore.QPointF(18,7),QtCore.QPointF(6,17)]));p.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(6,7),QtCore.QPointF(18,17),QtCore.QPointF(11,22),QtCore.QPointF(11,2)]))
    elif name in ('sound','muted'):
        p.drawPolygon(QtGui.QPolygonF([QtCore.QPointF(3,9),QtCore.QPointF(7,9),QtCore.QPointF(12,5),QtCore.QPointF(12,19),QtCore.QPointF(7,15),QtCore.QPointF(3,15)]))
        if name=='muted':p.drawLine(16,9,21,15);p.drawLine(21,9,16,15)
        else:p.drawArc(QtCore.QRectF(9,6,11,12),-60*16,120*16);p.drawArc(QtCore.QRectF(7,3,17,18),-55*16,110*16)
    elif name=='sun':
        p.drawEllipse(QtCore.QRectF(8,8,8,8))
        for i in range(8):p.save();p.translate(12,12);p.rotate(45*i);p.drawLine(0,-8,0,-10);p.restore()
    elif name=='power':p.drawArc(QtCore.QRectF(4,4,16,16),130*16,280*16);p.drawLine(12,2,12,11)
    elif name=='display':p.drawRoundedRect(QtCore.QRectF(3,4,18,13),2,2);p.drawLine(12,17,12,21);p.drawLine(7,21,17,21)
    elif name=='bell':
        path=QtGui.QPainterPath();path.moveTo(5,17);path.lineTo(6,14);path.lineTo(6,10);path.cubicTo(6,3,18,3,18,10);path.lineTo(18,14);path.lineTo(20,17);path.closeSubpath();p.drawPath(path);p.drawArc(QtCore.QRectF(9,17,6,5),180*16,180*16)
    elif name=='music':p.drawLine(10,5,10,18);p.drawLine(10,5,20,3);p.drawLine(20,3,20,16);p.drawEllipse(QtCore.QRectF(4,16,6,4));p.drawEllipse(QtCore.QRectF(14,14,6,4))
    elif name=='battery':p.drawRoundedRect(QtCore.QRectF(2,7,18,10),2,2);p.drawLine(22,10,22,14);p.setBrush(QtGui.QColor(color));p.drawRect(QtCore.QRectF(5,10,10,4))
    elif name=='calendar':p.drawRoundedRect(QtCore.QRectF(3,5,18,17),2,2);p.drawLine(3,10,21,10);p.drawLine(8,2,8,7);p.drawLine(16,2,16,7)
    elif name in ('next','previous'):
        if name=='previous':p.translate(24,0);p.scale(-1,1)
        p.setBrush(QtGui.QColor(color));p.drawPolygon(QtGui.QPolygonF([QtCore.QPointF(6,5),QtCore.QPointF(16,12),QtCore.QPointF(6,19)]));p.drawLine(18,5,18,19)
    elif name=='pause':p.drawLine(9,6,9,18);p.drawLine(15,6,15,18)
    elif name=='play':p.setBrush(QtGui.QColor(color));p.drawPolygon(QtGui.QPolygonF([QtCore.QPointF(8,5),QtCore.QPointF(18,12),QtCore.QPointF(8,19)]))
    elif name=='chevron':p.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(9,6),QtCore.QPointF(15,12),QtCore.QPointF(9,18)]))
    elif name=='close':p.drawLine(6,6,18,18);p.drawLine(18,6,6,18)
    p.end();pix.setDevicePixelRatio(2);result=QtGui.QIcon(pix);disabled=QtGui.QPixmap(pix.size());disabled.setDevicePixelRatio(2);disabled.fill(QtCore.Qt.transparent);faded=QtGui.QPainter(disabled);faded.setOpacity(.45);faded.drawPixmap(0,0,pix);faded.end();result.addPixmap(disabled,QtGui.QIcon.Disabled);return result

class GlassCard(QtWidgets.QFrame):
    def __init__(self,owner,radius=23):super().__init__();self.owner=owner;self.radius=radius;self.accent=False;self.setAttribute(QtCore.Qt.WA_TranslucentBackground);self.setProperty('glass',True)
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHints(QtGui.QPainter.Antialiasing|QtGui.QPainter.SmoothPixmapTransform);r=QtCore.QRectF(self.rect()).adjusted(.7,.7,-.7,-.7);clip=QtGui.QPainterPath();clip.addRoundedRect(r,self.radius,self.radius);p.setClipPath(clip)
        image=getattr(self.owner,'backdrop',QtGui.QImage())
        # Never expose live window text when capture is unavailable, a screen
        # moves, or the cached image does not cover the complete card.
        p.fillRect(self.rect(),QtGui.QColor('#485364'))
        if not image.isNull():p.drawImage(self.rect(),image,QtCore.QRect(self.mapToGlobal(QtCore.QPoint())-getattr(self.owner,'backdrop_origin',QtCore.QPoint()),self.size()))
        # A light application may sit behind the card. Keep secondary text
        # readable too; connectivity state is expressed by the orb and rim.
        p.fillRect(self.rect(),QtGui.QColor(16,23,32,185))
        gradient=QtGui.QLinearGradient(r.topLeft(),r.bottomRight());gradient.setColorAt(0,QtGui.QColor(225,239,255,34));gradient.setColorAt(.45,QtGui.QColor(140,180,230,5));gradient.setColorAt(1,QtGui.QColor(50,75,110,35));p.fillRect(r,gradient)
        # Refract only the empty edge band from the already diffused cache.
        # Text never overlaps this band and no live window content is exposed.
        if not image.isNull():
            inner=QtGui.QPainterPath();inner.addRoundedRect(r.adjusted(4,4,-4,-4),max(1,self.radius-4),max(1,self.radius-4))
            p.save();p.setClipPath(clip.subtracted(inner))
            origin=self.mapToGlobal(QtCore.QPoint())-getattr(self.owner,'backdrop_origin',QtCore.QPoint())
            source=QtCore.QRectF(QtCore.QPointF(origin),QtCore.QSizeF(self.size())).adjusted(7,7,-7,-7)
            p.drawImage(r,image,source);p.fillRect(r,QtGui.QColor(28,39,54,145));p.restore()
        # Thin curved rim catches light; center remains diffuse for readability.
        rim=QtGui.QLinearGradient(r.topLeft(),r.bottomRight());rim.setColorAt(0,QtGui.QColor(245,245,245,95));rim.setColorAt(.32,QtGui.QColor(180,216,245,12));rim.setColorAt(.72,QtGui.QColor(160,198,235,8));rim.setColorAt(1,QtGui.QColor(230,230,230,85))
        p.setBrush(QtCore.Qt.NoBrush);p.setPen(QtGui.QPen(QtGui.QBrush(rim),1.3));p.drawRoundedRect(r.adjusted(2,2,-2,-2),self.radius-2,self.radius-2)
        p.setClipping(False);p.setPen(QtGui.QPen(QtGui.QColor(230,230,230,66),1));p.setBrush(QtCore.Qt.NoBrush);p.drawRoundedRect(r,self.radius,self.radius)
        p.setPen(QtGui.QPen(QtGui.QColor(255,255,255,22),.7));p.drawRoundedRect(r.adjusted(1.5,1.5,-1.5,-1.5),self.radius-1,self.radius-1)
    def set_active(self,value):
        if self.accent!=value:self.accent=value;self.update()

def diffuse_backdrop(full, size):
    """Return an opaque logical-pixel cache, independent of capture DPR.

    QPixmap.scaled keeps its DPR. Passing its physical rect to a scene that
    uses logical coordinates leaves most of a HiDPI capture transparent.
    Pad the blur and flatten it onto a solid base to cover screen edges too.
    """
    source=full.toImage() if isinstance(full,QtGui.QPixmap) else full.copy()
    source.setDevicePixelRatio(1)
    small=source.scaled(max(1,size.width()//4),max(1,size.height()//4),QtCore.Qt.IgnoreAspectRatio,QtCore.Qt.SmoothTransformation)
    pad=32
    canvas=QtGui.QImage(small.width()+pad*2,small.height()+pad*2,QtGui.QImage.Format_RGB32);canvas.fill(QtGui.QColor('#485364'))
    painter=QtGui.QPainter(canvas)
    # Extend edge pixels so the blur never introduces a dark/transparent rim.
    xs=[(0,pad,0,1),(pad,small.width(),0,small.width()),(pad+small.width(),pad,small.width()-1,1)]
    ys=[(0,pad,0,1),(pad,small.height(),0,small.height()),(pad+small.height(),pad,small.height()-1,1)]
    for x,w,sx,sw in xs:
        for y,h,sy,sh in ys:painter.drawImage(QtCore.QRect(x,y,w,h),small,QtCore.QRect(sx,sy,sw,sh))
    painter.end()
    scene=QtWidgets.QGraphicsScene();item=scene.addPixmap(QtGui.QPixmap.fromImage(canvas));blur=QtWidgets.QGraphicsBlurEffect();blur.setBlurRadius(12);blur.setBlurHints(QtWidgets.QGraphicsBlurEffect.QualityHint);item.setGraphicsEffect(blur)
    image=QtGui.QImage(small.size(),QtGui.QImage.Format_RGB32);image.fill(QtGui.QColor('#485364'))
    painter=QtGui.QPainter(image);scene.render(painter,QtCore.QRectF(image.rect()),QtCore.QRectF(pad,pad,small.width(),small.height()));painter.end()
    return image.scaled(size,QtCore.Qt.IgnoreAspectRatio,QtCore.Qt.SmoothTransformation)

def blurred_capture(owner):
    screen=QtWidgets.QApplication.screenAt(owner.geometry().center()) or QtWidgets.QApplication.primaryScreen();full=screen.grabWindow(0)
    if full.isNull():return QtGui.QImage()
    area=screen.geometry();owner.backdrop_origin=area.topLeft()
    return diffuse_backdrop(full,area.size())


class CardChevron(QtWidgets.QAbstractButton):
    """A vector disclosure affordance with a larger, transparent hit target."""
    def __init__(self,title,parent=None):
        super().__init__(parent);self.setFixedSize(24,42);self.setCursor(QtCore.Qt.PointingHandCursor);self.setFocusPolicy(QtCore.Qt.StrongFocus);self.setMouseTracking(True);self.setToolTip(title);self.setAccessibleName(title)
    def enterEvent(self,event):self.update();super().enterEvent(event)
    def leaveEvent(self,event):self.update();super().leaveEvent(event)
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHint(QtGui.QPainter.Antialiasing)
        color=QtGui.QColor('#b7dcfa' if self.hasFocus() else '#f1f6fc' if self.underMouse() or self.isDown() else '#bfccd9')
        p.setPen(QtGui.QPen(color,1.7,QtCore.Qt.SolidLine,QtCore.Qt.RoundCap,QtCore.Qt.RoundJoin));p.setBrush(QtCore.Qt.NoBrush)
        center=QtCore.QPointF(self.width()/2+(1 if self.isDown() else 0),self.height()/2)
        p.drawPolyline(QtGui.QPolygonF([center+QtCore.QPointF(-2.5,-4.5),center+QtCore.QPointF(2,0),center+QtCore.QPointF(-2.5,4.5)]))
    def keyPressEvent(self,event):
        if event.key() in (QtCore.Qt.Key_Return,QtCore.Qt.Key_Enter):
            if not event.isAutoRepeat():self.click()
            event.accept()
        else:super().keyPressEvent(event)

class GlassStatusLabel(QtWidgets.QLabel):
    """Operation errors need the same opaque reading surface as the cards."""
    def __init__(self,owner):
        super().__init__();self.owner=owner;self.radius=14;self.accent=False
        self.setAttribute(QtCore.Qt.WA_TranslucentBackground);self.setMargin(8)
    def paintEvent(self,event):
        GlassCard.paintEvent(self,event)
        super().paintEvent(event)


class TooltipBubble(QtWidgets.QLabel):
    def __init__(self):
        super().__init__(None,QtCore.Qt.ToolTip|QtCore.Qt.FramelessWindowHint)
        self.setAttribute(QtCore.Qt.WA_TranslucentBackground);self.setAutoFillBackground(False)
        self.setObjectName('lquiTooltip');self.setTextFormat(QtCore.Qt.PlainText);self.setWordWrap(True);self.setMaximumWidth(310);self.setMargin(8)
        self.setStyleSheet('QLabel#lquiTooltip{background:transparent;color:#283039;border:0;font-size:12px;}')
    def paintEvent(self,event):
        painter=QtGui.QPainter(self);painter.setRenderHint(QtGui.QPainter.Antialiasing)
        painter.setCompositionMode(QtGui.QPainter.CompositionMode_Source);painter.fillRect(self.rect(),QtCore.Qt.transparent)
        painter.setCompositionMode(QtGui.QPainter.CompositionMode_SourceOver);painter.setPen(QtCore.Qt.NoPen);painter.setBrush(QtGui.QColor('#f0f1f3'))
        painter.drawRoundedRect(QtCore.QRectF(self.rect()).adjusted(1,1,-1,-1),10,10);painter.end();super().paintEvent(event)
    def resizeEvent(self,event):
        # Also clip the native window on platforms whose tooltip surface is opaque.
        path=QtGui.QPainterPath();path.addRoundedRect(QtCore.QRectF(self.rect()),10,10)
        self.setMask(QtGui.QRegion(path.toFillPolygon().toPolygon()));super().resizeEvent(event)

class ReadableTooltips(QtCore.QObject):
    """Own tooltip surface, insulated from the trigger's local button stylesheet."""
    def __init__(self,owner):
        super().__init__(owner);self.owner=owner
        self.bubble=TooltipBubble()
        self.timer=QtCore.QTimer(self);self.timer.setSingleShot(True);self.timer.timeout.connect(self.bubble.hide)
        owner.destroyed.connect(self.bubble.deleteLater);QtWidgets.QApplication.instance().installEventFilter(self)
    def eventFilter(self,obj,event):
        if event.type() in (QtCore.QEvent.Leave,QtCore.QEvent.MouseButtonPress,QtCore.QEvent.Hide):
            if obj is self.owner or isinstance(obj,QtWidgets.QWidget) and self.owner.isAncestorOf(obj):self.bubble.hide()
        if event.type()==QtCore.QEvent.ToolTip and isinstance(obj,QtWidgets.QWidget) and self.owner.isAncestorOf(obj) and obj.toolTip():
            self.bubble.setText(obj.toolTip());self.bubble.adjustSize()
            screen=QtWidgets.QApplication.screenAt(event.globalPos()) or QtWidgets.QApplication.primaryScreen();area=screen.availableGeometry();pos=event.globalPos()+QtCore.QPoint(10,16)
            self.bubble.move(max(area.left(),min(pos.x(),area.right()-self.bubble.width())),max(area.top(),min(pos.y(),area.bottom()-self.bubble.height())))
            self.bubble.show();self.timer.start(5000);event.accept();return True
        return False
