import importlib.util,unittest
from pathlib import Path
from unittest.mock import Mock,patch
spec=importlib.util.spec_from_file_location('bridge',Path(__file__).with_name('session_bridge.py'))
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)

class RecoveryErrors(unittest.TestCase):
    def trial(self):
        trial=bridge.Trial.__new__(bridge.Trial);trial.finished=False;trial.state={};trial.session=Mock();trial.loop=Mock();return trial
    def test_full_disk_does_not_block_native_recovery(self):
        trial=self.trial()
        with patch.object(bridge,'write_state',side_effect=OSError('ENOSPC')),patch.object(bridge,'native_desktops',return_value=[]):trial.restore('test')
        trial.session.restore_native.assert_called_once();trial.loop.quit.assert_called_once();self.assertTrue(trial.finished)
    def test_failed_recovery_retries_and_does_not_finish_early(self):
        trial=self.trial();trial.session.restore_native.side_effect=[RuntimeError('not ready'),None]
        with patch.object(bridge,'write_state'),patch.object(bridge,'native_desktops',return_value=[]),patch.object(bridge.GLib,'timeout_add_seconds') as schedule:
            trial.restore('test');self.assertFalse(trial.finished);trial.loop.quit.assert_not_called()
            schedule.call_args[0][1]();self.assertTrue(trial.finished);self.assertEqual(trial.session.restore_native.call_count,2)

if __name__=='__main__':unittest.main()
