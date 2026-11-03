#!/usr/bin/python3
"""Dock-hosted control center, using the existing UKUI system services."""
import datetime,json,sys,time
from pathlib import Path
from PyQt5 import QtCore,QtGui,QtWidgets,QtDBus
from system_backend import SystemBackend,calendar_data,service_error
from audio_events import AudioEvents
from volume_feedback import VolumeFeedback
from fusion_view import FusionView,FUSION_STYLE
from glass_widgets import GlassCard,GlassStatusLabel,ReadableTooltips,PageTransition,glyph

class Signals(QtCore.QObject):finished=QtCore.pyqtSignal(object)
class Job(QtCore.QRunnable):
    def __init__(self,method):super().__init__();self.method=method;self.signals=Signals()
    def run(self):
        try:result={'ok':True,'data':self.method()}
        except Exception as error:result={'ok':False,'error':service_error(error)}
        self.method=None
        self.signals.finished.emit(result)

class LunarCalendar(QtWidgets.QCalendarWidget):
    def __init__(self,parent=None):
        super().__init__(parent);self.dates={};self.setLocale(QtCore.QLocale('zh_CN'));self.setFirstDayOfWeek(QtCore.Qt.Monday);self.setVerticalHeaderFormat(self.NoVerticalHeader);self.setGridVisible(False);self.setMinimumDate(QtCore.QDate(1901,1,1));self.setMaximumDate(QtCore.QDate(2099,12,31));self.setFixedHeight(285)
        weekend=QtGui.QTextCharFormat();weekend.setForeground(QtGui.QColor('#aacdea'));self.setWeekdayTextFormat(QtCore.Qt.Saturday,weekend);self.setWeekdayTextFormat(QtCore.Qt.Sunday,weekend)
    def paintCell(self,painter,rect,date):
        painter.save();painter.setRenderHint(QtGui.QPainter.Antialiasing)
        current=date==self.selectedDate();today=date==QtCore.QDate.currentDate();area=QtCore.QRectF(rect).adjusted(1,1,-1,-1)
        if current:painter.setPen(QtCore.Qt.NoPen);painter.setBrush(QtGui.QColor('#4776a4'));painter.drawRoundedRect(area,7,7)
        elif today:painter.setPen(QtGui.QPen(QtGui.QColor('#8dbce9'),1));painter.setBrush(QtCore.Qt.NoBrush);painter.drawRoundedRect(area,7,7)
        painter.setPen(QtGui.QColor('#edf4fd' if date.month()==self.monthShown() else '#73869a'));font=painter.font();font.setPixelSize(13);font.setBold(today);painter.setFont(font)
        painter.drawText(rect.adjusted(0,2,0,-rect.height()//2+2),QtCore.Qt.AlignCenter,str(date.day()))
        info=self.dates.get(date.toString('yyyy-MM-dd'),{});font.setPixelSize(9);font.setBold(False);painter.setFont(font);painter.setPen(QtGui.QColor('#c1d5e8' if info.get('festival') else '#97b0c8'))
        painter.drawText(rect.adjusted(0,rect.height()//2-1,0,-2),QtCore.Qt.AlignCenter,painter.fontMetrics().elidedText(info.get('label',''),QtCore.Qt.ElideRight,rect.width()-2));painter.restore()

class ControlCenter(FusionView,QtWidgets.QWidget):
    def __init__(self,backend_factory=SystemBackend,resident=False):
        super().__init__(None,QtCore.Qt.Tool|QtCore.Qt.FramelessWindowHint|QtCore.Qt.WindowStaysOnTopHint)
        self.ui_style=QtWidgets.QStyleFactory.create('Fusion');self.ui_style.setParent(self);self.setStyle(self.ui_style)
        self.material_revision=0
        self.resident=resident;self.controls_reading=False;self.controls_ready=False;self.controls_read_at=0.
        self.microphone_reading=False;self.microphone_refresh_pending=False
        self.audio_reading=False;self.audio_refresh_pending=False
        self.audio_events=AudioEvents(self);self.audio_events.changed.connect(self.audio_changed)
        QtWidgets.QApplication.instance().aboutToQuit.connect(self.audio_events.stop)
        self.volume_feedback=VolumeFeedback(self)
        QtWidgets.QApplication.instance().aboutToQuit.connect(self.volume_feedback.stop)
        self.preloading=False;self.preload_ready=False
        self.preload_expiry=QtCore.QTimer(self);self.preload_expiry.setSingleShot(True);self.preload_expiry.timeout.connect(self.hide)
        self.material_loading=False;self.material_pending=None
        self.opening_pending=False;self.warmup_cards=[];self.snapshot_pending=False;self.auto_scan_pending=False
        self.warmup_timer=QtCore.QTimer(self);self.warmup_timer.setSingleShot(True);self.warmup_timer.timeout.connect(self.warm_opening_card)
        self.material_pool=QtCore.QThreadPool(self);self.material_pool.setMaxThreadCount(1);self.material_pool.setExpiryTimeout(0)
        self.popup_motion=QtCore.QVariantAnimation(self);self.popup_progress=0.;self.closing=False;self.popup_target=QtCore.QRect()
        self.popup_motion.valueChanged.connect(self.popup_frame);self.popup_motion.finished.connect(self.popup_finished)
        self.navigation_active=False;self.navigation_overlay=PageTransition(self)
        self.navigation_motion=QtCore.QVariantAnimation(self);self.navigation_motion.setDuration(190);self.navigation_motion.setEasingCurve(QtCore.QEasingCurve.InOutCubic)
        self.navigation_motion.valueChanged.connect(self.navigation_overlay.set_progress);self.navigation_motion.finished.connect(self.finish_navigation)
        self.setObjectName('lquiControlCenter');self.setWindowTitle('Kylin-LQUI 系统控制中心');self.setAttribute(QtCore.Qt.WA_TranslucentBackground);self.setStyleSheet(FUSION_STYLE);self.backdrop=QtGui.QImage();self.native_glass=None
        palette=self.palette()
        for role in (QtGui.QPalette.WindowText,QtGui.QPalette.Text,QtGui.QPalette.ButtonText):palette.setColor(role,QtGui.QColor('#edf3fa'))
        palette.setColor(QtGui.QPalette.Base,QtGui.QColor('#263a50'));self.setPalette(palette)
        self.backend_factory=backend_factory;self.data={};self.jobs=set();self.reading=False;self.changing=False;self.busy_domains=set();self.domain_revisions={};self.pending_actions={};self.dialog_open=False;self.page='home';self.anchor=QtCore.QRect();self.calendar_result={};self.calendar_revision=0;self.state_revision=0;self.pending_network=None
        layout=QtWidgets.QVBoxLayout(self);layout.setContentsMargins(6,6,6,6);layout.setSpacing(6)
        self.header_frame=GlassCard(self,17);header=QtWidgets.QHBoxLayout(self.header_frame);header.setContentsMargins(8,3,8,3);self.back=QtWidgets.QPushButton('‹');self.back.setFixedWidth(28);self.back.clicked.connect(lambda:self.navigate('home'));header.addWidget(self.back)
        self.title=QtWidgets.QLabel('控制中心');self.title.setObjectName('centerTitle');header.addWidget(self.title,1)
        close=QtWidgets.QPushButton('×');close.setFixedWidth(28);close.clicked.connect(self.hide);header.addWidget(close);layout.addWidget(self.header_frame)
        self.scroll=QtWidgets.QScrollArea();self.scroll.setWidgetResizable(True);self.scroll.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);self.scroll.setVerticalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);layout.addWidget(self.scroll,1)
        self.status=GlassStatusLabel(self);self.status.setWordWrap(True);self.status.hide();self.status.setTextFormat(QtCore.Qt.PlainText);layout.addWidget(self.status)
        self.poll=QtCore.QTimer(self);self.poll.setInterval(2500);self.poll.timeout.connect(self.refresh)
        self.idle=QtCore.QTimer(self);self.idle.setSingleShot(True);self.idle.timeout.connect(self.quit_if_idle)
        QtWidgets.QApplication.instance().aboutToQuit.connect(self.release_material)
        self.tooltips=ReadableTooltips(self)
        QtWidgets.QApplication.instance().installEventFilter(self)
        self.show_page('home')
    def release_material(self):
        self.material_revision+=1
        self.material_pending=None
        material=self.native_glass;self.native_glass=None
        if material is not None:material.close()
    def prepare_material(self,image,screen,region):
        self.material_pending=(image,screen,region,self.material_revision)
        self.start_material()
    def start_material(self):
        if self.material_loading or self.material_pending is None:return
        image,screen,region,revision=self.material_pending;self.material_pending=None;self.material_loading=True
        def prepare():
            from native_glass import NativeGlass
            if revision!=self.material_revision:return None
            return NativeGlass(image,screen,region,adaptive_smoke=True)
        def ready(result):
            self.material_loading=False;material=result.get('data') if result['ok'] else None
            if revision==self.material_revision and self.isVisible():
                if material is not None:
                    self.native_glass=material;self.backdrop=QtGui.QImage()
                else:
                    from glass_widgets import diffuse_backdrop
                    self.backdrop=diffuse_backdrop(image,region.size())
                if self.opening_pending:self.prepare_opening_frame()
                else:self.update()
            elif material is not None:material.close()
            self.start_material()
        # One active decode and only the latest pending crop. Rapid toggles
        # cannot accumulate a queue of obsolete wallpaper reads.
        self.job(prepare,ready,pool=self.material_pool)
    def prepare_opening_frame(self):
        if not self.opening_pending or not self.isVisible():return
        self.apply_snapshot(force=True);self.set_position();self.layout().activate()
        self.warmup_cards=[]
        if self.native_glass:
            for card in self.findChildren(GlassCard):
                if card.isVisible():self.warmup_cards.append(card)
        self.warmup_timer.start(0)
    def warm_opening_card(self):
        if not self.opening_pending or not self.isVisible():return
        if self.warmup_cards and self.native_glass:
            # Compile/upload and bake before the first visible animation frame,
            # yielding between cards so cancel/focus events remain responsive.
            card=self.warmup_cards.pop(0)
            origin=card.mapToGlobal(QtCore.QPoint())+self.material_origin-self.mapToGlobal(QtCore.QPoint())
            self.native_glass.render(QtCore.QRect(origin,card.size()),card.devicePixelRatioF(),card.radius)
            self.warmup_timer.start(0);return
        if not self.controls_ready:return # its completion resumes this step
        if self.preloading:self.preload_ready=True;return
        # Apply the fast, current slider values before any visible frame.
        self.apply_snapshot(force=True);self.layout().activate()
        self.opening_pending=False
        # Repaint without the native clip while opacity is still zero. Painting
        # under the one-pixel mask leaves child backgrounds absent in motion.
        self.clearMask();self.repaint()
        self.popup_target=self.geometry();self.popup_frame(0.)
        self.animate_popup(True);self.poll.start()
        if self.backend_factory is SystemBackend:self.audio_events.start()
    def event(self,event):
        if event.type()==QtCore.QEvent.WindowDeactivate:QtCore.QTimer.singleShot(100,self.dismiss_if_inactive)
        return super().event(event)
    def eventFilter(self,obj,event):
        if event.type()==QtCore.QEvent.MouseButtonPress and event.button()==QtCore.Qt.LeftButton and self.page!='home' and not self.navigation_active and isinstance(obj,QtWidgets.QWidget) and obj.window() is self:
            point=event.globalPos()
            if not any(card.isVisible() and QtCore.QRect(card.mapToGlobal(QtCore.QPoint()),card.size()).contains(point) for card in self.findChildren(GlassCard)):
                self.dismiss();return True
        return super().eventFilter(obj,event)
    def dismiss_if_inactive(self):
        if self.preloading:return
        if self.isVisible() and not self.isActiveWindow() and not self.dialog_open and not QtWidgets.QApplication.activePopupWidget() and not QtWidgets.QApplication.activeModalWidget():self.dismiss()
    def hideEvent(self,event):
        self.volume_feedback.stop()
        self.audio_events.stop();self.audio_refresh_pending=False;self.microphone_refresh_pending=False
        self.finish_navigation(apply=False)
        self.preloading=False;self.preload_ready=False;self.preload_expiry.stop()
        self.opening_pending=False;self.warmup_timer.stop();self.warmup_cards=[];self.auto_scan_pending=False
        self.popup_motion.stop();self.popup_progress=0.;self.closing=False;self.clearMask();self.setWindowOpacity(1)
        self.release_material();self.backdrop=QtGui.QImage();self.poll.stop()
        if self.resident:self.idle.stop()
        else:self.idle.start(60000)
        super().hideEvent(event)
    def closeEvent(self,event):event.ignore();self.dismiss()
    def keyPressEvent(self,event):
        if event.key()==QtCore.Qt.Key_Escape:self.dismiss()
        else:super().keyPressEvent(event)
    def quit_if_idle(self):
        if self.resident:return
        if self.isVisible():return
        if self.jobs:self.idle.start(10000)
        else:QtWidgets.QApplication.quit()
    def warm_start(self):
        """One-time hidden startup work; no recurring service reads or draws."""
        self.refresh_controls();self.refresh();self.refresh_microphone()
        def prime():
            if self.isVisible():return
            try:
                from native_glass import NativeGlass
                image=QtGui.QImage(64,64,QtGui.QImage.Format_ARGB32_Premultiplied);image.fill(QtGui.QColor('#304050'))
                material=NativeGlass(image,QtCore.QRect(0,0,64,64))
                try:material.render(QtCore.QRect(0,0,64,64),1,20)
                finally:material.close()
            except (OSError,RuntimeError,AttributeError):pass
        QtCore.QTimer.singleShot(0,prime)
    def refresh_controls(self):
        if self.controls_reading:return
        self.controls_reading=True;revisions=dict(self.domain_revisions)
        def read():
            backend=self.backend_factory()
            return backend.controls_snapshot() if hasattr(backend,'controls_snapshot') else {k:v for k,v in backend.snapshot().items() if k in ('audio','brightness')}
        def done(result):
            self.controls_reading=False;self.controls_ready=True;self.controls_read_at=time.monotonic()
            data=result.get('data',{}) if result['ok'] else {}
            for domain in ('audio','brightness'):
                if revisions.get(domain,0)==self.domain_revisions.get(domain,0):
                    self.data[domain]=data.get(domain,{'available':False})
                    # A slower all-services read started earlier cannot reset
                    # the freshly-read controls at the end of the animation.
                    self.domain_revisions[domain]=self.domain_revisions.get(domain,0)+1
            if self.opening_pending:
                if not self.material_loading and not self.warmup_timer.isActive():self.warmup_timer.start(0)
            elif self.isVisible():self.apply_snapshot()
        self.job(read,done)
    def job(self,method,callback,pool=None):
        worker=Job(method);self.jobs.add(worker)
        def finished(result):
            self.jobs.discard(worker)
            try:callback(result)
            finally:worker.signals.deleteLater()
        worker.signals.finished.connect(finished);(pool or QtCore.QThreadPool.globalInstance()).start(worker)
    def open_at(self,page,x,y,width,height,preload=False):
        page='calendar' if page=='calendar' else 'home'
        anchor=QtCore.QRect(x,y,width,height)
        if self.preloading and not preload:
            if self.page==page and self.anchor==anchor and self.preload_expiry.isActive():
                self.preloading=False;self.preload_expiry.stop()
                if not self.controls_ready or time.monotonic()-self.controls_read_at>.75:
                    self.controls_ready=False;self.refresh_controls()
                self.setAttribute(QtCore.Qt.WA_ShowWithoutActivating,False);self.raise_();self.activateWindow()
                if self.preload_ready:self.warmup_timer.start(0)
                return
            self.hide()
        if preload:
            if self.isVisible():return
            self.preloading=True;self.preload_ready=False;self.preload_expiry.start(3000)
        if self.isVisible() and not self.opening_pending and self.anchor==anchor and self.page!=page:
            self.closing=False;self.navigate(page);self.raise_();self.activateWindow();self.poll.start()
            if self.backend_factory is SystemBackend:self.audio_events.start()
            return
        if self.isVisible() and self.page==page:
            if self.closing:
                self.closing=False;self.animate_popup(True);self.raise_();self.activateWindow();self.poll.start()
                if self.backend_factory is SystemBackend:self.audio_events.start()
            else:self.dismiss()
            return
        if self.isVisible():self.hide()
        self.anchor=anchor;self.idle.stop()
        if self.page!=page:self.show_page(page)
        self.controls_ready=False;self.refresh_controls()
        self.set_position();self.release_material();self.opening_pending=True;self.capture_backdrop();self.popup_target=self.geometry();self.popup_progress=0.
        # Map at its final geometry without exposing a temporary flat surface.
        # Real glass is prepared once before moving the cached window pixels.
        self.setWindowOpacity(0);self.setMask(QtGui.QRegion(QtCore.QRect(-2,-2,1,1)))
        self.setAttribute(QtCore.Qt.WA_ShowWithoutActivating,preload);self.show()
        if not preload:self.raise_();self.activateWindow()
        self.refresh();self.refresh_microphone()
        if not self.material_loading and self.material_pending is None:self.prepare_opening_frame()
    def popup_frame(self,value):
        self.popup_progress=float(value);target=self.popup_target
        if not target.isValid():return
        top=self.anchor.center().y()<target.center().y();offset=round(target.height()*(1-self.popup_progress))*(-1 if top else 1)
        self.move(target.topLeft()+QtCore.QPoint(0,offset))
        if self.popup_progress>=1:self.clearMask()
        else:
            clip=target.translated(-self.pos()).intersected(self.rect())
            self.setMask(QtGui.QRegion(clip if not clip.isEmpty() else QtCore.QRect(-2,-2,1,1)))
        self.setWindowOpacity(min(1.,self.popup_progress*4))
    def popup_finished(self):
        if self.closing:QtWidgets.QWidget.hide(self)
        else:
            # A material completion during motion can repaint only the masked
            # strip. Removing the native mask does not invalidate Qt's cached
            # descendant pixels; repaint the full surface at the resting pose.
            if self.snapshot_pending:self.apply_snapshot(force=True)
            self.update()
    def animate_popup(self,opening):
        self.popup_motion.stop();end=1. if opening else 0.
        if self.reduced_motion():self.popup_frame(end);self.popup_finished();return
        self.popup_motion.setDuration(max(40,round((260 if opening else 220)*abs(end-self.popup_progress))))
        self.popup_motion.setEasingCurve(QtCore.QEasingCurve.OutCubic if opening else QtCore.QEasingCurve.InCubic)
        self.popup_motion.setStartValue(self.popup_progress);self.popup_motion.setEndValue(end);self.popup_motion.start()
    def dismiss(self):
        if not self.isVisible() or self.closing:return
        self.volume_feedback.stop()
        self.audio_events.stop()
        self.finish_navigation()
        if self.opening_pending:self.hide();return
        self.closing=True;self.poll.stop();self.animate_popup(False)
    @staticmethod
    def label(text,muted=False):
        result=QtWidgets.QLabel(text);result.setTextFormat(QtCore.Qt.PlainText);result.setWordWrap(True);result.setProperty('muted',muted);return result
    def show_detail(self,page):
        self.page=page;self.title.setText({'home':'控制中心','wifi':'Wi-Fi','sound':'声音','power':'电池与电源','calendar':'日历'}[page]);self.back.setVisible(page!='home')
        old=self.scroll.takeWidget()
        if old:old.deleteLater()
        content=QtWidgets.QWidget();content.setObjectName('content');layout=QtWidgets.QVBoxLayout(content);layout.setContentsMargins(0,0,0,0);layout.setSpacing(9)
        self.volume=None;self.output=None;self.mute=None;self.wifi_toggle=None;self.network_list=None;self.power_text=None;self.profile=None;self.summary=None;self.calendar=None
        self.reset_audio_details()
        if page=='sound':
            box=self.card(layout);layout.setStretch(0,1);self.build_sound_panel(box)
        elif page=='wifi':
            box=self.card(layout);layout.setStretch(0,1)
            row=QtWidgets.QHBoxLayout();self.wifi_toggle=QtWidgets.QCheckBox('启用 Wi-Fi');self.wifi_toggle.clicked.connect(lambda on:self.action('wifi-enabled',{'value':on}));row.addWidget(self.wifi_toggle,1);scan=QtWidgets.QPushButton('刷新');scan.clicked.connect(lambda:self.action('scan',{}));row.addWidget(scan);box.addLayout(row)
            self.network_scroll,self.network_list=self.device_list(box)
            self.network_signature=None;self.network_buttons={};self.network_empty=self.label('正在查找网络…',True);self.network_list.addWidget(self.network_empty)
            advanced=QtWidgets.QPushButton('高级网络设置');advanced.setObjectName('advanced_network_settings');advanced.clicked.connect(lambda:self.launch('nm-connection-editor'));box.addWidget(advanced)
        elif page=='power':
            box=self.card(layout);self.power_text=self.label('正在读取电源信息…');box.addWidget(self.power_text)
            box=self.card(layout);box.addWidget(self.label('电源模式'));self.profile_buttons={}
            for ident,title in [('performance','性能 · 优先运行速度'),('balanced','均衡 · 兼顾性能和续航'),('power-saver','省电 · 延长电池使用时间')]:
                button=QtWidgets.QPushButton(title);button.setObjectName('power_profile_'+ident);button.setProperty('powerProfile',True);button.setMinimumHeight(44)
                button.clicked.connect(lambda checked=False,p=ident:self.choose_power_profile(p));box.addWidget(button);self.profile_buttons[ident]=(button,title)
            self.profile_note=self.label('',True);self.profile_note.setWordWrap(True);box.addWidget(self.profile_note)
        elif page=='calendar':
            box=self.card(layout);self.calendar=LunarCalendar();box.addWidget(self.calendar);self.day_text=self.label('');box.addWidget(self.day_text);self.agenda=self.label('',True);box.addWidget(self.agenda)
            row=QtWidgets.QHBoxLayout();today=QtWidgets.QPushButton('今天');today.clicked.connect(lambda:self.calendar.setSelectedDate(QtCore.QDate.currentDate()));row.addWidget(today);system=QtWidgets.QPushButton('管理系统日程');system.clicked.connect(self.open_system_calendar);row.addWidget(system);box.addLayout(row)
            self.calendar.currentPageChanged.connect(self.load_calendar);self.calendar.selectionChanged.connect(self.update_calendar_day);self.load_calendar(self.calendar.yearShown(),self.calendar.monthShown())
        if page not in ('wifi','sound'):layout.addStretch()
        self.scroll.setWidget(content);content.setAutoFillBackground(False);self.scroll.viewport().setAutoFillBackground(False);self.apply_snapshot();self.set_position()
    def navigate(self,page):
        if page==self.page:return
        animate=self.isVisible() and not self.opening_pending and not self.closing and not self.reduced_motion()
        if self.popup_motion.state()==QtCore.QAbstractAnimation.Running:
            self.popup_motion.stop();self.popup_frame(1.)
        before=self.grab() if animate else QtGui.QPixmap()
        self.finish_navigation(apply=False)
        self.setUpdatesEnabled(False)
        self.auto_scan_pending=page=='wifi';self.show_page(page);self.refresh();self.maybe_scan_wifi()
        self.layout().activate()
        for widget in self.findChildren(QtWidgets.QWidget):
            widget.ensurePolished()
            if widget.layout():widget.layout().activate()
        # Nested scroll viewports settle via LayoutRequest. Flush only layout
        # work before the target snapshot, never paints or service callbacks.
        QtCore.QCoreApplication.sendPostedEvents(None,QtCore.QEvent.LayoutRequest)
        self.setUpdatesEnabled(True)
        if animate:
            after=self.grab();self.navigation_active=True;self.layout().setEnabled(False)
            for widget in (self.header_frame,self.scroll,self.status):widget.hide()
            self.navigation_overlay.set_frames(before,after,-1 if page=='home' else 1)
            self.navigation_motion.setStartValue(0.);self.navigation_motion.setEndValue(1.);self.navigation_motion.start();self.update()
        if self.opening_pending and not self.material_loading:self.prepare_opening_frame()
    def finish_navigation(self,apply=True):
        if not self.navigation_active:return
        self.navigation_motion.stop();self.navigation_active=False;self.navigation_overlay.clear()
        self.header_frame.setVisible(self.page!='home');self.scroll.show();self.status.setVisible(bool(self.status.text()) and (self.page!='home' or not self.changing))
        self.layout().setEnabled(True);self.layout().activate()
        if apply:self.apply_snapshot(force=True)
        self.update()
    @staticmethod
    def reduced_motion():
        return QtCore.QSettings('org.ukui','style').value('reduceAnimation',False,type=bool) or QtCore.QSettings(str(Path.home()/'.config/ukui/liquid-panel.ini'),QtCore.QSettings.IniFormat).value('appearance/reducedMotion',False,type=bool)
    def maybe_scan_wifi(self):
        if not self.auto_scan_pending or self.page!='wifi' or not self.isVisible() or 'wifi' not in self.data:return
        if self.is_busy('wifi'):return
        self.auto_scan_pending=False;wifi=self.data['wifi']
        if wifi.get('available') and wifi.get('enabled') and wifi.get('hardwareEnabled'):self.action('scan',{})
    @staticmethod
    def action_domain(operation):
        if operation in ('volume','mute','output'):return 'audio'
        if operation in ('input','input-volume','input-mute'):return 'microphone'
        if operation in ('scan','connect','wifi-enabled'):return 'wifi'
        if operation.startswith('bluetooth'):return 'bluetooth'
        if operation.startswith('notification') or operation=='dnd':return 'notifications'
        if operation.startswith('media'):return 'media'
        if operation=='power-profile':return 'power'
        return operation
    def is_busy(self,domain):return domain in self.busy_domains
    def merge_snapshot(self,data,revisions):
        for domain,value in data.items():
            if revisions.get(domain,0)==self.domain_revisions.get(domain,0):self.data[domain]=value
    def refresh(self):
        if self.page=='sound':self.refresh_audio();self.refresh_microphone()
        if self.reading:return
        self.reading=True;revisions=dict(self.domain_revisions)
        def done(result):
            self.reading=False
            if result['ok']:self.merge_snapshot(result['data'],revisions);self.update_pending_network();self.apply_snapshot();self.maybe_scan_wifi()
        self.job(lambda:self.backend_factory().snapshot(),done)
    def audio_changed(self):
        if not self.isVisible() or self.preloading or self.closing:return
        # Ignore snapshots begun before this event, even when another service
        # delays their completion. Re-read after any in-flight audio action.
        for domain in ('audio','microphone'):self.domain_revisions[domain]=self.domain_revisions.get(domain,0)+1
        self.refresh_audio()
        if self.page=='sound':self.refresh_microphone()
    def refresh_audio(self):
        if self.audio_reading or self.is_busy('audio'):
            self.audio_refresh_pending=True;return
        self.audio_refresh_pending=False;self.audio_reading=True;revision=self.domain_revisions.get('audio',0)
        def read():
            backend=self.backend_factory()
            return backend.audio() if hasattr(backend,'audio') else backend.snapshot().get('audio',{'available':False})
        def done(result):
            self.audio_reading=False
            if result['ok'] and revision==self.domain_revisions.get('audio',0):
                self.data['audio']=result['data'];self.domain_revisions['audio']=revision+1
                if self.isVisible():self.apply_snapshot()
            if self.audio_refresh_pending and self.isVisible() and not self.closing:self.refresh_audio()
        self.job(read,done)
    def refresh_microphone(self):
        if self.microphone_reading or self.is_busy('microphone'):
            self.microphone_refresh_pending=True;return
        self.microphone_refresh_pending=False;self.microphone_reading=True;revision=self.domain_revisions.get('microphone',0)
        def read_microphone():
            backend=self.backend_factory()
            return backend.microphone() if hasattr(backend,'microphone') else backend.snapshot().get('microphone',{'available':False,'inputs':[]})
        def done(result):
            self.microphone_reading=False
            if revision==self.domain_revisions.get('microphone',0):
                self.data['microphone']=result.get('data') if result['ok'] else {'available':False,'inputs':[]}
                self.domain_revisions['microphone']=revision+1
            if self.page=='sound':self.apply_snapshot()
            if self.microphone_refresh_pending and self.isVisible() and self.page=='sound' and not self.closing:self.refresh_microphone()
        self.job(read_microphone,done)
    def action(self,operation,values):
        domain=self.action_domain(operation)
        if domain=='audio':self.volume_feedback.stop()
        if self.is_busy(domain):
            if operation in ('volume','brightness','input-volume'):self.pending_actions[domain]=(operation,dict(values))
            return
        values=dict(values);self.busy_domains.add(domain);self.changing=True
        self.state_revision+=1;self.domain_revisions[domain]=self.domain_revisions.get(domain,0)+1
        revisions=dict(self.domain_revisions);self.status.setText('正在查找 Wi-Fi 网络…' if operation=='scan' else '正在处理…');self.apply_snapshot()
        def done(result):
            self.busy_domains.discard(domain);self.changing=bool(self.busy_domains)
            if result['ok']:
                self.merge_snapshot(result['data'],revisions);self.status.setText('正在处理…' if self.changing else '')
                if operation=='connect':self.pending_network=(values.get('id'),time.monotonic());self.network_signature=None
                self.update_pending_network();self.apply_snapshot()
            else:self.status.setText(result['error']);self.apply_snapshot();self.refresh()
            values.clear()
            pending=self.pending_actions.pop(domain,None)
            if pending:self.action(*pending)
            elif result['ok'] and operation=='volume' and self.isVisible() and not self.closing and self.backend_factory is SystemBackend:
                self.volume_feedback.request(result['data'].get('audio',{}).get('selected'))
            if self.audio_refresh_pending and self.isVisible() and not self.closing:self.refresh_audio()
            if self.microphone_refresh_pending and self.isVisible() and self.page=='sound' and not self.closing:self.refresh_microphone()
            self.maybe_scan_wifi()
        self.job(lambda:self.backend_factory().action(operation,values),done)
    def update_pending_network(self):
        if not self.pending_network:return
        ident,started=self.pending_network;network=next((n for n in self.data.get('wifi',{}).get('networks',[]) if n['id']==ident),{})
        if network.get('connected'):self.pending_network=None;self.status.setText('已连接')
        elif time.monotonic()-started>45:self.pending_network=None;self.status.setText('连接尚未成功，请检查密码或在高级网络设置中查看原因。')
        else:self.status.setText('正在连接 Wi-Fi…')
    def apply_detail_snapshot(self):
        audio=self.data.get('audio',{});selected=audio.get('selected') or {};wifi=self.data.get('wifi',{});power=self.data.get('power',{})
        if self.volume:
            self.volume.setEnabled(bool(audio.get('available')));self.mute.setEnabled(bool(audio.get('available')) and not self.is_busy('audio'))
            if not self.volume.isSliderDown() and not self.is_busy('audio') and 'audio' not in self.pending_actions:
                blocker=QtCore.QSignalBlocker(self.volume);self.volume.setValue(min(100,selected.get('volume',0)));del blocker;self.volume_label.setText(str(selected.get('volume',0))+'%' if audio.get('available') else '音频未就绪')
            self.mute.setText('取消静音' if selected.get('muted') else '静音')
        self.update_audio_devices('output',audio.get('outputs',[]),selected.get('id'),self.is_busy('audio'),'audio' not in self.data)
        microphone=self.data.get('microphone',{});current=microphone.get('selected') or {}
        if self.page=='sound':
            for label,text in ((self.audio_title,selected.get('routeLabel') or '扬声器与耳机'),(self.mic_title,current.get('routeLabel') or '麦克风')):
                label.setText(label.fontMetrics().elidedText(text,QtCore.Qt.ElideRight,230));label.setToolTip(text)
        self.update_audio_devices('input',microphone.get('inputs',[]),current.get('id'),self.is_busy('microphone'),'microphone' not in self.data)
        if self.mic_volume is not None:
            self.mic_volume.setEnabled(bool(current));self.mic_mute.setEnabled(bool(current) and not self.is_busy('microphone'))
            self.mic_mute.setIcon(glyph('microphone-muted' if current.get('muted') else 'microphone'))
            self.mic_mute.setToolTip('取消麦克风静音' if current.get('muted') else '麦克风静音');self.mic_mute.setAccessibleName(self.mic_mute.toolTip())
            if not self.mic_volume.isSliderDown() and not self.is_busy('microphone') and 'microphone' not in self.pending_actions:
                blocker=QtCore.QSignalBlocker(self.mic_volume);self.mic_volume.setValue(min(100,current.get('volume',0)));del blocker
                self.mic_label.setText(str(current['volume'])+'%' if current else '—')
        if self.summary:
            current=next((n['ssid'] for n in wifi.get('networks',[]) if n['connected']), '网络服务未就绪' if not wifi.get('available') else '未连接' if wifi.get('enabled') else 'Wi-Fi 已关闭');self.summary.setText(current+'  ·  '+self.power_summary(power))
        if self.wifi_toggle:
            blocker=QtCore.QSignalBlocker(self.wifi_toggle);self.wifi_toggle.setChecked(wifi.get('enabled',False));del blocker;self.wifi_toggle.setEnabled(bool(wifi.get('available') and wifi.get('hardwareEnabled')) and not self.is_busy('wifi'))
            networks=sorted(wifi.get('networks',[]),key=lambda n:(not n['connected'],-n['strength'],n['ssid'].casefold(),n['id'])) if wifi.get('enabled') else []
            ids={n['id'] for n in networks}
            for ident in list(self.network_buttons):
                if ident not in ids:
                    button=self.network_buttons.pop(ident);self.network_list.removeWidget(button);button.hide();button.deleteLater()
            self.network_empty.setText('未检测到无线网卡' if not wifi.get('available') else 'Wi-Fi 已关闭' if not wifi.get('enabled') else '正在查找网络…')
            self.network_empty.setVisible(not networks)
            # Preserve widgets/focus, but update their positions on every scan.
            # Otherwise a newly found strong hotspot stays appended at the end.
            for rank,network in enumerate(networks,1):
                ident=network['id'];button=self.network_buttons.get(ident)
                if button is None:
                    button=QtWidgets.QPushButton();self.network_buttons[ident]=button;self.network_list.addWidget(button)
                    button.clicked.connect(lambda checked=False,b=button:self.connect_network(dict(b.network)))
                    button.show() # include dynamic rows in the transition snapshot
                if self.network_list.indexOf(button)!=rank:self.network_list.insertWidget(rank,button)
                button.network=dict(network)
                band=network.get('band') or SystemBackend.wifi_band(network.get('frequency',0))
                button.setText(network['ssid'].replace('&','&&')+'  ·  '+band+'  ·  '+('已连接' if network['connected'] else '连接中…' if network.get('connecting') or self.pending_network and self.pending_network[0]==ident else str(network['strength'])+'%'))
                if button.property('accent')!=network['connected']:
                    button.setProperty('accent',network['connected']);button.style().unpolish(button);button.style().polish(button)
                button.setEnabled(not (network['connected'] or network.get('connecting') or self.is_busy('wifi') or self.pending_network))
                button.setToolTip(network['ssid']+'\n'+band+(' · '+str(network['frequency'])+' MHz' if network.get('frequency') else ''))
        if self.power_text:
            text=self.power_summary(power)
            seconds=power.get('timeToEmpty') if power.get('onBattery') else power.get('timeToFull')
            if seconds and seconds>0:text+='\n'+('预计可用 ' if power.get('onBattery') else '预计充满 ')+str(seconds//3600)+' 小时 '+str(seconds%3600//60)+' 分钟'
            if power.get('capacity'):text+='\n电池健康度 '+str(power['capacity'])+'%'
            self.power_text.setText(text);profiles=power.get('profiles',[])
            for ident,(button,title) in self.profile_buttons.items():
                current=ident==power.get('activeProfile');button.setText(title+('  ✓ 当前' if current else ''))
                button.setEnabled(ident in profiles and not current and power.get('profileWritable',True) and not self.is_busy('power'))
                button.setToolTip('当前模式' if current else '切换到此模式' if ident in profiles else '当前系统不支持此模式')
                if button.property('current')!=current:
                    button.setProperty('current',current);button.style().unpolish(button);button.style().polish(button)
            self.profile_note.setText(power.get('profileNote','') if profiles else '系统暂未提供可切换的电源模式。')
            self.profile_note.setVisible(bool(self.profile_note.text()))
    def choose_power_profile(self,profile):
        power=self.data.get('power',{})
        if profile in power.get('profiles',[]) and profile!=power.get('activeProfile') and not self.is_busy('power'):
            self.action('power-profile',{'value':profile})
    @staticmethod
    def power_summary(power):
        if not power.get('available'):return '电源信息暂不可用'
        if not power.get('present'):return '已接通电源'
        return str(power['percentage'])+'% · '+{1:'正在充电',2:'使用电池',3:'电量耗尽',4:'已充满',5:'等待充电',6:'等待放电'}.get(power.get('state'),'电池')
    def connect_network(self,network):
        if network['security'] in ('enterprise','unsupported') and not network['profile']:self.status.setText('此网络需要在“高级网络设置”中配置。');return
        values={'id':network['id'],'remember':True}
        if network['security']!='open' and not network['profile']:
            dialog=QtWidgets.QDialog(self);dialog.setWindowTitle('连接 Wi-Fi');dialog.setStyleSheet('QDialog{background:#26384c;}');layout=QtWidgets.QVBoxLayout(dialog);layout.addWidget(self.label(network['ssid']));password=QtWidgets.QLineEdit();password.setEchoMode(QtWidgets.QLineEdit.Password);password.setPlaceholderText('Wi-Fi 密码');layout.addWidget(password);remember=QtWidgets.QCheckBox('记住这个网络');remember.setChecked(True);layout.addWidget(remember);buttons=QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok|QtWidgets.QDialogButtonBox.Cancel);buttons.accepted.connect(dialog.accept);buttons.rejected.connect(dialog.reject);layout.addWidget(buttons)
            self.dialog_open=True;accepted=dialog.exec_()==QtWidgets.QDialog.Accepted;self.dialog_open=False;values.update(password=password.text(),remember=remember.isChecked());password.clear();dialog.deleteLater();self.raise_();self.activateWindow()
            if not accepted:values.clear();return
        self.action('connect',values)
    def load_calendar(self,year,month):
        self.calendar_revision+=1;revision=self.calendar_revision
        def done(result):
            if revision!=self.calendar_revision or not self.calendar:return
            if result['ok']:self.calendar_result=result['data'];self.calendar.dates=self.calendar_result.get('dates',{});self.calendar.updateCells();self.update_calendar_day()
        self.job(lambda:calendar_data(year,month),done)
    def update_calendar_day(self):
        if not self.calendar:return
        selected=self.calendar.selectedDate().toString('yyyy-MM-dd');info=self.calendar_result.get('dates',{}).get(selected,{})
        self.day_text.setText(selected+'  '+info.get('lunar','')+' '+info.get('festival',''))
        items=[item for item in self.calendar_result.get('items',[]) if item['date']<=selected<=item.get('endDate',item['date'])]
        self.agenda.setText('\n'.join(item['time']+'  '+item['title'] for item in items[:6]) or self.calendar_result.get('warning') or '当天暂无系统日程')
    def open_system_calendar(self):
        from gi.repository import Gio,GLib
        date=self.calendar.selectedDate();day,month,year=date.day(),date.month(),date.year()
        def open_day():
            bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
            bus.call_sync('org.ukui.calendar.Notifications','/org/ukui/calendar/Notifications','org.ukui.calendar.Notifications','OpenSchedule',GLib.Variant('(iii)',(day,month,year)),None,Gio.DBusCallFlags.NO_AUTO_START,2000,None)
        def done(result):
            if result['ok']:self.dismiss()
            else:self.status.setText('系统日程编辑器暂未响应；日历和已有日程仍可查看。')
        self.job(open_day,done)
    def launch(self,program):
        QtCore.QProcess.startDetached(program,[]);self.dismiss()
    def volume_ui_state(self):
        # Read-only geometry for verifying real mouse interaction through State.
        if not self.volume:return None
        slider=self.volume;option=QtWidgets.QStyleOptionSlider();slider.initStyleOption(option)
        def center(value):
            option.sliderPosition=value;option.sliderValue=value
            point=slider.mapToGlobal(slider.style().subControlRect(QtWidgets.QStyle.CC_Slider,option,QtWidgets.QStyle.SC_SliderHandle,slider).center())
            return [point.x(),point.y()]
        return {'value':slider.value(),'handle':center(slider.value()),'minimumHandle':center(slider.minimum()),
                'maximumHandle':center(slider.maximum()),'minimum':slider.minimum(),'maximum':slider.maximum(),'dpr':slider.devicePixelRatioF()}
    def state(self):return {'page':self.page,'visible':self.isVisible(),'closing':self.closing,'preparing':self.opening_pending,'navigationRunning':self.navigation_active,'resident':self.resident,'preloading':self.preloading,'controlsReady':self.controls_ready,'motionRunning':self.popup_motion.state()==QtCore.QAbstractAnimation.Running,'progress':self.popup_progress,'busy':self.changing,'reading':self.reading,'geometry':[self.x(),self.y(),self.width(),self.height()],'materialBackend':('snell-gpu' if self.native_glass.used_gpu else 'snell-cpu-fallback') if self.native_glass else 'blur-fallback','materialRenders':self.native_glass.render_count if self.native_glass else 0,'adaptiveSmoke':bool(getattr(self.native_glass,'adaptive_smoke',False)),'audioEventsRunning':self.audio_events.process.state()==QtCore.QProcess.Running,'audioReading':self.audio_reading,'microphoneReading':self.microphone_reading,'volumeFeedback':self.volume_feedback.state(),'volumeUi':self.volume_ui_state()}

class Adaptor(QtDBus.QDBusAbstractAdaptor):
    QtCore.Q_CLASSINFO('D-Bus Interface','org.kylin.Lqui.ControlCenter')
    @QtCore.pyqtSlot(str,int,int,int,int)
    def Open(self,page,x,y,w,h):self.parent().open_at(page,x,y,w,h)
    @QtCore.pyqtSlot(str,int,int,int,int)
    def Prepare(self,page,x,y,w,h):self.parent().open_at(page,x,y,w,h,preload=True)
    @QtCore.pyqtSlot(result=str)
    def State(self):return json.dumps(self.parent().state())
    @QtCore.pyqtSlot()
    def Dismiss(self):self.parent().dismiss()

def main():
    QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling);QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_UseHighDpiPixmaps);app=QtWidgets.QApplication(sys.argv);app.setApplicationName('kylin-lqui-control-center');app.setWindowIcon(QtGui.QIcon(str(Path(__file__).with_name('icon.svg'))));app.setDesktopFileName('kylin-lqui-control-center');app.setQuitOnLastWindowClosed(False)
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--page',default='home');parser.add_argument('--anchor',default='0,0,1,1');parser.add_argument('--resident',action='store_true',help='Start hidden and prewarm the control center');args=parser.parse_args();anchor=[int(x) for x in args.anchor.split(',')]
    if len(anchor)!=4:raise ValueError('invalid anchor')
    bus=QtDBus.QDBusConnection.sessionBus()
    if not bus.registerService('org.kylin.Lqui.ControlCenter'):
        if args.resident:return 0
        interface=QtDBus.QDBusInterface('org.kylin.Lqui.ControlCenter','/ControlCenter','org.kylin.Lqui.ControlCenter',bus);interface.call('Open',args.page,*anchor);return 0
    window=ControlCenter(resident=True);adaptor=Adaptor(window);bus.registerObject('/ControlCenter',window,QtDBus.QDBusConnection.ExportAdaptors)
    if args.resident:window.warm_start()
    else:window.open_at(args.page,*anchor)
    return app.exec_()
if __name__=='__main__':sys.exit(main())
