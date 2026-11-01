import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from file_transaction import FileTransaction

def module(relative):
    spec=importlib.util.spec_from_file_location('installer_'+str(abs(hash(relative))),ROOT/relative)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value

def state(path):
    if path.is_symlink():return ('link',os.readlink(path))
    if not path.exists():return ('absent',)
    return ('file',path.read_bytes(),path.stat().st_mode&0o7777)

class Publication(unittest.TestCase):
    def test_all_entry_types_roll_back_at_every_write(self):
        for fail in range(4):
            with self.subTest(fail=fail),tempfile.TemporaryDirectory() as t:
                root=Path(t);a=root/'a';a.write_bytes(b'original');a.chmod(0o640);b=root/'b';b.symlink_to('old-target');c=root/'c';paths=[a,b,c]
                before=[state(p) for p in paths]
                with self.assertRaises(RuntimeError):
                    with FileTransaction(root/'store') as transaction:
                        for p in paths:transaction.watch(p)
                        for n,p in enumerate(paths):
                            if n==fail:raise RuntimeError('injected')
                            if p.is_symlink():p.unlink()
                            p.write_bytes(b'new')
                        raise RuntimeError('manifest')
                self.assertEqual([state(p) for p in paths],before)
    def test_failed_commit_record_rolls_back_before_returning(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);path=root/'entry';path.write_bytes(b'old')
            tx=FileTransaction(root/'store');tx.__enter__();tx.watch(path);path.write_bytes(b'new')
            tx.save=lambda:(_ for _ in ()).throw(OSError('commit record failure'))
            with self.assertRaises(OSError):tx.__exit__(None,None,None)
            self.assertEqual(path.read_bytes(),b'old')
    def test_interrupted_publication_is_recovered_on_retry(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);path=root/'entry';path.write_bytes(b'old')
            tx=FileTransaction(root/'store');tx.__enter__();tx.watch(path);path.write_bytes(b'partial');tx.lock.close()
            with self.assertRaisesRegex(RuntimeError,'中断安装'):
                with FileTransaction(root/'store'):pass
            self.assertEqual(path.read_bytes(),b'partial')
            FileTransaction.restore(tx.journal) # Explicit recovery after inspecting the journal.
            with FileTransaction(root/'store') as retry:
                self.assertEqual(path.read_bytes(),b'old');retry.watch(path);path.write_bytes(b'committed')
            self.assertEqual(path.read_bytes(),b'committed')
    def test_cli_late_failure_restores_entire_publication(self):
        installer=module('code-work/liquid-cli/scripts/install.py')
        for upgrade in (False,True):
            with self.subTest(upgrade=upgrade),tempfile.TemporaryDirectory() as t:
                home=Path(t);base=home/'.local/lib/liquid-cli';base.mkdir(parents=True)
                targets=[base/'current',home/'.local/bin/liquid-cli',home/'.local/share/applications/liquid-cli.desktop',home/'.local/share/icons/hicolor/scalable/apps/liquid-cli.svg',home/'.config/autostart/kylin-lqui-ai-station.desktop']
                if upgrade:
                    old=base/'old';old.mkdir();targets[0].symlink_to(old);targets[1].parent.mkdir(parents=True);targets[1].symlink_to(base/'current/liquid-cli')
                    for p in targets[2:]:p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'original')
                before=[state(p) for p in targets];write=Path.write_text
                def write_fail(path,*a,**kw):
                    if path.name=='install.json':raise OSError('late manifest failure')
                    return write(path,*a,**kw)
                def run(args,**kwargs):
                    if args[0]=='cmake':
                        payload=Path(args[-1])/'lib/liquid-cli';payload.mkdir(parents=True);(payload/'liquid-cli').write_bytes(b'fixture')
                    return subprocess.CompletedProcess(args,0,stdout='',stderr='')
                with patch.object(installer.Path,'home',return_value=home),patch.object(installer.subprocess,'run',side_effect=run),patch.object(Path,'write_text',write_fail),self.assertRaises(OSError):installer.install()
                self.assertEqual([state(p) for p in targets],before)
    def test_settings_failure_restores_aliases_and_first_install(self):
        installer=module('code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/install.py')
        with tempfile.TemporaryDirectory() as t:
            home=Path(t);apps=home/'.local/share/applications';apps.mkdir(parents=True)
            alias=apps/'fcitx-liquid-settings.desktop';alias.write_text('[Desktop Entry]\nExec=fcitx-liquid-panel\n')
            targets=[home/'.local/lib/kylin-lqui-settings/current',home/'.local/bin/kylin-lqui-settings',home/'.local/bin/kylin-lqui-control-center',apps/'kylin-lqui-settings.desktop',apps/'kylin-lqui-control-center.desktop',alias]
            before=[state(p) for p in targets];write=Path.write_text
            def write_fail(path,*a,**kw):
                if path.name=='install.json' and path.parent.name.startswith('release-'):raise OSError('final manifest failure')
                return write(path,*a,**kw)
            with patch.object(installer.Path,'home',return_value=home),patch.object(Path,'write_text',write_fail),self.assertRaises(OSError):installer.install()
            self.assertEqual([state(p) for p in targets],before)
    def test_fcitx_refuses_unmanaged_launcher_and_autostart(self):
        installer=module('code-work/fcitx-liquid-panel/install-user.py')
        for rel in ('.local/bin/fcitx-liquid-panel','.config/autostart/fcitx-liquid-panel.desktop'):
            with self.subTest(rel=rel),tempfile.TemporaryDirectory() as t:
                home=Path(t);p=home/rel;p.parent.mkdir(parents=True);p.write_bytes(b'user-owned');before=state(p)
                with self.assertRaisesRegex(RuntimeError,'非托管'):installer.install(home)
                self.assertEqual(state(p),before);self.assertFalse((home/'.local/lib/fcitx-liquid-panel/current').exists())

    def test_hardware_policy_install_rolls_back_every_file_and_can_retry(self):
        installer=module('code-work/ukui-liquid-desktop/extras/ukui-desktop-performance/install.py')
        for fail in range(1,5):
            with self.subTest(fail=fail),tempfile.TemporaryDirectory() as t:
                root=Path(t);node=root/'gpu';(node/'device').mkdir(parents=True);(node/'device/driver').symlink_to(root/'ftg340');(node/'min_freq').write_text('200000')
                files={name:root/'payload'/name for name in installer.FILES};config=root/'config.json';targets=list(files.values())+[config];before=[state(p) for p in targets]
                atomic=installer.atomic_bytes;count=[0]
                def fail_once(*args,**kwargs):
                    count[0]+=1
                    if count[0]==fail:raise OSError('injected publication error')
                    return atomic(*args,**kwargs)
                def command(args,**kwargs):return subprocess.CompletedProcess(args,1 if args[1] in ('is-active','is-enabled') else 0)
                with patch.object(installer,'NODE',node),patch.object(installer,'FILES',files),patch.object(installer,'CONFIG',config),patch.object(installer,'STATE_DIR',root/'state'),patch.object(installer.os,'geteuid',return_value=0),patch.object(sys,'argv',['install.py','600000']),patch.object(installer.subprocess,'run',side_effect=command):
                    with patch.object(installer,'atomic_bytes',side_effect=fail_once),self.assertRaises(OSError):installer.main()
                    self.assertEqual([state(p) for p in targets],before)
                    installer.main();self.assertTrue(config.is_file())

if __name__=='__main__':unittest.main()
