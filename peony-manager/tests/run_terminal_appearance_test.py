"""Build an offscreen QTermWidget and verify private OSC probes over its real PTY."""
import argparse
import os
from pathlib import Path
import subprocess

MANAGER = Path(__file__).resolve().parent.parent
REPOSITORY = MANAGER.parent
DEFAULT_OUTPUT = REPOSITORY / "maintenance/cli-followup-20261104/osc-pty-build"

FIXTURE = r'''import json,os,pathlib,select,sys,termios,time,tty
name,marker=sys.argv[1:3]
fd=sys.stdin.fileno()
tty.setraw(fd,termios.TCSANOW)
def send(parts):
    for part in parts:
        os.write(1,part)
        time.sleep(.04)
def read_exactly(count,timeout=5):
    result=b''
    deadline=time.monotonic()+timeout
    while len(result)<count:
        left=deadline-time.monotonic()
        if left<=0: raise TimeoutError('terminal did not answer the color query')
        ready,_,_=select.select([fd],[],[],left)
        if not ready: raise TimeoutError('terminal did not answer the color query')
        result+=os.read(fd,count-len(result))
    return result
try:
    # These OSCs are deliberately not color queries and must not trigger input.
    send([b'\x1b]0;private fixture title\x07',b'\x1b]10;not-a-query\x07',b'\x1b]11;?suffix\x07'])
    send([b'\x1b]10',b';?',b'\x1b',b'\\'])       # split OSC 10, ST terminator
    send([b'\x1b]11;',b'?',b'\x07'])              # split OSC 11, BEL terminator
    send([b'\x1b]10;?',b'\x07'])                  # OSC 10, BEL terminator
    send([b'\x1b]11;?',b'\x1b\\'])               # OSC 11, ST terminator
    raw=read_exactly(4*25)
    extra=b''
    ready,_,_=select.select([fd],[],[],.2)
    if ready: extra=os.read(fd,1024)
    record={'name':name,'replies':[raw[i:i+25].hex() for i in range(0,len(raw),25)],'extra':extra.hex()}
    if name=='light':
        surface=b'\x1b[48;2;250;250;250m  Synthetic user prompt (no model call)  \x1b[0m\r\n'
        footer=b'\x1b[38;2;119;120;122m  model: gpt-6-luna  \x1b[0m\r\n'
    else:
        surface=b'\x1b[48;2;70;74;80m  Synthetic user prompt (no model call)  \x1b[0m\r\n'
        footer=b'\x1b[38;2;156;158;163m  model: gpt-6-luna  \x1b[0m\r\n'
    os.write(1,b'\r\nordinary reply: default palette foreground\r\n'+surface+footer+b'PREVIEW-READY\r\n')
    time.sleep(.4)
    pathlib.Path(marker).write_text(json.dumps(record))
except Exception as exc:
    pathlib.Path(marker+'.error').write_text(type(exc).__name__+': '+str(exc))
    raise
'''


def system_library(soname):
    output = subprocess.check_output(["ldconfig", "-p"], text=True)
    for line in output.splitlines():
        if line.strip().startswith(soname + " "):
            path = line.split("=>", 1)[1].strip()
            if Path(path).is_file():
                return path
    raise RuntimeError("missing system runtime library " + soname)


def build(output_dir, sdk_root):
    subprocess.run(["bash", str(MANAGER / "plugins/peony-cli-panel/build.sh"),
                    "--sdk-root", str(sdk_root), "--output-dir", str(output_dir)], check=True)
    multiarch = subprocess.check_output(["g++", "-print-multiarch"], text=True).strip()
    qt = sdk_root / "extract/qt/usr/include" / multiarch / "qt5"
    term = sdk_root / "qtermwidget/extract/usr/include/qtermwidget5"
    runtime = output_dir / "peony-cli-panel"
    executable = output_dir / "terminal-appearance-test"
    command = ["g++", "-std=c++11", "-O2", "-fPIC", "-DQT_CORE_LIB", "-DQT_GUI_LIB", "-DQT_WIDGETS_LIB",
               "-I" + str(qt), "-I" + str(qt / "QtCore"), "-I" + str(qt / "QtGui"),
               "-I" + str(qt / "QtWidgets"), "-I" + str(term),
               str(MANAGER / "tests/terminal-appearance-test.cpp"), "-o", str(executable),
               str(runtime / "libqtermwidget5.so.0"), str(runtime / "libutf8proc.so.2"),
               system_library("libQt5Widgets.so.5"), system_library("libQt5Gui.so.5"),
               system_library("libQt5Core.so.5"), "-ldl", "-pthread"]
    subprocess.run(command, check=True)
    return executable, runtime


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    sdk_root = args.sdk_root.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    fixture = output_dir / "osc-query-fixture.py"
    fixture.write_text(FIXTURE)
    runtime_case_dir = output_dir / "cases"
    runtime_case_dir.mkdir(exist_ok=True)
    os.chmod(runtime_case_dir, 0o700)

    executable, runtime = build(output_dir, sdk_root)
    xdg_runtime = output_dir / "xdg-runtime"
    xdg_runtime.mkdir(exist_ok=True)
    os.chmod(xdg_runtime, 0o700)
    test_home = output_dir / "test-home"
    test_home.mkdir(exist_ok=True)
    environment = {
        "PATH": "/usr/bin:/bin",
        "HOME": str(test_home),
        "QT_QPA_PLATFORM": "offscreen",
        "QT_QPA_PLATFORMTHEME": "",
        "LD_LIBRARY_PATH": str(runtime) + os.pathsep + "/usr/lib/" + subprocess.check_output(
            ["g++", "-print-multiarch"], text=True).strip(),
        "XDG_RUNTIME_DIR": str(xdg_runtime),
    }
    command = [str(executable), str(fixture), str(runtime_case_dir)]
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=30)
    log = "COMMAND: " + " ".join(command) + "\nEXIT: " + str(result.returncode) + "\nSTDOUT:\n" + result.stdout + "\nSTDERR:\n" + result.stderr
    (output_dir / "terminal-appearance-test.log").write_text(log)
    print(log)
    if result.returncode:
        raise SystemExit(result.returncode)
    print("Artifacts: " + str(output_dir))


if __name__ == "__main__":
    main()
