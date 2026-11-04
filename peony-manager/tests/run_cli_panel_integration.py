"""Exercise real Qt plugin + PTYs + systemd scopes without changing user's config."""
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import time
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
import sys
sys.path.insert(0, str(ROOT))
from app import cli_settings


def compile_harness(output):
    qt = ROOT / "vendor/extract/qt/usr/include/aarch64-linux-gnu/qt5"
    command = ["g++", "-std=c++11", "-O2", "-fPIC", "-I" + str(qt)]
    command += ["-I" + str(qt / p) for p in ("QtCore", "QtGui", "QtWidgets")]
    command += ["-I" + str(ROOT / "vendor/extract/libpeony-dev/usr/include/peony-qt"),
                str(ROOT / "tests/cli-panel-harness.cpp"), "-o", str(output)]
    command += ["/usr/lib/aarch64-linux-gnu/lib" + p + ".so.5" for p in ("Qt5Widgets", "Qt5Gui", "Qt5Core")]
    subprocess.run(command, check=True)


CHILD = '''import json,os,pathlib,re,subprocess,sys,time,threading,tty
print('当前文件夹内容：中文间距正常',flush=True)
print('ASCII: iii MMM 000 | 中文：当前文件夹 | ──────────',flush=True)
print('',flush=True)
print('文件检查完成',flush=True)
print('',flush=True)
print('  目录结构清晰，中文与 English 自然对齐。',flush=True)
print('  支持复制粘贴，收起后可继续原来的会话。',flush=True)
print('',flush=True)
print('  frontend/',flush=True)
print('    ├─ css/',flush=True)
print('    ├─ js/',flush=True)
print('    └─ index.html',flush=True)
print('',flush=True)
print('  const message = "你好，Peony";',flush=True)
print('  console.log(message);',flush=True)
print('',flush=True)
children=[subprocess.Popen(['/bin/sleep','90']),subprocess.Popen(['/bin/sleep','90'],start_new_session=True)]
unit=re.search(r'peony-cli-[0-9a-f]{32}\\.scope',pathlib.Path('/proc/self/cgroup').read_text()).group()
record={'cwd':os.getcwd(),'pids':[os.getpid()]+[p.pid for p in children],'unit':unit}
path=pathlib.Path(sys.argv[1]); tmp=path.with_suffix('.tmp'); tmp.write_text(json.dumps(record,ensure_ascii=False)); tmp.replace(path)
def receive():
 line=sys.stdin.readline()
 pathlib.Path(str(path)+'.input').write_text(line.rstrip('\\r\\n'))
 tty.setraw(sys.stdin.fileno())
 pathlib.Path(str(path)+'.rawready').write_text('ready')
 keys=b''
 while len(keys)<4: keys+=os.read(sys.stdin.fileno(),4-len(keys))
 pathlib.Path(str(path)+'.keys').write_bytes(keys)
threading.Thread(target=receive,daemon=True).start()
time.sleep(90)
'''

MANAGED_CODEX = '''#!/usr/bin/python3
import json,os,pathlib,re,subprocess,sys,time
children=[subprocess.Popen(['/bin/sleep','90']),subprocess.Popen(['/bin/sleep','90'],start_new_session=True)]
unit=re.search(r'peony-cli-[0-9a-f]{32}\\.scope',pathlib.Path('/proc/self/cgroup').read_text()).group()
record={'cwd':os.getcwd(),'argv':sys.argv[1:],'loginEnv':os.environ.get('PEONY_TEST_LOGIN_ENV'),'pids':[os.getpid()]+[p.pid for p in children],'unit':unit}
path=pathlib.Path(os.environ['PEONY_MANAGED_ARGV_MARKER']); tmp=path.with_suffix('.tmp'); tmp.write_text(json.dumps(record)); tmp.replace(path)
time.sleep(90)
'''


def running(pid):
    try:
        text = Path('/proc/%s/stat' % pid).read_text()
        return text[text.rfind(')') + 2] != 'Z'
    except FileNotFoundError:
        return False


