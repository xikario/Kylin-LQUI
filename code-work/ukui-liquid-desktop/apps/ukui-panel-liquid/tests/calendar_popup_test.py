"""Run under Xvfb + a private D-Bus session and disposable XDG directories."""
import datetime as dt
import json
import os
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import calendar_data as data
import calendar_popup as popup
from PyQt5 import QtTest


class CalendarTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.home = Path(cls.temp.name)
        os.environ['XDG_DATA_HOME'] = str(cls.home/'data')
        os.environ['XDG_CONFIG_HOME'] = str(cls.home/'config')
        popup.W.QApplication.setAttribute(popup.C.Qt.AA_EnableHighDpiScaling)
        cls.app = popup.W.QApplication.instance() or popup.W.QApplication([])
        cls.app.setStyle('Fusion'); cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.window = popup.Calendar(str(self.home/'missing-native.db'))
        self.window.settings.clear()

    def tearDown(self):
        for process in list(self.window.jobs):
            process.kill(); process.waitForFinished(2000)
        self.window.hide(); self.window.deleteLater(); self.app.processEvents()

    def test_personal_tasks_persist_and_reject_stale_edits(self):
        path = self.home/'isolated/tasks.sqlite'
        data.mutate_task(path,{'action':'add','title':'整理项目资料','date':'2026-11-12'})
        original=data.tasks(path)[0]
        data.mutate_task(path,dict(original,action='complete',completed=True))
        with self.assertRaisesRegex(ValueError,'已变化'):
            data.mutate_task(path,dict(original,action='edit',title='过期编辑'))
        current=data.tasks(path)[0]; self.assertEqual(current['title'],'整理项目资料'); self.assertEqual(current['completed'],1)
        data.mutate_task(path,dict(current,action='delete'))
        with self.assertRaisesRegex(ValueError,'已变化'):
            data.mutate_task(path,dict(current,action='edit',title='已删除'))

    def test_native_version_conflict_is_not_overwritten(self):
        path=self.home/'native.db'
        with sqlite3.connect(path) as conn:
            conn.execute('CREATE TABLE Schedule(id,descript)'); conn.execute("INSERT INTO Schedule VALUES('1','原内容')")
        version=data.read_snapshot.__globals__['row_version']({'id':'1','descript':'原内容'})
        with sqlite3.connect(path) as conn: conn.execute("UPDATE Schedule SET descript='外部更新'")
        with self.assertRaisesRegex(ValueError,'已变化'):
            data.mutate_schedule(path,{'action':'edit','id':'1','version':version,'title':'旧窗口内容'})
        with sqlite3.connect(path) as conn: self.assertEqual(conn.execute('SELECT descript FROM Schedule').fetchone()[0],'外部更新')

    def test_lunar_holiday_and_makeup_days_without_native_database(self):
        result=data.snapshot(str(self.home/'absent'),dt.date(2026,2,1),dt.date(2026,2,28))
        self.assertEqual(result['dates']['2026-02-17']['lunar'],'正月初一')
        self.assertEqual(result['dates']['2026-02-17']['festival'],'春节')
        self.assertTrue(result['dates']['2026-02-17']['off'])
        self.assertTrue(any(x.get('off') is False for x in result['dates'].values()))
        self.assertFalse((self.home/'absent').exists())

    def test_popup_placement_is_bounded_on_all_edges_and_negative_screen(self):
        for screen in (popup.C.QRect(0,0,1920,1080),popup.C.QRect(-1280,0,1280,720),popup.C.QRect(0,0,380,500)):
            for point in (screen.topLeft(),screen.topRight(),screen.bottomLeft(),screen.bottomRight()):
                rect=popup.popup_rect(popup.C.QRect(point,popup.C.QSize(32,32)),screen,popup.C.QSize(440,690))
                self.assertTrue(screen.contains(rect), (screen,rect))

    def test_external_1080p_at_150_percent_keeps_all_actions_visible(self):
        area=popup.C.QRect(1920,0,1280,672);window=self.window
        window.adapt_to_area(area);window.setGeometry(popup.popup_rect(popup.C.QRect(3100,672,70,36),area,popup.C.QSize(440,740)))
        window.show();window.info.setText('丙午年正月初一 · 春节 · 休假');self.app.processEvents()
        self.assertTrue(area.contains(window.geometry()))
        bottom=window.sync_button.mapTo(window,popup.C.QPoint(0,window.sync_button.height())).y()
        self.assertLessEqual(bottom,window.height()-8)
        self.assertFalse(window.findChildren(popup.W.QScrollArea))

    def test_work_approval_stays_pending_and_survives_importer_deletion(self):
        item={'id':'77','sourceId':'77','workTaskId':'5','date':'2026-11-12','endDate':'2026-11-12','title':'工作验收','time':'10:00'}
        self.window.data={'items':[item]}; self.window.statuses={'5':'pending_review'}
        self.window.save_work_cache(); self.window.data={'items':[]}
        records=self.window.records(); self.assertEqual(len(records),1); self.assertFalse(records[0]['completed'])
        self.assertEqual(records[0]['state'],'待审核')
        self.window.statuses={'5':'done'}; self.assertTrue(self.window.records()[0]['completed'])

    def test_details_readable_with_white_system_palette(self):
        # Reproduce the light native surface over the dark calendar using real pixels.
        W, C, G = popup.W, popup.C, popup.G
        light = self.window.palette()
        light.setColor(G.QPalette.Window, G.QColor('white'))
        light.setColor(G.QPalette.Base, G.QColor('white'))
        self.window.setPalette(light)
        text = ('项目例会通知。时间：11月25日 10:00。\n'
                '议题：核对本周计划，讨论交付安排。<会议资料>\n') * 35
        record = W.QListWidgetItem('项目例会')
        record.setData(C.Qt.UserRole, {'title':text, 'date':'2026-11-25', 'time':'待办', 'state':'个人待办'})
        self.window.list.addItem(record)
        self.window.list.setCurrentItem(record)
        observed = {}
        def count_tones(widget):
            image = widget.grab().toImage()
            dark = bright = 0
            for y in range(2, image.height()-2, 2):
                for x in range(2, image.width()-2, 2):
                    c = image.pixelColor(x, y)
                    if c.alpha() < 220: continue
                    dark += max(c.red(),c.green(),c.blue()) < 96
                    bright += min(c.red(),c.green(),c.blue()) > 140
            return dark, bright
        def inspect():
            dialog = self.app.activeModalWidget()
            try:
                body = dialog.findChild(W.QPlainTextEdit, 'agendaBody')
                close = dialog.findChild(W.QPushButton)
                observed['text'] = body.toPlainText()
                observed['meta'] = dialog.findChild(W.QLabel, 'agendaMeta').text()
                observed['body_tones'] = count_tones(body.viewport())
                observed['button_tones'] = count_tones(close)
                observed['scroll'] = body.verticalScrollBar().maximum()
                observed['button'] = (close.text(), close.isVisibleTo(dialog))
            except Exception as e:
                observed['error'] = repr(e)
            finally:
                if dialog: dialog.reject()
        C.QTimer.singleShot(100, inspect)
        self.window.details()
        self.assertNotIn('error', observed)
        self.assertEqual(observed['text'], text)
        self.assertIn('2026-11-25', observed['meta'])
        self.assertIn('个人待办', observed['meta'])
        self.assertGreater(observed['scroll'], 0)
        self.assertEqual(observed['button'], ('关闭',True))
        for dark,bright in (observed['body_tones'], observed['button_tones']):
            self.assertGreater(dark, 10)
            self.assertGreater(bright, 10)
        short = '项目例会通知\n\n时间：11 月 25 日 10:00\n地点：二楼会议室\n\n议题：确认本周交付计划，梳理待协调事项，并确定每项工作的负责人。'
        item = record.data(C.Qt.UserRole)
        item['title'] = short
        record.setData(C.Qt.UserRole, item)
        C.QTimer.singleShot(80, inspect)
        self.window.details()
        self.assertEqual(observed['text'], short)
        self.assertEqual(observed['scroll'], 0)

    def test_delete_confirmation_defaults_to_cancel(self):
        W, C = popup.W, popup.C
        default = []
        def cancel():
            dialog = self.app.activeModalWidget()
            default.extend(b.text() for b in dialog.findChildren(W.QPushButton) if b.isDefault())
            QtTest.QTest.keyClick(dialog, C.Qt.Key_Escape)
        C.QTimer.singleShot(80,cancel)
        self.assertFalse(self.window.read_dialog('删除事项','删除所选事项？',confirm=True))
        self.assertEqual(default,['取消'])

    def test_toggle_fades_window_opacity_in_place(self):
        window=self.window
        duration=popup.popup_style.fade_duration(True)
        window.toggle(popup.C.QRect(1100,850,70,38))
        if duration>0:
            self.assertLess(window.windowOpacity(),1.0)
            for _ in range(50):
                QtTest.QTest.qWait(20)
                if window.popup_fade.state()!=popup.C.QAbstractAnimation.Running: break
        self.assertAlmostEqual(window.windowOpacity(),1.0,places=2)
        window.hide(); self.assertEqual(window.windowOpacity(),1.0)

    def test_render_and_hidden_refresh_lifecycle(self):
        window=self.window
        window.month.setSelectedDate(popup.C.QDate(2026,2,17))
        window.toggle(popup.C.QRect(1100,850,70,38))
        for _ in range(100):
            QtTest.QTest.qWait(30)
            if window.loaded and not window.reading: break
        self.assertTrue(window.loaded)
        self.assertIn('正月初一',window.info.text())
        self.assertIn('春节',window.info.text())
        material_builds=window.material_builds
        for _ in range(5):window.render();window.repaint();self.app.processEvents()
        self.assertEqual(window.material_builds,material_builds)
        self.assertFalse(window.surface.isNull())
        self.assertEqual(window.surface.pixelColor(0,0).alpha(),0)
        self.assertFalse(window.findChildren(popup.W.QScrollArea))
        self.assertLessEqual(window.sync_button.mapTo(window,popup.C.QPoint(0,window.sync_button.height())).y(),window.height()-12)
        for _ in range(12):
            window.hide(); self.assertFalse(window.poll.isActive())
            window.toggle(popup.C.QRect(1100,850,70,38)); self.assertTrue(window.poll.isActive())
        window.hide(); generation=window.generation
        window.refresh(); self.assertEqual(window.generation,generation)

    def test_plain_note_not_json_and_worker_failure_reported(self):
        script=self.home/'worker.py'
        script.write_text('import sys,json\nprint(json.dumps({"note":sys.stdin.read()}))\n')
        received=[]
        self.window.run(script,[],'验收说明',received.append)
        for _ in range(100):
            QtTest.QTest.qWait(15)
            if received: break
        self.assertEqual(received[0]['note'],'验收说明')
        received.clear(); self.window.run(self.home/'not-found.py',[],None,received.append)
        for _ in range(100):
            QtTest.QTest.qWait(15)
            if received: break
        self.assertIn('error',received[0])

    def test_actual_dialog_add_and_complete_persist(self):
        window=self.window; window.toggle(popup.C.QRect(1100,850,70,38))
        for _ in range(100):
            QtTest.QTest.qWait(15)
            if window.loaded: break
        def fill():
            dialog=self.app.activeModalWidget()
            dialog.findChild(popup.W.QPlainTextEdit).setPlainText('弹窗新建回归待办')
            dialog.accept()
        popup.C.QTimer.singleShot(25,fill); window.add_task()
        for _ in range(100):
            QtTest.QTest.qWait(20)
            if any(x['title']=='弹窗新建回归待办' for x in window.records()): break
        task=next(x for x in window.records() if x['title']=='弹窗新建回归待办')
        for i in range(window.list.count()):
            row=window.list.item(i)
            if (row.data(popup.C.Qt.UserRole) or {}).get('id')==task['id']:window.list.setCurrentItem(row)
        window.complete()
        for _ in range(100):
            QtTest.QTest.qWait(20)
            if not window.writing and not window.reading:break
        self.assertTrue(next(x for x in data.tasks(data.task_path()) if x['id']==task['id'])['completed'])
        self.assertFalse(Path(window.database).exists())

    def test_render_worker_timeout_keeps_calendar_responsive(self):
        script=self.home/'stalled-material'
        script.write_text('#!/usr/bin/python3\nimport sys,time\nsys.stdin.buffer.read()\ntime.sleep(20)\n');script.chmod(0o700)
        previous=os.environ.get('LQUI_PANEL_MATERIAL_WORKER');os.environ['LQUI_PANEL_MATERIAL_WORKER']=str(script)
        try:
            self.window.toggle(popup.C.QRect(1100,850,70,38));self.assertIsNotNone(self.window.material_worker)
            ticks=[];timer=popup.C.QTimer();timer.timeout.connect(lambda:ticks.append(1));timer.start(10)
            for _ in range(180):
                QtTest.QTest.qWait(20)
                if self.window.material_worker is None:break
            self.assertIsNone(self.window.material_worker);self.assertGreater(len(ticks),100)
            self.assertFalse(self.window.surface.isNull());self.assertFalse(self.window.material_ready)
        finally:
            if previous is None:os.environ.pop('LQUI_PANEL_MATERIAL_WORKER',None)
            else:os.environ['LQUI_PANEL_MATERIAL_WORKER']=previous

    def test_reopen_starts_from_last_glass_and_crossfades(self):
        runtime=self.home/'runtime';runtime.mkdir(mode=0o700,exist_ok=True)
        previous_runtime=os.environ.get('XDG_RUNTIME_DIR');os.environ['XDG_RUNTIME_DIR']=str(runtime)
        script=self.home/'stalled-material-cache'
        script.write_text('#!/usr/bin/python3\nimport sys,time\nsys.stdin.buffer.read()\ntime.sleep(20)\n');script.chmod(0o700)
        previous=os.environ.get('LQUI_PANEL_MATERIAL_WORKER');os.environ['LQUI_PANEL_MATERIAL_WORKER']=str(script)
        try:
            window=self.window;window.hide();anchor=popup.C.QRect(1100,850,70,38)
            window.toggle(anchor);QtTest.QTest.qWait(50);window.hide()
            dpr=window.devicePixelRatioF();expected=popup.C.QSize(round(window.width()*dpr),round(window.height()*dpr))
            cached=popup.G.QImage(expected,popup.G.QImage.Format_ARGB32_Premultiplied);cached.fill(popup.G.QColor(200,30,30,220))
            screen=popup.W.QApplication.primaryScreen();geo=window.geometry()
            backdrop=window.backdrop_signature(screen.grabWindow(0,geo.x(),geo.y(),geo.width(),geo.height()))
            path=window.surface_cache(expected,dpr);path.parent.mkdir(parents=True,exist_ok=True)
            cached.setText('backdrop',backdrop);self.assertTrue(cached.save(str(path),'PNG'))
            window.surface=popup.G.QImage()                       # as in a freshly started calendar process
            window.toggle(anchor);QtTest.QTest.qWait(50)
            centre=window.surface.pixelColor(expected.width()//2,expected.height()//2)
            self.assertGreater(centre.red(),150,'same backdrop: reopening shows the saved glass')
            window.hide()
            other='ff'*64 if backdrop[:2]!='ff' else '00'*64       # saved over a very different backdrop
            cached.setText('backdrop',other);self.assertTrue(cached.save(str(path),'PNG'))
            window.toggle(anchor);QtTest.QTest.qWait(50)
            centre=window.surface.pixelColor(expected.width()//2,expected.height()//2)
            self.assertLess(centre.red(),150,'different backdrop: the stale glass is not shown')
            self.assertGreater(centre.alpha(),200,'a stand-in made from the current backdrop is shown instead')
            fresh=popup.G.QImage(expected,popup.G.QImage.Format_ARGB32_Premultiplied);fresh.fill(popup.G.QColor(30,30,200,220))
            window.show()                                  # no WM under Xvfb: keep it visible for the fade
            window.show_surface(fresh)
            self.assertFalse(window.fade_from.isNull(),'a new glass crossfades over the visible one')
            QtTest.QTest.qWait(400)
            self.assertTrue(window.fade_from.isNull());self.assertTrue(window.material_ready)
        finally:
            self.window.hide()
            for key,value in (('LQUI_PANEL_MATERIAL_WORKER',previous),('XDG_RUNTIME_DIR',previous_runtime)):
                if value is None:os.environ.pop(key,None)
                else:os.environ[key]=value

    def test_real_single_instance_close_reopen_and_crash_recovery(self):
        bus=popup.D.QDBusConnection.sessionBus()
        processes=[]
        def launch():
            child=subprocess.Popen(['/usr/bin/python3',str(ROOT/'scripts/calendar_popup.py'),'--anchor','800,700,70,35'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            processes.append(child);return child
        def state():
            call=popup.D.QDBusMessage.createMethodCall(popup.BUS,'/Calendar',popup.BUS,'State')
            result=bus.call(call,popup.D.QDBus.Block,300)
            return json.loads(result.arguments()[0]) if result.type()!=popup.D.QDBusMessage.ErrorMessage else {}
        def until(predicate):
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                if predicate():return True
                QtTest.QTest.qWait(25)
            return False
        try:
            first=launch();self.assertTrue(until(lambda:state().get('loaded')));self.assertTrue(until(lambda:state().get('materialReady')))
            self.assertEqual(state()['pid'],first.pid)
            second=launch();self.assertTrue(until(lambda:second.poll() is not None));self.assertEqual(second.returncode,0)
            self.assertFalse(state()['visible']);self.assertFalse(state()['refreshActive'])
            third=launch();self.assertTrue(until(lambda:third.poll() is not None));self.assertTrue(state()['visible'])
            self.assertEqual(state()['pid'],first.pid)
            first.kill();first.wait(timeout=3)
            self.assertTrue(until(lambda:not state()))
            replacement=launch();self.assertTrue(until(lambda:state().get('loaded')))
            self.assertEqual(state()['pid'],replacement.pid)
            self.assertIsNotNone(self.app)  # The caller remains alive after the calendar dies.
        finally:
            for child in processes:
                if child.poll() is None:child.terminate()
                child.wait(timeout=3)


if __name__=='__main__': unittest.main()
