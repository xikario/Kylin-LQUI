import datetime,json,math,html
from PyQt5 import QtCore,QtGui,QtWidgets
from glass_widgets import GlassCard,CardChevron,glyph,blurred_capture

FUSION_STYLE='''
QWidget{font-family:"Noto Sans CJK SC";font-size:12px;color:#f0f4fa;}
QFrame[glass="true"],QWidget#content,QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;border:0;}
QLabel{background:transparent;color:#eef3fa;}
QLabel[muted="true"]{color:#dde6f0;font-size:11px;}
QLabel#centerTitle{font-size:15px;font-weight:600;}
QLabel[cardTitle="true"]{font-size:14px;font-weight:600;}
QPushButton,QToolButton{background:rgba(220,235,255,20);color:#eef4ff;border:0;border-radius:10px;padding:6px;}
QPushButton:hover,QToolButton:hover{background:rgba(220,235,255,45);}
QPushButton:pressed,QToolButton:pressed{background:rgba(220,235,255,70);}
QPushButton[accent="true"]:disabled{color:#e5f2ff;background:rgba(135,195,235,40);}
QPushButton:disabled,QToolButton:disabled{color:#a0acba;background:rgba(180,205,230,8);}
QToolButton[round="true"]{border-radius:21px;background:rgba(230,239,252,38);}
QToolButton[round="true"]:checked{background:#ccdfed;}
QPushButton[flatText="true"]{background:transparent;text-align:left;padding:1px;}
QPushButton[primary="true"]{background:#c18037;}
QComboBox,QLineEdit{background:rgba(10,18,28,125);color:#f1f5ff;border:1px solid rgba(215,234,255,65);border-radius:8px;padding:6px;}
QComboBox::drop-down{border:0;width:24px;}
QComboBox QAbstractItemView{background:#293b4e;color:#f1f5ff;selection-background-color:#527b9c;}
QSlider::groove:horizontal{height:7px;background:rgba(230,240,255,55);border-radius:3px;}
QSlider::sub-page:horizontal{background:#d5e7f6;border-radius:3px;}
QSlider::handle:horizontal{width:17px;margin:-5px 0;background:#eef6ff;border:1px solid #b8ccdc;border-radius:8px;}
QScrollBar:vertical{width:6px;background:transparent;margin:2px;}
QScrollBar::handle:vertical{background:#8d9eae;min-height:20px;border-radius:2px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
QTextBrowser{background:transparent;color:#eef3fa;border:0;padding:0;}
QToolTip{background:#eef4fa;color:#27394d;border:1px solid #a7bdcf;padding:6px;}
QCalendarWidget QWidget{background:#293c50;}
QCalendarWidget QAbstractItemView{background:#293c50;color:#eff6ff;}
'''
class FusionView:
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setCompositionMode(QtGui.QPainter.CompositionMode_Source);p.fillRect(self.rect(),QtCore.Qt.transparent)
    def card_widget(self,parent_layout=None,radius=23):
        frame=GlassCard(self,radius);box=QtWidgets.QVBoxLayout(frame);box.setContentsMargins(13,10,13,10);box.setSpacing(5)
        if parent_layout is not None:parent_layout.addWidget(frame)
        return frame,box
    def card(self,layout):return self.card_widget(layout)[1]
    def icon_button(self,name,tip,callback,size=30):
        button=QtWidgets.QToolButton();button.setIcon(glyph(name));button.setIconSize(QtCore.QSize(19,19));button.setToolTip(tip);button.setAccessibleName(tip);button.setFixedSize(size,size);button.clicked.connect(callback);return button
    def show_page(self,page):
        self.scroll.setVerticalScrollBarPolicy(QtCore.Qt.ScrollBarAsNeeded);self.home_mode=page=='home';self.header_frame.setVisible(not self.home_mode)
        self.notification_page=0
        if page not in ('home','bluetooth'):
            self.show_detail(page)
            if page=='wifi':
                content=self.scroll.widget();layout=content.layout();frame,box=self.card_widget()
                while layout.count()>1:
                    item=layout.takeAt(0)
                    if item.widget():box.addWidget(item.widget())
                    elif item.layout():box.addLayout(item.layout())
                # Reserve one stable reading surface, with advanced settings
                # anchored at its bottom even while discovery is still empty.
                layout.takeAt(0);layout.addWidget(frame,1);box.setStretch(1,1);self.network_list.setAlignment(QtCore.Qt.AlignTop)
            if page=='power':
                content=self.scroll.widget();layout=content.layout();frame,box=self.card_widget();layout.insertWidget(max(0,layout.count()-1),frame)
                row=QtWidgets.QGridLayout();box.addLayout(row)
                for i,(action,title) in enumerate([('lock','锁屏'),('suspend','睡眠'),('hibernate','休眠'),('logout','注销'),('reboot','重新启动'),('poweroff','关机')]):
                    button=QtWidgets.QPushButton(title);button.setObjectName('session_'+action);button.clicked.connect(lambda checked=False,a=action,t=title:self.confirm_session(a,t));row.addWidget(button,i//2,i%2)
                self.apply_snapshot()
            self.set_position();return
        self.page=page;self.title.setText('蓝牙' if page=='bluetooth' else '控制中心');self.back.setVisible(page!='home')
        old=self.scroll.takeWidget()
        if old:old.deleteLater()
        self.volume=self.output=self.mute=self.wifi_toggle=self.network_list=self.power_text=self.profile=self.summary=self.calendar=None
        content=QtWidgets.QWidget();content.setObjectName('content');layout=QtWidgets.QVBoxLayout(content);layout.setContentsMargins(0,0,0,0);layout.setSpacing(7)
        if page=='bluetooth':
            frame,box=self.card_widget(layout);row=QtWidgets.QHBoxLayout();self.bluetooth_toggle=QtWidgets.QCheckBox('启用蓝牙');self.bluetooth_toggle.clicked.connect(lambda value:self.action('bluetooth-enabled',{'value':value}));row.addWidget(self.bluetooth_toggle,1);scan=QtWidgets.QPushButton('查找设备');scan.clicked.connect(lambda:self.action('bluetooth-scan',{}));row.addWidget(scan);box.addLayout(row);self.bluetooth_list=QtWidgets.QVBoxLayout();box.addLayout(self.bluetooth_list);self.bluetooth_signature=None;layout.addStretch()
        else:self.build_home(layout)
        self.scroll.setWidget(content);content.setAutoFillBackground(False);self.scroll.viewport().setAutoFillBackground(False);self.apply_snapshot();self.set_position()
    def build_home(self,layout):
        top=QtWidgets.QHBoxLayout();top.setSpacing(7);layout.addLayout(top)
        left=QtWidgets.QVBoxLayout();left.setSpacing(7);top.addLayout(left,1)
        self.tiles={}
        for kind,title in [('wifi','Wi-Fi'),('bluetooth','蓝牙')]:
            frame,box=self.card_widget(left,27);frame.setFixedHeight(67);row=QtWidgets.QHBoxLayout();row.setSpacing(8);box.addLayout(row);box.setContentsMargins(10,7,9,7)
            toggle=self.icon_button(kind,'开关'+title,lambda checked=False,k=kind:self.toggle_connectivity(k),42);toggle.setProperty('round',True);toggle.setCheckable(True);row.addWidget(toggle)
            words=QtWidgets.QVBoxLayout();words.setSpacing(0);label=QtWidgets.QPushButton(title);label.setProperty('flatText',True);label.setStyleSheet('font-size:14px;font-weight:600;');label.clicked.connect(lambda checked=False,k=kind:self.navigate(k));words.addWidget(label);detail=self.label('正在读取',True);detail.setWordWrap(False);words.addWidget(detail);row.addLayout(words,1)
            arrow=CardChevron(title+'详情');arrow.setObjectName(kind+'_details');arrow.clicked.connect(lambda checked=False,k=kind:self.navigate(k));row.addWidget(arrow);self.tiles[kind]=(frame,toggle,detail)
        frame,box=self.card_widget(radius=25);top.addWidget(frame,1);frame.setFixedHeight(141);self.media_card=frame
        row=QtWidgets.QHBoxLayout();self.media_cover=QtWidgets.QLabel();self.media_cover.setFixedSize(43,43);self.media_cover.setPixmap(glyph('music',size=35).pixmap(35,35));row.addWidget(self.media_cover);words=QtWidgets.QVBoxLayout();words.setSpacing(1);self.media_title=self.label('暂无播放');self.media_title.setProperty('cardTitle',True);self.media_title.setMaximumHeight(43);self.media_artist=self.label('媒体播放',True);self.media_artist.setMaximumHeight(30);words.addWidget(self.media_title);words.addWidget(self.media_artist);row.addLayout(words,1);box.addLayout(row);box.addStretch();transport=QtWidgets.QHBoxLayout();self.media_buttons={}
        for key,shape in [('Previous','previous'),('PlayPause','play'),('Next','next')]:
            button=self.icon_button(shape,{'Previous':'上一首','PlayPause':'播放 / 暂停','Next':'下一首'}[key],lambda checked=False,m=key:self.media_command(m),30);transport.addWidget(button);self.media_buttons[key]=button
        box.addLayout(transport)
        row=QtWidgets.QHBoxLayout();row.setSpacing(7);layout.addLayout(row)
        display=self.icon_button('display','显示设置',lambda:self.launch_args('ukui-control-center',['-m','Display']),43);display.setStyleSheet('background:rgba(70,80,100,180);border-radius:21px;');row.addWidget(display)
        power=self.icon_button('power','电池、电源与会话',lambda:self.navigate('power'),43);power.setProperty('primary',True);power.setStyleSheet('background:#bd803d;border-radius:21px;');row.addWidget(power)
        dndframe,dndbox=self.card_widget(radius=23);dndframe.setFixedHeight(43);row.addWidget(dndframe,1);dndbox.setContentsMargins(8,0,8,0);self.dnd=QtWidgets.QPushButton('勿扰模式');self.dnd.setIcon(glyph('bell'));self.dnd.setCheckable(True);self.dnd.setFixedHeight(41);self.dnd.setProperty('flatText',True);self.dnd.clicked.connect(lambda checked:self.action('dnd',{'value':checked}));dndbox.addWidget(self.dnd);self.dnd_card=dndframe
        frame,box=self.card_widget(layout);self.brightness,self.brightness_label=self.slider_card(box,'显示亮度','sun',5,100,'brightness');self.brightness.setToolTip('通过系统显示服务调节画面亮度；显示器的其他选项可从显示设置打开。');frame.setFixedHeight(70)
        frame,box=self.card_widget(layout);self.build_audio(box);frame.setFixedHeight(76)
        frame,box=self.card_widget(layout);head=QtWidgets.QHBoxLayout();title=self.label('通知历史');title.setProperty('cardTitle',True);head.addWidget(title,1);self.clear_notifications=QtWidgets.QPushButton('清空');self.clear_notifications.setFixedHeight(25);self.clear_notifications.clicked.connect(lambda:self.action('notifications-clear',{}));head.addWidget(self.clear_notifications);box.addLayout(head);self.notification_reader=QtWidgets.QTextBrowser();self.notification_reader.setObjectName('notificationReader');self.notification_reader.setOpenLinks(False);self.notification_reader.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);self.notification_reader.setMinimumHeight(55);self.notification_reader.setSizePolicy(QtWidgets.QSizePolicy.Expanding,QtWidgets.QSizePolicy.Ignored);box.addWidget(self.notification_reader,1);self.notification_signature=None
        nav=QtWidgets.QHBoxLayout();self.notification_prev=QtWidgets.QPushButton('‹');self.notification_next=QtWidgets.QPushButton('›');self.notification_prev.setFixedSize(23,22);self.notification_next.setFixedSize(23,22);self.notification_prev.clicked.connect(lambda:self.page_notifications(-1));self.notification_next.clicked.connect(lambda:self.page_notifications(1));self.notification_count=self.label('',True);nav.addWidget(self.notification_prev);nav.addWidget(self.notification_count);nav.addWidget(self.notification_next);nav.addStretch();self.system_notifications=QtWidgets.QPushButton('系统通知');self.system_notifications.setFixedHeight(24);self.system_notifications.clicked.connect(self.open_system_notifications);nav.addWidget(self.system_notifications);box.addLayout(nav)
        foot,box=self.card_widget(layout,17);foot.setFixedHeight(38);box.setContentsMargins(9,3,9,3);row=QtWidgets.QHBoxLayout();self.power_summary_button=QtWidgets.QPushButton('电池');self.power_summary_button.setProperty('flatText',True);self.power_summary_button.clicked.connect(lambda:self.navigate('power'));row.addWidget(self.power_summary_button,1);settings=QtWidgets.QPushButton('设置');settings.clicked.connect(lambda:self.launch_args(str(__import__('pathlib').Path.home()/'.local/bin/kylin-lqui-settings'),['--page','panel:layout']));row.addWidget(settings);row.addWidget(self.icon_button('close','收起',self.hide,27));box.addLayout(row)
    def slider_card(self,box,title,icon_name,low,high,operation):
        head=QtWidgets.QHBoxLayout();head.addWidget(self.label(title),1);readout=self.label('—');head.addWidget(readout);box.addLayout(head);row=QtWidgets.QHBoxLayout();icon=QtWidgets.QLabel();icon.setPixmap(glyph(icon_name,size=17).pixmap(17,17));row.addWidget(icon);slider=QtWidgets.QSlider(QtCore.Qt.Horizontal);slider.setRange(low,high);row.addWidget(slider,1);box.addLayout(row);slider.valueChanged.connect(lambda value:readout.setText(str(value)+'%'));self.bind_slider(slider,operation);return slider,readout
    def bind_slider(self,slider,operation):
        slider.sliderReleased.connect(lambda:self.action(operation,{'value':slider.value()}))
        def keyboard(action):
            if slider.isSliderDown():return
            value=slider.sliderPosition();QtCore.QTimer.singleShot(0,lambda:self.action(operation,{'value':value}))
        slider.actionTriggered.connect(keyboard)
    def build_audio(self,box):
        head=QtWidgets.QHBoxLayout();head.addWidget(self.label('声音'),1);self.volume_label=self.label('—');head.addWidget(self.volume_label)
        if self.page=='home':
            output=self.icon_button('chevron','输出设备与声音设置',lambda:self.navigate('sound'),24);head.addWidget(output)
        box.addLayout(head);row=QtWidgets.QHBoxLayout();self.mute=self.icon_button('sound','静音 / 取消静音',lambda:self.action('mute',{'value':not (self.data.get('audio',{}).get('selected') or {}).get('muted',False)}),26);row.addWidget(self.mute);self.volume=QtWidgets.QSlider(QtCore.Qt.Horizontal);self.volume.setRange(0,100);row.addWidget(self.volume,1);box.addLayout(row);label=self.volume_label;self.volume.valueChanged.connect(lambda value:label.setText(str(value)+'%'));self.bind_slider(self.volume,'volume')
    def set_position(self):
        screen=QtWidgets.QApplication.screenAt(self.anchor.center()) or QtWidgets.QApplication.primaryScreen();area=screen.availableGeometry().adjusted(7,7,-7,-7)
        self.resize(min(390,area.width()),min(660 if self.page=='home' else 440 if self.page in ('wifi','bluetooth') else 520 if self.page=='power' else 340 if self.page=='sound' else 570,area.height()))
        # Align the visible card edge, excluding the transparent window margin.
        x=self.anchor.left()-self.layout().contentsMargins().left();y=self.anchor.top()-self.height()-7
        if y<area.top():y=self.anchor.bottom()+7
        self.move(max(area.left(),min(x,area.right()-self.width()+1)),max(area.top(),min(y,area.bottom()-self.height()+1)))
    def capture_backdrop(self):self.backdrop=blurred_capture(self);self.update();[card.update() for card in self.findChildren(GlassCard)]
    def apply_snapshot(self):
        self.apply_detail_snapshot()
        if self.page!='home':self.status.setVisible(bool(self.status.text()));self.set_position()
        if self.mute:self.mute.setText('');self.mute.setIcon(glyph('muted' if (self.data.get('audio',{}).get('selected') or {}).get('muted') else 'sound'))
        if self.page=='power':
            for key,enabled in self.data.get('session',{}).get('actions',{}).items():
                button=self.findChild(QtWidgets.QPushButton,'session_'+key)
                if button:button.setEnabled(enabled and not self.changing)
        if self.page=='bluetooth':self.update_bluetooth();return
        if self.page!='home':return
        for kind,(frame,toggle,label) in self.tiles.items():
            data=self.data.get(kind,{});enabled=bool(data.get('enabled'));blocker=QtCore.QSignalBlocker(toggle);toggle.setChecked(enabled);del blocker;toggle.setEnabled(bool(data.get('available')) and not self.changing);toggle.setIcon(glyph(kind,'#3277ab' if enabled else '#e4edf7'));frame.set_active(enabled)
            if not data.get('available'):text='未检测到设备'
            elif not enabled:text='已关闭'
            elif kind=='wifi':text=next((n['ssid'] for n in data.get('networks',[]) if n['connected']),'未连接')
            else:text=next((n['name'] for n in data.get('devices',[]) if n['connected']),'已开启')
            label.setText(label.fontMetrics().elidedText(text,QtCore.Qt.ElideRight,100));label.setToolTip(text)
        brightness=self.data.get('brightness',{});self.brightness.setEnabled(bool(brightness.get('available')) and not self.changing)
        if not self.brightness.isSliderDown():blocker=QtCore.QSignalBlocker(self.brightness);self.brightness.setValue(brightness.get('value',5));del blocker;self.brightness_label.setText(str(brightness['value'])+'%' if brightness.get('available') else '未支持')
        notify=self.data.get('notifications',{});blocker=QtCore.QSignalBlocker(self.dnd);self.dnd.setChecked(bool(notify.get('dnd')));del blocker;self.dnd.setEnabled(bool(notify.get('dndAvailable')) and not self.changing);self.dnd.setText('勿扰已开启' if notify.get('dnd') else '勿扰模式');self.dnd_card.set_active(bool(notify.get('dnd')))
        self.update_notifications();player=self.data.get('media',{}).get('player',{});self.media_title.setText(player.get('title') or '暂无播放');self.media_artist.setText(player.get('artist') or '打开播放器即可控制');self.media_card.set_active(bool(player.get('playing')))
        for method,button in self.media_buttons.items():button.setEnabled(bool(player.get('canControl')) and (method=='PlayPause' or player.get('can'+method)) and not self.changing)
        self.media_buttons['PlayPause'].setIcon(glyph('pause' if player.get('playing') else 'play'))
        art=player.get('art','')
        if getattr(self,'media_art',None)!=art:
            self.media_art=art;url=QtCore.QUrl(art);image=QtGui.QPixmap(url.toLocalFile()) if url.isLocalFile() else QtGui.QPixmap()
            if not image.isNull():
                dpr=self.devicePixelRatioF();cover=image.scaled(round(43*dpr),round(43*dpr),QtCore.Qt.KeepAspectRatio,QtCore.Qt.SmoothTransformation);cover.setDevicePixelRatio(dpr);self.media_cover.setPixmap(cover)
            else:self.media_cover.setPixmap(glyph('music',size=33).pixmap(33,33))
        self.power_summary_button.setText(self.power_summary(self.data.get('power',{})))
    def toggle_connectivity(self,kind):self.action('wifi-enabled' if kind=='wifi' else 'bluetooth-enabled',{'value':not self.data.get(kind,{}).get('enabled',False)})
    def media_command(self,method):self.action('media',{'service':self.data.get('media',{}).get('player',{}).get('service'),'method':method})
    def update_bluetooth(self):
        data=self.data.get('bluetooth',{});blocker=QtCore.QSignalBlocker(self.bluetooth_toggle);self.bluetooth_toggle.setChecked(data.get('enabled',False));del blocker;self.bluetooth_toggle.setEnabled(bool(data.get('available')) and not self.changing);signature=json.dumps([data,self.changing],sort_keys=True)
        if signature==self.bluetooth_signature:return
        self.bluetooth_signature=signature
        while self.bluetooth_list.count():
            item=self.bluetooth_list.takeAt(0)
            if item.widget():item.widget().deleteLater()
        if not data.get('available'):self.bluetooth_list.addWidget(self.label('未检测到蓝牙适配器',True))
        elif not data.get('enabled'):self.bluetooth_list.addWidget(self.label('蓝牙已关闭',True))
        elif not data.get('devices'):self.bluetooth_list.addWidget(self.label('点击“查找设备”，配对确认由系统处理。',True))
        for device in data.get('devices',[]):
            button=QtWidgets.QPushButton(device['name'].replace('&','&&')+' · '+('断开连接' if device['connected'] else '连接' if device['paired'] else '配对并连接'));button.setEnabled(not self.changing);button.clicked.connect(lambda checked=False,d=device:self.action('bluetooth-device',{'id':d['id']}));self.bluetooth_list.addWidget(button)
    def page_notifications(self,delta):self.notification_page=max(0,self.notification_page+delta);self.notification_signature=None;self.update_notifications()
    def update_notifications(self):
        data=self.data.get('notifications',{});entries=data.get('entries',[]);pages=max(1,math.ceil(len(entries)/2));self.notification_page=min(self.notification_page,pages-1);signature=json.dumps([entries,self.notification_page,data.get('systemCount')],sort_keys=True)
        self.clear_notifications.setEnabled(bool(entries) and not self.changing);self.notification_prev.setEnabled(self.notification_page>0);self.notification_next.setEnabled(self.notification_page<pages-1);self.notification_count.setText(str(self.notification_page+1)+' / '+str(pages) if entries else '');self.system_notifications.setText('系统通知'+(' · '+str(data['systemCount']) if data.get('systemCount') else ''))
        if signature==self.notification_signature:return
        self.notification_signature=signature
        parts=[]
        for item in entries[self.notification_page*2:self.notification_page*2+2]:
            document=QtGui.QTextDocument();document.setHtml(item.get('body',''))
            body=html.escape(document.toPlainText()).replace('\n','<br>')
            title=html.escape(item.get('app','')+' · '+item.get('title',''))
            parts.append('<p style="margin:0 0 12px;color:#eef3fa"><b>'+title+'</b><br>'+body+'</p>')
        self.notification_reader.setHtml(''.join(parts) or '<p style="color:#c4cfde">暂无新通知</p>')
        self.notification_reader.verticalScrollBar().setValue(0)
    def confirm_session(self,action,title):
        self.dialog_open=True;dialog=QtWidgets.QMessageBox(self);dialog.setWindowTitle(title);dialog.setText('确认'+title+'？');dialog.setStandardButtons(QtWidgets.QMessageBox.Yes|QtWidgets.QMessageBox.Cancel);dialog.setDefaultButton(QtWidgets.QMessageBox.Cancel);dialog.button(QtWidgets.QMessageBox.Yes).setText(title);dialog.button(QtWidgets.QMessageBox.Cancel).setText('取消');dialog.setStyleSheet('QMessageBox{background:#27384b;}QLabel{color:#edf4fa;}QPushButton{background:#40546a;color:#edf4fa;padding:7px 18px;border-radius:8px;}');result=dialog.exec_();dialog.deleteLater();self.dialog_open=False
        if result==QtWidgets.QMessageBox.Yes:self.action('session',{'action':action})
    def open_system_notifications(self):
        from gi.repository import Gio,GLib
        def call():
            backend=self.backend_factory();return backend.session_call('org.ukui.Sidebar','/org/ukui/Sidebar','org.ukui.Sidebar','showSidebar',GLib.Variant('(s)',('notification',)))
        self.job(call,lambda result:self.hide() if result['ok'] else self.status.setText('系统通知暂未响应'))
    def launch_args(self,program,args):QtCore.QProcess.startDetached(program,args);self.hide()
