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
