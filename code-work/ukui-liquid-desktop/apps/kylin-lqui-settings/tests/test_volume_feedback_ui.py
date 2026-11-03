"""Real slider click/drag to native playback on isolated virtual audio.

Run with xvfb-run -a dbus-run-session -- /usr/bin/python3 test_volume_feedback_ui.py.
The private daemon loads no hardware modules; no microphone is recorded.
"""
import json,os,subprocess,sys,tempfile,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
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
            command('set-sink-volume','speakers','38%')
            # A null sink starts with a long idle clock interval; warm it before
            # measuring UI-triggered playback, rather than daemon cold startup.
            recorder=subprocess.Popen(['parec','--device=speakers.monitor','--latency-msec=20','--process-time-msec=10'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            try:
                window.open_at('home',1000,900,44,32)
                wait(lambda:not window.opening_pending and not window.jobs and not window.state()['motionRunning'])
                QtTest.QTest.qWait(2300)
                results=[]
                for page in ('home','sound'):
                    if page!=window.page:window.navigate(page);wait(lambda:not window.navigation_active and not window.jobs)
                    window.poll.stop();slider=window.volume
                    for gesture in ('click','drag'):
                        feedback=window.volume_feedback;before=feedback.completions;initial=slider.value();started=time.monotonic()
                        if gesture=='click':
                            QtTest.QTest.mouseClick(slider,QtCore.Qt.LeftButton,pos=QtCore.QPoint(slider.width()*3//4,slider.height()//2))
                        else:
                            option=QtWidgets.QStyleOptionSlider();slider.initStyleOption(option)
                            handle=slider.style().subControlRect(QtWidgets.QStyle.CC_Slider,option,QtWidgets.QStyle.SC_SliderHandle,slider).center()
                            target=handle-QtCore.QPoint(35,0)
                            QtTest.QTest.mousePress(slider,QtCore.Qt.LeftButton,pos=handle)
                            QtWidgets.QApplication.sendEvent(slider,QtGui.QMouseEvent(QtCore.QEvent.MouseMove,target,QtCore.Qt.NoButton,QtCore.Qt.LeftButton,QtCore.Qt.NoModifier))
                            QtTest.QTest.mouseRelease(slider,QtCore.Qt.LeftButton,pos=target)
                        wait(lambda:feedback.completions>before)
                        elapsed=round((time.monotonic()-started)*1000)
                        assert slider.value()!=initial and feedback.last_exit==0 and not feedback.last_error
                        assert feedback.completions==before+1 and elapsed<1500
                        assert SystemBackend().audio()['selected']['volume']==slider.value()
                        results.append({'page':page,'gesture':gesture,'before':initial,'after':slider.value(),'completedMs':elapsed})
                        QtTest.QTest.qWait(100)
                report={'verified':True,'gestures':results,'feedback':window.volume_feedback.state(),'hardwareModulesLoaded':False}
                print(json.dumps(report))
            finally:recorder.terminate();recorder.wait(timeout=2)
        finally:
            if window is not None:
                window.hide();window.audio_events.stop();window.audio_events.process.waitForFinished(1000)
                window.material_pool.waitForDone(5000);QtCore.QThreadPool.globalInstance().waitForDone(5000)
                QtTest.QTest.qWait(30);window.deleteLater();QtTest.QTest.qWait(30)
            server.terminate();server.wait(timeout=5)
    return report

if __name__=='__main__':main()
