#!/usr/bin/python3
"""Small one-shot adapter; reuse Peony Manager validators, no WebEngine process."""
import json
import sys
from pathlib import Path
from appconfig import ROOT,RUNTIME
BASE=RUNTIME/'peony-manager' if RUNTIME!=ROOT else ROOT.parent.parent/'peony-manager'
sys.path.insert(0,str(BASE))
from app import cli_settings, gsettings_store, scanner, operations

BOOLEAN_LABELS={
    'enable-peony-right-menu':'启用 Peony 右键扩展',
    'enable-desktop-file-menu':'启用桌面文件右键菜单',
    'enable-desktop-background-menu':'启用桌面背景右键菜单',
    'show-open-terminal':'显示“打开终端”',
    'show-file-extension':'显示文件扩展名',
    'show-hidden-file':'显示隐藏文件',
}
CLI_FIELDS=[('enabled','启用嵌入终端','boolean',None),('name','终端名称','text',80),('menuLabel','右键菜单名称','text',120),('command','启动命令','text',4096),('widthPercent','终端宽度','number',(25,65,' %')),('fontSize','字体大小','number',(8,24,' pt')),('historyLines','滚动历史','number',(100,10000,' 行')),('followSystemFont','跟随系统字体','boolean',None),('fontFamily','终端字体','choice',['Sarasa Fixed SC','Noto Sans Mono CJK SC']),('contentPadding','内容边距','number',(0,24,' px'))]

def field(fid,label,kind,value=None,**extra):
    return dict(id=fid,name=fid,label=label,kind=kind,value=value,enabled=True,help=label,**extra)

def catalog():
    return {'ok':True,'module':'peony','pages':[{'id':'general','title':'文件管理'},{'id':'terminal','title':'Peony 终端'},{'id':'plugins','title':'Peony 扩展'}]}

def page(name):
    fields=[]
    if name=='general':
        data=gsettings_store.get_all()
        for key,label in BOOLEAN_LABELS.items():
            if key in data['settings']:fields.append(field('global:'+key,label,'boolean',data['settings'][key]))
        if data.get('errors'):fields.append(field('errors','','note','部分系统设置不可用：'+', '.join(data['errors'])))
    elif name=='terminal':
        data=cli_settings.read()
        for key,label,kind,limits in CLI_FIELDS:
            extra={};value=data[key]
            if kind=='number':extra.update(min=limits[0],max=limits[1],suffix=limits[2],step=1)
            elif kind=='text':extra['maxLength']=limits
            elif kind=='choice':extra['options']=limits;value=limits.index(value)
            fields.append(field('cli:'+key,label,kind,value,**extra))
        fields.append(field('terminal-note','','note','设置立即保存。终端仍由用户点击时加载，关闭文件管理窗口时一起退出。'))
    elif name=='plugins':
        fields.append(field('restart-note','','note','启用状态保存后，在下次启动文件管理器时生效。不会关闭你已打开的文件窗口。'))
        for plugin in scanner.scan_plugins():
            fields.append(field('heading:'+plugin.file_name,'','heading',plugin.name))
            fields.append(field('description:'+plugin.file_name,'','note',plugin.description+'\n'+('已安装' if plugin.installed else '未安装')+' · '+plugin.path))
            if plugin.installed:
                if plugin.enabled is None:fields.append(field('unknown:'+plugin.file_name,'','note','无法读取启用状态，请确认 Peony 设置服务可用后刷新。'))
                else:fields.append(field('plugin:'+plugin.file_name,'启用 '+plugin.name,'boolean',plugin.enabled))
        for key,label in [('install-copy','安装 / 更新复制路径扩展'),('choose-copy','选择复制路径扩展文件…'),('remove-copy','卸载复制路径扩展'),('install-cli','安装 / 更新嵌入终端'),('remove-cli','卸载嵌入终端')]:
            fields.append(field(key,label,'action',confirm='确认'+label+'？' if key.startswith('remove') else ''))
    else:raise ValueError('设置页不存在')
    return {'ok':True,'module':'peony','page':name,'fields':fields,'dirty':False}

def set_value(name,fid,encoded):
    value=json.loads(encoded)['value']
    allowed={f['id']:f for f in page(name)['fields']}
    if fid not in allowed:raise ValueError('设置项已经变化，请刷新')
    definition=allowed[fid]
    if definition['kind']=='boolean' and type(value) is not bool:raise ValueError('需要开关值')
    if fid.startswith('global:'):
        key=fid[7:]
        if key not in BOOLEAN_LABELS:raise ValueError('无效的系统设置')
        result=gsettings_store.set(key,value)
        if not result.get('success'):raise ValueError(result.get('error','系统未接受此设置'))
    elif fid.startswith('cli:'):
        key=fid[4:];data=cli_settings.read()
        if definition['kind']=='number':
            if type(value) not in (int,float) or not float(value).is_integer():raise ValueError('需要整数')
            value=int(value)
        if definition['kind']=='choice':
            if type(value) is not int or not 0<=value<len(definition['options']):raise ValueError('无效的选项')
            value=definition['options'][value]
        data[key]=value;cli_settings.save(data)
    elif fid.startswith('plugin:'):
        from gi.repository import Gio
        filename=fid[7:]
        installed={p.file_name for p in scanner.scan_plugins() if p.installed}
        if filename not in installed:raise ValueError('插件已移除，请刷新')
        settings=Gio.Settings.new(gsettings_store.SCHEMA)
        disabled=list(settings.get_strv('disabled-extensions'))
        if value:disabled=[item for item in disabled if item!=filename]
        elif filename not in disabled:disabled.append(filename)
        if not settings.set_strv('disabled-extensions',disabled):raise ValueError('系统未接受插件状态')
        Gio.Settings.sync()
        if filename==cli_settings.FILE_NAME:
            data=cli_settings.read();data['enabled']=value;cli_settings.save(data)
    else:raise ValueError('此项目不可写入')
    return page(name)

def activate(name,fid):
    allowed={'install-copy':operations.install_copy_path_plugin,'remove-copy':operations.remove_copy_path_plugin,'install-cli':operations.install_cli_panel_plugin,'remove-cli':operations.remove_cli_panel_plugin}
    if name!='plugins':raise ValueError('无效操作')
    if fid=='choose-copy':
        from PyQt5.QtWidgets import QApplication,QFileDialog
        app=QApplication([])
        selected,_=QFileDialog.getOpenFileName(None,'选择复制路径扩展','','Peony 扩展 (libpeony-copy-path.so)')
        if not selected:return {'ok':True,'cancelled':True}
        result=operations.install_copy_path_plugin(selected)
    elif fid in allowed:result=allowed[fid]()
    else:raise ValueError('此操作不可用')
    if not result.get('success'):raise ValueError(result.get('error','操作失败'))
    return page(name)

def main():
    try:
        method=sys.argv[1];args=sys.argv[2:]
        functions={'Catalog':lambda:catalog(),'Page':page,'Set':set_value,'Activate':activate}
        result=functions[method](*args)
    except Exception as error:result={'ok':False,'error':str(error)}
    print(json.dumps(result,ensure_ascii=False))
if __name__=='__main__':main()
