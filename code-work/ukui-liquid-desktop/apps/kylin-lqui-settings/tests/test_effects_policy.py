"""Python effects policy must match the C++ switchboard exactly."""
import os, subprocess, sys, tempfile, unittest
from pathlib import Path

HOME = tempfile.TemporaryDirectory()
os.environ['XDG_CONFIG_HOME'] = HOME.name
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

    def test_values_written_by_python_read_by_cpp(self):
        probe = os.environ.get('LQUI_EFFECTS_CPP_PROBE')
        if not probe:
            self.skipTest('set LQUI_EFFECTS_CPP_PROBE to the C++ probe binary')
        E.set_level('balanced'); E.set_motion('off'); E.set_gpu_allowed(False); E.set_pointer_light(False)
        out = subprocess.run([probe], capture_output=True, text=True, timeout=10, env=os.environ).stdout.strip()
        self.assertEqual(out, 'level=balanced motion=off gpu=0 pointer=0')


if __name__ == '__main__':
    unittest.main()
