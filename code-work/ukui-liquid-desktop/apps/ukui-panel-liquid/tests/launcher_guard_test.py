import base64,hashlib,importlib.machinery,importlib.util,json,os,signal,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
path=Path(__file__).resolve().parents[1]/'scripts/ukui-panel-liquid'
loader=importlib.machinery.SourceFileLoader('launcher_guard',str(path));spec=importlib.util.spec_from_loader(loader.name,loader);guard=importlib.util.module_from_spec(spec);loader.exec_module(guard)

class GuardTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.panel=self.root/'panel';self.panel.write_text('#!/usr/bin/python3\nimport sys\nsys.exit(42)\n');self.panel.chmod(0o755)
        plugin=self.root/'.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so';plugin.parent.mkdir(parents=True);plugin.touch()
        self.bridge=plugin.parents[2]/'liblqui-calendar-geometry-bridge.so';self.bridge.write_bytes(b'fixture bridge');guard.CALENDAR_BRIDGE_SHA256=guard.digest(self.bridge)
        guard.PANEL=self.panel;guard.PLUGIN_SHA256='fixture'
        guard.HOST_MANIFEST_B64=base64.b64encode(json.dumps({str(self.panel):guard.digest(self.panel)}).encode()).decode()
        for context in [patch.object(guard.Path,'home',return_value=self.root),patch.dict(os.environ,XDG_RUNTIME_DIR=str(self.root)),patch.object(guard.sys,'argv',['fixture'])]:context.start();self.addCleanup(context.stop)
        real_spawn=guard.subprocess.Popen
        def spawn(*args,**kwargs):
            self.child_env=kwargs.get('env',{}).copy();kwargs['env']=self.child_env.copy();kwargs['env'].pop('LD_PRELOAD',None)
            return real_spawn(*args,**kwargs)
        context=patch.object(guard.subprocess,'Popen',side_effect=spawn);context.start();self.addCleanup(context.stop)
        for sig in (signal.SIGTERM,signal.SIGINT):self.addCleanup(signal.signal,sig,signal.getsignal(sig))
    def test_host_change_is_detected(self):
        self.assertTrue(guard.compatible());self.panel.write_text('changed');self.assertFalse(guard.compatible())
    def test_crash_falls_back_and_breaks_restart_loop(self):
        with patch.object(guard.os,'execv',side_effect=SystemExit) as native:
            with self.assertRaises(SystemExit):guard.main()
            self.assertEqual(native.call_args[0][1],[str(self.panel)])
        self.assertTrue((self.root/'kylin-lqui-panel-failed.json').is_file())
        with patch.object(guard.subprocess,'Popen') as spawn,patch.object(guard.os,'execv',side_effect=SystemExit):
            with self.assertRaises(SystemExit):guard.main()
            spawn.assert_not_called()
    def test_clean_shutdown_does_not_quarantine(self):
        self.panel.write_text('#!/usr/bin/python3\nimport os,signal\nos.kill(os.getpid(),signal.SIGTERM)\n')
        guard.HOST_MANIFEST_B64=base64.b64encode(json.dumps({str(self.panel):guard.digest(self.panel)}).encode()).decode()
        self.assertEqual(guard.main(),0);self.assertFalse((self.root/'kylin-lqui-panel-failed.json').exists())
        self.assertEqual(self.child_env['LD_PRELOAD'],str(self.bridge))
    def test_missing_or_changed_bridge_preserves_original_panel(self):
        for value in (None,b'changed'):
            if value is None:self.bridge.unlink()
            else:self.bridge.write_bytes(value)
            with patch.object(guard.subprocess,'Popen') as spawn,patch.object(guard.os,'execv',side_effect=SystemExit):
                with self.assertRaises(SystemExit):guard.main()
                spawn.assert_not_called()

if __name__=='__main__':unittest.main()
