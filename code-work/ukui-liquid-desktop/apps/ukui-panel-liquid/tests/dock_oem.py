#!/usr/bin/env python3
"""Dock adaptation against the real OEM taskbar in private X11/D-Bus/config."""
import argparse, ast, configparser, json, os, pathlib, re, shutil, signal, socket, subprocess, tempfile, time

ROOT=pathlib.Path(__file__).resolve().parents[1]
BUILD=pathlib.Path(os.environ.get('LQUI_PANEL_TEST_BUILD',str(ROOT/'build')))
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=pathlib.Path,default=ROOT/'build/artifacts/dock-port')
parser.add_argument('--inspect',action='store_true')
parser.add_argument('--calendar-native',action='store_true')
parser.add_argument('--cohesive',action='store_true')
parser.add_argument('--system-items',action='store_true')
parser.add_argument('--settings-center',type=pathlib.Path)
parser.add_argument('--launcher',type=pathlib.Path)
args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
assert os.environ.get('DOCK_ISOLATED')=='1','Run on a private Xvfb and D-Bus session'
owned=subprocess.check_output(['gdbus','call','--session','--dest','org.freedesktop.DBus','--object-path','/org/freedesktop/DBus','--method','org.freedesktop.DBus.NameHasOwner','org.ukui.AiStationPanel'],text=True)
assert 'false' in owned,'Refusing the running user panel bus; use a fresh dbus-run-session'

def nodes(w):
    yield w
    for child in w.get('children',[]):yield from nodes(child)
def named(r,name):return next((w for w in nodes(r['panel']) if w['name']==name),None)
def fixed(r):
    return {name:tuple(named(r,name)[k] for k in ('x','y','w','h')) for name in ('aiStationCluster','aiStationOrb','Calendar','StartMenuButton') if named(r,name)}
def anchored(r):
    panel=r['panel'];left=panel['x'];right=left+panel['w']
    return {name:(x-left if name=='StartMenuButton' else right-x-w,y,w,h) for name,(x,y,w,h) in fixed(r).items()}
def check(ok,message):
    assert ok,message
    print('PASS',message,flush=True)

