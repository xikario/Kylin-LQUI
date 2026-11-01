#!/usr/bin/python3
"""Read-only status, never infer enabled/startup from process existence."""
import configparser
import json
import os
import sys
from pathlib import Path
from appconfig import ROOT,RUNTIME
sys.path.insert(0,str(RUNTIME/'integration'))
from desktop_status import snapshot
FILES={
'fences':['ukui-fences.desktop'], 'launcher':['ukui-kaishicaidan.desktop'],
'panel':['ukui-panel-liquid-session.desktop'], 'ai-station':['kylin-lqui-ai-station.desktop'],
'input-ui':['fcitx-liquid-panel.desktop'], 'ai-space':['ai-space.desktop'],
'native-desktop':['peony-desktop.desktop'],
}

def startup(component):
    if component not in FILES:return '系统会话管理' if component in ('compositor','input-engine') else '按需打开'
    config=Path(os.environ.get('XDG_CONFIG_HOME',str(Path.home()/'.config')))
    enabled=False;found=False
    for filename in FILES[component]:
        choices=[config/'autostart'/filename,Path('/etc/xdg/autostart')/filename]
        path=next((p for p in choices if p.is_file()),None)
        if not path:continue
        found=True;parser=configparser.ConfigParser(interpolation=None)
        try:
            parser.read(str(path));entry=parser['Desktop Entry']
            enabled|=entry.get('Hidden','false').lower()!='true' and entry.get('X-GNOME-Autostart-enabled','true').lower()!='false'
        except (OSError,configparser.Error,KeyError):return '配置不可读取'
    return '已启用' if enabled else '已关闭' if found else '未设置'

def main():
    data=snapshot(source_root=ROOT)
    states={'control-center':'随任务栏控制区开关启用','panel':'随原生 Panel 宿主加载','fences':'桌面与小组件分别配置','launcher':'独立菜单，无总开关','ai-station':'按需连接模型','input-ui':'候选前端；保留 Fcitx 引擎','peony-manager':'扩展分别启用','ai-space':'索引与启动分别配置','native-desktop':'备用桌面提供者','compositor':'系统必需服务','input-engine':'系统必需服务'}
    for component in data['components']:
        component['startupState']=startup(component['id']);component['enabledState']=states.get(component['id'],'未确认')
    try:
        from gi.repository import Gio,GLib
        bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
        result=bus.call_sync('org.ukui.fences','/ukuiFences','org.ukui.fences','frameworkModuleStates',None,None,Gio.DBusCallFlags.NO_AUTO_START,500,None).unpack()[0]
        for component in data['components']:
            if component['id']=='fences':component['features']=json.loads(result)
    except Exception:pass
    print(json.dumps(data,ensure_ascii=False))
if __name__=='__main__':main()
