#!/usr/bin/python3
import json,os,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import peony_adapter as adapter

class TestPeony(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.before=os.environ.get('XDG_CONFIG_HOME');os.environ['XDG_CONFIG_HOME']=self.temp.name
    def tearDown(self):
        if self.before is None:os.environ.pop('XDG_CONFIG_HOME',None)
        else:os.environ['XDG_CONFIG_HOME']=self.before
        self.temp.cleanup()
    def test_all_cli_controls_round_trip(self):
        before=adapter.cli_settings.read();page=adapter.page('terminal')
        self.assertEqual(len([f for f in page['fields'] if f['id'].startswith('cli:')]),10)
        adapter.set_value('terminal','cli:fontSize',json.dumps({'value':14.0}))
        after=adapter.cli_settings.read();self.assertEqual(after['fontSize'],14);before['fontSize']=14;self.assertEqual(before,after)
        mode=adapter.cli_settings.config_path().stat().st_mode
        self.assertFalse(mode & 0o077)
        for invalid in (8.5,0,100,'12'):
            with self.assertRaises(ValueError):adapter.set_value('terminal','cli:fontSize',json.dumps({'value':invalid}))
        self.assertEqual(adapter.cli_settings.read()['fontSize'],14)
    def test_unknown_setting_does_not_write(self):
        with self.assertRaises(ValueError):adapter.set_value('terminal','cli:unexpected',json.dumps({'value':True}))
        self.assertFalse(adapter.cli_settings.config_path().exists())
    def test_choices_validate(self):
        adapter.set_value('terminal','cli:fontFamily',json.dumps({'value':1}));self.assertEqual(adapter.cli_settings.read()['fontFamily'],'Noto Sans Mono CJK SC')
        with self.assertRaises(ValueError):adapter.set_value('terminal','cli:fontFamily',json.dumps({'value':99}))
if __name__=='__main__':unittest.main()
