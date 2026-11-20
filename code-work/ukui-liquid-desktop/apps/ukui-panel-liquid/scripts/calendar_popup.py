#!/usr/bin/python3
"""Independent Qt5 calendar popup; no native calendar widgets in the panel.

The single session-bus owner receives logical screen coordinates. All data I/O
runs in bounded child processes. Hidden windows stop refreshing and exit after
one minute unless an explicit write is still in flight.
"""
import argparse
import json
import os
from pathlib import Path
import sys

from PyQt5 import QtCore as C, QtGui as G, QtWidgets as W, QtDBus as D
from calendar_data import ADAPTER
from calendar_appearance import Material, STYLE, AgendaDelegate, icon
import popup_style  # settings dir is on sys.path after calendar_appearance

BUS = 'org.kylin.Lqui.Calendar'
HERE = Path(__file__).resolve().parent
STATES = {'published': '待接收', 'accepted': '已接收', 'in_progress': '进行中',
          'pending_review': '待审核', 'done': '已完成', 'rejected': '已拒绝', 'cancelled': '已取消'}


def popup_rect(anchor, available, preferred):
    width, height = min(preferred.width(), available.width()), min(preferred.height(), available.height())
    x = max(available.left(), min(anchor.right() - width + 1, available.right() - width + 1))
    y = anchor.top() - height - popup_style.GAP
    if y < available.top():
        y = anchor.bottom() + popup_style.GAP
    y = max(available.top(), min(y, available.bottom() - height + 1))
    return C.QRect(x, y, width, height)


