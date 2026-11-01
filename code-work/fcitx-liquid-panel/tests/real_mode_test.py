#!/usr/bin/env python3
"""Real Fcitx/Rime shortcuts on an isolated bus and Xvfb, never the desktop."""
import os
from pathlib import Path
import subprocess
import tempfile
import time
import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

assert os.environ.get('FCITX_LIQUID_ISOLATED_TEST') == '1'
assert os.environ.get('DISPLAY') != ':0'
DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
assert not bus.name_has_owner('org.fcitx.Fcitx')
properties = {}
def update_property(value):
    properties[str(value).split(':',1)[0]] = str(value)
bus.add_signal_receiver(update_property,dbus_interface='org.kde.kimpanel.inputmethod',signal_name='UpdateProperty')
bus.add_signal_receiver(lambda values:[update_property(value) for value in values],
                        dbus_interface='org.kde.kimpanel.inputmethod',signal_name='RegisterProperties')
root = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix='fcitx-real-mode-') as directory:
    base = Path(directory)
    config = base/'config/fcitx'
    rime = config/'rime'
    rime.mkdir(parents=True)
    (config/'profile').write_text('[Profile]\nIMName=rime\nEnabledIMList=fcitx-keyboard-us:True,rime:True\n')
    (config/'config').write_text('[Program]\nDelayStart=0\n[Hotkey]\nTriggerKey=CTRL_SPACE\nSwitchKey=Disabled\n')
    # A tiny public test dictionary also verifies actual backend pagination.
    composer = ('ascii_composer:\n  good_old_caps_lock: true\n  switch_key:\n'
                '    Shift_L: commit_code\n    Shift_R: noop\n    Caps_Lock: clear\n')
    (rime/'default.yaml').write_text('config_version: "1.0"\nschema_list:\n  - schema: tray_test\n'+composer)
    (rime/'default.custom.yaml').write_text('patch:\n  schema_list:\n    - schema: tray_test\n'
                                           '  menu/page_size: 6\n  ascii_composer/good_old_caps_lock: true\n'
                                           '  ascii_composer/switch_key:\n'
                                           '    Shift_L: commit_code\n    Shift_R: noop\n    Caps_Lock: clear\n')
    (rime/'tray_test.schema.yaml').write_text(
        'schema:\n  schema_id: tray_test\n  name: 托盘测试\n  version: "1.0"\n'
        'switches:\n  - name: ascii_mode\n    reset: 0\n    states: [中文, 西文]\n'
        'engine:\n  processors: [ascii_composer, recognizer, speller, selector, navigator, express_editor]\n'
        '  segmentors: [ascii_segmentor, abc_segmentor, fallback_segmentor]\n'
        '  translators: [table_translator]\n'
        'speller:\n  alphabet: abc\ntranslator:\n  dictionary: tray_test\n  enable_user_dict: false\n'+composer)
    words=list('甲乙丙丁戊己庚辛壬癸子丑')
    (rime/'tray_test.dict.yaml').write_text('---\nname: tray_test\nversion: "1.0"\nsort: by_weight\n...\n'+''.join(word+'\ta\t'+str(100-i)+'\n' for i,word in enumerate(words)))
    env = dict(os.environ, XDG_CONFIG_HOME=str(base/'config'), XDG_DATA_HOME=str(base/'data'),
               XDG_CACHE_HOME=str(base/'cache'), QT_QPA_PLATFORM='xcb', QT_QPA_PLATFORMTHEME='',
               QT_STYLE_OVERRIDE='Fusion', QT_IM_MODULE='fcitx', XMODIFIERS='@im=fcitx',
               UKUI_LIQUID_GLASS_NO_GL='1')
    processes = []
    subprocess.run(['/usr/bin/python3',str(root/'manage.py'),'branding'],env=env,check=True)
    with (root/'build/real-mode-test.log').open('w') as log:
        try:
            processes.append(subprocess.Popen(['fcitx','-D','-s','0','--disable=fcitx-com.sogou.ime.next.kylin'],env=env,stdout=log,stderr=log))
            for _ in range(200):
                if bus.name_has_owner('org.fcitx.Fcitx'): break
                time.sleep(.05)
            assert bus.name_has_owner('org.fcitx.Fcitx'), 'Fcitx startup failed'
            props=dbus.Interface(bus.get_object('org.fcitx.Fcitx','/inputmethod'),'org.freedesktop.DBus.Properties')
            registered=[str(item[0]) for item in props.Get('org.fcitx.Fcitx.InputMethod','IMList') if str(item[1])=='rime']
            assert registered==['RIME液态输入法'],registered
            print('PASS: user-local Rime metadata registers the new product name')
            processes.append(subprocess.Popen([str(root/'build/fcitx-liquid-panel')],env=env,stdout=log,stderr=log))
            for _ in range(200):
                if bus.name_has_owner('org.kde.impanel'): break
                time.sleep(.05)
            assert bus.name_has_owner('org.kde.impanel'), 'Panel startup failed'
            # Give the Qt input context the same isolated Fcitx configuration.
            for key in ['XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','QT_QPA_PLATFORM','QT_QPA_PLATFORMTHEME',
                        'QT_STYLE_OVERRIDE','QT_IM_MODULE','XMODIFIERS']:
                os.environ[key] = env[key]
            from PyQt5.QtWidgets import QApplication, QLineEdit
            app = QApplication([])
            editor = QLineEdit()
            editor.setWindowTitle('Isolated real input mode test')
            editor.show()
            control = dbus.Interface(bus.get_object('org.kde.impanel','/org/ukui/FcitxLiquid'),'org.ukui.FcitxLiquid')
            im = dbus.Interface(bus.get_object('org.fcitx.Fcitx','/inputmethod'),'org.fcitx.Fcitx.InputMethod')
            def pump(seconds=.2):
                end = time.monotonic()+seconds
                while time.monotonic()<end:
                    app.processEvents()
                    context=GLib.MainContext.default()
                    while context.pending(): context.iteration(False)
                    time.sleep(.005)
            def key(value):
                subprocess.run(['xdotool','key',value],check=True)
                pump()
            def expect(label, description):
                for _ in range(100):
                    state=control.Status()
                    if state['trayLabel']==label:
                        print('PASS:',description,dict(state)['trayTooltip']);return
                    pump(.05)
                raise AssertionError((description,dict(state),im.GetCurrentState(),im.GetCurrentIM(),properties))
            pump()
            subprocess.run(['xdotool','windowfocus',str(int(editor.winId()))],check=True)
            editor.setFocus();pump(.5)
            im.SetCurrentIM('rime');im.ActivateIM();pump(1)
            expect('中','real Rime activation')
            key('Shift_L');expect('a','Shift enters ASCII')
            key('Shift_L');expect('中','Shift returns to Chinese')
            key('Caps_Lock');expect('A','Caps Lock enters uppercase')
            key('Caps_Lock');expect('中','Caps Lock restores Chinese')
            key('ctrl+space');expect('a','Ctrl+Space switches to keyboard')
            assert im.GetCurrentIM()=='fcitx-keyboard-us'
            key('ctrl+space');expect('中','Ctrl+Space returns to Rime')
            assert im.GetCurrentIM()=='rime'
            assert control.Status()['inputEnabled']
            assert editor.text()=='',editor.text()
            def candidate_count(count):
                for _ in range(160):
                    if int(control.Status()['candidateCount'])==count:return
                    pump(.05)
                raise AssertionError(('candidate count',count,dict(control.Status())))
            key('a');candidate_count(6);key('3');assert editor.text()=='丙',editor.text()
            editor.clear();key('a');candidate_count(6);key('Next');key('1');assert editor.text()=='庚',editor.text()
            editor.clear()
            subprocess.run(['/usr/bin/python3',str(root/'manage.py'),'rime-page-size','9'],env=env,check=True)
            (config/'liquid-panel.conf').write_text('[Interface]\nPageSize=9\n')
            control.ReloadSettings();control.ReloadRime();pump(2)
            # Fcitx4 refreshes the Rime status property on the next input event.
            key('a');candidate_count(9);expect('中','Rime ready after redeploy')
            key('9');assert editor.text()=='壬',editor.text()
            editor.clear();key('a');candidate_count(9);key('Next');candidate_count(3);key('1');assert editor.text()=='癸',editor.text()
            print('PASS: real backend 6→9 candidates, numeric selection and next-page indices')
            editor.close()
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill();process.wait(timeout=5)
