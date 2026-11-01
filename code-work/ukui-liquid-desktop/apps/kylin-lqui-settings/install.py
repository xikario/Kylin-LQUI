#!/usr/bin/python3
"""Atomic user-local installation; records exact source and payload hashes."""
import datetime,hashlib,json,os,shlex,shutil,tempfile
from pathlib import Path
SOURCE=Path(__file__).resolve().parent
FILES=['native_glass.py','glass_widgets.py','fusion_view.py','desktop_controls.py','control_center.py','system_backend.py','design.py','main.py','backend.py','appconfig.py','status.py','peony_adapter.py','down.svg','check.svg','icon.svg']

def link(target,path):
    temporary=path.with_name(path.name+'.new')
    if temporary.is_symlink():temporary.unlink()
    temporary.symlink_to(target);os.replace(str(temporary),str(path))

def install():
    home=Path.home();base=home/'.local/lib/kylin-lqui-settings';base.mkdir(parents=True,exist_ok=True)
    stamp=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).strftime('%Y%m%d-%H%M%S')
    previous=str((base/'current').resolve()) if (base/'current').is_symlink() else None
    release=base/('release-'+stamp)
    staging=Path(tempfile.mkdtemp(prefix='.prepare-',dir=str(base)))
    try:
        for name in FILES:shutil.copy2(str(SOURCE/name),str(staging/name))
        repo=SOURCE.parents[1];runtime=staging/'runtime'
        native=repo/'apps/ukui-panel-liquid/build/liblqui-glass-bridge.so'
        if native.is_file():shutil.copy2(native,staging/native.name)
        for relative in ['integration/desktop_status.py','integration/components.json',
                'apps/ukui-fences/scripts/calendar_snapshot.py','apps/ukui-fences/scripts/chinese_calendar.py',
                'apps/ukui-fences/scripts/holiday_sync.py','apps/ukui-fences/scripts/china_holidays_2026.json','apps/ukui-fences/scripts/lunar_hko_reference.json']:
            destination=runtime/relative;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/relative,destination)
        shutil.copytree(repo.parent.parent/'peony-manager/app',runtime/'peony-manager/app',ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
        (staging/'runtime.json').write_text(json.dumps({'sourceRoot':str(SOURCE.parents[1])},ensure_ascii=False))
        hashes={str(p.relative_to(staging)):hashlib.sha256(p.read_bytes()).hexdigest() for p in staging.rglob('*') if p.is_file()}
        manifest={'product':'Kylin-LQUI 设置','version':'0.4.0','release':str(release),'previous':previous,'source':str(SOURCE),'files':hashes}
        (staging/'install.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
        os.replace(str(staging),str(release));link(release,base/'current')
        bin_dir=home/'.local/bin';bin_dir.mkdir(parents=True,exist_ok=True);launcher=bin_dir/'kylin-lqui-settings'
        launcher_new=launcher.with_name(launcher.name+'.new');launcher_new.write_text('#!/bin/sh\nexec /usr/bin/python3 '+shlex.quote(str(base/'current/main.py'))+' "$@"\n');launcher_new.chmod(0o755);os.replace(str(launcher_new),str(launcher))
        control=bin_dir/'kylin-lqui-control-center';control_new=control.with_name(control.name+'.new');control_new.write_text('#!/bin/sh\nexec /usr/bin/python3 '+shlex.quote(str(base/'current/control_center.py'))+' \"$@\"\n');control_new.chmod(0o755);os.replace(str(control_new),str(control))
        applications=home/'.local/share/applications';applications.mkdir(parents=True,exist_ok=True)
        (applications/'kylin-lqui-settings.desktop').write_text('[Desktop Entry]\nType=Application\nName=Kylin-LQUI 设置\nComment=桌面、任务栏、AI 小站与模块状态\nExec="'+str(launcher)+'"\nIcon='+str(base/'current/icon.svg')+'\nTerminal=false\nCategories=Settings;DesktopSettings;\nStartupWMClass=kylin-lqui-settings\n')
        (applications/'kylin-lqui-control-center.desktop').write_text('[Desktop Entry]\nType=Application\nName=Kylin-LQUI 系统控制中心\nExec="'+str(control)+'"\nIcon='+str(base/'current/icon.svg')+'\nNoDisplay=true\nTerminal=false\nCategories=Settings;\n')
        aliases=[]
        for name,expected in [('ukui-fences-settings.desktop','ukui-fences-launcher'),('fcitx-liquid-settings.desktop','fcitx-liquid-panel'),('peony-manager.desktop','peony-manager/app/main.py')]:
            alias=applications/name
            if not alias.is_file():continue
            original=alias.read_text()
            if expected not in original:continue
            saved=release/'aliases-before';saved.mkdir(exist_ok=True);shutil.copy2(str(alias),str(saved/name))
            lines=[line for line in original.splitlines() if not line.startswith(('NoDisplay=','X-Kylin-Lqui-Settings-Alias='))]
            lines[1:1]=['NoDisplay=true','X-Kylin-Lqui-Settings-Alias=true']
            temporary=alias.with_name(alias.name+'.new');temporary.write_text('\n'.join(lines)+'\n');os.replace(str(temporary),str(alias));aliases.append(name)
        manifest['hiddenSettingsAliases']=aliases
        (release/'install.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
        print(json.dumps(manifest,ensure_ascii=False))
    except Exception:
        if previous:link(previous,base/'current')
        raise
    finally:
        if staging.exists():shutil.rmtree(str(staging))
if __name__=='__main__':
    import pwd
    if Path.home().resolve()!=Path(pwd.getpwuid(os.getuid()).pw_dir).resolve():raise SystemExit('HOME 与当前用户主目录不一致，未安装。')
    install()
