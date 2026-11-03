"""Light native settings vocabulary, inspired by the NextKde reference layout."""
import re
from PyQt5 import QtCore,QtGui,QtWidgets

COLORS={'overview':'#5e70dd','fences':'#aa5ae3','panel':'#328bf0','launcher':'#fa9b37','ai':'#8067dc','input':'#45b8d1','space':'#32ad9a','peony':'#4d9adc','integration':'#42bc78'}
DESCRIPTIONS={'panel:material':'挑选材质，让任务栏融入桌面。','panel:layout':'调整图标、托盘与任务栏布局。','panel:effects':'设置窗口的运动方式。','fences:appearance':'透明、光感与可读性，找到适合你的平衡。','ai:general':'模型、截图与对话，都在这里。','input:general':'字体、候选词与输入体验。','space:general':'管理本地知识库与检索偏好。','peony:general':'让文件管理更顺手。'}
STYLE='''
QMainWindow,QWidget#body{background:transparent;color:#20232b;}
QWidget{font-family:"Noto Sans CJK SC","Noto Sans";font-size:13px;}
QFrame#sidebar{background:#f1f2f5;border:0;border-top-left-radius:12px;border-bottom-left-radius:12px;}
QLabel{background:transparent;color:#232833;}
QLabel#integratedBrand{font-size:16px;font-weight:500;}
QToolButton[windowControl="true"]{background:transparent;border:0;border-radius:5px;padding:0;}
QToolButton[windowControl="true"]:hover{background:#e1e4e9;}
QLabel#brand{font-size:27px;font-weight:700;}
QLabel#title{font-size:27px;font-weight:700;}
QLabel#subtitle,QLabel[note="true"]{color:#7a808a;}
QLabel[section="true"]{font-size:12px;font-weight:600;color:#7c818b;padding:4px 0;}
QListWidget#navigation{background:#fff;border:0;border-radius:18px;outline:0;padding:6px;}
QListWidget#navigation::item{border:0;}
QLineEdit#search{background:#e3e5eb;border:0;border-radius:15px;padding:8px 11px;}
QFrame[card="true"]{background:#fff;border:1px solid #dce0e6;border-radius:19px;}
QLineEdit,QComboBox,QPlainTextEdit,QListWidget{background:#f5f6f9;color:#262b34;border:1px solid #e1e5ec;border-radius:8px;padding:6px;selection-background-color:#d3e6ff;selection-color:#214e89;}
QLineEdit:focus,QComboBox:focus,QPlainTextEdit:focus{border-color:#7bb2fa;}
QComboBox{min-height:21px;padding-right:25px;}
QComboBox::drop-down{border:0;width:24px;}
QComboBox QAbstractItemView{background:#fff;color:#262b34;selection-background-color:#e0ecff;border:0;padding:5px;}
QPushButton{background:#f2f4f8;color:#416086;border:1px solid #dce3ed;border-radius:10px;padding:7px 12px;}
QPushButton:hover{background:#e7effb;border-color:#b4ccec;}
QPushButton:pressed,QPushButton:checked{background:#c6dcf8;border-color:#7eaddf;padding:8px 11px 6px 13px;}
QPushButton:disabled{color:#a1a7b0;border-color:#e5e8ee;background:#f6f7fa;}
QPushButton[primary="true"]{background:#348ff6;color:#fff;border:1px solid #348ff6;}
QPushButton[primary="true"]:hover{background:#2682e9;border-color:#2682e9;}
QPushButton[primary="true"]:pressed{background:#1763be;border-color:#125299;padding:8px 11px 6px 13px;}
QPushButton[primary="true"]:disabled{background:#b3cbe6;color:#f8fbff;border-color:#b3cbe6;}
QSlider::groove:horizontal{height:5px;background:#d8dce3;border-radius:2px;}
QSlider::sub-page:horizontal{background:#63a8ff;border-radius:2px;}
QSlider::handle:horizontal{width:19px;margin:-5px 0;background:#eaf2fc;border:1px solid #a7bbd3;border-radius:7px;}
QScrollArea{background:transparent;border:0;}
QScrollBar:vertical{width:6px;background:transparent;margin:4px 0;}
QScrollBar::handle:vertical{background:#c2c7d0;min-height:35px;border-radius:3px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
QToolTip{color:#35455c;background:#f8faff;border:1px solid #c6d3e3;padding:7px;}
'''

