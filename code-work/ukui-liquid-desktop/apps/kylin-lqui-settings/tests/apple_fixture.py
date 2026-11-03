"""Synthetic settings protocol only. Never import/create the real Backend."""
import copy,json
from pathlib import Path
from PyQt5 import QtCore

PAGES=Path(__file__).with_name('fixtures')/'settings_pages.json'
NAMES={'appearance':'fences:appearance','wallpaper':'fences:wallpaper','activity':'fences:activity','file-manager':'peony:general','peony-plugins':'peony:plugins'}

class Fixture:
    def __init__(self):
        self.pages=json.loads(PAGES.read_text());self.calls=[];self.delay=0;self.cancel_selection=False;self.fail_apply=False;self.fail_actions=False
    def process(self,argv,callback):
        QtCore.QTimer.singleShot(0,lambda:callback({'components':[{'id':'fixture','name':'合成模块','processes':[],'enabledState':'按需启动','startupState':'未启用','pssKiB':0}]}))
    def call(self,module,method,args,callback):
        self.calls.append((module,method,copy.deepcopy(args)))
        if method=='Catalog':
            pages=[{'id':key.split(':',1)[1],'title':value['title']} for key,value in self.pages.items() if key.startswith(module+':')]
            result={'ok':bool(pages),'pages':pages}
        elif method=='Discard':result={'ok':True}
        else:
            key=module+':'+args[0];page=self.pages.get(key)
            if page is None:result={'ok':False,'error':'合成数据未提供此页'}
            else:
                item=next((f for f in page['fields'] if len(args)>1 and f['id']==args[1]),None)
                if method=='Set':
                    assert item is not None,'wrong runtime ID';assert item.get('enabled',True),'disabled control must not write'
                    item['value']=json.loads(args[2])['value']
                    if key=='fences:wallpaper':page['dirty']=True
                    if key=='fences:activity' and item['name']=='activityMetricsEnabled':
                        next(f for f in page['fields'] if f['name']=='activityMetricsInterval')['enabled']=item['value']
                if method=='Activate':
                    assert item is not None,'wrong runtime action ID'
                    if self.fail_actions or item['name']=='wallpaperApply' and self.fail_apply:
                        result={'ok':False,'error':'合成测试：操作失败，草稿保留。'}
                        QtCore.QTimer.singleShot(self.delay,lambda:callback(result));return
                    if item['name']=='wallpaperChooseImage' and not self.cancel_selection:
                        next(f for f in page['fields'] if f['name']=='wallpaperImagePath')['value']='/example/pictures/selected.png';page['dirty']=True
                    elif item['name']=='wallpaperClearImage':
                        next(f for f in page['fields'] if f['name']=='wallpaperImagePath')['value']='系统桌面壁纸';page['dirty']=True
                    elif item['name']=='wallpaperApply':page['dirty']=False
                if key=='fences:wallpaper':
                    video=next(f['value'] for f in page['fields'] if f['name']=='wallpaperKind')==1
                    for field in page['fields']:
                        if field['name'] in ('wallpaperChooseImage','wallpaperClearImage','wallpaperImageMode'):field['enabled']=not video
                        if field['name'] in ('videoWallpaperPath','videoWallpaperStart'):field['enabled']=video
                        if field['name']=='wallpaperImagePath':
                            field['ui']['collapsible']=video;field['label']='静态回退图片' if video else '图片文件'
                result=copy.deepcopy(page)
        QtCore.QTimer.singleShot(self.delay,lambda:callback(result))
