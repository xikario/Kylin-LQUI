#!/usr/bin/python3
"""Kylin-LQUI standalone settings; native module callbacks own configuration."""
import json
import math
import sys
from pathlib import Path
from PyQt5 import QtCore, QtGui, QtWidgets, QtDBus
from backend import Backend, MODULES, ROOT

from design import STYLE, COLORS, DESCRIPTIONS, NavigationDelegate, Switch, MaterialPreview, DesktopPreview, DragHeader, ImagePreview, WindowHeader, icon, navigation_text, FlowLayout

class HelpFilter(QtCore.QObject):
    """Delay helpful descriptions by one second; no native UKUI tooltip arrow."""
    def __init__(self, parent):
        super().__init__(parent)
        self.target = None
        self.timer = QtCore.QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.show)
    def install(self, widget, text):
        if not text: return
        widget.setProperty('helpText', text)
        widget.installEventFilter(self)
    def eventFilter(self, obj, event):
        if event.type() == QtCore.QEvent.Enter:
            self.target = obj
            self.timer.start(1000)
        elif event.type() in (QtCore.QEvent.Leave, QtCore.QEvent.MouseButtonPress, QtCore.QEvent.Hide):
            self.timer.stop()
            QtWidgets.QToolTip.hideText()
        elif event.type() == QtCore.QEvent.ToolTip:
            return True
        return False
    def show(self):
        if self.target and self.target.isVisible():
            QtWidgets.QToolTip.showText(QtGui.QCursor.pos()+QtCore.QPoint(8,16), self.target.property('helpText'), self.target)

