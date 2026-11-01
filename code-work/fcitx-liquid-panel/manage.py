#!/usr/bin/env python3
"""User-local deployment and explicit Rime page-size preferences; no user database edits."""
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import tempfile
import re
import configparser
import base64
import fcntl

def _atomic_bytes(path, value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    if value is None:
        if path.exists():path.unlink()
        return
    fd,temporary=tempfile.mkstemp(prefix='.liquid-transaction-',dir=str(path.parent))
    try:
        with os.fdopen(fd,'wb') as output:output.write(value);output.flush();os.fsync(output.fileno())
        os.replace(temporary,path)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)

def recover_settings_transaction(config_home):
    root=Path(config_home)/'fcitx';journal=root/'.liquid-settings-transaction.json'
    if not journal.exists():return
    data=json.loads(journal.read_text())
    allowed={'rime/default.custom.yaml','rime/rime_ice.custom.yaml','liquid-panel.conf'}
    if set(data)!=allowed:raise ValueError('配置恢复记录无效，请保留记录并检查')
    for relative,item in data.items():
        path=root/relative;current=path.read_bytes() if path.exists() else None
        before=None if item['before'] is None else base64.b64decode(item['before'])
        after=base64.b64decode(item['after'])
        if current not in (before,after):raise ValueError('保存期间配置被其他程序修改，已保留恢复记录')
        if current!=before:_atomic_bytes(path,before)
    journal.unlink()

