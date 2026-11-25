#!/usr/bin/env python3
"""Register this checkout's pet launcher and artwork for the current user."""
import os
import shlex
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
ELECTRON = ROOT / 'node_modules/electron/dist/electron'
LAUNCHER = Path.home() / '.local/bin/kylin-ai-pet'
DESKTOP = DATA / 'applications/kylin-ai-pet.desktop'
ASSETS = DATA / 'kylin-ai-pets'


def main():
    if not os.access(ELECTRON, os.X_OK):
        raise SystemExit('请先在此组件目录运行 npm ci。')
    if ASSETS.exists() and not ASSETS.is_symlink():
        raise SystemExit(f'已有同名文件或目录，请先移开：{ASSETS}')
    LAUNCHER.parent.mkdir(parents=True, exist_ok=True)
    DESKTOP.parent.mkdir(parents=True, exist_ok=True)
    # The desktop entry uses a command name. Paths to the checkout can therefore
    # contain spaces, quotes and literal percent signs without becoming field codes.
    command = f'exec nice -n 5 {shlex.quote(str(ELECTRON))} {shlex.quote(str(ROOT))} "$@"\n'
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=LAUNCHER.parent, delete=False) as f:
        f.write('#!/bin/sh\n' + command)
        os.fchmod(f.fileno(), 0o755)
    os.replace(f.name, LAUNCHER)
    icon = str(ASSETS / 'claude/avatar.png').replace('\\', '\\\\')
    content = '[Desktop Entry]\nType=Application\nName=AI 宠物\nExec=kylin-ai-pet --show\n'
    content += f'Icon={icon}\nTerminal=false\nCategories=Utility;\n'
    with tempfile.TemporaryDirectory(prefix='.kylin-ai-pet-', dir=DATA) as tmp:
        link = Path(tmp) / 'assets'
        link.symlink_to(ROOT / 'assets/pets', target_is_directory=True)
        item = Path(tmp) / 'kylin-ai-pet.desktop'
        item.write_text(content, encoding='utf-8')
        item.chmod(0o755)
        os.replace(link, ASSETS)
        os.replace(item, DESKTOP)
    print(f'已注册 {DESKTOP}')


if __name__ == '__main__':
    main()