def icon(module,size=24):
    pixmap=QtGui.QPixmap(size*2,size*2);pixmap.fill(QtCore.Qt.transparent)
    p=QtGui.QPainter(pixmap);p.setRenderHint(QtGui.QPainter.Antialiasing);p.scale(size/24*2,size/24*2)
    p.setPen(QtCore.Qt.NoPen);p.setBrush(QtGui.QColor(COLORS.get(module,'#64a1dc')));p.drawRoundedRect(QtCore.QRectF(1,1,22,22),7,7)
    p.setPen(QtGui.QPen(QtCore.Qt.white,1.35,QtCore.Qt.SolidLine,QtCore.Qt.RoundCap,QtCore.Qt.RoundJoin));p.setBrush(QtCore.Qt.NoBrush)
    if module in ('fences','launcher'):
        for x,y in ((7,7),(13,7),(7,13),(13,13)):p.drawRoundedRect(QtCore.QRectF(x,y,4,4),.8,.8)
    elif module=='panel':
        p.drawRoundedRect(QtCore.QRectF(5,8,14,8),2,2)
        for x in (8,12,16):p.drawLine(x,11,x,13)
    elif module=='ai':
        path=QtGui.QPainterPath();path.moveTo(12,5);path.lineTo(14,10);path.lineTo(19,12);path.lineTo(14,14);path.lineTo(12,19);path.lineTo(10,14);path.lineTo(5,12);path.lineTo(10,10);path.closeSubpath();p.drawPath(path)
    elif module=='input':
        font=p.font();font.setPixelSize(13);font.setBold(True);p.setFont(font);p.drawText(QtCore.QRectF(1,0,22,23),QtCore.Qt.AlignCenter,'文')
    elif module=='peony':
        path=QtGui.QPainterPath();path.moveTo(5,8);path.lineTo(10,8);path.lineTo(12,10);path.lineTo(19,10);path.lineTo(19,17);path.lineTo(5,17);path.closeSubpath();p.drawPath(path)
    elif module=='space':
        p.drawRoundedRect(QtCore.QRectF(7,5,11,13),1,1);p.drawLine(5,8,5,20);p.drawLine(5,20,15,20);p.drawLine(10,9,15,9);p.drawLine(10,12,15,12)
    elif module=='integration':p.drawLine(6,12,10,16);p.drawLine(10,16,18,7)
    else:
        p.drawRoundedRect(QtCore.QRectF(5,5,14,14),2,2);p.drawLine(9,8,9,16);p.drawLine(12,12,12,16);p.drawLine(15,10,15,16)
    p.end();pixmap.setDevicePixelRatio(2);return QtGui.QIcon(pixmap)

