"""Native compact settings rows. This module never reads/writes module settings."""
import base64
from PyQt5 import QtCore, QtGui, QtWidgets
from design import FlowLayout
from ui_tokens import COLORS, PREVIEW_BYTES, PREVIEW_PIXELS


class WrappingLabel(QtWidgets.QLabel):
    def minimumSizeHint(self):
        size=super().minimumSizeHint();size.setWidth(0);return size


def label(text='', secondary=False):
    result = WrappingLabel(str(text));result.setTextFormat(QtCore.Qt.PlainText)
    result.setWordWrap(True);result.setMinimumWidth(0)
    result.setProperty('secondary', secondary)
    result.setSizePolicy(QtWidgets.QSizePolicy.Preferred, QtWidgets.QSizePolicy.Preferred)
    return result


class StatusLabel(QtWidgets.QLabel):
    def setText(self, text):
        super().setText(text);self.setVisible(bool(text))


class ActionButton(QtWidgets.QPushButton):
    """Native button semantics with bounded, wrapping captions and Enter."""
    def __init__(self,text=''):
        super().__init__(text);policy=self.sizePolicy();policy.setHeightForWidth(True);self.setSizePolicy(policy)
    def heightForWidth(self,width):
        bounds=self.fontMetrics().boundingRect(QtCore.QRect(0,0,max(1,width-20),100000),QtCore.Qt.TextWordWrap|QtCore.Qt.TextWrapAnywhere,self.text())
        return max(32,bounds.height()+10)
    def sizeHint(self):
        size=super().sizeHint();size.setWidth(min(280,size.width()));size.setHeight(self.heightForWidth(size.width()));return size
    def minimumSizeHint(self):return QtCore.QSize(min(80,self.sizeHint().width()),max(32,self.fontMetrics().height()+10))
    def keyPressEvent(self,event):
        if event.key() in (QtCore.Qt.Key_Return,QtCore.Qt.Key_Enter):
            if not event.isAutoRepeat():self.click()
            event.accept()
        else:super().keyPressEvent(event)
    def paintEvent(self,event):
        option=QtWidgets.QStyleOptionButton();self.initStyleOption(option);text=option.text;option.text=''
        painter=QtGui.QPainter(self);self.style().drawControl(QtWidgets.QStyle.CE_PushButton,option,painter,self)
        color=COLORS['disabledText'] if not self.isEnabled() else '#FFFFFF' if self.property('primary') else COLORS['error'] if self.property('destructive') else COLORS['selectedText'] if self.isChecked() else COLORS['text']
        painter.setPen(QtGui.QColor(color));painter.setFont(self.font());painter.drawText(self.rect().adjusted(10,4,-10,-4),QtCore.Qt.AlignCenter|QtCore.Qt.TextWordWrap|QtCore.Qt.TextWrapAnywhere,text)


class HelpBubble(QtWidgets.QLabel):
    def __init__(self):
        super().__init__(None,QtCore.Qt.ToolTip|QtCore.Qt.FramelessWindowHint|QtCore.Qt.NoDropShadowWindowHint)
        self.setAttribute(QtCore.Qt.WA_TranslucentBackground);self.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents)
        self.setTextFormat(QtCore.Qt.PlainText);self.setWordWrap(True);self.setMargin(8);self.setMaximumWidth(360)
        self.setStyleSheet('QLabel{background:transparent;border:0;color:#1D1D1F;}')
    def resizeEvent(self,event):
        path=QtGui.QPainterPath();path.addRoundedRect(QtCore.QRectF(self.rect()),6,6);self.setMask(QtGui.QRegion(path.toFillPolygon().toPolygon()));super().resizeEvent(event)
    def paintEvent(self,event):
        painter=QtGui.QPainter(self);painter.setRenderHint(QtGui.QPainter.Antialiasing);painter.setCompositionMode(QtGui.QPainter.CompositionMode_Source);painter.fillRect(self.rect(),QtCore.Qt.transparent)
        painter.setCompositionMode(QtGui.QPainter.CompositionMode_SourceOver);painter.setBrush(QtGui.QColor(COLORS['surface']));painter.setPen(QtGui.QColor(COLORS['border']));painter.drawRoundedRect(QtCore.QRectF(self.rect()).adjusted(.5,.5,-.5,-.5),6,6);painter.end();super().paintEvent(event)