class SettingsWindow(QtWidgets.QMainWindow):
    def __init__(self, backend=None):
        super().__init__()
        self.backend = backend or Backend(self)
        self.current = ''
        self.generation = 0
        self.revision = 0
        self.busy = False
        self.fields = {}
        self.drafts = {}
        self.signature = ''
        self.request_serial = 0
        self.page_cache = {}
        self.catalog = {}
        self.collapsed = set(MODULES)
        self.setWindowTitle('Kylin-LQUI 设置')
        self.setWindowFlags(QtCore.Qt.Window|QtCore.Qt.FramelessWindowHint|QtCore.Qt.WindowMinimizeButtonHint|QtCore.Qt.WindowMaximizeButtonHint|QtCore.Qt.WindowCloseButtonHint)
        self.setAttribute(QtCore.Qt.WA_TranslucentBackground)
        self.setWindowIcon(QtGui.QIcon(str(Path(__file__).with_name('icon.svg'))))
        self.resize(1040,740)
        screen=QtWidgets.QApplication.screenAt(QtGui.QCursor.pos()) or QtWidgets.QApplication.primaryScreen()
        if screen:
            area=screen.availableGeometry().adjusted(10,10,-10,-10);self.resize(self.size().boundedTo(area.size()));self.move(area.center()-self.rect().center())
        self.setMinimumSize(720,480)
        self.setStyleSheet(STYLE.replace(":/qt-project.org/styles/commonstyle/images/standardbutton-apply-16.png",str(Path(__file__).with_name("check.svg")))+"QComboBox::down-arrow{image:url("+str(Path(__file__).with_name("down.svg"))+");width:14px;height:14px;}")
        self.help = HelpFilter(self)
        body = QtWidgets.QWidget();body.setObjectName('body');self.setCentralWidget(body)
        # Leave the parent's single continuous outline visible on both halves.
        root = QtWidgets.QHBoxLayout(body);root.setContentsMargins(1,1,1,1);root.setSpacing(0)
        sidebar = QtWidgets.QFrame();sidebar.setObjectName('sidebar');sidebar.setFixedWidth(230)
        left = QtWidgets.QVBoxLayout(sidebar);left.setContentsMargins(18,8,15,18);left.setSpacing(12)
        side_header=DragHeader(sidebar);side_header.setFixedHeight(48);side_title=QtWidgets.QHBoxLayout(side_header);side_title.setContentsMargins(0,0,0,0)
        logo=QtWidgets.QLabel();logo.setPixmap(self.windowIcon().pixmap(25,25));side_title.addWidget(logo)
        brand=QtWidgets.QLabel('Kylin-LQUI 设置');brand.setObjectName('integratedBrand');side_title.addWidget(brand);side_title.addStretch();left.addWidget(side_header)
        self.search=QtWidgets.QLineEdit();self.search.setObjectName('search');self.search.setPlaceholderText('搜索设置')
        self.navigation = QtWidgets.QListWidget();self.navigation.setObjectName('navigation');self.navigation.setItemDelegate(NavigationDelegate(self.navigation));self.navigation.setMouseTracking(True);self.navigation.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);left.addWidget(self.navigation,1)
        self.add_navigation('总览与模块状态','overview')
        for module,(title,service,icon) in MODULES.items():
            self.add_navigation(icon+'  '+title,module+':general')
        self.add_navigation('集成与维护','integration')
        system = QtWidgets.QPushButton('UKUI 系统设置');system.clicked.connect(self.open_system);left.addWidget(system)
        root.addWidget(sidebar)
        right = QtWidgets.QWidget();right_layout = QtWidgets.QVBoxLayout(right);right_layout.setContentsMargins(24,8,14,13);right_layout.setSpacing(12)
        self.window_header=WindowHeader(self,self.search);right_layout.addWidget(self.window_header)
        header_widget=QtWidgets.QWidget(right);header = QtWidgets.QHBoxLayout(header_widget);header.setContentsMargins(0,0,0,0);self.title=QtWidgets.QLabel();self.title.setObjectName('title');header.addWidget(self.title,1)
        self.open_space=QtWidgets.QPushButton('打开 AI 空间');self.open_space.clicked.connect(lambda:self.start_module(show_main=True));self.open_space.hide();header.addWidget(self.open_space)
        refresh = QtWidgets.QPushButton('刷新');refresh.clicked.connect(self.reload_page);header.addWidget(refresh);right_layout.addWidget(header_widget)
        self.description=QtWidgets.QLabel('');self.description.setWordWrap(True);self.description.setObjectName('subtitle');right_layout.addWidget(self.description)
        self.scroll=QtWidgets.QScrollArea();self.scroll.setWidgetResizable(True);self.scroll.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);right_layout.addWidget(self.scroll,1)
        self.message=QtWidgets.QLabel();self.message.setWordWrap(True);self.message.setObjectName('subtitle');footer=QtWidgets.QHBoxLayout();footer.addWidget(self.message,1);footer.addWidget(QtWidgets.QSizeGrip(self));right_layout.addLayout(footer)
        root.addWidget(right,1)
        self.navigation.currentItemChanged.connect(self.selection_changed)
        self.navigation.itemClicked.connect(self.toggle_group)
        self.navigation.installEventFilter(self)
        self.search.textChanged.connect(self.filter_search)
        self.poll=QtCore.QTimer(self);self.poll.setInterval(1800);self.poll.timeout.connect(self.refresh);self.poll.start()
        self.load_catalogs()
        self.open_page('overview')

    def paintEvent(self,event):
        painter=QtGui.QPainter(self);painter.setRenderHint(QtGui.QPainter.Antialiasing);painter.setPen(QtGui.QPen(QtGui.QColor('#d1d7df'),1));painter.setBrush(QtGui.QColor('#e7e9ed'));painter.drawRoundedRect(QtCore.QRectF(self.rect()).adjusted(.5,.5,-.5,-.5),12,12)

    def view_state(self):
        return {'page':self.current,'loading':self.busy,'fields':len(self.fields),'visible':self.isVisible(),'title':self.title.text(),'error':self.message.text()}

    def toggle_group(self,item):
        if not item:return
        group=item.data(QtCore.Qt.UserRole+2)
        if not group:return
        if group in self.collapsed or not self.current.startswith(group+':'):
            self.collapsed.discard(group)
            pages=self.catalog.get(group,[])
            if pages:self.open_page(group+':'+pages[0]['id'])
        else:self.collapsed.add(group)
        self.filter_search(self.search.text())

    def closeEvent(self,event):
        if self.current.startswith('ai:'):self.backend.call('ai','Discard',[self.current.split(':',1)[1]],lambda result:None)
        super().closeEvent(event)

    def eventFilter(self,watched,event):
        if watched==self.navigation and event.type()==QtCore.QEvent.KeyPress and event.key() in (QtCore.Qt.Key_Return,QtCore.Qt.Key_Enter,QtCore.Qt.Key_Space):
            item=self.navigation.currentItem()
            if item and item.data(QtCore.Qt.UserRole+2):self.toggle_group(item);return True
        return super().eventFilter(watched,event)

    def add_navigation(self,title,key):
        item=QtWidgets.QListWidgetItem(title);item.setData(QtCore.Qt.UserRole,key);self.navigation.addItem(item)
        return item

    def reload_page(self):
        self.load_catalogs();self.refresh(force=True)

    def load_catalogs(self):
        for module in MODULES:
            self.backend.call(module,'Catalog',[],lambda result,m=module:self.receive_catalog(m,result))

    def receive_catalog(self,module,result):
        if not result.get('ok'):return
        pages=result.get('pages',[])
        if not pages:return
        old=self.current
        blocker=QtCore.QSignalBlocker(self.navigation)
        indexes=[i for i in range(self.navigation.count()) if self.navigation.item(i).data(QtCore.Qt.UserRole).startswith(module+':')]
        start=indexes[0] if indexes else self.navigation.count()-1
        for i in reversed(indexes):self.navigation.takeItem(i)
        if len(pages)>1:
            header=QtWidgets.QListWidgetItem(('▸  ' if module in self.collapsed else '▾  ')+MODULES[module][0]);header.setData(QtCore.Qt.UserRole,module+':__group__');header.setData(QtCore.Qt.UserRole+2,module);self.navigation.insertItem(start,header);start+=1
        for i,page in enumerate(pages):
            key=module+':'+page['id'];title=page.get('title',page['id'])
            label=MODULES[module][2]+'  '+MODULES[module][0] if len(pages)==1 else '    '+('分区 · ' if page.get('group')=='分区' else '')+title
            item=QtWidgets.QListWidgetItem(label);item.setData(QtCore.Qt.UserRole,key)
            if len(pages)>1:item.setData(QtCore.Qt.UserRole+1,module)
            item.setToolTip(MODULES[module][0]);self.navigation.insertItem(start+i,item)
            if key==old:self.navigation.setCurrentItem(item);self.title.setText(title if len(pages)>1 else MODULES[module][0])
        del blocker
        self.catalog[module]=pages
        if old==module+':general' and not any(p['id']=='general' for p in pages):self.open_page(module+':'+pages[0]['id'])
        self.filter_search(self.search.text())

    def open_page(self,key):
        module=key.split(':')[0]
        if module not in MODULES and key not in ('overview','integration'):key='overview';module='overview'
        if key in MODULES or key==module+':__group__':key=module+':general'
        if key==module+':general' and self.catalog.get(module) and not any(p['id']=='general' for p in self.catalog[module]):key=module+':'+self.catalog[module][0]['id']
        self.collapsed.discard(module)
        self.search.clear()
        selected=None
        for i in range(self.navigation.count()):
            item=self.navigation.item(i)
            if item.data(QtCore.Qt.UserRole)==key:selected=item;break
        if selected:
            blocker=QtCore.QSignalBlocker(self.navigation);self.navigation.setCurrentItem(selected);del blocker
        title=navigation_text(selected.text()) if selected else MODULES.get(module,(key,))[0]
        self.activate_page(key,title)
        self.filter_search('')
        if self.isMinimized():self.showNormal()
        else:self.show()
        self.raise_();self.activateWindow()

    def activate_page(self,key,title):
        if self.current.startswith('ai:') and self.current!=key:self.backend.call('ai','Discard',[self.current.split(':',1)[1]],lambda result:None)
        # A page owns its request state. Late responses from the previous page
        # cannot retain the busy flag or overwrite the newly selected content.
        for entry in self.fields.values():
            control=entry['control']
            if isinstance(control,QtWidgets.QPlainTextEdit) and control.document().isModified():self.drafts[(self.current,entry['field'].get('name') or entry['field']['label'])]=control.toPlainText()
        self.current=key;self.generation+=1;self.request_serial+=1;self.signature='';self.fields={};self.busy=False
        self.poll.setInterval(6000 if key=='overview' else 15000 if key.startswith('peony:') else 2500)
        self.title.setText(title);self.message.clear();self.open_space.setVisible(key.startswith('space:'))
        self.description.setText(DESCRIPTIONS.get(key,''))
        self.set_content(QtWidgets.QLabel('正在读取…'))
        self.refresh(force=True)

    def selection_changed(self,item,previous):
        if not item or item.data(QtCore.Qt.UserRole+2):return
        self.activate_page(item.data(QtCore.Qt.UserRole),navigation_text(item.text()))

    def set_content(self,widget):
        old=self.scroll.takeWidget()
        if old:old.deleteLater()
        self.scroll.setWidget(widget);widget.setAutoFillBackground(False);self.scroll.viewport().setAutoFillBackground(False)

    def refresh(self,force=False):
        if (self.busy and not force) or (not force and (not self.isVisible() or self.isMinimized())):return
        self.request_serial+=1;request=self.request_serial
        generation=self.generation;revision=self.revision;key=self.current;self.busy=True
        def receive(result):
            if generation!=self.generation or revision!=self.revision or request!=self.request_serial:return
            self.busy=False
            if key=='overview':self.render_overview(result)
            elif key=='integration':self.render_integration(result)
            else:self.receive_page(result)
        if key=='overview':self.backend.process(['/usr/bin/python3',str(Path(__file__).with_name('status.py'))],receive)
        elif key=='integration':receive({})
        elif ':' in key:
            module,page=key.split(':',1);self.backend.call(module,'Page',[page],receive)
        else:self.busy=False

    def receive_page(self,result):
        if not result.get('ok'):
            self.message.setText(result.get('error','设置暂不可用'))
            if not self.fields:
                box=QtWidgets.QWidget();layout=QtWidgets.QVBoxLayout(box);label=QtWidgets.QLabel('模块未就绪\n\n请启动对应模块，或完成本轮组件更新后刷新。');label.setWordWrap(True);layout.addWidget(label);start=QtWidgets.QPushButton('打开 AI 空间并加载设置' if self.current.startswith('space:') else '启动模块');start.clicked.connect(lambda:self.start_module(show_main=True));layout.addWidget(start);
                if self.current.startswith('space:'):
                    load=QtWidgets.QPushButton('只加载设置');load.clicked.connect(lambda:self.start_module(show_main=False));layout.addWidget(load)
                layout.addStretch();self.set_content(box)
            return
        fields=result.get('fields',[])
        if not fields:
            self.message.setText('此页面暂未提供设置，请刷新或检查模块状态。');return
        self.page_cache[self.current]=fields
        structural=[{**{k:v for k,v in f.items() if k not in ('value','enabled','help','text','items')}, 'items':[(x['text'],x.get('checkable',False)) for x in f.get('items',[])]} for f in fields]
        signature=json.dumps(structural,ensure_ascii=False,sort_keys=True)
        focused=QtWidgets.QApplication.focusWidget()
        editing=focused and self.scroll.isAncestorOf(focused) and (isinstance(focused,(QtWidgets.QLineEdit,QtWidgets.QPlainTextEdit)) or isinstance(focused,QtWidgets.QSlider) and focused.isSliderDown() or isinstance(focused,QtWidgets.QComboBox) and focused.view().isVisible())
        if signature!=self.signature:
            if editing:return
            self.signature=signature;self.render_fields(fields)
        else:
            for field in fields:self.update_field(field)
        self.message.setText('此页有未应用修改，请点击“应用”或“保存”。' if result.get('dirty') else '')
        if any(isinstance(e['control'],QtWidgets.QPlainTextEdit) and e['control'].document().isModified() for e in self.fields.values()):self.message.setText('文字修改尚未更新，请点“更新”。')
        if result.get('modalActive'):self.message.setText('请先完成当前的确认或文件选择窗口。')

    def card(self,layout):
        frame=QtWidgets.QFrame();frame.setProperty('card',True);box=QtWidgets.QVBoxLayout(frame);box.setContentsMargins(18,12,18,12);box.setSpacing(5);layout.addWidget(frame);return box

    def render_fields(self,fields):
        position=self.scroll.verticalScrollBar().value()
        widget=QtWidgets.QWidget();layout=QtWidgets.QVBoxLayout(widget);layout.setContentsMargins(0,0,6,0);layout.setSpacing(13);self.fields={}
        box=None;action_flow=None
        presets=[f for f in fields if f.get('name','').startswith('liquidMaterialPreset_')]
        if presets:
            section=QtWidgets.QLabel('材质风格');section.setProperty('section',True);layout.addWidget(section)
            preview=self.card(layout);preview_row=QtWidgets.QHBoxLayout();preview_row.setSpacing(10);preview.addLayout(preview_row)
            for field in presets:
                control=MaterialPreview(field['label'],field['name'].split('_')[-1]);control.setObjectName(field['name']);control.setChecked(bool(field.get('value')));control.setEnabled(field.get('enabled',True))
                control.clicked.connect(lambda checked=False,f=field['id'],k=self.current:self.mutate(f,activate=True,page_key=k));preview_row.addWidget(control,1)
                self.fields[field['id']]={'row':control,'control':control,'field':field,'label':QtWidgets.QLabel(field['label'])}
        elif self.current=='fences:appearance':layout.addWidget(DesktopPreview('液态桌面','光感、透明与阅读舒适度'))
        # Category controls always precede their visible fields.
        for field in sorted(fields,key=lambda f:0 if f['kind']=='tabs' else 1):
            if field in presets or field.get('name')=='pageHeading':continue
            kind=field['kind'];name=field.get('label','选项');fid=field['id']
            if kind!='action':action_flow=None
            if kind=='heading':
                label=QtWidgets.QLabel(str(field.get('value',name)));label.setProperty('section',True);label.setWordWrap(True);layout.addWidget(label);box=None;continue
            if box is None:box=self.card(layout)
            if kind=='action':
                if action_flow is None:
                    action_row=QtWidgets.QWidget();action_flow=FlowLayout(action_row);box.addWidget(action_row)
                control=QtWidgets.QPushButton(name);control.setObjectName(field.get('name') or 'field_'+fid);control.setEnabled(field.get('enabled',True));control.setAccessibleName(name)
                if name in ('应用','保存','保存设置'):control.setProperty('primary',True)
                control.clicked.connect(lambda checked=False,f=fid,k=self.current:self.mutate(f,activate=True,page_key=k));action_flow.addWidget(control);self.help.install(control,field.get('help') or name)
                self.fields[fid]={'row':control,'control':control,'field':field,'label':None};continue
            row=QtWidgets.QWidget();row.setMinimumHeight(36);line=QtWidgets.QHBoxLayout(row);line.setContentsMargins(0,4,0,4);line.setSpacing(14)
            if kind not in ('note','preview','action','toggle'):
                badge=QtWidgets.QLabel();badge.setPixmap(icon(self.current.split(':')[0],21).pixmap(21,21));badge.setFixedSize(21,24);line.addWidget(badge)
            label=QtWidgets.QLabel(name);label.setWordWrap(True);label.setMinimumWidth(110);label.setMaximumWidth(190)
            control=None;entry={'row':row,'field':field,'label':label}
            if kind=='note':
                control=QtWidgets.QLabel(str(field.get('value','')));control.setWordWrap(True);control.setProperty('note',True);control.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse|QtCore.Qt.LinksAccessibleByMouse);line.addWidget(control,1)
            elif kind=='preview':
                control=ImagePreview(field.get('value',''));line.addWidget(control,1)
            elif kind in ('action','toggle'):
                control=QtWidgets.QPushButton(name);control.setCheckable(kind=='toggle');control.setChecked(bool(field.get('value')))
                if name in ('应用','保存','保存设置'):control.setProperty('primary',True)
                control.clicked.connect(lambda checked=False,f=fid,k=self.current:self.mutate(f,activate=True,page_key=k));line.addWidget(control);line.addStretch()
            elif kind=='boolean':
                control=Switch(name);control.setChecked(bool(field.get('value')));control.clicked.connect(lambda value,f=fid,k=self.current:self.mutate(f,value,page_key=k));line.addWidget(control,1)
            elif kind=='number':
                line.addWidget(label);control=QtWidgets.QSlider(QtCore.Qt.Horizontal);control.setMinimumWidth(80)
                step=max(float(field.get('step',1)),.0001);low=float(field.get('min',0));high=float(field.get('max',100));ticks=max(0,min(1000000,round((high-low)/step)))
                control.setRange(0,ticks);entry.update(low=low,step=step)
                value=QtWidgets.QLabel();value.setAlignment(QtCore.Qt.AlignRight|QtCore.Qt.AlignVCenter);value.setMinimumWidth(70);entry['readout']=value
                control.setValue(round((float(field.get('value',low))-low)/step))
                def changed(v,e=entry):e['readout'].setText(self.number_text(e['low']+v*e['step'],e['field']))
                control.valueChanged.connect(changed);value.setText(self.number_text(float(field.get('value',low)),field))
                control.sliderReleased.connect(lambda f=fid,e=entry,c=control,k=self.current:self.mutate(f,round(e['low']+c.value()*e['step'],int(e['field'].get('decimals',0))),page_key=k))
                control.actionTriggered.connect(lambda action,f=fid,e=entry,c=control,k=self.current:QtCore.QTimer.singleShot(0,lambda:self.mutate(f,round(e['low']+c.value()*e['step'],int(e['field'].get('decimals',0))),page_key=k)) if not c.isSliderDown() else None)
                line.addWidget(control,1);line.addWidget(value)
            elif kind in ('choice','tabs'):
                line.addWidget(label);control=QtWidgets.QComboBox();control.setSizeAdjustPolicy(QtWidgets.QComboBox.AdjustToMinimumContentsLengthWithIcon);control.setMinimumContentsLength(10);control.setSizePolicy(QtWidgets.QSizePolicy.Expanding,QtWidgets.QSizePolicy.Fixed);control.addItems(field.get('options',[]));
                for index,allowed in enumerate(field.get('optionsEnabled',[])):
                    if control.model().item(index):control.model().item(index).setEnabled(allowed)
                control.setEditable(bool(field.get('editable')));control.setCurrentIndex(field.get('value',0));control.activated[int].connect(lambda value,f=fid,k=self.current:self.mutate(f,value,page_key=k))
                if control.isEditable():control.lineEdit().editingFinished.connect(lambda f=fid,c=control,k=self.current:self.mutate(f,c.currentText(),page_key=k))
                line.addWidget(control,1)
            elif kind=='text':
                line.addWidget(label);control=QtWidgets.QLineEdit(str(field.get('value','')));control.setPlaceholderText(field.get('placeholder',''));control.setMaxLength(field.get('maxLength',32767));control.setReadOnly(bool(field.get('readOnly')))
                if field.get('secret'):control.setEchoMode(QtWidgets.QLineEdit.Password)
                control.editingFinished.connect(lambda f=fid,c=control,k=self.current:self.mutate(f,c.text(),page_key=k) if c.isModified() else None);line.addWidget(control,1)
            elif kind=='multiline':
                line.setDirection(QtWidgets.QBoxLayout.TopToBottom);label.setMaximumWidth(16777215);line.addWidget(label);control=QtWidgets.QPlainTextEdit(str(field.get('value','')));control.setReadOnly(bool(field.get('readOnly')));control.setFixedHeight(120);line.addWidget(control,1)
                draft=self.drafts.get((self.current,field.get('name') or name))
                if draft is not None:control.setPlainText(draft);control.document().setModified(True)
                save=QtWidgets.QPushButton('更新');save.clicked.connect(lambda checked=False,f=fid,c=control,k=self.current:self.mutate(f,c.toPlainText(),page_key=k));line.addWidget(save)
            elif kind=='list':
                line.setDirection(QtWidgets.QBoxLayout.TopToBottom);label.setMaximumWidth(16777215);line.addWidget(label);control=QtWidgets.QListWidget();control.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection if field.get('multiple') else QtWidgets.QAbstractItemView.SingleSelection);control.setFixedHeight(min(240,max(90,len(field.get('items',[]))*32+18)));line.addWidget(control,1)
                self.populate_list(control,field)
                control.itemSelectionChanged.connect(lambda f=fid,c=control,k=self.current:self.send_selection(f,c,page_key=k))
                control.itemChanged.connect(lambda item,f=fid,c=control,k=self.current:self.mutate(f,{'row':c.row(item),'checked':item.checkState()==QtCore.Qt.Checked},page_key=k))
            else:continue
            control.setObjectName(field.get('name') or 'field_'+fid);control.setEnabled(field.get('enabled',True));control.setAccessibleName(name)
            self.help.install(row,field.get('help') or name);self.help.install(control,field.get('help') or name)
            entry['control']=control;self.fields[fid]=entry
            if box.count():
                divider=QtWidgets.QFrame();divider.setFixedHeight(1);divider.setStyleSheet('background:#eef0f4;border:0;');box.addWidget(divider)
            box.addWidget(row)
        layout.addStretch();self.set_content(widget);self.filter_search(self.search.text());QtCore.QTimer.singleShot(0,lambda:self.scroll.verticalScrollBar().setValue(position))

    @staticmethod
    def number_text(value,field):
        labels=field.get('valueLabels',[])
        if labels and 0<=int(value)<len(labels):return labels[int(value)]
        return ('%.*f'%(int(field.get('decimals',0)),value))+field.get('suffix','')

    def populate_list(self,control,field):
        blocker=QtCore.QSignalBlocker(control);control.clear()
        for data in field.get('items',[]):
            item=QtWidgets.QListWidgetItem(data['text']);control.addItem(item)
            flags=QtCore.Qt.ItemIsSelectable
            if data.get('enabled',True):flags|=QtCore.Qt.ItemIsEnabled
            if data.get('checkable'):flags|=QtCore.Qt.ItemIsUserCheckable
            item.setFlags(flags)
            if data.get('checkable'):item.setCheckState(QtCore.Qt.Checked if data.get('checked') else QtCore.Qt.Unchecked)
            item.setSelected(data.get('selected',False))
        current=field.get('value',-1)
        if isinstance(current,int) and 0<=current<control.count():control.setCurrentRow(current,QtCore.QItemSelectionModel.NoUpdate)
        del blocker

    def send_selection(self,fid,control,page_key=None):
        row=control.currentRow()
        if row>=0:self.mutate(fid,{'row':row,'selectedRows':[control.row(item) for item in control.selectedItems()]},page_key=page_key)

    def update_field(self,field):
        entry=self.fields.get(field['id'])
        if not entry:return
        control=entry['control'];entry['field']=field;control.setEnabled(field.get('enabled',True))
        focus=QtWidgets.QApplication.focusWidget()
        if isinstance(control,(QtWidgets.QLineEdit,QtWidgets.QPlainTextEdit)) and focus and (focus==control or control.isAncestorOf(focus)):return
        if isinstance(control,QtWidgets.QSlider) and control.isSliderDown():return
        if isinstance(control,QtWidgets.QComboBox) and control.view().isVisible():return
        if isinstance(control,QtWidgets.QListWidget) and focus==control:return
        blocker=QtCore.QSignalBlocker(control);value=field.get('value');kind=field['kind']
        if kind in ('boolean','toggle'):control.setChecked(bool(value))
        elif kind=='number':
            control.setValue(round((float(value)-entry['low'])/entry['step']));entry['readout'].setText(self.number_text(value,field))
        elif kind in ('choice','tabs'):control.setCurrentIndex(int(value))
        elif kind in ('note','text'):control.setText(str(value or ''))
        elif kind=='multiline' and not control.document().isModified():control.setPlainText(str(value or ''))
        elif kind=='list':self.populate_list(control,field)
        elif kind=='preview':control.set_data(value or '')
        del blocker

    def mutate(self,fid,value=None,activate=False,page_key=None):
        entry=self.fields.get(fid)
        if activate and entry and entry['field'].get('confirm'):
            if QtWidgets.QMessageBox.question(self,'确认操作',entry['field']['confirm'])!=QtWidgets.QMessageBox.Yes:return
        key=page_key or self.current;module,page=key.split(':',1);generation=self.generation
        draft_key=(key,entry['field'].get('name') or entry['field']['label']) if entry and isinstance(entry['control'],QtWidgets.QPlainTextEdit) else None
        visible=key==self.current
        if visible:self.revision+=1;self.request_serial+=1;self.busy=True
        revision=self.revision
        submitted_control=entry['control'] if entry else None
        submitted_text=submitted_control.toPlainText() if isinstance(submitted_control,QtWidgets.QPlainTextEdit) else submitted_control.text() if isinstance(submitted_control,QtWidgets.QLineEdit) else None
        def done(result):
            if not visible or generation!=self.generation or revision!=self.revision:return
            self.busy=False
            current_entry=self.fields.get(fid)
            current_control=current_entry['control'] if current_entry else None
            if submitted_text is not None:
                if current_control is not submitted_control:return
                current_text=current_control.toPlainText() if isinstance(current_control,QtWidgets.QPlainTextEdit) else current_control.text()
                if current_text!=submitted_text:return
            if result.get('ok') and draft_key and self.drafts.get(draft_key,submitted_text)==submitted_text:self.drafts.pop(draft_key,None)
            if not result.get('ok'):self.message.setText(result.get('error','保存失败'));return
            entry=self.fields.get(fid)
            if entry:
                control=entry['control']
                if isinstance(control,QtWidgets.QLineEdit):control.setModified(False)
                if isinstance(control,QtWidgets.QPlainTextEdit):control.document().setModified(False)
            if 'fields' in result:self.receive_page(result)
            else:QtCore.QTimer.singleShot(100,lambda:self.refresh(force=True))
        args=[page,fid] if activate else [page,fid,json.dumps({'value':value},ensure_ascii=False)]
        self.backend.call(module,'Activate' if activate else 'Set',args,done)

    def filter_search(self,query):
        query=query.strip().lower()
        for i in range(self.navigation.count()):
            item=self.navigation.item(i);key=item.data(QtCore.Qt.UserRole);cached=self.page_cache.get(key,[])
            matches=not query or query in item.text().lower() or any(query in (f.get('label','')+' '+f.get('help','')).lower() for f in cached)
            group=item.data(QtCore.Qt.UserRole+2)
            child=item.data(QtCore.Qt.UserRole+1)
            if group:item.setText(('▸  ' if group in self.collapsed else '▾  ')+MODULES[group][0])
            item.setHidden((not matches and key!=self.current) or bool(not query and child in self.collapsed))
        for entry in self.fields.values():
            field=entry['field'];entry['row'].setVisible(not query or query in (field.get('label','')+' '+str(field.get('value',''))+' '+field.get('help','')).lower())

    def render_overview(self,result):
        if not result.get('components'):
            self.message.setText(result.get('error','读取模块状态失败'));return
        components=result['components'];ids=[item['id'] for item in components]
        rebuild=self.signature!=str(ids)
        if rebuild:
            widget=QtWidgets.QWidget();layout=QtWidgets.QVBoxLayout(widget);layout.setContentsMargins(0,0,6,0);layout.setSpacing(12);layout.addWidget(DesktopPreview('你的桌面，井然有序','模块、启动与资源，一目了然'));self.overview_rows={}
            for item in components:
                box=self.card(layout);header=QtWidgets.QHBoxLayout();name=QtWidgets.QLabel(item['name']);name.setProperty('section',True);header.addWidget(name,1)
                label=QtWidgets.QLabel();header.addWidget(label);box.addLayout(header)
                info=QtWidgets.QLabel();info.setWordWrap(True);box.addWidget(info);pids=QtWidgets.QLabel();box.addWidget(pids)
                self.overview_rows[item['id']]=(label,info,pids)
            layout.addStretch();self.set_content(widget);self.signature=str(ids)
        for item in components:
            label,info,pids=self.overview_rows[item['id']];running=bool(item.get('processes'))
            label.setText('● 运行中' if running else '○ 未运行');label.setStyleSheet('color:'+('#29977f' if running else '#8994a3'))
            memory=item.get('pssKiB');parts=['功能：'+item.get('enabledState','由模块配置'), '登录启动：'+item.get('startupState','未确认')]
            if running:parts.append('PSS：'+('%.1f MB'%(memory/1024) if memory is not None else '未知'))
            info.setText('   ·   '.join(parts)+''.join('\n'+feature['name']+'：'+('启用' if feature['enabled'] else '关闭')+' · '+('登录自动开启' if feature['autoStart'] else '手动开启') for feature in item.get('features',[])));pids.setText('PID  '+', '.join(str(p['pid']) for p in item.get('processes',[])));pids.setVisible(running)
        self.description.setText('查看各模块状态，管理你的桌面。')

    def render_integration(self,result):
        widget=QtWidgets.QWidget();layout=QtWidgets.QVBoxLayout(widget);layout.setContentsMargins(0,0,6,0)
        for title,text in [('当前系统','Kylin-LQUI 负责桌面视觉与交互。KWin、Fcitx、网络、音频、电源和安全服务继续由 UKUI 提供。'),('桌面接管','“限时试用”只暂时让 Fences 单独显示桌面，60 秒后恢复原生桌面。开机进入 Fences 的正式模式尚未启用。'),('配置与恢复','各组件继续使用原有配置文件。统一设置通过所属组件写入，应用安装保留版本清单和配置备份。'),('资料与诊断','源码、原始 NextKde 参考、验收记录和待办都保存在 Kylin-LQUI 工作目录。')]:
            box=self.card(layout);heading=QtWidgets.QLabel(title);heading.setProperty('section',True);box.addWidget(heading);label=QtWidgets.QLabel(text);label.setWordWrap(True);box.addWidget(label)
        layout.addStretch();self.set_content(widget)

    def start_module(self,show_main=False):
        module=self.current.split(':')[0]
        programs={
            'ai':(str(Path.home()/'.local/bin/liquid-cli'),['--resident']),
            'space':(str(Path.home()/'.local/bin/ai-space'),['--show'] if show_main else ['--settings-service']),
            'fences':(str(Path.home()/'.local/bin/ukui-fences'),[]),
            'launcher':(str(Path.home()/'.local/bin/ukui-kaishicaidan-launcher'),['--autostart']),
            'panel':(str(Path.home()/'.local/bin/ukui-panel-liquid-session'),[]),
            'input':(str(Path.home()/'.local/bin/fcitx-liquid-panel'),[]),
        }
        if module not in programs:return
        program,args=programs[module]
        if not Path(program).is_file():self.message.setText('模块尚未安装，请先完成组件安装。');return
        started=QtCore.QProcess.startDetached(program,args)
        if started[0] if isinstance(started,tuple) else started:
            self.message.setText('正在启动模块…');QtCore.QTimer.singleShot(1200,self.load_catalogs);QtCore.QTimer.singleShot(1500,lambda:self.refresh(force=True))
        else:self.message.setText('启动失败，请检查组件安装。')

    def open_system(self):
        QtCore.QProcess.startDetached('ukui-control-center',[])