def apply_settings_transaction(payload, config_home=None):
    config_home=Path(config_home or CONFIG_HOME);root=config_home/'fcitx';root.mkdir(parents=True,exist_ok=True)
    if type(payload.get('pinyin')) is not bool or type(payload.get('pageSize')) is not int or not 1<=payload['pageSize']<=10:raise ValueError('无效的候选设置')
    appearance=payload.get('appearance')
    if not isinstance(appearance,str) or not appearance or len(appearance)>65536:raise ValueError('外观配置无效')
    parser=configparser.ConfigParser(interpolation=None);parser.read_string(appearance)
    names=['rime/default.custom.yaml','rime/rime_ice.custom.yaml','liquid-panel.conf']
    with (root/'.liquid-settings.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        recover_settings_transaction(config_home)
        before={name:(root/name).read_bytes() if (root/name).exists() else None for name in names}
        # Validate and transform both Rime files in a disposable config root.
        # No live file is changed if either parser/validation fails.
        with tempfile.TemporaryDirectory(prefix='liquid-preferences-') as temporary:
            stage=Path(temporary)
            for name,value in before.items():
                if value is not None:_atomic_bytes(stage/'fcitx'/name,value)
            import contextlib,io
            with contextlib.redirect_stdout(io.StringIO()):
                set_rime_page_size(payload['pageSize'],stage);set_rime_pinyin(payload['pinyin'],stage)
            after={name:(stage/'fcitx'/name).read_bytes() for name in names[:2]}
            after['liquid-panel.conf']=appearance.encode('utf-8')
        if any(((root/name).read_bytes() if (root/name).exists() else None)!=before[name] for name in names):raise ValueError('配置已被其他程序修改，请刷新后再保存')
        journal=root/'.liquid-settings-transaction.json'
        record={name:{'before':None if before[name] is None else base64.b64encode(before[name]).decode(),'after':base64.b64encode(after[name]).decode()} for name in names}
        _atomic_bytes(journal,json.dumps(record).encode())
        try:
            for name in names:
                if before[name]!=after[name]:_atomic_bytes(root/name,after[name])
            journal.unlink()
        except Exception:
            recover_settings_transaction(config_home)
            raise
        return {'changed':any(before[name]!=after[name] for name in names[:2])}

ROOT = Path(__file__).resolve().parent
BINARY = ROOT / 'build/fcitx-liquid-panel'
CONFIG_HOME = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
DATA_HOME = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
AUTOSTART = CONFIG_HOME / 'autostart/fcitx-liquid-panel.desktop'
USER_CONFIG = CONFIG_HOME / 'fcitx/liquid-panel.conf'
CONFIG_TEMPLATE = ROOT / 'liquid-panel.conf.example'
STATE = Path.home() / '.local/state/fcitx-liquid-panel'

def install_settings_entry():
    entry = DATA_HOME / 'applications/fcitx-liquid-settings.desktop'
    entry.parent.mkdir(parents=True, exist_ok=True)
    entry.write_text('[Desktop Entry]\nType=Application\nName=RIME液态输入法设置\n'
                     'Comment=字体、颜色、玻璃效果与实时预览\n'
                     f'Exec=/usr/bin/python3 "{ROOT / "manage.py"}" settings\n'
                     f'Icon={ROOT / "assets/tray/settings.png"}\nTerminal=false\nCategories=Settings;\n'
                     'X-UKUI-FcitxLiquid=true\n'+('NoDisplay=true\nX-Kylin-Lqui-Settings-Alias=true\n' if (Path.home()/'.local/bin/kylin-lqui-settings').is_file() else ''))

def install_branding():
    """User-local Fcitx metadata override. Keep the system addon and IM id intact."""
    source = Path('/usr/share/fcitx/inputmethod/rime.conf')
    target = CONFIG_HOME / 'fcitx/inputmethod/rime.conf'
    if source.exists():
        original = target.read_text() if target.exists() else source.read_text()
        # Keep localized fields distinct while using the same product name.
        updated = re.sub(r'(?m)^(Name(?:\[[^\]]+\])?)=.*$', lambda m: m.group(1)+'=RIME液态输入法', original)
        updated = re.sub(r'(?m)^IconName=.*$', 'IconName=rime-liquid', updated)
        if updated != original or not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            if target.exists():
                backup=target.with_name('rime.conf.before-liquid')
                if not backup.exists():shutil.copy2(target,backup)
            target.write_text(updated)
    icon = ROOT / 'assets/tray/settings.png'
    if icon.exists():
        target_icon=DATA_HOME/'icons/hicolor/512x512/apps/rime-liquid.png'
        target_icon.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(icon,target_icon)
    install_settings_entry()
    if AUTOSTART.exists() and 'X-UKUI-FcitxLiquid=true' in AUTOSTART.read_text():
        text=AUTOSTART.read_text()
        AUTOSTART.write_text(re.sub(r'(?m)^Name=.*$', 'Name=RIME液态输入法',text))

def set_rime_page_size(count, config_home=CONFIG_HOME):
    """Change the real Rime page size, retaining every unrelated patch key."""
    import yaml
    count=int(count)
    if not 1 <= count <= 10:raise ValueError('候选数量必须为 1–10')
    target=Path(config_home)/'fcitx/rime/default.custom.yaml'
    original=target.read_text() if target.exists() else ''
    data=yaml.safe_load(original) or {}
    if not isinstance(data,dict):raise ValueError('Rime 配置必须为 YAML 映射，未写入任何内容')
    patch=data.setdefault('patch',{})
    if not isinstance(patch,dict):raise ValueError('现有 patch 不是映射，未写入任何内容')
    nested=patch.get('menu',{})
    if not isinstance(nested,dict):raise ValueError('现有 menu 不是映射，未写入任何内容')
    current=patch.get('menu/page_size',nested.get('page_size'))
    if current==count:
        print('Rime page size unchanged:',count);return
    patch['menu/page_size']=count
    if 'page_size' in nested:nested['page_size']=count
    # Preserve comments/format for the usual single flat preference. Fall back
    # to a safe YAML round trip for nested configurations and new patch keys.
    pattern=r'(?m)^(\s+menu/page_size:\s*)\d+(\s*(?:#.*)?)$'
    if len(re.findall(pattern,original))==1 and not nested:
        updated=re.sub(pattern,lambda m:m.group(1)+str(count)+m.group(2),original)
    else:updated=yaml.safe_dump(data,allow_unicode=True,sort_keys=False)
    # Back up only when a change is necessary. Write and replace atomically.
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():
        backup=target.parent/'liquid-settings-backups'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f.yaml')
        backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,backup)
    fd,temporary=tempfile.mkstemp(prefix='.liquid-page-',dir=str(target.parent))
    try:
        with os.fdopen(fd,'w') as output:output.write(updated)
        os.replace(temporary,target)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)
    print('Rime page size:',count)