class SettingSlider(QtWidgets.QSlider):
    committed=QtCore.pyqtSignal()
    def __init__(self):
        super().__init__(QtCore.Qt.Horizontal);self.pointer=False;self.setMinimumHeight(32)
        self.actionTriggered.connect(lambda action:self.committed.emit() if not self.pointer and not self.isSliderDown() else None)
        self.sliderReleased.connect(lambda:self.committed.emit() if not self.pointer else None)
    def mousePressEvent(self,event):
        if event.button()==QtCore.Qt.LeftButton and self.isEnabled():self.pointer=True
        super().mousePressEvent(event)
    def mouseReleaseEvent(self,event):
        commit=self.pointer and event.button()==QtCore.Qt.LeftButton and self.isEnabled()
        super().mouseReleaseEvent(event)
        if event.button()==QtCore.Qt.LeftButton:self.pointer=False
        if commit:self.committed.emit()
    def cancel_pointer(self):
        blocker=QtCore.QSignalBlocker(self);self.setSliderDown(False);self.pointer=False;del blocker
    def hideEvent(self,event):self.cancel_pointer();super().hideEvent(event)
    def changeEvent(self,event):
        if event.type()==QtCore.QEvent.EnabledChange and not self.isEnabled():self.cancel_pointer()
        super().changeEvent(event)


