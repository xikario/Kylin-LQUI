"""Event-driven polish: no idle timer, screen capture, or additional blur pass."""
from PyQt5 import QtCore, QtGui, QtWidgets


class PressFeedback(QtCore.QObject):
    """Paint only the existing button perimeter; no child compositing layer."""
    def __init__(self, button, reduced_motion):
        super().__init__(button)
        self.button = button
        self.reduced_motion = reduced_motion
        self.motion = None
        self.amount = 0.
        self.paint_count = 0
        button.installEventFilter(self)
        button.pressed.connect(self.pressed)
        button.released.connect(self.released)

    def damage(self):
        outer = self.button.rect()
        inner = outer.adjusted(5, 5, -5, -5)
        return QtGui.QRegion(outer).subtracted(QtGui.QRegion(inner)) if inner.isValid() else QtGui.QRegion(outer)

    def animate(self, pressed):
        if self.motion is None:
            self.motion = QtCore.QTimeLine(130,self)
            self.motion.setUpdateInterval(33)  # a small rim needs at most 30 updates/s
            self.motion.valueChanged.connect(lambda value:self.frame(self.start_amount+(self.target_amount-self.start_amount)*float(value)))
        self.motion.stop()
        target = 1. if pressed else 0.
        if abs(target-self.amount) < .001:
            return
        self.motion.setDuration(90 if pressed else 130)
        self.motion.setEasingCurve(QtCore.QEasingCurve.OutCubic)
        blocker = QtCore.QSignalBlocker(self.motion)
        self.start_amount,self.target_amount=self.amount,target
        self.motion.setCurrentTime(0)
        del blocker
        self.motion.start()

    def frame(self, value):
        self.amount = float(value)
        self.button.update(self.damage())

    def pressed(self):
        if self.reduced_motion() or not self.button.isVisible() or not self.button.isEnabled():
            self.reset()
        else:
            self.animate(True)

    def released(self):
        if self.motion:
            if self.reduced_motion(): self.reset()
            else: self.animate(False)

    def reset(self):
        if self.motion: self.motion.stop()
        if self.amount:
            self.amount = 0.
            self.button.update(self.damage())

    def eventFilter(self, watched, event):
        if event.type() == QtCore.QEvent.Hide or (event.type() == QtCore.QEvent.EnabledChange and not self.button.isEnabled()):
            self.reset()
        elif event.type() == QtCore.QEvent.Paint and self.amount > .001:
            # The usual style paints native text, icon, check and focus state.
            # Only a tiny additional outline is drawn on that same surface.
            self.paint_count += 1
            p = QtGui.QPainter(self.button)
            p.setClipRegion(event.region())
            if isinstance(self.button, QtWidgets.QToolButton):
                option = QtWidgets.QStyleOptionToolButton();self.button.initStyleOption(option)
                self.button.style().drawComplexControl(QtWidgets.QStyle.CC_ToolButton, option, p, self.button)
            else:
                option = QtWidgets.QStyleOptionButton();self.button.initStyleOption(option)
                self.button.style().drawControl(QtWidgets.QStyle.CE_PushButton, option, p, self.button)
            p.setRenderHint(QtGui.QPainter.Antialiasing)
            inset = 1.2+self.amount
            area = QtCore.QRectF(self.button.rect()).adjusted(inset, inset, -inset, -inset)
            radius = min(area.width(), area.height())/2 if self.button.property('round') else min(10., area.height()/2)
            color = self.button.palette().color(QtGui.QPalette.ButtonText)
            glow = QtGui.QLinearGradient(area.topLeft(), area.bottomRight())
            for pos,alpha in ((0.,140),(.48,32),(1.,75)):
                ink=QtGui.QColor(color);ink.setAlpha(round(alpha*self.amount));glow.setColorAt(pos,ink)
            p.setPen(QtGui.QPen(QtGui.QBrush(glow),1.2));p.setBrush(QtCore.Qt.NoBrush)
            p.drawRoundedRect(area,radius,radius)
            p.end()
            return True
        return False


def attach_button(button, reduced_motion):
    if button.property('flatText') or hasattr(button, '_lqui_press_feedback'):
        return getattr(button, '_lqui_press_feedback', None)
    button._lqui_press_feedback = PressFeedback(button, reduced_motion)
    return button._lqui_press_feedback


