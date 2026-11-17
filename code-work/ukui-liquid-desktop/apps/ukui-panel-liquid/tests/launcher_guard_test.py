import base64,hashlib,importlib.machinery,importlib.util,json,os,signal,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
path=Path(__file__).resolve().parents[1]/'scripts/ukui-panel-liquid'
loader=importlib.machinery.SourceFileLoader('launcher_guard',str(path));spec=importlib.util.spec_from_loader(loader.name,loader);guard=importlib.util.module_from_spec(spec);loader.exec_module(guard)
REAL_X_ALIVE=getattr(guard,'x_alive',None)

class GuardTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.panel=self.root/'panel';self.panel.write_text('#!/usr/bin/python3\nimport sys\nsys.exit(42)\n');self.panel.chmod(0o755)
        plugin=self.root/'.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so';plugin.parent.mkdir(parents=True);plugin.touch()
        self.bridge=plugin.parents[2]/'liblqui-calendar-geometry-bridge.so';self.bridge.write_bytes(b'fixture bridge');guard.CALENDAR_BRIDGE_SHA256=guard.digest(self.bridge)
        guard.PANEL=self.panel;guard.PLUGIN_SHA256=guard.digest(plugin)
        self.guard_lib=plugin.parents[2]/'liblqui-panel-dbus-guard.so';self.guard_lib.write_bytes(b'fixture guard');guard.DBUS_GUARD_SHA256=guard.digest(self.guard_lib)
        guard.HOST_MANIFEST_B64=base64.b64encode(json.dumps({str(self.panel):guard.digest(self.panel)}).encode()).decode()
        for context in [patch.object(guard.Path,'home',return_value=self.root),patch.dict(os.environ,XDG_RUNTIME_DIR=str(self.root)),patch.object(guard.sys,'argv',['fixture'])]:context.start();self.addCleanup(context.stop)
        real_spawn=guard.subprocess.Popen
        def spawn(*args,**kwargs):
            self.child_env=kwargs.get('env',{}).copy();kwargs['env']=self.child_env.copy();kwargs['env'].pop('LD_PRELOAD',None)
            return real_spawn(*args,**kwargs)
        context=patch.object(guard.subprocess,'Popen',side_effect=spawn);context.start();self.addCleanup(context.stop)
        for sig in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP,signal.SIGQUIT):self.addCleanup(signal.signal,sig,signal.getsignal(sig))
        for context in [patch.object(guard,'x_alive',return_value=True),patch.object(guard,'EXTERNAL_STOP_GRACE',0)]:context.start();self.addCleanup(context.stop)
    def test_host_change_is_detected(self):
        self.assertTrue(guard.compatible());self.panel.write_text('changed');self.assertFalse(guard.compatible())
    def test_crash_falls_back_and_breaks_restart_loop(self):
        with patch.object(guard.os,'execve',side_effect=SystemExit) as native:
            with self.assertRaises(SystemExit):guard.main()
            self.assertEqual(native.call_args[0][1],[str(self.panel)])
        self.assertEqual(native.call_args[0][2]['LD_PRELOAD'],str(self.guard_lib))
        self.assertTrue((self.root/'.local/state/ukui-panel-liquid/launcher-events.jsonl').is_file())
        self.assertTrue((self.root/'kylin-lqui-panel-failed.json').is_file())
        with patch.object(guard.subprocess,'Popen') as spawn,patch.object(guard.os,'execve',side_effect=SystemExit):
            with self.assertRaises(SystemExit):guard.main()
            spawn.assert_not_called()
    def test_modified_plugin_never_enters_liquid_mode(self):
        plugin=self.root/'.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so'
        plugin.write_bytes(b'changed')
        with patch.object(guard.subprocess,'Popen') as spawn,patch.object(guard.os,'execve',side_effect=SystemExit) as native:
            with self.assertRaises(SystemExit):guard.main()
            spawn.assert_not_called()
            self.assertEqual(native.call_args[0][2]['LD_PRELOAD'],str(self.guard_lib))
        log=self.root/'.local/state/ukui-panel-liquid/launcher-events.jsonl'
        self.assertIn('plugin-mismatch',log.read_text())

    def test_invalid_guard_is_not_preloaded_into_native_fallback(self):
        self.guard_lib.write_bytes(b'changed')
        with patch.object(guard.subprocess,'Popen') as spawn,patch.object(guard.os,'execve',side_effect=SystemExit) as native:
            with self.assertRaises(SystemExit):guard.main()
            spawn.assert_not_called()
            self.assertNotIn(str(self.guard_lib),native.call_args[0][2].get('LD_PRELOAD',''))

    def test_original_fallback_drops_inherited_private_preloads(self):
        with patch.dict(os.environ,LD_PRELOAD=str(self.bridge)+':'+str(self.guard_lib)),patch.object(guard.os,'execve',side_effect=SystemExit) as native:
            with self.assertRaises(SystemExit):guard.native_fallback([], 'explicit-original')
            self.assertNotIn('LD_PRELOAD',native.call_args[0][2])

    def kill_self_panel(self,sig='SIGTERM'):
        self.panel.write_text('#!/usr/bin/python3\nimport os,signal\nos.kill(os.getpid(),signal.'+sig+')\n')
        guard.HOST_MANIFEST_B64=base64.b64encode(json.dumps({str(self.panel):guard.digest(self.panel)}).encode()).decode()
    def events(self):
        log=self.root/'.local/state/ukui-panel-liquid/launcher-events.jsonl'
        return [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []

    def test_clean_shutdown_does_not_quarantine(self):
        # Someone else (session manager) already restarted a panel: leave it alone.
        self.kill_self_panel()
        with patch.object(guard,'panel_running',return_value=True),patch.object(guard.os,'execve',side_effect=SystemExit) as native:
            self.assertEqual(guard.main(),0);native.assert_not_called()
        self.assertFalse((self.root/'kylin-lqui-panel-failed.json').exists())
        self.assertEqual(self.child_env['LD_PRELOAD'],str(self.guard_lib)+':'+str(self.bridge))

    def test_external_stop_without_replacement_restores_a_panel(self):
        # The liquid panel was ended by a signal that did not come through this
        # launcher and nothing replaced it: never leave the desktop without a taskbar.
        self.kill_self_panel()
        with patch.object(guard,'panel_running',return_value=False),patch.object(guard.os,'execve',side_effect=SystemExit) as native:
            with self.assertRaises(SystemExit):guard.main()
            native.assert_called_once()
        self.assertFalse((self.root/'kylin-lqui-panel-failed.json').exists())
        self.assertIn('external-stop-no-panel',[e.get('reason') for e in self.events()])

    def test_session_teardown_is_not_a_crash(self):
        # X is gone (logout/shutdown): the child exits 1 and there is nothing to fall back to.
        self.panel.write_text('#!/usr/bin/python3\nimport sys\nsys.exit(1)\n')
        guard.HOST_MANIFEST_B64=base64.b64encode(json.dumps({str(self.panel):guard.digest(self.panel)}).encode()).decode()
        with patch.object(guard,'x_alive',return_value=False),patch.object(guard.os,'execve',side_effect=SystemExit) as native:
            self.assertEqual(guard.main(),0);native.assert_not_called()
        self.assertFalse((self.root/'kylin-lqui-panel-failed.json').exists())
        self.assertIn('session-ended',[e['event'] for e in self.events()])

    def test_crash_records_signal_name(self):
        self.kill_self_panel('SIGSEGV')
        with patch.object(guard.os,'execve',side_effect=SystemExit):
            with self.assertRaises(SystemExit):guard.main()
        exits=[e for e in self.events() if e['event']=='liquid-exit']
        self.assertEqual(exits[-1]['signal'],'SIGSEGV')

    def test_stale_failure_marker_does_not_disable_liquid_forever(self):
        import time
        marker=self.root/'kylin-lqui-panel-failed.json'
        plugin_hash=guard.PLUGIN_SHA256
        self.panel.write_text('#!/usr/bin/python3\n');guard.HOST_MANIFEST_B64=base64.b64encode(json.dumps({str(self.panel):guard.digest(self.panel)}).encode()).decode()
        marker.write_text(json.dumps({'plugin':plugin_hash,'exitCode':1,'time':time.time()-7200}))
        with patch.object(guard,'panel_running',return_value=True):self.assertEqual(guard.main(),0)
        self.assertIn('LD_PRELOAD',self.child_env)
        del self.child_env
        marker.write_text(json.dumps({'plugin':plugin_hash,'exitCode':1,'time':time.time()}))
        with patch.object(guard.os,'execve',side_effect=SystemExit):
            with self.assertRaises(SystemExit):guard.main()
        self.assertFalse(hasattr(self,'child_env'))

    def test_x_alive_checks_the_display_socket(self):
        import socket
        with patch.dict(os.environ,DISPLAY=':987654'):self.assertFalse(REAL_X_ALIVE())
        with patch.dict(os.environ):
            os.environ.pop('DISPLAY',None);self.assertTrue(REAL_X_ALIVE())  # unknown: keep old behaviour
        folder=self.root/'x11';folder.mkdir();path=folder/'X77'
        server=socket.socket(socket.AF_UNIX);server.bind(str(path));server.listen(1);self.addCleanup(server.close)
        with patch.dict(os.environ,DISPLAY=':77.0'),patch.object(guard,'X11_SOCKET_DIR',str(folder)):self.assertTrue(REAL_X_ALIVE())

    def test_missing_or_changed_bridge_preserves_original_panel(self):
        for value in (None,b'changed'):
            if value is None:self.bridge.unlink()
            else:self.bridge.write_bytes(value)
            with patch.object(guard.subprocess,'Popen') as spawn,patch.object(guard.os,'execve',side_effect=SystemExit):
                with self.assertRaises(SystemExit):guard.main()
                spawn.assert_not_called()

if __name__=='__main__':unittest.main()