class Month(W.QCalendarWidget):
    def __init__(self):
        super().__init__()
        self.dates, self.marked = {}, set()
        self.setVerticalHeaderFormat(self.NoVerticalHeader)
        self.setHorizontalHeaderFormat(self.ShortDayNames)
        self.setGridVisible(False)
        self.setLocale(C.QLocale('zh_CN'))
        self.setFirstDayOfWeek(C.Qt.Monday)
        weekend = G.QTextCharFormat(); weekend.setForeground(G.QColor('#e2a59e'))
        for day in (C.Qt.Saturday,C.Qt.Sunday): self.setWeekdayTextFormat(day,weekend)
        self.setMinimumDate(C.QDate(1901, 1, 1))
        self.setMaximumDate(C.QDate(2099, 12, 31))
        self.setNavigationBarVisible(False)
        self.setMinimumHeight(234)

    def paintCell(self, painter, rect, date):
        key = date.toString('yyyy-MM-dd')
        info = self.dates.get(key, {})
        painter.save()
        painter.setRenderHint(G.QPainter.Antialiasing)
        painter.setPen(C.Qt.NoPen)
        selected = date == self.selectedDate()
        if selected:
            painter.setBrush(G.QColor(126,210,194,52))
            painter.drawRoundedRect(rect.adjusted(3, 2, -3, -2), 10, 10)
        if date == C.QDate.currentDate():
            painter.setPen(G.QPen(G.QColor('#8fd7cd'), 1))
            painter.setBrush(C.Qt.NoBrush)
            painter.drawRoundedRect(rect.adjusted(3, 3, -3, -3), 7, 7)
        font = painter.font(); font.setPixelSize(15); painter.setFont(font)
        painter.setPen(G.QColor('#f1f5f9' if date.month() == self.monthShown() else '#708191'))
        painter.drawText(rect.adjusted(0, 3, 0, -rect.height() // 2 + 2), C.Qt.AlignCenter, str(date.day()))
        font.setPixelSize(10); painter.setFont(font)
        painter.setPen(G.QColor('#ffd28b' if info.get('festival') or info.get('solarTerm') else '#aebfcd'))
        label = info.get('label', '')
        painter.drawText(rect.adjusted(2, rect.height() // 2, -2, -2), C.Qt.AlignCenter,
                         painter.fontMetrics().elidedText(label, C.Qt.ElideRight, rect.width()-4))
        if 'off' in info:
            painter.setPen(G.QColor('#ffb6ac' if info['off'] else '#8dbdff'))
            painter.drawText(rect.adjusted(0, 1, -4, 0), C.Qt.AlignTop | C.Qt.AlignRight, '休' if info['off'] else '班')
        if key in self.marked:
            painter.setPen(C.Qt.NoPen); painter.setBrush(G.QColor('#8fd7cd'))
            painter.drawEllipse(C.QPointF(rect.left()+7, rect.center().y()), 2, 2)
        painter.restore()


class Agenda(W.QListWidget):
    def sizeHint(self): return C.QSize(300,110)


class Calendar(Material, W.QWidget):
    def __init__(self, database=None):
        super().__init__(None, C.Qt.Tool | C.Qt.FramelessWindowHint)
        self.init_material()
        self.setObjectName('calendarRoot')
        self.setWindowTitle('LQ 日历')
        self.resize(440, 740)
        self.database = database or str(Path(os.environ.get('XDG_CONFIG_HOME') or Path.home()/'.config') / '.kylin_calendar_Schedule2.db')
        self.anchor = C.QRect()
        self.jobs, self.pending, self.writing = set(), False, False
        self.generation, self.reading, self.loaded = 0, False, False
        self.data, self.statuses = {}, {}
        self.settings = C.QSettings('kylin', 'ukui-fences')
        self.poll = C.QTimer(self); self.poll.setInterval(30000); self.poll.timeout.connect(self.refresh)
        self.idle = C.QTimer(self); self.idle.setSingleShot(True); self.idle.timeout.connect(self.idle_exit)
        self.popup_fade = C.QVariantAnimation(self); self.popup_fade.setStartValue(0.0); self.popup_fade.setEndValue(1.0)
        self.popup_fade.setEasingCurve(C.QEasingCurve.OutCubic)
        self.popup_fade.valueChanged.connect(lambda value: self.setWindowOpacity(float(value)))
        self.setStyleSheet(STYLE)
        outer = W.QVBoxLayout(self); outer.setContentsMargins(18,17,18,17); outer.setSpacing(10)
        header = W.QHBoxLayout(); title = W.QLabel('日历'); title.setObjectName('centerTitle')
        header.addWidget(title); subtitle=W.QLabel('日程与待办'); subtitle.setObjectName('muted'); header.addWidget(subtitle); header.addStretch()
        self.icon_button(header,'refresh','刷新',self.refresh)
        self.icon_button(header,'close','关闭日历',self.hide)
        outer.addLayout(header)
        card=W.QFrame(); card.setObjectName('monthCard'); month_layout=W.QVBoxLayout(card)
        month_layout.setContentsMargins(10,8,10,10); month_layout.setSpacing(4)
        navigation=W.QHBoxLayout(); self.month_title=self.button(navigation,'',self.choose_month)
        self.month_title.setObjectName('monthTitle'); navigation.addStretch()
        self.button(navigation,'今天',lambda:self.month.setSelectedDate(C.QDate.currentDate())).setProperty('quiet',True)
        self.icon_button(navigation,'back','上个月',lambda:self.month.showPreviousMonth())
        self.icon_button(navigation,'chevron','下个月',lambda:self.month.showNextMonth())
        month_layout.addLayout(navigation)
        self.month = Month(); month_layout.addWidget(self.month); outer.addWidget(card)
        self.date_label = W.QLabel(); self.date_label.setObjectName('selectedDate'); outer.addWidget(self.date_label)
        self.info = W.QLabel(); self.info.setObjectName('muted'); self.info.setWordWrap(True); outer.addWidget(self.info)
        tabs=W.QHBoxLayout(); segment=W.QFrame(); segment.setObjectName('segment')
        segment_layout=W.QHBoxLayout(segment); segment_layout.setContentsMargins(3,3,3,3); segment_layout.setSpacing(2)
        self.day_tab=self.button(segment_layout,'当天事项',self.render)
        self.todo_tab=self.button(segment_layout,'全部待办',self.render)
        tabs.addWidget(segment)
        group = W.QButtonGroup(self); group.setExclusive(True)
        for button in (self.day_tab, self.todo_tab): button.setCheckable(True); group.addButton(button)
        self.day_tab.setChecked(True); tabs.addStretch(); self.button(tabs, '＋ 待办', self.add_task)
        outer.addLayout(tabs)
        self.list = Agenda(); self.list.setMinimumHeight(116); self.list.setItemDelegate(AgendaDelegate(self.list)); outer.addWidget(self.list, 1)
        self.list.setHorizontalScrollBarPolicy(C.Qt.ScrollBarAlwaysOff)
        self.list.itemDoubleClicked.connect(self.details)
        self.list.currentItemChanged.connect(self.actions)
        actions = W.QHBoxLayout()
        self.detail_button = self.button(actions, '详情', self.details)
        self.complete_button = self.button(actions, '完成', self.complete)
        self.edit_button = self.button(actions, '编辑', self.edit)
        self.delete_button = self.button(actions, '删除', self.delete)
        for action in (self.detail_button,self.complete_button,self.edit_button,self.delete_button): action.setProperty("quiet",True)
        outer.addLayout(actions)
        self.message = W.QLabel('正在读取日程…'); self.message.setObjectName('muted'); self.message.setWordWrap(True); outer.addWidget(self.message)
        footer = W.QHBoxLayout(); legend = W.QLabel('休 法定休假   班 调休上班'); legend.setObjectName('muted'); footer.addWidget(legend)
        footer.addStretch(); self.sync_button = self.button(footer, '同步节假日', self.sync_holidays); outer.addLayout(footer)
        self.month.currentPageChanged.connect(self.page_changed)
        self.month.selectionChanged.connect(self.render)
        self.update_month_title(); self.actions()

    def button(self, layout, text, callback, tip=None):
        button = W.QPushButton(text); button.setCursor(C.Qt.PointingHandCursor); button.clicked.connect(callback); button.setToolTip(tip or text)
        layout.addWidget(button); return button

    def icon_button(self,layout,name,tip,callback):
        button=W.QToolButton(); button.setIcon(icon(name)); button.setIconSize(C.QSize(18,18)); button.setFixedSize(28,28)
        button.setProperty('quiet',True); button.setToolTip(tip); button.setAccessibleName(tip); button.setCursor(C.Qt.PointingHandCursor)
        button.clicked.connect(callback); layout.addWidget(button); return button

    def update_month_title(self):
        self.month_title.setText('%d 年 %d 月' % (self.month.yearShown(),self.month.monthShown()))

    def choose_month(self):
        menu=W.QMenu(self)
        year_menu=menu.addMenu('选择年份')
        for year in range(max(1901,self.month.yearShown()-10),min(2099,self.month.yearShown()+10)+1):
            action=year_menu.addAction(str(year)+' 年'); action.triggered.connect(lambda checked=False,y=year:self.month.setCurrentPage(y,self.month.monthShown()))
        for month in range(1,13):
            action=menu.addAction(str(month)+' 月'); action.setCheckable(True); action.setChecked(month==self.month.monthShown())
            action.triggered.connect(lambda checked=False,m=month:self.month.setCurrentPage(self.month.yearShown(),m))
        menu.exec_(self.month_title.mapToGlobal(C.QPoint(0,self.month_title.height())))

    def run(self, script, args, payload, callback, timeout=6000):
        process = C.QProcess(self); self.jobs.add(process)
        timer = C.QTimer(process); timer.setSingleShot(True); timer.timeout.connect(process.kill)
        def finish(code=1, status=None):
            if process not in self.jobs: return
            timer.stop(); self.jobs.remove(process)
            try: result = json.loads(bytes(process.readAllStandardOutput()))
            except (ValueError, UnicodeError): result = {'error': '读取超时或服务未响应，请重试。'}
            if not isinstance(result, dict): result = {'error': '服务返回了无效数据。'}
            if code and not result.get('error'): result['error'] = '服务执行失败，请重试。'
            process.deleteLater(); callback(result)
        process.finished.connect(finish)
        process.errorOccurred.connect(lambda error: finish() if error == C.QProcess.FailedToStart else None)
        if payload is not None:
            content = payload if isinstance(payload,str) else json.dumps(payload, ensure_ascii=False)
            process.started.connect(lambda: (process.write(content.encode()), process.closeWriteChannel()))
        process.start('/usr/bin/python3', [str(script), *args]); timer.start(timeout)

    def worker(self, request, callback):
        self.run(HERE/'calendar_data.py', [], request, callback)

    def page_changed(self, *_):
        self.update_month_title()
        self.generation += 1; self.refresh()

    def refresh(self):
        if not self.isVisible(): return
        if self.reading: self.pending = True; return
        self.reading = True; token = self.generation
        first = C.QDate(self.month.yearShown(), self.month.monthShown(), 1)
        begin = first.addDays(1-first.dayOfWeek()); end = begin.addDays(41)
        def done(data):
            self.reading = False
            if token == self.generation:
                if data.get('error'): self.message.setText(data['error'])
                else:
                    self.data = data; self.loaded = True; self.month.dates = data.get('dates', {})
                    self.render(); self.refresh_work()
            if self.pending or token != self.generation:
                self.pending = False; self.refresh()
        self.worker({'operation':'snapshot', 'database':self.database,
                     'begin':begin.toString('yyyy-MM-dd'), 'end':end.toString('yyyy-MM-dd')}, done)

    def refresh_work(self):
        if not self.isVisible() or getattr(self, 'work_busy', False) or not any(x.get('workTaskId') for x in self.records()): return
        self.work_busy = True
        def done(data):
            self.work_busy = False
            if 'statuses' in data:
                self.statuses = data['statuses']; self.save_work_cache()
            self.render()
            if data.get('error'): self.message.setText(data['error'])
        self.run(ADAPTER/'calendar_snapshot.py', ['--work-status'], None, done, 12000)

    def records(self):
        self.settings.sync()
        completed = set(self.settings.value('desklets/calendar/completed', []) or [])
        result = []
        native = list(self.data.get('items', []))
        # The importer removes rows after submission. Retain their date slots,
        # sharing the existing desktop calendar's cache and authoritative states.
        cached = self.work_cache()
        known = {x.get('workTaskId') for x in native}
        for task, item in cached.get('items', {}).items():
            if task not in known and self.statuses.get(task,cached.get('statuses',{}).get(task)) in ('pending_review','done'):
                native.append(item)
        for original in native:
            item = dict(original); item['kind'] = 'native'
            task = item.get('workTaskId')
            state = self.statuses.get(task,cached.get('statuses',{}).get(task))
            item['state'] = STATES.get(state, '状态待确认') if task else '系统日程'
            if task and task not in self.statuses and state: item['state'] += '（上次状态）'
            item['completed'] = state == 'done' if task else item.get('completionKey') in completed
            result.append(item)
        for original in self.data.get('tasks', []):
            item = dict(original); item.update(kind='task', time='待办', endDate=item['date'], state='个人待办')
            result.append(item)
        return result

    def work_cache(self):
        try:
            raw = self.settings.value('desklets/calendar/workAgenda', C.QByteArray())
            data = json.loads(bytes(raw) if isinstance(raw,C.QByteArray) else raw)
            return data if isinstance(data,dict) and isinstance(data.get('items'),dict) and isinstance(data.get('statuses'),dict) else {}
        except (ValueError,TypeError): return {}

    def save_work_cache(self):
        self.settings.sync(); data = self.work_cache()
        slots = data.get('items', {})
        for item in self.data.get('items', []):
            if item.get('workTaskId'): slots[item['workTaskId']] = item
        data = {'items':slots, 'statuses':self.statuses}
        self.settings.setValue('desklets/calendar/workAgenda', C.QByteArray(json.dumps(data,ensure_ascii=False).encode()))
        self.settings.sync()

    def render(self):
        key = self.month.selectedDate().toString('yyyy-MM-dd'); info = self.month.dates.get(key, {})
        self.date_label.setText(C.QLocale('zh_CN').toString(self.month.selectedDate(),'yyyy 年 M 月 d 日  dddd'))
        labels = [info.get('yearName', '')+info.get('lunar', ''), info.get('festival', ''), info.get('solarTerm', '')]
        if 'off' in info:
            if info.get('holiday'): labels.append(info['holiday'])
            labels.append('休假' if info['off'] else '调休上班')
        if info and not info.get('scheduleKnown'): labels.append('本年调休数据尚未收录')
        self.info.setText('  ·  '.join(dict.fromkeys(x for x in labels if x)))
        previous = self.current(); selected = (previous.get('kind'), previous.get('id'))
        self.list.clear(); records = self.records()
        self.month.marked = {item['date'] for item in records if not item.get('completed')}; self.month.updateCells()
        shown = [item for item in records if (not item.get('completed') if self.todo_tab.isChecked() else item['date'] <= key <= item.get('endDate', item['date']))]
        for item in sorted(shown, key=lambda x:(bool(x.get('completed')), x['date'], x.get('time',''), x.get('title',''))):
            prefix = '✓ ' if item.get('completed') else ''
            row = W.QListWidgetItem(prefix+item.get('title','')+'\n'+item['date']+'  '+item.get('time','')+' · '+item['state'])
            row.setData(C.Qt.UserRole, item); row.setToolTip(item.get('title','')); self.list.addItem(row)
            if (item['kind'], item['id']) == selected: self.list.setCurrentItem(row)
        if not shown:
            row = W.QListWidgetItem('暂无未完成待办' if self.todo_tab.isChecked() else '当天暂无事项，可点击「＋ 待办」添加')
            row.setFlags(C.Qt.NoItemFlags); self.list.addItem(row)
        warnings = [self.data.get(key, '') for key in ('warning','calendarWarning','taskError')]
        self.message.setText('；'.join(x for x in warnings if x) or ('共 %d 条 · 工作待办完成需经中台审核' % len(shown)))
        self.actions()

    def current(self):
        row = self.list.currentItem()
        return row.data(C.Qt.UserRole) or {} if row else {}

    def actions(self, *_):
        item = self.current(); work = bool(item.get('workTaskId')); enabled = bool(item) and not self.writing
        self.detail_button.setEnabled(bool(item))
        self.edit_button.setEnabled(enabled and not work); self.delete_button.setEnabled(enabled and not work)
        self.complete_button.setText('提交审核' if work else '恢复待办' if item.get('completed') else '完成')
        self.complete_button.setEnabled(enabled and (not work or self.statuses.get(item['workTaskId']) in ('accepted','in_progress')))

    def details(self, *_):
        item = self.current()
        if not item: return
        W.QMessageBox.information(self, '事项详情', item.get('title','')+'\n\n'+item['date']+'  '+item.get('time','')+'\n'+item['state']+
                                 ('\n工作待办由中台管理，提交审核后才可确认完成。' if item.get('workTaskId') else ''))

    def save(self, operation, change):
        if self.writing: return
        self.writing = True; self.actions()
        def done(result):
            self.writing = False; self.actions(); self.generation += 1; self.refresh()
            if result.get('error'): W.QMessageBox.warning(self, '未保存', result['error'])
        self.worker({'operation':operation, 'database':self.database, 'change':change}, done)

    def editor(self, item=None):
        item = item or {}; dialog = W.QDialog(self); dialog.setWindowTitle('编辑事项' if item else '新建个人待办')
        form = W.QFormLayout(dialog); title = W.QPlainTextEdit(item.get('title','')); title.setMinimumWidth(300); title.setMaximumHeight(110)
        form.addRow('内容', title)
        date = W.QDateEdit(C.QDate.fromString(item.get('date',''), 'yyyy-MM-dd') if item else self.month.selectedDate()); date.setCalendarPopup(True)
        date.setDisplayFormat('yyyy-MM-dd'); form.addRow('日期', date)
        editable = item.get('editableTime', {}); native = item.get('kind') == 'native'
        date.setEnabled(not native or bool(editable))
        start, end = W.QTimeEdit(), W.QTimeEdit()
        if native and editable and not item.get('allDay'):
            start.setTime(C.QTime.fromString(editable['start'],'HH:mm')); end.setTime(C.QTime.fromString(editable['end'],'HH:mm'))
            form.addRow('开始', start); form.addRow('结束', end)
        if item.get('series'): form.addRow(W.QLabel('修改内容作用于整组重复日程；此处不调整重复规则。'))
        buttons = W.QDialogButtonBox(W.QDialogButtonBox.Save | W.QDialogButtonBox.Cancel)
        buttons.button(W.QDialogButtonBox.Save).setText('保存'); buttons.button(W.QDialogButtonBox.Cancel).setText('取消')
        form.addRow(buttons); buttons.accepted.connect(dialog.accept); buttons.rejected.connect(dialog.reject)
        if dialog.exec_() != W.QDialog.Accepted: return
        change = {'action':'edit' if item else 'add', 'title':title.toPlainText(), 'date':date.date().toString('yyyy-MM-dd')}
        if native:
            change.update(id=item['sourceId'], version=item['version'])
            if not editable: change.pop('date')
            elif not item.get('allDay'): change.update(start=start.time().toString('HH:mm'), end=end.time().toString('HH:mm'))
        elif item: change.update(id=item['id'], version=item['version'])
        self.save('native' if native else 'task', change)

    def add_task(self):
        if not self.writing: self.editor()

    def edit(self):
        item = self.current()
        if item and not item.get('workTaskId') and not self.writing: self.editor(item)

    def delete(self):
        item = self.current()
        if not item or item.get('workTaskId') or self.writing: return
        text = '删除整组重复事项？' if item.get('series') else '删除所选事项？'
        if W.QMessageBox.question(self,'删除事项',text,W.QMessageBox.Yes | W.QMessageBox.Cancel,W.QMessageBox.Cancel) == W.QMessageBox.Yes:
            self.save('task' if item['kind']=='task' else 'native', {'action':'delete','id':item.get('sourceId',item['id']), 'version':item['version']})

    def complete(self):
        item = self.current()
        if not item or self.writing: return
        if item.get('workTaskId'):
            note, accepted = W.QInputDialog.getText(self, '提交工作待办审核', '验收说明（审核通过后才显示已完成）：')
            if not accepted or not note.strip(): return
            self.writing = True; self.actions()
            def done(result):
                self.writing = False
                if 'statuses' in result:
                    self.statuses = result['statuses']; self.save_work_cache()
                self.render()
                if result.get('error'): W.QMessageBox.warning(self,'提交审核',result['error'])
            # Work adapter accepts plain UTF-8 note, not JSON.
            self.run(ADAPTER/'calendar_snapshot.py', ['--work-submit',item['workTaskId']], note, done, 12000)
        elif item['kind']=='task':
            self.save('task', {'action':'complete','id':item['id'],'version':item['version'],'completed':not item.get('completed')})
        else:
            self.settings.sync(); keys = set(self.settings.value('desklets/calendar/completed',[]) or [])
            key = item['completionKey']; keys.discard(key) if key in keys else keys.add(key)
            self.settings.setValue('desklets/calendar/completed',sorted(keys)); self.settings.sync()
            if self.settings.status()!=C.QSettings.NoError: W.QMessageBox.warning(self,'未保存','完成状态未能保存，请重试。')
            self.render()

    def sync_holidays(self):
        self.sync_button.setEnabled(False)
        def done(result):
            self.sync_button.setEnabled(True); self.generation += 1; self.refresh()
            W.QMessageBox.information(self,'节假日同步',result.get('message') or result.get('error','同步完成'))
        years = sorted({C.QDate.currentDate().year(), self.month.yearShown(), C.QDate.currentDate().year()+1})
        self.run(ADAPTER/'holiday_sync.py', [arg for y in years for arg in ('--year',str(y))], None, done, 35000)

    def adapt_to_area(self, area):
        compact=area.height()<730
        self.month.setFixedHeight(200 if compact else 234)
        self.list.setMinimumHeight(90 if compact else 116)
        self.layout().setSpacing(6 if compact else 10)
        self.layout().setContentsMargins(16 if compact else 18,12 if compact else 17,16 if compact else 18,12 if compact else 17)
        self.layout().activate()

    def toggle(self, anchor):
        if W.QApplication.activeModalWidget(): return
        if self.isVisible(): self.hide(); return
        self.anchor = anchor
        screen = W.QApplication.screenAt(anchor.center()) or W.QApplication.primaryScreen()
        self.adapt_to_area(screen.availableGeometry())
        self.setGeometry(popup_rect(anchor, screen.availableGeometry(), C.QSize(440,740)))
        self.prepare_material()
        self.idle.stop(); self.fade_in(); self.poll.start(); self.refresh()

    def fade_in(self):
        self.popup_fade.stop()
        duration = popup_style.fade_duration(True)
        self.setWindowOpacity(0.0 if duration > 0 else 1.0)
        self.show(); self.raise_(); self.activateWindow()
        if duration > 0:
            self.popup_fade.setDuration(duration); self.popup_fade.start()

    def hideEvent(self, event):
        self.popup_fade.stop(); self.setWindowOpacity(1.0)
        self.cancel_material()
        self.poll.stop(); self.idle.start(60000); super().hideEvent(event)

    def idle_exit(self):
        if self.isVisible(): return
        if self.jobs or self.writing: self.idle.start(10000)
        else: W.QApplication.quit()

    def keyPressEvent(self, event):
        if event.key()==C.Qt.Key_Escape: self.hide()
        else: super().keyPressEvent(event)

    def event(self, event):
        if event.type()==C.QEvent.WindowDeactivate:
            C.QTimer.singleShot(0,self.hide_if_inactive)
        return super().event(event)

    def hide_if_inactive(self):
        if self.isVisible() and not self.isActiveWindow() and not W.QApplication.activeModalWidget() and not W.QApplication.activePopupWidget(): self.hide()


class BusObject(C.QObject):
    C.Q_CLASSINFO('D-Bus Interface', BUS)
    def __init__(self, window): super().__init__(); self.window = window
    @C.pyqtSlot(int,int,int,int)
    def Toggle(self,x,y,w,h): self.window.toggle(C.QRect(x,y,w,h))
    @C.pyqtSlot(result=str)
    def State(self):
        window=self.window; rect=window.geometry()
        return json.dumps({'pid':os.getpid(),'visible':window.isVisible(),'loaded':window.loaded,
                           'refreshActive':window.poll.isActive(),'jobs':len(window.jobs),'materialBuilds':window.material_builds,'materialReady':window.material_ready,
                           'geometry':[rect.x(),rect.y(),rect.width(),rect.height()]})


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--anchor', default='0,0,1,1'); args=parser.parse_args()
    try:
        coords = [int(x) for x in args.anchor.split(',')]
        if len(coords)!=4 or coords[2]<1 or coords[3]<1: raise ValueError()
    except ValueError: parser.error('anchor must be x,y,width,height')
    configure_application()
    app=W.QApplication(sys.argv); app.setStyle('Fusion'); app.setQuitOnLastWindowClosed(False)
    app.setApplicationName('lqui-calendar'); app.setOrganizationName('kylin')
    bus=D.QDBusConnection.sessionBus()
    if not bus.isConnected(): return 1
    if not bus.registerService(BUS):
        message=D.QDBusMessage.createMethodCall(BUS,'/Calendar',BUS,'Toggle'); message.setArguments(coords)
        reply=bus.call(message, D.QDBus.Block, 1500)
        return 1 if reply.type()==D.QDBusMessage.ErrorMessage else 0
    window=Calendar(); bridge=BusObject(window)
    if not bus.registerObject('/Calendar',bridge,D.QDBusConnection.ExportAllSlots): return 1
    window.toggle(C.QRect(*coords)); return app.exec_()


def configure_application():
    # Must run before QApplication exists. Without high-DPI pixmaps, Qt hands a
    # logical-size pixmap to a 1.5x screen and upscales it: icons turn jagged.
    W.QApplication.setAttribute(C.Qt.AA_EnableHighDpiScaling)
    W.QApplication.setAttribute(C.Qt.AA_UseHighDpiPixmaps)


if __name__=='__main__': sys.exit(main())
