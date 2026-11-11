#!/usr/bin/python3
"""Read the native calendar, or apply one explicitly requested agenda change."""
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
from chinese_calendar import metadata


def day(value):
    return dt.date.fromisoformat(str(value)[:10])


NON_REPEATING = ('', '不重复', '不重複', 'No repeat', 'Never')
WORK_TODO_PREFIX = re.compile(r'^【工作待办】#([0-9]+)(?=\s|$)')
WORK_TODO_STATES = {'published', 'accepted', 'in_progress', 'pending_review',
                    'done', 'rejected', 'cancelled'}
TIME_COLUMNS = {'start_day', 'start_month', 'end_day', 'end_month', 'start_hour',
                'start_minute', 'end_hour', 'end_minute', 'beginDateTime', 'endDateTime'}


def row_version(row):
    return hashlib.sha256(json.dumps(row, sort_keys=True).encode()).hexdigest()


def time_fields(row):
    """Only reschedule single-day Gregorian events with the known OEM schema."""
    if (not TIME_COLUMNS <= row.keys() or row.get('isLunar')
            or row.get('repeat', '') not in NON_REPEATING):
        return {}
    try:
        first = day(row['start_date'])
        if first != day(row['end_date']):
            return {}
        for field in ('beginDateTime', 'endDateTime'):
            if dt.datetime.fromisoformat(row[field]) != dt.datetime.combine(first, dt.time()):
                return {}
        start = dt.time(int(row['start_hour']), int(row['start_minute']))
        end = dt.time(int(row['end_hour']), int(row['end_minute']))
        return {'date': str(first), 'start': start.strftime('%H:%M'), 'end': end.strftime('%H:%M')}
    except (KeyError, ValueError, TypeError, OverflowError):
        return {}


def read_snapshot(path, begin, end, agenda_today=None):
    path=Path(path)
    if not path.exists():
        return {'items': [], 'warning': '系统日历尚无数据'}
    items=[];warnings=set()
    ranges = [(begin, end)]
    if agenda_today:
        recent = (agenda_today - dt.timedelta(days=30), agenda_today + dt.timedelta(days=90))
        if recent[0] < begin or recent[1] > end:
            ranges.append(recent)
    with sqlite3.connect(path.resolve().as_uri()+'?mode=ro', uri=True, timeout=.3) as conn:
        conn.execute('PRAGMA query_only=ON');conn.row_factory=sqlite3.Row
        for row in conn.execute('SELECT * FROM Schedule LIMIT 10000'):
            r=dict(row)
            try:
                first=day(r['start_date']);last=max(first,day(r['end_date']))
                rule=str(r.get('repeat','不重复'))
                series=rule not in NON_REPEATING
                repeating=series
                if repeating and (r.get('isLunar') or rule not in ('每天','每日','每周','每星期','每月','每年','工作日','每个工作日')):
                    warnings.add('部分重复事项请在系统日历查看')
                    repeating=False
                until=dt.date.max
                if r.get('beginrepeat') not in ('无限重复',''):
                    try:until=day(r.get('endrepeat',str(end)))
                    except ValueError:pass
                duration=(last-first).days
                starts=set()
                if not repeating:
                    if agenda_today or (last>=begin and first<=end):starts.add(first)
                else:
                    for lower, upper in ranges:
                        cursor=max(first,lower-dt.timedelta(days=min(duration,366)))
                        limit=min(until,upper)
                        while cursor<=limit:
                            yes=(rule in ('每天','每日') or
                                 rule in ('每周','每星期') and cursor.weekday()==first.weekday() or
                                 rule=='每月' and cursor.day==first.day or
                                 rule=='每年' and (cursor.month,cursor.day)==(first.month,first.day) or
                                 rule in ('工作日','每个工作日') and cursor.weekday()<5)
                            if yes:starts.add(cursor)
                            cursor+=dt.timedelta(days=1)
                source_id=str(r['id'])
                title=str(r['descript'])
                # The work-todo importer deletes/recreates its calendar rows
                # on sync. The task number in its reserved prefix survives.
                work_task=WORK_TODO_PREFIX.match(title) if not series else None
                version=row_version(r)
                editable_time=time_fields(r)
                for date in starts:
                    all_day=bool(r.get('isAllDay'))
                    hour=int(r['start_hour']);minute=int(r['start_minute'])
                    clock=dt.time(hour,minute).strftime('%H:%M')
                    native_key=json.dumps([source_id,str(date)] if series else [source_id],
                                          ensure_ascii=False,separators=(',', ':'))
                    item={'id':source_id+'@'+str(date),'date':str(date),'endDate':str(date+dt.timedelta(days=duration)),
                                  'time':'全天' if all_day else clock,'title':title, 'allDay':all_day,
                                  'sourceId':source_id,'version':version,'series':series,'editableTime':editable_time,
                                  'completionKey':native_key}
                    if work_task:
                        item['completionKey']=json.dumps(['work-todo',work_task[1]],separators=(',', ':'))
                        item['workTaskId']=work_task[1]
                    items.append(item)
            except (KeyError,ValueError,TypeError,OverflowError):
                warnings.add('部分日程格式异常，请在系统日历查看')
    items.sort(key=lambda x:(x['date'],not x['allDay'],x['time'],x['id']))
    return {'items':items,'warning':'；'.join(sorted(warnings))}


