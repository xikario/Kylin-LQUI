#!/usr/bin/python3
"""Kylin-LQUI standalone settings; native module callbacks own configuration."""
import json
import math
import sys
from pathlib import Path
from PyQt5 import QtCore, QtGui, QtWidgets, QtDBus
from backend import Backend, MODULES, ROOT

from design import STYLE, COLORS, DESCRIPTIONS, NavigationDelegate, Switch, MaterialPreview, DesktopPreview, DragHeader, ImagePreview, WindowHeader, icon, navigation_text, FlowLayout

from ui_tokens import COLORS as UI_COLORS, application_scale, build_settings_style, sidebar_width
from ui_schema import normalize_page, structural_signature, searchable_text, field_visible, presentation_rows, navigation_pages, pinned_page_row
from ui_components import SettingsGroup, SettingRow, ActionRow, DisclosureRow, PluginRow, WallpaperPreview, WallpaperFileRow, PathView, StatusLabel, SettingSlider, ActionButton, HelpBubble, RadioChoice, ModeChoiceRow, ProfileColumns, label
import sip

_OBSERVED_EVENTS=frozenset((QtCore.QEvent.MouseButtonPress,QtCore.QEvent.KeyPress,QtCore.QEvent.Wheel,QtCore.QEvent.InputMethod,QtCore.QEvent.ApplicationFontChange,QtCore.QEvent.Resize,QtCore.QEvent.FocusIn))

class HelpFilter(QtCore.QObject):
    """Delay helpful descriptions by one second; no native UKUI tooltip arrow."""
    def __init__(self, parent):
        super().__init__(parent)
        self.target = None
        self.timer = QtCore.QTimer(self)
        self.bubble=HelpBubble();self.destroyed.connect(self.bubble.deleteLater)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.show)
    def install(self, widget, text):
        if not text: return
        widget.setProperty('helpText', text)
        widget.installEventFilter(self)
        widget.destroyed.connect(self.destroyed_target)
    @QtCore.pyqtSlot()
    def destroyed_target(self):self.cancel()
    def cancel(self, widget=None):
        if widget is None or self.target is widget:
            self.target=None;self.timer.stop()
            if not sip.isdeleted(self.bubble):self.bubble.hide()
            QtWidgets.QToolTip.hideText()
    def eventFilter(self, obj, event):
        if event.type() == QtCore.QEvent.Enter:
            self.target = obj
            self.timer.start(1000)
        elif event.type() in (QtCore.QEvent.Leave, QtCore.QEvent.MouseButtonPress, QtCore.QEvent.Hide):
            self.cancel()
        elif event.type() == QtCore.QEvent.ToolTip:
            return True
        return False
    def show(self):
        if not sip.isdeleted(self.bubble) and self.target and not sip.isdeleted(self.target) and self.target.isVisible():
            if not self.target.property('helpText'):self.cancel();return
            self.bubble.setFont(self.target.font());self.bubble.setText(self.target.property('helpText'));self.bubble.adjustSize()
            screen=QtWidgets.QApplication.screenAt(QtGui.QCursor.pos()) or QtWidgets.QApplication.primaryScreen();area=screen.availableGeometry();point=QtGui.QCursor.pos()+QtCore.QPoint(8,16)
            self.bubble.move(max(area.left(),min(point.x(),area.right()-self.bubble.width())),max(area.top(),min(point.y(),area.bottom()-self.bubble.height())));self.bubble.show()

