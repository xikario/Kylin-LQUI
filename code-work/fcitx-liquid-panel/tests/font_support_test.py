#!/usr/bin/python3
"""Installer failure paths must never remove the working font configuration."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import font_support as fonts


class FontInstallation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        env = {key: str(self.root / key) for key in ('XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_STATE_HOME')}
        self.patch = patch.dict(os.environ, env)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.target = Path(env['XDG_DATA_HOME']) / 'fonts/noto-emoji-rime/NotoColorEmoji.ttf'
        self.rules = Path(env['XDG_CONFIG_HOME']) / 'fontconfig/conf.d'
        self.old = self.root / 'old/NotoColorEmoji.ttf'
        self.old.parent.mkdir()
        self.old.write_bytes(b'old font stays untouched')
        self.font = self.root / 'input.ttf'
        self.data = b'\x00\x01\x00\x00test-verified-font'
        self.font.write_bytes(self.data)
        self.spec = fonts.manifest()
        self.spec.update(bytes=len(self.data), sha256=hashlib.sha256(self.data).hexdigest())
        self.spec_patch = patch.object(fonts, 'manifest', return_value=self.spec)
        self.spec_patch.start()
        self.addCleanup(self.spec_patch.stop)
        self.calls = []

    def command(self, *args):
        self.calls.append(args)
        if args[0] == 'fc-list':
            return 'SimSun-ExtG' if ':charset=31342' in args else str(self.old)
        if args[0] == 'fc-query':
            return '1f600' if args[-1] == str(self.old) else '1fae0-1fae5'
        return ''

    def test_bad_font_is_rejected_before_any_publication(self):
        self.font.write_bytes(self.data + b'corrupted')
        with patch.object(fonts, 'command', self.command), self.assertRaises(ValueError):
            fonts.install(self.font, 'Test Font', None)
        self.assertFalse(self.target.exists())
        self.assertFalse(self.rules.exists())
        self.assertFalse(self.calls)

    def test_custom_rule_is_preserved_and_font_is_not_published(self):
        self.rules.mkdir(parents=True)
        path = self.rules / '71-rime-cjk-extensions.conf'
        original = fonts.cjk_rule('User Font', 'Custom Fallback')
        path.write_text(original)
        with patch.object(fonts, 'command', self.command), self.assertRaises(RuntimeError):
            fonts.install(self.font, 'Test Font', None)
        self.assertEqual(path.read_text(), original)
        self.assertFalse(self.target.exists())

    def test_cache_failure_restores_existing_license_and_rules(self):
        self.target.parent.mkdir(parents=True)
        license_path = self.target.parent / 'LICENSE'
        license_path.write_bytes(b'original license')
        self.rules.mkdir(parents=True)
        rule = self.rules / '70-noto-color-emoji-local.conf'
        original = fonts.emoji_rule([self.old]).replace('<selectfont>', '<!-- user comment --><selectfont>')
        rule.write_text(original)
        def fail_cache(*args):
            if args[0] == 'fc-cache':
                raise subprocess.CalledProcessError(1, args)
            return self.command(*args)
        with patch.object(fonts, 'command', fail_cache), self.assertRaises(subprocess.CalledProcessError):
            fonts.install(self.font, 'Test Font', None)
        self.assertEqual(license_path.read_bytes(), b'original license')
        self.assertEqual(rule.read_text(), original)
        self.assertFalse(self.target.exists())
        self.assertFalse((self.rules / '71-rime-cjk-extensions.conf').exists())
        self.assertFalse((self.target.parent / 'kylin-lqui-source.json').exists())

    def test_offline_install_is_repeatable_and_does_not_touch_old_font(self):
        with patch.object(fonts, 'command', self.command):
            first = fonts.install(self.font, 'Test Font', None)
            second = fonts.install(self.font, 'Test Font', None)
        self.assertGreater(first['changedFiles'], 0)
        self.assertEqual(second['changedFiles'], 0)
        self.assertEqual(sum(args[0] == 'fc-cache' for args in self.calls), 1)
        self.assertEqual(self.target.read_bytes(), self.data)
        self.assertEqual(self.old.read_bytes(), b'old font stays untouched')
        spec = json.loads((self.target.parent / 'kylin-lqui-source.json').read_text())
        self.assertEqual(spec['sha256'], self.spec['sha256'])

    def test_xml_escapes_family_names_and_accepts_equivalent_old_rule(self):
        text = fonts.cjk_rule('A&B', 'C<D')
        self.assertIn('A&amp;B', text)
        path = self.root / 'rule.conf'
        path.write_text(text.replace('<match ', '\n <!-- existing note -->\n <match '))
        fonts.guard_rule(path, text)
        with self.assertRaises(RuntimeError):
            fonts.guard_rule(path, fonts.cjk_rule('Other', 'C<D'))


if __name__ == '__main__':
    unittest.main()
