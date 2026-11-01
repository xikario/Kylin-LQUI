#!/usr/bin/env python3
"""Install one prepared user-local release; keep the previous release recoverable."""
import datetime
import uuid
import json
import os
from pathlib import Path
import sys
sys.path.insert(0,str(next(p for p in Path(__file__).resolve().parents if (p/'tools/file_transaction.py').is_file())/'tools'))
from file_transaction import FileTransaction
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def atomic_link(target, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".new")
    if temporary.is_symlink():
        temporary.unlink()
    os.symlink(str(target), str(temporary))
    os.replace(str(temporary), str(destination))


def install():
    home = Path.home()
    base = home / ".local/lib/liquid-cli"
    base.mkdir(parents=True, exist_ok=True)
    launcher = home / ".local/bin/liquid-cli"
    if os.path.lexists(str(launcher)) and not (launcher.is_symlink() and str(base) in os.readlink(str(launcher))):
        raise RuntimeError("已有同名启动程序，未覆盖：%s" % launcher)
    settings_file=home/'.config/liquid-cli/settings.json'
    if settings_file.exists():
        config=json.loads(settings_file.read_text())
        if not isinstance(config,dict):raise ValueError('设置必须是 JSON 对象')
    with FileTransaction(base) as transaction:
        transaction.watch(base/'current')
        transaction.watch(launcher)
        transaction.watch(home/'.local/share/icons/hicolor/scalable/apps/liquid-cli.svg')
        transaction.watch(home/'.local/share/applications/liquid-cli.desktop')
        transaction.watch(home/'.config/autostart/kylin-lqui-ai-station.desktop')
        stamp = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).strftime("%Y%m%d-%H%M%S")
        release = base / ("release-" + stamp + "-" + uuid.uuid4().hex[:8])
        staging = Path(tempfile.mkdtemp(prefix=".prepare-", dir=str(base)))
        old = os.readlink(str(base / "current")) if (base / "current").is_symlink() else None
        try:
            subprocess.run(["cmake", "--install", str(ROOT / "build"), "--prefix", str(staging)], check=True)
            payload = staging / "lib/liquid-cli"
            os.replace(str(payload), str(release))
            result = subprocess.run(["ldd", str(release / "liquid-cli")], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            if "not found" in result.stdout:
                raise RuntimeError("运行库未完整解析：" + result.stdout)
            atomic_link(release, base / "current")
            atomic_link(base / "current/liquid-cli", launcher)
            icons = home / ".local/share/icons/hicolor/scalable/apps"
            icons.mkdir(parents=True, exist_ok=True)
            shutil.copy2(str(ROOT / "assets/app.svg"), str(icons / "liquid-cli.svg"))
            desktop = home / ".local/share/applications/liquid-cli.desktop"
            desktop.parent.mkdir(parents=True, exist_ok=True)
            desktop.write_text("[Desktop Entry]\nType=Application\nName=AI小站\nComment=AI小站 · Codex / 千问 / DeepSeek\nExec=%s --show\nIcon=%s\nTerminal=false\nCategories=Utility;System;\nStartupWMClass=liquid-cli\nStartupNotify=false\n" % (launcher, icons / "liquid-cli.svg"))
            autostart = home / ".config/autostart/kylin-lqui-ai-station.desktop"
            autostart.parent.mkdir(parents=True, exist_ok=True)
            # Install the requested resident UI; no model backend is launched.
            settings_file = home / ".config/liquid-cli/settings.json"
            resident = json.loads(settings_file.read_text()).get("residentOnLogin", True) if settings_file.exists() else True
            if resident:
                autostart.write_text("[Desktop Entry]\nType=Application\nName=Kylin-LQUI AI小站\nExec=\"%s\" --resident\nIcon=liquid-cli\nTerminal=false\nOnlyShowIn=UKUI;\nX-GNOME-Autostart-enabled=true\nX-GNOME-Autostart-Delay=1\n" % launcher)
            elif autostart.exists():
                autostart.unlink()

            record = {"release": str(release), "previous": old, "launcher": str(launcher), "autostart": str(autostart)}
            (release / "install.json").write_text(json.dumps(record, ensure_ascii=False, indent=2))
            print(json.dumps(record, ensure_ascii=False))
        except Exception:
            if old is not None:
                atomic_link(old, base / "current")
            elif (base / "current").is_symlink():
                (base / "current").unlink()
            raise
        finally:
            shutil.rmtree(str(staging), ignore_errors=True)


if __name__ == "__main__":
    import pwd
    if Path.home().resolve()!=Path(pwd.getpwuid(os.getuid()).pw_dir).resolve():raise SystemExit('HOME 与当前用户主目录不一致，未安装。')
    install()
