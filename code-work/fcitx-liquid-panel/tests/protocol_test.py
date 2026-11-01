#!/usr/bin/env python3
"""Run ONLY under dbus-run-session + xvfb-run; never sends keys to real apps."""
import os
from pathlib import Path
import subprocess
import time
import tkinter
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

assert os.environ.get('FCITX_LIQUID_ISOLATED_TEST') == '1'
assert os.environ.get('DISPLAY') != ':0'
DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
assert not bus.name_has_owner('org.fcitx.Fcitx'), 'Refuse to use a real Fcitx bus'
owner = dbus.service.BusName('org.fcitx.Fcitx', bus, do_not_queue=True)
sentinel=tkinter.Tk()
sentinel.title('Isolated focus sentinel')
sentinel.geometry('1000x600+0+0')
sentinel.configure(background='#72899a')
sentinel.update()
sentinel.focus_force()

class Backend(dbus.service.Object):
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='b')
    def ShowLookupTable(self, visible): pass
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='b')
    def ShowPreedit(self, visible): pass
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='ss')
    def UpdatePreeditText(self, text, attrs): pass
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='i')
    def UpdateLookupTableCursor(self, cursor): pass
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='b')
    def Enable(self, enabled): pass
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='as')
    def RegisterProperties(self, properties): pass
    @dbus.service.signal('org.kde.kimpanel.inputmethod', signature='s')
    def UpdateProperty(self, property): pass

backend = Backend(bus, '/kimpanel')
received = []
bus.add_signal_receiver(lambda member=None, **kwargs: received.append(member),
                        dbus_interface='org.kde.impanel2', signal_name='PanelCreated2',
                        member_keyword='member')
bus.add_signal_receiver(lambda index: received.append(('selected', int(index))),
                        dbus_interface='org.kde.impanel', signal_name='SelectCandidate')
bus.add_signal_receiver(lambda: received.append('page-down'),
                        dbus_interface='org.kde.impanel', signal_name='LookupTablePageDown')

def pump(seconds=.15):
    end=time.monotonic()+seconds
    context=GLib.MainContext.default()
    while time.monotonic()<end:
        sentinel.update()
        while context.pending(): context.iteration(False)
        time.sleep(.005)

root = Path(__file__).resolve().parents[1]
env=os.environ.copy()
env.update(QT_QPA_PLATFORM='xcb', QT_QPA_PLATFORMTHEME='', QT_STYLE_OVERRIDE='Fusion',
           UKUI_LIQUID_GLASS_NO_GL='1')
