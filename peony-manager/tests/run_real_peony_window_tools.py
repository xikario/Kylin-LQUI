"""Validate merge and native menu sorting without touching the user's session."""
import os
import struct, zlib
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT if (ROOT / 'vendor').is_dir() else ROOT.parent.parent / 'peony-manager'
with tempfile.TemporaryDirectory(prefix='peony-window-tools-test-') as temp:
    base = Path(temp)
    for i in range(3): (base / str(i)).mkdir()
    (base/'0/folder2').mkdir();(base/'0/second.txt').write_text('second file')
    (base/'0/folder').mkdir();(base/'0/sample.txt').write_text('menu context test')
    def png_chunk(kind, data): return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
    (base/'0/picture.png').write_bytes(b'\x89PNG\r\n\x1a\n'+png_chunk(b'IHDR',struct.pack('!2I5B',1,1,8,2,0,0,0))+png_chunk(b'IDAT',zlib.compress(b'\x00\xff\xff\xff'))+png_chunk(b'IEND',b''))
    qt = SDK / 'vendor/extract/qt/usr/include/aarch64-linux-gnu/qt5'
    probe = base / 'probe.so'
    command = ['g++', '-std=c++11', '-O2', '-fPIC', '-shared', str(ROOT / 'tests/real-peony-window-tools.cpp'), '-o', str(probe), '-I' + str(qt)]
    command += ['-I' + str(qt / part) for part in ('QtCore','QtGui','QtWidgets')]
    command += ['-I' + str(SDK / 'vendor/extract/libpeony-dev/usr/include/peony-qt')]
    command += ['/usr/lib/aarch64-linux-gnu/lib' + part + '.so.5' for part in ('Qt5Widgets','Qt5Gui','Qt5Core')]
    subprocess.run(command + ['/usr/lib/aarch64-linux-gnu/libpeony.so.3', '-ldl'], check=True)
    editor_command = [part for part in command if part != '-shared']
    editor_command[editor_command.index(str(ROOT / 'tests/real-peony-window-tools.cpp'))] = str(ROOT / 'tests/menu-order-test.cpp')
    editor_test = base / 'editor-test'
    editor_command[editor_command.index(str(probe))] = str(editor_test)
    subprocess.run(editor_command, check=True)
    subprocess.run([str(editor_test)], env=dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(base/'editor-config')), check=True, timeout=10)

    installed = bool(os.environ.get('PEONY_TEST_INSTALLED'))
    plugin = Path('/usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-window-tools.so') if installed else ROOT / 'plugins/peony-window-tools/build/libpeony-window-tools.so'
    context_probe=base/'context-probe.so'
    context_command=list(command)
    context_command[context_command.index(str(ROOT/'tests/real-peony-window-tools.cpp'))]=str(ROOT/'tests/real-peony-context-order.cpp')
    context_command[context_command.index(str(probe))]=str(context_probe)
    subprocess.run(context_command+['-ldl'],check=True)
    cases=('contexts', 'normal') if installed else ('contexts', 'normal', 'invalid', 'terminal')
    for case in cases:
        active_probe=context_probe if case=='contexts' else probe
        env = dict(os.environ, PEONY_TEST_CASE=case, PEONY_TEST_DIRECTORY=str(base), PEONY_TEST_PLUGIN=str(plugin))
        result = subprocess.run(['xvfb-run','-a','dbus-run-session','--','env','QT_QPA_PLATFORM=xcb','XDG_CONFIG_HOME='+str(base/('config-'+case)), 'GSETTINGS_BACKEND=memory', 'LD_PRELOAD='+str(active_probe), '/usr/bin/peony',str(base/'0')], env=env, text=True, capture_output=True, timeout=25)
        output = result.stdout + result.stderr
        print(case + ':')
        print('\n'.join(line for line in output.splitlines() if line.startswith(('PASS:', 'FAIL:'))))
        if result.returncode: print(output[-7000:])
        assert result.returncode == 0, 'Real Peony window-tools probe failed: '+case
        assert 'PASS:' in output
