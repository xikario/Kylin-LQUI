#!/usr/bin/python3
"""Actual OEM panel, private bus/display/config; never starts real casting."""
import json,os,shutil,signal,subprocess,tempfile,time
from pathlib import Path
import dbus
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/artifacts'/('appdb-timeout'+('-native' if os.environ.get('TEST_NATIVE') else '')+('-guard' if os.environ.get('TEST_GUARD') else '')+('-fallback-call' if os.environ.get('TEST_PRIMARY_EMPTY') else ''));OUT.mkdir(parents=True,exist_ok=True)
assert os.environ.get('LQUI_OFFLINE_ISOLATED')=='1'
bus=dbus.SessionBus();children=[]
def spawn(cmd,env,log):
 p=subprocess.Popen(cmd,env=env,stdout=open(OUT/log,'w'),stderr=subprocess.STDOUT,start_new_session=True);children.append(p);return p
with tempfile.TemporaryDirectory(prefix='lqui-offline-panel-') as directory:
 t=Path(directory);home=Path.home()
 for name in ('config/ukui','cache','data','log'):(t/name).mkdir(parents=True)
 for name in ('panel.conf','panel-commission.ini','liquid-panel.ini'):
  src=home/'.config/ukui'/name
  if src.exists():shutil.copy2(src,t/'config/ukui'/name)
 flag=t/'slow';fixture=(ROOT/'tests/system_items_fixture.py').read_text()
 fixture=fixture.replace('import os,dbus,','import time\nfrom pathlib import Path\nimport os,dbus,')
 marker=" @dbus.service.method('org.freedesktop.DBus.Properties',in_signature='s',out_signature='a{sv}')"
 fixture=fixture.replace(marker," @dbus.service.signal('org.kde.StatusNotifierItem',signature='')\n def NewIcon(self):pass\n"+marker,1)
 fixture=fixture.replace(" def GetAll(self,interface):\n  return {'Category'", " def GetAll(self,interface):\n  if self.title=='hidden-fixture-8' and Path(os.environ['SLOW_TRAY_FLAG']).exists():print('INJECTED delayed tray reply',flush=True);time.sleep(4)\n  return {'Category'",1)
 fixture=fixture.replace('GLib.MainLoop().run()',"""def notify():
 if Path(os.environ['SLOW_TRAY_FLAG']).exists():items[-1].NewIcon();return False
 return True
GLib.timeout_add(100,notify)
def storm():
 if Path(os.environ['TRAY_STORM_FLAG']).exists():
  for item in items:item.NewIcon()
 return True
GLib.timeout_add(50,storm)
GLib.MainLoop().run()""")
 (t/'tray.py').write_text(fixture)
 env=dict(os.environ,XDG_CONFIG_HOME=str(home/'.config'),XDG_CACHE_HOME=str(home/'.cache'),XDG_DATA_HOME=str(t/'data'),GSETTINGS_BACKEND='memory',QT_PLUGIN_PATH=str(ROOT/'build/plugins'),SLOW_TRAY_FLAG=str(flag),TRAY_STORM_FLAG=str(t/'storm'))
 command=['bwrap','--die-with-parent','--ro-bind','/','/','--dev-bind','/dev','/dev','--proc','/proc','--bind','/tmp','/tmp','--bind',str(t/'config'),str(home/'.config'),'--bind',str(t/'cache'),str(home/'.cache'),'--bind',str(t/'log'),str(home/'.log'),'/usr/bin/ukui-panel','-style','ukuiliquidpanel']
 results=[]
 def measure(phase,seconds):
  start=time.monotonic();samples=[]
  while time.monotonic()-start<seconds:
   before=time.monotonic()
   try:
    bus.call_blocking('org.ukui.panel','/panel/position','org.ukui.panel','GetPanelPosition','',(),timeout=.8)
    result='ok'
   except dbus.DBusException as error:result=error.get_dbus_name()
   samples.append({'ms':round((time.monotonic()-before)*1000),'result':result});time.sleep(.15)
  record={'phase':phase,'samples':len(samples),'timeouts':sum(s['result']!='ok' for s in samples),'max_reply_ms':max(s['ms'] for s in samples),'details':samples}
  results.append(record);print(json.dumps({k:v for k,v in record.items() if k!='details'}),flush=True)
 try:
  spawn(['/usr/bin/python3',str(t/'tray.py')],env,'tray.log');time.sleep(.4)
  appdb=(ROOT/'tests/oem_appdb_fixture.py').read_text()
  appdb=appdb.replace('import dbus,','import time\nimport dbus,',1)
  appdb=appdb.replace("  ident='grouped'", "  if (Path(os.environ['LAYOUT_APP_DATA']).parent/'slow-appdb').exists():print('delaying app lookup',flush=True);time.sleep(8)\n  ident='grouped'")
  # A second service is the OEM fallback lookup. Exercise its success and
  # independently stalled response, not merely an absent fallback service.
  extra="""
secondary=dbus.service.BusName('org.ukui.panel.daemon',bus)
class Secondary(dbus.service.Object):
 @dbus.service.method('org.ukui.panel.daemon',in_signature='i',out_signature='s')
 def WIDToDesktop(self,window):
  if os.environ.get('TEST_DAEMON_SLOW'):print('delaying fallback lookup',flush=True);time.sleep(8)
  path=Path(os.environ['LAYOUT_APP_DATA'])/'fallback.desktop'
  path.write_text('[Desktop Entry]\\nType=Application\\nName=Fallback fixture\\nExec=/bin/true\\nIcon=application-x-executable\\n')
  return str(path)
secondary_object=Secondary(bus,'/convert/desktopwid')
"""
  appdb=appdb.replace('app=AppDB(',extra+'\napp=AppDB(')
  if os.environ.get('TEST_PRIMARY_EMPTY'):
   appdb=appdb.replace("  if (Path(os.environ['LAYOUT_APP_DATA']).parent/'slow-appdb').exists():print","  if (Path(os.environ['LAYOUT_APP_DATA']).parent/'slow-appdb').exists():return ''\n  if (Path(os.environ['LAYOUT_APP_DATA']).parent/'slow-appdb').exists():print")
  (t/'appdb.py').write_text(appdb)
  env['LAYOUT_APP_DATA']=str(t/'data')
  spawn(['/usr/bin/python3',str(t/'appdb.py')],env,'appdb.log')
  spawn(command[:-3]+['/usr/bin/ukui-kwin_x11','--no-kactivities'],env,'kwin.log');time.sleep(2)
  if os.environ.get('TEST_NATIVE'):command=command[:-2]
  if os.environ.get('TEST_GUARD'):
   index=command.index('/usr/bin/ukui-panel');command[index:index]=['--setenv','LD_PRELOAD',str(ROOT/'build/liblqui-panel-dbus-guard.so')]
  panel=spawn(command,env,'panel.log')
  for _ in range(100):
   if bus.name_has_owner('org.ukui.panel'):break
   if panel.poll() is not None:raise RuntimeError('panel exited')
   time.sleep(.1)
  else:raise RuntimeError('panel not ready')
  native_pid=int(bus.call_blocking('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetConnectionUnixProcessID','s',(bus.get_name_owner('org.ukui.panel'),)))
  time.sleep(1)
  healthy_script="from PyQt5.QtWidgets import QApplication,QWidget;from PyQt5.QtCore import QTimer;app=QApplication([]);w=QWidget();w.setWindowTitle('Healthy app lookup');w.resize(300,200);w.show();QTimer.singleShot(17000,app.quit);app.exec_()"
  spawn(['/usr/bin/python3','-c',healthy_script],env,'healthy-window.log')
  measure('baseline',2)
  assert list((t/'data').glob('layout-app-*.desktop')), 'Normal app identification did not complete'

  (t/'slow-appdb').touch()
  script="""from PyQt5.QtWidgets import QApplication,QWidget
from PyQt5.QtCore import QTimer
app=QApplication([]);w=QWidget();w.setWindowTitle('Isolated application lookup fixture');w.resize(500,300);w.show();QTimer.singleShot(16000,app.quit);app.exec_()
"""
  spawn(['/usr/bin/python3','-c',script],env,'window.log')
  measure('native-appdb-reply-delayed-eight-seconds',3)
  if results[-1]['timeouts']:
   stack=subprocess.run(['gdb','-nx','-nh','-batch','-ex','set sysroot /','-ex','file /usr/bin/ukui-panel','-ex','attach '+str(native_pid),'-ex','set pagination off','-ex','thread apply all bt 18','-ex','detach'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=8)
   (OUT/'blocked-panel-stack.txt').write_text(stack.stdout)
  measure('after-appdb-reply',10)
  (OUT/'results.json').write_text(json.dumps(results,indent=2))
  if os.environ.get('TEST_GUARD'):
   assert all(r['timeouts']==0 for r in results), 'Guarded panel failed to dispatch GUI requests'
   lookup_log=(t/'log/ukui-panel.log').read_text(errors='replace')
   if os.environ.get('TEST_DAEMON_SLOW'):assert any('LquiPanelDbusGuard' in line and 'org.ukui.panel.daemon' in line for line in lookup_log.splitlines())
   assert 'LquiPanelDbusGuard' in lookup_log, 'Delayed lookup did not exercise timeout guard'
   assert 'liblqui-panel-dbus-guard.so' in Path('/proc',str(native_pid),'maps').read_text()
  else:
   assert results[1]['timeouts']>0, 'Unguarded OEM blocking path did not reproduce'

  shutil.copy2(t/'log/ukui-panel.log',OUT/'native-panel.log')
 finally:
  for child in reversed(children):
   if child.poll() is None:
    os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
