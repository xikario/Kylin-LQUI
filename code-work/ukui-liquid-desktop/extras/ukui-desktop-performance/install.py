#!/usr/bin/python3
"""Install/uninstall the narrow AC-only GPU policy; run through pkexec."""
from pathlib import Path
import fcntl,json,os,subprocess,sys
sys.path.insert(0,str(next(p for p in Path(__file__).resolve().parents if (p/'tools/file_transaction.py').is_file())/'tools'))
from file_transaction import FileTransaction,atomic_bytes
BASE=Path(__file__).resolve().parent
UNIT='ukui-ftg340-responsiveness.service'
FILES={'ftg340-responsiveness.py':Path('/usr/local/libexec/ukui-ftg340-responsiveness'),UNIT:Path('/etc/systemd/system')/UNIT,'90-ukui-ftg340-responsiveness.rules':Path('/etc/udev/rules.d/90-ukui-ftg340-responsiveness.rules')}
CONFIG=Path('/etc/ukui-ftg340-responsiveness.json')
STATE_DIR=Path('/var/lib/ukui-ftg340-responsiveness')
NODE=Path('/sys/class/devfreq/PHYT0048:00')
def call(*args):subprocess.run(args,check=True)
def reload_services():
    call('systemctl','daemon-reload');call('udevadm','control','--reload-rules')
def main():
    if os.geteuid()!=0:raise SystemExit('Administrator authentication required')
    if sys.argv[1:]==['--remove']:
        call('systemctl','disable','--now',UNIT)
        for target in list(FILES.values())+[CONFIG]:
            if target.exists():target.unlink()
        reload_services();return
    if len(sys.argv)!=2 or sys.argv[1] not in ('600000','800000'):raise SystemExit('Usage: install.py {600000|800000|--remove}')
    if (NODE/'device/driver').resolve().name!='ftg340':raise SystemExit('FTG340 not detected')
    if int((NODE/'min_freq').read_text())!=200000 and not CONFIG.exists():raise SystemExit('Temporary trial is still active or baseline changed')
    was_enabled=subprocess.run(['systemctl','is-enabled','--quiet',UNIT],check=False).returncode==0
    was_active=subprocess.run(['systemctl','is-active','--quiet',UNIT],check=False).returncode==0
    published=False
    try:
        with FileTransaction(STATE_DIR) as transaction:
            for target in FILES.values():
                if target.exists() and not CONFIG.exists():raise SystemExit('Unmanaged file exists: '+str(target))
            for target in list(FILES.values())+[CONFIG]:transaction.watch(target)
            # Same lock as runtime policy/trial. Release before systemctl, whose
            # ExecStart/ExecStop acquire this lock themselves.
            published=True
            with (STATE_DIR/'policy.lock').open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX)
                for source,target in FILES.items():atomic_bytes(target,(BASE/source).read_bytes(),0o755 if source.endswith('.py') else 0o644)
                atomic_bytes(CONFIG,(json.dumps({'ac_min_freq':int(sys.argv[1])})+'\n').encode())
            reload_services()
            try:
                call('systemctl','enable','--now',UNIT);call('systemctl','reload',UNIT)
            except BaseException:
                subprocess.run(['systemctl','disable','--now',UNIT],check=False)
                raise
    except BaseException:
        # Publication rollback has completed before systemd sees the old files.
        if published:
            reload_services()
            call('systemctl','enable' if was_enabled else 'disable',UNIT)
            call('systemctl','start' if was_active else 'stop',UNIT)
        raise
    print('Installed AC-only GPU floor:',sys.argv[1])
if __name__=='__main__':main()