class NavigationDelegate(QtWidgets.QStyledItemDelegate):
    def sizeHint(self,option,index):
        width=option.widget.viewport().width() if isinstance(option.widget,QtWidgets.QAbstractItemView) else 180
        font=QtGui.QFont(option.font);font.setBold(True);metrics=QtGui.QFontMetrics(font)
        available=max(40,width-4-10-31-(17 if index.data(QtCore.Qt.UserRole+2) else 8)-1)
        bounds=metrics.boundingRect(QtCore.QRect(0,0,available,10000),QtCore.Qt.TextWordWrap,navigation_text(index.data()))
        return QtCore.QSize(140,max(32 if index.data(QtCore.Qt.UserRole+1) else 36,metrics.height()+14,bounds.height()+12))
    def paint(self,painter,option,index):
        painter.save();painter.setRenderHint(QtGui.QPainter.Antialiasing)
        rect=option.rect.adjusted(2,2,-2,-2);key=index.data(QtCore.Qt.UserRole) or '';module=key.split(':')[0];child=index.data(QtCore.Qt.UserRole+1);group=index.data(QtCore.Qt.UserRole+2)
        selected=bool(option.state&QtWidgets.QStyle.State_Selected);hover=bool(option.state&QtWidgets.QStyle.State_MouseOver)
        if selected or hover:painter.setPen(QtCore.Qt.NoPen);painter.setBrush(QtGui.QColor('#DCEBFF' if selected else '#F0F3F8'));painter.drawRoundedRect(rect,6,6)
        left=rect.left()+10
        if not child:icon(module).paint(painter,QtCore.QRect(left,rect.center().y()-11,22,22));left+=31
        else:left+=30
        text=navigation_text(index.data())
        font=option.font;font.setBold(selected);painter.setFont(font);painter.setPen(QtGui.QColor('#095DB5' if selected else '#1D1D1F'))
        painter.drawText(QtCore.QRect(left,rect.top(),rect.right()-left-(17 if group else 8),rect.height()),QtCore.Qt.AlignVCenter|QtCore.Qt.TextWordWrap,text)
        if group:
            painter.setPen(QtGui.QPen(QtGui.QColor('#9aa1ad'),1.3));x=rect.right()-11;y=rect.center().y()
            if str(index.data()).startswith('▾'):painter.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(x-3,y-2),QtCore.QPointF(x,y+1),QtCore.QPointF(x+3,y-2)]))
            else:painter.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(x-2,y-3),QtCore.QPointF(x+1,y),QtCore.QPointF(x-2,y+3)]))
        painter.restore()

class Switch(QtWidgets.QCheckBox):
    def __init__(self,text='',parent=None):
        super().__init__(text,parent);self.setAccessibleName(text);self.setCursor(QtCore.Qt.PointingHandCursor);self.setFixedSize(48,32)
    def sizeHint(self):return QtCore.QSize(48,32)
    def hitButton(self,position):return self.rect().contains(position)
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHint(QtGui.QPainter.Antialiasing)
        rect=QtCore.QRectF(5,(self.height()-22)/2,38,22);p.setPen(QtCore.Qt.NoPen);p.setOpacity(1 if self.isEnabled() else .45);p.setBrush(QtGui.QColor('#007AFF' if self.isChecked() else '#B8BDC7'));p.drawRoundedRect(rect,11,11)
        p.setBrush(QtCore.Qt.white);p.drawEllipse(QtCore.QRectF(rect.left()+(18 if self.isChecked() else 2),rect.top()+2,18,18))
        if self.hasFocus():p.setOpacity(1);p.setPen(QtGui.QPen(QtGui.QColor('#007AFF'),2));p.setBrush(QtCore.Qt.NoBrush);p.drawRoundedRect(rect.adjusted(-3,-3,3,3),13,13)

class MaterialPreview(QtWidgets.QAbstractButton):
    def __init__(self,label,variant,parent=None):
        super().__init__(parent);self.setText(label);self.variant=variant;self.setCheckable(True);self.setCursor(QtCore.Qt.PointingHandCursor);self.setMinimumSize(105,184);self.setSizePolicy(QtWidgets.QSizePolicy.Expanding,QtWidgets.QSizePolicy.Fixed)
    def sizeHint(self):return QtCore.QSize(210,190)
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHints(QtGui.QPainter.Antialiasing|QtGui.QPainter.TextAntialiasing)
        rect=QtCore.QRectF(self.rect()).adjusted(1,1,-1,-1);p.setPen(QtGui.QPen(QtGui.QColor('#3896ff' if self.isChecked() else '#e9ebf0'),2 if self.isChecked() else 1));p.setBrush(QtGui.QColor('#f2f3f6'));p.drawRoundedRect(rect,19,19)
        area=rect.adjusted(12,12,-12,-67);preview_scene(p,area,self.variant)
        font=p.font();font.setPixelSize(14);font.setBold(True);p.setFont(font);p.setPen(QtGui.QColor('#252a34'));p.drawText(QtCore.QRectF(14,rect.bottom()-54,rect.width()-28,23),QtCore.Qt.AlignVCenter,self.text())
        font.setPixelSize(11);font.setBold(False);p.setFont(font);p.setPen(QtGui.QColor('#7e8692'));desc={'liquid':'轻透折射 · 自然光感','soft':'柔和反光 · 舒适阅读','frosted':'细腻磨砂 · 稳定对比'}.get(self.variant,'你的桌面 · 统一外观');p.drawText(QtCore.QRectF(14,rect.bottom()-30,rect.width()-28,18),QtCore.Qt.AlignVCenter,p.fontMetrics().elidedText(desc,QtCore.Qt.ElideRight,int(rect.width()-28)))
        if self.isChecked():p.setBrush(QtGui.QColor('#3596ff'));p.setPen(QtCore.Qt.NoPen);p.drawEllipse(QtCore.QPointF(rect.right()-20,rect.bottom()-44),7,7);p.setPen(QtGui.QPen(QtCore.Qt.white,1.5));p.drawLine(QtCore.QPointF(rect.right()-23,rect.bottom()-44),QtCore.QPointF(rect.right()-21,rect.bottom()-42));p.drawLine(QtCore.QPointF(rect.right()-21,rect.bottom()-42),QtCore.QPointF(rect.right()-17,rect.bottom()-47))