class SettingsWindow(QtWidgets.QMainWindow):
    def __init__(self, backend=None, font_scale=None):
        super().__init__()
        self.backend=backend or Backend(self)
        self.current='';self.generation=0;self.revision=0;self.busy=False
        self.fields={};self.drafts={};self.signature='';self.request_serial=0
        self.page_cache={};self.page_group_cache={};self.catalog={};self.collapsed=set(MODULES)|{'fences:layout','fences:widgets'}
        self.view_states={};self._render_serial=0;self._rebuilding=False;self._pending_actions=set()
        self._catalog_requests={}
        self._rejected_navigation_click=None
        self._restoring_generation=None;self._updating_visibility=False;self._font_update_queued=False;self._interaction_serial=0;self._ambiguous_native_edit=False
        self._normalized=None;self._page_data={};self._rendered_rows=[];self.groups=[];self._blocked_signature='';self._deferred_fields=set()
        self._explicit_font_scale=font_scale;self.ui_scale=application_scale(QtWidgets.QApplication.font(),font_scale)
        self._last_app_point=QtWidgets.QApplication.font().pointSizeF();self._last_focused_key=None
        self.setWindowTitle('Kylin-LQUI 设置')
        self.setWindowFlags(QtCore.Qt.Window|QtCore.Qt.FramelessWindowHint|QtCore.Qt.WindowMinimizeButtonHint|QtCore.Qt.WindowMaximizeButtonHint|QtCore.Qt.WindowCloseButtonHint)
        self.setAttribute(QtCore.Qt.WA_TranslucentBackground);self.setWindowIcon(QtGui.QIcon(str(Path(__file__).with_name('icon.svg'))))
        self.resize(1040,740);self.setMinimumSize(720,480)
        screen=QtWidgets.QApplication.screenAt(QtGui.QCursor.pos()) or QtWidgets.QApplication.primaryScreen()
        if screen:
            area=screen.availableGeometry().adjusted(10,10,-10,-10);self.resize(self.size().boundedTo(area.size()));self.move(area.center()-self.rect().center())
        self.apply_ui_style();self.help=HelpFilter(self)
        body=QtWidgets.QWidget();body.setObjectName('body');self.setCentralWidget(body)
        shell=QtWidgets.QVBoxLayout(body);shell.setContentsMargins(1,1,1,1);shell.setSpacing(0)
        self.search=QtWidgets.QLineEdit();self.search.setObjectName('search');self.search.setPlaceholderText('搜索设置');self.search.setAccessibleName('搜索设置')
        self.window_header=WindowHeader(self,self.search);shell.addWidget(self.window_header)
        for button in self.window_header.findChildren(QtWidgets.QToolButton):self.help.install(button,button.toolTip());button.setAccessibleName(button.toolTip())
        split=QtWidgets.QHBoxLayout();split.setSpacing(0);shell.addLayout(split,1)
        self.sidebar=QtWidgets.QFrame();self.sidebar.setObjectName('sidebar')
        left=QtWidgets.QVBoxLayout(self.sidebar);left.setContentsMargins(8,12,8,12);left.setSpacing(8)
        self.navigation=QtWidgets.QListWidget();self.navigation.setObjectName('navigation');self.navigation.setItemDelegate(NavigationDelegate(self.navigation));self.navigation.setMouseTracking(True);self.navigation.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);left.addWidget(self.navigation,1)
        self.add_navigation('总览与模块状态','overview')
        for module,(title,service,glyph) in MODULES.items():self.add_navigation(glyph+'  '+title,module+':general')
        self.add_navigation('帮助与恢复','integration')
        system=ActionButton('UKUI 系统设置');system.clicked.connect(self.open_system);left.addWidget(system)
        split.addWidget(self.sidebar)
        right=QtWidgets.QWidget();self.right_layout=QtWidgets.QVBoxLayout(right);self.right_layout.setContentsMargins(20,16,20,12);self.right_layout.setSpacing(8);split.addWidget(right,1)
        head=QtWidgets.QHBoxLayout();self.title=QtWidgets.QLabel();self.title.setObjectName('title');self.title.setWordWrap(True);head.addWidget(self.title,1)
        self.save_mode=label('',True);head.addWidget(self.save_mode)
        self.open_space=ActionButton('打开 AI 空间');self.open_space.clicked.connect(lambda:self.start_module(show_main=True));self.open_space.hide();head.addWidget(self.open_space)
        refresh=ActionButton('刷新');refresh.clicked.connect(self.reload_page);head.addWidget(refresh);self.right_layout.addLayout(head)
        self.description=label('',True);self.description.setObjectName('subtitle');self.right_layout.addWidget(self.description)
        self.page_actions=QtWidgets.QWidget();self.page_actions.setObjectName('pageActions');self.page_actions.setMaximumWidth(900)
        self.page_actions_layout=QtWidgets.QVBoxLayout(self.page_actions);self.page_actions_layout.setContentsMargins(0,0,0,0);self.page_actions_layout.setSpacing(0)
        self.right_layout.addWidget(self.page_actions);self.page_actions.hide()
        self.scroll=QtWidgets.QScrollArea();self.scroll.setWidgetResizable(True);self.scroll.setAlignment(QtCore.Qt.AlignLeft|QtCore.Qt.AlignTop);self.scroll.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);self.right_layout.addWidget(self.scroll,1)
        self.message=StatusLabel();self.message.setWordWrap(True);self.message.setTextFormat(QtCore.Qt.PlainText);self.message.setProperty('warning',True);self.message.setText('');self.right_layout.insertWidget(self.right_layout.indexOf(self.scroll),self.message)
        self.grip=QtWidgets.QSizeGrip(self);self.grip.setFixedSize(16,16)
        self.navigation.currentItemChanged.connect(self.selection_changed);self.navigation.itemClicked.connect(self.toggle_group)
        self.search.textChanged.connect(self.filter_search)
        self.poll=QtCore.QTimer(self);self.poll.setInterval(1800);self.poll.timeout.connect(self.refresh);self.poll.start()
        self.load_catalogs();self.open_page('overview');self.reflow();QtWidgets.QApplication.instance().installEventFilter(self)

    def apply_ui_style(self):
        self.setStyleSheet(build_settings_style(self.ui_scale)+"QComboBox::down-arrow{image:url("+str(Path(__file__).with_name('down.svg'))+");width:14px;height:14px;}")

    def resizeEvent(self,event):
        super().resizeEvent(event)
        if hasattr(self,'scroll'):self.reflow()

    def changeEvent(self,event):
        super().changeEvent(event)
        if event.type()==QtCore.QEvent.WindowStateChange and hasattr(self,'scroll'):self.reflow()

    def sync_application_font(self):
        if sip.isdeleted(self):return
        self._font_update_queued=False
        point=QtWidgets.QApplication.font().pointSizeF()
        if point==self._last_app_point:return
        state=self.capture_view_state()
        self.ui_scale=application_scale(QtWidgets.QApplication.font(),self.ui_scale*point/self._last_app_point if self._explicit_font_scale is not None else None)
        self._last_app_point=point;self.apply_ui_style();self.reflow();self.restore_view_state(state)

    def reflow(self):
        previous=self.sidebar.width();self.sidebar.setFixedWidth(sidebar_width(self.width(),self.navigation.fontMetrics()))
        if self.sidebar.width()!=previous:self.navigation.doItemsLayout()
        margin=12 if self.width()<900 else 20;self.right_layout.setContentsMargins(margin,16,margin,12)
        available=min(900,self.scroll.viewport().width())
        for record in self._rendered_rows:
            row=record['widget']
            if hasattr(row,'reflow'):row.reflow((available-16)//2-2 if record.get('profile_column') else available-2)
        for group in self.groups:group.refresh_rows(self.current=='peony:general',available)
        for pair in self.scroll.findChildren(ProfileColumns):pair.schedule()
        for entry in self.fields.values():
            if 'readout' in entry:
                f=entry['field'];readout=entry['readout'];strings=[self.number_text(v,f) for v in (f.get('min',0),f.get('max',100),f.get('value',0))]
                readout.setMinimumWidth(max(56,max(readout.fontMetrics().horizontalAdvance(t) for t in strings)+4))
            if isinstance(entry['control'],PathView):entry['control'].elide()
        self.grip.move(self.width()-18,self.height()-18);self.grip.setVisible(not self.isMaximized());self.grip.raise_()

    def paintEvent(self,event):
        painter=QtGui.QPainter(self);painter.setRenderHint(QtGui.QPainter.Antialiasing);painter.setPen(QtGui.QPen(QtGui.QColor(UI_COLORS['border']),1));painter.setBrush(QtGui.QColor(UI_COLORS['window']));painter.drawRoundedRect(QtCore.QRectF(self.rect()).adjusted(.5,.5,-.5,-.5),12,12)

    def view_state(self):
        return {'page':self.current,'loading':self.busy,'fields':len(self.fields),'visible':self.isVisible(),'title':self.title.text(),'error':self.message.text(),'uiDiagnostics':self._normalized.get('diagnostics',[]) if self._normalized else []}

    def toggle_group(self,item):
        if not item:return
        if self._rejected_navigation_click==item.data(QtCore.Qt.UserRole):
            self._rejected_navigation_click=None;return
        subgroup=item.data(QtCore.Qt.UserRole+5)
        if subgroup:
            if subgroup in self.collapsed:self.collapsed.discard(subgroup)
            else:self.collapsed.add(subgroup)
            self.filter_search(self.search.text());return
        group=item.data(QtCore.Qt.UserRole+2)
        if not group:return
        if group in self.collapsed or not self.current.startswith(group+':'):
            self.collapsed.discard(group)
            pages=self.catalog.get(group,[])
            if pages:self.open_page(group+':'+pages[0]['id'])
        else:self.collapsed.add(group)
        self.filter_search(self.search.text())

    def closeEvent(self,event):
        if self._unstable_draft() and not self.confirm_discard():event.ignore();return
        if self.current.startswith('ai:'):self.backend.call('ai','Discard',[self.current.split(':',1)[1]],lambda result:None)
        super().closeEvent(event)

    def eventFilter(self,watched,event):
        kind=event.type()
        if kind not in _OBSERVED_EVENTS:return False
        if kind in (QtCore.QEvent.MouseButtonPress,QtCore.QEvent.KeyPress,QtCore.QEvent.Wheel,QtCore.QEvent.InputMethod) and isinstance(watched,QtWidgets.QWidget) and (watched is self or self.isAncestorOf(watched)):self._interaction_serial+=1
        if kind==QtCore.QEvent.ApplicationFontChange and watched is QtWidgets.QApplication.instance() and not self._font_update_queued:
            self._font_update_queued=True;QtCore.QTimer.singleShot(0,self.sync_application_font)
        if kind==QtCore.QEvent.Resize and hasattr(self,'scroll'):
            if watched is self.scroll.viewport():self.reflow()
            elif watched is self.navigation.viewport():self.navigation.scheduleDelayedItemsLayout()
        if kind==QtCore.QEvent.FocusIn:
            for entry in self.fields.values():
                if (entry['control'] is watched or self.line_editor(entry['control']) is watched) and entry['field'].get('_stable') and not entry['field'].get('secret'):self._last_focused_key=entry['field']['_key']
        if kind==QtCore.QEvent.KeyPress and watched is self.navigation and event.key() in (QtCore.Qt.Key_Return,QtCore.Qt.Key_Enter,QtCore.Qt.Key_Space):
            item=self.navigation.currentItem()
            if item and (item.data(QtCore.Qt.UserRole+2) or item.data(QtCore.Qt.UserRole+5)):self.toggle_group(item);return True
        return False

    def add_navigation(self,title,key):
        item=QtWidgets.QListWidgetItem(title);item.setData(QtCore.Qt.UserRole,key);self.navigation.addItem(item)
        return item

    def reload_page(self):
        self.load_catalogs();self.refresh(force=True)

    def load_catalogs(self):
        for module in MODULES:
            request=self._catalog_requests.get(module,0)+1;self._catalog_requests[module]=request
            self.backend.call(module,'Catalog',[],lambda result,m=module,r=request:self.receive_catalog_reply(m,r,result))

    def receive_catalog_reply(self,module,request,result):
        if sip.isdeleted(self) or self._catalog_requests.get(module)!=request:return
        self.receive_catalog(module,result)

    def receive_catalog(self,module,result):
        if sip.isdeleted(self):return
        if not result.get('ok'):return
        pages=result.get('pages',[])
        if not pages:return
        old=self.current
        removes_current=old.startswith(module+':') and not any(module+':'+p['id']==old for p in pages)
        if removes_current and self._unstable_draft() and not self.confirm_discard():
            self.message.setText('目录已变化；当前未保存内容和导航已保留，完成编辑后再刷新。');return
        old_item=next((self.navigation.item(i) for i in range(self.navigation.count()) if self.navigation.item(i).data(QtCore.Qt.UserRole)==old),None)
        old_ancestors=set(self.navigation_ancestors(old_item)) if old_item else set()
        blocker=QtCore.QSignalBlocker(self.navigation)
        indexes=[i for i in range(self.navigation.count()) if self.navigation.item(i).data(QtCore.Qt.UserRole).startswith(module+':')]
        start=indexes[0] if indexes else self.navigation.count()-1
        for i in reversed(indexes):self.navigation.takeItem(i)
        if len(pages)>1:
            header=QtWidgets.QListWidgetItem(('▸  ' if module in self.collapsed else '▾  ')+MODULES[module][0]);header.setData(QtCore.Qt.UserRole,module+':__group__');header.setData(QtCore.Qt.UserRole+2,module);self.navigation.insertItem(start,header);start+=1
        for i,(page,parent,has_children) in enumerate(navigation_pages(module,pages)):
            key=module+':'+page['id'];title=page.get('title',page['id'])
            if key=='fences:session':title='调试专用'
            label=MODULES[module][2]+'  '+MODULES[module][0] if len(pages)==1 else title.strip()
            item=QtWidgets.QListWidgetItem(label);item.setData(QtCore.Qt.UserRole,key)
            if len(pages)>1:item.setData(QtCore.Qt.UserRole+1,module)
            item.setData(QtCore.Qt.UserRole+3,module+':'+parent if parent else module+':__group__' if len(pages)>1 else '')
            item.setData(QtCore.Qt.UserRole+4,2 if parent else 1 if len(pages)>1 else 0)
            if has_children:item.setData(QtCore.Qt.UserRole+5,key)
            item.setData(QtCore.Qt.UserRole+6,label)
            item.setToolTip(MODULES[module][0]);self.navigation.insertItem(start+i,item)
            if key==old:self.navigation.setCurrentItem(item);self.title.setText(title if len(pages)>1 else MODULES[module][0])
        del blocker
        selected=self.navigation.currentItem()
        if selected and selected.data(QtCore.Qt.UserRole)==old:
            for ancestor in set(self.navigation_ancestors(selected))-old_ancestors:
                self.collapsed.discard(ancestor.split(':')[0] if ancestor.endswith(':__group__') else ancestor)
        self.catalog[module]=pages
        if removes_current:
            self.open_page(module+':'+pages[0]['id'])
        self.filter_search(self.search.text())

    def open_page(self,key):
        module=key.split(':')[0]
        if module not in MODULES and key not in ('overview','integration'):key='overview';module='overview'
        if key in MODULES or key==module+':__group__':key=module+':general'
        if key==module+':general' and self.catalog.get(module) and not any(p['id']=='general' for p in self.catalog[module]):key=module+':'+self.catalog[module][0]['id']
        if key!=self.current and self._unstable_draft() and not self.confirm_discard():return
        self.collapsed.discard(module)
        selected=None
        for i in range(self.navigation.count()):
            item=self.navigation.item(i)
            if item.data(QtCore.Qt.UserRole)==key:selected=item;break
        if selected:
            ancestors=self.navigation_ancestors(selected)
            for ancestor in ancestors:self.collapsed.discard(ancestor.split(':')[0] if ancestor.endswith(':__group__') else ancestor)
            blocker=QtCore.QSignalBlocker(self.navigation);self.navigation.setCurrentItem(selected);del blocker
        title=navigation_text(selected.text()) if selected else MODULES.get(module,(key,))[0]
        self.activate_page(key,title)
        self.filter_search(self.search.text())
        if self.isMinimized():self.showNormal()
        else:self.show()
        self.raise_();self.activateWindow()

    def activate_page(self,key,title):
        if key!=self.current and self._unstable_draft() and not self.confirm_discard():return False
        if self.current:self.view_states[self.current]=self.capture_view_state()
        if self.current.startswith('ai:') and self.current!=key:self.backend.call('ai','Discard',[self.current.split(':',1)[1]],lambda result:None)
        # A page owns its request state. Late responses from the previous page
        # cannot retain the busy flag or overwrite the newly selected content.
        for entry in self.fields.values():
            control=entry['control']
            if isinstance(control,QtWidgets.QPlainTextEdit) and control.document().isModified() and entry['field'].get('_stable') and not entry['field'].get('secret'):self.drafts[(self.current,entry['field']['name'])]=control.toPlainText()
        self.current=key;self.generation+=1;self.request_serial+=1;self.signature='';self.fields={};self.busy=False
        self.clear_page_actions()
        self.groups=[];self._rendered_rows=[];self._normalized=None;self._page_data={};self._last_focused_key=None
        self._ambiguous_native_edit=False;self._deferred_fields.clear();self._blocked_signature=''
        self.poll.setInterval(6000 if key=='overview' else 15000 if key.startswith('peony:') else 2500)
        titles={'fences:appearance':'液态外观','fences:wallpaper':'壁纸','fences:activity':'活动统计','fences:session':'调试专用 · 非调试请勿开启','peony:general':'文件管理','peony:plugins':'Peony 扩展','integration':'帮助与恢复'}
        self.title.setText(titles.get(key,title));self.message.setText('');self.save_mode.setText('');self.open_space.setVisible(key.startswith('space:'))
        descriptions={'fences:appearance':'调整分区玻璃通透档位与动态效果。','fences:wallpaper':'壁纸修改需应用；主题配色按对应按钮立即执行。','fences:activity':'管理应用记录、资源采样与详细日志保留。','ai:general':'外观与日常选项即时生效；后台、命令、目录及权限需点击应用。','input:general':'先安装并启用 Rime；调整参数即可查看示例效果，应用后用于实际输入。','peony:general':'设置由文件管理器即时保存。','peony:plugins':''}
        self.description.setText(descriptions.get(key,DESCRIPTIONS.get(key,'')));self.description.setVisible(bool(self.description.text()))
        saved=self.view_states.get(key,{})
        blocker=QtCore.QSignalBlocker(self.search);self.search.setText(saved.get('search',''));del blocker
        # Avoid replacing two complete widget trees for a fast async reply.
        # The old form is inert immediately; only a slow request gets a loader.
        generation=self.generation
        if self.scroll.widget():self.scroll.widget().setEnabled(False)
        def loading():
            if sip.isdeleted(self):return
            if generation==self.generation and self.busy and not self.fields:self.set_content(QtWidgets.QLabel('正在读取…'))
        self.refresh(force=True)
        QtCore.QTimer.singleShot(80,loading)
        return True

    def selection_changed(self,item,previous):
        if not item or item.data(QtCore.Qt.UserRole+2):return
        self._rejected_navigation_click=None
        if self.activate_page(item.data(QtCore.Qt.UserRole),navigation_text(item.text())) is False:
            key=item.data(QtCore.Qt.UserRole);self._rejected_navigation_click=key
            def clear_rejected_click():
                if not sip.isdeleted(self) and self._rejected_navigation_click==key:self._rejected_navigation_click=None
            QtCore.QTimer.singleShot(0,clear_rejected_click)
            if previous:
                blocker=QtCore.QSignalBlocker(self.navigation);self.navigation.setCurrentItem(previous);del blocker
            return
        for ancestor in self.navigation_ancestors(item):self.collapsed.discard(ancestor.split(':')[0] if ancestor.endswith(':__group__') else ancestor)
        self.filter_search(self.search.text())

    def clear_page_actions(self):
        while self.page_actions_layout.count():
            widget=self.page_actions_layout.takeAt(0).widget()
            if widget:widget.hide();widget.deleteLater()
        self.page_actions.hide()

    def set_content(self,widget):
        old=self.scroll.takeWidget()
        if old:old.deleteLater()
        self.scroll.setWidget(widget);widget.setAutoFillBackground(False);self.scroll.viewport().setAutoFillBackground(False)

    def refresh(self,force=False):
        if (self.busy and not force) or (not force and (not self.isVisible() or self.isMinimized())):return
        self.request_serial+=1;request=self.request_serial
        generation=self.generation;revision=self.revision;key=self.current;self.busy=True
        def receive(result):
            if sip.isdeleted(self):return
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

    @staticmethod
    def line_editor(control):
        if isinstance(control,QtWidgets.QLineEdit):return control
        if isinstance(control,QtWidgets.QComboBox) and control.isEditable():return control.lineEdit()
        return None

    @staticmethod
    def modified(control):
        return control.isModified() if isinstance(control,QtWidgets.QLineEdit) else control.document().isModified() if isinstance(control,QtWidgets.QPlainTextEdit) else control.lineEdit().isModified() if isinstance(control,QtWidgets.QComboBox) and control.isEditable() else False

    def _unstable_draft(self):
        if any(self.modified(e['control']) and (not e['field'].get('_stable') or e['field'].get('secret')) for e in self.fields.values()):return True
        return self._ambiguous_native_edit and bool(self._page_data.get('dirty'))

    def confirm_discard(self):
        dialog=QtWidgets.QMessageBox(self);dialog.setWindowTitle('保留当前编辑');dialog.setText('当前页面包含无法安全跨页面恢复的未保存内容。')
        stay=dialog.addButton('继续编辑',QtWidgets.QMessageBox.RejectRole);leave=dialog.addButton('放弃修改并离开',QtWidgets.QMessageBox.DestructiveRole);dialog.setDefaultButton(stay)
        previous=self._rebuilding;self._rebuilding=True
        try:dialog.exec_()
        finally:self._rebuilding=previous
        if dialog.clickedButton() is not leave:return False
        self._ambiguous_native_edit=False
        for entry in self.fields.values():
            control=entry['control']
            editor=self.line_editor(control)
            if editor:editor.setModified(False)
            elif isinstance(control,QtWidgets.QPlainTextEdit):control.document().setModified(False)
        if ':' in self.current and not self.current.startswith('ai:'):
            module,page=self.current.split(':',1);self.backend.call(module,'Discard',[page],lambda result:None)
        return True

    def capture_view_state(self):
        state={'fields':{},'expanded':{},'search':self.search.text(),'scroll':self.scroll.verticalScrollBar().value(),'focus':self._last_focused_key}
        for entry in self.fields.values():
            f=entry['field'];control=entry['control']
            if f.get('secret'):
                if control is QtWidgets.QApplication.focusWidget() or control.isAncestorOf(QtWidgets.QApplication.focusWidget()):state['focus']=None
                continue
            if not f.get('_stable'):continue
            data={'kind':f['kind']};editor=self.line_editor(control)
            if editor:
                data.update(modified=editor.isModified(),cursor=editor.cursorPosition(),selection=editor.selectionStart(),selectionLength=len(editor.selectedText().encode('utf-16-le'))//2)
                if editor.isModified():data['text']=editor.text()
            elif isinstance(control,QtWidgets.QPlainTextEdit):
                cursor=control.textCursor();data.update(modified=control.document().isModified(),cursor=cursor.position(),anchor=cursor.anchor())
                if control.document().isModified():data['text']=control.toPlainText()
            elif isinstance(control,QtWidgets.QListWidget):
                data.update(selected=[i.text() for i in control.selectedItems()],current=control.currentItem().text() if control.currentItem() else None)
            state['fields'][f['_key']]=data
        for record in self._rendered_rows:
            row=record['widget']
            if hasattr(row,'user_expanded'):state['expanded'][str(record['spec']['key'])]=row.user_expanded
            for f in record['spec']['fields']:
                disclosure=self.fields[f['id']].get('disclosure')
                if disclosure:state['expanded']['field:'+str(f['_key'])]=disclosure.user_expanded
        for entry in self.fields.values():
            f=entry['field'];row=entry['row']
            if f.get('_stable') and not f.get('secret') and not f.get('_absorbed') and not row.isHidden() and self.scroll.isAncestorOf(row):
                y=row.mapTo(self.scroll.viewport(),QtCore.QPoint()).y()
                if y<=8 and y+row.height()>0:state['anchor']=(f['_key'],y);break
        return state

    def restore_view_state(self,state):
        self._restoring_generation=self.generation;restored_text={}
        for entry in self.fields.values():
            f=entry['field'];control=entry['control'];saved=state.get('fields',{}).get(f.get('_key'))
            if not saved or f.get('secret') or saved.get('kind')!=f['kind']:continue
            blocker=QtCore.QSignalBlocker(control);editor=self.line_editor(control)
            if editor:
                if saved.get('modified'):
                    if editor.text()!=saved['text']:editor.setText(saved['text'])
                    editor.setModified(True)
                editor.setCursorPosition(min(saved.get('cursor',0),len(editor.text().encode('utf-16-le'))//2))
                if saved.get('selection',-1)>=0:editor.setSelection(saved['selection'],saved.get('selectionLength',0))
                restored_text[f['_key']]=editor.text()
            elif isinstance(control,QtWidgets.QPlainTextEdit):
                if saved.get('modified'):
                    if control.toPlainText()!=saved['text']:control.setPlainText(saved['text'])
                    control.document().setModified(True)
                cursor=control.textCursor();size=len(control.toPlainText().encode('utf-16-le'))//2;cursor.setPosition(min(saved.get('anchor',0),size));cursor.setPosition(min(saved.get('cursor',0),size),QtGui.QTextCursor.KeepAnchor);control.setTextCursor(cursor)
                restored_text[f['_key']]=control.toPlainText()
            elif isinstance(control,QtWidgets.QListWidget):
                for i in range(control.count()):
                    item=control.item(i);item.setSelected(item.text() in saved.get('selected',[]))
                    if item.text()==saved.get('current'):control.setCurrentItem(item,QtCore.QItemSelectionModel.NoUpdate)
            del blocker
        for record in self._rendered_rows:
            row=record['widget'];key=str(record['spec']['key'])
            if hasattr(row,'set_expanded') and key in state.get('expanded',{}):row.set_expanded(state['expanded'][key])
            for f in record['spec']['fields']:
                disclosure=self.fields[f['id']].get('disclosure');key='field:'+str(f['_key'])
                if disclosure and key in state.get('expanded',{}):disclosure.set_expanded(state['expanded'][key])
        self.filter_search(self.search.text())
        generation=self.generation;revision=self.revision;interaction=self._interaction_serial
        self._restoring_generation=None
        def finish():
            if sip.isdeleted(self):return
            if generation!=self.generation or revision!=self.revision or interaction!=self._interaction_serial:return
            self._restoring_generation=generation
            bar=self.scroll.verticalScrollBar();bar.setValue(state.get('scroll',0))
            if state.get('anchor'):
                key,y=state['anchor']
                for entry in self.fields.values():
                    if entry['field'].get('_key')==key and self.scroll.isAncestorOf(entry['row']):
                        now=entry['row'].mapTo(self.scroll.viewport(),QtCore.QPoint()).y();bar.setValue(bar.value()+now-y);break
            for entry in self.fields.values():
                if entry['field'].get('_key')==state.get('focus') and entry['control'].isVisible() and entry['control'].isEnabled():entry['control'].setFocus();break
            # QComboBox's final layout/focus pass may clear its line selection.
            # Restore cursor geometry only if the user has not edited since.
            for entry in self.fields.values():
                f=entry['field'];control=entry['control'];saved=state.get('fields',{}).get(f.get('_key'));editor=self.line_editor(control)
                if not saved or f.get('secret') or f.get('_key') not in restored_text:continue
                if editor is not None and editor.text()==restored_text[f['_key']]:
                    editor.setCursorPosition(min(saved.get('cursor',0),len(editor.text().encode('utf-16-le'))//2))
                    if saved.get('selection',-1)>=0:editor.setSelection(saved['selection'],saved.get('selectionLength',0))
                elif isinstance(control,QtWidgets.QPlainTextEdit) and control.toPlainText()==restored_text[f['_key']]:
                    cursor=control.textCursor();size=len(control.toPlainText().encode('utf-16-le'))//2;cursor.setPosition(min(saved.get('anchor',0),size));cursor.setPosition(min(saved.get('cursor',0),size),QtGui.QTextCursor.KeepAnchor);control.setTextCursor(cursor)
            self._restoring_generation=None
        QtCore.QTimer.singleShot(0,finish)

    def receive_page(self,result):
        if not result.get('ok'):
            self.message.setText(result.get('error','设置暂不可用'));self.save_mode.setText('读取失败')
            if not self.fields:
                box=QtWidgets.QWidget();layout=QtWidgets.QVBoxLayout(box);layout.addWidget(label('模块未就绪，请启动对应模块后刷新。'))
                start=ActionButton('打开 AI 空间并加载设置' if self.current.startswith('space:') else '启动模块');start.clicked.connect(lambda:self.start_module(show_main=True));layout.addWidget(start)
                if self.current.startswith('space:'):
                    load=ActionButton('只加载设置');load.clicked.connect(lambda:self.start_module(show_main=False));layout.addWidget(load)
                layout.addStretch();self.set_content(box)
            return
        fields=result.get('fields',[])
        if not fields and self.fields and any(self.modified(e['control']) for e in self.fields.values()):
            self.message.setText('此页面暂未提供设置，当前文字草稿已保留；完成编辑后再刷新。');return
        module,page=self.current.split(':',1)
        normalized=normalize_page(result,module,page,self._render_serial)
        self.page_cache[self.current]=normalized['fields']
        self.page_group_cache[self.current]=[g.get('title','') for g in normalized['groups']]
        signature=structural_signature(normalized)
        focused=QtWidgets.QApplication.focusWidget()
        # Native combo popups move focus to their QAbstractItemView, which is
        # a separate window. Inspect the owned combo's popup, not focus type.
        popup_open=any(isinstance(entry['control'],QtWidgets.QComboBox) and entry['control'].view().isVisible() for entry in self.fields.values())
        editing=popup_open or focused and self.scroll.isAncestorOf(focused) and (isinstance(focused,(QtWidgets.QLineEdit,QtWidgets.QPlainTextEdit)) or isinstance(focused,QtWidgets.QSlider) and focused.isSliderDown())
        self._page_data={**result,'fields':normalized['fields']}
        if not result.get('dirty'):self._ambiguous_native_edit=False
        if signature!=self.signature:
            if editing:return
            if self._unstable_draft():
                if self._blocked_signature==signature or not self.confirm_discard():
                    self._blocked_signature=signature;self.message.setText('页面内容已变化，当前草稿已保留；完成编辑后再刷新。');return
            self._blocked_signature=''
            state=self.capture_view_state() if self.fields else self.view_states.get(self.current,{})
            self.view_states[self.current]=state
            self.signature=signature;self.render_fields(fields,result);self.restore_view_state(state)
        else:
            changed=normalized['fields']!=self._normalized['fields']
            self._normalized=normalized
            updated=False;previous=self._rebuilding;self._rebuilding=True
            try:
                # Disabling an editor can synchronously emit editingFinished.
                # A server update must never submit the user's local draft.
                for field in normalized['fields']:
                    if changed or field['id'] in self._deferred_fields:updated=self.update_field(field) or updated
            finally:self._rebuilding=previous
            if changed or updated:self.refresh_semantic_rows()
        self.update_save_mode(result)
        self.message.setText('此页有未应用修改，请点击对应的“应用”或“保存”。' if result.get('dirty') else '')
        if any(isinstance(e['control'],QtWidgets.QPlainTextEdit) and e['control'].document().isModified() for e in self.fields.values()):self.message.setText('文字修改尚未更新，请点“更新”。')
        if not fields:self.message.setText('此页面暂未提供设置，请刷新或检查模块状态。')
        if result.get('modalActive'):self.message.setText('请先完成当前的确认或文件选择窗口。');self.save_mode.setText('等待确认')

    def update_save_mode(self,result):
        modes={'fences:activity':'即时保存','peony:general':'即时保存','peony:terminal':'即时保存','fences:wallpaper':'部分设置需应用',
               'panel:material':'即时生效','panel:layout':'即时生效','panel:effects':'即时生效','ai:general':'部分设置需应用','input:general':'预览后应用'}
        self.save_mode.setText('有待应用修改' if result.get('dirty') else modes.get(self.current,''))

    def card(self,layout):
        frame=QtWidgets.QFrame();frame.setProperty('card',True);box=QtWidgets.QVBoxLayout(frame);box.setContentsMargins(12,8,12,8);box.setSpacing(8);layout.addWidget(frame);return box

    @staticmethod
    def set_choice_value(control,field):
        value=field.get('value',0);index=-1
        if type(value) is int or type(value) is float and math.isfinite(value) and value.is_integer():
            index=int(value)
        elif isinstance(value,str):
            # Editable choices return literal custom text in some providers;
            # numeric text must not silently select an unrelated option index.
            if control.isEditable():index=control.findText(value)
            else:
                try:index=int(value)
                except ValueError:index=control.findText(value)
        control.setCurrentIndex(index if 0<=index<control.count() else -1)
        if control.isEditable():
            if 'text' in field:control.setEditText(str(field['text']))
            elif isinstance(value,str):control.setEditText(value)

    def _create_control(self,field,canvas):
        kind=field.get('kind');name=field.get('label') or '设置项';fid=field['id'];key=self.current
        entry={'field':field,'label':None};control=None;container=None
        if field.get('_unsupported'):
            control=label('暂不支持的设置类型：'+str(kind));control.setProperty('warning',True)
        elif kind in ('note','heading'):
            control=PathView(field.get('value','')) if field['_ui'].get('component')=='file-picker' else label(field.get('value',''),kind=='note')
            control.setProperty('warning',field['_role']=='warning')
        elif kind=='preview':control=WallpaperPreview(field.get('value',''),field.get('previewStatus','') or ('随参数更新的示例预览' if key.startswith('input:') else ''),field['_ui'].get('alt') or name)
        elif kind in ('action','toggle'):
            control=ActionButton(name)
            control.setCheckable(kind=='toggle');control.setChecked(bool(field.get('value')))
            control.setProperty('primary',field['_ui'].get('emphasis')=='primary' or name in ('应用','保存','保存设置','应用壁纸'))
            control.setProperty('destructive',field['_ui'].get('emphasis')=='destructive')
            control.clicked.connect(lambda checked=False,f=fid,k=key,c=control:self.mutate(f,activate=True,page_key=k,source=c))
        elif kind=='boolean':
            control=Switch(name);control.setChecked(bool(field.get('value')))
            control.clicked.connect(lambda value,f=fid,k=key,c=control:self.mutate(f,value,page_key=k,source=c))
        elif kind=='number':
            control=SettingSlider();control.setMinimumWidth(60)
            step=max(float(field.get('step',1)),.0001);low=float(field.get('min',0));high=float(field.get('max',100));control.setRange(0,max(0,min(1000000,round((high-low)/step))))
            entry.update(low=low,step=step)
            readout=label();readout.setWordWrap(False);readout.setAlignment(QtCore.Qt.AlignRight|QtCore.Qt.AlignVCenter);entry['readout']=readout
            def changed(value,e=entry):e['readout'].setText(self.number_text(e['low']+value*e['step'],e['field']))
            control.valueChanged.connect(changed);control.setValue(round((float(field.get('value',low))-low)/step));changed(control.value())
            control.committed.connect(lambda f=fid,e=entry,c=control,k=key:self.mutate(f,round(e['low']+c.sliderPosition()*e['step'],int(e['field'].get('decimals',0))),page_key=k,source=c))
            container=QtWidgets.QWidget();line=QtWidgets.QHBoxLayout(container);line.setContentsMargins(0,0,0,0);line.setSpacing(8);line.addWidget(control,1);line.addWidget(readout)
        elif kind=='choice' and field.get('_radio_choices'):
            control=RadioChoice(field.get('options',[]),field.get('optionsEnabled',[]));self.set_choice_value(control,field)
            control.activated.connect(lambda value,f=fid,k=key,c=control:self.mutate(f,value,page_key=k,source=c))
        elif kind in ('choice','tabs'):
            control=QtWidgets.QComboBox();control.setSizeAdjustPolicy(QtWidgets.QComboBox.AdjustToMinimumContentsLengthWithIcon);control.setMinimumContentsLength(1);control.setMinimumWidth(0);control.setSizePolicy(QtWidgets.QSizePolicy.Expanding,QtWidgets.QSizePolicy.Fixed)
            control.addItems(field.get('options',[]))
            for index,allowed in enumerate(field.get('optionsEnabled',[])):
                if control.model().item(index):control.model().item(index).setEnabled(allowed)
            control.setEditable(bool(field.get('editable')))
            self.set_choice_value(control,field)
            control.activated[int].connect(lambda value,f=fid,k=key,c=control:self.mutate(f,value,page_key=k,source=c))
            if control.isEditable():control.lineEdit().editingFinished.connect(lambda f=fid,c=control,k=key:self.mutate(f,c.currentText(),page_key=k,source=c) if c.lineEdit().isModified() else None)
        elif kind=='text':
            control=QtWidgets.QLineEdit(str(field.get('value','')));control.setMinimumWidth(0);control.setPlaceholderText(field.get('placeholder',''));control.setMaxLength(field.get('maxLength',32767));control.setReadOnly(bool(field.get('readOnly')))
            if field.get('secret'):control.setEchoMode(QtWidgets.QLineEdit.Password)
            control.editingFinished.connect(lambda f=fid,c=control,k=key:self.mutate(f,c.text(),page_key=k,source=c) if c.isModified() else None)
            if field['_ui'].get('component')=='file-picker' and not field.get('secret'):
                container=QtWidgets.QWidget();line=QtWidgets.QHBoxLayout(container);line.setContentsMargins(0,0,0,0);line.setSpacing(4)
                copy=QtWidgets.QToolButton();copy.setText('复制路径');copy.setAccessibleName('复制完整路径');copy.clicked.connect(lambda checked=False,c=control:QtWidgets.QApplication.clipboard().setText(c.text()))
                control.textChanged.connect(lambda text,c=control,b=copy:(c.setToolTip(text),b.setEnabled(bool(text))));copy.setEnabled(bool(control.text()));control.setToolTip(control.text());line.addWidget(control,1);line.addWidget(copy)
        elif kind=='multiline':
            control=QtWidgets.QPlainTextEdit(str(field.get('value','')));control.setMinimumWidth(0);control.setReadOnly(bool(field.get('readOnly')));control.setMinimumHeight(max(100,round(100*self.ui_scale)));control.setMaximumHeight(round(240*self.ui_scale))
            if field.get('_stable') and not field.get('secret'):
                draft=self.drafts.get((key,field['name']))
                if draft is not None:control.setPlainText(draft);control.document().setModified(True)
            container=QtWidgets.QWidget();box=QtWidgets.QVBoxLayout(container);box.setContentsMargins(0,0,0,0);box.setSpacing(8);box.addWidget(control)
            if not field.get('readOnly'):
                save=ActionButton('更新');save.clicked.connect(lambda checked=False,f=fid,c=control,k=key:self.mutate(f,c.toPlainText(),page_key=k,source=c));row=QtWidgets.QHBoxLayout();row.addStretch();row.addWidget(save);box.addLayout(row)
        elif kind=='list':
            control=QtWidgets.QListWidget();control.setMinimumWidth(0);control.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection if field.get('multiple') else QtWidgets.QAbstractItemView.SingleSelection);control.setMinimumHeight(min(240,max(90,len(field.get('items',[]))*32+18)));control.setMaximumHeight(300);self.populate_list(control,field)
            control.itemSelectionChanged.connect(lambda f=fid,c=control,k=key:self.send_selection(f,c,page_key=k))
            control.itemChanged.connect(lambda item,f=fid,c=control,k=key:self.mutate(f,{'row':c.row(item),'checked':item.checkState()==QtCore.Qt.Checked},page_key=k,source=c))
        else:control=label('此设置类型暂不支持。')
        if container is None:container=control
        control.setObjectName(field.get('name') or 'field_'+str(fid));control.setEnabled(field.get('enabled',True) and (key,fid) not in self._pending_actions);control.setAccessibleName(name);control.setAccessibleDescription(field.get('help',''))
        self.help.install(control,field.get('help') or name)
        if isinstance(control,PathView):self.help.install(control.path,control.full_path)
        entry.update(control=control,container=container,row=container)
        return entry

    def render_fields(self,fields,page_data=None):
        self._rebuilding=True;self._render_serial+=1;self._deferred_fields.clear()
        module,page=self.current.split(':',1) if ':' in self.current else ('','')
        raw=page_data if page_data is not None else {'fields':fields}
        self._normalized=normalize_page(raw,module,page,self._render_serial)
        widget=QtWidgets.QWidget(self.scroll.viewport());widget.setObjectName('pageCanvas');widget.setMaximumWidth(900);widget.setMinimumWidth(0);widget.resize(min(900,self.scroll.viewport().width()),self.scroll.viewport().height())
        layout=QtWidgets.QVBoxLayout(widget);layout.setContentsMargins(0,0,0,0);layout.setSpacing(16)
        self.fields={};self.groups=[];self._rendered_rows=[]
        self.clear_page_actions()
        profile_pair=None
        pinned=SettingsGroup();pinned.setObjectName('pinnedPageActions');pinned.set_unframed();self.page_actions_layout.addWidget(pinned)
        for field in self._normalized['fields']:
            entry=self._create_control(field,widget);self.fields[field['id']]=entry
            if field.get('_absorbed'):entry['container'].hide()
        for spec in self._normalized['groups']:
            group=SettingsGroup(spec.get('title',''),spec.get('columns',1))
            if 'profileColumn' in spec and profile_pair is None:
                profile_pair=ProfileColumns();layout.addWidget(profile_pair)
            for field in spec.get('sourceHeadings',[]):
                entry=self.fields[field['id']];entry['container'].deleteLater();entry.update(control=group.title,container=group.title,row=group)
                entry['header']=group.title if spec['key'].startswith('@legacy:') else None
            for row in (item for source_row in spec['rows'] for item in presentation_rows(source_row)):
                entries=[self.fields[f['id']] for f in row['fields']];controls=[e['container'] for e in entries]
                if row.get('entity'):
                    title=next((str(e['field'].get('value','')) for e in entries if e['field']['kind']=='heading'),'扩展')
                    toggle=next((e['control'] for e in entries if e['field']['kind']=='boolean'),None)
                    details=QtWidgets.QWidget();box=QtWidgets.QVBoxLayout(details);box.setContentsMargins(0,0,0,0);box.setSpacing(8)
                    for e in entries:
                        if e['field']['_role']=='detail':box.addWidget(e['container'])
                        elif e['field']['kind']!='boolean':e['container'].hide()
                    view=PluginRow(title,'',details,toggle)
                elif any(e['field']['kind']=='preview' for e in entries) and len(entries)>1:
                    preview=next(e['container'] for e in entries if e['field']['kind']=='preview')
                    files=QtWidgets.QWidget();box=QtWidgets.QVBoxLayout(files);box.setContentsMargins(0,0,0,0);box.setSpacing(8);actions=None
                    for e in entries:
                        f=e['field']
                        if f['kind']=='preview':continue
                        if f['kind']=='action':
                            if actions is None:actions=ActionRow();box.addWidget(actions)
                            actions.add_action(e['control']);e['action_container']=actions
                        else:
                            actions=None
                            if f['_role']=='detail' and f['_ui'].get('collapsible'):
                                disclosure=DisclosureRow(f.get('label') or '静态回退图片',e['container']);box.addWidget(disclosure);e['disclosure']=disclosure
                            else:box.addWidget(e['container'])
                    box.addStretch();view=WallpaperFileRow(preview,files)
                elif row['actions']:
                    view=ActionRow()
                    for e in entries:view.add_action(e['control'])
                elif all(e['field']['kind'] in ('note','heading') for e in entries):
                    content=QtWidgets.QWidget();box=QtWidgets.QVBoxLayout(content);box.setContentsMargins(0,0,0,0);box.setSpacing(8)
                    for e in entries:box.addWidget(e['container'])
                    if all(e['field']['_role']=='detail' for e in entries) and any(e['field']['_ui'].get('collapsible') for e in entries):
                        view=DisclosureRow(entries[0]['field'].get('label') or '补充说明',content)
                    else:
                        view=QtWidgets.QWidget();box=QtWidgets.QVBoxLayout(view);box.setContentsMargins(12,8,12,8);box.addWidget(content)
                elif isinstance(entries[0]['control'],RadioChoice):
                    e=entries[0];view=ModeChoiceRow(e['field'].get('label') or '默认问答模式',e['container']);e['label']=view.title
                    for attached in row['attachments']:
                        entry=self.fields[attached['id']];entry['container'].deleteLater();entry.update(control=view.help,container=view.help,row=view)
                else:
                    e=entries[0];f=e['field'];view=SettingRow(f.get('label') or '设置项',e['container'],boolean=f['kind']=='boolean')
                    e['label']=view.title
                    for attached in row['attachments']:
                        entry=self.fields[attached['id']];entry['container'].deleteLater();presenter=view.title if attached['_role']=='label' else view.help;entry.update(control=presenter,container=presenter,row=view)
                sticky=pinned_page_row(row)
                if 'profileColumn' in spec and isinstance(view.layout(),QtWidgets.QVBoxLayout):view.layout().addStretch(1)
                owner=pinned if sticky else group;owner.add_row(view)
                if 'profileColumn' in spec:profile_pair.add_profile_row(row['profileSlot'],spec['profileColumn'],view)
                record={'widget':view,'spec':row,'group':owner,'pinned':sticky,'profile_column':'profileColumn' in spec};self._rendered_rows.append(record)
                for e in entries:e['row']=e['control'] if row['actions'] else view
            if group.rows:
                self.groups.append(group)
                if 'profileColumn' in spec:
                    group.setObjectName('normalProfilePanel' if spec['profileColumn']==0 else 'quickProfilePanel')
                    profile_pair.add_group(group,spec['profileColumn'])
                else:layout.addWidget(group)
            else:group.setParent(widget);group.hide()
            if spec['rows'] and all(row['actions'] for row in spec['rows']) and not spec.get('title'):group.set_unframed()
        if pinned.rows:self.groups.append(pinned)
        for field in self._normalized['fields']:
            if field.get('_absorbed')=='page-title':
                entry=self.fields[field['id']];entry['container'].deleteLater();entry.update(control=widget,container=widget,row=widget)
        for entry in self.fields.values():
            if entry['container'].parentWidget() is None:entry['container'].setParent(widget)
        self.no_results=QtWidgets.QWidget();empty=QtWidgets.QHBoxLayout(self.no_results);empty.setContentsMargins(0,0,0,0);empty.addWidget(label('未找到相关设置'),1);clear=ActionButton('清除搜索');clear.clicked.connect(self.search.clear);empty.addWidget(clear);layout.addWidget(self.no_results);self.no_results.hide()
        layout.addStretch();self.set_content(widget);self._rebuilding=False
        self.page_actions.setVisible(bool(pinned.rows))
        self.refresh_semantic_rows();self.reflow()

    def refresh_semantic_rows(self):
        for record in self._rendered_rows:
            view=record['widget'];spec=record['spec'];fields=[self.fields[f['id']]['field'] for f in spec['fields']]
            spec['fields']=fields;spec['attachments']=[self.fields[f['id']]['field'] for f in spec['attachments']]
            if isinstance(view,(SettingRow,ModeChoiceRow)):
                f=fields[0];attached=[self.fields[f['id']]['field'] for f in spec['attachments']]
                texts=[f.get('help','')] if f.get('help') and f['help']!=f.get('label') else []
                texts += [str(item.get('value','')) for item in attached if item['_role']!='label']
                title=next((str(item.get('value','')) for item in attached if item['_role']=='label'),f.get('label','设置项'))
                view.title.setText(title);view.title.setEnabled(f.get('enabled',True));view.set_help('\n'.join(filter(None,texts)))
                view.control.setAccessibleDescription('\n'.join(filter(None,texts)));view.control.setProperty('helpText','\n'.join(filter(None,texts)) or title)
            elif isinstance(view,PluginRow):
                view.caption=next((str(f.get('value','')) for f in fields if f['kind']=='heading'),'扩展');view.name.setText(view.caption)
                view.state.setText(next((str(f.get('value','')) for f in fields if str(f.get('name','')).startswith('state:')),'状态未知' if not any(f['kind']=='boolean' for f in fields) else '已安装'))
                notice='\n'.join(str(f.get('value','')) for f in fields if str(f.get('name','')).startswith('unknown:'));view.notice.setText(notice);view.notice.setVisible(bool(notice))
                view.button.setAccessibleName(view.caption+('，已展开' if view.button.isChecked() else '，已折叠'))
        self.filter_search(self.search.text())

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
        if row>=0:self.mutate(fid,{'row':row,'selectedRows':[control.row(item) for item in control.selectedItems()]},page_key=page_key,source=control)

    def update_field(self,field):
        entry=self.fields.get(field['id'])
        if not entry:return False
        control=entry['control'];entry['field']=field
        if field.get('_absorbed'):
            if field['_absorbed']=='group-title' and entry.get('header') is not None:entry['header'].setText(str(field.get('value','')))
            self._deferred_fields.discard(field['id']);return True
        control.setEnabled(field.get('enabled',True) and (self.current,field['id']) not in self._pending_actions)
        focus=QtWidgets.QApplication.focusWidget();editor=self.line_editor(control)
        text_focus=(editor is not None or isinstance(control,QtWidgets.QPlainTextEdit)) and focus is not None and (focus is control or control.isAncestorOf(focus))
        if text_focus or self.modified(control) or isinstance(control,QtWidgets.QSlider) and control.isSliderDown() or isinstance(control,QtWidgets.QComboBox) and control.view().isVisible() or isinstance(control,QtWidgets.QListWidget) and focus is control:
            self._deferred_fields.add(field['id']);return False
        blocker=QtCore.QSignalBlocker(control);value=field.get('value');kind=field['kind']
        if kind in ('boolean','toggle'):control.setChecked(bool(value))
        elif kind=='number':
            control.setValue(round((float(value)-entry['low'])/entry['step']));entry['readout'].setText(self.number_text(value,field))
        elif kind in ('choice','tabs'):
            self.set_choice_value(control,field)
        elif kind in ('note','text','heading'):
            if isinstance(control,PathView):control.set_value(value or '')
            else:control.setText(str(value or ''))
            if kind=='heading' and entry.get('header') is not None:entry['header'].setText(str(value or ''))
        elif kind=='multiline' and not control.document().isModified():control.setPlainText(str(value or ''))
        elif kind=='list':self.populate_list(control,field)
        elif kind=='preview':control.set_data(value or '',field.get('previewStatus','') or ('随参数更新的示例预览' if self.current.startswith('input:') else ''))
        del blocker
        self._deferred_fields.discard(field['id']);return True

    def mutate(self,fid,value=None,activate=False,page_key=None,source=None):
        if self._rebuilding or self._updating_visibility or self._restoring_generation==self.generation:return
        entry=self.fields.get(fid)
        if source is not None and (not entry or entry['control'] is not source or (page_key and page_key!=self.current)):return
        if activate and (page_key or self.current,fid) in self._pending_actions:return
        if activate and entry and entry['field'].get('confirm'):
            if QtWidgets.QMessageBox.question(self,'确认操作',entry['field']['confirm'])!=QtWidgets.QMessageBox.Yes:return
        key=page_key or self.current;module,page=key.split(':',1);generation=self.generation
        if not activate and entry and not entry['field'].get('_stable') and value!=entry['field'].get('value'):self._ambiguous_native_edit=True
        draft_key=(key,entry['field']['name']) if entry and entry['field'].get('_stable') and not entry['field'].get('secret') and isinstance(entry['control'],QtWidgets.QPlainTextEdit) else None
        visible=key==self.current
        if visible:
            self.revision+=1;self.request_serial+=1;self.busy=True
            if entry:self._deferred_fields.add(fid)
        if visible:self.message.setText('正在处理…');self.save_mode.setText('正在处理')
        revision=self.revision
        if activate:
            self._pending_actions.add((key,fid))
            if entry:entry['control'].setEnabled(False)
        submitted_control=entry['control'] if entry else None
        submitted_text=submitted_control.toPlainText() if isinstance(submitted_control,QtWidgets.QPlainTextEdit) else self.line_editor(submitted_control).text() if self.line_editor(submitted_control) is not None else None
        def done(result):
            if sip.isdeleted(self):return
            if activate:
                self._pending_actions.discard((key,fid))
                current=self.fields.get(fid)
                if key==self.current and current and current['field']['kind'] in ('action','toggle'):
                    current['control'].setEnabled(current['field'].get('enabled',True));self._deferred_fields.add(fid)
            if not visible or generation!=self.generation or revision!=self.revision:return
            self.busy=False
            current_entry=self.fields.get(fid)
            entry=current_entry
            current_control=current_entry['control'] if current_entry else None
            if submitted_text is not None:
                if current_control is not submitted_control:return
                current_text=current_control.toPlainText() if isinstance(current_control,QtWidgets.QPlainTextEdit) else self.line_editor(current_control).text()
                if current_text!=submitted_text:return
            if result.get('ok') and draft_key and self.drafts.get(draft_key,submitted_text)==submitted_text:self.drafts.pop(draft_key,None)
            if not result.get('ok'):
                if entry:entry['control'].setEnabled(entry['field'].get('enabled',True))
                self.message.setText('保存失败，请检查凭据后重试。' if entry and entry['field'].get('secret') else result.get('error','保存失败'));self.save_mode.setText('操作失败');return
            entry=self.fields.get(fid)
            if entry:
                control=entry['control']
                control.setEnabled(entry['field'].get('enabled',True))
                editor=self.line_editor(control)
                if editor:editor.setModified(False)
                if isinstance(control,QtWidgets.QPlainTextEdit):control.document().setModified(False)
            if 'fields' in result:self.receive_page(result)
            else:
                def refresh_after_action():
                    if sip.isdeleted(self):return
                    if generation==self.generation and revision==self.revision and self.isVisible():self.refresh(force=True)
                QtCore.QTimer.singleShot(100,refresh_after_action)
        args=[page,fid] if activate else [page,fid,json.dumps({'value':value},ensure_ascii=False)]
        self.backend.call(module,'Activate' if activate else 'Set',args,done)

    def filter_search(self,query):
        previous=self._updating_visibility;self._updating_visibility=True
        try:self._filter_search(query)
        finally:self._updating_visibility=previous

    def navigation_ancestors(self,item,items=None):
        if items is None:items={self.navigation.item(i).data(QtCore.Qt.UserRole):self.navigation.item(i) for i in range(self.navigation.count())}
        result=[];parent=item.data(QtCore.Qt.UserRole+3)
        while parent and parent not in result:
            result.append(parent);parent=items[parent].data(QtCore.Qt.UserRole+3) if parent in items else ''
        return result

    def _filter_search(self,query):
        query=query.strip().casefold()
        navigation_items={self.navigation.item(i).data(QtCore.Qt.UserRole):self.navigation.item(i) for i in range(self.navigation.count())}
        matches_by_key={}
        for i in range(self.navigation.count()):
            item=self.navigation.item(i);key=item.data(QtCore.Qt.UserRole);cached=self.page_cache.get(key,[])
            matches=not query or query in item.text().casefold() or any(query in searchable_text(f) for f in cached) or any(query in title.casefold() for title in self.page_group_cache.get(key,[]))
            group=item.data(QtCore.Qt.UserRole+2)
            if group:item.setText(('▸  ' if group in self.collapsed else '▾  ')+MODULES[group][0])
            subgroup=item.data(QtCore.Qt.UserRole+5)
            if subgroup:item.setText(('▸  ' if subgroup in self.collapsed else '▾  ')+item.data(QtCore.Qt.UserRole+6))
            matches_by_key[key]=matches or key==self.current
        if query:
            for i in range(self.navigation.count()):
                item=self.navigation.item(i)
                if matches_by_key[item.data(QtCore.Qt.UserRole)]:
                    for ancestor in self.navigation_ancestors(item,navigation_items):matches_by_key[ancestor]=True
        for i in range(self.navigation.count()):
            item=self.navigation.item(i);ancestors=self.navigation_ancestors(item,navigation_items)
            collapsed=any((key.split(':')[0] if key.endswith(':__group__') else key) in self.collapsed for key in ancestors)
            item.setHidden(not matches_by_key[item.data(QtCore.Qt.UserRole)] or bool(not query and collapsed))
        if not self._normalized:return
        values={f.get('name'):f.get('value') for f in self._normalized['fields'] if f.get('_stable') and not f.get('secret')}
        shown={group:False for group in self.groups}
        for record in self._rendered_rows:
            spec=record['spec'];view=record['widget'];group=record['group']
            fields=[self.fields[f['id']]['field'] for f in spec['fields']+spec['attachments']]
            primaries=[self.fields[f['id']]['field'] for f in spec['fields'] if field_visible(self.fields[f['id']]['field'],values)]
            available=[f for f in fields if field_visible(f,values)] if primaries else []
            group_match=bool(query and query in group.title.text().casefold())
            match=bool(available) and (record.get('pinned') or not query or group_match or any(query in searchable_text(f) for f in available))
            view.setVisible(match);shown[group]|=match
            for f in spec['fields']:
                entry=self.fields[f['id']];visible=field_visible(entry['field'],values)
                if spec['actions'] and not record.get('pinned'):visible &= not query or group_match or query in searchable_text(entry['field'])
                # Plugin headings/status are represented by their row widgets.
                if entry.get('disclosure'):
                    entry['disclosure'].setVisible(visible);entry['disclosure'].search_expansion(bool(query and query in searchable_text(entry['field'])),bool(query))
                elif not spec.get('entity') or entry['field']['_role']=='detail' or entry['field']['kind']=='boolean':entry['container'].setVisible(visible)
            if hasattr(view,'search_expansion'):view.search_expansion(bool(query and any(query in searchable_text(f) for f in available if f['_role']=='detail')),bool(query))
            action_containers={self.fields[f['id']].get('action_container') for f in spec['fields']}-{None}
            for container in action_containers:container.setVisible(any(not self.fields[f['id']]['control'].isHidden() for f in spec['fields'] if self.fields[f['id']].get('action_container') is container))
        for group,visible in shown.items():group.setVisible(visible);group.refresh_rows(self.current=='peony:general',min(900,self.scroll.viewport().width()))
        for pair in self.scroll.findChildren(ProfileColumns):pair.schedule()
        self.page_actions.setVisible(any(record.get('pinned') and not record['widget'].isHidden() for record in self._rendered_rows))
        if hasattr(self,'no_results'):self.no_results.setVisible(bool(query) and not any(not record.get('pinned') and not record['widget'].isHidden() for record in self._rendered_rows))

    def render_overview(self,result):
        if not result.get('components'):
            self.message.setText(result.get('error','读取模块状态失败'));return
        components=result['components'];ids=[item['id'] for item in components]
        rebuild=self.signature!=str(ids)
        if rebuild:
            widget=QtWidgets.QWidget();layout=QtWidgets.QVBoxLayout(widget);layout.setContentsMargins(0,0,6,0);layout.setSpacing(12);self.overview_rows={}
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
        self.description.setText('常见问题、设置入口与恢复说明。');self.description.show()
        sections=[('找不到系统设置？','无线网络、声音、显示器和电源，请打开 UKUI 系统设置。','打开系统设置',self.open_system),
                  ('输入法没有候选词？','先安装并在 Fcitx 中启用 Rime，再选择 Rime 输入法；详细步骤在输入法页面顶部。','查看输入法设置',lambda:self.open_page('input:general')),
                  ('想恢复原来的外观？','在对应设置页使用“恢复默认”。只恢复按钮说明中的设置，不清除文件、账号或聊天记录。','查看桌面外观',lambda:self.open_page('fences:appearance')),
                  ('桌面显示异常？','“调试专用”用于临时测试桌面接管，普通使用不需要开启。已经开始测试时，可在那里立即恢复原生桌面。','查看调试与恢复',lambda:self.open_page('fences:session'))]
        for title,text,action,callback in sections:
            box=self.card(layout);heading=QtWidgets.QLabel(title);heading.setProperty('section',True);box.addWidget(heading);label=QtWidgets.QLabel(text);label.setWordWrap(True);box.addWidget(label)
            button=ActionButton(action);button.clicked.connect(callback);box.addWidget(button,0,QtCore.Qt.AlignLeft)
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
        if QtWidgets.QApplication.activeModalWidget() or window.busy or window.drafts or window._page_data.get('dirty'):return False
        for entry in window.fields.values():
            if window.modified(entry['control']):return False
        return bool(window.close())

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
