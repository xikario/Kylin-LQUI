"""Date resolution, persistence and the explicit fallback picker, in isolation."""

import datetime as dt
import sys
import tempfile
import unittest
from pathlib import Path

from PyQt5 import QtCore, QtWidgets

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import calendar_data
import drop_todo

TODAY = dt.date(2026, 11, 24)
MEETING = "各位同事，本周项目例会通知。时间：11月25日 10:00 地点：办公楼2022会议室。议题：1、进度；2、安排。"


class DropTodoTests(unittest.TestCase):
    def test_dates(self):
        examples = [
            (MEETING, "2026-11-25"),
            ("25号交报告", "2026-11-25"),
            ("时间：１１月２５日 １０：００", "2026-11-25"),
            ("二〇二七年一月二十五日", "2027-01-25"),
            ("2027/1/2 复查", "2027-01-02"),
            ("开会时间：11/25", "2026-11-25"),
            ("下月2号交表", "2026-12-02"),
            ("明年1月5日开会", "2027-01-05"),
            ("明天下午提交", "2026-11-25"),
            ("大后天再看", "2026-11-27"),
            ("周五前交周报", "2026-11-27"),
            ("下周一开会", "2026-11-30"),
            ("会议时间：12月9日（星期三）", "2026-12-09"),
            ("通知日期：11月24日，会议时间：11月25日", "2026-11-25"),
            ("原定于11月24日，改至11月25日", "2026-11-25"),
            ("今天开会", "2026-11-24"),
        ]
        for text, expected in examples:
            with self.subTest(text=text):
                self.assertEqual(
                    drop_todo.infer_date(text, TODAY)[0], dt.date.fromisoformat(expected)
                )
        self.assertEqual(
            drop_todo.infer_date("1月2日开会", dt.date(2026, 12, 30))[0], dt.date(2027, 1, 2)
        )

    def test_uncertain_requires_input(self):
        for text in (
            "整理材料",
            "地点：2022会议室，时间10:00",
            "建设路25号开会",
            "任务完成1/2",
            "时间：2月30日",
            "11月25日或26日",
            "11月25日到12月2日",
            "时间：11/25 或 11/26",
            "会议时间：11月25日下午3点至26日",
            "今天收到通知，请整理材料",
            "每周三开会",
            "仅有发布日期：11月24日",
            "今天收到通知，会议时间待定",
            "11月25日（周五）",
            "参见 https://example.org/2026-11-25",
        ):
            with self.subTest(text=text):
                self.assertIsNone(drop_todo.infer_date(text, TODAY)[0])
        self.assertEqual(
            drop_todo.infer_date("12月25日或26日", TODAY)[1],
            [dt.date(2026, 12, 25), dt.date(2026, 12, 26)],
        )

    def test_long_text_and_local_file_dates(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp) / "11月24日发送.txt"
            f.write_text(MEETING)
            for payload in [{"text": "说明。" * 120 + MEETING}, {"files": [str(f)]}]:
                writes = []
                result = drop_todo.add_from_drop(
                    dict(payload, title="例会"),
                    TODAY,
                    writes.append,
                    lambda *_: self.fail("清晰日期不应询问"),
                )
                self.assertEqual(result, "2026-11-25")
                self.assertEqual(writes, [{"action": "add", "title": "例会", "date": "2026-11-25"}])

    def test_confirm_cancel_and_sqlite(self):
        with tempfile.TemporaryDirectory() as tmp:
            db = Path(tmp) / "tasks.sqlite"

            def write(request):
                calendar_data.mutate_task(db, request)

            data = {"text": "讨论会议安排", "title": "讨论会议安排"}
            self.assertIsNone(drop_todo.add_from_drop(data, TODAY, write, lambda *_: None))
            self.assertFalse(db.exists())
            chosen = dt.date(2026, 12, 8)
            self.assertEqual(
                drop_todo.add_from_drop(data, TODAY, write, lambda *_: chosen), chosen.isoformat()
            )
            self.assertEqual([r["date"] for r in calendar_data.tasks(db)], ["2026-12-08"])

    def test_picker_requires_explicit_valid_day(self):
        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(["drop-test"])
        facts = []

        def fill():
            dlg = app.activeModalWidget()
            box = dlg.findChild(QtWidgets.QComboBox)
            save = dlg.findChild(QtWidgets.QDialogButtonBox).button(QtWidgets.QDialogButtonBox.Save)
            facts.append((box.currentText(), save.isEnabled()))
            box.setEditText("2026-02-30")
            facts.append(save.isEnabled())
            box.setEditText("2026-12-08")
            facts.append(save.isEnabled())
            save.click()

        QtCore.QTimer.singleShot(80, fill)
        self.assertEqual(drop_todo.choose_date("讨论会议安排", [TODAY]), dt.date(2026, 12, 8))
        self.assertEqual(facts, [("", False), False, True])
        QtCore.QTimer.singleShot(80, lambda: app.activeModalWidget().reject())
        self.assertIsNone(drop_todo.choose_date("讨论会议安排", []))


if __name__ == "__main__":
    unittest.main()
