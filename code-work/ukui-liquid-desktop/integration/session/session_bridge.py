#!/usr/bin/python3
"""Bounded Fences desktop takeover with native-desktop recovery; no login changes."""
import argparse
import ctypes
import fcntl
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

from gi.repository import Gio, GLib

NATIVE = "/usr/bin/peony-qt-desktop"
DISPLAY = os.environ.get("DISPLAY", "")
KEY = hashlib.sha256(DISPLAY.encode()).hexdigest()[:12]
STATE_DIR = Path(os.environ.get("XDG_STATE_HOME", str(Path.home()/".local/state"))) / "ukui-liquid-desktop"
STATE = STATE_DIR / ("desktop-trial-"+KEY+".json")
LOCK = Path(os.environ.get("XDG_RUNTIME_DIR", "/run/user/"+str(os.getuid()))) / ("liquid-desktop-trial-"+KEY+".lock")


def process_identity(pid):
    proc = Path("/proc", str(pid))
    try:
        if proc.stat().st_uid != os.getuid():
            return None
        fields = (proc/"stat").read_text().rsplit(") ", 1)[1].split()
        if fields[0] == "Z":
            return None
        if ("DISPLAY="+DISPLAY).encode() not in (proc/"environ").read_bytes().split(b"\0"):
            return None
        return {"pid": int(pid), "startTicks": fields[19], "executable": os.readlink(proc/"exe")}
    except (OSError, ValueError, IndexError):
        return None


def native_desktops():
    result = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if os.readlink(proc/"exe") != NATIVE:
                continue
        except OSError:
            continue
        item = process_identity(proc.name)
        if item and item["executable"] == NATIVE:
            result.append(item)
    return result


def native_surfaces():
    pids = {item["pid"] for item in native_desktops()}
    if not pids:
        return []
    x = ctypes.CDLL("libX11.so.6")
    P, W, I = ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int
    def bind(name, args, result):
        function = getattr(x, name); function.argtypes = args; function.restype = result
        return function
    open_display = bind("XOpenDisplay", [ctypes.c_char_p], P)
    close_display = bind("XCloseDisplay", [P], I)
    root = bind("XDefaultRootWindow", [P], W)
    atom = bind("XInternAtom", [P, ctypes.c_char_p, I], W)
    get = bind("XGetWindowProperty", [P,W,W,ctypes.c_long,ctypes.c_long,I,W,ctypes.POINTER(W),ctypes.POINTER(I),ctypes.POINTER(W),ctypes.POINTER(W),ctypes.POINTER(P)], I)
    free = bind("XFree", [P], I)
    handler_type = ctypes.CFUNCTYPE(I,P,P)
    ignore = handler_type(lambda *_: 0)
    set_handler = bind("XSetErrorHandler", [P], P)
    previous_handler=set_handler(ctypes.cast(ignore,P))
    display = open_display(None)
    if not display:
        set_handler(previous_handler)
        return []
    def values(window, name):
        actual, count, remaining, fmt, data = W(), W(), W(), I(), P()
        result=[]
        if get(display,window,atom(display,name,0),0,8192,0,0,ctypes.byref(actual),ctypes.byref(fmt),ctypes.byref(count),ctypes.byref(remaining),ctypes.byref(data))==0 and data and fmt.value==32:
            result=list(ctypes.cast(data,ctypes.POINTER(W))[:count.value])
        if data:free(data)
        return result
    try:
        desktop_type=atom(display,b"_NET_WM_WINDOW_TYPE_DESKTOP",0)
        result=[]
        for window in values(root(display),b"_NET_CLIENT_LIST"):
            pid=values(window,b"_NET_WM_PID")
            if pid and pid[0] in pids:
                result.append({"pid":pid[0],"window":window,"desktop":desktop_type in values(window,b"_NET_WM_WINDOW_TYPE")})
        return result
    finally:
        close_display(display)
        set_handler(previous_handler)


def native_surface_ready():
    return any(surface["desktop"] for surface in native_surfaces())