def ensure_config():
    USER_CONFIG.parent.mkdir(parents=True, exist_ok=True)
    if not USER_CONFIG.exists():
        if not CONFIG_TEMPLATE.is_file():
            raise RuntimeError('Missing configuration template: ' + str(CONFIG_TEMPLATE))
        shutil.copy2(CONFIG_TEMPLATE, USER_CONFIG)
        print('Created:', USER_CONFIG)

def set_rime_pinyin(enabled, config_home=CONFIG_HOME):
    """Expose the engine's own dictionary pronunciation; retain compatibility fixes."""
    import yaml
    target=Path(config_home)/'fcitx/rime/rime_ice.custom.yaml'
    original=target.read_text() if target.exists() else ''
    data=yaml.safe_load(original) or {}
    if not isinstance(data,dict):raise ValueError('Rime 方案配置不是映射，未写入')
    patch=data.setdefault('patch',{})
    if not isinstance(patch,dict):raise ValueError('现有方案 patch 不是映射，未写入')
    nested=patch.get('translator',{})
    if not isinstance(nested,dict):raise ValueError('现有 translator 不是映射，未写入')
    formats=['xform/^/［/','xform/$/］/'] if enabled else ['xform/.+//']
    before=json.dumps(data,ensure_ascii=False,sort_keys=True)
    patch['translator/always_show_comments']=bool(enabled)
    patch['translator/comment_format']=formats
    if 'always_show_comments' in nested:nested['always_show_comments']=bool(enabled)
    if 'comment_format' in nested:nested['comment_format']=formats
    if json.dumps(data,ensure_ascii=False,sort_keys=True)==before:return False
    updated=yaml.safe_dump(data,allow_unicode=True,sort_keys=False)
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():
        backup=target.parent/'liquid-settings-backups'/('rime_ice-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f.yaml'))
        backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,backup)
    fd,temporary=tempfile.mkstemp(prefix='.liquid-pinyin-',dir=str(target.parent))
    try:
        with os.fdopen(fd,'w') as output:output.write(updated)
        os.replace(temporary,target)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)
    return True

def sync_rime_pinyin():
    recover_settings_transaction(CONFIG_HOME)
    if not (CONFIG_HOME/'fcitx/rime/rime_ice.schema.yaml').exists():return
    settings=configparser.RawConfigParser();settings.read(str(USER_CONFIG),encoding='utf-8')
    enabled=settings.getboolean('Interface','ShowCandidatePinyin',fallback=True)
    if set_rime_pinyin(enabled):
        result=call('org.kde.impanel','/org/ukui/FcitxLiquid','org.ukui.FcitxLiquid.ReloadRime')
        if result.returncode:raise RuntimeError('拼音设置已保存，Rime 重新部署请求失败：'+result.stderr)

def call(dest, path, method, *args):
    return subprocess.run(['gdbus', 'call', '--session', '--dest', dest,
                           '--object-path', path, '--method', method, *args],
                          text=True, capture_output=True, timeout=6)

def status():
    return call('org.kde.impanel', '/org/ukui/FcitxLiquid', 'org.ukui.FcitxLiquid.Status')

def ui():
    return call('org.fcitx.Fcitx', '/inputmethod', 'org.fcitx.Fcitx.InputMethod.GetCurrentUI')

def stop():
    if status().returncode == 0:
        result = call('org.kde.impanel', '/org/ukui/FcitxLiquid', 'org.ukui.FcitxLiquid.Quit')
        if result.returncode:
            raise RuntimeError(result.stderr)
        for _ in range(30):
            owner = call('org.freedesktop.DBus', '/org/freedesktop/DBus',
                         'org.freedesktop.DBus.NameHasOwner', 'org.kde.impanel')
            # The control object can disappear before the process releases
            # its bus name; wait for that release before starting a new panel.
            if owner.returncode == 0 and 'false' in owner.stdout:
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Panel did not exit')
    print('Fcitx UI:', ui().stdout.strip())

