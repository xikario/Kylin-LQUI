"""End-to-end live refresh against an isolated PulseAudio with virtual devices.

Run with xvfb-run -a dbus-run-session -- /usr/bin/python3 test_audio_hotplug.py.
The private daemon loads no hardware modules; no microphone is recorded.
"""
import json,os,subprocess,sys,tempfile,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtWidgets,QtTest
from system_backend import SystemBackend
from control_center import ControlCenter

def main():
    report={}
    with tempfile.TemporaryDirectory(prefix='lqui-hotplug-') as folder:
        root=Path(folder)
        for name in ('runtime','state','config'):(root/name).mkdir(mode=0o700)
        socket=root/'native';script=root/'test.pa'
        script.write_text('load-module module-native-protocol-unix socket='+str(socket)+' auth-anonymous=1\nload-module module-null-sink sink_name=speakers sink_properties=device.description=Speakers\nload-module module-remap-source master=speakers.monitor source_name=internal_mic\nset-default-sink speakers\nset-default-source internal_mic\n')
        env=dict(os.environ,PULSE_RUNTIME_PATH=str(root/'runtime'),PULSE_STATE_PATH=str(root/'state'),XDG_CONFIG_HOME=str(root/'config'),PULSE_SERVER='unix:'+str(socket))
        os.environ.update({key:env[key] for key in ('PULSE_SERVER','XDG_CONFIG_HOME')})
        server=subprocess.Popen(['pulseaudio','-n','--daemonize=no','--use-pid-file=no','--exit-idle-time=-1','--disable-shm=yes','--log-level=error','--file='+str(script)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        window=None
        try:
            end=time.monotonic()+5
            while not socket.exists() and time.monotonic()<end:
                assert server.poll() is None;time.sleep(.02)
            assert socket.exists()
            def command(*args):return subprocess.check_output(['/usr/bin/pactl',*args],env=dict(env,LC_ALL='C'),text=True).strip()
            # Keep system/network/calendar services out of the isolated test.
            SystemBackend.snapshot=lambda self:{'audio':self.audio()}
            SystemBackend.controls_snapshot=lambda self:{'audio':self.audio(),'brightness':{'available':False}}
            app=QtWidgets.QApplication([]);app.setQuitOnLastWindowClosed(False)
            window=ControlCenter();window.dialog_open=True
            def wait(predicate):
                start=time.monotonic()
                while time.monotonic()-start<4:
                    if predicate():return round((time.monotonic()-start)*1000)
                    QtTest.QTest.qWait(10)
                raise AssertionError('live audio update timed out')
            window.open_at('home',1000,900,44,32)
            wait(lambda:not window.opening_pending and window.audio_events.process.state()==QtCore.QProcess.Running)
            window.navigate('sound');wait(lambda:not window.navigation_active and not window.audio_reading and not window.microphone_reading)
            window.poll.stop() # Every update below must come from subscribe.
            material=window.native_glass;revision=window.material_revision
            def selected(domain):return (window.data.get(domain,{}).get('selected') or {}).get('id')
            headphone=command('load-module','module-null-sink','sink_name=headphones','sink_properties=device.description=Headphones')
            command('set-default-sink','headphones')
            report['outputInsertMs']=wait(lambda:selected('audio')=='headphones' and window.audio_device_lists['output']['buttons'].get('headphones') is not None and window.audio_device_lists['output']['buttons']['headphones'].property('current'))
            microphone=command('load-module','module-remap-source','master=headphones.monitor','source_name=external_mic')
            command('set-default-source','external_mic')
            report['inputInsertMs']=wait(lambda:selected('microphone')=='external_mic' and window.audio_device_lists['input']['buttons'].get('external_mic') is not None and window.audio_device_lists['input']['buttons']['external_mic'].property('current'))
            for ident in ('speakers','headphones','speakers','headphones'):command('set-default-sink',ident)
            wait(lambda:selected('audio')=='headphones');QtTest.QTest.qWait(250)
            assert selected('audio')=='headphones'
            command('set-default-source','internal_mic');command('unload-module',microphone)
            report['inputRemoveMs']=wait(lambda:selected('microphone')=='internal_mic' and 'external_mic' not in window.audio_device_lists['input']['buttons'])
            command('set-default-sink','speakers');command('unload-module',headphone)
            report['outputRemoveMs']=wait(lambda:selected('audio')=='speakers' and 'headphones' not in window.audio_device_lists['output']['buttons'])
            assert window.page=='sound' and window.isVisible() and not window.poll.isActive()
            assert window.native_glass is material and window.material_revision==revision
            wait(lambda:not window.jobs);window.hide();wait(lambda:window.audio_events.process.state()==QtCore.QProcess.NotRunning)
            assert not window.audio_events.retry.isActive()
            commands=[];original=SystemBackend.command
            SystemBackend.command=staticmethod(lambda args:commands.append(args) or original(args))
            command('load-module','module-null-sink','sink_name=while_hidden')
            QtTest.QTest.qWait(350);assert not commands
            SystemBackend.command=staticmethod(original)
            window.open_at('home',1000,900,44,32)
            wait(lambda:not window.opening_pending and window.audio_events.process.state()==QtCore.QProcess.Running)
            window.navigate('sound');wait(lambda:not window.navigation_active and not window.jobs);window.poll.stop()
            assert 'while_hidden' in window.audio_device_lists['output']['buttons']
            window.audio_events.process.kill();wait(lambda:window.audio_events.process.state()==QtCore.QProcess.NotRunning)
            command('set-default-sink','while_hidden')
            report['listenerRecoveryMs']=wait(lambda:selected('audio')=='while_hidden' and window.audio_events.process.state()==QtCore.QProcess.Running)
            report['listenerRecovered']=True
            window.dismiss();window.open_at('home',1000,900,44,32)
            wait(lambda:not window.closing and window.page=='home' and window.audio_events.process.state()==QtCore.QProcess.Running)
            report['closingReversalRestartsListener']=True
            modules=command('list','short','modules');assert 'module-alsa' not in modules and 'module-udev' not in modules
            report.update(verified=True,pollingDisabled=True,sameSoundPage=True,materialPreserved=True,hiddenListenerStopped=True,hiddenReads=0,hardwareModulesLoaded=False)
            print(json.dumps(report))
        finally:
            if window is not None:
                window.hide();window.audio_events.stop();window.audio_events.process.waitForFinished(1000)
                window.material_pool.waitForDone(5000);QtCore.QThreadPool.globalInstance().waitForDone(5000)
                QtTest.QTest.qWait(30);window.deleteLater();QtTest.QTest.qWait(30)
            server.terminate();server.wait(timeout=5)
    return report

if __name__=='__main__':main()
