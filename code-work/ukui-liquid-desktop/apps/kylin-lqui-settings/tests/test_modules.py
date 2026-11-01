#!/usr/bin/python3
"""Run freshly built modules on a disposable display, bus, HOME and config."""
import json, os, shlex, subprocess, sys, tempfile, time
from pathlib import Path
if '--isolated' not in sys.argv:
    raise SystemExit(subprocess.call(['xvfb-run','-a','-s','-screen 0 1920x1080x24','dbus-run-session','--',sys.executable,str(Path(__file__).resolve()),'--isolated']))
ROOT=Path(__file__).resolve().parents[3]
WORK=ROOT.parent
BUILDS=Path(os.environ['LQUI_TEST_BUILD_ROOT']) if os.environ.get('LQUI_TEST_BUILD_ROOT') else None
def build_path(name,default):return BUILDS/name if BUILDS else default
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtWidgets,QtDBus,QtTest
from backend import Backend
from main import SettingsWindow

with tempfile.TemporaryDirectory(prefix='lqui-module-fixture-') as folder:
    home=Path(folder)
    for name in ('config','data','cache','runtime','Desktop'): (home/name).mkdir()
    (home/'runtime').chmod(0o700)
    os.environ.update(XDG_RUNTIME_DIR=str(home/'runtime'),AI_SPACE_ACCOUNT_HOME_FALLBACK='0',HOME=folder,XDG_CONFIG_HOME=str(home/'config'),XDG_DATA_HOME=str(home/'data'),XDG_CACHE_HOME=str(home/'cache'),GSETTINGS_BACKEND='memory',LQUI_SETTINGS_LEGACY='1',UKUI_LIQUID_GLASS_NO_GL='1',UKUI_FENCES_GLASS_NO_GL='1',AI_SPACE_TEST_NO_BOOTSTRAP='1',QT_QPA_PLATFORMTHEME='')
    (home/'config/liquid-cli').mkdir()
    (home/'config/liquid-cli/settings.json').write_text(json.dumps({'command':shlex.quote(str(build_path('liquid-cli',WORK/'liquid-cli/build')/'cli-fixture'))+' '+shlex.quote(str(home/'cli-marker')),'directory':folder,'animation':'none'}))
    app=QtWidgets.QApplication([])
    modules={
        'ai':([str(build_path('liquid-cli',WORK/'liquid-cli/build')/'liquid-cli'),'--resident'],'org.ukui.LiquidCli'),
        'input':([str(build_path('fcitx',WORK/'fcitx-liquid-panel/build')/'fcitx-liquid-panel')],'org.kde.impanel'),
        'launcher':([str(build_path('launcher',ROOT/'apps/ukui-kaishicaidan-v2/build-v2')/'ukui-kaishicaidan-v2'),'--autostart'],'org.ukui.kaishicaidan.v2'),
        'fences':([str(build_path('fences',ROOT/'apps/ukui-fences/build')/'ukui-fences')],'org.ukui.fences'),
        'space':([str(build_path('ai-space',WORK/'ai-space/build')/'ai-space'),'--settings-service'],'io.github.xikario.AiSpace'),
    }
    processes=[];process_by_module={};report={};backend=Backend()
    def call(module,method,args):
        process=process_by_module[module];owner=QtDBus.QDBusConnection.sessionBus().interface().servicePid(modules[module][1])
        assert process.poll() is None and owner.isValid() and owner.value()==process.pid, (module,'wrong service owner')
        result=[];backend.call(module,method,args,result.append)
        deadline=time.monotonic()+6
        while not result and time.monotonic()<deadline:QtTest.QTest.qWait(20)
        assert result, (module,method,'timeout')
        return result[0]
    try:
        for module,(argv,service) in modules.items():
            process=subprocess.Popen(argv,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);processes.append(process)
            process_by_module[module]=process
            deadline=time.monotonic()+12
            bus=QtDBus.QDBusConnection.sessionBus().interface()
            while time.monotonic()<deadline:
                assert process.poll() is None,(module,'exited before registering')
                owner=bus.servicePid(service)
                if owner.isValid():
                    assert owner.value()==process.pid,(module,'another instance owns the name')
                    break
                QtTest.QTest.qWait(50)
            else:raise AssertionError((module,'service registration timeout'))
            catalog={}
            while time.monotonic()<deadline:
                catalog=call(module,'Catalog',[])
                if catalog.get('ok'):break
                QtTest.QTest.qWait(80)
            assert catalog.get('ok'),(module,catalog,process.poll())
            if module=='fences':
                iface=QtDBus.QDBusInterface(service,'/ukuiFences',service,QtDBus.QDBusConnection.sessionBus());response=iface.call('showSystemMonitorWidget');assert response.type()!=QtDBus.QDBusMessage.ErrorMessage;QtTest.QTest.qWait(100)
            report[module]={}
            for page in catalog['pages']:
                result=call(module,'Page',[page['id']])
                if not result.get('ok'):
                    QtTest.QTest.qWait(500);result=call(module,'Page',[page['id']])
                assert result.get('ok'),(module,page,result)
                fields=result.get('fields',[])
                assert fields,(module,page,'empty page')
                report[module][page['id']]={'fields':len(fields),'kinds':sorted(set(f['kind'] for f in fields)),'unnamed':[f.get('name','') for f in fields if f.get('label')=='选项']}
                for field in fields:
                    if field['kind']=='tabs':
                        for index in range(len(field['options'])):
                            switched=call(module,'Set',[page['id'],field['id'],json.dumps({'value':index})])
                            assert switched.get('ok'),(module,page,'tab failed',switched)
                            report[module][page['id']].setdefault('tabFields',[]).append(len(switched['fields']))
            if module=='space':
                interface=QtDBus.QDBusInterface(service,'/aiSpace',service,QtDBus.QDBusConnection.sessionBus());state=json.loads(interface.call('status').arguments()[0]);assert not state['visible'];assert not state['indexBusy'];assert not state.get('indexLoading',False)
                response=interface.call('showWindow');assert response.type()!=QtDBus.QDBusMessage.ErrorMessage
                QtTest.QTest.qWait(120);state=json.loads(interface.call('status').arguments()[0]);assert state['visible'],state
                assert state['x']<1920 and state['y']<1080
                assert call(module,'Page',['general']).get('fields')
                report[module]['general']['settingsServiceToVisible']=True
                interface.call('hideWindow')
        # Validate every exported scalar's no-op round trip and rejected bounds.
        # Action buttons (delete/reset/install/session operations) are never fired.
        checked_controls=0;validated=set()
        for module,pages in report.items():
            for page_id in pages:
                original=call(module,'Page',[page_id]);tabs=[f for f in original.get('fields',[]) if f['kind']=='tabs']
                cases=[None]+[(tab['id'],index) for tab in tabs for index in range(len(tab['options']))]
                for case in cases:
                    if case:assert call(module,'Set',[page_id,case[0],json.dumps({'value':case[1]})]).get('ok')
                    original=call(module,'Page',[page_id])
                    for field in original.get('fields',[]):
                        identity=(module,page_id,field['id'])
                        if identity in validated:continue
                        current=call(module,'Page',[page_id]);field=next((f for f in current.get('fields',[]) if f['id']==field['id']),None)
                        if not field or not field.get('enabled',True) or field.get('readOnly') or field.get('secret'):continue
                        if field['kind'] not in ('number','boolean','choice','text'):continue
                        if field['kind']=='choice' and (field.get('value',-1)<0 or not field.get('options') or field.get('optionsEnabled') and not field['optionsEnabled'][field['value']]):continue
                        result=call(module,'Set',[page_id,field['id'],json.dumps({'value':field['value']})])
                        assert result.get('ok'),(module,page_id,field.get('name'),result.get('error'))
                        checked_controls+=1;validated.add(identity)
                        if field['kind']=='number':
                            rejected=call(module,'Set',[page_id,field['id'],json.dumps({'value':field['max']+100000})])
                            assert not rejected.get('ok'),(module,page_id,'accepted an out-of-range value')
        # Test real async transport and a real form in the separate settings UI.
        window=SettingsWindow();rendered=0
        def ready():
            deadline=time.monotonic()+8
            while window.busy and time.monotonic()<deadline:QtTest.QTest.qWait(25)
            assert not window.busy,window.view_state()
            assert window.fields,window.view_state()
        def check_geometry():
            for width in (1040,720):
                window.resize(width,740 if width==1040 else 480);QtTest.QTest.qWait(60)
                assert window.scroll.widget().width()<=window.scroll.viewport().width(),(window.current,width,'page exceeds viewport',window.scroll.widget().width(),window.scroll.viewport().width())
                for entry in window.fields.values():
                    widget=entry['control']
                    right=widget.mapTo(window.scroll.widget(),QtCore.QPoint(widget.width(),0)).x()
                    assert right<=window.scroll.widget().width()+2,(window.current,entry['field'].get('label'),right,window.scroll.widget().width())
        for module,pages in report.items():
            for page_id in pages:
                window.open_page(module+':'+page_id);ready();check_geometry();rendered+=1
                tab=next((entry['field'] for entry in window.fields.values() if entry['field']['kind']=='tabs'),None)
                if tab:
                    for index in range(len(tab['options'])):
                        window.setFocus();window.mutate(tab['id'],index);ready();check_geometry();rendered+=1
                report[module][page_id]['rendered']=True
        # Peony's three pages use the same form renderer, without WebEngine.
        for page_id in ('general','terminal','plugins'):
            window.open_page('peony:'+page_id);ready();check_geometry();rendered+=1
        window.open_page('ai:general');ready();window.resize(1040,740);QtTest.QTest.qWait(100)
        window.grab().save('/tmp/lqui-settings-ai-real.png');window.close()
        report['audit']={'scalarRoundTrips':checked_controls,'renderedViews':rendered,'widths':[1040,720]}
        print(json.dumps(report,ensure_ascii=False,indent=2))
    finally:
        for process in processes:process.terminate()
        for process in processes:
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
