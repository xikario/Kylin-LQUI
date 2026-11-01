#!/usr/bin/env python3
"""Worker stdout is a JSON-lines protocol; parser diagnostics must stay out of it."""
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import knowledge_jobs
import smart_space_extract as extract


class StageProtocolTest(unittest.TestCase):
    def forward(self, script):
        process = subprocess.Popen([sys.executable, "-c", script], stdout=subprocess.PIPE)
        events = []
        stderr = io.StringIO()
        try:
            with mock.patch.object(
                knowledge_jobs, "emit", side_effect=lambda **row: events.append(row)
            ), contextlib.redirect_stderr(stderr):
                result = knowledge_jobs.forward_stage_output(process, lambda: None)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
        return result, events, stderr.getvalue()

    def test_stray_parser_output_does_not_fail_the_stage(self):
        # Mirrors xlrd's CompDoc diagnostics for a damaged legacy workbook.
        script = (
            "import json\n"
            "print(json.dumps(dict(task='build',processed=1,total=2)),flush=True)\n"
            "print('_locate_stream(Workbook): seen')\n"
            "print('    0  5 4 4 4 4 4')\n"
            "print('4')\n"
            "print(json.dumps(dict(done=True,task='build',documents=2)),flush=True)\n"
        )
        result, events, stderr = self.forward(script)
        self.assertEqual(result, {})
        self.assertEqual([row.get("processed") for row in events], [1, None])
        self.assertTrue(events[-1]["done"])
        self.assertIn("_locate_stream(Workbook): seen", stderr)

    def test_stray_output_reports_are_bounded(self):
        script = "for i in range(500): print('noise %d' % i)\n"
        result, events, stderr = self.forward(script)
        self.assertEqual(events, [])
        self.assertEqual(stderr.count("\n"), knowledge_jobs.STRAY_LINE_REPORT_LIMIT)

    def test_worker_error_is_still_reported_next_to_noise(self):
        script = (
            "import json\n"
            "print('stray diagnostic')\n"
            "print(json.dumps(dict(done=True,task='build',error='容量不足')))\n"
            "raise SystemExit(1)\n"
        )
        result, events, stderr = self.forward(script)
        self.assertEqual(result["error"], "容量不足")


class LegacyWorkbookDiagnosticsTest(unittest.TestCase):
    def test_xlrd_diagnostics_never_reach_stdout(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "damaged.xls"
            path.write_bytes(extract.OLE_MAGIC)
            released = []

            def open_workbook(filename, on_demand=False, logfile=None):
                # xlrd's own default is logfile=sys.stdout.
                print("_locate_stream(Workbook): seen", file=logfile or sys.stdout)
                return types.SimpleNamespace(
                    sheet_names=lambda: [], release_resources=lambda: released.append(True)
                )

            import xlrd

            stdout = io.StringIO()
            with mock.patch.object(xlrd, "open_workbook", open_workbook), contextlib.redirect_stdout(
                stdout
            ):
                self.assertEqual(list(extract.Extraction(path).extract_xls()), [])
            self.assertEqual(stdout.getvalue(), "")
            self.assertEqual(released, [True])

    def test_open_failure_still_raises(self):
        import xlrd

        with mock.patch.object(xlrd, "open_workbook", side_effect=xlrd.XLRDError("bad")):
            with self.assertRaises(xlrd.XLRDError):
                list(extract.Extraction(Path("/nonexistent.xls")).extract_xls())


if __name__ == "__main__":
    unittest.main()