def mutate_schedule(path, request):
    action = request['action']
    if action not in ('edit', 'delete') or not request['id'] or not request['version']:
        raise ValueError('事项操作无效')
    path = Path(path)
    # Read/write mode will never create an empty system calendar database.
    with sqlite3.connect(path.resolve().as_uri()+'?mode=rw', uri=True, timeout=.7) as conn:
        conn.row_factory = sqlite3.Row
        conn.execute('BEGIN IMMEDIATE')
        rows = conn.execute('SELECT * FROM Schedule WHERE id=? LIMIT 2', (request['id'],)).fetchall()
        if len(rows) != 1 or row_version(dict(rows[0])) != request['version']:
            raise ValueError('事项已变化，请刷新后再试')
        row = dict(rows[0])
        if action == 'delete':
            conn.execute('DELETE FROM Schedule WHERE id=?', (request['id'],))
            return
        title = request['title'].strip()
        if not title:
            raise ValueError('请填写事项内容')
        changes = {'descript': title}
        if 'date' in request:
            current = time_fields(row)
            if not current:
                raise ValueError('此事项的时间请在系统日历修改')
            new = dt.date.fromisoformat(request['date'])
            iso = str(new)
            if iso != current['date']:
                changes.update(start_date=iso, end_date=iso, start_day=str(new.day), start_month=str(new.month),
                               beginDateTime=iso+row['beginDateTime'][10:], endDateTime=iso+row['endDateTime'][10:])
                for field, value in (('end_day', new.day), ('end_month', new.month)):
                    if str(row[field]) != '0':
                        changes[field] = str(value)
            if not row['isAllDay']:
                start, end = (dt.time.fromisoformat(request[key]) for key in ('start', 'end'))
                if end < start:
                    raise ValueError('结束时间不能早于开始时间')
                changes.update(start_hour=str(start.hour), start_minute=str(start.minute),
                               end_hour=str(end.hour), end_minute=str(end.minute))
        # All column names above are fixed by the application; bind every value.
        assignments = ','.join(f'{column}=?' for column in changes)
        conn.execute(f'UPDATE Schedule SET {assignments} WHERE id=?', (*changes.values(), request['id']))


def work_todo_client():
    override = os.environ.get('UKUI_FENCES_WORK_TODO_CLIENT')
    candidates = [Path(override)] if override else [
        Path.home()/location/'skills/work-todo-midplatform/scripts/work_todo.py'
        for location in ('.gemini/config', '.kylinbot/workspace')
    ]
    path = next((p for p in candidates if p.is_file()), None)
    if path is None:
        raise FileNotFoundError('work-todo client')
    spec = importlib.util.spec_from_file_location('fences_work_todo_client', path)
    client = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(client)
    return client


