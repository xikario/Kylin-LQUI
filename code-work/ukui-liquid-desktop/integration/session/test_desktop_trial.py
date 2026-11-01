#!/usr/bin/python3
"""Actual Peony/Fences/KWin takeover, only inside private X11 and session bus."""
import argparse
import contextlib
import ctypes as c
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

parser=argparse.ArgumentParser()
parser.add_argument("--fences",type=Path,required=True)
parser.add_argument("--output",type=Path,required=True)
parser.add_argument("--inside",action="store_true")
parser.add_argument("--workspace",type=Path)
args=parser.parse_args();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
assert os.environ.get("LIQUID_SESSION_ISOLATED")=="1", "Use a private Xvfb and dbus-run-session"
helper=Path(__file__).resolve().with_name("session_bridge.py")
spec=importlib.util.spec_from_file_location("bridge",helper);bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
if not args.inside:
    assert not bridge.native_desktops() and not bridge.Session().owner('org.ukui.fences'), 'Refusing a display/bus that already owns a desktop; start fresh Xvfb and dbus-run-session'
else:
    assert args.workspace and args.workspace.is_absolute() and args.workspace.name.startswith('liquid-session-lab-'), 'Missing private lab workspace'
def wait(predicate,message,timeout=15):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        if predicate():print("PASS",message,flush=True);return
        time.sleep(.15)
    raise AssertionError(message)
def call(method,*values):
    return subprocess.check_output(["gdbus","call","--session","--timeout","3","--dest","org.ukui.fences","--object-path","/ukuiFences","--method","org.ukui.fences."+method,*values],text=True,stderr=subprocess.DEVNULL)
def visible():
    try:return "true" in call("fencesDesktopVisible")
    except subprocess.CalledProcessError:return False

