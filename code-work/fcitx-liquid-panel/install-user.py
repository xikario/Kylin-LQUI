#!/usr/bin/python3
"""Install the candidate frontend and its helper as one recoverable release."""
import uuid
import datetime,hashlib,json,os,pwd,re,shutil,subprocess,tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(next(p for p in Path(__file__).resolve().parents if (p/'tools/file_transaction.py').is_file())/'tools'))
from file_transaction import FileTransaction

def install(home):
    root=Path(__file__).resolve().parent;home=Path(home)
    base=home/'.local/lib/fcitx-liquid-panel';base.mkdir(parents=True,exist_ok=True)
    launcher=home/'.local/bin/fcitx-liquid-panel';autostart=home/'.config/autostart/fcitx-liquid-panel.desktop'
    with FileTransaction(base) as transaction:
        if os.path.lexists(launcher) and not (launcher.is_symlink() and launcher.resolve()==(base/'current/build/fcitx-liquid-panel').resolve()):
            raise RuntimeError('已有非托管同名启动程序，未覆盖：'+str(launcher))
        if os.path.lexists(autostart) and (autostart.is_symlink() or not autostart.is_file() or 'X-UKUI-FcitxLiquid=true' not in autostart.read_text()):
            raise RuntimeError('已有非托管自启动条目，未覆盖：'+str(autostart))
        transaction.watch(base/'current')
        transaction.watch(home/'.local/bin/fcitx-liquid-panel')
        transaction.watch(home/'.config/autostart/fcitx-liquid-panel.desktop')
        release=base/('release-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8])
        previous=str((base/'current').resolve()) if (base/'current').exists() else None
        stage=Path(tempfile.mkdtemp(prefix='.prepare-',dir=str(base)))
        try:
            (stage/'build').mkdir();shutil.copy2(root/'build/fcitx-liquid-panel',stage/'build/fcitx-liquid-panel')
            for name in ('manage.py','liquid-panel.conf.example'):shutil.copy2(root/name,stage/name)
            shutil.copytree(root/'assets/tray',stage/'assets/tray')
            deps=subprocess.check_output(['ldd',str(stage/'build/fcitx-liquid-panel')],text=True)
            if 'not found' in deps:raise RuntimeError('Missing runtime dependency')
            manifest={'previous':previous,'release':str(release),'source':str(root),'files':{str(p.relative_to(stage)):hashlib.sha256(p.read_bytes()).hexdigest() for p in stage.rglob('*') if p.is_file()}}
            (stage/'install.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
            stage.rename(release)
            def link(target,path):
                path.parent.mkdir(parents=True,exist_ok=True);temporary=path.with_name(path.name+'.new')
                if temporary.is_symlink():temporary.unlink()
                temporary.symlink_to(target);temporary.replace(path)
            link(release,base/'current');link(base/'current/build/fcitx-liquid-panel',home/'.local/bin/fcitx-liquid-panel')
            autostart=home/'.config/autostart/fcitx-liquid-panel.desktop'
            if autostart.is_file():
                shutil.copy2(autostart,release/'autostart-before.desktop')
                text=autostart.read_text();text=re.sub(r'(?m)^Exec=.*$',lambda _: 'Exec=/usr/bin/python3 "'+str(base/'current/manage.py')+'" start',text)
                temporary=autostart.with_suffix('.desktop.new');temporary.write_text(text);temporary.replace(autostart)
            print(json.dumps({'release':str(release),'previous':previous},ensure_ascii=False))
        except Exception:
            if previous:
                temporary=base/'current.rollback';temporary.symlink_to(previous);temporary.replace(base/'current')
            raise
        finally:
            if stage.exists():shutil.rmtree(stage)

if __name__=='__main__':
    home=Path(pwd.getpwuid(os.getuid()).pw_dir)
    if Path.home().resolve()!=home.resolve():raise SystemExit('HOME 与当前用户主目录不一致，未安装。')
    install(home)
