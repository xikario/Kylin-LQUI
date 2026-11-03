"""Visible-only PulseAudio notifications; no recording or audio mutations."""
import re
from PyQt5 import QtCore

class AudioEvents(QtCore.QObject):
    changed=QtCore.pyqtSignal()
    def __init__(self,parent=None):
        super().__init__(parent);self.wanted=False;self.buffer=b''
        self.process=QtCore.QProcess(self)
        env=QtCore.QProcessEnvironment.systemEnvironment();env.insert('LC_ALL','C');self.process.setProcessEnvironment(env)
        self.process.setStandardInputFile(QtCore.QProcess.nullDevice())
        self.process.setStandardErrorFile(QtCore.QProcess.nullDevice())
        self.process.readyReadStandardOutput.connect(self.read_events)
        self.process.started.connect(self.changed)
        self.process.finished.connect(self.exited)
        self.process.errorOccurred.connect(self.failed)
        self.coalesce=QtCore.QTimer(self);self.coalesce.setSingleShot(True);self.coalesce.setInterval(90)
        self.coalesce.timeout.connect(self.notify)
        self.retry=QtCore.QTimer(self);self.retry.setSingleShot(True);self.retry.setInterval(1500);self.retry.timeout.connect(self.launch)
    def start(self):
        if self.wanted:return
        self.wanted=True;self.launch()
    def launch(self):
        if self.wanted and self.process.state()==QtCore.QProcess.NotRunning:
            self.buffer=b'';self.process.start('/usr/bin/pactl',['subscribe'])
    def stop(self):
        self.wanted=False;self.coalesce.stop();self.retry.stop();self.buffer=b''
        if self.process.state()!=QtCore.QProcess.NotRunning:self.process.kill()
    def exited(self,*args):
        if self.wanted and not self.retry.isActive():self.retry.start()
    def failed(self,error):
        if error==QtCore.QProcess.FailedToStart:self.exited()
    def read_events(self):self.feed(bytes(self.process.readAllStandardOutput()))
    def feed(self,data):
        if not self.wanted:return
        self.buffer+=data
        lines=self.buffer.split(b'\n');self.buffer=lines.pop()[-4096:]
        relevant=any(re.match(rb"^Event '(?:new|change|remove)' on (?:sink|source|card|server) #\d+\r?$",line) for line in lines)
        # Don't postpone indefinitely while volume events keep arriving.
        if relevant and not self.coalesce.isActive():self.coalesce.start()
    def notify(self):
        if self.wanted:self.changed.emit()
