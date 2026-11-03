#!/usr/bin/python3
"""Capture only synthetic pages; no real Backend, installation, or actions."""
import argparse,json,math,os,sys,time
from pathlib import Path

def parse():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--extras',action='store_true')
    parser.add_argument('--width',type=int,default=1040);parser.add_argument('--height',type=int,default=740);parser.add_argument('--font-scale',type=float,default=1.)
    args=parser.parse_args()
    if not 720<=args.width<=4096 or not 480<=args.height<=2160 or not math.isfinite(args.font_scale) or not .75<=args.font_scale<=3:
        parser.error('Unsupported logical size/font scale')
    return args

def main():
    args=parse()  # Validate before constructing QApplication or showing anything.
    os.environ['QT_AUTO_SCREEN_SCALE_FACTOR']='0';os.environ.setdefault('QT_SCREEN_SCALE_FACTORS','1')
    from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
    QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling);QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_UseHighDpiPixmaps)
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
    from main import SettingsWindow
    from apple_fixture import Fixture,NAMES
    app=QtWidgets.QApplication([]);app.setQuitOnLastWindowClosed(False);app.setFont(QtGui.QFont('Noto Sans CJK SC',10))
    fixture=Fixture();window=SettingsWindow(fixture,font_scale=args.font_scale);window.poll.stop();args.out.mkdir(parents=True,exist_ok=True);report={'scope':'Synthetic fixtures; no real backend or writes','logicalSize':[args.width,args.height],'fontScale':args.font_scale,'pages':{}}
    try:
        for name,key in NAMES.items():
            started=time.monotonic();window.open_page(key);deadline=time.monotonic()+4
            while window.busy and time.monotonic()<deadline:QtTest.QTest.qWait(2)
            assert not window.busy
            QtTest.QTest.qWait(30);window.resize(args.width,args.height);QtTest.QTest.qWait(30)
            pixmap=window.grab();assert pixmap.save(str(args.out/(name+'.png')))
            report['pages'][name]={'dpr':window.devicePixelRatioF(),'pixelSize':[pixmap.width(),pixmap.height()],'fields':len(window.fields),'openReplayMs':round((time.monotonic()-started)*1000,2),'bodyPixelFont':next(e['control'].font().pixelSize() for e in window.fields.values())}
        if args.extras:
            import copy
            from ui_components import DisclosureRow,PluginRow
            def shot(name):
                QtTest.QTest.qWait(30);window.grab().save(str(args.out/(name+'.png')))
            window.open_page('fences:wallpaper');QtTest.QTest.qWait(50)
            video=copy.deepcopy(fixture.pages['fences:wallpaper']);video['dirty']=True
            for field in video['fields']:
                if field['name']=='wallpaperKind':field['value']=1
                if field['name']=='wallpaperImagePath':field['ui']['collapsible']=True;field['label']='静态回退图片'
                if field['name'] in ('videoWallpaperPath','videoWallpaperStart'):field['enabled']=True
                if field['name'] in ('wallpaperChooseImage','wallpaperClearImage','wallpaperImageMode'):field['enabled']=False
            window.receive_page(video);window.message.setText('合成失败场景：应用失败，草稿保留。');shot('wallpaper-video-failure')
            window.open_page('fences:activity');QtTest.QTest.qWait(50)
            for row in window.scroll.findChildren(DisclosureRow):row.set_expanded(True)
            shot('activity-expanded')
            window.open_page('peony:plugins');QtTest.QTest.qWait(50)
            for row in window.scroll.findChildren(PluginRow):
                if '长中文' in row.caption:row.set_expanded(True)
            shot('plugins-long-unknown')
            window.search.setText('无匹配的合成搜索词');shot('no-results');window.search.clear()
            window.open_page('fences:appearance');QtTest.QTest.qWait(50)
            window.activateWindow();QtTest.QTest.qWait(20)
            next(e['control'] for e in window.fields.values() if e['field']['name']=='fenceLiquidGlass').setFocus();shot('keyboard-focus')
            window.render_fields([{'id':'future','name':'future','kind':'future-control','label':'未知设置类型','help':'保留原字段说明，协议不认识时仍然可见。'}]);shot('unknown-control')
            report['extras']=['unsupported kind','video/failure/draft','activity expanded','long/unknown plugins','no results','keyboard focus']
        assert not any(method in ('Set','Activate') for _,method,_ in fixture.calls)
        (args.out/'capture.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    finally:window.close();window.deleteLater();QtTest.QTest.qWait(20)

if __name__=='__main__':main()
