#!/usr/bin/python3
"""Dock-hosted control center, using the existing UKUI system services."""
import datetime,json,sys,time
from pathlib import Path
from PyQt5 import QtCore,QtGui,QtWidgets,QtDBus
from system_backend import SystemBackend,calendar_data,service_error
from fusion_view import FusionView,FUSION_STYLE
from glass_widgets import GlassCard,GlassStatusLabel,ReadableTooltips

class Signals(QtCore.QObject):finished=QtCore.pyqtSignal(object)
class Job(QtCore.QRunnable):
    def __init__(self,method):super().__init__();self.method=method;self.signals=Signals()
    def run(self):
        try:result={'ok':True,'data':self.method()}
        except Exception as error:result={'ok':False,'error':service_error(error)}
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
    def __init__(self,backend_factory=SystemBackend):
        super().__init__(None,QtCore.Qt.Tool|QtCore.Qt.FramelessWindowHint|QtCore.Qt.WindowStaysOnTopHint)
        self.ui_style=QtWidgets.QStyleFactory.create('Fusion');self.ui_style.setParent(self);self.setStyle(self.ui_style)
        self.setObjectName('lquiControlCenter');self.setWindowTitle('Kylin-LQUI 系统控制中心');self.setAttribute(QtCore.Qt.WA_TranslucentBackground);self.setStyleSheet(FUSION_STYLE);self.backdrop=QtGui.QImage();self.native_glass=None
        palette=self.palette()
        for role in (QtGui.QPalette.WindowText,QtGui.QPalette.Text,QtGui.QPalette.ButtonText):palette.setColor(role,QtGui.QColor('#edf3fa'))
        palette.setColor(QtGui.QPalette.Base,QtGui.QColor('#263a50'));self.setPalette(palette)
        self.backend_factory=backend_factory;self.data={};self.jobs=set();self.reading=False;self.changing=False;self.busy_domains=set();self.domain_revisions={};self.pending_actions={};self.dialog_open=False;self.page='home';self.anchor=QtCore.QRect();self.calendar_result={};self.calendar_revision=0;self.state_revision=0;self.pending_network=None
        layout=QtWidgets.QVBoxLayout(self);layout.setContentsMargins(6,6,6,6);layout.setSpacing(6)
        self.header_frame=GlassCard(self,17);header=QtWidgets.QHBoxLayout(self.header_frame);header.setContentsMargins(8,3,8,3);self.back=QtWidgets.QPushButton('‹');self.back.setFixedWidth(28);self.back.clicked.connect(lambda:self.show_page('home'));header.addWidget(self.back)
        self.title=QtWidgets.QLabel('控制中心');self.title.setObjectName('centerTitle');header.addWidget(self.title,1)
        close=QtWidgets.QPushButton('×');close.setFixedWidth(28);close.clicked.connect(self.hide);header.addWidget(close);layout.addWidget(self.header_frame)
        self.scroll=QtWidgets.QScrollArea();self.scroll.setWidgetResizable(True);self.scroll.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);self.scroll.setVerticalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);layout.addWidget(self.scroll,1)
        self.status=GlassStatusLabel(self);self.status.setWordWrap(True);self.status.hide();self.status.setTextFormat(QtCore.Qt.PlainText);layout.addWidget(self.status)
        self.poll=QtCore.QTimer(self);self.poll.setInterval(2500);self.poll.timeout.connect(self.refresh)
        self.idle=QtCore.QTimer(self);self.idle.setSingleShot(True);self.idle.timeout.connect(self.quit_if_idle)
        QtWidgets.QApplication.instance().aboutToQuit.connect(self.release_material)
        self.tooltips=ReadableTooltips(self)
        self.show_page('home')
    def release_material(self):
        material=self.native_glass;self.native_glass=None
        if material is not None:material.close()
    def event(self,event):
        if event.type()==QtCore.QEvent.WindowDeactivate:QtCore.QTimer.singleShot(100,self.dismiss_if_inactive)
        return super().event(event)
    def dismiss_if_inactive(self):
        if self.isVisible() and not self.isActiveWindow() and not self.dialog_open and not QtWidgets.QApplication.activePopupWidget() and not QtWidgets.QApplication.activeModalWidget():self.hide()
    def hideEvent(self,event):self.release_material();self.backdrop=QtGui.QImage();self.poll.stop();self.idle.start(60000);super().hideEvent(event)
    def closeEvent(self,event):event.ignore();self.hide()
    def keyPressEvent(self,event):
        if event.key()==QtCore.Qt.Key_Escape:self.hide()
        else:super().keyPressEvent(event)
    def quit_if_idle(self):
        if self.isVisible():return
        if self.jobs:self.idle.start(10000)
        else:QtWidgets.QApplication.quit()
    def job(self,method,callback):
        worker=Job(method);self.jobs.add(worker)
        def finished(result):self.jobs.discard(worker);callback(result)
        worker.signals.finished.connect(finished);QtCore.QThreadPool.globalInstance().start(worker)
    def open_at(self,page,x,y,width,height):
        page='calendar' if page=='calendar' else 'home'
        if self.isVisible() and self.page==page:self.hide();return
        if self.isVisible():self.hide()
        self.anchor=QtCore.QRect(x,y,width,height);self.idle.stop();self.show_page(page)
        self.set_position();self.release_material();self.capture_backdrop();self.show();self.raise_();self.activateWindow();self.refresh();self.poll.start()
    @staticmethod
    def label(text,muted=False):
        result=QtWidgets.QLabel(text);result.setTextFormat(QtCore.Qt.PlainText);result.setWordWrap(True);result.setProperty('muted',muted);return result
    def show_detail(self,page):
        self.page=page;self.title.setText({'home':'控制中心','wifi':'Wi-Fi','sound':'声音','power':'电池与电源','calendar':'日历'}[page]);self.back.setVisible(page!='home')
        old=self.scroll.takeWidget()
        if old:old.deleteLater()
        content=QtWidgets.QWidget();content.setObjectName('content');layout=QtWidgets.QVBoxLayout(content);layout.setContentsMargins(0,0,0,0);layout.setSpacing(9)
        self.volume=None;self.output=None;self.mute=None;self.wifi_toggle=None;self.network_list=None;self.power_text=None;self.profile=None;self.summary=None;self.calendar=None
        if page=='sound':
            box=self.card(layout);self.build_audio(box);box.addWidget(self.label('输出设备',True));self.output=QtWidgets.QComboBox();box.addWidget(self.output);self.output.activated[int].connect(lambda i:self.action('output',{'id':self.output.itemData(i)}))
        elif page=='wifi':
            row=QtWidgets.QHBoxLayout();self.wifi_toggle=QtWidgets.QCheckBox('启用 Wi-Fi');self.wifi_toggle.clicked.connect(lambda on:self.action('wifi-enabled',{'value':on}));row.addWidget(self.wifi_toggle,1);scan=QtWidgets.QPushButton('刷新');scan.clicked.connect(lambda:self.action('scan',{}));row.addWidget(scan);layout.addLayout(row)
            self.network_list=QtWidgets.QVBoxLayout();layout.addLayout(self.network_list);self.network_signature=None;self.network_buttons={};self.network_empty=self.label('正在查找网络…',True);self.network_list.addWidget(self.network_empty)
            advanced=QtWidgets.QPushButton('高级网络设置');advanced.clicked.connect(lambda:self.launch('nm-connection-editor'));layout.addWidget(advanced)
        elif page=='power':
            box=self.card(layout);self.power_text=self.label('正在读取电源信息…');box.addWidget(self.power_text);self.profile=QtWidgets.QComboBox();self.profile.hide();box.addWidget(self.profile);self.profile.activated[int].connect(lambda i:self.action('power-profile',{'value':self.profile.itemData(i)}))
        elif page=='calendar':
            box=self.card(layout);self.calendar=LunarCalendar();box.addWidget(self.calendar);self.day_text=self.label('');box.addWidget(self.day_text);self.agenda=self.label('',True);box.addWidget(self.agenda)
            row=QtWidgets.QHBoxLayout();today=QtWidgets.QPushButton('今天');today.clicked.connect(lambda:self.calendar.setSelectedDate(QtCore.QDate.currentDate()));row.addWidget(today);system=QtWidgets.QPushButton('管理系统日程');system.clicked.connect(self.open_system_calendar);row.addWidget(system);box.addLayout(row)
            self.calendar.currentPageChanged.connect(self.load_calendar);self.calendar.selectionChanged.connect(self.update_calendar_day);self.load_calendar(self.calendar.yearShown(),self.calendar.monthShown())
        layout.addStretch();self.scroll.setWidget(content);content.setAutoFillBackground(False);self.scroll.viewport().setAutoFillBackground(False);self.apply_snapshot();self.set_position()
    def navigate(self,page):self.show_page(page);self.refresh()
    @staticmethod
    def action_domain(operation):
        if operation in ('volume','mute','output'):return 'audio'
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
        if self.reading:return
        self.reading=True;revisions=dict(self.domain_revisions)
        def done(result):
            self.reading=False
            if result['ok']:self.merge_snapshot(result['data'],revisions);self.update_pending_network();self.apply_snapshot()
        self.job(lambda:self.backend_factory().snapshot(),done)
    def action(self,operation,values):
        domain=self.action_domain(operation)
        if self.is_busy(domain):
            if operation in ('volume','brightness'):self.pending_actions[domain]=(operation,dict(values))
            return
        values=dict(values);self.busy_domains.add(domain);self.changing=True
        self.state_revision+=1;self.domain_revisions[domain]=self.domain_revisions.get(domain,0)+1
        revisions=dict(self.domain_revisions);self.status.show();self.status.setText('正在处理…');self.apply_snapshot()
        def done(result):
            self.busy_domains.discard(domain);self.changing=bool(self.busy_domains)
            if result['ok']:
                self.merge_snapshot(result['data'],revisions);self.status.setText('正在处理…' if self.changing else '')
                if operation=='connect':self.pending_network=(values.get('id'),time.monotonic());self.network_signature=None
                self.update_pending_network();self.apply_snapshot();self.status.setVisible(bool(self.status.text()))
            else:self.status.setText(result['error']);self.apply_snapshot();self.refresh()
            values.clear()
            pending=self.pending_actions.pop(domain,None)
            if pending:self.action(*pending)
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
        if self.output is not None and not self.output.view().isVisible():
            blocker=QtCore.QSignalBlocker(self.output);self.output.clear()
            for item in audio.get('outputs',[]):self.output.addItem(item['name'],item['id'])
            self.output.setCurrentIndex(self.output.findData(selected.get('id')));del blocker;self.output.setEnabled(not self.is_busy('audio') and bool(audio.get('available')))
        if self.summary:
            current=next((n['ssid'] for n in wifi.get('networks',[]) if n['connected']), '网络服务未就绪' if not wifi.get('available') else '未连接' if wifi.get('enabled') else 'Wi-Fi 已关闭');self.summary.setText(current+'  ·  '+self.power_summary(power))
        if self.wifi_toggle:
            blocker=QtCore.QSignalBlocker(self.wifi_toggle);self.wifi_toggle.setChecked(wifi.get('enabled',False));del blocker;self.wifi_toggle.setEnabled(bool(wifi.get('available') and wifi.get('hardwareEnabled')) and not self.is_busy('wifi'))
            networks=wifi.get('networks',[]) if wifi.get('enabled') else []
            ids={n['id'] for n in networks}
            for ident in list(self.network_buttons):
                if ident not in ids:
                    button=self.network_buttons.pop(ident);self.network_list.removeWidget(button);button.hide();button.deleteLater()
            self.network_empty.setText('未检测到无线网卡' if not wifi.get('available') else 'Wi-Fi 已关闭' if not wifi.get('enabled') else '正在查找网络…')
            self.network_empty.setVisible(not networks)
            # Preserve existing row identity and order as signal strength changes.
            for network in networks:
                ident=network['id'];button=self.network_buttons.get(ident)
                if button is None:
                    button=QtWidgets.QPushButton();self.network_buttons[ident]=button;self.network_list.addWidget(button)
                    button.clicked.connect(lambda checked=False,b=button:self.connect_network(dict(b.network)))
                button.network=dict(network)
                button.setText(network['ssid'].replace('&','&&')+'  ·  '+('已连接' if network['connected'] else '连接中…' if network.get('connecting') or self.pending_network and self.pending_network[0]==ident else str(network['strength'])+'%'))
                if button.property('accent')!=network['connected']:
                    button.setProperty('accent',network['connected']);button.style().unpolish(button);button.style().polish(button)
                button.setEnabled(not (network['connected'] or network.get('connecting') or self.is_busy('wifi') or self.pending_network));button.setToolTip(network['ssid'])
        if self.power_text:
            text=self.power_summary(power)
            seconds=power.get('timeToEmpty') if power.get('onBattery') else power.get('timeToFull')
            if seconds and seconds>0:text+='\n'+('预计可用 ' if power.get('onBattery') else '预计充满 ')+str(seconds//3600)+' 小时 '+str(seconds%3600//60)+' 分钟'
            if power.get('capacity'):text+='\n电池健康度 '+str(power['capacity'])+'%'
            self.power_text.setText(text);profiles=power.get('profiles',[]);self.profile.setVisible(bool(profiles))
            if profiles and not self.profile.view().isVisible():
                blocker=QtCore.QSignalBlocker(self.profile);self.profile.clear()
                for profile in profiles:self.profile.addItem({'balanced':'均衡','power-saver':'节能','performance':'性能'}.get(profile,profile),profile)
                self.profile.setCurrentIndex(self.profile.findData(power.get('activeProfile')));del blocker
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
            if result['ok']:self.hide()
            else:self.status.setText('系统日程编辑器暂未响应；日历和已有日程仍可查看。')
        self.job(open_day,done)
    def launch(self,program):
        QtCore.QProcess.startDetached(program,[]);self.hide()
    def state(self):return {'page':self.page,'visible':self.isVisible(),'busy':self.changing,'reading':self.reading,'geometry':[self.x(),self.y(),self.width(),self.height()],'materialBackend':'launcher-cpu-refraction' if self.native_glass else 'blur-fallback','materialRenders':self.native_glass.render_count if self.native_glass else 0}

class Adaptor(QtDBus.QDBusAbstractAdaptor):
    QtCore.Q_CLASSINFO('D-Bus Interface','org.kylin.Lqui.ControlCenter')
    @QtCore.pyqtSlot(str,int,int,int,int)
    def Open(self,page,x,y,w,h):self.parent().open_at(page,x,y,w,h)
    @QtCore.pyqtSlot(result=str)
    def State(self):return json.dumps(self.parent().state())
    @QtCore.pyqtSlot()
    def Dismiss(self):self.parent().hide()

def main():
    QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling);QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_UseHighDpiPixmaps);app=QtWidgets.QApplication(sys.argv);app.setApplicationName('kylin-lqui-control-center');app.setWindowIcon(QtGui.QIcon(str(Path(__file__).with_name('icon.svg'))));app.setDesktopFileName('kylin-lqui-control-center');app.setQuitOnLastWindowClosed(False)
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--page',default='home');parser.add_argument('--anchor',default='0,0,1,1');args=parser.parse_args();anchor=[int(x) for x in args.anchor.split(',')]
    if len(anchor)!=4:raise ValueError('invalid anchor')
    bus=QtDBus.QDBusConnection.sessionBus()
    if not bus.registerService('org.kylin.Lqui.ControlCenter'):
        interface=QtDBus.QDBusInterface('org.kylin.Lqui.ControlCenter','/ControlCenter','org.kylin.Lqui.ControlCenter',bus);interface.call('Open',args.page,*anchor);return 0
    window=ControlCenter();adaptor=Adaptor(window);bus.registerObject('/ControlCenter',window,QtDBus.QDBusConnection.ExportAdaptors);window.open_at(args.page,*anchor);return app.exec_()
if __name__=='__main__':sys.exit(main())
