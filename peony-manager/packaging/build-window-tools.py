#!/usr/bin/python3
"""Build a standalone package; no privileged scripts or automatic Peony restart."""
import argparse
import subprocess
import tempfile
import shutil
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def build(output):
    arch = subprocess.check_output(['dpkg', '--print-architecture'], text=True).strip()
    multiarch = subprocess.check_output(['dpkg-architecture', '-qDEB_HOST_MULTIARCH'], text=True).strip()
    with tempfile.TemporaryDirectory(prefix='peony-window-tools-package-') as directory:
        stage = Path(directory)
        stage.chmod(0o755)
        for source, relative in [
            ('libpeony-window-tools.so', 'usr/lib/'+multiarch+'/peony-extensions/libpeony-window-tools.so'),
        ]:
            destination = stage / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / 'plugins/peony-window-tools/build' / source, destination)
            destination.chmod(0o755 if source == 'peony-menu-order' else 0o644)
        documentation = stage / 'usr/share/doc/peony-window-tools'
        documentation.mkdir(parents=True)
        shutil.copy2(ROOT / 'plugins/peony-window-tools/README.md', documentation / 'README.md')
        shutil.copy2(ROOT.parent / 'LICENSE', documentation / 'copyright')
        for path in documentation.iterdir(): path.chmod(0o644)
        control = stage / 'DEBIAN'
        control.mkdir()
        (control / 'control').write_text('Package: peony-window-tools\nVersion: 1.1.0\nArchitecture: '+arch+'\nMaintainer: Kylin-LQUI local build\nDepends: peony, libqt5widgets5, libqt5gui5, libqt5core5a\nDescription: Custom context menu order and merge file manager tabs\n')
        for path in stage.rglob('*'):
            if path.is_dir(): path.chmod(0o755)
        output = Path(output).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['dpkg-deb', '--root-owner-group', '--build', str(stage), str(output)], check=True)
        return output
if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output')
    print(build(parser.parse_args().output))