log = root/'build/protocol-test.log'
with log.open('w') as output:
    subprocess.run(['xdotool','key','Caps_Lock'],check=True)
    panel=subprocess.Popen([str(root/'build/fcitx-liquid-panel')],env=env,stdout=output,stderr=output)
    try:
        for _ in range(100):
            if bus.name_has_owner('org.kde.impanel'): break
            assert panel.poll() is None, log.read_text()
            pump(.05)
        assert bus.name_has_owner('org.kde.impanel')
        pump()
        assert 'PanelCreated2' in received, received
        api=dbus.Interface(bus.get_object('org.kde.impanel','/org/kde/impanel'),'org.kde.impanel2')
        control=dbus.Interface(bus.get_object('org.kde.impanel','/org/ukui/FcitxLiquid'),'org.ukui.FcitxLiquid')
        chinese='/Fcitx/rime-enzh:双拼:rime-im-double_pinyin:双拼:menu'
        english='/Fcitx/rime-enzh:A:rime-latin:A:menu'
        backend.RegisterProperties(['/Fcitx/im:中州韻:fcitx-rime:中州韻:menu',chinese])
        backend.Enable(True);pump()
        assert control.Status()['trayLabel']=='A' and control.Status()['capsLock'],control.Status()
        subprocess.run(['xdotool','key','Caps_Lock'],check=True);pump()
        assert control.Status()['trayLabel']=='中',control.Status()
        backend.UpdateProperty(english);pump()
        assert control.Status()['trayLabel']=='a',control.Status()
        # Actual XKB Caps Lock events, including changes while IM is disabled.
        subprocess.run(['xdotool','key','Caps_Lock'],check=True);pump()
        assert control.Status()['trayLabel']=='A' and control.Status()['capsLock'],control.Status()
        backend.Enable(False);pump()
        assert control.Status()['trayLabel']=='A' and not control.Status()['inputEnabled']
        subprocess.run(['xdotool','key','Caps_Lock'],check=True);pump()
        assert control.Status()['trayLabel']=='a' and not control.Status()['capsLock']
        backend.UpdateProperty(chinese);backend.Enable(True);pump()
        assert control.Status()['trayLabel']=='中',control.Status()
        api.SetSpotRect(100,100,1,24)
        api.SetLookupTable(['1.','2.','3.'],['你好','您好','拟好'],['','',''],False,True,0,2)
        backend.UpdatePreeditText('ni hao',''); backend.ShowPreedit(True); backend.ShowLookupTable(True)
        pump(.5)
        initial=control.Status()
        assert initial['visible'] and initial['captures']==1, initial
        backend.UpdateLookupTableCursor(1); pump()
        unchanged=control.Status()
        assert unchanged['renders']==initial['renders'] and unchanged['captures']==1
        # On the isolated X server, verify actual mouse delivery and no focus theft.
        wid=subprocess.check_output(['xdotool','search','--name','RIME液态输入法'],text=True).split()[0]
        geometry=str(initial['geometry']); pos,size=geometry.split()
        gx,gy=map(int,pos.split(',')); width,height=map(int,size.split('x'))
        hints=subprocess.check_output(['xprop','-id',wid,'WM_HINTS'],text=True)
        assert 'input focus: False' in hints, hints
        first_pos,first_size=str(initial['first']).split()
        fx,fy=map(int,first_pos.split(',')); fw,fh=map(int,first_size.split('x'))
        assert fw > 0 and fh > 0, initial
        subprocess.run(['xdotool','mousemove',str(gx+fx+fw//2),str(gy+fy+fh//2)],check=True)
        pump()
        subprocess.run(['xdotool','click','1'],check=True)
        pump()
        assert ('selected',0) in received, received
        next_pos,next_size=str(initial['next']).split()
        nx,ny=map(int,next_pos.split(',')); nw,nh=map(int,next_size.split('x'))
        assert nw > 0 and nh > 0, initial
        subprocess.run(['xdotool','mousemove',str(gx+nx+nw//2),str(gy+ny+nh//2),'click','1'],check=True)
        pump()
        assert 'page-down' in received, received
        # Geometry changes reuse cached background and remain on-screen.
        api.SetSpotRect(1250,780,1,24)
        api.SetLookupTable(['1.','2.'],['屏幕边缘长候选词','长候选词'],['',''],True,False,0,2)
        pump()
        changed=control.Status()
        assert changed['captures']==1 and changed['visible'], changed
        pos,size=str(changed['geometry']).split(); x,y=map(int,pos.split(',')); w,h=map(int,size.split('x'))
        assert 0<=x and 0<=y and x+w<=1280 and y+h<=800, changed
        # Same rectangle after disable/enable is not resent by Fcitx.
        backend.Enable(False); pump(); assert not control.Status()['visible']
        backend.Enable(True); backend.UpdatePreeditText('hao',''); backend.ShowPreedit(True); pump()
        assert control.Status()['visible']
        control.Quit(); panel.wait(timeout=5)
        assert not bus.name_has_owner('org.kde.impanel')
        print('PASS: tray Chinese/ASCII/off-on, native Caps Lock events, handshake, v2 candidate/spot methods, legacy signals, clicks, paging, focus, cache, edges, clean exit')
    finally:
        if panel.poll() is None: panel.terminate(); panel.wait(timeout=5)
        sentinel.destroy()
