"""Private-bus fixture and runner; never connects to the desktop session bus."""
import os,subprocess,sys,tempfile,time
from pathlib import Path
if sys.argv[1]=='--fixture':
 import dbus,dbus.service,dbus.mainloop.glib
 from gi.repository import GLib
 dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
 bus=dbus.SessionBus();names=[];objects=[]
 class Panel(dbus.service.Object):
  @dbus.service.method('org.ukui.panel',in_signature='',out_signature='s',async_callbacks=('reply','error'))
  def GetPanelPosition(self,reply,error):GLib.timeout_add(3000,lambda:(reply('bottom'),False)[1])
  @dbus.service.method('org.ukui.panel',in_signature='',out_signature='av',async_callbacks=('reply','error'))
  def GetPrimaryScreenGeometry(self,reply,error):GLib.timeout_add(3000,lambda:(reply([0,0,1920,1080]),False)[1])
 class Bridge(dbus.service.Object):
  @dbus.service.method('org.ukui.LiquidPanelLauncher',in_signature='',out_signature='b',async_callbacks=('reply','error'))
  def Available(self,reply,error):GLib.timeout_add(3000,lambda:(reply(True),False)[1])
 class Lock(dbus.service.Object):
  @dbus.service.method('org.ukui.ScreenSaver',in_signature='',out_signature='b',async_callbacks=('reply','error'))
  def GetLockState(self,reply,error):GLib.timeout_add(200,lambda:(reply(False),False)[1])
 for name,kind,path in [('org.ukui.panel',Panel,'/panel/position'),('org.ukui.LiquidPanelLauncher',Bridge,'/Launcher'),('org.ukui.ScreenSaver',Lock,'/')]:
  names.append(dbus.service.BusName(name,bus));objects.append(kind(bus,path))
 Path(sys.argv[2]).touch();GLib.MainLoop().run()
else:
 with tempfile.TemporaryDirectory() as d:
  ready=Path(d)/'ready';child=subprocess.Popen(['/usr/bin/python3',__file__,'--fixture',str(ready)])
  try:
   deadline=time.monotonic()+3
   while not ready.exists() and time.monotonic()<deadline:time.sleep(.02)
   if not ready.exists():raise RuntimeError('fixture not ready')
   raise SystemExit(subprocess.call(sys.argv[1:]))
  finally:child.terminate();child.wait(timeout=3)
