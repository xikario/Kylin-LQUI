import importlib.util
import sys
from pathlib import Path
import sqlite3
import tempfile
import datetime as dt
import unittest
sys.path.insert(0,str(Path(__file__).parents[1]/'scripts'))
spec=importlib.util.spec_from_file_location('reader',Path(__file__).parents[1]/'scripts/calendar_snapshot.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class CalendarTests(unittest.TestCase):
    def test_native_dates_spans_and_repeats_are_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            db=Path(tmp)/'calendar.db'
            with sqlite3.connect(db) as c:
                c.execute('CREATE TABLE Schedule(id, start_date, end_date, start_hour, start_minute, descript, repeat, isAllDay, isLunar, beginrepeat, endrepeat)')
                c.executemany('INSERT INTO Schedule VALUES(?,?,?,?,?,?,?,?,?,?,?)',[
                    ('a','2026-09-28','2026-09-28','17','30','待办','不重复',0,0,'',''),
                    ('b','2026-09-27','2026-09-29','0','0','跨天','不重复',1,0,'',''),
                    ('c','2026-09-01','2026-09-01','9','0','每日','每天',0,0,'无限重复',''),
                    ('d','2026-09-01','2026-09-01','9','0','未知','自定义',0,0,'',''),
                ])
            original=db.read_bytes();r=m.read_snapshot(db,dt.date(2026,9,28),dt.date(2026,9,30))
            self.assertEqual(len(r['items']),5);self.assertEqual(db.read_bytes(),original)
            self.assertTrue(r['warning']);self.assertEqual(next(i for i in r['items'] if i['title']=='待办')['time'],'17:30')
            self.assertEqual(next(i for i in r['items'] if i['title']=='跨天')['time'],'全天')
    def test_missing_database_is_not_created(self):
        with tempfile.TemporaryDirectory() as tmp:
            db=Path(tmp)/'absent.db';r=m.read_snapshot(db,dt.date.today(),dt.date.today())
            self.assertEqual(r['items'],[]);self.assertFalse(db.exists())
            with self.assertRaises(sqlite3.OperationalError):
                m.mutate_schedule(db, {'action':'delete','id':'missing','version':'stale'})
            self.assertFalse(db.exists())

    def test_edit_and_delete_use_the_exact_versioned_native_row(self):
        with tempfile.TemporaryDirectory() as tmp:
            db=Path(tmp)/'calendar.db'
            row=dict(id='chosen', start_date='2026-09-08', end_date='2026-09-08',
                     start_day='8',start_month='9',end_day='0',end_month='0',
                     start_hour='9',start_minute='0',end_hour='10',end_minute='0',
                     beginDateTime='2026-09-08T00:00:00',endDateTime='2026-09-08T00:00:00',
                     descript='原始内容',repeat='不重复',isAllDay=0,isLunar=0,remind='日程发生时')
            columns=','.join(row)
            with sqlite3.connect(db) as conn:
                conn.execute('CREATE TABLE Schedule ('+columns+')')
                stmt='INSERT INTO Schedule VALUES ('+','.join('?' for _ in row)+')'
                conn.execute(stmt,tuple(row.values()))
                conn.execute(stmt,tuple(dict(row,id='neighbor').values()))
            today=dt.date(2026,11,10)
            def item():
                return next(i for i in m.read_snapshot(db,today,today,today)['items'] if i['sourceId']=='chosen')
            before=item()
            request=dict(action='edit',id='chosen',version=before['version'],title='完整事项\n保留换行',
                         date='2026-11-11',start='14:30',end='13:00')
            with self.assertRaisesRegex(ValueError,'结束时间'):
                m.mutate_schedule(db,request)
            self.assertEqual(item()['version'],before['version'])
            m.mutate_schedule(db,dict(request,end='15:00'))
            after=item()
            self.assertEqual((after['date'],after['time'],after['title']),('2026-11-11','14:30','完整事项\n保留换行'))
            self.assertEqual(after['completionKey'],before['completionKey'])
            with sqlite3.connect(db) as conn:
                self.assertEqual(conn.execute("SELECT beginDateTime,endDateTime,start_day,start_month,end_day,remind FROM Schedule WHERE id='chosen'").fetchone(),
                                 ('2026-11-11T00:00:00','2026-11-11T00:00:00','11','11','0','日程发生时'))
            delete=dict(action='delete',id='chosen',version=before['version'])
            with self.assertRaisesRegex(ValueError,'已变化'):
                m.mutate_schedule(db,delete)
            m.mutate_schedule(db,dict(delete,version=after['version']))
            with sqlite3.connect(db) as conn:
                self.assertEqual(conn.execute('SELECT * FROM Schedule').fetchall(),[tuple(dict(row,id='neighbor').values())])

    def test_today_recurring_occurrences_survive_distant_month_navigation(self):
        with tempfile.TemporaryDirectory() as tmp:
            db=Path(tmp)/'calendar.db'
            with sqlite3.connect(db) as conn:
                conn.execute('CREATE TABLE Schedule(id,start_date,end_date,start_hour,start_minute,descript,repeat,isAllDay,isLunar,beginrepeat)')
                conn.execute("INSERT INTO Schedule VALUES('r','2026-11-01','2026-11-01','9','30','每日','每天',0,0,'无限重复')")
            today=dt.date(2026,11,10)
            distant=dt.date(2028,5,1)
            items=m.read_snapshot(db,distant,distant,today)['items']
            dates={item['date']:item for item in items}
            self.assertEqual(dates[str(today)]['time'],'09:30')
            self.assertIn(str(distant),dates)
            self.assertNotEqual(dates[str(today)]['completionKey'],dates[str(distant)]['completionKey'])
            self.assertFalse(dates[str(today)]['editableTime'])
class ChineseCalendarTests(unittest.TestCase):
    def test_lunar_festivals_leap_month_and_year_boundary(self):
        from chinese_calendar import LunarCalendar
        with LunarCalendar() as calendar:
            for date in ('2027-02-06','2030-02-03'):
                value=calendar.date(dt.date.fromisoformat(date));self.assertEqual((value['month'],value['day'],value['festival']),(1,1,'春节'));self.assertEqual(value['calendarSource'],'hko')
            self.assertEqual(calendar.date(dt.date(2027,2,5))['festival'],'除夕')
            self.assertEqual(calendar.date(dt.date(2020,10,1))['festival'],'中秋 · 国庆节')
            self.assertEqual(calendar.date(dt.date(2030,4,5))['solarTerm'],'清明')
            for solar,lunar,festival in [
                ('2026-02-17','正月初一','春节'),('2026-02-16','腊月廿九','除夕'),
                ('2026-06-19','五月初五','端午'),('2026-09-25','八月十五','中秋'),
                ('2025-07-25','闰六月初一',''),('2026-04-05','二月十八','清明'),
                ('2026-10-01','八月廿一','国庆节')]:
                value=calendar.date(dt.date.fromisoformat(solar))
                self.assertEqual(value['lunar'],lunar,solar)
                self.assertEqual(value['festival'],festival,solar)
            self.assertTrue(calendar.date(dt.date(2025,7,25))['leap'])
            self.assertEqual(calendar.date(dt.date(2026,2,17))['yearName'],'丙午年')

    def test_official_2026_schedule_and_unknown_year(self):
        from chinese_calendar import metadata
        days=metadata(dt.date(2026,1,1),dt.date(2026,12,31))
        # Independently transcribed from State Council notice 2025 No. 7.
        expected_off=set()
        for month,first,last in [(1,1,3),(2,15,23),(4,4,6),(5,1,5),(6,19,21),(9,25,27),(10,1,7)]:
            expected_off.update(str(dt.date(2026,month,day)) for day in range(first,last+1))
        expected_work={'2026-01-04','2026-02-14','2026-02-28','2026-05-09','2026-09-20','2026-10-10'}
        self.assertEqual({day for day,v in days.items() if v.get('off') is True},expected_off)
        self.assertEqual({day for day,v in days.items() if v.get('off') is False},expected_work)
        self.assertNotIn('off',days['2026-09-28'])
        future=metadata(dt.date(2030,1,1),dt.date(2030,1,1))['2030-01-01']
        self.assertFalse(future['scheduleKnown']);self.assertNotIn('off',future)

if __name__=='__main__':unittest.main()
