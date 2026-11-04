#!/usr/bin/python3
"""Small one-shot adapter; reuse Peony Manager validators, no WebEngine process."""
import json
import os
import subprocess
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
MODEL_VALUES=['','gpt-6-luna','gpt-6.1-sol','gpt-6.1-terra','gpt-6-astra']
EFFORT_VALUES=['','none','minimal','low','medium','high','xhigh','max','ultra']

def field(fid,label,kind,value=None,**extra):
    return dict(id=fid,name=fid,label=label,kind=kind,value=value,enabled=True,help=label,**extra)

def catalog():
    return {'ok':True,'module':'peony','pages':[{'id':'general','title':'文件管理'},{'id':'terminal','title':'Peony 终端'},{'id':'plugins','title':'Peony 扩展'}]}

def page(name):
    fields=[];groups=[]
    def group(key,title,order,columns=1):groups.append(dict(key=key,title=title,order=order,columns=columns))
    def annotate(item,key,order,role='control',**extra):item['ui']=dict(group=key,order=order,role=role,**extra);return item
    if name=='general':
        group('menus','菜单与终端',10,2);group('files','文件显示',20,2)
        data=gsettings_store.get_all()
        for index,(key,label) in enumerate(BOOLEAN_LABELS.items()):
            if key in data['settings']:fields.append(annotate(field('global:'+key,label,'boolean',data['settings'][key]),'files' if key in ('show-file-extension','show-hidden-file') else 'menus',index))
        if data.get('errors'):fields.append(annotate(field('errors','','note','部分系统设置不可用：'+', '.join(data['errors'])),'files',99,'warning'))
        group('defaults','',30)
        fields.append(annotate(field('restore-file-defaults','恢复菜单与文件显示默认值','action',confirm='恢复菜单与文件显示的系统默认设置？不会改变插件启用状态或删除文件。'),'defaults',0,'action'))
    elif name=='terminal':
        group('terminal','嵌入终端',10)
        data=cli_settings.read()
        for key,label,kind,limits in CLI_FIELDS:
            extra={};value=data[key]
            if kind=='number':extra.update(min=limits[0],max=limits[1],suffix=limits[2],step=1)
            elif kind=='text':extra['maxLength']=limits
            elif kind=='choice':extra['options']=limits;value=limits.index(value)
            fields.append(annotate(field('cli:'+key,label,kind,value,**extra),'terminal',len(fields)))
        fields.append(annotate(field('terminal-note','','note','设置立即保存。终端仍由用户点击时加载，关闭文件管理窗口时一起退出。'),'terminal',99,'help'))
        fields.append(annotate(field('restore-terminal-defaults','恢复终端外观默认值','action',confirm='恢复终端宽度、字体、留白和滚动历史上限？保留启动命令、名称、模型偏好及启用状态。'),'terminal',100,'action'))
        group('model','新会话默认模型',5)
        model=data.get('model','');effort=data.get('reasoningEffort','')
        fields.append(annotate(field('cli:model','默认模型','choice',MODEL_VALUES.index(model) if model in MODEL_VALUES else -1,
            options=['跟随 Codex 设置']+MODEL_VALUES[1:],editable=True,text=model or '跟随 Codex 设置'),'model',0))
        fields.append(annotate(field('cli:reasoningEffort','推理强度','choice',EFFORT_VALUES.index(effort),
            options=['跟随 Codex 设置','不推理','最低','低','中','高','很高（xhigh）','最大（max）','超高（ultra）']),'model',1))
        fields.append(annotate(field('model-note','','note','文件夹 CLI 的模型和推理强度会单独记住，不影响 Codex 桌面端。在 CLI 输入 /model，选好后按 Enter 保存；按 s 仅用于本次会话。选择“跟随 Codex 设置”则使用公共默认值。自定义启动命令不受此设置影响，可用模型由当前账号和 CLI 决定。'),'model',2,'help'))
    elif name=='plugins':
        group('intro','',0);group('plugins','已发现的扩展',10);group('maintenance','扩展维护',20)
        fields.append(annotate(field('open-manager','打开 Peony 插件管理器','action'),'intro',0,'action'))
        fields.append(annotate(field('restart-note','','note','启用状态保存后，在下次启动文件管理器时生效。不会关闭你已打开的文件窗口。'),'intro',1,'help'))
        for index,plugin in enumerate(scanner.scan_plugins()):
            def plugin_field(item,role):return annotate(item,'plugins',index,role,entity=plugin.file_name,component='plugin-row',row=plugin.file_name)
            fields.append(plugin_field(field('heading:'+plugin.file_name,'','heading',plugin.name),'label'))
            fields.append(plugin_field(field('description:'+plugin.file_name,'','note',plugin.description),'detail'))
            path=plugin_field(field('path:'+plugin.file_name,'安装路径','note',plugin.path),'detail');path['ui']['component']='file-picker';fields.append(path)
            fields.append(plugin_field(field('state:'+plugin.file_name,'','note','未安装' if not plugin.installed else '状态未知' if plugin.enabled is None else '已启用' if plugin.enabled else '已停用'),'status'))
            if plugin.installed:
                if plugin.enabled is None:fields.append(plugin_field(field('unknown:'+plugin.file_name,'','note','无法读取启用状态，请确认 Peony 设置服务可用后刷新。'),'status'))
                else:fields.append(plugin_field(field('plugin:'+plugin.file_name,'启用 '+plugin.name,'boolean',plugin.enabled),'control'))
        for key,label in [('install-copy','安装 / 更新复制路径扩展'),('remove-copy','卸载复制路径扩展'),('install-cli','安装 / 更新嵌入终端'),('remove-cli','卸载嵌入终端')]:
            fields.append(annotate(field(key,label,'action',confirm='确认'+label+'？' if key.startswith('remove') else ''),'maintenance',len(fields),'action',emphasis='destructive' if key.startswith('remove') else 'normal'))
    else:raise ValueError('设置页不存在')
    return {'ok':True,'module':'peony','page':name,'fields':fields,'dirty':False,'ui':{'version':1,'layout':'plugin-list' if name=='plugins' else 'grouped','groups':groups}}

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
        key=fid[4:]
        if key=='model':
            if type(value) is int and 0<=value<len(MODEL_VALUES):value=MODEL_VALUES[value]
            elif isinstance(value,str):value='' if value.strip()=='跟随 Codex 设置' else value.strip()
            else:raise ValueError('无效的模型选项')
            cli_settings.update({key:value});return page(name)
        if key=='reasoningEffort':
            if type(value) is not int or not 0<=value<len(EFFORT_VALUES):raise ValueError('无效的推理强度')
            cli_settings.update({key:EFFORT_VALUES[value]});return page(name)
        if definition['kind']=='number':
            if type(value) not in (int,float) or not float(value).is_integer():raise ValueError('需要整数')
            value=int(value)
        if definition['kind']=='choice':
            if type(value) is not int or not 0<=value<len(definition['options']):raise ValueError('无效的选项')
            value=definition['options'][value]
        cli_settings.update({key:value})
    elif fid.startswith('plugin:'):
        from gi.repository import Gio
        filename=fid[7:]
        installed={p.file_name for p in scanner.scan_plugins() if p.installed}
        if filename not in installed:raise ValueError('插件已移除，请刷新')
        source=Gio.SettingsSchemaSource.get_default();schema=source.lookup(gsettings_store.SCHEMA,True) if source else None
        if not schema:raise ValueError('系统未安装文件管理器插件设置')
        settings=Gio.Settings.new_full(schema,None,None)
        disabled=list(settings.get_strv('disabled-extensions'))
        if value:disabled=[item for item in disabled if item!=filename]
        elif filename not in disabled:disabled.append(filename)
        if not settings.set_strv('disabled-extensions',disabled):raise ValueError('系统未接受插件状态')
        Gio.Settings.sync()
        if filename==cli_settings.FILE_NAME:
            cli_settings.update({'enabled':value})
    else:raise ValueError('此项目不可写入')
    return page(name)

def activate(name,fid):
    if name=='general' and fid=='restore-file-defaults':
        available=gsettings_store.get_all()['settings']
        gsettings_store.reset_boolean_defaults([key for key in BOOLEAN_LABELS if key in available])
        return page(name)
    if name=='terminal' and fid=='restore-terminal-defaults':
        defaults=cli_settings.defaults()
        cli_settings.update({key:defaults[key] for key in ('widthPercent','fontSize','historyLines','followSystemFont','fontFamily','contentPadding')});return page(name)
    allowed={'install-copy':operations.install_copy_path_plugin,'remove-copy':operations.remove_copy_path_plugin,'install-cli':operations.install_cli_panel_plugin,'remove-cli':operations.remove_cli_panel_plugin}
    if name!='plugins':raise ValueError('无效操作')
    if fid=='open-manager':
        if not (BASE/'peony_manager.html').is_file():raise ValueError('插件管理器界面未安装，请重新安装 Kylin-LQUI 设置。')
        subprocess.Popen([sys.executable,str(BASE/'app/main.py')],env=dict(os.environ,LQUI_SETTINGS_LEGACY='1'),
                         stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
        return page(name)
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