with (contextlib.nullcontext(str(args.workspace)) if args.inside else tempfile.TemporaryDirectory(prefix="liquid-session-lab-")) as folder:
    root=Path(folder);home=Path.home()
    for name in ("config/kylin","cache","data","state","log","runtime","Desktop"):(root/name).mkdir(parents=True,exist_ok=True)
    (root/"runtime").chmod(0o700)
    (root/"config/user-dirs.dirs").write_text('XDG_DESKTOP_DIR="'+str(root/"Desktop")+'"\n')
    (root/"config/kylin/ukui-fences.conf").write_text("[systemMonitor]\nautoStart=false\n[desklets]\nclock\\autoStart=false\nactivity\\autoStart=false\nmusic\\autoStart=false\ncalendar\\autoStart=false\n")
    fixture=root/"Desktop/keep.txt";fixture.write_text("desktop fixture stays unchanged\n")
    env=dict(os.environ,XDG_CONFIG_HOME=str(home/".config"),XDG_CACHE_HOME=str(home/".cache"),XDG_DATA_HOME=str(root/"data"),XDG_STATE_HOME=str(root/"state"),XDG_RUNTIME_DIR=str(root/"runtime"),XDG_DESKTOP_DIR=str(root/"Desktop"),GSETTINGS_BACKEND="memory",GIO_USE_VFS="local",GIO_USE_VOLUME_MONITOR="unix",UKUI_FENCES_GLASS_NO_GL="1",UKUI_LIQUID_GLASS_NO_GL="1")
    prefix=["bwrap","--die-with-parent","--ro-bind","/","/","--dev-bind","/dev","/dev","--proc","/proc","--bind","/tmp","/tmp","--bind",str(root/"config"),str(home/".config"),"--bind",str(root/"cache"),str(home/".cache"),"--bind",str(root/"log"),str(home/".log")]
    if not args.inside:
        # One sandbox for the whole lab: sibling user namespaces intentionally
        # cannot inspect each other's /proc identities. Keep the production
        # identity checks strict instead of bypassing them for tests.
        subprocess.run(prefix+["--bind",str(args.output),str(args.output),"/usr/bin/python3",str(Path(__file__).resolve()),"--inside","--workspace",str(root),"--fences",str(args.fences.resolve()),"--output",str(args.output)],env=env,check=True,timeout=100)
        sys.exit(0)
    prefix=[]
    processes=[];trials=[]
    def start(command,name):
        stream=(args.output/name).open("w")
        process=subprocess.Popen(prefix+command,env=env,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True);stream.close();processes.append(process);return process
    def status():
        return json.loads(subprocess.check_output(prefix+["/usr/bin/python3",str(helper),"--status"],env=env,text=True))
    try:
        wm=start(["/usr/bin/ukui-kwin_x11","--no-kactivities"],"kwin.log")
        native=start([bridge.NATIVE,"-w","-d"],"native.log")
        wait(bridge.native_surface_ready,"original native desktop is visible")
        fences=start([str(args.fences.resolve()),"--autostart"],"fences.log")
        wait(visible,"Fences desktop is ready before takeover")
        session=bridge.Session();owner=session.owner("org.ukui.fences");owner_pid=session.call("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetConnectionUnixProcessID","(s)",(owner,))[0]
        proc=Path('/proc',str(owner_pid))
        diagnostic={'pid':owner_pid,'identity':bridge.process_identity(owner_pid),'uid':proc.stat().st_uid,'exe':os.readlink(proc/'exe'),'displayMatch':('DISPLAY='+os.environ['DISPLAY']).encode() in (proc/'environ').read_bytes().split(b'\0')}
        (args.output/'identity.json').write_text(json.dumps(diagnostic,ensure_ascii=False,indent=2))
        # Model a file window hosted by the desktop PID through real X11
        # properties; takeover must refuse before signalling that process.
        x=c.CDLL('libX11.so.6');P,W,I=c.c_void_p,c.c_ulong,c.c_int
        def bind(name,types,result):
            fn=getattr(x,name);fn.argtypes=types;fn.restype=result;return fn
        display=bind('XOpenDisplay',[c.c_char_p],P)(None);xroot=bind('XDefaultRootWindow',[P],W)(display)
        atom=bind('XInternAtom',[P,c.c_char_p,I],W);create=bind('XCreateSimpleWindow',[P,W,I,I,c.c_uint,c.c_uint,c.c_uint,W,W],W)
        prop=bind('XChangeProperty',[P,W,W,W,I,I,P,I],I);flush=bind('XFlush',[P],I)
        window=create(display,xroot,20,20,200,100,0,0,0);original=bridge.native_desktops();pid=(W*1)(original[0]['pid']);normal=(W*1)(atom(display,b'_NET_WM_WINDOW_TYPE_NORMAL',0))
        prop(display,window,atom(display,b'_NET_WM_PID',0),6,32,0,pid,1);prop(display,window,atom(display,b'_NET_WM_WINDOW_TYPE',0),4,32,0,normal,1)
        bind('XMapWindow',[P,W],I)(display,window);flush(display)
        wait(lambda:any(not item['desktop'] for item in bridge.native_surfaces()),'ordinary window associated with native desktop PID is visible')
        refusal=subprocess.run(['/usr/bin/python3',str(helper),'--trial','5'],env=env,capture_output=True,text=True)
        assert refusal.returncode!=0 and bridge.native_desktops()==original
        print('PASS takeover refuses to close a desktop process that owns file/dialog windows',flush=True)
        bind('XDestroyWindow',[P,W],I)(display,window);flush(display);bind('XCloseDisplay',[P],I)(display)
        wait(lambda:not any(not item['desktop'] for item in bridge.native_surfaces()),'fixture file window removed without touching the desktop')
        for mode,seconds in (("deadline",5),("exit",30),("hide",30)):
            if mode=="hide":
                fences=start([str(args.fences.resolve()),"--autostart"],"fences-restarted.log");wait(visible,"Fences restarted for hide recovery")
            trial=start(["/usr/bin/python3",str(helper),"--trial",str(seconds)],"trial-"+mode+".log")
            wait(lambda:status().get("active"),"exclusive Fences desktop active: "+mode)
            assert not bridge.native_desktops(),"native desktop must actually exit, not merely hide"
            assert visible(),"Fences remains available"
            if mode=="exit":
                # The only terminated application is this private fixture.
                call("quitApp")
            elif mode=="hide":call("hideFences")
            wait(lambda:status().get("phase")=="restored","native desktop automatically restored: "+mode,20)
            wait(bridge.native_surface_ready,"restored native desktop owns a real desktop surface")
            trial.wait(timeout=5);assert trial.returncode==0
            record=status();trials.append(record)
            assert record["reason"] in ({"deadline"} if mode=="deadline" else {"fences-exited","fences-unresponsive"} if mode=="exit" else {"fences-hidden"})
            assert fixture.read_text()=="desktop fixture stays unchanged\n"
        (args.output/"results.json").write_text(json.dumps(trials,ensure_ascii=False,indent=2))
        print("PASS no login configuration changed; all three recovery paths restored Peony",flush=True)
    finally:
        for item in bridge.native_desktops():
            if bridge.process_identity(item["pid"])==item:os.kill(item["pid"],signal.SIGTERM)
        for process in reversed(processes):
            if process.poll() is None:
                os.killpg(process.pid,signal.SIGTERM)
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
