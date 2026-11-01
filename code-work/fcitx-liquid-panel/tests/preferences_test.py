#!/usr/bin/env python3
"""Explicit Rime preference edits must preserve unrelated config and user data."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import yaml

spec=importlib.util.spec_from_file_location('manager',Path(__file__).resolve().parents[1]/'manage.py')
manager=importlib.util.module_from_spec(spec);spec.loader.exec_module(manager)

class PreferencesTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='rime-preferences-')
        self.root=Path(self.temp.name);self.file=self.root/'fcitx/rime/default.custom.yaml'
        self.file.parent.mkdir(parents=True)
    def tearDown(self):self.temp.cleanup()
    def test_transaction_validates_both_rime_files_before_writing(self):
        from unittest.mock import patch
        self.file.write_text('patch:\n  menu/page_size: 6\n')
        pinyin=self.file.with_name('rime_ice.custom.yaml');pinyin.write_text('patch: [broken')
        before=self.file.read_bytes()
        with self.assertRaises(Exception):manager.apply_settings_transaction({'pageSize':9,'pinyin':True,'appearance':'[General]\nfontSize=19\n'},self.root)
        self.assertEqual(self.file.read_bytes(),before);self.assertFalse((self.root/'fcitx/liquid-panel.conf').exists())
    def test_transaction_rolls_back_earlier_files_on_commit_error(self):
        from unittest.mock import patch
        self.file.write_text('patch:\n  menu/page_size: 6\n')
        pinyin=self.file.with_name('rime_ice.custom.yaml');pinyin.write_text('patch: {}\n')
        original={p:p.read_bytes() for p in (self.file,pinyin)};atomic=manager._atomic_bytes;failed=[]
        def inject(path,value):
            if Path(path)==self.root/'fcitx/liquid-panel.conf' and not failed:
                failed.append(True);raise OSError('injected disk failure')
            return atomic(path,value)
        with patch.object(manager,'_atomic_bytes',side_effect=inject):
            with self.assertRaises(OSError):manager.apply_settings_transaction({'pageSize':9,'pinyin':True,'appearance':'[General]\nfontSize=19\n'},self.root)
        for path,value in original.items():self.assertEqual(path.read_bytes(),value)
        self.assertFalse((self.root/'fcitx/.liquid-settings-transaction.json').exists())
    def test_preserves_other_keys_and_comments(self):
        original='# 我的方案\npatch:\n  menu/page_size: 6 # count\n  schema_list:\n    - schema: rime_ice\n  ascii_composer/good_old_caps_lock: true\n'
        self.file.write_text(original)
        manager.set_rime_page_size(9,self.root)
        self.assertEqual(self.file.read_text(),original.replace('6 # count','9 # count'))
        backups=list((self.file.parent/'liquid-settings-backups').glob('*.yaml'))
        self.assertEqual(len(backups),1);self.assertEqual(backups[0].read_text(),original)
    def test_nested_menu(self):
        self.file.write_text('patch:\n  menu:\n    page_size: 6\n    alternative_select_keys: ABC\n  schema_list: [{schema: rime_ice}]\n')
        manager.set_rime_page_size(4,self.root)
        patch=yaml.safe_load(self.file.read_text())['patch']
        self.assertEqual(patch['menu']['page_size'],4);self.assertEqual(patch['menu/page_size'],4)
        self.assertEqual(patch['menu']['alternative_select_keys'],'ABC')
    def test_no_change_does_not_rewrite(self):
        text='patch:\n  menu/page_size: 6\n';self.file.write_text(text)
        before=self.file.stat().st_mtime_ns;manager.set_rime_page_size(6,self.root)
        self.assertEqual(self.file.stat().st_mtime_ns,before)
    def test_new_config(self):
        manager.set_rime_page_size(6,self.root)
        self.assertEqual(yaml.safe_load(self.file.read_text())['patch']['menu/page_size'],6)
    def test_invalid_input_keeps_original(self):
        for text in ['- not-a-mapping\n','patch: [existing, ordered, patches]\n','patch:\n  menu: invalid\n','patch: [broken']:
            self.file.write_text(text)
            with self.assertRaises((ValueError,yaml.YAMLError)):manager.set_rime_page_size(8,self.root)
            self.assertEqual(self.file.read_text(),text)
    def test_invalid_count(self):
        for count in [0,11,-1,'invalid']:
            with self.assertRaises(ValueError):manager.set_rime_page_size(count,self.root)
            self.assertFalse(self.file.exists())
    def test_user_database_untouched(self):
        db=self.file.parent/'rime_ice.userdb';db.mkdir();p=db/'example';p.write_bytes(b'private-user-data')
        manager.set_rime_page_size(9,self.root);self.assertEqual(p.read_bytes(),b'private-user-data')
    def test_pinyin_preserves_compatibility(self):
        target=self.file.parent/'rime_ice.custom.yaml'
        original={'patch':{'translator/always_show_comments':False,'translator/comment_format':['xform/.+//'],'engine/processors':['ascii_composer','speller'],'engine/filters':['simplifier@emoji','simplifier@traditionalize']}}
        target.write_text(yaml.safe_dump(original))
        self.assertTrue(manager.set_rime_pinyin(True,self.root))
        patch=yaml.safe_load(target.read_text())['patch']
        self.assertTrue(patch['translator/always_show_comments'])
        self.assertEqual(patch['translator/comment_format'],['xform/^/［/','xform/$/］/'])
        self.assertEqual(patch['engine/processors'],original['patch']['engine/processors'])
        self.assertEqual(patch['engine/filters'],original['patch']['engine/filters'])
        before=target.read_bytes();self.assertFalse(manager.set_rime_pinyin(True,self.root));self.assertEqual(target.read_bytes(),before)
        self.assertTrue(manager.set_rime_pinyin(False,self.root))
        self.assertFalse(yaml.safe_load(target.read_text())['patch']['translator/always_show_comments'])
    def test_invalid_pinyin_config_unchanged(self):
        target=self.file.parent/'rime_ice.custom.yaml';text='patch: [ordered, patches]\n';target.write_text(text)
        with self.assertRaises(ValueError):manager.set_rime_pinyin(True,self.root)
        self.assertEqual(target.read_text(),text)

    def test_nested_other_page_size_does_not_replace_the_wrong_key(self):
        self.file.write_text('patch:\n  other:\n    menu/page_size: 6\n')
        manager.set_rime_page_size(9,self.root);data=yaml.safe_load(self.file.read_text())
        self.assertEqual(data['patch']['other']['menu/page_size'],6);self.assertEqual(data['patch']['menu/page_size'],9)
    def test_transaction_keeps_durable_original_backups(self):
        self.file.write_text('patch:\n  menu/page_size: 6\n');before=self.file.read_bytes()
        manager.apply_settings_transaction({'pageSize':9,'pinyin':True,'appearance':'[General]\nfontSize=19\n'},self.root)
        backups=list((self.root/'fcitx/liquid-settings-backups').glob('*/rime/default.custom.yaml'))
        self.assertEqual(len(backups),1);self.assertEqual(backups[0].read_bytes(),before)
    def test_recovery_cannot_acquire_another_process_save_lock(self):
        import fcntl
        lock_path=self.root/'fcitx/.liquid-settings.lock'
        with lock_path.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            with self.assertRaises(BlockingIOError):manager.recover_settings_transaction(self.root)

    def test_startup_config_creation_uses_the_same_save_lock(self):
        import fcntl
        from unittest.mock import patch
        with (self.root/'fcitx/.liquid-settings.lock').open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            with patch.object(manager,'CONFIG_HOME',self.root),self.assertRaises(BlockingIOError):manager.ensure_config()

if __name__=='__main__':unittest.main()
