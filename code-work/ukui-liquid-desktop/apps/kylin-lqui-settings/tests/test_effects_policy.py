"""Python effects policy must match the C++ switchboard exactly."""
import os, subprocess, sys, tempfile, unittest
from pathlib import Path

HOME = tempfile.TemporaryDirectory()
os.environ['XDG_CONFIG_HOME'] = HOME.name
RUNTIME = tempfile.TemporaryDirectory()
os.chmod(RUNTIME.name, 0o700)
os.environ['XDG_RUNTIME_DIR'] = RUNTIME.name
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore
import effects_policy as E


def put(key, value):
    s = QtCore.QSettings(E.path(), QtCore.QSettings.IniFormat); s.setValue(key, value); s.sync()


def system_reduce(on):
    s = QtCore.QSettings('org.ukui', 'style'); s.setValue('reduceAnimation', on); s.sync()


class EffectsPolicyTest(unittest.TestCase):
    def setUp(self):
        if Path(E.path()).exists(): Path(E.path()).unlink()
        if Path(E.health_path()).exists(): Path(E.health_path()).unlink()
        system_reduce(False)

    def test_defaults_change_nothing(self):
        self.assertEqual(E.motion(), 'system')
        self.assertFalse(E.reduced_motion())
        self.assertTrue(E.reduced_motion(True))
        self.assertTrue(E.gpu_allowed())
        self.assertTrue(E.pointer_light())
        self.assertFalse(E.pointer_light(False))

    def test_system_and_explicit_motion(self):
        system_reduce(True); self.assertTrue(E.reduced_motion())
        put('effects/motion', 'off'); self.assertFalse(E.reduced_motion(True))
        system_reduce(False); put('effects/motion', 'on'); self.assertTrue(E.reduced_motion())

    def test_unknown_values_are_safe(self):
        put('effects/motion', 'sideways'); put('effects/gpu', 'maybe')
        self.assertEqual(E.motion(), 'system'); self.assertTrue(E.gpu_allowed())

    def test_setters_keep_icon_settings(self):
        put('icons/uniform', False)
        self.assertTrue(E.set_motion('on')); self.assertTrue(E.set_gpu_allowed(False)); self.assertTrue(E.set_pointer_light(False))
        self.assertEqual(E.motion(), 'on'); self.assertFalse(E.gpu_allowed()); self.assertFalse(E.pointer_light())
        s = QtCore.QSettings(E.path(), QtCore.QSettings.IniFormat)
        self.assertEqual(str(s.value('icons/uniform')).lower(), 'false')
        with self.assertRaises(ValueError): E.set_motion('loud')

    def test_levels_cap_everything(self):
        self.assertEqual(E.level(), 'full')
        self.assertTrue(E.gpu_allowed() and E.live_capture() and E.decorative_frames() and E.glass_material())
        self.assertEqual(E.motion_scale(), 1.0)
        E.set_level('balanced')
        self.assertTrue(E.gpu_allowed() and E.live_capture() and E.glass_material())
        self.assertFalse(E.decorative_frames() or E.pointer_light(True))
        self.assertAlmostEqual(E.motion_scale(), 0.7); self.assertFalse(E.reduced_motion())
        E.set_level('lite'); E.set_motion('off')
        self.assertFalse(E.gpu_allowed() or E.live_capture()); self.assertTrue(E.glass_material())
        self.assertTrue(E.reduced_motion()); self.assertEqual(E.motion_scale(), 0.0)
        E.set_level('off'); self.assertFalse(E.glass_material())
        put('effects/level', 'turbo'); self.assertEqual(E.level(), 'full')
        with self.assertRaises(ValueError): E.set_level('max')

    def test_session_health_caps_without_touching_choice(self):
        self.assertFalse(E.report_failure('calendar-worker', 'timeout'))
        self.assertEqual(E.level(), 'full')
        self.assertTrue(E.report_failure('calendar-worker', 'timeout'))
        self.assertTrue(E.tripped()); self.assertEqual(E.level(), 'lite'); self.assertEqual(E.user_level(), 'full')
        self.assertIn('calendar-worker', E.trip_reason())
        E.set_level('off'); self.assertEqual(E.level(), 'off')
        E.set_level('full'); self.assertTrue(E.clear_health()); self.assertEqual(E.level(), 'full')
        self.assertTrue(E.report_failure('renderer', 'gpu-init-failed', severe=True)); self.assertEqual(E.level(), 'lite')

    def test_casting_cap_is_switchable(self):
        E.clear_health()
        self.assertTrue(E.casting_lite())
        self.assertTrue(E.set_casting(True, 'ExceedShare'))
        self.assertTrue(E.casting()); self.assertEqual(E.casting_app(), 'ExceedShare'); self.assertEqual(E.level(), 'lite')
        E.set_casting_lite(False); self.assertEqual(E.level(), 'full')
        E.set_casting_lite(True); E.set_casting(False, ''); self.assertEqual(E.level(), 'full')

    def test_health_is_per_display(self):
        E.clear_health(); E.set_casting(False, '')
        old = os.environ.get('DISPLAY')
        try:
            os.environ['DISPLAY'] = ':77'
            E.report_failure('renderer', 'gpu-init-failed', severe=True)
            self.assertTrue(E.tripped())
            os.environ['DISPLAY'] = ':0'
            self.assertFalse(E.tripped())                 # the real desktop is unaffected
            self.assertIn('effects-health-:0', E.health_path())
        finally:
            os.environ['DISPLAY'] = ':77'; E.clear_health()
            if old is None: os.environ.pop('DISPLAY', None)
            else: os.environ['DISPLAY'] = old

    def test_corrupt_health_is_ignored(self):
        Path(E.health_path()).parent.mkdir(parents=True, exist_ok=True)
        Path(E.health_path()).write_text('{oops')
        self.assertFalse(E.tripped()); self.assertEqual(E.level(), 'full')

    def test_health_written_by_cpp_is_read_by_python_and_back(self):
        probe = os.environ.get('LQUI_EFFECTS_CPP_PROBE')
        if not probe:
            self.skipTest('set LQUI_EFFECTS_CPP_PROBE to the C++ probe binary')
        E.clear_health(); E.set_casting(True, 'ExceedShare')
        out = subprocess.run([probe], capture_output=True, text=True, timeout=10, env=os.environ).stdout.strip()
        self.assertTrue(out.startswith('level=lite '), out)
        E.set_casting(False, '')

    def test_values_written_by_python_read_by_cpp(self):
        probe = os.environ.get('LQUI_EFFECTS_CPP_PROBE')
        if not probe:
            self.skipTest('set LQUI_EFFECTS_CPP_PROBE to the C++ probe binary')
        E.set_level('balanced'); E.set_motion('off'); E.set_gpu_allowed(False); E.set_pointer_light(False)
        out = subprocess.run([probe], capture_output=True, text=True, timeout=10, env=os.environ).stdout.strip()
        self.assertEqual(out, 'level=balanced motion=off gpu=0 pointer=0')


if __name__ == '__main__':
    unittest.main()
