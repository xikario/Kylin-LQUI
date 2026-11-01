#!/usr/bin/env python3
"""FTS5 segment merging after builds and Agent library discovery."""
import contextlib
import io
import json
import os
from pathlib import Path
import runpy
import sqlite3
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

APP = Path(__file__).resolve().parents[1]
SCRIPTS = APP / "scripts"
QUERY = APP / "skills/ai-space-index-query/scripts/query.py"
sys.path.insert(0, str(SCRIPTS))
import knowledge_library as lib
import smart_space_knowledge as worker


def merge_pending(db):
    """True when an FTS5 'merge' would still do work (the index is fragmented)."""
    db.execute("SAVEPOINT probe")
    before = db.total_changes
    db.execute("INSERT INTO chunks_fts(chunks_fts, rank) VALUES('merge', -2000)")
    pending = db.total_changes - before >= 2
    db.execute("ROLLBACK TO probe")
    db.execute("RELEASE probe")
    return pending


def fragment(db, rounds=12):
    """Rewrite identical FTS rows in separate commits, leaving many segments."""
    rows = db.execute(
        "SELECT rowid,name,path,content,locator,document_id,chunk_id FROM chunks_fts"
    ).fetchall()
    for _ in range(rounds):
        for row in rows:
            db.execute("DELETE FROM chunks_fts WHERE rowid=?", (row[0],))
            db.execute(
                "INSERT INTO chunks_fts(rowid,name,path,content,locator,document_id,chunk_id) VALUES(?,?,?,?,?,?,?)",
                row,
            )
        db.commit()
    db.execute("DELETE FROM meta WHERE key=?", (worker.FTS_OPTIMIZED_KEY,))
    db.commit()


class IndexMaintenanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="fts-merge-test-")
        self.root = Path(self.temp.name)
        self.env = mock.patch.dict(
            os.environ,
            XDG_CONFIG_HOME=str(self.root / "config"),
            XDG_DATA_HOME=str(self.root / "data"),
        )
        self.env.start()
        originals = self.root / "originals"
        originals.mkdir()
        items = []
        for number in range(6):
            document = originals / ("方案%d.txt" % number)
            document.write_text("宁波银行 KMS 部署方案第%d章。\n" % number + "统一密钥管理。\n" * 200)
            items.append(
                dict(
                    path=str(document),
                    root=str(originals),
                    name=document.name,
                    isDir=False,
                    category="documents",
                    content="",
                )
            )
        self.index = self.root / "index.json"
        self.index.write_text(json.dumps(dict(items=items), ensure_ascii=False))
        self.db = self.root / "personal.sqlite"

    def tearDown(self):
        self.env.stop()
        self.temp.cleanup()

    def build(self, *extra):
        output = subprocess.check_output(
            [
                sys.executable,
                str(SCRIPTS / "smart_space_knowledge.py"),
                "build",
                "--db",
                str(self.db),
                "--index-json",
                str(self.index),
                "--no-ocr",
                *extra,
            ],
            text=True,
            timeout=60,
        )
        events = [json.loads(line) for line in output.splitlines() if line.strip()]
        self.assertFalse(events[-1].get("error"), events[-1])
        return events

    def marker(self):
        with contextlib.closing(sqlite3.connect(str(self.db))) as db:
            row = db.execute(
                "SELECT value FROM meta WHERE key=?", (worker.FTS_OPTIMIZED_KEY,)
            ).fetchone()
            return row[0] if row else None

    def search(self):
        answer = lib.search_one(self.db, "宁波银行 部署方案", 20, 5000, True)
        return sorted((r["document_id"], r["chunk_id"]) for r in answer["results"])

    def test_build_merges_index_and_unchanged_rebuild_skips(self):
        events = self.build()
        self.assertEqual(events[-1]["indexMaintenance"]["status"], "optimized")
        self.assertTrue(any(e.get("phase") == "migrate" for e in events[:-1]))
        self.assertTrue(self.marker())
        with contextlib.closing(sqlite3.connect(str(self.db))) as db:
            self.assertFalse(merge_pending(db))
        self.assertEqual(self.build()[-1]["indexMaintenance"]["status"], "current")

    def test_fragmented_legacy_index_is_merged_without_changing_results(self):
        self.build()
        expected = self.search()
        self.assertTrue(expected)
        db = worker.open_db(str(self.db))
        try:
            fragment(db)
            self.assertTrue(merge_pending(db))
            with contextlib.redirect_stdout(io.StringIO()):
                result = worker.optimize_fts(db, str(self.db), changed=False)
            self.assertEqual(result["status"], "optimized")
            self.assertGreater(result["steps"], 1)
            self.assertFalse(merge_pending(db))
            db.execute("INSERT INTO chunks_fts(chunks_fts) VALUES('integrity-check')")
        finally:
            db.close()
        self.assertTrue(self.marker())
        self.assertEqual(self.search(), expected)

    def test_storage_limit_skips_merge_and_keeps_text(self):
        self.build()
        db = worker.open_db(str(self.db))
        try:
            fragment(db, rounds=3)
            limit = lib.StorageLimitError("Library reached configured size limit")
            with mock.patch.dict(
                os.environ, AI_SPACE_STORAGE_DIRECTORY=str(self.root)
            ), mock.patch.object(lib, "guard_storage", side_effect=limit):
                with contextlib.redirect_stdout(io.StringIO()):
                    result = worker.optimize_fts(db, str(self.db), changed=True)
            self.assertEqual(result["status"], "skipped")
            self.assertIn("size limit", result["reason"])
            self.assertEqual(db.execute("SELECT count(*) FROM documents").fetchone()[0], 6)
        finally:
            db.close()
        self.assertIsNone(self.marker(), "an unfinished merge must be retried next build")
        self.assertTrue(self.search())

    def test_retry_build_defers_merge_to_next_build(self):
        self.build()
        events = self.build("--retry-document-id", "1")
        self.assertEqual(events[-1]["indexMaintenance"]["status"], "deferred")
        self.assertIsNone(self.marker())
        self.assertEqual(self.build()[-1]["indexMaintenance"]["status"], "optimized")


