"""Resolve the date of a dropped personal task before writing to the calendar."""

import datetime as dt
import json
from pathlib import Path
import re
import sqlite3
import sys
import unicodedata

DIGITS = "零一二三四五六七八九"
N = r"[0-9一二三四五六七八九十]{1,3}"
TOKENS = re.compile(
    rf"(?<!\d)(?:(?P<year>\d{{4}}|[零一二三四五六七八九〇○]{{4}}|今|明|后|去)\s*年\s*)?"
    rf"(?P<month>{N})\s*月\s*(?P<md>{N})\s*[日号]?(?!\d)"
    r"|(?<![\d./-])(?P<iso_y>\d{4})(?P<sep>[-/.])(?P<iso_m>\d{1,2})(?P=sep)(?P<iso_d>\d{1,2})(?!\d)"
    r"|(?<![\d./-])(?P<short_m>\d{1,2})[-/.](?P<short_d>\d{1,2})(?![\d./-])"
    rf"|(?<!\d)(?:(?P<when_month>下个?月|上个?月|本月|这个月|当月)\s*)?(?P<day>{N})\s*[日号]"
    r"|(?P<relative>大后天|后天|明天|明日|今天|今日|昨天|昨日|前天)"
    r"|(?P<week>下下|上上|这|本|下|上)?(?:星期|周|礼拜)\s*(?P<weekday>[一二三四五六日天1-7])"
)
LABEL = re.compile(
    r"(?:(?:会议|开会|活动|开课|培训|面试|预约|提醒)(?:时间|日期)|截止(?:日期|时间)?|最迟|最晚|定于|安排在|(?:^|[\s,。;])(?:时间|日期))\s*[:为是于在]?\s*$"
)
CHANGED_TO = re.compile(r"(?:改至|改到|改为|延期至|推迟到)\s*[:]?\s*$")
METADATA = re.compile(r"(?:发文|发布|印发|收件|通知|发送|更新|创建)(?:日期|时间)\s*[:为]?\s*$")
TASK_FOLLOWS = re.compile(r"^\s*(?:\d{1,2}:\d{2}\s*)?(?:开会|参会|召开|举行|提交|交付|前提交)")
CHOICE = re.compile(
    r"^\s*(?:\(?(?:周|星期)[一二三四五六日天]\)?\s*)?"
    r"(?:(?:上午|下午|晚上|中午)?\s*\d{1,2}(?::\d{2}|点(?:半|\d{1,2}分?)?)\s*)?"
    r"\)?\s*(?:或(?:者)?|[至到与和、~/—-])\s*$"
)
RELATIVE = {
    "今天": 0,
    "今日": 0,
    "明天": 1,
    "明日": 1,
    "后天": 2,
    "大后天": 3,
    "昨天": -1,
    "昨日": -1,
    "前天": -2,
}
TEXT_LIMIT = 256 * 1024


def number(s):
    if s.isascii() and s.isdecimal():
        return int(s)
    if "十" in s:
        ten, unit = s.split("十", 1)
        if len(ten) > 1 or len(unit) > 1:
            raise ValueError("无效的中文数字")
        return (DIGITS.index(ten) if ten else 1) * 10 + (DIGITS.index(unit) if unit else 0)
    return int("".join(str(DIGITS.index(c)) for c in s.replace("〇", "零").replace("○", "零")))


def token_date(m, today):
    g = m.groupdict()
    if g["iso_y"]:
        return dt.date(int(g["iso_y"]), int(g["iso_m"]), int(g["iso_d"]))
    if g["relative"]:
        return today + dt.timedelta(days=RELATIVE[g["relative"]])
    if g["weekday"]:
        day = g["weekday"]
        weekday = (
            int(day) - 1 if day.isdecimal() else "一二三四五六日".index(day.replace("天", "日"))
        )
        if g["week"]:
            weeks = {"下下": 2, "下": 1, "上上": -2, "上": -1, "本": 0, "这": 0}[g["week"]]
            return today + dt.timedelta(days=weeks * 7 + weekday - today.weekday())
        return today + dt.timedelta(days=(weekday - today.weekday()) % 7)
    if g["day"]:
        offset = {"上月": -1, "上个月": -1, "下月": 1, "下个月": 1}.get(g["when_month"], 0)
        year, month = divmod(today.year * 12 + today.month - 1 + offset, 12)
        return dt.date(year, month + 1, number(g["day"]))
    month = number(g["month"] or g["short_m"])
    day = number(g["md"] or g["short_d"])
    years = {"今": 0, "明": 1, "后": 2, "去": -1}
    year = today.year
    if g["year"]:
        year = today.year + years[g["year"]] if g["year"] in years else number(g["year"])
    if not g["year"] and today.month == 12 and month == 1:
        year += 1
    return dt.date(year, month, day)


