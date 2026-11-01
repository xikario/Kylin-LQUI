"""Asynchronous module access. Every setting is still validated by its owner."""
import json
from pathlib import Path
from PyQt5 import QtCore, QtDBus
import sip

from appconfig import ROOT
MODULES = {
    'fences': ('桌面与小组件', 'org.ukui.fences', '◈'),
    'panel': ('任务栏与 Dock', 'org.ukui.AiStationPanel', '▰'),
    'launcher': ('开始菜单', 'org.ukui.kaishicaidan.v2', '▦'),
    'ai': ('AI 小站', 'org.ukui.LiquidCli', '✦'),
    'input': ('输入法', 'org.kde.impanel', '文'),
    'space': ('AI 空间', 'io.github.xikario.AiSpace', '◇'),
    'peony': ('文件管理与扩展', '', '▣'),
}

class Backend(QtCore.QObject):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.pending = set()

    def call(self, module, method, args, callback):
        if module == 'peony':
            self.process(['/usr/bin/python3', str(Path(__file__).with_name('peony_adapter.py')), method] + args, callback)
            return
        message = QtDBus.QDBusMessage.createMethodCall(MODULES[module][1], '/org/kylin/Lqui/Settings', 'org.kylin.Lqui.SettingsModule', method)
        message.setArguments(args)
        message.setAutoStartService(False)
        watcher = QtDBus.QDBusPendingCallWatcher(QtDBus.QDBusConnection.sessionBus().asyncCall(message, 4000), self)
        self.pending.add(watcher)
        completed=[False]
        watchdog=QtCore.QTimer(watcher);watchdog.setSingleShot(True)
        def deliver(result):
            if completed[0]:return
            completed[0]=True;watchdog.stop();self.pending.discard(watcher);watcher.deleteLater();callback(result)
        def done():
            if completed[0]:return
            reply=QtDBus.QDBusPendingReply(watcher).reply()
            if reply.type()==QtDBus.QDBusMessage.ErrorMessage:
                deliver({'ok':False,'offline':True,'error':'模块暂未就绪，请启动模块或重试。'});return
            try:result=json.loads(reply.arguments()[0])
            except (IndexError,ValueError,TypeError):result={'ok':False,'error':'设置内容无法读取，请重试。'}
            deliver(result)
        watcher.finished.connect(done)
        watchdog.timeout.connect(lambda:deliver({'ok':False,'error':'读取超时，请点击刷新重试。'}));watchdog.start(5000)
        if watcher.isFinished():QtCore.QTimer.singleShot(0,done)

    def process(self, argv, callback):
        process = QtCore.QProcess(self)
        self.pending.add(process)
        finished = [False]
        timer = QtCore.QTimer(process)
        timer.setSingleShot(True)
        def done(*unused):
            if finished[0] or sip.isdeleted(process): return
            finished[0] = True
            timer.stop()
            data = bytes(process.readAllStandardOutput())
            self.pending.discard(process)
            process.deleteLater()
            try:
                result = json.loads(data)
            except (ValueError, TypeError):
                result = {'ok': False, 'error': '读取失败，请检查组件是否安装完整。'}
            callback(result)
        process.finished.connect(done)
        process.errorOccurred.connect(lambda error: done() if error == QtCore.QProcess.FailedToStart else None)
        timer.timeout.connect(process.kill)
        process.start(argv[0], argv[1:])
        timer.start(180000 if "Activate" in argv else 10000)
