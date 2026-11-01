"""Exercise the real restore entry point using only fake HOME/proc/system calls."""
from pathlib import Path
import os,runpy,sys,tempfile,unittest
from unittest.mock import patch,Mock
SCRIPT=Path(__file__).resolve().parents[1]/'scripts/restore.py'
class RestoreSession(unittest.TestCase):
    def test_only_current_display_is_signalled(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);home=root/'home';home.mkdir();proc=root/'proc';proc.mkdir()
            for ident,display in ((900001,':41'),(900002,':42')):
                p=proc/str(ident);p.mkdir();(p/'exe').symlink_to('/usr/bin/ukui-panel');(p/'maps').write_text('/fixture/libukuiliquidpanel.so');(p/'environ').write_bytes(('DISPLAY='+display+'\0').encode())
            class FixturePath:
                def __new__(cls,*args):return proc if args==('/proc',) else Path(*args)
                @staticmethod
                def home():return home
            with patch('pathlib.Path',FixturePath),patch.dict(os.environ,DISPLAY=':41'),patch.object(sys,'argv',[str(SCRIPT),'--restart']),patch('os.kill') as kill,patch('subprocess.run'),patch('subprocess.Popen'),patch('time.sleep'):
                runpy.run_path(str(SCRIPT),run_name='__main__')
            self.assertEqual([call.args[0] for call in kill.call_args_list],[900001])
if __name__=='__main__':unittest.main()
