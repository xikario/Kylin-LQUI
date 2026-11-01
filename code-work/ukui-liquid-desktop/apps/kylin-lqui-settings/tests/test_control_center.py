#!/usr/bin/python3
import copy,json,os,sys,unittest,tempfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
from system_backend import SystemBackend,NM,NMP,calendar_data
from control_center import ControlCenter

SAMPLE={'audio':{'available':True,'selected':{'id':'speaker','volume':42,'muted':False},'outputs':[{'id':'speaker','name':'内置扬声器'},{'id':'headset','name':'USB 耳机'}]},'wifi':{'available':True,'hardwareEnabled':True,'enabled':True,'devices':['/device'],'networks':[{'id':'/ap','ssid':'测试无线网络','strength':88,'connected':True,'profile':'/profile','security':'psk'},{'id':'/other','ssid':'访客网络','strength':60,'connected':False,'profile':'','security':'open'}]},'power':{'available':True,'present':True,'percentage':76,'state':2,'onBattery':True,'timeToEmpty':7200,'capacity':95,'profiles':['balanced','power-saver'],'activeProfile':'balanced'}}
SAMPLE.update(bluetooth={'available':True,'enabled':True,'devices':[{'id':'/device1','name':'蓝牙耳机','paired':True,'connected':False}]},brightness={'available':True,'value':80,'maximum':512},media={'available':True,'player':{'title':'测试曲目','artist':'测试播放器','canControl':True,'canPrevious':True,'canNext':True,'playing':True,'service':'org.mpris.MediaPlayer2.fixture'},'players':[]},notifications={'available':True,'dndAvailable':True,'dnd':False,'entries':[],'systemCount':0},session={'actions':{key:True for key in ('lock','suspend','hibernate','logout','reboot','poweroff')}})
class Fixture:
    calls=[]
    def snapshot(self):return copy.deepcopy(SAMPLE)
    def action(self,operation,values):self.calls.append((operation,dict(values)));return self.snapshot()
