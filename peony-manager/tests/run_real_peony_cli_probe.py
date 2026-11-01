"""Regression test against installed Peony in an isolated Xvfb display."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    with tempfile.TemporaryDirectory(prefix='peony-real-cli-test-') as directory:
        base = Path(directory)
        case = base / 'folder'
        case.mkdir()
        (case / 'example.txt').write_text('Peony CLI alignment test\n')
        qt = ROOT / 'vendor/extract/qt/usr/include/aarch64-linux-gnu/qt5'
        library = base / 'probe.so'
        command = ['g++', '-std=c++11', '-O2', '-fPIC', '-shared',
                   str(ROOT / 'tests/real-peony-cli-probe.cpp'), '-o', str(library), '-I' + str(qt)]
        command += ['-I' + str(qt / part) for part in ('QtCore', 'QtGui', 'QtWidgets')]
        command += ['/usr/lib/aarch64-linux-gnu/lib' + part + '.so.5'
                    for part in ('Qt5Widgets', 'Qt5Gui', 'Qt5Core')]
        subprocess.run(command + ['-ldl'], check=True)
        environment = dict(os.environ,
                           PEONY_TEST_USER_BUS=os.environ['DBUS_SESSION_BUS_ADDRESS'],
                           PEONY_TEST_DIRECTORY=str(case),
                           PEONY_TEST_RUNTIME=str(ROOT / 'plugins/peony-cli-panel/build/peony-cli-panel/libpeony-cli-terminal.so'),
                           PEONY_TEST_CAPTURE=os.environ.get('PEONY_TEST_CAPTURE', str(base / 'collapsed.png')))
        command = ['xvfb-run', '-a', '-s', '-screen 0 1800x1100x24', 'dbus-run-session', '--',
                   'env', 'QT_QPA_PLATFORM=xcb', 'XDG_CONFIG_HOME=' + str(base / 'config'),
                   'LD_PRELOAD=' + str(library), '/usr/bin/peony', str(case)]
        result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=25)
        output = result.stdout + result.stderr
        print('\n'.join(line for line in output.splitlines() if 'PASS:' in line or 'FAIL:' in line))
        if result.returncode:
            print(output[-5000:])
        assert result.returncode == 0, 'Real Peony probe failed'
        assert 'PASS: last session close removes restore button' in output, 'Probe did not finish'


if __name__ == '__main__':
    main()
