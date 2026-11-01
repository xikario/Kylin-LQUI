"""Run with xvfb-run -a /usr/bin/python3 tests/test_cli_manager_gui.py."""
import json
import os
from pathlib import Path
import sys
import tempfile

os.environ.setdefault("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu")
os.environ["QT_QUICK_BACKEND"] = "software"
os.environ["QT_STYLE_OVERRIDE"] = "Fusion"
os.environ["QT_QPA_PLATFORMTHEME"] = ""
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from PyQt5.QtCore import QTimer
from PyQt5.QtWidgets import QApplication
from PyQt5.QtWebEngineCore import QWebEngineUrlRequestInterceptor
import sip
from app.main import MainWindow

class OfflineRequests(QWebEngineUrlRequestInterceptor):
    def __init__(self,parent):super().__init__(parent);self.remote=[]
    def interceptRequest(self,info):
        if info.requestUrl().scheme() in ('http','https'):
            self.remote.append(info.requestUrl().toString());info.block(True)


def main():
    directory = tempfile.TemporaryDirectory(prefix="peony-cli-gui-")
    os.environ["XDG_CONFIG_HOME"] = directory.name
    app = QApplication(sys.argv)
    window = MainWindow()
    offline=OfflineRequests(app);window.web_view.page().profile().setRequestInterceptor(offline)
    window.show()
    done = {"stage": 0, "ticks": 0}
    config = Path(directory.name) / "peony-manager/cli-profiles.json"
    timer = QTimer()

    def callback(ready):
        if ready and done["stage"] == 0:
            done["stage"] = 1
            window.web_view.page().runJavaScript('''
                document.getElementById('cli-name').value = '研究 CLI';
                document.getElementById('cli-menu').value = '在此打开研究工具';
                document.getElementById('cli-command').value = '/bin/echo "中文$HOME"';
                document.getElementById('cli-width').value = 45;
                document.getElementById('cli-enabled').checked = false;
                document.getElementById('cli-system-font').checked = false;
                document.getElementById('cli-font-family').value = 'Noto Sans Mono CJK SC';
                document.getElementById('cli-padding').value = 12;
                saveCliPanelSettings();
            ''')

    def check():
        done["ticks"] += 1
        if done["ticks"] > 100:
            print("FAIL: manager GUI timed out", flush=True)
            app.exit(1)
        elif done["stage"] == 0:
            window.web_view.page().runJavaScript("!!document.getElementById('cli-command')", callback)
        elif config.exists():
            data = json.loads(config.read_text())
            assert data["name"] == "研究 CLI"
            assert data["menuLabel"] == "在此打开研究工具"
            assert data["command"] == '/bin/echo "中文$HOME"'
            assert data["widthPercent"] == 45
            assert data["enabled"] is False
            assert data["defaultVisible"] is False
            assert data["followSystemFont"] is False
            assert data["fontFamily"] == 'Noto Sans Mono CJK SC'
            assert data["contentPadding"] == 12
            print("PASS: real manager form saves through QtWebChannel to JSON", flush=True)
            app.exit(0)

    timer.timeout.connect(check)
    timer.start(500)
    result = app.exec_()
    assert not offline.remote, 'manager UI must load without remote styles or fonts'
    if os.environ.get('PEONY_GUI_ARTIFACT'):
        window.grab().save(os.environ['PEONY_GUI_ARTIFACT'])
    sip.delete(window)
    app.processEvents()
    directory.cleanup()
    return result


if __name__ == "__main__":
    sys.exit(main())
