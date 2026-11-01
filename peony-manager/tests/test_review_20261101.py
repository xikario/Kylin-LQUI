import sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from app import operations,scanner,cli_settings
class ReviewRegressions(unittest.TestCase):
    def test_failed_quit_does_not_report_a_restart_or_spawn_a_replacement(self):
        import subprocess
        with patch.object(operations.subprocess,'run',return_value=subprocess.CompletedProcess([],1,stderr='fixture failure')),patch.object(operations.subprocess,'Popen') as spawn:
            result=operations.restart_peony();self.assertFalse(result['success']);spawn.assert_not_called()
    def test_unknown_host_state_stays_unknown_with_local_enabled(self):
        with patch.object(scanner.paths,'get_plugin_dirs',return_value=['/fixture']),patch.object(scanner,'_collect_so_files',return_value=[('/fixture/libpeony-cli-panel.so','libpeony-cli-panel.so')]),patch.object(scanner.gsettings_store,'get_disabled_extensions',return_value=None),patch.object(scanner.package_store,'owners_of',return_value={}),patch.object(scanner.package_store,'version_of',return_value=''),patch.object(cli_settings,'read',return_value={'enabled':True}):
            plugin=next(p for p in scanner.scan_plugins() if p.id=='peony-cli-panel');self.assertIsNone(plugin.enabled);self.assertIsNone(plugin.pending_enabled)
    def test_packaged_installability_does_not_require_a_development_build(self):
        def present(path):return str(path).startswith(('/usr/share/kylin-lqui/peony-plugins','/usr/libexec/kylin-lqui/peony-manager-helper'))
        with patch.object(Path,'is_file',present),patch.object(cli_settings,'read',return_value={}):
            state=cli_settings.status();self.assertTrue(state['installable']);self.assertFalse(state['built'])
if __name__=='__main__':unittest.main()