def preview_scene(p,area,variant='liquid'):
    p.save();path=QtGui.QPainterPath();path.addRoundedRect(area,10,10);p.setClipPath(path)
    gradient=QtGui.QLinearGradient(area.topLeft(),area.bottomRight());palette={'liquid':('#a4c8e6','#b39bce'),'soft':('#b7dbd0','#b7c9e5'),'frosted':('#c6d5df','#a0b5d0')};a,b=palette.get(variant,palette['liquid']);gradient.setColorAt(0,QtGui.QColor(a));gradient.setColorAt(1,QtGui.QColor(b));p.fillRect(area,gradient)
    w=area.width();h=area.height();window=QtCore.QRectF(area.left()+w*.10,area.top()+h*.17,w*.74,h*.57)
    p.setPen(QtGui.QPen(QtGui.QColor(255,255,255,125),1));p.setBrush(QtGui.QColor(255,255,255,100 if variant=='liquid' else 170 if variant=='soft' else 210));p.drawRoundedRect(window,7,7)
    for i,color in enumerate(('#ff8c92','#f1cd82','#89c9b6')):p.setPen(QtCore.Qt.NoPen);p.setBrush(QtGui.QColor(color));p.drawEllipse(QtCore.QPointF(window.left()+8+i*7,window.top()+8),2.2,2.2)
    p.setBrush(QtGui.QColor(92,112,145,70));p.drawRoundedRect(QtCore.QRectF(window.left()+8,window.top()+19,window.width()*.24,window.height()-26),4,4)
    for i in range(3):p.drawRoundedRect(QtCore.QRectF(window.left()+window.width()*.37,window.top()+21+i*7,window.width()*(.44-i*.07),3),1.5,1.5)
    dock=QtCore.QRectF(area.left()+w*.19,area.top()+h*.77,w*.65,h*.17);p.setBrush(QtGui.QColor(255,255,255,130));p.setPen(QtGui.QPen(QtGui.QColor(255,255,255,180),.8));p.drawRoundedRect(dock,7,7)
    side=min(h*.11,w*.075)
    for i,color in enumerate(('#5cafff','#ffb668','#88c1a0','#9d8acb','#69bfd1')):p.setPen(QtCore.Qt.NoPen);p.setBrush(QtGui.QColor(color));p.drawRoundedRect(QtCore.QRectF(dock.left()+6+i*(dock.width()-12)/5,dock.center().y()-side/2,side,side),3,3)
    p.restore()