def work_todo_request(client, task_id=None, note=''):
    conf = client.load_conf()

    def read_states():
        tasks = client.api(conf, 'GET', '/api/tasks', params={'mine': 'true', 'archived': 'include'})
        if not isinstance(tasks, list):
            raise ValueError('invalid work-todo response')
        return {str(task['id']): task['status'] for task in tasks
                if isinstance(task, dict) and str(task.get('id', '')).isdigit()
                and isinstance(task.get('status'), str) and task['status'] in WORK_TODO_STATES}

    states = read_states()
    if task_id is None:
        return {'statuses': states}
    # Verify current ownership and state. A task already awaiting review or
    # accepted as done is never submitted twice, even from a stale calendar.
    if states.get(task_id) in ('pending_review', 'done'):
        return {'statuses': states}
    if states.get(task_id) not in ('accepted', 'in_progress'):
        return {'statuses': states, 'error': '这条待办当前不能提交审核，请核对中台状态。'}
    if not note.strip() or len(note)>2000:
        return {'statuses': states, 'error': '请填写 1～2000 字的验收说明。'}
    result = client.api(conf, 'POST', '/api/tasks/%d/submit' % int(task_id),
                        {'type': 'result', 'content': note.strip(), 'progress_pct': 100})
    status = result.get('status') if isinstance(result, dict) else None
    if isinstance(status, str) and status in WORK_TODO_STATES:
        states[task_id] = status
    else:
        states = read_states()
    reply = {'statuses': states}
    if states.get(task_id) not in ('pending_review', 'done'):
        reply['error'] = '中台尚未确认提交，请稍后核对状态。'
    return reply


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--database');parser.add_argument('--begin');parser.add_argument('--end')
    parser.add_argument('--agenda-today');parser.add_argument('--mutate',action='store_true')
    work=parser.add_mutually_exclusive_group()
    work.add_argument('--work-status',action='store_true');work.add_argument('--work-submit')
    args=parser.parse_args()
    if args.work_status or args.work_submit is not None:
        try:
            task_id=args.work_submit
            if task_id is not None and not re.fullmatch(r'[0-9]{1,18}',task_id):
                raise ValueError('invalid work-todo id')
            result=work_todo_request(work_todo_client(),task_id,sys.stdin.read(4096) if task_id is not None else '')
        except FileNotFoundError:
            result={'error':'未找到本机工作待办连接。'}
        except (Exception,SystemExit):
            result={'error':'中台未确认提交，请核对最新状态。' if args.work_submit is not None else '暂时无法刷新中台状态，显示上次结果。'}
        print(json.dumps(result,ensure_ascii=False));raise SystemExit(1 if result.get('error') else 0)
    if not args.database:
        parser.error('--database is required for calendar operations')
    if args.mutate:
        try:
            mutate_schedule(args.database,json.load(sys.stdin))
            print('{}')
        except ValueError as error:
            print(json.dumps({'error':str(error)},ensure_ascii=False));raise SystemExit(1)
        except (sqlite3.Error,OSError,KeyError,TypeError):
            print(json.dumps({'error':'系统日历暂时无法保存，请稍后重试'},ensure_ascii=False));raise SystemExit(1)
        raise SystemExit(0)
    try:
        begin,end=day(args.begin),day(args.end)
        if end<begin or (end-begin).days>400:raise ValueError('Invalid date range')
        snapshot=read_snapshot(args.database,begin,end,day(args.agenda_today) if args.agenda_today else None)
        try:snapshot['dates']=metadata(begin,end)
        except (OSError,RuntimeError,ValueError,AttributeError) as error:
            snapshot['dates']={};snapshot['calendarWarning']='农历数据暂不可用'
        print(json.dumps(snapshot,ensure_ascii=False))
    except (sqlite3.Error,ValueError,OSError) as error:
        print(json.dumps({'error':'系统日历暂时无法读取，请稍后重试'},ensure_ascii=False));raise SystemExit(1)