configuration=tempfile.TemporaryDirectory(prefix='lqui-control-test-')
os.environ['XDG_CONFIG_HOME']=configuration.name
app=QtWidgets.QApplication([])
app.setQuitOnLastWindowClosed(False)
class ControlUi(unittest.TestCase):
    def setUp(self):
        self.window=ControlCenter(Fixture);self.window.open_at('home',1500,1030,40,40)
        deadline=QtCore.QElapsedTimer();deadline.start()
        while (self.window.reading or not self.window.data or self.window.material_loading or self.window.popup_motion.state()==QtCore.QAbstractAnimation.Running) and deadline.elapsed()<3000:QtTest.QTest.qWait(20)
        self.assertTrue(self.window.data);self.assertFalse(self.window.reading)
    def tearDown(self):
        self.window.poll.stop();self.window.material_pool.waitForDone(5000);QtCore.QThreadPool.globalInstance().waitForDone(5000);QtTest.QTest.qWait(10);self.window.hide();self.window.deleteLater();QtTest.QTest.qWait(10)
    def test_pages_fit_screen_and_have_content(self):
        root=Path(os.environ.get('LQUI_CENTER_ARTIFACT','/tmp/lqui-control-center'));root.mkdir(exist_ok=True,parents=True)
        for page in ('home','wifi','bluetooth','sound','power','calendar'):
            self.window.show_page(page);QtTest.QTest.qWait(150)
            area=app.primaryScreen().availableGeometry();self.assertTrue(area.contains(self.window.geometry()))
            self.assertLessEqual(self.window.scroll.widget().width(),self.window.scroll.viewport().width())
            self.assertTrue(self.window.scroll.findChildren(QtWidgets.QPushButton) or self.window.scroll.findChildren(QtWidgets.QLabel))
            if page=='sound':self.assertEqual(self.window.output.count(),2);self.assertEqual(self.window.output.currentText(),'内置扬声器')
            self.window.grab().save(str(root/(page+'.png')))
        deadline=QtCore.QElapsedTimer();deadline.start()
        while not self.window.calendar.dates and deadline.elapsed()<3000:QtTest.QTest.qWait(25)
        self.assertTrue(self.window.calendar.dates)
    def test_slider_commits_only_on_release(self):
        Fixture.calls=[];slider=self.window.volume;slider.setSliderDown(True);slider.setValue(18);slider.setValue(54);QtTest.QTest.qWait(20);self.assertFalse(Fixture.calls)
        slider.setSliderDown(False);QtTest.QTest.qWait(70);self.assertEqual(Fixture.calls,[('volume',{'value':54})])
    def test_stale_read_cannot_overwrite_completed_action(self):
        jobs=[];self.window.job=lambda fn,cb:jobs.append((fn,cb));self.window.refresh();old=jobs.pop();self.window.action('mute',{'value':True});write=jobs.pop()
        changed=copy.deepcopy(SAMPLE);changed['audio']['selected']['muted']=True
        write[1]({'ok':True,'data':changed});old[1]({'ok':True,'data':copy.deepcopy(SAMPLE)})
        self.assertTrue(self.window.data['audio']['selected']['muted'])
    def test_all_home_controls_fit_and_use_same_popup(self):
        self.assertEqual(self.window.page,'home')
        self.assertEqual(self.window.scroll.verticalScrollBar().maximum(),0)
        for page in ('wifi','sound','power','notifications'):
            self.window.hide();self.window.open_at(page,1500,1030,25,33);QtTest.QTest.qWait(30);self.assertEqual(self.window.page,'home')
        self.assertTrue(self.window.media_buttons['PlayPause'].isEnabled());self.assertTrue(self.window.brightness.isEnabled());self.assertTrue(self.window.dnd.isEnabled())
    def test_long_notification_pages_do_not_clip_footer(self):
        self.window.data['notifications']['entries']=[{'id':str(i),'app':'测试应用','title':'较长标题 '*12,'body':'较长正文 '*60} for i in range(8)]
        self.window.apply_snapshot();QtTest.QTest.qWait(30);self.assertEqual(self.window.scroll.verticalScrollBar().maximum(),0)
        self.window.notification_next.click();self.assertEqual(self.window.notification_page,1);self.assertEqual(len(self.window.data['notifications']['entries']),8)
    def test_wifi_refresh_keeps_geometry_and_existing_row_identity(self):
        self.window.poll.stop();self.window.show_page('wifi');QtTest.QTest.qWait(30)
        rect=self.window.geometry();button=self.window.network_buttons['/ap']
        for count in (1,8,2,12,1,8):
            self.window.data['wifi']['networks']=[dict(SAMPLE['wifi']['networks'][0],id='/ap' if i==0 else '/ap'+str(i),strength=50+i,connected=i==0) for i in range(count)]
            self.window.apply_snapshot();QtTest.QTest.qWait(20)
            self.assertEqual(rect,self.window.geometry());self.assertIs(button,self.window.network_buttons['/ap'])
        self.assertTrue(self.window.scroll.verticalScrollBar().maximum()>0)
    def test_notification_reader_reaches_end_and_keeps_scroll_on_poll(self):
        text='长通知内容。'*350+'最后一行完整可读'
        self.window.data['notifications']['entries']=[{'app':'应用','title':'标题','body':text}]
        self.window.apply_snapshot();QtTest.QTest.qWait(30)
        reader=self.window.notification_reader;bar=reader.verticalScrollBar()
        self.assertIn('最后一行完整可读',reader.toPlainText());self.assertGreater(bar.maximum(),0)
        bar.setValue(bar.maximum());self.window.apply_snapshot();self.assertEqual(bar.value(),bar.maximum())
    def test_tooltip_is_independent_of_dark_button_style(self):
        button=next(b for b in self.window.findChildren(QtWidgets.QToolButton) if b.toolTip()=='显示设置')
        event=QtGui.QHelpEvent(QtCore.QEvent.ToolTip,button.rect().center(),button.mapToGlobal(button.rect().center()))
        QtWidgets.QApplication.sendEvent(button,event);QtTest.QTest.qWait(10)
        bubble=self.window.tooltips.bubble;self.assertTrue(bubble.isVisible());self.assertEqual(bubble.text(),'显示设置')
        self.assertEqual(bubble.palette().color(QtGui.QPalette.WindowText).name(),'#283039')
        self.assertFalse(bubble.mask().contains(QtCore.QPoint(0,0)));self.assertTrue(bubble.testAttribute(QtCore.Qt.WA_TranslucentBackground))
        self.assertEqual(bubble.grab().toImage().pixelColor(0,0).alpha(),0)
    def test_primary_save_button_changes_appearance_on_press(self):
        from design import STYLE
        button=QtWidgets.QPushButton('保存');button.setProperty('primary',True);button.setStyleSheet(STYLE);button.resize(100,40);button.show();QtTest.QTest.qWait(10)
        normal=button.grab().toImage();QtTest.QTest.mousePress(button,QtCore.Qt.LeftButton);pressed=button.grab().toImage()
        self.assertNotEqual(normal,pressed);QtTest.QTest.mouseRelease(button,QtCore.Qt.LeftButton);button.close();button.deleteLater()
    def test_session_cancel_sends_no_command(self):
        Fixture.calls=[]
        with patch.object(QtWidgets.QMessageBox,'exec_',return_value=QtWidgets.QMessageBox.Cancel):self.window.confirm_session('poweroff','关机')
        self.assertFalse(Fixture.calls);self.assertFalse(self.window.dialog_open)
    def test_hidden_window_stops_polling(self):
        self.assertTrue(self.window.poll.isActive());self.window.hide();self.assertFalse(self.window.poll.isActive());self.assertTrue(self.window.idle.isActive())
    def test_detail_clicks_and_back_keep_window_visible_and_material(self):
        class VisibilityEvents(QtCore.QObject):
            def __init__(self):super().__init__();self.events=[]
            def eventFilter(self,owner,event):
                if event.type() in (QtCore.QEvent.Hide,QtCore.QEvent.Show):self.events.append(event.type())
                return False
        events=VisibilityEvents();self.window.installEventFilter(events)
        native_id=int(self.window.winId());revision=self.window.material_revision
        material=self.window.native_glass;backdrop_key=self.window.backdrop.cacheKey()
        for page in ('wifi','bluetooth','sound','power')*2:
            if page in ('wifi','bluetooth'):button=self.window.findChild(QtWidgets.QAbstractButton,page+'_details')
            else:button=next(b for b in self.window.findChildren(QtWidgets.QToolButton) if b.toolTip()=={'sound':'输出设备与声音设置','power':'电池、电源与会话'}[page])
            self.assertIsNotNone(button);QtTest.QTest.mouseClick(button,QtCore.Qt.LeftButton);QtTest.QTest.qWait(400)
            self.assertEqual(self.window.page,page);self.assertTrue(self.window.isVisible());self.assertFalse(self.window.closing)
            self.assertTrue(self.window.poll.isActive());self.assertFalse(self.window.idle.isActive())
            QtTest.QTest.mouseClick(self.window.back,QtCore.Qt.LeftButton);QtTest.QTest.qWait(150)
            self.assertEqual(self.window.page,'home');self.assertTrue(self.window.isVisible())
        self.assertEqual(events.events,[]);self.assertEqual(int(self.window.winId()),native_id)
        self.assertEqual(self.window.material_revision,revision);self.assertIs(self.window.native_glass,material)
        self.assertEqual(self.window.backdrop.cacheKey(),backdrop_key)
    def test_detail_click_while_reading_applies_result_to_current_page(self):
        pending=[];self.window.job=lambda fn,cb:pending.append((fn,cb))
        self.window.refresh();self.assertTrue(self.window.reading)
        for page in ('wifi','bluetooth','sound'):
            if page!='wifi':self.window.back.click()
            button=self.window.findChild(QtWidgets.QAbstractButton,page+'_details') if page in ('wifi','bluetooth') else next(b for b in self.window.findChildren(QtWidgets.QToolButton) if b.toolTip()=='输出设备与声音设置')
            QtTest.QTest.mouseClick(button,QtCore.Qt.LeftButton);QtTest.QTest.qWait(30)
        result=copy.deepcopy(SAMPLE);result['audio']['selected']['volume']=64
        pending[0][1]({'ok':True,'data':result});QtTest.QTest.qWait(400)
        self.assertEqual(self.window.page,'sound');self.assertTrue(self.window.isVisible());self.assertEqual(self.window.volume.value(),64)
        self.assertFalse(self.window.reading);self.assertEqual(len(pending),1)
    def test_dock_motion_reverses_and_stops_without_rebuilding_material(self):
        self.window.dialog_open=True # exclude platform focus changes from the motion-only check
        self.window.hide();self.window.open_at('home',1500,1030,40,40);target=QtCore.QRect(self.window.popup_target)
        QtTest.QTest.qWait(70);self.assertGreater(self.window.y(),target.y());self.assertFalse(self.window.mask().isEmpty())
        self.window.dismiss();QtTest.QTest.qWait(30);self.window.open_at('home',1500,1030,40,40)
        QtTest.QTest.qWait(350);self.assertEqual(self.window.geometry(),target);self.assertTrue(self.window.mask().isEmpty());self.assertFalse(self.window.state()['motionRunning'])
        self.window.dismiss();deadline=QtCore.QElapsedTimer();deadline.start()
        while self.window.isVisible() and deadline.elapsed()<1200:QtTest.QTest.qWait(20)
        self.assertFalse(self.window.isVisible());self.assertIsNone(self.window.native_glass);self.assertFalse(self.window.poll.isActive());self.assertFalse(self.window.state()['motionRunning'])
    def test_reduced_motion_opens_and_closes_immediately(self):
        setting=QtCore.QSettings('org.ukui','style');old=setting.value('reduceAnimation');setting.setValue('reduceAnimation',True)
        try:
            self.window.hide();self.window.open_at('home',1500,1030,40,40)
            self.assertEqual(self.window.popup_progress,1.);self.assertFalse(self.window.state()['motionRunning']);self.window.dismiss();self.assertFalse(self.window.isVisible())
        finally:
            if old is None:setting.remove('reduceAnimation')
            else:setting.setValue('reduceAnimation',old)
    def test_reopening_same_page_keeps_controls_and_discards_hidden_material(self):
        self.window.hide();volume=self.window.volume;pending=[]
        self.window.job=lambda work,done,**unused:pending.append((work,done))
        self.window.open_at('home',1500,1030,40,40)
        self.assertIs(volume,self.window.volume)
        self.assertTrue(self.window.isVisible());self.assertGreaterEqual(len(pending),1)
        self.window.hide()
        class Material:
            closed=False
            def close(self):self.closed=True
        material=Material();pending[0][1]({'ok':True,'data':material})
        self.assertTrue(material.closed);self.assertIsNone(self.window.native_glass)
    def test_rapid_toggles_keep_only_the_latest_material_request(self):
        self.window.hide();pending=[];self.window.refresh=lambda:None
        self.window.job=lambda work,done,**unused:pending.append((work,done))
        image=QtGui.QImage(40,40,QtGui.QImage.Format_RGB32);image.fill(QtCore.Qt.blue)
        def capture(owner,**unused):owner.prepare_material(image,QtCore.QRect(0,0,800,600),owner.geometry());return QtGui.QImage()
        with patch('fusion_view.blurred_capture',side_effect=capture):
            for _ in range(12):self.window.open_at('home',1500,1030,40,40);self.window.hide()
            self.window.open_at('home',1500,1030,40,40)
        self.assertEqual(len(pending),1)
        class Material:
            closed=False
            def close(self):self.closed=True
        old=Material();pending[0][1]({'ok':True,'data':old});self.assertTrue(old.closed);self.assertEqual(len(pending),2)
        self.window.hide();latest=Material();pending[1][1]({'ok':True,'data':latest});self.assertTrue(latest.closed);self.assertIsNone(self.window.material_pending)
    def test_wifi_pending_does_not_claim_connected(self):
        self.window.show_page('wifi');self.window.pending_network=('/other',__import__('time').monotonic());self.window.update_pending_network();self.window.apply_snapshot()
        self.assertIn('正在连接',self.window.status.text());self.assertTrue(any('连接中' in b.text() for b in self.window.scroll.findChildren(QtWidgets.QPushButton)))

    def test_bluetooth_scan_does_not_block_audio_and_latest_slider_wins(self):
        jobs=[];self.window.job=lambda work,done:jobs.append((work,done))
        self.window.action('bluetooth-scan',{});self.window.action('volume',{'value':30});self.window.action('volume',{'value':80})
        self.assertEqual(len(jobs),2);self.assertTrue(self.window.volume.isEnabled());self.assertTrue(self.window.brightness.isEnabled())
        result=jobs[1][0]();jobs[1][1]({'ok':True,'data':result});self.assertEqual(len(jobs),3)
        result=jobs[2][0]();jobs[2][1]({'ok':True,'data':result})
        self.assertEqual(Fixture.calls[-1],('volume',{'value':80}));self.assertTrue(self.window.is_busy('bluetooth'));self.assertFalse(self.window.is_busy('audio'))
        jobs[0][1]({'ok':True,'data':copy.deepcopy(SAMPLE)})
        self.assertFalse(self.window.changing)
    def test_same_media_art_is_replayed_when_home_is_rebuilt(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            image=QtGui.QImage(43,43,QtGui.QImage.Format_RGB32);image.fill(QtCore.Qt.red);path=Path(folder)/'cover.png';image.save(str(path))
            self.window.data['media']['player']['art']=QtCore.QUrl.fromLocalFile(str(path)).toString();self.window.apply_snapshot()
            for page in ('sound','home','bluetooth','home'):
                self.window.show_page(page)
                if page=='home':self.assertEqual(self.window.media_cover.pixmap().toImage().pixelColor(10,10),QtGui.QColor('red'))


class Adapter(unittest.TestCase):
    def test_pulseaudio_legacy_format(self):
        b=SystemBackend();b.command=lambda argv:'Default Sink: test\n' if argv[-1]=='info' else 'Sink #0\n Name: test\n Description: Speakers\n Mute: yes\n Volume: front-left: 32768 / 50% / -18.06 dB, front-right: 32768 / 50% / -18.06 dB\n'
        self.assertEqual(b.audio()['selected'],{'id':'test','name':'Speakers','volume':50,'muted':True})
    def test_command_input_validation(self):
        b=SystemBackend();calls=[];b.command=lambda argv:calls.append(argv) or '';b.snapshot=lambda:{}
        for value in (-1,101,True,'60; malicious'):
            with self.assertRaises(ValueError):b.action('volume',{'value':value})
        self.assertEqual(calls,[]);b.action('volume',{'value':57});self.assertEqual(calls,[['/usr/bin/pactl','set-sink-volume','@DEFAULT_SINK@','57%']])
    def test_wifi_connection_uses_typed_dbus_and_memory_policy(self):
        b=SystemBackend();calls=[];b.wifi=lambda:{'networks':[{'id':'/ap','device':'/dev','ssid':'TEST','profile':'','security':'psk'}]};b.props=lambda *a:{'Ssid':b'TEST'};b.snapshot=lambda:{};b.call=lambda *a:calls.append(a)
        b.action('connect',{'id':'/ap','password':'test-password','remember':False})
        call=calls[0];self.assertEqual(call[3],'AddAndActivateConnection2');params=call[4].unpack();self.assertEqual(params[3]['persist'],'memory');self.assertFalse(params[0]['connection']['autoconnect']);self.assertEqual(params[0]['802-11-wireless-security']['psk'],'test-password')
    def test_actual_state_needed_for_connection(self):
        b=SystemBackend()
        def props(service,path,interface):
            if path==NMP:return {'WirelessEnabled':True,'WirelessHardwareEnabled':True}
            if interface.endswith('.Wireless'):return {'ActiveAccessPoint':'/ap','AccessPoints':['/ap']}
            if interface.endswith('.Device'):return {'DeviceType':2,'State':70,'AvailableConnections':[]}
            return {'Ssid':b'TEST','Strength':80,'RsnFlags':256,'Flags':1}
        b.props=props;b.call=lambda *args:(['/device'],)
        n=b.wifi()['networks'][0];self.assertFalse(n['connected']);self.assertTrue(n['connecting'])
    def test_brightness_commits_to_session_display_service(self):
        b=SystemBackend();calls=[];b.brightness=lambda:{'available':True,'value':70};b.session_call=lambda *args:calls.append(args) or (0,)
        b.desktop_action('brightness',{'value':65});self.assertEqual(calls[0][3],'setPrimaryBrightness');self.assertEqual(calls[0][4].unpack(),('Kylin-LQUI',65))
        with self.assertRaises(ValueError):b.desktop_action('brightness',{'value':0})
    def test_bluetooth_routes_only_current_devices(self):
        b=SystemBackend();calls=[];b.bluetooth=lambda:SAMPLE['bluetooth'];b.call=lambda *args,**kwargs:calls.append(args)
        b.desktop_action('bluetooth-device',{'id':'/device1'});self.assertEqual(calls[-1][3],'Connect')
        with self.assertRaises(ValueError):b.desktop_action('bluetooth-device',{'id':'/unknown'})
    def test_power_action_is_validated_before_dispatch(self):
        b=SystemBackend();calls=[];b.session=lambda:{'actions':{'poweroff':False}};b.session_call=lambda *args:calls.append(args)
        with self.assertRaises(ValueError):b.desktop_action('session',{'action':'poweroff'})
        self.assertFalse(calls)
    def test_missing_service_isolated_from_other_sections(self):
        b=SystemBackend();b.audio=lambda:SAMPLE['audio'];b.wifi=lambda:(_ for _ in ()).throw(RuntimeError());b.power=lambda:SAMPLE['power'];r=b.snapshot();self.assertTrue(r['audio']['available']);self.assertFalse(r['wifi']['available']);self.assertTrue(r['power']['available'])
if __name__=='__main__':unittest.main()
