"""Peony Manager — 桌面 GUI 主入口.

PyQt5 + QWebEngineView 内嵌加载本地 HTML，通过 QtWebChannel
连接 Python 后端。不启动 HTTP 服务，不暴露 localhost 入口。
"""

import os
import sys

# Redirect before importing WebEngine: the central application is lightweight.
if __name__ == "__main__" and not os.environ.get("LQUI_SETTINGS_LEGACY"):
    launcher = os.path.expanduser("~/.local/bin/kylin-lqui-settings")
    if os.path.isfile(launcher) and os.access(launcher, os.X_OK):
        os.execv(launcher, [launcher, "--page", "peony:general"])

from PyQt5.QtCore import QUrl, QTimer, QObject, pyqtSlot, Q_CLASSINFO
from PyQt5.QtDBus import QDBusConnection,QDBusInterface
from PyQt5.QtGui import QIcon
from PyQt5.QtWebEngineWidgets import QWebEngineView, QWebEnginePage
from PyQt5.QtWebChannel import QWebChannel
from PyQt5.QtWidgets import QApplication, QMainWindow, QVBoxLayout, QWidget

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if BASE_DIR not in sys.path:
    sys.path.insert(0, BASE_DIR)

from app.bridge import PeonyBridge

APP_NAME = "Peony Manager"
WINDOW_WIDTH = 1380
WINDOW_HEIGHT = 860
APP_ICON_PATH = os.path.join(BASE_DIR, "assets", "peony-manager.png")


class ManagerPage(QWebEnginePage):

    def javaScriptConsoleMessage(self, level, msg, line, source):
        print(f"[JS] {msg}", file=sys.stderr, flush=True)


class MainWindow(QMainWindow):

    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"{APP_NAME} — 麒麟文件管理器插件管理")
        self.setMinimumSize(1024, 680)

        screen = QApplication.primaryScreen().availableGeometry()
        width = min(WINDOW_WIDTH, max(1024, int(screen.width() * 0.86)))
        height = min(WINDOW_HEIGHT, max(680, int(screen.height() * 0.86)))
        self.resize(width, height)
        x = screen.x() + (screen.width() - width) // 2
        y = screen.y() + (screen.height() - height) // 2
        self.move(x, y)

        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)

        self.web_view = QWebEngineView()
        layout.addWidget(self.web_view)

        page = ManagerPage(self.web_view)
        self.channel = QWebChannel(page)
        self.bridge = PeonyBridge()
        self.channel.registerObject("peonyBridge", self.bridge)
        page.setWebChannel(self.channel)
        self.web_view.setPage(page)

        QTimer.singleShot(0, self._load_page)

    def _load_page(self):
        html_path = os.path.join(BASE_DIR, "peony_manager.html")
        self.web_view.load(QUrl.fromLocalFile(html_path))


class ManagerControl(QObject):
    Q_CLASSINFO('D-Bus Interface','org.kylin.Lqui.PeonyManager')
    @pyqtSlot()
    def Close(self):self.parent().close()
    @pyqtSlot()
    def Show(self):
        window=self.parent();window.showNormal();window.raise_();window.activateWindow()

def main():
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    app.setDesktopFileName("peony-manager")
    bus=QDBusConnection.sessionBus()
    if not bus.registerService('org.kylin.Lqui.PeonyManager'):
        if bus.interface() and bus.interface().isServiceRegistered('org.kylin.Lqui.PeonyManager').value():
            QDBusInterface('org.kylin.Lqui.PeonyManager','/Manager','org.kylin.Lqui.PeonyManager',bus).call('Show')
            return 0
    try:
        app_icon = QIcon(APP_ICON_PATH)
        if app_icon.isNull():
            app_icon = QIcon.fromTheme("peony")
        if not app_icon.isNull():
            app.setWindowIcon(app_icon)
    except Exception:
        pass

    window = MainWindow()
    control=ManagerControl(window)
    bus.registerObject('/Manager',control,QDBusConnection.ExportAllSlots)
    if not app.windowIcon().isNull():
        window.setWindowIcon(app.windowIcon())
    window.show()

    print("Peony Manager GUI running", flush=True)

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
