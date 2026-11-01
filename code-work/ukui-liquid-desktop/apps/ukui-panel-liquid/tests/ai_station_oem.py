#!/usr/bin/env python3
"""Actual OEM panel + actual AI小站 on private X11/D-Bus/config; no model request."""
import ast,json,os,pathlib,re,shlex,shutil,signal,socket,struct,subprocess,tempfile,time,zlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
APP=ROOT.parents[2]/'liquid-cli'
OUT=ROOT/'build/artifacts'/os.environ.get('AI_STATION_OEM_OUTPUT','ai-station-oem');OUT.mkdir(parents=True,exist_ok=True)
assert os.environ.get('AI_STATION_ISOLATED')=='1'
with tempfile.TemporaryDirectory(prefix='ai-station-oem-') as d:
 t=pathlib.Path(d);home=pathlib.Path.home()
 for n in ('config/ukui','cache','data','log'):(t/n).mkdir(parents=True,exist_ok=True)
 for n in ('panel.conf','panel-commission.ini','liquid-panel.ini'):
  src=home/'.config/ukui'/n
  if src.is_file():shutil.copy2(src,t/'config/ukui'/n)
 # Only fixture messages/config; never mount real conversation files.
 (t/'config/liquid-cli').mkdir();(t/'config/liquid-cli/settings.json').write_text(json.dumps({'animation':'none','command':shlex.quote(str(APP/'build/cli-fixture'))+' '+shlex.quote(str(t/'catalog-probe'))}))
 endpoint=t/'probe.sock'
 env=dict(os.environ,XDG_CONFIG_HOME=str(home/'.config'),XDG_CACHE_HOME=str(home/'.cache'),XDG_DATA_HOME=str(t/'data'),GSETTINGS_BACKEND='memory',QT_PLUGIN_PATH=str(ROOT/'build/test-plugins')+':'+str(ROOT/'build/plugins'),UKUI_PANEL_LAYOUT_PROBE_SOCKET=str(endpoint),UKUI_AI_STATION_LAUNCHER=str(APP/'build/liquid-cli'))
 cmd=['bwrap','--die-with-parent','--ro-bind','/','/','--dev-bind','/dev','/dev','--proc','/proc','--bind','/tmp','/tmp','--bind',str(t/'config'),str(home/'.config'),'--bind',str(t/'cache'),str(home/'.cache'),'--bind',str(t/'log'),str(home/'.log'),'/usr/bin/ukui-panel','-style','layoutprobe']
 def call(action='state',**kw):
  with socket.socket(socket.AF_UNIX) as s:
   s.settimeout(15);s.connect(str(endpoint));s.sendall((json.dumps(dict(action=action,**kw))+'\n').encode());r=json.loads(s.makefile('rb').readline())
  assert r['ok'],(action,r);return r
 def find(w,name):
  if w['name']==name:return w
  for c in w['children']:
   x=find(c,name)
   if x:return x
 def state():
  raw=subprocess.check_output(['gdbus','call','--session','--dest','org.ukui.LiquidCli','--object-path','/Cli','--method','org.ukui.LiquidCli.State'],text=True)
  return json.loads(ast.literal_eval(raw)[0])
 fixture=subprocess.Popen(['/usr/bin/python3',str(ROOT/'tests/oem_appdb_fixture.py')],env=dict(os.environ,LAYOUT_APP_DATA=str(t/'data')),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 wm=subprocess.Popen(['/usr/bin/ukui-kwin_x11','--no-kactivities'],stdout=open(OUT/'kwin.log','w'),stderr=subprocess.STDOUT)
 process=subprocess.Popen(cmd,env=env,stdout=open(OUT/'stdout.log','w'),stderr=subprocess.STDOUT,start_new_session=True)
 try:
  for _ in range(100):
   if endpoint.exists():break
   assert process.poll() is None,'OEM exited';time.sleep(.1)
  time.sleep(2)
  r=call();(OUT/'tree.json').write_text(json.dumps(r,ensure_ascii=False,indent=2))
  cluster=find(r['panel'],'aiStationCluster');orb=find(r['panel'],'aiStationOrb');calendar=find(r['panel'],'Calendar');start=find(r['panel'],'StartMenuButton')
  corner=next((w for w in r['windows'] if w['name']=='aiStationDesktopCorner'),None);desktop=find(r['panel'],'ShowDesktop')
  assert cluster and cluster['visible'] and desktop and corner['visible'], (cluster,corner)
  assert calendar['x']+calendar['w']<=orb['x'],(calendar,orb,cluster)
  left=start['x']+start['w']/2-r['panel']['x'];right=r['panel']['x']+r['panel']['w']-orb['x']-orb['w']/2
  assert abs(left-right)<=2 and orb['w']==start['w'] and orb['h']==start['h'],(left,right,orb,start)
  assert 200<=cluster['w']<300 and desktop['w']>=8,(cluster,desktop)
  print('PASS native metrics -> calendar -> AI; left/right orb cells mirror each other',flush=True)
  tree=subprocess.check_output(['xwininfo','-root','-tree'],text=True)
  line=next(line for line in tree.splitlines() if '"显示桌面"' in line);xid=re.search(r'0x[0-9a-f]+',line).group()
  def info(xid):
   text=subprocess.check_output(['xwininfo','-id',xid],text=True)
   return {key:int(re.search(r'^\s*'+re.escape(key)+r':\s*(-?\d+)',text,re.M).group(1)) for key in ['Absolute upper-left X','Absolute upper-left Y','Width','Height']}
  actual=info(xid);root=info('0x'+re.search(r'Window id: 0x([0-9a-f]+)',subprocess.check_output(['xwininfo','-root'],text=True)).group(1))
  assert actual['Absolute upper-left X']+actual['Width']==root['Width'] and actual['Absolute upper-left Y']+actual['Height']==root['Height'],(actual,root)
  print('PASS hot corner reaches the physical bottom/right pixels at fractional DPI',flush=True)

  size=r['panel']['w'];position=(r['panel']['x'],r['panel']['y']);
  for _ in range(4):
   r=call('realign');assert r['panel']['w']==size
  r=call(wait=4500);assert r['panel']['w']==size and (r['panel']['x'],r['panel']['y'])==position
  assert len(r['events'])<12,r['events']
  print('PASS realign + changing metrics preserve panel geometry',flush=True)
  call('applications',count=1)
  def showing():return subprocess.check_output(['xprop','-root','_NET_SHOWING_DESKTOP'],text=True).strip().endswith('= 1')
  assert not showing();call('click',name='aiStationDesktopCorner',wait=500);assert showing()
  call('click',name='aiStationDesktopCorner',wait=500);assert not showing()
  call('applications',count=0)
  print('PASS EWMH screen-corner show-desktop toggles and restores real windows',flush=True)
  call('capture',name='BackgroundWidget',path=str(t/'panel.png'));shutil.copy2(t/'panel.png',OUT/'panel.png')
  for attempt in range(2):
   call('orb-settings',wait=1500);p=state();assert p['settingsVisible'] and not p['sessionRunning'],p
   assert abs(p['settingsX']+p['settingsWidth']-orb['x']-orb['w'])<=2 and p['settingsY']+p['settingsHeight']<orb['y'],(p,orb)
   (OUT/f'settings-state-{attempt}.json').write_text(json.dumps(p,ensure_ascii=False,indent=2))
   managed=subprocess.check_output(['wmctrl','-lp'],text=True)
   assert any(int(line.split()[0],16)==int(p['settingsWindowId']) for line in managed.splitlines()),managed
   identity=subprocess.check_output(['xprop','-id',hex(int(p['settingsWindowId'])),'_KDE_NET_WM_DESKTOP_FILE','WM_CLASS'],text=True)
   assert '_KDE_NET_WM_DESKTOP_FILE(UTF8_STRING) = "liquid-cli"' in identity,identity

   if attempt==0:first_settings=p['settingsWindowId']
   else:assert p['settingsWindowId']==first_settings
  print('PASS right-click settings cold/reused process stays aligned above the unchanged orb',flush=True)
  subprocess.run([str(APP/'build/liquid-cli'),'--quit'],check=True,stdout=subprocess.DEVNULL);time.sleep(.4)
  call('click',name='aiStationOrb',wait=1800);a=state();assert a['expanded'] and a['panelHosted'] and not a['edgeVisible'] and not a['sessionRunning'],a
  assert a['y']+a['height']<orb['y'],(a,orb)
  print('PASS orb cold-start opens anchored chat; no duplicate edge or Codex backend',flush=True)
  call('click',name='aiStationOrb',wait=350);assert not state()['expanded']
  call('click',name='aiStationOrb',wait=350);assert state()['expanded']
  print('PASS orb toggles live chat through D-Bus',flush=True)
  subprocess.run([str(APP/'build/liquid-cli'),'--quit'],check=True,stdout=subprocess.DEVNULL);time.sleep(.4)
  call('orb-drop',text='拖入的文字',wait=1800);drop=state();assert drop['expanded'] and drop['draftCharacters']>=len('拖入的文字') and not drop['sessionRunning'] and drop['messages']==0,drop
  sheet=t/'录入 资料.xlsx';sheet.write_bytes(b'fixture');call('orb-drop',files=[str(sheet),str(t)],wait=600);assert state()['draftCharacters']>drop['draftCharacters']
  def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
  picture=t/'图片.png';picture.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',24,20,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\x00'+bytes([30,180,210])*24)*20))+chunk(b'IEND',b''))
  call('orb-drop',image=str(picture),wait=700);assert state()['draftImages']==1
  call('orb-drop',files=[str(picture)],wait=700);drop=state();assert drop['draftImages']==2 and not drop['sessionRunning'] and drop['messages']==0,drop
  assert not list((t/'cache/ai-station/drops').glob('*.json')) and not list((t/'cache/ai-station/drops').glob('*.png'))
  print('PASS orb file/text/image drops: cold launch, preserve draft, absolute paths, attachments, no send/backend',flush=True)
  # Exercise the actual Xdnd protocol, including a source preferring Move.
  dragEnv=dict(os.environ);dragEnv.pop('QT_PLUGIN_PATH',None)
  source=subprocess.Popen(['/usr/bin/python3',str(ROOT/'tests/xdnd_source.py'),str(picture)],env=dragEnv,stdout=subprocess.PIPE,stderr=open(OUT/'xdnd-source.log','w'),text=True)
  try:
   coords=json.loads(source.stdout.readline());nativePanel=info(hex(int(r['winId'])));scale=nativePanel['Width']/r['panel']['w']
   before=state()['draftImages'];subprocess.run(['gdbus','call','--session','--dest','org.ukui.LiquidCli','--object-path','/Cli','--method','org.ukui.LiquidCli.Hide'],check=True,stdout=subprocess.DEVNULL);time.sleep(.3)
   subprocess.run(['xdotool','mousemove',str(round(coords['x'])),str(round(coords['y'])),'mousedown','1'],check=True)
   subprocess.run(['xdotool','mousemove',str(round(coords['x']+25)),str(round(coords['y']))],check=True);time.sleep(.2)
   subprocess.run(['xdotool','mousemove',str(round((orb['x']+orb['w']/2)*scale)),str(round((orb['y']+orb['h']/2)*scale))],check=True);time.sleep(.7)
   selection=subprocess.check_output(['/usr/bin/python3','-c',"import ctypes; x=ctypes.CDLL('libX11.so.6');x.XOpenDisplay.restype=ctypes.c_void_p;x.XInternAtom.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int];x.XInternAtom.restype=ctypes.c_ulong;x.XGetSelectionOwner.argtypes=[ctypes.c_void_p,ctypes.c_ulong];x.XGetSelectionOwner.restype=ctypes.c_ulong;d=x.XOpenDisplay(None);print(x.XGetSelectionOwner(d,x.XInternAtom(d,b'XdndSelection',0)))"],text=True).strip()
   (OUT/'offered-actions.txt').write_text(subprocess.check_output(['xprop','-id',selection,'XdndActionList'],text=True))
   subprocess.run(['xdotool','mouseup','1'],check=True);source.wait(timeout=12);time.sleep(.8)
   (OUT/'xdnd-probe.json').write_text(json.dumps({'probe':call(),'mouse':subprocess.check_output(['xdotool','getmouselocation','--shell'],text=True),'nativePanel':nativePanel,'scale':scale,'orb':orb,'source':coords},ensure_ascii=False,indent=2))
   result=json.loads(source.stdout.readline());after=state();(OUT/'real-xdnd.json').write_text(json.dumps({'source':result,'beforeImages':before,'after':after},ensure_ascii=False,indent=2))
   assert result['dropAction']==1 and picture.is_file() and after['draftImages']==before+1 and after['expanded'],(result,after)
   print('PASS actual Xdnd image drop opens AI and attaches; never acknowledges Move or removes the source',flush=True)
  finally:
   if source.poll() is None:source.terminate();source.wait(timeout=3)


  # Resident launch and repeated autostart invocations must never toggle UI.
  subprocess.run([str(APP/'build/liquid-cli'),'--quit'],check=True,stdout=subprocess.DEVNULL);time.sleep(.4)
  resident=subprocess.Popen(cmd[:-3]+[str(APP/'build/liquid-cli'),'--resident'],env=env,stdout=open(OUT/'resident.log','w'),stderr=subprocess.STDOUT)
  time.sleep(2);ready=state();assert not ready['expanded'] and not ready['sessionRunning'] and ready['glassBuilds']>0,ready
  subprocess.run([str(APP/'build/liquid-cli'),'--resident'],check=True);assert not state()['expanded']
  call('click',name='aiStationOrb',wait=350);shown=state();assert shown['expanded'] and shown['glassBuilds']==ready['glassBuilds'] and not shown['sessionRunning'],(ready,shown)
  (OUT/'resident.json').write_text(json.dumps({'ready':ready,'shown':shown},ensure_ascii=False,indent=2))
  print('PASS resident UI prewarms once; repeated startup stays hidden and opening reuses glass without starting model',flush=True)
  (OUT/'result.json').write_text(json.dumps({'cluster':cluster,'orb':orb,'start':start,'corner':corner,'desktop':desktop,'calendar':calendar,'app':a},ensure_ascii=False,indent=2))
 finally:
  subprocess.run(['gdbus','call','--session','--dest','org.ukui.LiquidCli','--object-path','/Cli','--method','org.ukui.LiquidCli.Quit'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=4)
  if (t/'log/ukui-panel.log').exists():shutil.copy2(t/'log/ukui-panel.log',OUT/'panel.log')
  os.killpg(process.pid,signal.SIGTERM)
  try:process.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL)
  wm.terminate();fixture.terminate()