def main():
    with tempfile.TemporaryDirectory(prefix="peony-cli-test-") as directory:
        base = Path(directory)
        harness = base / "harness"
        compile_harness(harness)
        for mode in ("normal", "crash"):
            case = base / mode
            case.mkdir()
            (case / "child.py").write_text(CHILD)
            environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="",
                               XDG_CONFIG_HOME=str(case / "config"))
            command = [str(harness), str(ROOT / "plugins/peony-cli-panel/build/libpeony-cli-panel.so"), str(case)]
            if mode == "crash":
                command.append("crash")
            result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=30)
            print(mode + ": " + result.stderr[-1800:])
            records = [json.loads(p.read_text()) for p in case.glob("*.json")]
            try:
                assert result.returncode == 0, "Qt harness failed"
                assert records, "No process markers"
                deadline = time.monotonic() + 8
                while any(running(pid) for record in records for pid in record['pids']) and time.monotonic() < deadline:
                    time.sleep(.1)
                assert not any(running(pid) for record in records for pid in record['pids']), "Processes survived window/crash cleanup"
            finally:
                for record in records:
                    subprocess.run(["systemctl", "--user", "stop", record['unit']], capture_output=True, timeout=10)

        case = base / "managed-argv"
        case.mkdir()
        home = case / "isolated home"
        home.mkdir()
        (home / ".bash_profile").write_text("export PEONY_TEST_LOGIN_ENV='from login profile'\n")
        fake_codex = case / "fake codex"
        fake_codex.write_text(MANAGED_CODEX)
        fake_codex.chmod(0o755)
        with patch.object(cli_settings.shutil, "which", return_value=str(fake_codex)):
            managed_settings = cli_settings.defaults()
            managed_settings.update(command=shlex.quote(str(fake_codex)), model="gpt-6-luna", reasoningEffort="xhigh")
            managed_settings = cli_settings.validate(managed_settings)
        (case / "managed-settings.fixture").write_text(json.dumps(managed_settings))
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="",
                           XDG_CONFIG_HOME=str(case / "config"), HOME=str(home),
                           PEONY_MANAGED_ARGV_MARKER=str(case / "managed.json"))
        command = [str(harness), str(ROOT / "plugins/peony-cli-panel/build/libpeony-cli-panel.so"),
                   str(case), "managed-argv"]
        # The parent may define CODEX_HOME. Mask its filesystem target instead
        # of changing that variable or allowing a fixture to write real defaults.
        profile_home = home / ".codex"
        profile_home.mkdir()
        codex_home = os.environ.get("CODEX_HOME", str(Path.home() / ".codex"))
        command = ["bwrap", "--bind", "/", "/", "--dev-bind", "/dev", "/dev", "--bind", str(profile_home), codex_home, "--"] + command
        result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=30)
        print("managed-argv: " + result.stderr[-1800:])
        records = [json.loads((case / "managed.json").read_text())] if (case / "managed.json").exists() else []
        try:
            assert result.returncode == 0, "managed Codex argv harness failed"
            assert records, "Managed fake Codex did not start"
            profile = home / ".codex/peony-cli.config.toml"
            assert 'model = "gpt-6-luna"' in profile.read_text(), "Legacy model seed was not migrated"
            assert 'model_reasoning_effort = "xhigh"' in profile.read_text(), "Legacy effort seed was not migrated"
            assert profile.stat().st_mode & 0o777 == 0o600, "Model profile must be private"
            assert records[0]["argv"] == ["--profile", "peony-cli"], "Profile arguments changed across the login shell"
            assert records[0]["cwd"].endswith("目录 空格 ' $HOME ; test"), "Managed working directory changed"
            assert records[0]["loginEnv"] == "from login profile", "Login shell profile was not loaded"
            deadline = time.monotonic() + 8
            while any(running(pid) for pid in records[0]['pids']) and time.monotonic() < deadline:
                time.sleep(.1)
            assert not any(running(pid) for pid in records[0]['pids']), "Managed CLI descendants survived tab close"
        finally:
            for record in records:
                subprocess.run(["systemctl", "--user", "stop", record['unit']], capture_output=True, timeout=10)
        print("PASS: default Codex model args survive the login shell literally; cwd and scope cleanup verified")
        print("PASS: normal window cleanup and abrupt host death clean all descendant processes")


if __name__ == "__main__":
    main()