def require_desktop_only():
    surfaces=native_surfaces()
    if not any(surface["desktop"] for surface in surfaces):
        raise RuntimeError("原生桌面尚未就绪，未开始试运行")
    if any(not surface["desktop"] for surface in surfaces):
        raise RuntimeError("原生桌面进程还承载文件管理器或对话窗口，请先关闭这些窗口再试运行；没有结束任何进程")


def read_state():
    try:
        value = json.loads(STATE.read_text())
        return value if isinstance(value, dict) and value.get("display") == DISPLAY else {}
    except (OSError, ValueError):
        return {}


def write_state(value):
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".desktop-trial-", dir=str(STATE_DIR))
    try:
        with os.fdopen(fd, "w") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, STATE)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def guard_running(state):
    identity = process_identity(state.get("guardPid", 0))
    if not identity or identity["startTicks"] != state.get("guardStartTicks"):
        return False
    try:
        args = Path("/proc", str(identity["pid"]), "cmdline").read_bytes().split(b"\0")
        return os.fsencode(state.get("script", "")) in args and b"--trial" in args
    except OSError:
        return False


class Session:
    def __init__(self):
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def call(self, destination, path, interface, method, signature=None, args=(), timeout=1500):
        value = self.bus.call_sync(destination, path, interface, method,
            GLib.Variant(signature, args) if signature else None, None,
            Gio.DBusCallFlags.NO_AUTO_START, timeout, None)
        return value.unpack() if value else ()

    def owner(self, name):
        try:
            return self.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner", "(s)", (name,))[0]
        except GLib.Error:
            return None

    def fences(self):
        owner = self.owner("org.ukui.fences")
        if not owner:
            raise RuntimeError("Fences 尚未运行")
        pid = self.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID", "(s)", (owner,))[0]
        identity = process_identity(pid)
        if not identity or Path(identity["executable"]).name != "ukui-fences":
            raise RuntimeError("Fences 身份与当前用户、显示会话不匹配")
        if not self.call("org.ukui.fences", "/ukuiFences", "org.ukui.fences", "fencesDesktopVisible")[0]:
            raise RuntimeError("请先显示 Fences 桌面")
        return owner, identity

    def module(self, method):
        if not self.owner("org.gnome.SessionManager"):
            return False
        self.call("org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager", method, "(s)", ("peony-desktop.desktop",))
        return True

    def restore_native(self):
        if native_surface_ready():
            return
        requested = False
        try:
            requested = self.module("startModule")
        except GLib.Error:
            pass
        if requested:
            deadline = time.monotonic()+6
            while time.monotonic() < deadline and not native_desktops():
                time.sleep(.2)
        if not native_desktops():
            subprocess.Popen([NATIVE, "-w", "-d"], stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            if native_surface_ready():
                return
            time.sleep(.2)
        raise RuntimeError("原生桌面尚未恢复；Fences 未被关闭，可再次点击恢复")


class Trial:
    def __init__(self, seconds):
        self.session = Session()
        self.owner, self.fences = self.session.fences()
        native = native_desktops()
        if not native:
            raise RuntimeError("原生 Peony 桌面未运行，无需进行重复接管")
        require_desktop_only()
        own = process_identity(os.getpid())
        self.state = {"schemaVersion": 1, "display": DISPLAY, "active": False, "phase": "preparing",
            "guardPid": os.getpid(), "guardStartTicks": own["startTicks"], "script": str(Path(__file__).resolve()),
            "fencesPid": self.fences["pid"], "nativeBefore": native, "seconds": seconds,
            "deadline": time.time()+seconds, "permanent": False}
        self.loop = GLib.MainLoop()
        self.finished = False
        self.failures = 0

    def restore(self, reason):
        if self.finished or getattr(self, "restoring", False) or getattr(self, "recovery_retry", False):
            return False
        self.restoring = True
        self.state.update(active=False, phase="restoring", reason=reason)
        def persist():
            try:write_state(self.state)
            except OSError as error:
                print("恢复状态无法保存："+str(error),file=sys.stderr)
        # Recovery must work even when the disk is full or the state directory
        # disappeared. Only a confirmed restoration may finish the guard.
        persist()
        try:
            self.session.restore_native()
            self.state.update(phase="restored", nativeAfter=native_desktops())
        except Exception as error:
            self.restoring = False
            self.state.update(phase="recovery-required", error=str(error))
            persist()
            if not getattr(self,"recovery_retry",False):
                self.recovery_retry=True
                def retry():
                    self.recovery_retry=False
                    self.restore(reason)
                    return False
                GLib.timeout_add_seconds(3,retry)
            return False
        self.finished = True
        self.restoring = False
        persist()
        self.loop.quit()
        return False

    def check(self):
        if self.finished or self.state.get("phase") in ("restoring", "recovery-required"):
            return False
        if self.session.owner("org.ukui.fences") != self.owner or process_identity(self.fences["pid"]) != self.fences:
            return self.restore("fences-exited")
        try:
            visible = self.session.call("org.ukui.fences", "/ukuiFences", "org.ukui.fences", "fencesDesktopVisible")[0]
            self.failures = 0
            if not visible:
                return self.restore("fences-hidden")
        except GLib.Error:
            self.failures += 1
            if self.failures >= 3:
                return self.restore("fences-unresponsive")
        if native_desktops():
            return self.restore("native-desktop-returned")
        return True

    def run(self):
        write_state(self.state)
        for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGUSR1):
            signal.signal(sig, lambda *_: GLib.idle_add(self.restore, "manual"))
        try:
            require_desktop_only()
            managed = self.session.module("stopModule")
            deadline = time.monotonic()+(5 if managed else 0)
            while time.monotonic() < deadline and native_desktops():
                time.sleep(.2)
            # Only the exact same-user, same-display desktop executable and
            # start-time identity captured above may be stopped. Never peony -q.
            for item in self.state["nativeBefore"]:
                if process_identity(item["pid"]) == item:
                    require_desktop_only()
                    os.kill(item["pid"], signal.SIGTERM)
            deadline = time.monotonic()+5
            while time.monotonic() < deadline and native_desktops():
                time.sleep(.2)
            if native_desktops():
                raise RuntimeError("原生桌面未退出，取消试运行")
            self.session.fences()
            self.state.update(active=True, phase="active", deadline=time.time()+self.state["seconds"])
            write_state(self.state)
            GLib.timeout_add_seconds(1, self.check)
            GLib.timeout_add_seconds(self.state["seconds"], self.restore, "deadline")
            self.loop.run()
        except Exception as error:
            self.state["error"] = str(error)
            self.restore("error")
            # A failed start still owns recovery. Run queued retries until a
            # native surface is confirmed, retaining the process lock meanwhile.
            if not self.finished:
                self.loop.run()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--status", action="store_true")
    action.add_argument("--restore", action="store_true")
    action.add_argument("--trial", type=int, metavar="SECONDS")
    args = parser.parse_args()
    if not DISPLAY:
        raise RuntimeError("需要当前 X11 显示会话")
    if args.status:
        state = read_state()
        schema = Gio.SettingsSchemaSource.get_default().lookup("org.ukui.session.required-components", True)
        state["loginDesktopProvider"] = Gio.Settings.new_full(schema, None, None).get_string("filemanager") if schema else None
        state["persistentTakeoverSupported"] = False
        state["active"] = bool(state.get("active") and guard_running(state))
        state["nativeRunning"] = bool(native_desktops())
        state["remainingSeconds"] = max(0, int(state.get("deadline", 0)-time.time())) if state["active"] else 0
        print(json.dumps(state, ensure_ascii=False))
        return
    if args.restore:
        state = read_state()
        if guard_running(state):
            os.kill(state["guardPid"], signal.SIGUSR1)
        else:
            Session().restore_native()
            state.update(display=DISPLAY, active=False, phase="restored", reason="manual-recovery")
            write_state(state)
        print(json.dumps({"restorationRequested": True}))
        return
    if not 5 <= args.trial <= 300:
        raise RuntimeError("试运行时间范围为 5–300 秒")
    with LOCK.open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        Trial(args.trial).run()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        if "--trial" in sys.argv:
            state=read_state()
            if not guard_running(state):
                write_state({"display":DISPLAY,"active":False,"phase":"failed","error":str(error)})
        print(json.dumps({"error": str(error)}, ensure_ascii=False), file=sys.stderr)
        sys.exit(1)