class Adaptor(QtDBus.QDBusAbstractAdaptor):
    QtCore.Q_CLASSINFO('D-Bus Interface','org.kylin.Lqui.Settings')
    @QtCore.pyqtSlot(str)
    def Open(self,page):self.parent().open_page(page)
    @QtCore.pyqtSlot(result=str)
    def State(self):return json.dumps(self.parent().view_state(),ensure_ascii=False)
    @QtCore.pyqtSlot(result=bool)
    def CloseForUpdate(self):
        window=self.parent()
        if QtWidgets.QApplication.activeModalWidget() or window.busy or window.drafts:return False
        for entry in window.fields.values():
            widget=entry['control']
            if isinstance(widget,QtWidgets.QLineEdit) and widget.isModified():return False
            if isinstance(widget,QtWidgets.QPlainTextEdit) and widget.document().isModified():return False
        window.close();return True

def main():
    QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling)
    QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_UseHighDpiPixmaps)
    app=QtWidgets.QApplication(sys.argv);app.setApplicationName('kylin-lqui-settings');app.setApplicationDisplayName('Kylin-LQUI 设置');app.setDesktopFileName('kylin-lqui-settings')
    page='overview'
    if '--page' in sys.argv:
        index=sys.argv.index('--page')+1
        if index<len(sys.argv):page=sys.argv[index]
    bus=QtDBus.QDBusConnection.sessionBus()
    if not bus.registerService('org.kylin.Lqui.Settings'):
        interface=QtDBus.QDBusInterface('org.kylin.Lqui.Settings','/Settings','org.kylin.Lqui.Settings',bus);interface.call('Open',page);return 0
    window=SettingsWindow();adaptor=Adaptor(window);bus.registerObject('/Settings',window,QtDBus.QDBusConnection.ExportAdaptors)
    window.open_page(page)
    return app.exec_()

if __name__=='__main__':sys.exit(main())