def start():
    install_branding()
    ensure_config()
    if status().returncode == 0:
        sync_rime_pinyin()
        print('Liquid panel is already running')
        return
    if not BINARY.is_file():
        raise RuntimeError('Build fcitx-liquid-panel first')
    owner = call('org.freedesktop.DBus', '/org/freedesktop/DBus',
                 'org.freedesktop.DBus.NameHasOwner', 'org.kde.impanel')
    if 'true' in owner.stdout:
        raise RuntimeError('Another input panel owns org.kde.impanel; leaving it untouched')
    env = os.environ.copy()
    env['QT_QPA_PLATFORM'] = 'xcb'
    env['QT_QPA_PLATFORMTHEME'] = ''
    env['QT_STYLE_OVERRIDE'] = 'Fusion'
    STATE.mkdir(parents=True, exist_ok=True)
    with (STATE / 'panel.log').open('ab') as log:
        child = subprocess.Popen([str(BINARY)], stdin=subprocess.DEVNULL, stdout=log,
                                 stderr=log, env=env, start_new_session=True, cwd=ROOT)
    for _ in range(50):
        if child.poll() is not None:
            raise RuntimeError('Panel startup failed; see ' + str(STATE / 'panel.log'))
        if status().returncode == 0:
            sync_rime_pinyin()
            print('Liquid panel started:', child.pid)
            print('Fcitx UI:', ui().stdout.strip())
            return
        time.sleep(.1)
    raise RuntimeError('Panel startup timed out')

def apply():
    # Preserve a reviewable baseline, but restoration never overwrites user data.
    backup = ROOT / 'backups' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    backup.mkdir(parents=True)
    base = Path.home() / '.config/fcitx'
    for relative in ['config', 'profile', 'conf/fcitx-classic-ui.config']:
        src = base / relative
        if src.is_file():
            dst = backup / relative
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
    (backup / 'before.json').write_text(json.dumps({'ui': ui().stdout.strip()}, ensure_ascii=False, indent=2))
    start()
    # Wait for the live protocol handshake before persisting login startup.
    for _ in range(40):
        if 'fcitx-kimpanel-ui' in ui().stdout:
            break
        time.sleep(.1)
    else:
        stop()
        raise RuntimeError('Fcitx did not switch to Kimpanel; startup was not installed')
    AUTOSTART.parent.mkdir(parents=True, exist_ok=True)
    entry = ('[Desktop Entry]\nType=Application\nName=RIME液态输入法\n'
             'Comment=Fcitx liquid candidate panel\n'
             f'Exec=/usr/bin/python3 "{ROOT / "manage.py"}" start\n'
             'Terminal=false\nX-UKUI-FcitxLiquid=true\nX-GNOME-Autostart-enabled=true\n')
    AUTOSTART.write_text(entry)
    install_settings_entry()
    print('Installed:', AUTOSTART)
    print('Baseline:', backup)

def settings():
    install_settings_entry()
    start()
    result = call('org.kde.impanel', '/org/ukui/FcitxLiquid', 'org.ukui.FcitxLiquid.ShowSettings')
    if result.returncode:
        raise RuntimeError(result.stderr)

def restart():
    stop()
    start()

def restore():
    if AUTOSTART.exists():
        if 'X-UKUI-FcitxLiquid=true' not in AUTOSTART.read_text():
            raise RuntimeError('Autostart entry is not ours; refusing to remove it')
        AUTOSTART.unlink()
    stop()
    print('Restored the existing classic skin; Rime data was not changed')

if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else 'status'
    try:
        if mode == 'status':
            result = status()
            print(result.stdout.strip() if result.returncode == 0 else 'Liquid panel is stopped')
            print('Fcitx UI:', ui().stdout.strip())
        elif mode == 'rime-page-size':
            set_rime_page_size(sys.argv[2])
        elif mode == 'rime-pinyin':
            if sys.argv[2] not in ('true','false'):raise ValueError('拼音显示应为 true 或 false')
            print(json.dumps({'changed':set_rime_pinyin(sys.argv[2]=='true')}))
        elif mode == 'save-settings':
            print(json.dumps(apply_settings_transaction(json.load(sys.stdin))))
        elif mode == 'branding':
            install_branding()
        elif mode in ('apply', 'start', 'stop', 'restart', 'restore', 'settings'):
            globals()[mode]()
        else:
            raise RuntimeError('Use apply, start, stop, restart, restore, settings, branding or status')
    except (RuntimeError, ValueError, IndexError, subprocess.TimeoutExpired) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