class DesktopPreview(QtWidgets.QWidget):
    def __init__(self,title='你的桌面，井然有序',subtitle='Kylin-LQUI',parent=None):super().__init__(parent);self.title=title;self.subtitle=subtitle;self.setMinimumHeight(146);self.setMaximumHeight(146)
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHint(QtGui.QPainter.Antialiasing);rect=QtCore.QRectF(self.rect()).adjusted(1,1,-1,-1);p.setPen(QtGui.QPen(QtGui.QColor('#dce2ed'),1));p.setBrush(QtGui.QColor('#f8faff'));p.drawRoundedRect(rect,20,20)
        area=QtCore.QRectF(rect.right()-rect.width()*.42,13,rect.width()*.39,120);preview_scene(p,area)
        p.setPen(QtGui.QColor('#2b405d'));font=p.font();font.setPixelSize(19);font.setBold(True);p.setFont(font);p.drawText(QtCore.QRectF(22,36,rect.width()*.53,33),QtCore.Qt.AlignVCenter,p.fontMetrics().elidedText(self.title,QtCore.Qt.ElideRight,int(rect.width()*.52)))
        font.setPixelSize(12);font.setBold(False);p.setFont(font);p.setPen(QtGui.QColor('#7b8da5'));p.drawText(QtCore.QRectF(22,76,rect.width()*.53,28),QtCore.Qt.AlignVCenter,self.subtitle)

class DragHeader(QtWidgets.QWidget):
    def mousePressEvent(self,event):
        if event.button()==QtCore.Qt.LeftButton:self.offset=event.globalPos()-self.window().frameGeometry().topLeft();event.accept()
    def mouseMoveEvent(self,event):
        if event.buttons()&QtCore.Qt.LeftButton and hasattr(self,'offset') and not self.window().isMaximized():self.window().move(event.globalPos()-self.offset)
    def mouseDoubleClickEvent(self,event):
        if event.button()==QtCore.Qt.LeftButton:self.window().showNormal() if self.window().isMaximized() else self.window().showMaximized()

