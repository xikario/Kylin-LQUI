import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from app import cli_settings


class CliSettingsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.env = patch.dict(os.environ, {"XDG_CONFIG_HOME": self.directory.name})
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self.directory.cleanup()

    def test_default_is_lazy_and_window_bound(self):
        value = cli_settings.read()
        self.assertFalse(value["defaultVisible"])
        self.assertTrue(value["closeWithWindow"])
        self.assertTrue(value["followSystemFont"])
        self.assertEqual(value["fontFamily"], "Sarasa Fixed SC")
        self.assertEqual(value["contentPadding"], 10)
        self.assertFalse(cli_settings.config_path().exists())

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