class AgentDiscoveryTest(unittest.TestCase):
    """query.py runs in-process; the worker call and account lookup are mocked."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="skill-discovery-test-")
        self.root = Path(self.temp.name)
        self.profile = self.root / "profile"
        self.account = self.root / "account"
        settings = self.account / ".config/xikario/ai-space.ini"
        settings.parent.mkdir(parents=True)
        settings.write_text("[smartSpace]\nknowledgeDirectory=%s\n" % (self.root / "lib"))

    def tearDown(self):
        self.temp.cleanup()

    def run_query(self, *arguments, **environment):
        base = {
            "HOME": str(self.profile),
            "XDG_CONFIG_HOME": str(self.profile / ".config"),
            "XDG_DATA_HOME": str(self.profile / ".local/share"),
        }
        base.update(environment)
        account = types.SimpleNamespace(pw_dir=str(self.account))
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, base), mock.patch.object(
            sys, "argv", [str(QUERY), *arguments]
        ), mock.patch("pwd.getpwuid", return_value=account), mock.patch(
            "subprocess.call", return_value=0
        ) as call, contextlib.redirect_stderr(
            stderr
        ):
            for name in ("AI_SPACE_KNOWLEDGE_DB", "UKUI_FENCES_KNOWLEDGE_DB"):
                if name not in environment:
                    os.environ.pop(name, None)
            with self.assertRaises(SystemExit):
                runpy.run_path(str(QUERY), run_name="__main__")
        return call.call_args[1]["env"], stderr.getvalue()

    def test_rewritten_home_without_data_uses_account_home(self):
        env, note = self.run_query("libraries")
        self.assertEqual(env["XDG_CONFIG_HOME"], str(self.account / ".config"))
        self.assertEqual(env["XDG_DATA_HOME"], str(self.account / ".local/share"))
        self.assertIn("AI_SPACE_ACCOUNT_HOME_FALLBACK=0", note)

    def test_profile_with_own_settings_is_respected(self):
        own = self.profile / ".config/ai-space/libraries.json"
        own.parent.mkdir(parents=True)
        own.write_text('{"version": 1, "libraries": []}')
        env, note = self.run_query("search", "--query", "银行")
        self.assertEqual(env["XDG_CONFIG_HOME"], str(self.profile / ".config"))
        self.assertEqual(note, "")

    def test_explicit_selection_or_opt_out_disables_fallback(self):
        cases = [
            (("--db", str(self.root / "x.aikb"), "stats"), {}),
            (("libraries",), {"AI_SPACE_KNOWLEDGE_DB": str(self.root / "x.sqlite")}),
            (("libraries",), {"UKUI_FENCES_KNOWLEDGE_DB": str(self.root / "x.sqlite")}),
            (("libraries",), {"AI_SPACE_ACCOUNT_HOME_FALLBACK": "0"}),
        ]
        for arguments, environment in cases:
            with self.subTest(arguments=arguments, environment=environment):
                env, note = self.run_query(*arguments, **environment)
                self.assertEqual(env["XDG_CONFIG_HOME"], str(self.profile / ".config"))
                self.assertEqual(note, "")

    def test_account_without_ai_space_data_changes_nothing(self):
        (self.account / ".config/xikario/ai-space.ini").unlink()
        env, note = self.run_query("libraries")
        self.assertEqual(env["XDG_CONFIG_HOME"], str(self.profile / ".config"))
        self.assertEqual(note, "")


if __name__ == "__main__":
    unittest.main()