def infer_date(text, today):
    """Return a single clear date or choices for the review dialog."""
    text = unicodedata.normalize("NFKC", text)
    text = re.sub(r"(?:https?://|file://)\S+", " ", text)
    scores = {}
    uncertain = bool(re.search(r"(?:时间|日期)\s*:?\s*(?:待定|另行通知|待确认)", text))
    previous = None
    for m in TOKENS.finditer(text):
        before = text[max(0, m.start() - 18) : m.start()]
        after = text[m.end() : m.end() + 16]
        rank = int(bool(LABEL.search(before) or TASK_FOLLOWS.match(after)))
        if METADATA.search(before) or re.match(
            r"\s*(?:收到|发布|印发|转发)(?:了|的)?(?:通知|文件)", after
        ):
            rank = -1
        elif CHANGED_TO.search(before):
            rank = 2
        # A fraction, software version, elapsed duration or a recurring day is
        # insufficient to schedule a one-off task.
        joined = previous and CHOICE.fullmatch(text[previous[0] : m.start()])
        if m["short_m"] and not (
            rank > 0
            or joined
            or re.match(r"\s*(?:\d{1,2}:\d{2}|[日号]|开会|会议|前|提交|截止)", after)
            or text.strip() == m.group()
        ):
            continue
        if m["day"] and re.search(
            r"(?:有效期|历时|为期|提前|持续|第|工作满|路|街|巷|门牌|工位|编号|房间|批号|会议室)\s*[:]?\s*$",
            before,
        ):
            continue
        if m["day"] and m.group().endswith("号") and re.match(r"[线楼房栋桌座]", after):
            continue
        if re.search(r"每(?:个?月|个)?\s*$", before) or re.search(r"农历\s*$", before):
            uncertain = True
        try:
            date = token_date(m, today)
        except (ValueError, OverflowError, KeyError):
            uncertain = True
            continue
        if previous:
            end, prev = previous
            between = text[end : m.start()]
            # A weekday after an explicit date often annotates the same day.
            if m["weekday"] and not m["week"] and re.fullmatch(r"\s*\(?\s*", between):
                if prev.weekday() != date.weekday():
                    uncertain = True
                previous = (m.end(), prev)
                continue
            if CHOICE.fullmatch(between):
                if m["day"] and not m["when_month"]:
                    try:
                        date = prev.replace(day=number(m["day"]))
                    except ValueError:
                        uncertain = True
                if date != prev:
                    uncertain = True
        scores[date] = max(scores.get(date, rank), rank)
        previous = (m.end(), date)
    highest = max(scores.values(), default=-1)
    best = [d for d, rank in scores.items() if rank == highest and rank >= 0]
    return (best[0] if len(best) == 1 and not uncertain else None), sorted(scores)


def date_material(payload):
    parts = [payload.get("text", "")]
    names = []
    budget = TEXT_LIMIT
    for path in map(Path, payload.get("files", [])):
        names.append(path.name)
        if path.suffix.lower() not in {".txt", ".md", ".markdown", ".rst"} or budget <= 0:
            continue
        try:
            if not path.is_file() or path.stat().st_size > budget:
                continue
            with open(path, "rb") as f:
                raw = f.read(budget + 1)
            if len(raw) > budget:
                continue
            budget -= len(raw)
            parts.append(raw.decode("utf-8-sig"))
        except (OSError, UnicodeError):
            pass
    return "\n".join(parts), "\n".join(names)


def choose_date(title, choices):
    from PyQt5 import QtCore as C, QtWidgets as W

    app = W.QApplication.instance()
    if app is None:
        W.QApplication.setAttribute(C.Qt.AA_EnableHighDpiScaling)
        app = W.QApplication(sys.argv[:1])
    dialog = W.QDialog()
    dialog.setWindowTitle("选择待办日期")
    dialog.setWindowFlag(C.Qt.WindowStaysOnTopHint)
    layout = W.QVBoxLayout(dialog)
    hint = W.QLabel(
        "内容中有多个或不明确的日期，请确认。" if choices else "未能确定待办日期，请选择或填写。"
    )
    layout.addWidget(hint)
    note = W.QLabel(title[:100])
    note.setTextFormat(C.Qt.PlainText)
    note.setWordWrap(True)
    layout.addWidget(note)
    field = W.QComboBox()
    field.setEditable(True)
    field.setInsertPolicy(W.QComboBox.NoInsert)
    field.addItems([d.isoformat() for d in choices])
    field.setCurrentIndex(-1)
    field.lineEdit().setPlaceholderText("输入 YYYY-MM-DD 或点击日历")
    field.setAccessibleName("待办日期")
    layout.addWidget(field)
    cal = W.QCalendarWidget()
    cal.setGridVisible(False)
    if choices:
        cal.setCurrentPage(choices[0].year, choices[0].month)
    layout.addWidget(cal)

    def select(day):
        field.setEditText(day.toString(C.Qt.ISODate))

    cal.clicked.connect(select)
    cal.activated.connect(select)
    buttons = W.QDialogButtonBox(W.QDialogButtonBox.Save | W.QDialogButtonBox.Cancel)
    save = buttons.button(W.QDialogButtonBox.Save)
    save.setText("加入待办")
    save.setEnabled(False)
    buttons.button(W.QDialogButtonBox.Cancel).setText("取消")
    layout.addWidget(buttons)

    def parsed():
        text = unicodedata.normalize("NFKC", field.currentText().strip())
        match = re.fullmatch(r"(\d{4})[-/.年](\d{1,2})[-/.月](\d{1,2})日?", text)
        if match:
            try:
                return dt.date(*map(int, match.groups()))
            except ValueError:
                pass
        return None

    field.currentTextChanged.connect(lambda _: save.setEnabled(parsed() is not None))
    buttons.accepted.connect(dialog.accept)
    buttons.rejected.connect(dialog.reject)
    field.setFocus()
    C.QTimer.singleShot(0, dialog.activateWindow)
    return parsed() if dialog.exec_() == W.QDialog.Accepted else None


def add_from_drop(payload, today, writer, chooser=choose_date):
    title = payload["title"]
    body, names = date_material(payload)
    date, choices = infer_date(body, today)
    if not body.strip():
        date, choices = infer_date(names, today)
    if date is None:
        date = chooser(title, choices)
    if date is None:
        return None
    writer({"action": "add", "title": title, "date": date.isoformat()})
    return date.isoformat()


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import calendar_data

    try:
        result = add_from_drop(
            json.load(sys.stdin),
            dt.date.today(),
            lambda request: calendar_data.mutate_task(calendar_data.task_path(), request),
        )
        print(json.dumps({"date": result}))
    except (OSError, ValueError, sqlite3.Error) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
