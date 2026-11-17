"""Calendar worker: reuse the desktop calendar adapter, keep personal tasks separate.

Runs outside both the taskbar and the calendar's GUI thread. Native records retain
their existing IDs and optimistic concurrency checks; personal tasks never write
the OEM calendar schema or impersonate work-platform tasks.
"""
import datetime as dt
import json
import os
from pathlib import Path
import sqlite3
import sys
import uuid

HERE = Path(__file__).resolve().parent
ADAPTER = HERE if (HERE / 'calendar_snapshot.py').exists() else HERE.parent.parent / 'ukui-fences/scripts'
sys.path.insert(0, str(ADAPTER))
from calendar_snapshot import read_snapshot, mutate_schedule
from chinese_calendar import metadata


def task_path():
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / 'lqui-calendar/tasks.sqlite'


def tasks(path):
    if not path.exists():
        return []
    with sqlite3.connect(path.resolve().as_uri() + '?mode=ro', uri=True, timeout=.5) as conn:
        conn.row_factory = sqlite3.Row
        return [dict(row) for row in conn.execute('SELECT * FROM tasks ORDER BY date,title LIMIT 10000')]


def mutate_task(path, request):
    action = request['action']
    if action not in ('add', 'edit', 'delete', 'complete'):
        raise ValueError('无效的待办操作')
    if action in ('add', 'edit'):
        title = str(request['title']).strip()
        if not title or len(title) > 2000:
            raise ValueError('待办内容需为 1～2000 字')
        date = dt.date.fromisoformat(request['date']).isoformat()
    path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(path, timeout=.5) as conn:
        conn.execute('CREATE TABLE IF NOT EXISTS tasks(id TEXT PRIMARY KEY,title TEXT NOT NULL,date TEXT NOT NULL,completed INTEGER NOT NULL DEFAULT 0,version INTEGER NOT NULL DEFAULT 1)')
        conn.execute('BEGIN IMMEDIATE')
        if action == 'add':
            conn.execute('INSERT INTO tasks(id,title,date) VALUES(?,?,?)', (str(uuid.uuid4()), title, date))
            return
        key = (request['id'], int(request['version']))
        if action == 'delete':
            result = conn.execute('DELETE FROM tasks WHERE id=? AND version=?', key)
        elif action == 'complete':
            result = conn.execute('UPDATE tasks SET completed=?,version=version+1 WHERE id=? AND version=?', (int(bool(request['completed'])), *key))
        else:
            result = conn.execute('UPDATE tasks SET title=?,date=?,version=version+1 WHERE id=? AND version=?', (title, date, *key))
        if result.rowcount != 1:
            raise ValueError('待办已变化，请刷新后再试')


def snapshot(database, begin, end):
    result = {'items': [], 'dates': {}, 'tasks': []}
    # An unavailable native store must not disable the date grid or personal tasks.
    try:
        result.update(read_snapshot(database, begin, end, dt.date.today()))
    except (sqlite3.Error, OSError, ValueError):
        result['warning'] = '系统日程暂时无法读取；农历和个人待办仍可使用。'
    try:
        result['dates'] = metadata(begin, end)
    except (OSError, RuntimeError, ValueError, AttributeError):
        result['calendarWarning'] = '农历／节假日数据暂不可用，请稍后刷新。'
    try:
        result['tasks'] = tasks(task_path())
    except (sqlite3.Error, OSError):
        result['taskError'] = '个人待办暂时无法读取，请稍后刷新。'
    return result


def main():
    request = json.load(sys.stdin)
    action = request['operation']
    if action == 'snapshot':
        begin, end = (dt.date.fromisoformat(request[key]) for key in ('begin', 'end'))
        if end < begin or (end - begin).days > 400:
            raise ValueError('日期范围无效')
        return snapshot(request['database'], begin, end)
    if action == 'native':
        mutate_schedule(request['database'], request['change'])
    elif action == 'task':
        mutate_task(task_path(), request['change'])
    else:
        raise ValueError('无效的日历操作')
    return {}


if __name__ == '__main__':
    try:
        print(json.dumps(main(), ensure_ascii=False))
    except Exception as error:
        message = str(error) if isinstance(error, ValueError) else '日历操作失败，原有数据已保留，请刷新后重试。'
        print(json.dumps({'error': message}, ensure_ascii=False))
        sys.exit(1)