class GroupSurface(QtWidgets.QFrame):
    def paintEvent(self, event):
        super().paintEvent(event)
        group = self.parentWidget()
        painter = QtGui.QPainter(self);painter.setPen(QtGui.QColor(COLORS['divider']))
        positions = sorted({row.y() for row in group.rows if not row.isHidden()})
        for y in positions[1:]:painter.drawLine(12, y, self.width()-12, y)
        if group.columns == 2:painter.drawLine(self.width()//2, 8, self.width()//2, self.height()-8)


class SettingsGroup(QtWidgets.QWidget):
    def __init__(self, title='', columns=1):
        super().__init__();self.rows=[];self.columns=1;self.requested_columns=columns;self._layout_signature=None
        self.box=QtWidgets.QVBoxLayout(self);self.box.setContentsMargins(0,0,0,0);self.box.setSpacing(8)
        self.title=label(title);self.title.setProperty('section',True);self.title.setVisible(bool(title));self.box.addWidget(self.title)
        self.surface=GroupSurface(self);self.surface.setProperty('card',True);self.grid=QtWidgets.QGridLayout(self.surface)
        self.grid.setContentsMargins(0,0,0,0);self.grid.setSpacing(0);self.box.addWidget(self.surface)
    def add_row(self, row):
        self.rows.append(row);self.grid.addWidget(row,len(self.rows)-1,0)
    def refresh_rows(self, allow_columns=False, available=None):
        visible=[r for r in self.rows if not r.isHidden()]
        # Default 1040px window stays single-column. Wider canvases may use
        # two columns only when captions still fit at the actual font size.
        width=min(self.width(),available) if available is not None else self.width()
        columns=2 if allow_columns and self.requested_columns==2 and width>=820 else 1
        if columns==2:
            available=self.width()//2-90
            if any(getattr(r,'title',None) and r.title.fontMetrics().horizontalAdvance(r.title.text())>available*1.6 for r in visible):columns=1
        signature=(columns,tuple(visible))
        if signature==self._layout_signature:return
        self._layout_signature=signature;self.columns=columns
        while self.grid.count():self.grid.takeAt(0)
        for i,row in enumerate(visible):self.grid.addWidget(row,i//columns,i%columns)
        self.grid.setColumnStretch(0,1);self.grid.setColumnStretch(1,1 if columns==2 else 0)
        self.surface.update();self.updateGeometry()
    def set_unframed(self, value=True):
        self.surface.setProperty('card',not value);self.surface.style().unpolish(self.surface);self.surface.style().polish(self.surface)


class SettingRow(QtWidgets.QWidget):
    def __init__(self, title, control, help_text='', boolean=False):
        super().__init__();self.setProperty('settingRow',True);self.control=control;self.boolean=boolean
        self.setMinimumWidth(0);self.setMinimumHeight(56 if help_text else 40)
        box=QtWidgets.QVBoxLayout(self);box.setContentsMargins(12,8,12,8);box.setSpacing(4)
        self.line=QtWidgets.QBoxLayout(QtWidgets.QBoxLayout.LeftToRight);self.line.setSpacing(12);box.addLayout(self.line)
        self.title=label(title);self.words=self.title;self.help=label(help_text,True);self.help.setVisible(bool(help_text))
        self.line.addWidget(self.words,1 if boolean else 0);self.line.addWidget(control,0 if boolean else 1)
        box.addWidget(self.help)
        if boolean:self.title.installEventFilter(self)
    def eventFilter(self, obj, event):
        if obj is self.title and event.type()==QtCore.QEvent.MouseButtonRelease and event.button()==QtCore.Qt.LeftButton:
            if self.control.isEnabled():self.control.click()
            return True
        return super().eventFilter(obj,event)
    def reflow(self, available):
        stacked=available<560 and not self.boolean
        self.line.setDirection(QtWidgets.QBoxLayout.TopToBottom if stacked else QtWidgets.QBoxLayout.LeftToRight)
        self.words.setMinimumWidth(0);self.words.setMaximumWidth(16777215)
        if not stacked and not self.boolean:self.words.setFixedWidth(160)
        self.line.setStretch(0,1 if self.boolean else 0);self.line.setStretch(1,0 if self.boolean else 1)
    def set_help(self, text):
        self.help.setText(text);self.help.setVisible(bool(text));self.setMinimumHeight(56 if text else 40)


class ActionRow(QtWidgets.QWidget):
    def __init__(self):
        super().__init__();self.flow=FlowLayout(self);self.flow.setContentsMargins(12,8,12,8)
    def add_action(self, button):self.flow.addWidget(button)


class DisclosureButton(QtWidgets.QToolButton):
    def keyPressEvent(self, event):
        if event.key() in (QtCore.Qt.Key_Return,QtCore.Qt.Key_Enter):
            self.click();event.accept()
        else:super().keyPressEvent(event)


class DisclosureRow(QtWidgets.QWidget):
    changed=QtCore.pyqtSignal(bool)
    def __init__(self, title, content):
        super().__init__();self.caption=title;self.content=content;self.user_expanded=False;self._search_previous=None
        box=QtWidgets.QVBoxLayout(self);box.setContentsMargins(12,8,12,8);box.setSpacing(8)
        self.button=DisclosureButton();self.button.setCheckable(True);self.button.setToolButtonStyle(QtCore.Qt.ToolButtonTextBesideIcon)
        self.button.setText(title);self.button.setStyleSheet('QToolButton{border:0;background:transparent;text-align:left;padding:0;}');box.addWidget(self.button)
        box.addWidget(content);self.button.toggled.connect(self.set_expanded);self.set_expanded(False)
    def set_expanded(self, expanded):
        blocker=QtCore.QSignalBlocker(self.button);self.button.setChecked(expanded);del blocker
        self.button.setArrowType(QtCore.Qt.DownArrow if expanded else QtCore.Qt.RightArrow)
        self.button.setAccessibleName(self.caption+('，已展开' if expanded else '，已折叠'))
        self.button.setAccessibleDescription('按 Enter 或空格展开、折叠补充说明')
        self.content.setVisible(expanded)
        if self._search_previous is None:self.user_expanded=expanded
        self.changed.emit(expanded);self.updateGeometry()
    def search_expansion(self, matched, searching):
        if searching and matched:
            if self._search_previous is None:self._search_previous=self.user_expanded
            self.set_expanded(True)
        elif not searching and self._search_previous is not None:
            before=self._search_previous;self._search_previous=None;self.set_expanded(before)


class PreviewCanvas(QtWidgets.QWidget):
    def __init__(self):
        super().__init__();self.image=QtGui.QImage();self.setMinimumWidth(0);self.setFixedHeight(160)
    def paintEvent(self,event):
        painter=QtGui.QPainter(self);painter.setRenderHint(QtGui.QPainter.Antialiasing)
        painter.setPen(QtGui.QColor(COLORS['border']));painter.setBrush(QtGui.QColor('#F0F3F8'));painter.drawRoundedRect(QtCore.QRectF(self.rect()).adjusted(.5,.5,-.5,-.5),6,6)
        if self.image.isNull():painter.setPen(QtGui.QColor(COLORS['secondary']));painter.drawText(self.rect(),QtCore.Qt.AlignCenter,'暂无预览')
        else:
            size=self.image.size();size.scale(self.size()-QtCore.QSize(8,8),QtCore.Qt.KeepAspectRatio)
            rect=QtCore.QRect(QtCore.QPoint((self.width()-size.width())//2,(self.height()-size.height())//2),size)
            painter.setRenderHint(QtGui.QPainter.SmoothPixmapTransform);painter.drawImage(rect,self.image)


class WallpaperPreview(QtWidgets.QWidget):
    def __init__(self,data='',status='',alt=''):
        super().__init__();self._data=None;self.decode_count=0;self._error=''
        box=QtWidgets.QVBoxLayout(self);box.setContentsMargins(0,0,0,0);box.setSpacing(4)
        self.canvas=PreviewCanvas();self.status=label('',True);box.addWidget(self.canvas);box.addWidget(self.status)
        self.setAccessibleName(alt or '壁纸预览');self.set_data(data,status)
    def set_data(self,data,status=''):
        error=''
        if data!=self._data:
            self._data=data;self.canvas.image=QtGui.QImage()
            if data:
                try:
                    if not isinstance(data,str) or len(data)>((PREVIEW_BYTES+2)//3)*4:raise ValueError('预览超过大小限制')
                    raw=base64.b64decode(data,validate=True)
                    if len(raw)>PREVIEW_BYTES:raise ValueError('预览超过大小限制')
                    buffer=QtCore.QBuffer();buffer.setData(raw);buffer.open(QtCore.QIODevice.ReadOnly)
                    reader=QtGui.QImageReader(buffer);size=reader.size()
                    if bytes(reader.format()).lower()!=b'png' or not size.isValid() or size.width()*size.height()>PREVIEW_PIXELS or size.width()>1360 or size.height()>800:raise ValueError('预览格式或像素尺寸无效')
                    image=reader.read()
                    if image.isNull():raise ValueError('无法读取预览')
                    self.canvas.image=image;self.decode_count+=1
                except (ValueError,TypeError):error='预览不可用：图片损坏或超过限制。'
            self.canvas.update()
            self._error=error
        self.status.setText(self._error or status or ('真实静态预览' if not self.canvas.image.isNull() else '尚未提供预览'))
        self.status.setVisible(bool(self.status.text()))


class PathView(QtWidgets.QWidget):
    def __init__(self,value=''):
        super().__init__();row=QtWidgets.QHBoxLayout(self);row.setContentsMargins(0,0,0,0);row.setSpacing(4)
        self.path=QtWidgets.QLineEdit();self.path.setReadOnly(True);self.path.setMinimumWidth(0)
        self.full_path='';self.copy=DisclosureButton();self.copy.setText('复制路径');self.copy.setAccessibleName('复制完整路径');self.copy.clicked.connect(lambda:QtWidgets.QApplication.clipboard().setText(self.full_path))
        row.addWidget(self.path,1);row.addWidget(self.copy);self.set_value(value)
    def set_value(self,value):
        self.full_path=str(value);self.path.setToolTip(self.full_path);self.path.setProperty('helpText',self.full_path);self.path.setAccessibleName(self.full_path);self.copy.setEnabled(self.full_path.startswith('/'));self.elide()
    def elide(self):self.path.setText(self.path.fontMetrics().elidedText(self.full_path,QtCore.Qt.ElideMiddle,max(1,self.path.width()-20)))
    def resizeEvent(self,event):super().resizeEvent(event);QtCore.QTimer.singleShot(0,self.elide)


class WallpaperFileRow(QtWidgets.QWidget):
    def __init__(self,preview,files):
        super().__init__();self.preview=preview;self.files=files;self.line=QtWidgets.QBoxLayout(QtWidgets.QBoxLayout.LeftToRight,self)
        self.line.setContentsMargins(12,8,12,8);self.line.setSpacing(12);self.line.addWidget(preview);self.line.addWidget(files,1)
    def reflow(self,available):
        self.line.setDirection(QtWidgets.QBoxLayout.LeftToRight if available>=640 else QtWidgets.QBoxLayout.TopToBottom)
        self.preview.setFixedWidth(min(260,max(120,available-24)))


class PluginRow(QtWidgets.QWidget):
    def __init__(self,title,status,details,control=None,notice=''):
        super().__init__();self.caption=title;self.user_expanded=False;self._search_previous=None
        box=QtWidgets.QVBoxLayout(self);box.setContentsMargins(12,8,12,8);box.setSpacing(8)
        head=QtWidgets.QHBoxLayout();head.setSpacing(8);self.name=label(title);head.addWidget(self.name,1)
        self.button=DisclosureButton();self.button.setCheckable(True);self.button.setFixedSize(32,32)
        self.button.setStyleSheet('QToolButton{border:0;background:transparent;padding:0;}')
        self.state=label(status,True);head.addWidget(self.state)
        head.addWidget(self.button)
        if control is not None:head.addWidget(control)
        box.addLayout(head);self.notice=label(notice,True);self.notice.setVisible(bool(notice));box.addWidget(self.notice)
        self.content=details;box.addWidget(details);self.button.toggled.connect(self.set_expanded);self.set_expanded(False);self.setMinimumHeight(52)
    def set_expanded(self,expanded):
        blocker=QtCore.QSignalBlocker(self.button);self.button.setChecked(expanded);del blocker
        self.button.setArrowType(QtCore.Qt.DownArrow if expanded else QtCore.Qt.RightArrow);self.content.setVisible(expanded)
        self.button.setAccessibleName(self.caption+('，已展开' if expanded else '，已折叠'))
        if self._search_previous is None:self.user_expanded=expanded
        self.updateGeometry()
    search_expansion=DisclosureRow.search_expansion
