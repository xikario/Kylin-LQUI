"""Short, low-latency volume preview with one owned playback process."""
from pathlib import Path

def playback_command(device):
    from gi.repository import Gio
    paths=[];schemas=Gio.SettingsSchemaSource.get_default()
    schema=schemas.lookup('org.ukui.sound',True) if schemas else None
    if schema and schema.has_key('audio-volume-change'):
        paths.append(Gio.Settings.new_full(schema,None,None).get_string('audio-volume-change'))
    paths+=['/usr/share/sounds/freedesktop/stereo/audio-volume-change.oga','/usr/share/sounds/Light-Seeking/stereo/audio-volume-change.ogg']
    sound=next((str(Path(p).resolve()) for p in paths if p and Path(p).is_file()),None)
    if not sound or not Path('/usr/bin/paplay').is_file():return []
    # Own the native player directly, with explicit short buffering and no
    # Python/Gio child initialization on every click. Leave stream gain unset
    # to inherit the sink's actual volume instead of raising the device.
    return ['/usr/bin/paplay','--device='+device,'--latency-msec=30','--process-time-msec=10',
            '--client-name=Kylin-LQUI Volume Feedback','--stream-name=Volume preview',
            '--property=media.role=event','--property=application.id=org.kylin.Lqui.VolumeFeedback','--',sound]

if __name__=='__main__':
    import os,sys
    command=playback_command(sys.argv[2]) if len(sys.argv)==3 and sys.argv[1]=='--play' else []
    if command:os.execv(command[0],command)
    sys.exit(1)

from PyQt5 import QtCore

class VolumeFeedback(QtCore.QObject):
    def __init__(self,parent=None):
        super().__init__(parent);self.target=''
        self.requests=0;self.starts=0;self.completions=0;self.last_exit=None;self.last_error=''
        self.timer=QtCore.QTimer(self);self.timer.setSingleShot(True);self.timer.setInterval(60);self.timer.timeout.connect(self.launch)
        self.process=QtCore.QProcess(self);self.process.setStandardInputFile(QtCore.QProcess.nullDevice());self.process.setStandardOutputFile(QtCore.QProcess.nullDevice())
        self.process.started.connect(self.started);self.process.finished.connect(self.finished)
        self.process.errorOccurred.connect(self.failed);self.process.readyReadStandardError.connect(self.read_error)
        self.limit=QtCore.QTimer(self);self.limit.setSingleShot(True);self.limit.setInterval(3000);self.limit.timeout.connect(self.stop)
    def request(self,selected):
        self.stop()
        if not selected or selected.get('muted') or selected.get('volume',0)<=0:return
        self.target=selected.get('id','')
        if self.target:self.requests+=1;self.timer.start()
    def launch(self):
        if not self.target or self.process.state()!=QtCore.QProcess.NotRunning:return
        target=self.target;self.target='';command=playback_command(target)
        if not command:self.last_error='Volume sound or player unavailable';return
        self.last_error='';self.last_exit=None
        self.process.start(command[0],command[1:]);self.limit.start()
    def started(self):self.starts+=1
    def failed(self,error):
        if error==QtCore.QProcess.FailedToStart:self.last_error=self.process.errorString();self.limit.stop()
    def read_error(self):self.last_error=(self.last_error+bytes(self.process.readAllStandardError()).decode('utf-8','replace'))[-512:]
    def finished(self,code,status):
        self.limit.stop();self.last_exit=code
        if status==QtCore.QProcess.NormalExit and code==0:self.completions+=1
        if self.target and not self.timer.isActive():self.launch()
    def stop(self):
        self.target='';self.timer.stop();self.limit.stop()
        if self.process.state()!=QtCore.QProcess.NotRunning:self.process.kill()
    def state(self):
        return {'requests':self.requests,'starts':self.starts,'completions':self.completions,'pending':bool(self.target),
                'running':self.process.state()!=QtCore.QProcess.NotRunning,'lastExit':self.last_exit,'lastError':self.last_error}
