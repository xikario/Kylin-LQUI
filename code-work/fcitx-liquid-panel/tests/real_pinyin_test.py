#!/usr/bin/env python3
"""Real Rime Ice pronunciation through Fcitx4/Kimpanel, in a private session."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

assert os.environ.get('FCITX_LIQUID_ISOLATED_TEST')=='1'
assert os.environ.get('DISPLAY')!=':0'
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--cache',type=Path)
args=parser.parse_args();root=Path(__file__).resolve().parents[1]
DBusGMainLoop(set_as_default=True);bus=dbus.SessionBus();assert not bus.name_has_owner('org.fcitx.Fcitx')
with tempfile.TemporaryDirectory(prefix='rime-actual-pinyin-') as directory:
    base=Path(directory);config=base/'config/fcitx';rime=config/'rime'
    shutil.copytree(args.source,rime,ignore=shutil.ignore_patterns('*.userdb','sync','build','liquid-settings-backups'))
    if args.cache:shutil.copytree(args.cache,rime/'build')
    (config/'profile').write_text('[Profile]\nIMName=rime\nEnabledIMList=fcitx-keyboard-us:True,rime:True\n')
    (config/'config').write_text('[Program]\nDelayStart=0\n')
    (rime/'default.custom.yaml').write_text('patch:\n  menu/page_size: 6\n  schema_list:\n    - schema: rime_ice\n')
    env=dict(os.environ,XDG_CONFIG_HOME=str(base/'config'),XDG_DATA_HOME=str(base/'data'),XDG_CACHE_HOME=str(base/'cache'),QT_QPA_PLATFORM='xcb',QT_QPA_PLATFORMTHEME='',QT_STYLE_OVERRIDE='Fusion',QT_IM_MODULE='fcitx',XMODIFIERS='@im=fcitx',UKUI_LIQUID_GLASS_NO_GL='1')
    subprocess.run(['/usr/bin/python3',str(root/'manage.py'),'rime-pinyin','true'],env=env,check=True)
    processes=[]
    with (root/'build/real-pinyin-backend.log').open('w') as log:
        try:
            processes.append(subprocess.Popen(['fcitx','-D','-s','0','--disable=fcitx-com.sogou.ime.next.kylin'],env=env,stdout=log,stderr=log))
            for _ in range(300):
                if bus.name_has_owner('org.fcitx.Fcitx'):break
                time.sleep(.1)
            assert bus.name_has_owner('org.fcitx.Fcitx')
            processes.append(subprocess.Popen([str(root/'build/fcitx-liquid-panel')],env=env,stdout=log,stderr=log))
            for _ in range(100):
                if bus.name_has_owner('org.kde.impanel'):break
                time.sleep(.1)
            assert bus.name_has_owner('org.kde.impanel')
            for key in ['XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','QT_QPA_PLATFORM','QT_QPA_PLATFORMTHEME','QT_STYLE_OVERRIDE','QT_IM_MODULE','XMODIFIERS']:os.environ[key]=env[key]
            from PyQt5.QtWidgets import QApplication,QLineEdit
            app=QApplication([]);editor=QLineEdit();editor.show()
            control=dbus.Interface(bus.get_object('org.kde.impanel','/org/ukui/FcitxLiquid'),'org.ukui.FcitxLiquid')
            im=dbus.Interface(bus.get_object('org.fcitx.Fcitx','/inputmethod'),'org.fcitx.Fcitx.InputMethod')
            def pump(seconds=.2):
                end=time.monotonic()+seconds;context=GLib.MainContext.default()
                while time.monotonic()<end:
                    app.processEvents()
                    while context.pending():context.iteration(False)
                    time.sleep(.01)
            def enter_word(want_pinyin):
                editor.clear();subprocess.run(['xdotool','type','--clearmodifiers','nihao'],check=True)
                for _ in range(300):
                    pump(.1);state=control.Status()
                    if state['visible'] and state['candidateCount']>0 and bool(state['candidatePinyinCount'])==want_pinyin:break
                else:raise AssertionError(dict(state))
                subprocess.run(['xdotool','key','1'],check=True);pump()
                assert editor.text()=='你好',editor.text()
            pump();subprocess.run(['xdotool','windowfocus',str(int(editor.winId()))],check=True);editor.setFocus();pump(.5)
            im.SetCurrentIM('rime');im.ActivateIM();pump(2)
            # A real Ice snapshot can still be compiling dictionaries. Esc is
            # sent only in this private display to refresh Rime's ready state.
            for _ in range(600):
                im.SetCurrentIM('rime');im.ActivateIM()
                subprocess.run(['xdotool','key','Escape'],check=True);pump(.2)
                if str(control.Status()['trayLabel'])=='中':break
            else:raise AssertionError(('Rime not ready',dict(control.Status())))
            enter_word(True)
            # Burst typing/deleting changes word lengths while a paint is pending.
            # This used to crash the desktop panel and restore Classic UI.
            for _ in range(20):
                subprocess.run(['xdotool','type','--delay','0','nihaoshijie'],check=True);pump(.015)
                subprocess.run(['xdotool','key','--repeat','6','--delay','1','BackSpace'],check=True);pump(.015)
                subprocess.run(['xdotool','type','--delay','0','ge'],check=True);pump(.015)
                subprocess.run(['xdotool','key','Escape'],check=True);pump(.015)
                assert processes[1].poll() is None,'panel exited during rapid candidate updates'
                assert bus.name_has_owner('org.kde.impanel')
            # Let the backend drain the burst before starting the capture composition.
            pump(3);subprocess.run(['xdotool','key','Escape'],check=True);pump(.5)
            subprocess.run(['xdotool','type','--delay','80','zhege'],check=True)
            for _ in range(200):
                pump(.1);live=control.Status()
                if live['visible'] and live['candidateCount']>0 and live['candidatePinyinCount']>0:break
            else:raise AssertionError(dict(live))
            import re
            from PyQt5.QtCore import QRect
            x,y,w,h=map(int,re.fullmatch(r'(-?\d+),(-?\d+) (\d+)x(\d+)',str(live['geometry'])).groups())
            assert app.primaryScreen().grabWindow(0,x,y,w,h).save(str(root/'build/rime-live-pinyin.png'))
            subprocess.run(['xdotool','key','Escape'],check=True);pump()
            subprocess.run(['/usr/bin/python3',str(root/'manage.py'),'rime-pinyin','false'],env=env,check=True)
            control.ReloadRime();pump(2)
            for _ in range(600):
                subprocess.run(['xdotool','key','Escape'],check=True);pump(.2)
                if str(control.Status()['trayLabel'])=='中':break
            enter_word(False)
            print('PASS: real Rime Ice pinyin on/off, rapid typing/deleting stays alive, live 这个 capture; 你好 selection unchanged')
            editor.close()
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:process.wait(timeout=5)
                    except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
