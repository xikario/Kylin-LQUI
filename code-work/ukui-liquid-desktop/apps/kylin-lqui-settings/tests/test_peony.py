#!/usr/bin/python3
import json,os,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import peony_adapter as adapter

class TestPeony(unittest.TestCase):
    def test_manager_is_started_only_by_explicit_action(self):
        with patch.object(adapter.subprocess,'Popen') as spawn,patch.object(adapter.scanner,'scan_plugins',return_value=[]):
            page=adapter.page('plugins');self.assertFalse(spawn.called)
            self.assertTrue(any(f['id']=='open-manager' for f in page['fields']))
            adapter.activate('plugins','open-manager')
            self.assertEqual(spawn.call_count,1)
            args,kwargs=spawn.call_args
            self.assertEqual(args[0],[sys.executable,str(adapter.BASE/'app/main.py')])
            self.assertEqual(kwargs['env']['LQUI_SETTINGS_LEGACY'],'1')
            self.assertTrue(kwargs['start_new_session'])
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.before=os.environ.get('XDG_CONFIG_HOME');os.environ['XDG_CONFIG_HOME']=self.temp.name
    def tearDown(self):
        if self.before is None:os.environ.pop('XDG_CONFIG_HOME',None)
        else:os.environ['XDG_CONFIG_HOME']=self.before
        self.temp.cleanup()
    def test_all_cli_controls_round_trip(self):
        before=adapter.cli_settings.read();page=adapter.page('terminal')
        self.assertEqual(len([f for f in page['fields'] if f['id'].startswith('cli:')]),12)
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
    def test_terminal_defaults_preserve_command_and_identity(self):
        data=adapter.cli_settings.read();data.update(command='custom-cli --profile personal',name='我的终端',enabled=False,fontSize=20,widthPercent=60)
        adapter.cli_settings.save(data);adapter.activate('terminal','restore-terminal-defaults')
        after=adapter.cli_settings.read()
        for key in ('command','name','enabled'):self.assertEqual(after[key],data[key])
        for key in ('fontSize','widthPercent'):self.assertEqual(after[key],adapter.cli_settings.defaults()[key])
    def test_file_defaults_never_reset_plugin_selection(self):
        keys={'show-hidden-file':True,'disabled-extensions':['personal.so']}
        with patch.object(adapter.gsettings_store,'get_all',return_value={'settings':keys,'errors':None}),patch.object(adapter.gsettings_store,'reset_boolean_defaults') as reset:
            adapter.activate('general','restore-file-defaults');reset.assert_called_once_with(['show-hidden-file'])
        with self.assertRaises(ValueError):adapter.activate('plugins','restore-file-defaults')
    def test_model_preferences_roundtrip_and_survive_appearance_reset(self):
        adapter.set_value('terminal','cli:model',json.dumps({'value':1}))
        adapter.set_value('terminal','cli:reasoningEffort',json.dumps({'value':6}))
        adapter.activate('terminal','restore-terminal-defaults')
        data=adapter.cli_settings.read();self.assertEqual(data['model'],'gpt-6-luna');self.assertEqual(data['reasoningEffort'],'xhigh')
        adapter.set_value('terminal','cli:model',json.dumps({'value':'custom-model'}));self.assertEqual(adapter.cli_settings.read()['model'],'custom-model')
        for bad in (True,99,'xhigh'):
            with self.assertRaises(ValueError):adapter.set_value('terminal','cli:reasoningEffort',json.dumps({'value':bad}))
if __name__=='__main__':unittest.main()