class ImagePreview(QtWidgets.QWidget):
    def __init__(self,data='',parent=None):
        super().__init__(parent);self.setMinimumWidth(0);self.setFixedHeight(150);self.setSizePolicy(QtWidgets.QSizePolicy.Ignored,QtWidgets.QSizePolicy.Fixed);self.set_data(data)
    def set_data(self,data):
        self.image=QtGui.QPixmap();self.image.loadFromData(QtCore.QByteArray.fromBase64(str(data).encode()));self.update()
    def paintEvent(self,event):
        if self.image.isNull():return
        p=QtGui.QPainter(self);p.setRenderHint(QtGui.QPainter.SmoothPixmapTransform);size=self.image.size().scaled(self.size(),QtCore.Qt.KeepAspectRatio);p.drawPixmap(QtCore.QRect(QtCore.QPoint(0,(self.height()-size.height())//2),size),self.image)

def navigation_text(value):
    return re.sub(r'^[▸▾◈▰▦✦文◇▣]\s+', '', str(value or '').strip())

class FlowLayout(QtWidgets.QLayout):
    def __init__(self,parent=None):super().__init__(parent);self.items=[];self.setContentsMargins(0,0,0,0);self.setSpacing(8)
    def addItem(self,item):self.items.append(item)
    def count(self):return len(self.items)
    def itemAt(self,index):return self.items[index] if 0<=index<len(self.items) else None
    def takeAt(self,index):return self.items.pop(index) if 0<=index<len(self.items) else None
    def expandingDirections(self):return QtCore.Qt.Orientations(0)
    def hasHeightForWidth(self):return True
    def heightForWidth(self,width):return self.arrange(QtCore.QRect(0,0,width,0),False)
    def sizeHint(self):return self.minimumSize()
    def minimumSize(self):
        size=QtCore.QSize()
        for item in self.items:
            if not item.widget().isHidden():size=size.expandedTo(item.minimumSize())
        left,top,right,bottom=self.getContentsMargins()
        return size+QtCore.QSize(left+right,top+bottom)
    def setGeometry(self,rect):super().setGeometry(rect);self.arrange(rect,True)
    def arrange(self,rect,place):
        left,top,right,bottom=self.getContentsMargins();outer_y=rect.y()
        rect=rect.adjusted(left,top,-right,-bottom)
        x=rect.x();y=rect.y();height=0
        for item in self.items:
            if item.widget().isHidden():continue
            size=item.sizeHint()
            if size.width()>rect.width():
                size.setWidth(max(1,rect.width()))
                if item.hasHeightForWidth():size.setHeight(item.heightForWidth(size.width()))
            next_x=x+size.width()
            if next_x>rect.right()+1 and x>rect.x():x=rect.x();y+=height+self.spacing();height=0
            if place:item.setGeometry(QtCore.QRect(QtCore.QPoint(x,y),size))
            x+=size.width()+self.spacing();height=max(height,size.height())
        return y+height-outer_y+bottom


class WindowHeader(DragHeader):
    """One client-side header, with standard Qt/system window-control glyphs."""
    def __init__(self,window,search):
        super().__init__(window);self.setObjectName('integratedWindowHeader');self.setFixedHeight(48)
        row=QtWidgets.QHBoxLayout(self);row.setContentsMargins(12,0,8,0);row.setSpacing(4)
        logo=QtWidgets.QLabel();logo.setPixmap(window.windowIcon().pixmap(22,22));row.addWidget(logo)
        brand=QtWidgets.QLabel('Kylin-LQUI 设置');brand.setObjectName('integratedBrand');row.addWidget(brand)
        home=QtWidgets.QToolButton();home.setProperty('windowControl',True);home.setIcon(window_symbol('home'));home.setFixedSize(34,32);home.setToolTip('总览');home.clicked.connect(lambda:window.open_page('overview'));row.addWidget(home)
        row.addStretch();search.setMaximumWidth(360);search.setMinimumWidth(120);row.addWidget(search,2);row.addStretch()
        for name,icon_name,callback in [('minimize',QtWidgets.QStyle.SP_TitleBarMinButton,window.showMinimized),('maximize',QtWidgets.QStyle.SP_TitleBarMaxButton,lambda:window.showNormal() if window.isMaximized() else window.showMaximized()),('close',QtWidgets.QStyle.SP_TitleBarCloseButton,window.close)]:
            button=QtWidgets.QToolButton();button.setObjectName('window_'+name);button.setProperty('windowControl',True);button.setIcon(window_symbol(name));button.setIconSize(QtCore.QSize(17,17));button.setFixedSize(32,32);button.setToolTip({'minimize':'最小化','maximize':'最大化 / 还原','close':'关闭'}[name]);button.clicked.connect(callback);row.addWidget(button)
            if name=='maximize':self.maximize=button
        window.installEventFilter(self)
    def eventFilter(self,obj,event):
        if event.type()==QtCore.QEvent.WindowStateChange:self.maximize.setIcon(window_symbol('restore' if self.window().isMaximized() else 'maximize'))
        return super().eventFilter(obj,event)


def window_symbol(name):
    pixmap=QtGui.QPixmap(36,36);pixmap.fill(QtCore.Qt.transparent);p=QtGui.QPainter(pixmap);p.setRenderHint(QtGui.QPainter.Antialiasing);p.scale(2,2);p.setPen(QtGui.QPen(QtGui.QColor('#30343b'),1.1,QtCore.Qt.SolidLine,QtCore.Qt.RoundCap,QtCore.Qt.RoundJoin));p.setBrush(QtCore.Qt.NoBrush)
    if name=='close':p.drawLine(4,4,14,14);p.drawLine(14,4,4,14)
    elif name=='minimize':p.drawLine(4,10,14,10)
    elif name=='maximize':p.drawRoundedRect(QtCore.QRectF(3.5,3.5,11,11),1,1)
    elif name=='restore':p.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(6,4),QtCore.QPointF(6,2),QtCore.QPointF(16,2),QtCore.QPointF(16,12),QtCore.QPointF(14,12)]));p.drawRect(QtCore.QRectF(3,5,10,10))
    else:p.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(2,8),QtCore.QPointF(9,2),QtCore.QPointF(16,8)]));p.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(4,7),QtCore.QPointF(4,15),QtCore.QPointF(7,15),QtCore.QPointF(7,11),QtCore.QPointF(11,11),QtCore.QPointF(11,15),QtCore.QPointF(14,15),QtCore.QPointF(14,7)]))
    p.end();pixmap.setDevicePixelRatio(2);return QtGui.QIcon(pixmap)
