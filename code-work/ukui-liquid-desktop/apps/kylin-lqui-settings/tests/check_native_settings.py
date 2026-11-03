#!/usr/bin/python3
"""Synthetic client on a fresh X11 display/bus/WM; never uses real Backend."""
import argparse,json,os,re,subprocess,sys,tempfile,time
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',required=True,type=Path)
parser.add_argument('--private',action='store_true',help=argparse.SUPPRESS)
args=parser.parse_args()
if not args.private:
    raise SystemExit(subprocess.call(['xvfb-run','-a','-s','-screen 0 2560x1440x24','dbus-run-session','--',sys.executable,str(Path(__file__).resolve()),'--private','--out',str(args.out)],env=dict(os.environ,LQUI_NATIVE_SETTINGS_ISOLATED='1')))

assert os.environ.get('LQUI_NATIVE_SETTINGS_ISOLATED')=='1','Use the normal entry to create an isolated session'
existing_wm=subprocess.check_output(['xprop','-root','_NET_SUPPORTING_WM_CHECK'],text=True)
assert not re.search(r'0x[1-9a-fA-F]',existing_wm),'Refusing a display with an existing window manager'

with tempfile.TemporaryDirectory(prefix='compact-settings-native-') as directory:
    root=Path(directory);args.out.mkdir(parents=True,exist_ok=True)
    for name in ('result.json','native-window.png'):(args.out/name).unlink(missing_ok=True)
    for name in ('config','data','cache'):(root/name).mkdir()
    os.environ.update(XDG_CONFIG_HOME=str(root/'config'),XDG_DATA_HOME=str(root/'data'),XDG_CACHE_HOME=str(root/'cache'),GSETTINGS_BACKEND='memory',QT_AUTO_SCREEN_SCALE_FACTOR='0',QT_SCREEN_SCALE_FACTORS='1',QT_QPA_PLATFORM='xcb',DBUS_SYSTEM_BUS_ADDRESS='unix:path='+str(root/'no-system-bus'))
    with (args.out/'wm.log').open('w') as log:
        wm=subprocess.Popen(['/usr/bin/ukui-kwin_x11','--no-kactivities'],stdout=log,stderr=subprocess.STDOUT)
        try:
            from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
            QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling)
            app=QtWidgets.QApplication([]);app.setFont(QtGui.QFont('Noto Sans CJK SC',10));app.setQuitOnLastWindowClosed(False)
            sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
            from main import SettingsWindow
            from apple_fixture import Fixture
            fixture=Fixture();window=SettingsWindow(fixture,font_scale=1);window.poll.stop()
            def settle():
                deadline=time.monotonic()+4
                while window.busy and time.monotonic()<deadline:QtTest.QTest.qWait(10)
                assert not window.busy;QtTest.QTest.qWait(100)
            def entry(name):return next(e for e in window.fields.values() if e['field'].get('name')==name)
            window.open_page('fences:appearance');settle();window.activateWindow();QtTest.QTest.qWait(250)
            assert wm.poll() is None
            deadline=time.monotonic()+5;wm_check=''
            while time.monotonic()<deadline:
                wm_check=subprocess.check_output(['xprop','-root','_NET_SUPPORTING_WM_CHECK'],text=True)
                if re.search(r'0x[1-9a-fA-F]',wm_check):break
                QtTest.QTest.qWait(50)
            wm_id=re.search(r'0x[0-9a-fA-F]+',wm_check);assert wm_id,'private native WM must publish EWMH support'
            wm_name=subprocess.check_output(['xprop','-id',wm_id.group(),'_NET_WM_NAME'],text=True)
            assert 'KWin' in wm_name or 'ukui' in wm_name.lower(),wm_name
            expose_deadline=time.monotonic()+5
            while not window.windowHandle().isExposed() and time.monotonic()<expose_deadline:QtTest.QTest.qWait(20)
            assert window.windowHandle().isExposed();QtTest.QTest.qWait(100)
            header=window.window_header;origin=QtCore.QPoint(header.width()//2,20);global_pos=header.mapToGlobal(origin);old=window.pos();delta=QtCore.QPoint(35,24)
            for kind,local,global_at,button,buttons in [(QtCore.QEvent.MouseButtonPress,origin,global_pos,QtCore.Qt.LeftButton,QtCore.Qt.LeftButton),(QtCore.QEvent.MouseMove,origin+delta,global_pos+delta,QtCore.Qt.NoButton,QtCore.Qt.LeftButton),(QtCore.QEvent.MouseButtonRelease,origin+delta,global_pos+delta,QtCore.Qt.LeftButton,QtCore.Qt.NoButton)]:
                app.sendEvent(header,QtGui.QMouseEvent(kind,local,global_at,button,buttons,QtCore.Qt.NoModifier))
            QtTest.QTest.qWait(100);assert window.pos()==old+delta
            maximize=window.findChild(QtWidgets.QToolButton,'window_maximize');maximize.click();QtTest.QTest.qWait(150);assert window.isMaximized();maximize.click();QtTest.QTest.qWait(150);assert not window.isMaximized()
            control=entry('fenceLiquidGlass')['control'];control.setFocus();QtTest.QTest.keyClick(control,QtCore.Qt.Key_Space);settle()
            writes=[c for c in fixture.calls if c[1] in ('Set','Activate')];assert len(writes)==1 and writes[0][2][1]=='runtime-fenceLiquidGlass'
            combo=entry('glassScrim')['control'];combo.showPopup();QtTest.QTest.qWait(50);assert combo.view().isVisible()
            QtTest.QTest.keyClick(combo.view(),QtCore.Qt.Key_Down);QtTest.QTest.keyClick(combo.view(),QtCore.Qt.Key_Return);settle();assert not combo.view().isVisible()
            window.help.target=combo;window.help.timer.start(20);QtTest.QTest.qWait(80);assert window.help.bubble.isVisible();window.help.cancel()
            window.open_page('peony:plugins');settle();remove=entry('remove-copy')['control'];before=len(fixture.calls)
            cancel_deadline=time.monotonic()+4
            def reject():
                modal=QtWidgets.QApplication.activeModalWidget() or QtWidgets.QApplication.modalWindow()
                if modal is not None:QtTest.QTest.keyClick(modal,QtCore.Qt.Key_Escape)
                elif time.monotonic()<cancel_deadline:QtCore.QTimer.singleShot(30,reject)
                else:raise AssertionError('native confirmation did not map')
            QtCore.QTimer.singleShot(50,reject);remove.click();QtTest.QTest.qWait(20)
            assert not any(method=='Activate' for _,method,_ in fixture.calls[before:])
            window.open_page('fences:appearance');settle()
            native=window.windowHandle().screen().grabWindow(int(window.winId()));assert not native.isNull();assert native.save(str(args.out/'native-window.png'))
            report={'verified':True,'platform':app.platformName(),'dpr':window.devicePixelRatioF(),'nativeWindowExposed':True,'privateKWin':True,'drag':True,'maximizeRestore':True,'switchSpace':True,'comboKeyboard':True,'tooltip':True,'cancelConfirmationNoActivate':True,'backend':'synthetic only','runtimeScreenDpiChange':'not exercised; separate 1.0/1.5 processes'}
            (args.out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report));window.close();window.deleteLater();QtTest.QTest.qWait(30)
        finally:
            wm.terminate()
            try:wm.wait(timeout=5)
            except subprocess.TimeoutExpired:wm.kill();wm.wait()