with tempfile.TemporaryDirectory(prefix='panel-dock-oem-') as directory:
    temp=pathlib.Path(directory);home=pathlib.Path.home()
    for name in ('config/ukui','cache','data','log'):(temp/name).mkdir(parents=True,exist_ok=True)
    for name in ('panel.conf','panel-commission.ini','liquid-panel.ini'):
        src=home/'.config/ukui'/name
        if src.exists():shutil.copy2(src,temp/'config/ukui'/name)
    config=configparser.ConfigParser();config.read(temp/'config/ukui/liquid-panel.ini')
    if not config.has_section('dock'):config.add_section('dock')
    config['dock']['adaptive']='true';config['dock']['clickFeedback']='true'
    with (temp/'config/ukui/liquid-panel.ini').open('w') as f:config.write(f)
    safeBin=temp/'safe-bin';safeBin.mkdir()
    for program in ('systemctl','loginctl','ukui-session','ukui-screensaver-command','mate-session-save','pkexec','sudo'):
        stub=safeBin/program;stub.write_text('#!/bin/sh\nexit 1\n');stub.chmod(0o755)
    endpoint=temp/'probe.sock'
    env=dict(os.environ,XDG_CONFIG_HOME=str(home/'.config'),XDG_CACHE_HOME=str(home/'.cache'),XDG_DATA_HOME=str(temp/'data'),
             GSETTINGS_BACKEND='memory',LQUI_SETTINGS_LEGACY='1',QT_PLUGIN_PATH=str(BUILD/'test-plugins')+':'+str(BUILD/'plugins'),
             UKUI_PANEL_LAYOUT_PROBE_SOCKET=str(endpoint),UKUI_LIQUID_GLASS_NO_GL='1',PATH=str(safeBin)+':'+os.environ['PATH'],DBUS_SYSTEM_BUS_ADDRESS='unix:path='+str(temp/'no-system-bus'))
    if args.system_items:
        control_launcher=temp/'open-control';control_log=temp/'control-open.jsonl'
        control_launcher.write_text('#!/usr/bin/python3\nimport sys,json\nwith open('+repr(str(control_log))+',"a") as f:f.write(json.dumps(sys.argv[1:])+"\\n")\n');control_launcher.chmod(0o755)
        env['LQUI_CONTROL_CENTER_LAUNCHER']=str(control_launcher)
    fixture=subprocess.Popen(['/usr/bin/python3',str(ROOT/'tests/oem_appdb_fixture.py')],env=dict(os.environ,LAYOUT_APP_DATA=str(temp/'data')),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    wm=subprocess.Popen(['/usr/bin/ukui-kwin_x11','--no-kactivities'],stdout=open(args.output/'kwin.log','w'),stderr=subprocess.STDOUT)
    command=['bwrap','--die-with-parent','--ro-bind','/','/','--dev-bind','/dev','/dev','--proc','/proc','--bind','/tmp','/tmp',
             '--bind',str(temp/'config'),str(home/'.config'),'--bind',str(temp/'cache'),str(home/'.cache'),'--bind',str(temp/'log'),str(home/'.log'),
             '/usr/bin/ukui-panel','-style','layoutprobe']
    process=subprocess.Popen(command,env=env,stdout=open(args.output/'stdout.log','w'),stderr=subprocess.STDOUT,start_new_session=True)
    observations=[]
    launcher=None;system_items=None
    def call(action='state',**kw):
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(12);s.connect(str(endpoint));s.sendall((json.dumps(dict(action=action,**kw))+'\n').encode());r=json.loads(s.makefile('rb').readline())
        observations.append(dict(action=action,**r));check(r['ok'],action);return r
    def capture(name,path):
        target=temp/path;call('capture',name=name,path=str(target));shutil.copy2(target,args.output/path)
    try:
        for _ in range(100):
            if endpoint.exists():break
            assert process.poll() is None,'panel exited';time.sleep(.1)
        time.sleep(2)
        if args.system_items:
            system_items=subprocess.Popen(['/usr/bin/python3',str(ROOT/'tests/system_items_fixture.py')],env=env);time.sleep(2)
        if args.cohesive:
            call('settings',wait=150);call('settingsTab',index=0,wait=50);call('click',name='liquidMaterialPreset_liquid',wait=200);call('settingsTab',index=1,wait=50);call('layout',enabled=True,percent=61,wait=250)
            from PIL import Image
            theme_pixels={};theme_geometry=None
            for index,key in enumerate(('color','glass','mist')):
                state=call('select',name='liquidIconTheme',index=index,wait=200)
                if theme_geometry is None:theme_geometry=fixed(state)
                check(fixed(state)==theme_geometry,'icon theme '+key+' preserves date group and orb geometry')
                for name in ('StartMenuButton','aiStationOrb'):
                    filename=key+'-'+name+'.png';capture(name,filename)
                    theme_pixels[(key,name)]=Image.open(args.output/filename).tobytes()
                capture('BackgroundWidget','theme-'+key+'.png')
            for name in ('StartMenuButton','aiStationOrb'):
                check(len({theme_pixels[(key,name)] for key in ('color','glass','mist')})==3,'all three themes update '+name+' artwork')
            call('select',name='liquidIconTheme',index=0,wait=100);call('closeSettings',wait=300)
        initial=call();
        if args.system_items:
            snapshot=fixed(initial)
            before_rapid=call(wait=500)
            rapid=call('rapidBlank',wait=1400)
            check(rapid['dock']['materialFallbacks']==before_rapid['dock']['materialFallbacks'],'rapid native blank-area clicks never replace finished glass with a placeholder')
            check(rapid['dock']['materialBuilds']<=before_rapid['dock']['materialBuilds']+1,'transient OEM press geometry is coalesced before material preparation')
            check(fixed(rapid)==snapshot,'rapid blank clicks preserve the fixed Dock geometry')
            core=[w for w in nodes(initial['panel']) if w.get('accessibleName') in ['ukui-panel_sni_QToolButton_'+name for name in ('kylin-nm','ukui-volume-control-applet-qt','ukui-power-manager-tray')]]
            check(len(core)==3 and all(w['w']==25 for w in core),'all three system controls are compact and present')
            compact_width=named(initial,'StatusNotifier')['w']
            check(named(initial,'liquidSystemCapsule')['w']==compact_width,'capsule encloses the entire tray group')
            samples=[call(wait=250) for _ in range(4)];(args.output/'control-samples.json').write_text(json.dumps(samples,ensure_ascii=False))
            check(samples[-1]['controlSyncs']-samples[0]['controlSyncs']<=4,'idle control group has no relayout or repaint loop')
            for target,page in [('ukui-panel_sni_QToolButton_kylin-nm','wifi'),('ukui-panel_sni_QToolButton_ukui-volume-control-applet-qt','sound'),('ukui-panel_sni_QToolButton_ukui-power-manager-tray','power'),('ukui-panel_sni_QToolButton_ukui-sidebar','notifications')]:
                call('click',name=target,wait=350)
                records=[json.loads(line) for line in control_log.read_text().splitlines()]
                check(records[-1][1]==page,'native '+page+' entry opens the correct control page')
            native=call('click',name='kylin-calendar-plugin_CalendarButtonText_calendar_button',wait=350)
            calendar=next(w for w in native['windows'] if w['class']=='LunarCalendarWidget')
            background=named(native,'BackgroundWidget')
            check(calendar['x']+calendar['w']==background['x']+background['w'],'full native calendar aligns with Dock right edge')
            visible_motion=[e for e in native.get('calendarEvents',[]) if e['opacity']>.05]
            check(len(visible_motion)>2 and len({e['x'] for e in visible_motion})==1 and visible_motion[0]['y']>visible_motion[-1]['y'],'calendar rises vertically with no visible sideways motion')
            (args.output/'calendar-motion.json').write_text(json.dumps(visible_motion,indent=2))
            calendar_positions=[]
            for _ in range(12):
                state=call(wait=100);cal=next(w for w in state['windows'] if w['class']=='LunarCalendarWidget');calendar_positions.append(tuple(cal[k] for k in ('x','y','w','h')))
            check(len(set(calendar_positions))==1,'native calendar remains stationary across twelve layout samples')
            call('click',name='kylin-calendar-plugin_CalendarButtonText_calendar_button',wait=150)
            popup=call('click',name='StatusNotifier_arrowBtn',wait=650)
            overflow=next(w for w in popup['windows'] if w['name']=='liquidTrayOverflow')
            scrolls=[w for w in nodes(overflow) if w['class']=='QScrollArea']
            check(bool(scrolls) and all(w['scroll']['verticalMaximum']==0 and not w['scroll']['verticalVisible'] for w in scrolls),'ordinary hidden tray has no cropped row or scrollbar')
            capture('liquidTrayOverflow','hidden-tray.png');call('click',name='StatusNotifier_arrowBtn',wait=400)
            def history_call(method):
                output=subprocess.check_output(['gdbus','call','--session','--dest','org.ukui.AiStationPanel','--object-path','/org/kylin/Lqui/Notifications','--method','org.kylin.Lqui.Notifications.'+method],text=True)
                return ast.literal_eval(output)[0] if method=='List' else output
            for text in ('first','updated'):
                subprocess.run(['gdbus','call','--session','--dest','org.ukui.Sidebar','--object-path','/org/ukui/Sidebar/notification','--method','org.ukui.Sidebar.notification.EmitFixture',text],check=True,stdout=subprocess.DEVNULL);time.sleep(.3)
            history=json.loads(history_call('List'));check(len(history)==1 and history[0]['body']=='updated','native notification signal is retained and replacement does not duplicate it')
            cache=temp/'data/kylin-lqui/notifications.json';check(cache.exists() and not cache.stat().st_mode & 0o077,'notification history has private permissions')
            history_call('Clear');check(not json.loads(history_call('List')),'clear removes captured notification history')
            disabled=call('systemControls',value=False,wait=700)
            check(named(disabled,'StatusNotifier')['w']>compact_width,'opt-out restores original tray width')
            check(named(disabled,'liquidSystemCapsule') is None,'opt-out removes the new capsule')
            restored=call('systemControls',value=True,wait=700)
            check(named(restored,'StatusNotifier')['w']==compact_width,'re-enabling compact controls recovers the same width')
            check(fixed(restored)==snapshot,'system controls preserve date ring and both orb positions')
            initial=restored
        if args.calendar_native:
            call('systemControls',value=False,wait=300);native=call('click',name='kylin-calendar-plugin_CalendarButtonText_calendar_button',wait=500)
            (args.output/'calendar-native.json').write_text(json.dumps(native,ensure_ascii=False,indent=2))
            for window in native['windows']:
                print('calendar window',window['class'],window['name'],window['title'],flush=True)
                if window['name']:capture(window['name'],'calendar-'+window['class']+'.png')
            call('click',name='kylin-calendar-plugin_CalendarButtonText_calendar_button',wait=300)
        print('Dock state:',initial.get('dock'),flush=True)
        (args.output/'initial.json').write_text(json.dumps(initial,ensure_ascii=False,indent=2))
        capture('BackgroundWidget','rest.png')
        call('hover',name='StartMenuButton',wait=500);capture('BackgroundWidget','start-hover.png');call('away',wait=500)
        if not args.inspect:
            check(initial['dock']['icon']>0 and initial['dock']['count']>0,'native taskbar uses adaptive layout')
            pages=named(initial,'TaskBar_KButtonBox');check(pages and pages['w']==0 and pages['offscreen'] and not pages['enabled'],'paired page arrows have no footprint, painting or input')
            snapshot=fixed(initial)
            def task_order(state):return [w['name'] for w in sorted(nodes(state['panel']),key=lambda w:w['x']) if w['class']=='UKUITaskButton' and w['visible']]
            order=task_order(initial)
            check(len(order)>=2,'two native task buttons available for drag regression')
            dragged=call('dragTask',name=order[0],target=order[1],wait=1200)
            check(task_order(dragged)!=order,'real pointer drag reorders native pinned tasks')
            call('shutdown',wait=0);check(process.wait(timeout=8)==0,'panel stops after saving reordered tasks')
            if endpoint.exists():endpoint.unlink()
            process=subprocess.Popen(command,env=env,stdout=open(args.output/'reorder-restart.log','w'),stderr=subprocess.STDOUT,start_new_session=True)
            for _ in range(100):
                if endpoint.exists():break
                assert process.poll() is None;time.sleep(.1)
            restarted=call(wait=1500)
            check(task_order(restarted)==task_order(dragged),'pinned order survives a native panel restart')
            call('dragTask',name=order[0],target=order[1],wait=1200)
            initial=call()
            call('settings',wait=250)
            for tab in (1,2,0,2,1):
                call('settingsTab',index=tab,wait=60)
                capture('liquidPanelAppearanceDialog','settings-tab-%s.png'%tab)
            call('closeSettings',wait=250)
            settled=call();
            expectedPositions={w['name']:(w['x'],w['w']) for w in nodes(settled['panel']) if w['class']=='UKUITaskButton'}
            for _ in range(20):
                transient=call('taskbarRealignFrame',wait=35)
                positions={w['name']:(w['x'],w['width']) for w in transient['immediateDock']}
                check(positions==expectedPositions,'native cell reset is repaired before an intermediate frame is drawn')
                check(fixed(transient)==snapshot,'native realign keeps status ring and orb geometry')
            settled=call();baselineBatches=settled['dock']['batches']
            targets=[w for w in nodes(settled['panel']) if w['class']=='UKUITaskButton' and w['visible'] and w['dockIconSize']>0]
            for target in targets[:8]+list(reversed(targets[:8])):
                moved=call('hover',name=target['name'],wait=45)
                check(fixed(moved)==snapshot,'rapid hover preserves fixed accessories')
            rested=call('away',wait=750)
            check(rested['dock']['batches']<=baselineBatches+1,'rapid hover does not repeatedly reset Dock layout')
            check(not rested['dock']['running'],'Dock settles after pointer leaves')

            buttons=[w for w in nodes(initial['panel']) if w['class']=='UKUITaskButton' and w['visible'] and w['dockIconSize']>0]
            middle=buttons[len(buttons)//2]
            hovered=call('hover',name=middle['name'],wait=350)
            check(all(abs(w['dockScale']-1)<.0001 for w in nodes(hovered['panel']) if w['class']=='UKUITaskButton'),'hover leaves every icon at its resting size')
            check(fixed(hovered)==snapshot,'hover preserves metrics/date ring and both orbs')
            capture('BackgroundWidget','hover.png')
            frames=hovered['dock']['frames']
            for target in buttons[:8]:
                moved=call('hover',name=target['name'],wait=25)
                check(moved['dock']['frames']==frames,'moving across icons starts no emphasis timer')
            # Create our own windows. Their app database fixture resolves only
            # to /bin/true, so click tests never launch a real user application.
            old_names={w['name'] for w in buttons}
            with_apps=call('applications',count=3,wait=1300)
            fake=[w for w in nodes(with_apps['panel']) if w['class']=='UKUITaskButton' and w['name'] not in old_names and w.get('dockIconSize',0)>0]
            check(bool(fake),'test windows provide a safe click target')
            target=fake[len(fake)//2]
            pressed=call('press',name=target['name'],wait=260)
            current=named(pressed,target['name'])
            others=[w for w in nodes(pressed['panel']) if w['class']=='UKUITaskButton' and w['name']!=target['name']]
            check(current['dockScale']<.94,'pressed icon has a distinct compression')
            check(all(w['dockScale']>.97 for w in others),'neighbour feedback stays much lighter than the pressed icon')
            capture('BackgroundWidget','pressed.png')
            trace=[call('release',name=target['name'],wait=65)]
            for _ in range(8):trace.append(call(wait=20))
            peak=max(r['dock']['frames'] for r in trace)
            check(peak>pressed['dock']['frames'],'release runs a short feedback animation')
            check(all(fixed(r)==fixed(with_apps) for r in trace),'click feedback does not move the fixed status group or orbs')
            settled=call('away',wait=900);idle=call(wait=400)
            check(settled['dock']['frames']==idle['dock']['frames'] and not idle['dock']['running'],'click feedback settles and stops its timer')
            check(all(abs(w['dockScale']-1)<.0001 for w in nodes(idle['panel']) if w['class']=='UKUITaskButton'),'all icons return to resting size')
            call('applications',count=0,wait=700)
            subprocess.run(['gdbus','call','--session','--dest','com.ukui.search.appdb.service','--object-path','/org/ukui/search/appDataBase/dbManager','--method','org.ukui.search.appDBManager.SetGrouped','true'],check=True,stdout=subprocess.DEVNULL)
            grouped=call('applications',count=3,grouped=True,wait=1300)
            (args.output/'grouped-debug.json').write_text(json.dumps(grouped,ensure_ascii=False,indent=2))
            sameapp=[w for w in nodes(grouped['panel']) if w['class']=='UKUITaskButton' and w.get('dockWindows')==3 and w['visible']]
            check(len(sameapp)==1,'three windows of the same application share exactly one icon and a line indicator')
            for index in (0,1,2):
                active=call('activateApplication',index=index,wait=250)
                leader=[w for w in nodes(active['panel']) if w['class']=='UKUITaskButton' and w.get('dockWindows')==3 and w['visible']]
                check(len(leader)==1 and leader[0]['dockGroupActive'],'activating group window '+str(index+1)+' highlights the shared indicator')
            capture('BackgroundWidget','grouped-three.png')
            two=call('closeFirstApplication',wait=700)
            check(len([w for w in nodes(two['panel']) if w['class']=='UKUITaskButton' and w.get('dockWindows')==2 and w['visible']])==1,'closing the visible group leader promotes another window without losing the icon')
            one=call('applications',count=1,wait=700)
            check(len([w for w in nodes(one['panel']) if w['class']=='UKUITaskButton' and w.get('dockWindows')==1 and w['visible']])>=1,'last window switches the running marker back to a dot')
            capture('BackgroundWidget','grouped-one.png')
            call('applications',count=0,wait=700)
            subprocess.run(['gdbus','call','--session','--dest','com.ukui.search.appdb.service','--object-path','/org/ukui/search/appDataBase/dbManager','--method','org.ukui.search.appDBManager.SetGrouped','false'],check=True,stdout=subprocess.DEVNULL)
            crowded=call('applications',count=35,wait=1500)
            check(not crowded['dock']['overflow'] and named(crowded,'liquidDockMore') is None,'crowded Dock shrinks icons without scrolling or a more button')
            check(fixed(crowded)==snapshot and crowded['panel']['w']==initial['panel']['w'],'default centered width and every fixed accessory stay unchanged')
            check(0<crowded['dock']['icon']<26,'icons continue shrinking below the former 26px scroll threshold')
            taskbar=lambda r:next(w for w in nodes(r['panel']) if w['class']=='UKUITaskBar')
            def all_inside(r):
                bar=taskbar(r);buttons=[w for w in nodes(r['panel']) if w['class']=='UKUITaskButton' and w['visible']]
                return bool(buttons) and len({b['y'] for b in buttons})==1 and all(b['x']>=bar['x']-1 and b['x']+b['w']<=bar['x']+bar['w']+1 for b in buttons)
            check(all_inside(crowded),'all applications fit completely in one row')
            check(taskbar(crowded)['scroll']['maximum']==0 and not taskbar(crowded)['scroll']['horizontalVisible'],'no invisible scrolling range or exposed scrollbar remains')
            settled=call(wait=700);check(settled['dock']['batches']-crowded['dock']['batches']<=2,'dense icon row settles without relayout')
            capture('BackgroundWidget','dense-icons.png')
            wheel=call('wheel',delta=-2400,wait=250)
            check(taskbar(wheel)['scroll']['value']==0 and fixed(wheel)==snapshot,'wheel input cannot page the fitted icon row')
            denser=call('applications',count=65,wait=1300)
            check(all_inside(denser) and not denser['dock']['overflow'] and denser['dock']['icon']<crowded['dock']['icon'],'additional windows continue proportional shrinking in the same space')
            # Optional expansion remains explicit and never rewrites base length.
            call('settings',wait=150);call('settingsTab',index=1,wait=100)
            expanded=call('check',name='liquidDockAutoExpand',value=True,wait=900)
            check(expanded['panel']['w']>denser['panel']['w'] and anchored(expanded)==anchored(initial),'explicit auto-expand opt-in grows only the application capacity')
            fixed_again=call('check',name='liquidDockAutoExpand',value=False,wait=500)
            check(fixed(fixed_again)==snapshot and fixed_again['panel']['w']==initial['panel']['w'],'disabling expansion restores the exact configured centered width')
            magnified=call('check',name='liquidDockHoverMagnification',value=True,wait=200)
            button=next(w for w in nodes(magnified['panel']) if w['class']=='UKUITaskButton' and w['visible'])
            hover=call('hover',name=button['name'],wait=650)
            check(any(w['dockScale']>1.1 for w in nodes(hover['panel']) if w['class']=='UKUITaskButton') and fixed(hover)==snapshot,'optional magnification enlarges icons without moving the Dock')
            idle=call(wait=350);check(idle['dock']['frames']==hover['dock']['frames'],'stationary magnification has no running frame timer')
            call('away',wait=650);call('check',name='liquidDockHoverMagnification',value=False,wait=150);call('closeSettings',wait=200)
            call('applications',count=0,wait=1200)
            after=call(wait=700);check(fixed(after)==snapshot,'closing apps restores same fixed controls')
            check(after['dock']['icon']==initial['dock']['icon'],'icon size recovers after crowding')
            disabled=call('dockEnable',value=False,wait=600);check(disabled['dock']['icon']==0 and named(disabled,'liquidDockMore') is None,'Dock effect can restore native layout')
            restored=call('dockEnable',value=True,wait=600);check(restored['dock']['icon']==initial['dock']['icon'] and fixed(restored)==snapshot,'enabling Dock again restores adaptive layout and fixed ring')
        if args.launcher:
            launcher=subprocess.Popen(command[:-3]+[str(args.launcher.resolve())],env=env,stdout=open(args.output/'launcher.log','w'),stderr=subprocess.STDOUT,start_new_session=True)
            def launcherState():
                output=subprocess.check_output(['gdbus','call','--session','--timeout','2','--dest','org.ukui.LiquidLauncherContext','--object-path','/StartButton','--method','org.ukui.LiquidLauncherContext.State'],text=True,stderr=subprocess.DEVNULL)
                return json.loads(ast.literal_eval(output)[0])
            def awaitState(predicate,message):
                deadline=time.monotonic()+10
                while time.monotonic()<deadline:
                    try:
                        state=launcherState()
                        if predicate(state):check(True,message);return state
                    except subprocess.CalledProcessError:pass
                    time.sleep(.1)
                raise AssertionError(message)
            awaitState(lambda s:s['nativeButton'] and not s['overlayVisible'] and not s['legacyWatcher'],'native start-button integration removes the transparent overlay and X watcher')
            subprocess.run(['gdbus','call','--session','--dest','com.ukui.search.appdb.service','--object-path','/org/ukui/search/appDataBase/dbManager','--method','org.ukui.search.appDBManager.SetGrouped','true'],check=True,stdout=subprocess.DEVNULL)
            call('applications',count=3,grouped=True,wait=1000)
            call('hover',name='StartMenuButton',wait=80);call('click',name='StartMenuButton',wait=450)
            awaitState(lambda s:s['menuVisible'],'real OEM start button opens the V2 menu without an overlay')
            call('click',name='StartMenuButton',wait=450)
            awaitState(lambda s:not s['menuVisible'],'real OEM start button closes the V2 menu exactly once')
            toggles=launcherState()['toggleRequests'];subprocess.run(['xdotool','click','--repeat','2','--delay','60','1'],check=True);time.sleep(.5)
            check(launcherState()['toggleRequests']==toggles+2,'two physical clicks deliver exactly two pointer commands')
            awaitState(lambda s:not s['menuVisible'],'two rapid real pointer clicks cancel without a delayed menu reappearance')
            steady=call('traceDock',value=True,wait=50)
            expected={w['name']:(w['x'],w['y'],w['w'],w['h']) for w in nodes(steady['panel']) if w['class']=='UKUITaskButton' and w['visible']}
            for delay in (35,70,120):
                subprocess.run(['xdotool','click','--repeat','12','--delay',str(delay),'1'],check=True)
            for _ in range(4):
                subprocess.run(['xdotool','mousedown','1'],check=True)
                call('realign',wait=50)
                subprocess.run(['xdotool','mouseup','1'],check=True)
                call(wait=70)
            moving=call('traceDock',value=False,wait=300)
            failures=[e for e in moving['dockMotion'] if e['name'] in expected and (e['x'],e['y'],e['w'],e['h'])!=expected[e['name']]]
            (args.output/'rapid-start-motion.json').write_text(json.dumps({'expected':expected,'events':moving['dockMotion'],'failures':failures},indent=2))
            check(not failures,'36 rapid start-menu clicks keep task icon geometry fixed during every paint and layout event')
            call('context',name='StartMenuButton',wait=450)
            awaitState(lambda s:s['contextVisible'],'native right click opens the existing launcher context menu')
            subprocess.run(['xdotool','key','Escape'],check=True);awaitState(lambda s:not s['contextVisible'],'launcher context menu closes without retaining input')
            requests=launcherState()['contextRequests'];subprocess.run(['xdotool','click','3'],check=True)
            state=awaitState(lambda s:s['contextVisible'],'real right-button input reaches the launcher context menu')
            check(state['contextRequests']==requests+1,'right press plus native context event produce only one menu request')
            subprocess.run(['xdotool','key','Escape'],check=True);awaitState(lambda s:not s['contextVisible'],'real right-click menu dismisses cleanly')
            subprocess.run(['xdotool','key','Super_L'],check=True);awaitState(lambda s:s['menuVisible'],'Super key still opens the menu with native button integration')
            subprocess.run(['xdotool','key','Super_L'],check=True);awaitState(lambda s:not s['menuVisible'],'Super key still closes the menu exactly once')
            call('shutdown',wait=0)
            check(process.wait(timeout=8)==0,'native panel shuts down cleanly with floating corner and status widgets')
            awaitState(lambda s:not s['nativeButton'] and not s['overlayVisible'],'panel exit leaves no invisible start-button click trap')
            if endpoint.exists():endpoint.unlink()
            process=subprocess.Popen(command,env=env,stdout=open(args.output/'panel-restarted.log','w'),stderr=subprocess.STDOUT,start_new_session=True)
            awaitState(lambda s:s['nativeButton'] and not s['overlayVisible'] and not s['legacyWatcher'],'panel restart automatically restores native button integration')
        print('PASS native Dock probe completed',flush=True)
        if args.settings_center:
            gui_env=dict(env,HOME=str(temp),XDG_CONFIG_HOME=str(temp/'config'),XDG_CACHE_HOME=str(temp/'cache'))
            settings=subprocess.Popen(['/usr/bin/python3',str(args.settings_center),'--page','panel:material'],env=gui_env,stdout=open(args.output/'settings-center.log','w'),stderr=subprocess.STDOUT)
            from gi.repository import Gio,GLib
            bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
            def ui_call(method,params=None):return bus.call_sync('org.kylin.Lqui.Settings','/Settings','org.kylin.Lqui.Settings',method,params,None,Gio.DBusCallFlags.NO_AUTO_START,1000,None).unpack()
            try:
                records=[]
                for page in ('material','layout','effects','material'):
                    deadline=time.monotonic()+8;state=None
                    while time.monotonic()<deadline:
                        try:
                            ui_call('Open',GLib.Variant('(s)',('panel:'+page,)));break
                        except GLib.Error:time.sleep(.1)
                    while time.monotonic()<deadline:
                        try:state=json.loads(ui_call('State')[0])
                        except GLib.Error:time.sleep(.1);continue
                        if not state['loading'] and state['fields']>0:break
                        time.sleep(.1)
                    check(state and state['page']=='panel:'+page and not state['loading'] and state['fields']>0,'real settings client opens '+page+' without a blank loading page')
                    records.append(state)
                idle_start=call(wait=600);idle_end=call(wait=1800)
                check(idle_end['dock']['batches']-idle_start['dock']['batches']<=2,'open settings window does not cause a persistent Dock layout loop')
                (args.output/'settings-routes.json').write_text(json.dumps(records,ensure_ascii=False,indent=2))
                ui_call('CloseForUpdate')
            finally:
                if settings.poll() is None:settings.terminate()
                settings.wait(timeout=5)
        if process.poll() is None:
            call('shutdown',wait=0)
            check(process.wait(timeout=8)==0,'final panel shutdown has no teardown crash')
    finally:
        if system_items:system_items.terminate();system_items.wait(timeout=4)
        if launcher and launcher.poll() is None:
            os.killpg(launcher.pid,signal.SIGTERM);launcher.wait(timeout=5)
        (args.output/'observations.json').write_text(json.dumps(observations,ensure_ascii=False,indent=2))
        if (temp/'log/ukui-panel.log').exists():shutil.copy2(temp/'log/ukui-panel.log',args.output/'panel.log')
        if process.poll() is None:
            os.killpg(process.pid,signal.SIGTERM)
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
        wm.terminate();fixture.terminate();wm.wait(timeout=5);fixture.wait(timeout=5)