class _FadeStrip(QtWidgets.QWidget):
    def __init__(self, viewport, card, bottom):
        super().__init__(viewport)
        self.setObjectName('lquiScrollFadeBottom' if bottom else 'lquiScrollFadeTop')
        self.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents)
        self.setAutoFillBackground(False)
        self.card = card
        self.bottom = bottom
        self.image = QtGui.QImage()
        self.key = None
        self.paint_count = self.cache_builds = 0
        self.hide()

    def clear(self):
        self.image = QtGui.QImage()
        self.key = None

    def hideEvent(self, event):
        self.clear()
        super().hideEvent(event)

    def paintEvent(self, event):
        self.paint_count += 1
        surface_key = self.card.background_key()
        if surface_key is None:
            self.clear()
            return
        origin = self.mapTo(self.card, QtCore.QPoint())
        dpr = self.devicePixelRatioF()
        key = (surface_key, origin.x(), origin.y(), self.width(), self.height(), dpr)
        if key != self.key:
            image = QtGui.QImage(max(1, round(self.width()*dpr)), max(1, round(self.height()*dpr)), QtGui.QImage.Format_ARGB32_Premultiplied)
            image.setDevicePixelRatio(dpr)
            image.fill(QtCore.Qt.transparent)
            painter = QtGui.QPainter(image)
            painter.translate(-origin)
            ready = self.card.paint_background(painter, cached_only=True)
            painter.end()
            if not ready:
                self.clear()
                return
            painter = QtGui.QPainter(image)
            painter.setCompositionMode(QtGui.QPainter.CompositionMode_DestinationIn)
            mask = QtGui.QLinearGradient(0, 0, 0, self.height())
            mask.setColorAt(0, QtCore.Qt.transparent if self.bottom else QtCore.Qt.white)
            mask.setColorAt(1, QtCore.Qt.white if self.bottom else QtCore.Qt.transparent)
            painter.fillRect(self.rect(), mask)
            painter.end()
            self.image, self.key = image, key
            self.cache_builds += 1
        painter = QtGui.QPainter(self)
        painter.drawImage(QtCore.QPoint(), self.image)


class ScrollEdgeFade(QtCore.QObject):
    """Two 12px cached strips; scroll range/value signals replace polling."""
    def __init__(self, area, card):
        super().__init__(area)
        self.area = area
        self.viewport = area.viewport()
        self.top = _FadeStrip(area, card, False)
        self.bottom = _FadeStrip(area, card, True)
        self.viewport.installEventFilter(self)
        self.bar = area.verticalScrollBar()
        self.bar.valueChanged.connect(self.sync)
        self.bar.rangeChanged.connect(self.sync)
        self.sync()

    def sync(self, *unused):
        height = min(12, self.viewport.height()//4)
        overflow = self.bar.maximum() > self.bar.minimum() and height > 0
        states = ((self.top, 0, overflow and self.bar.value() > self.bar.minimum()),
                  (self.bottom, self.viewport.height()-height, overflow and self.bar.value() < self.bar.maximum()))
        for strip, y, shown in states:
            rect = QtCore.QRect(self.viewport.x(), self.viewport.y()+y, self.viewport.width(), height)
            if strip.geometry() != rect:
                strip.setGeometry(rect)
            if strip.isHidden() == shown:
                strip.setVisible(shown)
                if shown: strip.raise_()

    def eventFilter(self, watched, event):
        if event.type() in (QtCore.QEvent.Resize, QtCore.QEvent.Show, QtCore.QEvent.LayoutRequest):
            self.sync()
        elif event.type() == QtCore.QEvent.Hide:
            self.top.clear(); self.bottom.clear()
        return False


def attach_scroll(area):
    if hasattr(area, '_lqui_scroll_fade'):
        return area._lqui_scroll_fade
    card = area.parentWidget()
    while card is not None and not callable(getattr(card, 'background_key', None)):
        card = card.parentWidget()
    if card is None:
        return None  # No matching cached material: leave the view unchanged.
    area._lqui_scroll_fade = ScrollEdgeFade(area, card)
    return area._lqui_scroll_fade


def install_effects(container, reduced_motion):
    for button in container.findChildren((QtWidgets.QPushButton, QtWidgets.QToolButton)):
        attach_button(button, reduced_motion)
    for area in container.findChildren((QtWidgets.QScrollArea, QtWidgets.QTextBrowser)):
        attach_scroll(area)
