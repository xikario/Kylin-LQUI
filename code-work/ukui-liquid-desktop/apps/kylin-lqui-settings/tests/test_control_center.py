#!/usr/bin/python3
import copy,json,os,sys,unittest,tempfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
from system_backend import SystemBackend,NM,NMP,calendar_data
from control_center import ControlCenter
from audio_events import AudioEvents
from volume_feedback import VolumeFeedback

SAMPLE={'audio':{'available':True,'selected':{'id':'speaker','volume':42,'muted':False},'outputs':[{'id':'speaker','name':'内置扬声器'},{'id':'headset','name':'USB 耳机'}]},'wifi':{'available':True,'hardwareEnabled':True,'enabled':True,'devices':['/device'],'networks':[{'id':'/ap','ssid':'测试无线网络','strength':88,'connected':True,'profile':'/profile','security':'psk'},{'id':'/other','ssid':'访客网络','strength':60,'connected':False,'profile':'','security':'open'}]},'power':{'available':True,'present':True,'percentage':76,'state':2,'onBattery':True,'timeToEmpty':7200,'capacity':95,'profiles':['balanced','power-saver'],'activeProfile':'balanced'}}
SAMPLE.update(bluetooth={'available':True,'enabled':True,'devices':[{'id':'/device1','name':'蓝牙耳机','paired':True,'connected':False}]},brightness={'available':True,'value':80,'maximum':512},media={'available':True,'player':{'title':'测试曲目','artist':'测试播放器','canControl':True,'canPrevious':True,'canNext':True,'playing':True,'service':'org.mpris.MediaPlayer2.fixture'},'players':[]},notifications={'available':True,'dndAvailable':True,'dnd':False,'entries':[],'systemCount':0},session={'actions':{key:True for key in ('lock','suspend','hibernate','logout','reboot','poweroff')}})
SAMPLE['microphone']={'available':True,'selected':{'id':'mic','index':'1','name':'内置麦克风','volume':61,'muted':False},'inputs':[{'id':'mic','index':'1','name':'内置麦克风','volume':61,'muted':False},{'id':'usb','index':'2','name':'USB 麦克风','volume':70,'muted':False}]}
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
        while (self.window.reading or not self.window.data or self.window.material_loading or self.window.opening_pending or self.window.popup_motion.state()==QtCore.QAbstractAnimation.Running) and deadline.elapsed()<3000:QtTest.QTest.qWait(20)
        self.assertTrue(self.window.data);self.assertFalse(self.window.reading)
    def tearDown(self):
        self.window.hide();self.window.poll.stop();self.window.material_pool.waitForDone(5000);QtCore.QThreadPool.globalInstance().waitForDone(5000);QtTest.QTest.qWait(10);self.window.hide();self.window.deleteLater();QtTest.QTest.qWait(10)
    def test_pages_fit_screen_and_have_content(self):
        root=Path(os.environ.get('LQUI_CENTER_ARTIFACT','/tmp/lqui-control-center'));root.mkdir(exist_ok=True,parents=True)
        for page in ('home','wifi','bluetooth','sound','power','calendar'):
            self.window.show_page(page);QtTest.QTest.qWait(150)
            area=app.primaryScreen().availableGeometry();self.assertTrue(area.contains(self.window.geometry()))
            self.assertLessEqual(self.window.scroll.widget().width(),self.window.scroll.viewport().width())
            self.assertTrue(self.window.scroll.findChildren(QtWidgets.QPushButton) or self.window.scroll.findChildren(QtWidgets.QLabel))
            if page=='sound':
                self.assertEqual(len(self.window.audio_device_lists['output']['buttons']),2);self.assertTrue(self.window.audio_device_lists['output']['buttons']['speaker'].property('current'))
                self.assertEqual(self.window.mic_volume.value(),61);self.assertFalse(self.window.findChildren(QtWidgets.QComboBox))
            self.window.grab().save(str(root/(page+'.png')))
        deadline=QtCore.QElapsedTimer();deadline.start()
        while not self.window.calendar.dates and deadline.elapsed()<3000:QtTest.QTest.qWait(25)
        self.assertTrue(self.window.calendar.dates)
    def test_new_wifi_hotspot_and_strength_changes_reorder_existing_rows(self):
        w=self.window;w.show_page('wifi');QtTest.QTest.qWait(50);w.poll.stop()
        existing=w.network_buttons['/other'];data=w.data['wifi']['networks']
        strong=dict(data[1],id='/strong',ssid='测试热点 5G',strength=100,frequency=2462)
        data.append(strong);w.apply_snapshot();QtTest.QTest.qWait(30)
        def order():return [w.network_list.itemAt(i).widget().network['id'] for i in range(w.network_list.count()) if hasattr(w.network_list.itemAt(i).widget(),'network')]
        self.assertEqual(order(),['/ap','/strong','/other']);self.assertIs(existing,w.network_buttons['/other'])
        data[1].update(strength=95,frequency=5180);strong['strength']=40;w.apply_snapshot()
        self.assertEqual(order(),['/ap','/other','/strong'])
        clicked=[];w.connect_network=lambda network:clicked.append(network['id']);existing.click();self.assertEqual(clicked,['/other'])
    def test_wifi_rows_show_actual_band_for_connected_and_unconnected_networks(self):
        w=self.window;w.show_page('wifi');w.poll.stop();QtTest.QTest.qWait(50)
        networks=w.data['wifi']['networks'];networks[0].update(ssid='测试热点 5G',frequency=2462,band='2.4 GHz')
        networks[1].update(ssid='测试热点 5G',frequency=5180,band='5 GHz',strength=100)
        w.apply_snapshot();QtTest.QTest.qWait(30)
        connected=w.network_buttons['/ap'];other=w.network_buttons['/other']
        self.assertIn('2.4 GHz',connected.text());self.assertIn('已连接',connected.text())
        self.assertIn('5 GHz',other.text());self.assertIn('100%',other.text())
        self.assertIn('2462 MHz',connected.toolTip());self.assertIn('5180 MHz',other.toolTip())
        w.pending_network=('/other',__import__('time').monotonic());w.apply_snapshot()
        self.assertIn('5 GHz',other.text());self.assertIn('连接中',other.text())
    def test_power_modes_are_inline_and_only_confirmed_selection_is_current(self):
        w=self.window;w.show_page('power');w.poll.stop();QtTest.QTest.qWait(50)
        self.assertFalse(w.findChildren(QtWidgets.QComboBox));self.assertEqual(len(w.profile_buttons),3)
        self.assertFalse(w.profile_buttons['performance'][0].isEnabled())
        self.assertTrue(w.profile_buttons['balanced'][0].property('current'))
        w.data['power']['profiles']=['performance','balanced','power-saver'];w.apply_snapshot();pending=[]
        w.job=lambda work,done:pending.append((work,done));w.profile_buttons['performance'][0].click()
        self.assertTrue(w.is_busy('power'));self.assertTrue(w.profile_buttons['balanced'][0].property('current'))
        current=copy.deepcopy(w.data['power']);current['activeProfile']='performance';pending[0][1]({'ok':True,'data':{'power':current}})
        self.assertTrue(w.profile_buttons['performance'][0].property('current'));self.assertFalse(w.profile_buttons['balanced'][0].property('current'))
        w.data['power']['profiles']=[];w.apply_snapshot();self.assertTrue(w.profile_note.isVisible())
        self.assertTrue(all(not b.isEnabled() for b,title in w.profile_buttons.values()))
    def test_slider_commits_only_on_release(self):
        Fixture.calls=[];slider=self.window.volume;slider.setSliderDown(True);slider.setValue(18);slider.setValue(54);QtTest.QTest.qWait(20);self.assertFalse(Fixture.calls)
        slider.setSliderDown(False);QtTest.QTest.qWait(70);self.assertEqual(Fixture.calls,[('volume',{'value':54})])
    def test_sound_is_one_full_card_with_inline_device_lists(self):
        from glass_widgets import GlassCard
        self.window.show_page('sound');QtTest.QTest.qWait(80)
        self.assertFalse(self.window.findChildren(QtWidgets.QComboBox))
        self.assertEqual(len(self.window.scroll.findChildren(GlassCard)),1)
        for kind in ('output','input'):
            rows=self.window.audio_device_lists[kind];self.assertEqual(len(rows['buttons']),2)
            self.assertTrue(any(b.property('current') for b in rows['buttons'].values()))
        self.assertEqual(self.window.mic_volume.value(),61)
    def test_volume_feedback_in_home_and_sound_waits_for_success(self):
        w=self.window;w.poll.stop()
        for page in ('home','sound'):
            w.show_page(page);QtTest.QTest.qWait(50);pending=[]
            w.job=lambda work,done:pending.append((work,done))
            with patch('control_center.SystemBackend',Fixture),patch.object(w.volume_feedback,'request') as preview:
                w.volume.setSliderDown(True);w.volume.setValue(36);preview.assert_not_called()
                w.volume.setSliderDown(False);self.assertEqual(len(pending),1);preview.assert_not_called()
                result=copy.deepcopy(SAMPLE);result['audio']['selected']['volume']=36
                pending[0][1]({'ok':True,'data':result})
                preview.assert_called_once_with(result['audio']['selected'])
                w.apply_snapshot();self.assertEqual(preview.call_count,1)
    def test_queued_volume_feedback_plays_only_final_success_and_never_hidden(self):
        w=self.window;w.poll.stop();pending=[];w.job=lambda work,done:pending.append((work,done))
        with patch('control_center.SystemBackend',Fixture),patch.object(w.volume_feedback,'request') as preview:
            w.action('volume',{'value':30});w.action('volume',{'value':60})
            pending[0][1]({'ok':True,'data':copy.deepcopy(SAMPLE)});preview.assert_not_called();self.assertEqual(len(pending),2)
            final=copy.deepcopy(SAMPLE);final['audio']['selected']['volume']=60
            pending[1][1]({'ok':True,'data':final});preview.assert_called_once_with(final['audio']['selected'])
            preview.reset_mock();w.action('volume',{'value':35});w.hide();pending[2][1]({'ok':True,'data':final});preview.assert_not_called()
    def test_failed_volume_and_microphone_changes_do_not_play_feedback(self):
        w=self.window;w.poll.stop();pending=[];w.job=lambda work,done:pending.append((work,done));w.refresh=lambda:None
        with patch('control_center.SystemBackend',Fixture),patch.object(w.volume_feedback,'request') as preview:
            w.action('volume',{'value':30});pending[0][1]({'ok':False,'error':'test failure'});preview.assert_not_called()
            w.action('input-volume',{'id':'mic','value':40});pending[1][1]({'ok':True,'data':copy.deepcopy(SAMPLE)});preview.assert_not_called()
    def test_microphone_slider_only_commits_on_release_and_keeps_target(self):
        self.window.show_page('sound');QtTest.QTest.qWait(50);Fixture.calls=[]
        self.window.mic_volume.setSliderDown(True);self.window.mic_volume.setValue(20);self.window.mic_volume.setValue(64)
        QtTest.QTest.qWait(20);self.assertEqual(Fixture.calls,[])
        self.window.mic_volume.setSliderDown(False);QtTest.QTest.qWait(80)
        self.assertEqual(Fixture.calls,[('input-volume',{'value':64,'id':'mic'})])
    def test_jack_change_updates_existing_rows_and_titles_without_audio_actions(self):
        self.window.show_page('sound');QtTest.QTest.qWait(80);self.window.poll.stop();Fixture.calls=[]
        for domain,key,kind,label in [('audio','outputs','output','有线耳机'),('microphone','inputs','input','外接麦克风')]:
            item=self.window.data[domain][key][0]
            item.update(displayName=label+'（回声消除）',routeLabel=label,detail='内置音频 · 同一设备的回声消除通道')
            self.window.data[domain]['selected']=item
        self.window.apply_snapshot();QtTest.QTest.qWait(50)
        row=self.window.audio_device_lists['output']['buttons']['speaker']
        self.assertEqual(row.name_label.text(),'有线耳机（回声消除）');self.assertTrue(row.detail_label.isVisible())
        self.assertTrue(row.property('current'));self.assertEqual(self.window.audio_title.text(),'有线耳机')
        self.assertEqual(self.window.mic_title.text(),'外接麦克风')
        self.assertGreaterEqual(row.height(),row.heightForWidth(row.width()))
        self.window.data['audio']['outputs'][0].update(displayName='扬声器（回声消除）',routeLabel='扬声器')
        self.window.apply_snapshot();QtTest.QTest.qWait(50)
        self.assertIs(row,self.window.audio_device_lists['output']['buttons']['speaker'])
        self.assertEqual(row.name_label.text(),'扬声器（回声消除）');self.assertEqual(self.window.audio_title.text(),'扬声器')
        self.assertEqual(Fixture.calls,[])
    def test_audio_event_refreshes_current_page_without_waiting_for_slow_snapshot(self):
        w=self.window;w.show_page('sound');QtTest.QTest.qWait(80);w.poll.stop()
        before=copy.deepcopy(w.data);revisions=dict(w.domain_revisions);w.reading=True;pending=[]
        w.job=lambda work,done:pending.append((work,done));Fixture.calls=[]
        row=w.audio_device_lists['output']['buttons']['speaker'];material=w.native_glass
        w.audio_changed();self.assertEqual(len(pending),2)
        audio=copy.deepcopy(SAMPLE['audio']);audio['outputs'][0].update(displayName='有线耳机',routeLabel='有线耳机');audio['selected']=audio['outputs'][0]
        pending[0][1]({'ok':True,'data':audio});pending[1][1]({'ok':True,'data':copy.deepcopy(SAMPLE['microphone'])})
        w.merge_snapshot(before,revisions);w.apply_snapshot();QtTest.QTest.qWait(30)
        self.assertTrue(w.reading);self.assertEqual(w.page,'sound');self.assertEqual(w.audio_title.text(),'有线耳机')
        self.assertIs(row,w.audio_device_lists['output']['buttons']['speaker']);self.assertEqual(row.name_label.text(),'有线耳机')
        self.assertIs(w.native_glass,material);self.assertEqual(Fixture.calls,[])
    def test_audio_events_during_a_read_queue_latest_state_and_reject_old_reply(self):
        w=self.window;w.show_page('sound');QtTest.QTest.qWait(80);w.poll.stop();pending=[]
        w.job=lambda work,done:pending.append((work,done));w.audio_changed();w.audio_changed()
        self.assertEqual(len(pending),2)
        old=copy.deepcopy(SAMPLE['audio']);old['selected']['volume']=5
        pending[0][1]({'ok':True,'data':old});pending[1][1]({'ok':True,'data':copy.deepcopy(SAMPLE['microphone'])})
        self.assertEqual(len(pending),4);self.assertNotEqual(w.volume.value(),5)
        latest=copy.deepcopy(SAMPLE['audio']);latest['selected']['volume']=73
        pending[2][1]({'ok':True,'data':latest});pending[3][1]({'ok':True,'data':copy.deepcopy(SAMPLE['microphone'])})
        self.assertEqual(w.volume.value(),73);self.assertFalse(w.audio_reading);self.assertFalse(w.microphone_reading)
    def test_audio_event_waits_for_user_action_then_reads_confirmed_state(self):
        w=self.window;w.show_page('sound');QtTest.QTest.qWait(80);w.poll.stop();pending=[]
        w.job=lambda work,done:pending.append((work,done))
        w.action('input-mute',{'id':'mic','value':True});w.audio_changed()
        self.assertEqual(len(pending),2);self.assertTrue(w.microphone_refresh_pending)
        pending[0][1]({'ok':True,'data':{'microphone':copy.deepcopy(SAMPLE['microphone'])}})
        self.assertEqual(len(pending),3)
        current=copy.deepcopy(SAMPLE['microphone']);current['selected']['muted']=True
        pending[2][1]({'ok':True,'data':current});pending[1][1]({'ok':True,'data':copy.deepcopy(SAMPLE['audio'])})
        self.assertTrue(w.data['microphone']['selected']['muted']);self.assertFalse(w.is_busy('microphone'))
    def test_hidden_panel_ignores_audio_events_and_stops_listener(self):
        w=self.window;w.audio_events.wanted=True;w.hide();calls=[];w.job=lambda work,done:calls.append(work)
        revisions=dict(w.domain_revisions);w.audio_changed()
        self.assertEqual(w.domain_revisions,revisions);self.assertEqual(calls,[])
        self.assertFalse(w.audio_events.wanted);self.assertFalse(w.audio_events.retry.isActive())
    def test_device_selection_is_confirmed_and_poll_preserves_rows(self):
        self.window.show_page('sound');QtTest.QTest.qWait(60);self.window.poll.stop()
        rows=self.window.audio_device_lists['input']['buttons'];button=rows['usb'];pending=[]
        self.window.job=lambda work,done:pending.append((work,done));button.click()
        self.assertTrue(rows['mic'].property('current'));self.assertTrue(self.window.is_busy('microphone'))
        result=copy.deepcopy(SAMPLE['microphone']);result['selected']=result['inputs'][1]
        pending[0][1]({'ok':True,'data':{'microphone':result}});QtTest.QTest.qWait(40)
        self.assertTrue(button.property('current'));self.assertFalse(rows['mic'].property('current'))
        self.window.apply_snapshot();self.assertIs(button,self.window.audio_device_lists['input']['buttons']['usb'])
    def test_old_microphone_read_cannot_undo_a_completed_mute(self):
        self.window.show_page('sound');QtTest.QTest.qWait(40);pending=[]
        self.window.job=lambda work,done:pending.append((work,done));self.window.refresh_microphone()
        self.window.action('input-mute',{'id':'mic','value':True})
        current=copy.deepcopy(SAMPLE['microphone']);current['selected']['muted']=True
        pending[1][1]({'ok':True,'data':{'microphone':current}})
        pending[0][1]({'ok':True,'data':copy.deepcopy(SAMPLE['microphone'])})
        self.assertTrue(self.window.data['microphone']['selected']['muted'])
    def test_disconnected_microphone_clears_selection_and_disables_controls(self):
        self.window.show_page('sound');QtTest.QTest.qWait(40)
        self.window.data['microphone']={'available':False,'inputs':[],'selected':None};self.window.apply_snapshot();QtTest.QTest.qWait(30)
        self.assertFalse(self.window.mic_volume.isEnabled());self.assertFalse(self.window.mic_mute.isEnabled())
        self.assertFalse(self.window.audio_device_lists['input']['buttons']);self.assertTrue(self.window.audio_device_lists['input']['empty'].isVisible())
    def test_long_audio_device_names_wrap_and_lists_reach_the_last_row(self):
        self.window.show_page('sound');self.window.poll.stop()
        for domain,key,kind in (('audio','outputs','output'),('microphone','inputs','input')):
            sample=self.window.data[domain][key][0]
            self.window.data[domain][key]=[dict(sample,id='device'+str(i),name=('会议室 USB 音频设备 '*10)+str(i)) for i in range(12)]
        self.window.apply_snapshot();QtTest.QTest.qWait(80)
        for view in self.window.audio_device_lists.values():
            bar=view['scroll'].verticalScrollBar();self.assertGreater(bar.maximum(),0)
            last=view['buttons']['device11'];self.assertGreaterEqual(last.name_label.height(),last.name_label.heightForWidth(last.name_label.width()))
            bar.setValue(bar.maximum());QtTest.QTest.qWait(30)
            self.assertTrue(view['scroll'].viewport().rect().contains(QtCore.QRect(last.mapTo(view['scroll'].viewport(),QtCore.QPoint()),last.size())))
    def test_microphone_mute_and_latest_queued_volume_are_separate_from_output(self):
        self.window.show_page('sound');QtTest.QTest.qWait(40);Fixture.calls=[];pending=[]
        self.window.job=lambda work,done:pending.append((work,done))
        self.window.mic_mute.click();self.assertTrue(self.window.is_busy('microphone'));self.assertFalse(self.window.is_busy('audio'))
        self.window.action('input-volume',{'id':'mic','value':20});self.window.action('input-volume',{'id':'mic','value':55})
        result=pending[0][0]();pending[0][1]({'ok':True,'data':result})
        self.assertEqual(Fixture.calls[0],('input-mute',{'id':'mic','value':True}));self.assertEqual(len(pending),2)
        pending[1][0]();self.assertEqual(Fixture.calls[-1],('input-volume',{'id':'mic','value':55}))
    def test_three_quick_actions_share_the_row_equally(self):
        for dnd in (False,True):
            self.window.data['notifications']['dnd']=dnd;self.window.apply_snapshot();QtTest.QTest.qWait(30)
            cards=[self.window.quick_cards[k] for k in ('display','power','dnd')]
            widths=[c.width() for c in cards]
            self.assertLessEqual(max(widths)-min(widths),1)
            self.assertEqual([c.height() for c in cards],[43]*3)
            self.assertEqual(len({c.y() for c in cards}),1)
            self.assertEqual(cards[1].x()-cards[0].geometry().right()-1,7)
            self.assertEqual(cards[2].x()-cards[1].geometry().right()-1,7)
    def test_first_frame_uses_fast_controls_and_ignores_slow_stale_snapshot(self):
        self.window.dialog_open=True
        import time
        class SlowServices(Fixture):
            def snapshot(self):time.sleep(.18);return super().snapshot()
            def controls_snapshot(self):
                result=copy.deepcopy(SAMPLE);result['audio']['selected']['volume']=71;result['brightness']['value']=89
                return {k:result[k] for k in ('audio','brightness')}
        self.window.hide();self.window.data={};self.window.backend_factory=SlowServices
        frames=[]
        self.window.popup_motion.valueChanged.connect(lambda progress:frames.append((progress,self.window.volume.value(),self.window.brightness.value(),self.window.volume.geometry().getRect(),self.window.brightness.geometry().getRect())))
        self.window.open_at('home',1500,1030,40,40);QtTest.QTest.qWait(900);self.window.poll.stop()
        moving=[f for f in frames if f[0]>0];self.assertGreater(len(moving),2)
        self.assertEqual({f[1:3] for f in moving},{(71,89)})
        self.assertEqual(len({f[3:] for f in moving}),1)
        self.assertEqual((self.window.volume.value(),self.window.brightness.value()),(71,89))
    def test_ready_glass_waits_for_current_controls_before_becoming_visible(self):
        self.window.dialog_open=True
        self.window.hide();saved=[];job=self.window.job
        def defer(work,done,pool=None):
            if work.__name__=='read':saved.append(done)
            else:job(work,done,pool=pool)
        self.window.job=defer;self.window.open_at('home',1500,1030,40,40);QtTest.QTest.qWait(300)
        self.assertTrue(self.window.opening_pending);self.assertEqual(self.window.windowOpacity(),0)
        self.assertFalse(self.window.state()['motionRunning']);self.assertEqual(len(saved),1)
        result=copy.deepcopy(SAMPLE);result['audio']['selected']['volume']=64;result['brightness']['value']=93
        saved.pop()({'ok':True,'data':result});QtTest.QTest.qWait(400)
        self.assertFalse(self.window.opening_pending);self.assertEqual((self.window.volume.value(),self.window.brightness.value()),(64,93))
    def test_resident_is_idle_without_polling_or_exit_timer_when_hidden(self):
        self.window.resident=True;self.window.hide()
        self.assertFalse(self.window.poll.isActive());self.assertFalse(self.window.idle.isActive());self.assertFalse(self.window.warmup_timer.isActive())
        self.assertIsNone(self.window.native_glass)
        with patch.object(QtWidgets.QApplication,'quit') as quit:
            self.window.quit_if_idle();quit.assert_not_called()
    def test_hover_preload_is_invisible_then_click_reuses_the_material(self):
        self.window.hide();self.window.open_at('home',1500,1030,40,40,preload=True)
        QtTest.QTest.qWait(500)
        self.assertTrue(self.window.preload_ready);self.assertTrue(self.window.preloading)
        self.assertEqual(self.window.windowOpacity(),0);self.assertEqual(self.window.popup_progress,0)
        self.assertFalse(self.window.poll.isActive());material=self.window.native_glass
        self.window.open_at('home',1500,1030,40,40);QtTest.QTest.qWait(400)
        self.assertFalse(self.window.preloading);self.assertFalse(self.window.preload_expiry.isActive())
        self.assertTrue(self.window.isVisible());self.assertEqual(self.window.popup_progress,1)
        self.assertIs(self.window.native_glass,material)
    def test_expired_preload_releases_the_scene_without_opening(self):
        self.window.hide();self.window.open_at('home',1500,1030,40,40,preload=True)
        self.window.preload_expiry.start(20);QtTest.QTest.qWait(250)
        self.assertFalse(self.window.isVisible());self.assertFalse(self.window.preloading)
        self.assertIsNone(self.window.native_glass);self.assertFalse(self.window.poll.isActive())
    def test_switching_tray_entries_keeps_the_shared_popup_open(self):
        self.window.dialog_open=True;self.window.hide()
        anchor=(1500,1030,175,48)
        for entry in ('notifications','wifi','sound','power','bluetooth','notifications'):
            self.window.open_at(entry,*anchor);QtTest.QTest.qWait(400)
            self.assertTrue(self.window.isVisible(),entry)
            self.assertFalse(self.window.closing,entry)
            self.assertEqual(self.window.popup_progress,1.,entry)
            self.assertEqual(self.window.page,'home')
        self.window.open_at('notifications',*anchor);QtTest.QTest.qWait(300)
        self.assertFalse(self.window.isVisible(),'the same tray entry still toggles closed')
    def test_switching_entry_during_preparation_does_not_cancel_opening(self):
        self.window.dialog_open=True;self.window.hide();pending=[];job=self.window.job
        def defer(work,done,pool=None):
            if pool is self.window.material_pool:pending.append((work,done))
            else:job(work,done,pool=pool)
        self.window.job=defer;anchor=(1500,1030,175,48)
        self.window.open_at('notifications',*anchor);QtTest.QTest.qWait(50)
        self.assertTrue(self.window.opening_pending);self.assertEqual(len(pending),1)
        self.window.open_at('wifi',*anchor)
        self.assertTrue(self.window.isVisible());self.assertTrue(self.window.opening_pending)
        work,done=pending.pop();done({'ok':True,'data':work()});QtTest.QTest.qWait(500)
        self.assertTrue(self.window.isVisible());self.assertEqual(self.window.popup_progress,1.)
    def test_preload_click_preserves_the_tray_entry_for_later_switches(self):
        self.window.dialog_open=True;self.window.hide();anchor=(1500,1030,175,48)
        self.window.open_at('wifi',*anchor,preload=True);QtTest.QTest.qWait(400)
        self.window.open_at('wifi',*anchor);QtTest.QTest.qWait(400)
        self.assertTrue(self.window.isVisible());material=self.window.native_glass
        self.window.open_at('sound',*anchor);QtTest.QTest.qWait(300)
        self.assertTrue(self.window.isVisible());self.assertIs(self.window.native_glass,material)
        self.window.open_at('sound',*anchor);QtTest.QTest.qWait(300)
        self.assertFalse(self.window.isVisible())
    def test_slider_readout_does_not_change_geometry_for_three_digits(self):
        self.window.poll.stop();rects=None
        for volume,brightness in ((9,9),(33,80),(100,100)):
            self.window.data['audio']['selected']['volume']=volume;self.window.data['brightness']['value']=brightness;self.window.apply_snapshot();QtTest.QTest.qWait(20)
            current=(self.window.volume.geometry(),self.window.brightness.geometry(),self.window.volume_label.size(),self.window.brightness_label.size())
            if rects is not None:self.assertEqual(current,rects)
            rects=current
    def test_stale_read_cannot_overwrite_completed_action(self):
        jobs=[];self.window.job=lambda fn,cb:jobs.append((fn,cb));self.window.refresh();old=jobs.pop();self.window.action('mute',{'value':True});write=jobs.pop()
        changed=copy.deepcopy(SAMPLE);changed['audio']['selected']['muted']=True
        write[1]({'ok':True,'data':changed});old[1]({'ok':True,'data':copy.deepcopy(SAMPLE)})
        self.assertTrue(self.window.data['audio']['selected']['muted'])
    def test_all_home_controls_fit_and_use_same_popup(self):
        self.assertEqual(self.window.page,'home')
        self.assertEqual(self.window.scroll.verticalScrollBar().maximum(),0)
        for page in ('wifi','sound','power','notifications','bluetooth'):
            self.window.hide();self.window.open_at(page,1500,1030,25,33);QtTest.QTest.qWait(30);self.assertEqual(self.window.page,'home')
        self.assertTrue(self.window.media_buttons['PlayPause'].isEnabled());self.assertTrue(self.window.brightness.isEnabled());self.assertTrue(self.window.dnd.isEnabled())
    def test_long_notification_pages_do_not_clip_footer(self):
        self.window.data['notifications']['entries']=[{'id':str(i),'app':'测试应用','title':'较长标题 '*12,'body':'较长正文 '*60} for i in range(8)]
        self.window.apply_snapshot();QtTest.QTest.qWait(30);self.assertEqual(self.window.scroll.verticalScrollBar().maximum(),0)
        self.window.notification_next.click();self.assertEqual(self.window.notification_page,1);self.assertEqual(len(self.window.data['notifications']['entries']),8)
    def test_wifi_refresh_keeps_geometry_and_existing_row_identity(self):
        self.window.poll.stop();self.window.show_page('wifi');QtTest.QTest.qWait(30)
        rect=self.window.geometry();button=self.window.network_buttons['/ap']
        for count in (1,8,2,24,1,24):
            self.window.data['wifi']['networks']=[dict(SAMPLE['wifi']['networks'][0],id='/ap' if i==0 else '/ap'+str(i),strength=50+i,connected=i==0) for i in range(count)]
            self.window.apply_snapshot();QtTest.QTest.qWait(20)
            self.assertEqual(rect,self.window.geometry());self.assertIs(button,self.window.network_buttons['/ap'])
        self.assertGreater(self.window.network_scroll.verticalScrollBar().maximum(),0)
    def test_long_device_lists_scroll_inside_the_captured_glass(self):
        from glass_widgets import GlassCard
        self.window.poll.stop();capture=QtCore.QRect(self.window.geometry())
        for page in ('wifi','bluetooth'):
            self.window.data[page]['enabled']=True
            key='networks' if page=='wifi' else 'devices'
            sample=SAMPLE[page][key][0]
            self.window.data[page][key]=[dict(sample,id='/item'+str(i)) for i in range(40)]
            self.window.show_page(page);QtTest.QTest.qWait(80)
            scroll=self.window.network_scroll if page=='wifi' else self.window.bluetooth_scroll
            bar=scroll.verticalScrollBar();self.assertTrue(bar.isVisible());self.assertGreater(bar.maximum(),0)
            self.assertEqual(self.window.scroll.verticalScrollBar().maximum(),0)
            for position in (0,bar.maximum()//2,bar.maximum()):
                bar.setValue(position);QtTest.QTest.qWait(20)
                for card in self.window.findChildren(GlassCard):
                    if card.isVisible():self.assertTrue(capture.contains(QtCore.QRect(card.mapToGlobal(QtCore.QPoint()),card.size())))
                if page=='wifi':
                    advanced=self.window.findChild(QtWidgets.QPushButton,'advanced_network_settings')
                    self.assertTrue(self.window.scroll.viewport().rect().contains(QtCore.QRect(advanced.mapTo(self.window.scroll.viewport(),QtCore.QPoint()),advanced.size())))
                self.window.apply_snapshot();QtTest.QTest.qWait(20);self.assertEqual(bar.value(),position)
            last=scroll.widget().findChildren(QtWidgets.QPushButton)[-1]
            self.assertTrue(scroll.viewport().rect().contains(QtCore.QRect(last.mapTo(scroll.viewport(),QtCore.QPoint()),last.size())))
    def test_notification_reader_reaches_end_and_keeps_scroll_on_poll(self):
        text='长通知内容。'*350+'最后一行完整可读'
        self.window.data['notifications']['entries']=[{'app':'应用','title':'标题','body':text}]
        self.window.apply_snapshot();QtTest.QTest.qWait(30)
        reader=self.window.notification_reader;bar=reader.verticalScrollBar()
        self.assertIn('最后一行完整可读',reader.toPlainText());self.assertGreater(bar.maximum(),0)
        bar.setValue(bar.maximum());self.window.apply_snapshot();self.assertEqual(bar.value(),bar.maximum())
    def test_tooltip_is_independent_of_dark_button_style(self):
        button=next(b for b in self.window.findChildren(QtWidgets.QAbstractButton) if b.toolTip()=='显示设置')
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
            else:button=next(b for b in self.window.findChildren(QtWidgets.QAbstractButton) if b.toolTip()=={'sound':'输出设备与声音设置','power':'电池、电源与会话'}[page])
            self.assertIsNotNone(button);QtTest.QTest.mouseClick(button,QtCore.Qt.LeftButton);QtTest.QTest.qWait(400)
            self.assertEqual(self.window.page,page);self.assertTrue(self.window.isVisible());self.assertFalse(self.window.closing)
            self.assertTrue(self.window.poll.isActive());self.assertFalse(self.window.idle.isActive())
            QtTest.QTest.mouseClick(self.window.back,QtCore.Qt.LeftButton);QtTest.QTest.qWait(240)
            self.assertEqual(self.window.page,'home');self.assertTrue(self.window.isVisible())
        self.assertEqual(events.events,[]);self.assertEqual(int(self.window.winId()),native_id)
        self.assertEqual(self.window.material_revision,revision);self.assertIs(self.window.native_glass,material)
        self.assertEqual(self.window.backdrop.cacheKey(),backdrop_key)
    def test_page_transitions_hold_position_and_do_not_render_glass_per_frame(self):
        self.window.dialog_open=True;rect=self.window.geometry();native=self.window.native_glass
        for page in ('wifi','home','bluetooth','home','sound','home','power','home'):
            self.window.navigate(page);self.assertTrue(self.window.navigation_active)
            self.assertFalse(self.window.navigation_overlay.before.isNull());self.assertFalse(self.window.navigation_overlay.after.isNull())
            count=native.render_count if native else 0
            while self.window.navigation_active:
                self.assertEqual(self.window.geometry(),rect)
                if native:self.assertEqual(native.render_count,count)
                QtTest.QTest.qWait(15)
            self.assertTrue(self.window.navigation_overlay.before.isNull());self.assertTrue(self.window.scroll.isVisible())
            self.assertEqual(self.window.geometry(),rect)
    def test_navigation_retarget_and_dismiss_release_transition_frames(self):
        self.window.dialog_open=True;rect=self.window.geometry()
        self.window.navigate('wifi');QtTest.QTest.qWait(35);self.window.navigate('home');QtTest.QTest.qWait(35);self.window.navigate('sound')
        QtTest.QTest.qWait(250);self.assertEqual(self.window.page,'sound');self.assertEqual(self.window.geometry(),rect)
        self.window.navigate('home');QtTest.QTest.qWait(30);self.window.dismiss();QtTest.QTest.qWait(300)
        self.assertFalse(self.window.isVisible());self.assertFalse(self.window.navigation_active)
        self.assertTrue(self.window.navigation_overlay.before.isNull());self.assertTrue(self.window.navigation_overlay.after.isNull())
    def test_reduced_motion_switches_pages_without_transition(self):
        setting=QtCore.QSettings('org.ukui','style');old=setting.value('reduceAnimation');setting.setValue('reduceAnimation',True)
        try:
            rect=self.window.geometry();self.window.navigate('wifi')
            self.assertFalse(self.window.navigation_active);self.assertEqual(self.window.geometry(),rect)
            self.assertTrue(self.window.scroll.isVisible())
        finally:
            if old is None:setting.remove('reduceAnimation')
            else:setting.setValue('reduceAnimation',old)
    def test_returning_home_during_scan_keeps_footer_in_place(self):
        self.window.dialog_open=True;self.window.poll.stop();height=self.window.notification_reader.height();pending=[]
        self.window.job=lambda work,done:pending.append((work,done))
        self.window.navigate('wifi');QtTest.QTest.qWait(230);self.assertTrue(self.window.is_busy('wifi'))
        self.window.navigate('home');QtTest.QTest.qWait(230)
        self.assertFalse(self.window.status.isVisible());self.assertEqual(self.window.notification_reader.height(),height)
        pending[1][1]({'ok':True,'data':copy.deepcopy(SAMPLE)});QtTest.QTest.qWait(30)
        self.assertFalse(self.window.status.isVisible());self.assertEqual(self.window.notification_reader.height(),height)
    def test_empty_detail_canvas_click_dismisses_the_popup(self):
        self.window.dialog_open=True;self.window.navigate('power');QtTest.QTest.qWait(240)
        viewport=self.window.scroll.viewport()
        QtTest.QTest.mouseClick(viewport,QtCore.Qt.LeftButton,pos=QtCore.QPoint(50,viewport.height()-60));QtTest.QTest.qWait(300)
        self.assertFalse(self.window.isVisible())
    def test_detail_click_while_reading_applies_result_to_current_page(self):
        self.window.maybe_scan_wifi=lambda:None # this test isolates the snapshot/navigation race
        self.window.refresh_microphone=lambda:None
        self.window.refresh_audio=lambda:None
        pending=[];self.window.job=lambda fn,cb:pending.append((fn,cb))
        self.window.refresh();self.assertTrue(self.window.reading)
        for page in ('wifi','bluetooth','sound'):
            if page!='wifi':self.window.back.click()
            button=self.window.findChild(QtWidgets.QAbstractButton,page+'_details') if page in ('wifi','bluetooth') else next(b for b in self.window.findChildren(QtWidgets.QAbstractButton) if b.toolTip()=='输出设备与声音设置')
            QtTest.QTest.mouseClick(button,QtCore.Qt.LeftButton);QtTest.QTest.qWait(30)
        result=copy.deepcopy(SAMPLE);result['audio']['selected']['volume']=64
        pending[0][1]({'ok':True,'data':result});QtTest.QTest.qWait(400)
        self.assertEqual(self.window.page,'sound');self.assertTrue(self.window.isVisible());self.assertEqual(self.window.volume.value(),64)
        self.assertFalse(self.window.reading);self.assertEqual(len(pending),1)
    def test_dock_motion_reverses_and_stops_without_rebuilding_material(self):
        self.window.dialog_open=True # exclude platform focus changes from the motion-only check
        self.window.hide();self.window.open_at('home',1500,1030,40,40);target=QtCore.QRect(self.window.popup_target)
        deadline=QtCore.QElapsedTimer();deadline.start()
        while self.window.opening_pending and deadline.elapsed()<3000:QtTest.QTest.qWait(10)
        QtTest.QTest.qWait(40);self.assertGreater(self.window.y(),target.y());self.assertFalse(self.window.mask().isEmpty())
        self.window.dismiss();QtTest.QTest.qWait(30);self.window.open_at('home',1500,1030,40,40)
        QtTest.QTest.qWait(350);self.assertEqual(self.window.geometry(),target);self.assertTrue(self.window.mask().isEmpty());self.assertFalse(self.window.state()['motionRunning'])
        self.window.dismiss();deadline=QtCore.QElapsedTimer();deadline.start()
        while self.window.isVisible() and deadline.elapsed()<1200:QtTest.QTest.qWait(20)
        self.assertFalse(self.window.isVisible());self.assertIsNone(self.window.native_glass);self.assertFalse(self.window.poll.isActive());self.assertFalse(self.window.state()['motionRunning'])
    def test_reduced_motion_displays_prepared_surface_without_animation(self):
        self.window.dialog_open=True # isolate motion from Xvfb's focus handoff
        setting=QtCore.QSettings('org.ukui','style');old=setting.value('reduceAnimation');setting.setValue('reduceAnimation',True)
        try:
            self.window.hide();self.window.open_at('home',1500,1030,40,40)
            deadline=QtCore.QElapsedTimer();deadline.start()
            while self.window.opening_pending and deadline.elapsed()<3000:
                self.assertFalse(self.window.state()['motionRunning']);QtTest.QTest.qWait(10)
            self.assertEqual(self.window.popup_progress,1.);self.assertFalse(self.window.state()['motionRunning']);self.window.dismiss();self.assertFalse(self.window.isVisible())
        finally:
            if old is None:setting.remove('reduceAnimation')
            else:setting.setValue('reduceAnimation',old)
    def test_reopening_same_page_keeps_controls_and_discards_hidden_material(self):
        self.window.refresh_controls=lambda:setattr(self.window,"controls_ready",True)
        self.window.refresh_microphone=lambda:None
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
        self.window.refresh_controls=lambda:setattr(self.window,"controls_ready",True)
        self.window.refresh_microphone=lambda:None
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

    def test_wifi_arrow_scans_on_each_entry_but_not_on_poll(self):
        Fixture.calls=[]
        for count in (1,2):
            QtTest.QTest.mouseClick(self.window.findChild(QtWidgets.QAbstractButton,'wifi_details'),QtCore.Qt.LeftButton)
            QtTest.QTest.qWait(100)
            self.assertEqual(Fixture.calls,[('scan',{})]*count)
            self.window.refresh();QtTest.QTest.qWait(100)
            self.assertEqual(Fixture.calls,[('scan',{})]*count)
            self.window.back.click();QtTest.QTest.qWait(30)
    def test_wifi_entry_waits_for_initial_state_and_does_not_scan_after_leaving(self):
        pending=[];self.window.data.pop('wifi');self.window.job=lambda work,done:pending.append((work,done))
        self.window.navigate('wifi');self.assertTrue(self.window.auto_scan_pending);self.assertEqual(len(pending),1)
        pending[0][1]({'ok':True,'data':copy.deepcopy(SAMPLE)})
        self.assertFalse(self.window.auto_scan_pending);self.assertEqual(len(pending),2);self.assertTrue(self.window.is_busy('wifi'))
        pending[1][1]({'ok':True,'data':copy.deepcopy(SAMPLE)})
        self.window.navigate('home');pending[-1][1]({'ok':True,'data':copy.deepcopy(SAMPLE)})
        pending.clear();self.window.data.pop('wifi')
        self.window.navigate('wifi');self.window.navigate('home')
        pending[0][1]({'ok':True,'data':copy.deepcopy(SAMPLE)})
        self.assertEqual(len(pending),1);self.assertFalse(self.window.auto_scan_pending)
    def test_wifi_entry_keeps_disabled_radio_off(self):
        Fixture.calls=[];self.window.data['wifi']['enabled']=False
        self.window.refresh=lambda:None;self.window.navigate('wifi');QtTest.QTest.qWait(30)
        self.assertEqual(Fixture.calls,[]);self.assertFalse(self.window.auto_scan_pending)
    def test_cancel_preparing_opening_never_flashes_or_reopens(self):
        self.window.refresh_controls=lambda:setattr(self.window,"controls_ready",True)
        self.window.refresh_microphone=lambda:None
        self.window.hide();pending=[];self.window.refresh=lambda:None
        self.window.job=lambda work,done,**kwargs:pending.append((work,done))
        self.window.open_at('home',1500,1030,40,40)
        self.assertTrue(self.window.opening_pending);self.assertEqual(self.window.windowOpacity(),0)
        self.assertFalse(self.window.state()['motionRunning']);self.window.dismiss()
        class Material:
            closed=False
            def close(self):self.closed=True
        material=Material();pending[0][1]({'ok':True,'data':material});QtTest.QTest.qWait(20)
        self.assertTrue(material.closed);self.assertFalse(self.window.isVisible());self.assertFalse(self.window.warmup_timer.isActive())

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


class AudioNotifications(unittest.TestCase):
    def test_preview_coalesces_and_stays_silent_for_mute_zero_and_cancel(self):
        played=[]
        class SilentPreview(VolumeFeedback):
            def launch(self):played.append(self.target);self.target=''
        feedback=SilentPreview()
        try:
            feedback.request({'id':'a','volume':20});feedback.request({'id':'b','volume':60})
            QtTest.QTest.qWait(110);self.assertEqual(played,['b'])
            for selected in (None,{'id':'a','volume':0},{'id':'a','volume':20,'muted':True}):
                feedback.request(selected);QtTest.QTest.qWait(100)
            self.assertEqual(played,['b'])
            feedback.request({'id':'a','volume':20});feedback.stop();QtTest.QTest.qWait(110);self.assertEqual(played,['b'])
        finally:feedback.stop();feedback.deleteLater()
    def test_fragmented_device_bursts_coalesce_and_stream_noise_is_ignored(self):
        events=AudioEvents();events.wanted=True;seen=[];events.changed.connect(lambda:seen.append(True))
        try:
            events.feed(b"Event 'change' on sink-input #3\nEvent 'new' on client #1\n")
            QtTest.QTest.qWait(120);self.assertEqual(seen,[])
            events.feed(b"Event 'change' on si");events.feed(b"nk #0\nEvent 'change' on card #0\n")
            events.feed(b"Event 'remove' on source #2\nEvent 'change' on server #4294967295\n")
            QtTest.QTest.qWait(120);self.assertEqual(len(seen),1)
            events.feed(b"Event 'new' on sink #4\n");events.stop();QtTest.QTest.qWait(120)
            self.assertEqual(len(seen),1);self.assertFalse(events.retry.isActive())
        finally:events.stop();events.deleteLater()
    def test_failed_subscription_retries_only_while_visible(self):
        events=AudioEvents();events.wanted=True
        try:
            events.failed(QtCore.QProcess.FailedToStart);self.assertTrue(events.retry.isActive())
            events.stop();events.exited();self.assertFalse(events.retry.isActive())
        finally:events.stop();events.deleteLater()

class Adapter(unittest.TestCase):
    def test_wifi_band_is_derived_from_frequency(self):
        for frequency,band in [(2412,'2.4 GHz'),(2484,'2.4 GHz'),(5180,'5 GHz'),(5825,'5 GHz'),(5955,'6 GHz'),(0,'频段未知')]:
            self.assertEqual(SystemBackend.wifi_band(frequency),band)
    def test_wifi_deduplicates_within_band_and_respects_saved_band_lock(self):
        b=SystemBackend();aps={'/active24':(2412,40),'/strong24':(2462,99),'/strong5':(5180,90),'/weak5':(5745,50)}
        def props(service,path,interface):
            if path==NMP:return {'WirelessEnabled':True,'WirelessHardwareEnabled':True}
            if interface.endswith('.Device'):return {'DeviceType':2,'State':100,'Interface':'wlan0','AvailableConnections':['/saved24']}
            if interface.endswith('.Wireless'):return {'ActiveAccessPoint':'/active24','AccessPoints':list(aps)}
            frequency,strength=aps[path]
            return {'Ssid':b'Same 5G Name','Frequency':frequency,'Strength':strength,'Flags':1,'RsnFlags':256}
        b.props=props
        b.call=lambda service,path,interface,method,*args: (['/device'],) if method=='GetDevices' else ({'802-11-wireless':{'ssid':b'Same 5G Name','band':'bg'},'802-11-wireless-security':{'key-mgmt':'wpa-psk'}},)
        networks=b.wifi()['networks'];self.assertEqual([n['id'] for n in networks],['/active24','/strong5'])
        self.assertEqual([n['band'] for n in networks],['2.4 GHz','5 GHz'])
        self.assertEqual(networks[0]['profile'],'/saved24');self.assertEqual(networks[1]['profile'],'')
        self.assertEqual(networks[0]['frequency'],2412);self.assertTrue(networks[0]['connected'])
    def test_ukui_power_modes_use_supply_specific_native_settings(self):
        class Settings:
            values={'power-policy-ac':0,'power-policy-battery':2};writable=True
            def get_int(self,key):return self.values[key]
            def is_writable(self,key):return self.writable
            def set_int(self,key,value):self.values[key]=value;return True
        settings=Settings();b=SystemBackend();b.ukui_power_settings=lambda:settings;b.session_call=lambda *args:(True,)
        self.assertEqual(b.ukui_power_profiles(False)['activeProfile'],'performance')
        self.assertEqual(b.ukui_power_profiles(True)['activeProfile'],'power-saver')
        b.power=lambda:b.ukui_power_profiles(True)
        with patch('system_backend.Gio.Settings.sync'):
            result=b.action('power-profile',{'value':'balanced'})
        self.assertEqual(result['power']['activeProfile'],'balanced');self.assertEqual(settings.values['power-policy-ac'],0)
        self.assertEqual(settings.values['power-policy-battery'],1)
        with self.assertRaises(ValueError):b.action('power-profile',{'value':'turbo'})
        settings.writable=False
        with self.assertRaises(ValueError):b.action('power-profile',{'value':'performance'})
        b.session_call=lambda *args:(False,);self.assertEqual(b.ukui_power_profiles(True),{})
    def test_standard_power_profiles_take_precedence_over_ukui(self):
        b=SystemBackend();b.call=lambda *args:('/display',)
        def props(service,path,interface):
            if service=='org.freedesktop.UPower':return {'IsPresent':True,'OnBattery':False,'Percentage':90}
            return {'Profiles':[{'Profile':'balanced'},{'Profile':'power-saver'}],'ActiveProfile':'balanced'}
        b.props=props
        with patch.object(b,'ukui_power_profiles',side_effect=AssertionError('must keep existing standard provider')):
            data=b.power()
        self.assertEqual(data['profileBackend'],'power-profiles-daemon');self.assertEqual(data['activeProfile'],'balanced')
    def test_headphone_port_and_echo_route_follow_same_device_on_unplug(self):
        text='''Sink #1
 Name: processed
 Description: 内置音频 立体声 (echo cancelled with 内置音频 立体声)
 Driver: module-echo-cancel.c
 Mute: no
 Volume: mono: 100 / 57% / 0 dB
 Properties:
  device.master_device = "hardware"
  device.class = "filter"
Sink #0
 Name: hardware
 Description: 内置音频 立体声
 Mute: no
 Volume: mono: 100 / 57% / 0 dB
 Ports:
  analog-output-speaker: 扬声器 (priority: 10000, not available)
  analog-output-headphones: 模拟耳机 (priority: 9900, available)
 Active Port: analog-output-headphones
'''
        b=SystemBackend();calls=[]
        b.command=lambda args:calls.append(args) or ('Default Sink: processed\n' if args[-1]=='info' else text)
        plugged=b.audio();current=plugged['selected']
        self.assertEqual(current['displayName'],'有线耳机（回声消除）')
        self.assertEqual(current['physicalId'],'hardware');self.assertEqual(current['volume'],57)
        self.assertIn('同一设备',current['detail']);self.assertEqual(plugged['outputs'][1]['displayName'],'有线耳机')
        text=text.replace('Active Port: analog-output-headphones','Active Port: analog-output-speaker')
        unplugged=b.audio();self.assertEqual(unplugged['selected']['id'],current['id'])
        self.assertEqual(unplugged['selected']['displayName'],'扬声器（回声消除）')
        self.assertTrue(all(args[1] in ('info','list') for args in calls))
    def test_output_filter_reports_hardware_mute_independent_of_order(self):
        blocks=['Sink #1\n Name: processed\n Mute: no\n Properties:\n  device.master_device = "remap"\n',
                'Sink #2\n Name: remap\n Mute: no\n Properties:\n  device.master_device = "hardware"\n',
                'Sink #0\n Name: hardware\n Mute: yes\n Active Port: analog-output-speaker\n']
        b=SystemBackend();calls=[]
        for ordered in (blocks,list(reversed(blocks))):
            b.command=lambda args:calls.append(args) or ('Default Sink: processed\n' if args[-1]=='info' else ''.join(ordered))
            selected=b.audio()['selected']
            self.assertTrue(selected['muted']);self.assertFalse(selected['ownMuted'])
            self.assertEqual(selected['muteTargets'],['processed','remap','hardware'])
        self.assertTrue(all(args[1] in ('info','list') for args in calls))
    def test_explicit_unmute_releases_master_chain_and_mute_only_selected(self):
        b=SystemBackend();calls=[]
        selected={'id':'processed','muted':True,'muteTargets':['processed','remap','hardware']}
        b.audio=lambda:{'selected':selected};b.command=lambda args:calls.append(args)
        with patch.object(b,'snapshot',side_effect=AssertionError('must not wait for slow services')):
            b.action('mute',{'value':False})
        self.assertEqual(calls,[['/usr/bin/pactl','set-sink-mute',sink,'0'] for sink in ('hardware','remap','processed')])
        calls.clear();b.action('mute',{'value':True})
        self.assertEqual(calls,[['/usr/bin/pactl','set-sink-mute','processed','1']])
    def test_nonzero_volume_recovers_muted_master_but_zero_preserves_mute(self):
        b=SystemBackend();calls=[]
        selected={'id':'processed','muted':True,'muteTargets':['processed','hardware']}
        b.audio=lambda:{'selected':selected};b.command=lambda args:calls.append(args)
        b.action('volume',{'value':46})
        self.assertEqual(calls,[['/usr/bin/pactl','set-sink-volume','processed','46%'],
                                ['/usr/bin/pactl','set-sink-mute','hardware','0'],
                                ['/usr/bin/pactl','set-sink-mute','processed','0']])
        calls.clear();b.action('volume',{'value':0})
        self.assertEqual(calls,[['/usr/bin/pactl','set-sink-volume','processed','0%']])
    def test_unmute_does_not_guess_dangling_or_cyclic_master(self):
        b=SystemBackend();writes=[]
        for master in ('missing','processed'):
            text='Sink #0\n Name: processed\n Mute: yes\n Properties:\n  device.master_device = "'+master+'"\n'
            def command(args):
                if args[1] not in ('info','list'):writes.append(args)
                return 'Default Sink: processed\n' if args[-1]=='info' else text
            b.command=command
            with self.assertRaises(ValueError):b.action('mute',{'value':False})
        self.assertEqual(writes,[])
    def test_microphone_filter_chain_inherits_real_input_port_not_its_name(self):
        text='''Source #4
 Name: noiseReduceSource
 Description: Generic processed input
 Driver: module-echo-cancel.c
 Monitor of Sink: n/a
 Properties:
  device.master_device = "remap"
  device.class = "filter"
Source #3
 Name: remap
 Description: Remapped input
 Monitor of Sink: n/a
 Properties:
  device.master_device = "mic"
Source #2
 Name: mic
 Description: 内置音频 立体声
 Monitor of Sink: n/a
 Volume: mono: 100 / 100% / 0 dB
 Ports:
  analog-input-internal-mic: 内部话筒 (priority: 8900, not available)
  analog-input-mic: 话筒 (priority: 8700, available)
 Active Port: analog-input-mic
Source #8
 Name: speaker.monitor
 Monitor of Sink: speaker
'''
        b=SystemBackend();b.command=lambda args:'Default Source: noiseReduceSource\n' if args[-1]=='info' else text
        data=b.microphone();self.assertEqual(len(data['inputs']),3)
        self.assertEqual(data['selected']['displayName'],'外接麦克风（回声消除）')
        self.assertEqual(data['selected']['physicalId'],'mic');self.assertEqual(data['selected']['index'],'4')
        text=text.replace('Active Port: analog-input-mic','Active Port: analog-input-internal-mic')
        self.assertEqual(b.microphone()['selected']['displayName'],'内置麦克风（回声消除）')
    def test_unknown_and_cyclic_audio_routes_do_not_claim_a_headphone(self):
        b=SystemBackend()
        for master in ('missing','a'):
            text='Sink #0\n Name: a\n Description: Custom audio\n Properties:\n  device.master_device = "'+master+'"\n  device.class = "filter"\n'
            b.command=lambda args:'Default Sink: a\n' if args[-1]=='info' else text
            item=b.audio()['selected'];self.assertNotIn('physicalId',item)
            self.assertIn('暂不可确认',item['detail']);self.assertEqual(item['displayName'],'Custom audio')
        self.assertEqual(b.pulse_field(' Active Port: \n Formats: pcm','Active Port'),'')
    def test_microphone_parser_excludes_monitors_and_keeps_echo_cancel_source(self):
        backend=SystemBackend()
        text='Source #1\n Name: mic\n Description: Built-in mic\n Monitor of Sink: n/a\n Mute: no\n Volume: left: 100 / 60% / 0 dB, right: 100 / 80% / 0 dB\nSource #2\n Name: echo\n Description: Echo cancelled mic\n Monitor of Sink: n/a\n Mute: yes\n Volume: mono: 100 / 55% / 0 dB\nSource #3\n Name: speaker.monitor\n Monitor of Sink: sink\nSource #4\n Name: monitorWithoutSuffix\n Monitor of Sink: sink\n'
        backend.command=lambda argv:'Default Source: echo\n' if argv[-1]=='info' else text
        data=backend.microphone();self.assertEqual([i['id'] for i in data['inputs']],['mic','echo']);self.assertEqual(data['inputs'][0]['volume'],70)
        self.assertEqual(data['selected']['id'],'echo');self.assertTrue(data['selected']['muted'])
    def test_microphone_actions_validate_target_and_values(self):
        backend=SystemBackend();backend.microphone=lambda:copy.deepcopy(SAMPLE['microphone']);calls=[];backend.command=lambda argv:calls.append(argv) or ''
        for operation,values in [('input',{'id':'missing'}),('input-volume',{'id':'mic','value':True}),('input-volume',{'id':'mic','value':101}),('input-mute',{'id':'mic','value':'yes'})]:
            with self.assertRaises(ValueError):backend.action(operation,values)
        self.assertFalse(calls)
        backend.action('input-volume',{'id':'mic','value':65});backend.action('input-mute',{'id':'mic','value':True})
        self.assertEqual(calls,[['/usr/bin/pactl','set-source-volume','mic','65%'],['/usr/bin/pactl','set-source-mute','mic','1']])
    def test_input_switch_moves_application_mics_but_not_monitors_or_filters(self):
        backend=SystemBackend();backend.microphone=lambda:copy.deepcopy(SAMPLE['microphone']);calls=[]
        streams='Source Output #0\n Client: n/a\n Source: 1\nSource Output #3\n Client: 123\n Source: 1\nSource Output #4\n Client: 123\n Source: 0\nSource Output #5\n Client: 123\n Source: 2\n media.role = "filter"\n'
        def command(argv):calls.append(argv);return streams if argv[-1]=='source-outputs' else ''
        backend.command=command;backend.action('input',{'id':'usb'})
        self.assertEqual(calls,[['/usr/bin/pactl','set-default-source','usb'],['/usr/bin/pactl','list','source-outputs'],['/usr/bin/pactl','move-source-output','3','usb']])
    def test_output_switch_preserves_echo_cancel_internal_streams(self):
        backend=SystemBackend();backend.audio=lambda:copy.deepcopy(SAMPLE['audio']);backend.snapshot=lambda:{};calls=[]
        streams='Sink Input #0\n Client: n/a\nSink Input #1\n Client: 42\nSink Input #2\n Client: 11\n media.role = "filter"\n'
        def command(argv):calls.append(argv);return streams if argv[-1]=='sink-inputs' else ''
        backend.command=command;backend.action('output',{'id':'headset'})
        self.assertEqual(calls,[['/usr/bin/pactl','set-default-sink','headset'],['/usr/bin/pactl','list','sink-inputs'],['/usr/bin/pactl','move-sink-input','1','headset']])
    def test_controls_snapshot_does_not_wait_for_other_services(self):
        backend=SystemBackend();backend.audio=lambda:{'available':True,'selected':{'volume':37}};backend.brightness=lambda:{'available':True,'value':82}
        with patch.object(backend,'wifi') as wifi,patch.object(backend,'notifications') as notifications,patch.object(backend,'session') as session:
            result=backend.controls_snapshot();self.assertEqual(set(result),{'audio','brightness'})
            wifi.assert_not_called();notifications.assert_not_called();session.assert_not_called()
    def test_scan_publishes_only_after_scan_generation_advances(self):
        backend=SystemBackend();reads=[];calls=[];polls=[1000,2000]
        def wifi():
            reads.append(1);return {'enabled':True,'devices':['/device'],'networks':['old' if len(reads)==1 else 'fresh']}
        backend.wifi=wifi;backend.props=lambda *args:{'LastScan':1000}
        def call(*args):
            calls.append(args)
            return ({'LastScan':polls.pop(0)},) if args[3]=='GetAll' else ()
        backend.call=call
        with patch('system_backend.time.clock_gettime',return_value=100),patch('system_backend.time.sleep') as sleep:
            result=backend.action('scan',{})
        self.assertEqual(result['wifi']['networks'],['fresh']);self.assertEqual(len(reads),2)
        self.assertEqual([args[3] for args in calls],['RequestScan','GetAll','GetAll']);sleep.assert_called_once_with(.15)
    def test_recent_scan_reuses_results_without_requesting_again(self):
        backend=SystemBackend();backend.wifi=lambda:{'enabled':True,'devices':['/device'],'networks':['fresh']};backend.props=lambda *args:{'LastScan':95000}
        with patch.object(backend,'call') as call,patch('system_backend.time.clock_gettime',return_value=100):
            self.assertEqual(backend.action('scan',{})['wifi']['networks'],['fresh']);call.assert_not_called()
    def test_scan_timeout_is_bounded_and_never_toggles_radio(self):
        backend=SystemBackend();backend.wifi=lambda:{'enabled':True,'devices':['/device']};backend.props=lambda *args:{'LastScan':-1};calls=[];backend.call=lambda *args:calls.append(args)
        with patch('system_backend.time.monotonic',side_effect=[0,13]):
            with self.assertRaises(TimeoutError):backend.action('scan',{})
        self.assertEqual([args[3] for args in calls],['RequestScan'])
    def test_pulseaudio_legacy_format(self):
        b=SystemBackend();b.command=lambda argv:'Default Sink: test\n' if argv[-1]=='info' else 'Sink #0\n Name: test\n Description: Speakers\n Mute: yes\n Volume: front-left: 32768 / 50% / -18.06 dB, front-right: 32768 / 50% / -18.06 dB\n'
        self.assertEqual(b.audio()['selected'],{'id':'test','name':'Speakers','volume':50,'muted':True,'ownMuted':True,'muteTargets':['test']})
    def test_command_input_validation(self):
        b=SystemBackend();calls=[];b.command=lambda argv:calls.append(argv) or '';b.snapshot=lambda:{};b.audio=lambda:copy.deepcopy(SAMPLE['audio'])
        for value in (-1,101,True,'60; malicious'):
            with self.assertRaises(ValueError):b.action('volume',{'value':value})
        self.assertEqual(calls,[]);b.action('volume',{'value':57});self.assertEqual(calls,[['/usr/bin/pactl','set-sink-volume','speaker','57%']])
    def test_volume_confirmation_does_not_wait_for_other_services(self):
        b=SystemBackend();calls=[];b.command=lambda args:calls.append(args) or '';b.audio=lambda:copy.deepcopy(SAMPLE['audio'])
        with patch.object(b,'snapshot',side_effect=AssertionError('slow services must not be queried')):
            result=b.action('volume',{'value':42})
        self.assertEqual(set(result),{'audio'});self.assertEqual(result['audio']['selected']['volume'],42)
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
