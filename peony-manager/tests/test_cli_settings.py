import json
import os
import fcntl
import multiprocessing
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time
import uuid
import unittest
from unittest.mock import patch
from app import cli_settings, codex_profile


def _concurrent_preference_update(changes, start):
    start.wait()
    cli_settings.update(changes)


class CliSettingsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.env = patch.dict(os.environ, {"XDG_CONFIG_HOME": self.directory.name})
        self.env.start()
        self.profile = Path(self.directory.name) / "codex-home" / "peony-cli.config.toml"
        self.profile_override = patch.object(codex_profile, "profile_path", return_value=self.profile)
        self.profile_override.start()

    def tearDown(self):
        self.profile_override.stop()
        self.env.stop()
        self.directory.cleanup()

    def test_default_is_lazy_and_window_bound(self):
        value = cli_settings.read()
        self.assertFalse(value["defaultVisible"])
        self.assertTrue(value["closeWithWindow"])
        self.assertTrue(value["followSystemFont"])
        self.assertEqual(value["fontFamily"], "Sarasa Fixed SC")
        self.assertEqual(value["contentPadding"], 10)
        self.assertEqual(value["model"], "")
        self.assertEqual(value["reasoningEffort"], "")
        self.assertEqual(value["modelArgs"], [])
        self.assertFalse(cli_settings.config_path().exists())
        self.assertFalse(self.profile.exists())

    def test_roundtrip_and_forced_lifecycle(self):
        value = cli_settings.defaults()
        value.update(name='工具 <"中文">', command="codex --profile '有 空格'", enabled=False,
                     defaultVisible=True, closeWithWindow=False)
        cli_settings.save(value)
        loaded = cli_settings.read()
        self.assertEqual(loaded["command"], value["command"])
        self.assertEqual(loaded["name"], value["name"])
        self.assertFalse(loaded["enabled"])
        self.assertFalse(loaded["defaultVisible"])
        self.assertTrue(loaded["closeWithWindow"])
        self.assertEqual(cli_settings.config_path().stat().st_mode & 0o777, 0o600)

    def test_model_preferences_are_peony_only_and_validated(self):
        value = cli_settings.defaults()
        value.update(model="gpt-6-luna", reasoningEffort="xhigh")
        isolated_codex_home = Path(self.directory.name) / "other-codex-home"
        with patch.dict(os.environ, {"CODEX_HOME": str(isolated_codex_home)}):
            saved = cli_settings.save(value)
        self.assertTrue(self.profile.exists())
        self.assertFalse(isolated_codex_home.exists())
        self.assertEqual(saved["modelArgs"], ["--model", "gpt-6-luna", "--config", 'model_reasoning_effort="xhigh"'])
        self.assertTrue(saved["modelArgsApplicable"])
        self.assertEqual(cli_settings.read()["model"], "gpt-6-luna")
        self.assertEqual(cli_settings.read()["reasoningEffort"], "xhigh")

        custom = dict(saved, command="codex --profile user-choice -m custom-model", model="vendor/custom-model")
        custom = cli_settings.save(custom)
        self.assertFalse(custom["modelArgsApplicable"])
        self.assertEqual(custom["command"], "codex --profile user-choice -m custom-model")
        self.assertEqual(custom["model"], "vendor/custom-model")

        before = cli_settings.config_path().read_bytes()
        for bad in ({"model": "gpt-6-luna; touch /tmp/pwned"}, {"model": "-moverride"},
                    {"reasoningEffort": "xhigh; echo unsafe"}, {"reasoningEffort": "future-level"}):
            with self.assertRaises(ValueError):
                cli_settings.save(dict(cli_settings.defaults(), **bad))
            self.assertEqual(cli_settings.config_path().read_bytes(), before)

    def test_legacy_ui_save_preserves_model_until_explicitly_cleared(self):
        saved = cli_settings.defaults()
        saved.update(model="gpt-6-luna", reasoningEffort="xhigh")
        saved = cli_settings.save(saved)
        old_keys = ("version", "enabled", "name", "menuLabel", "command", "widthPercent", "fontSize",
                    "historyLines", "defaultVisible", "closeWithWindow", "followSystemFont", "fontFamily", "contentPadding")
        old_page = {key: saved[key] for key in old_keys}
        old_page["fontSize"] = 14
        after_old_save = cli_settings.save(old_page)
        self.assertEqual(after_old_save["fontSize"], 14)
        self.assertEqual(after_old_save["model"], "gpt-6-luna")
        self.assertEqual(after_old_save["reasoningEffort"], "xhigh")

        # Codex's profile-backed /model picker is the effective source of truth.
        self.profile.write_text('model = "gpt-6.1-terra" # chosen in /model\n', encoding="utf-8")
        self.assertEqual(cli_settings.read()["model"], "gpt-6.1-terra")
        after_old_save = cli_settings.save(old_page)
        self.assertEqual(after_old_save["model"], "gpt-6.1-terra")
        self.assertIn('model = "gpt-6.1-terra" # chosen in /model', self.profile.read_text(encoding="utf-8"))

        old_page.update(model="", reasoningEffort="")
        cleared = cli_settings.save(old_page)
        self.assertEqual(cleared["model"], "")
        self.assertEqual(cleared["reasoningEffort"], "")
        self.assertNotRegex(self.profile.read_text(encoding="utf-8"), r"(?m)^\s*(?:model|model_reasoning_effort)\s*=")

    def test_profile_creation_migrates_unmodified_legacy_preference(self):
        old = cli_settings.defaults()
        old.update(model="gpt-6-luna", reasoningEffort="xhigh")
        cli_settings._write_unlocked(cli_settings.config_path(), cli_settings.validate(old))

        saved = cli_settings.update({"model": "gpt-6.1-sol"})

        self.assertEqual(saved["model"], "gpt-6.1-sol")
        self.assertEqual(saved["reasoningEffort"], "xhigh")
        self.assertEqual(codex_profile.read_preferences(),
                         {"model": "gpt-6.1-sol", "reasoningEffort": "xhigh"})

    def test_profile_update_preserves_toml_and_only_edits_root_keys(self):
        original = ('# owned by Codex\n'
                    'model = "gpt-6.1-sol" # picker choice\n'
                    'approval_policy = "on-request"\n'
                    '\n[tui]\n'
                    'model = "table-value"\n'
                    '\n[tui.model_availability_nux]\n'
                    'seen = true\n'
                    '\n[[extensions]]\n'
                    'name = "untouched"\n')
        self.profile.parent.mkdir(parents=True)
        self.profile.write_text(original, encoding="utf-8")

        cli_settings.update({"reasoningEffort": "xhigh"})
        updated = self.profile.read_text(encoding="utf-8")
        self.assertIn('model = "gpt-6.1-sol" # picker choice\n', updated)
        self.assertIn('approval_policy = "on-request"\n', updated)
        self.assertIn('model_reasoning_effort = "xhigh"\n', updated)
        self.assertIn('[tui]\nmodel = "table-value"\n', updated)
        self.assertIn('[tui.model_availability_nux]\nseen = true\n', updated)
        self.assertIn('[[extensions]]\nname = "untouched"\n', updated)
        self.assertEqual(cli_settings.read()["model"], "gpt-6.1-sol")

        cli_settings.update({"model": "gpt-6.1-terra"})
        self.assertIn('model = "gpt-6.1-terra" # picker choice\n', self.profile.read_text(encoding="utf-8"))

    def test_unsafe_profile_is_never_overwritten(self):
        self.profile.parent.mkdir(parents=True)
        for content in (
                'model = "gpt-6-luna"\nmodel = "gpt-6.1-sol"\n',
                '"model" = "gpt-6-luna"\n',
                '"\\u006dodel" = "gpt-6-luna"\n',
                'model.provider = "custom"\n',
                'other = [\n  "value",\n]\n'):
            with self.subTest(content=content):
                self.profile.write_text(content, encoding="utf-8")
                before = self.profile.read_bytes()
                with self.assertRaises(ValueError):
                    cli_settings.update({"reasoningEffort": "xhigh"})
                self.assertEqual(self.profile.read_bytes(), before)

    def test_concurrent_model_and_effort_updates_serialize(self):
        cli_settings.save(cli_settings.defaults())
        context = multiprocessing.get_context("fork")
        start = context.Event()
        model = context.Process(target=_concurrent_preference_update, args=({"model": "gpt-6-luna"}, start))
        effort = context.Process(target=_concurrent_preference_update, args=({"reasoningEffort": "xhigh"}, start))
        with cli_settings.lock_path().open("a+") as held_lock:
            fcntl.flock(held_lock.fileno(), fcntl.LOCK_EX)
            model.start()
            effort.start()
            start.set()
            try:
                time.sleep(0.2)
                self.assertTrue(model.is_alive(), "model update must wait for the config transaction lock")
                self.assertTrue(effort.is_alive(), "effort update must wait for the config transaction lock")
            finally:
                fcntl.flock(held_lock.fileno(), fcntl.LOCK_UN)
        model.join(timeout=5)
        effort.join(timeout=5)
        self.assertEqual(model.exitcode, 0)
        self.assertEqual(effort.exitcode, 0)
        saved = cli_settings.read()
        self.assertEqual(saved["model"], "gpt-6-luna")
        self.assertEqual(saved["reasoningEffort"], "xhigh")

    def test_managed_model_args_reach_a_fake_cli_as_literal_argv(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ is required to exercise the native session helper")
        with tempfile.TemporaryDirectory(prefix="peony-cli-argv-") as temporary:
            root = Path(temporary)
            working = root / "dir quote ' $HOME ; literal"
            working.mkdir()
            codex = root / "fake codex"
            marker = root / "codex-argv.json"
            codex.write_text("#!/usr/bin/python3\nimport json,os,sys\nopen(os.environ['PEONY_CLI_ARGV_MARKER'],'w').write(json.dumps({'argv':sys.argv[1:],'cwd':os.getcwd(),'loginEnv':os.environ.get('PEONY_TEST_LOGIN_ENV')}))\n")
            codex.chmod(0o755)
            runner = root / "fake systemd-run"
            run_marker = root / "systemd-run-argv.json"
            runner.write_text("#!/usr/bin/python3\nimport json,os,sys\nopen(os.environ['PEONY_SYSTEMD_RUN_MARKER'],'w').write(json.dumps(sys.argv[1:]))\ni=sys.argv.index('--')+1\nos.execv(sys.argv[i],sys.argv[i:])\n")
            runner.chmod(0o755)
            home = root / "isolated home"
            home.mkdir()
            (home / ".bash_profile").write_text("export PEONY_TEST_LOGIN_ENV='from login profile'\n")
            helper = root / "peony-cli-session"
            source = Path(__file__).resolve().parents[1] / "plugins/peony-cli-panel/src/session-guard.cpp"
            subprocess.run([compiler, "-std=c++11", "-O2", "-DPEONY_CLI_SYSTEMD_RUN=\"%s\"" % runner,
                            str(source), "-o", str(helper)], check=True)

            with patch.object(cli_settings.shutil, "which", return_value=str(codex)):
                settings = cli_settings.defaults()
                settings.update(command=shlex.quote(str(codex)), model="gpt-6-luna", reasoningEffort="xhigh")
                settings = cli_settings.validate(settings)
            self.assertTrue(settings["modelArgsApplicable"])
            unit = "peony-cli-" + uuid.uuid4().hex + ".scope"
            values = [str(working), settings["codexExecutable"], *settings["modelArgs"]]
            encoded = [value.encode("utf-8").hex() for value in values]
            environment = dict(os.environ, HOME=str(home), PEONY_CLI_ARGV_MARKER=str(marker),
                               PEONY_SYSTEMD_RUN_MARKER=str(run_marker))
            subprocess.run([str(helper), "--run-argv", unit, *encoded], cwd=str(root), env=environment, check=True, timeout=10)

            actual = json.loads(marker.read_text())
            self.assertEqual(actual["argv"], settings["modelArgs"])
            self.assertEqual(actual["cwd"], str(working))
            self.assertEqual(actual["loginEnv"], "from login profile")
            systemd_argv = json.loads(run_marker.read_text())
            self.assertEqual(systemd_argv[systemd_argv.index("--unit") + 1], unit)
            shell_start = systemd_argv.index("--") + 1
            self.assertEqual(systemd_argv[shell_start:shell_start + 4], ["/bin/bash", "-lc", 'exec "$@"', "peony-cli"])
            self.assertEqual(systemd_argv[shell_start + 4:], [settings["codexExecutable"], *settings["modelArgs"]])

    def test_invalid_save_preserves_existing_settings(self):
        cli_settings.save(cli_settings.defaults())
        before = cli_settings.config_path().read_bytes()
        for bad in ({"widthPercent": 99}, {"enabled": "false"}, {"command": ""},
                    {"fontSize": True}, {"followSystemFont": "yes"}, {"name": "a\0b"},
                    {"contentPadding": 99}, {"fontFamily": "unavailable"}, []):
            with self.assertRaises(ValueError):
                cli_settings.save(bad)
            self.assertEqual(cli_settings.config_path().read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
