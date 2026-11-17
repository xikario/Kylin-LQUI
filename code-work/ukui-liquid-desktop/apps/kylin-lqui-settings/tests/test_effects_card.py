"""Global liquid-effects card on the settings overview page."""
import os, sys, tempfile, unittest
from pathlib import Path

HOME = tempfile.TemporaryDirectory()
os.environ['XDG_CONFIG_HOME'] = HOME.name
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore, QtWidgets
app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(sys.argv)
import effects_policy as E
from effects_card import GlobalEffectsCard


class EffectsCardTest(unittest.TestCase):
    def setUp(self):
        if Path(E.path()).exists(): Path(E.path()).unlink()
        self.card = GlobalEffectsCard(); self.card.resize(700, 400); self.card.show(); app.processEvents()

    def tearDown(self):
        self.card.close(); self.card.deleteLater(); app.processEvents()

    def test_defaults_shown(self):
        self.assertEqual(self.card.motion.currentIndex(), 0)       # 跟随系统
        self.assertTrue(self.card.gpu.isChecked())
        self.assertTrue(self.card.pointer.isChecked())

    def test_user_changes_are_saved_globally(self):
        self.card.motion.buttons[1].click()                        # 开启
        self.card.gpu.click(); self.card.pointer.click(); app.processEvents()
        self.assertEqual(E.motion(), 'on')
        self.assertFalse(E.gpu_allowed())
        self.assertFalse(E.pointer_light())

    def test_external_change_is_reflected_on_refresh(self):
        E.set_motion('off'); E.set_gpu_allowed(False); E.set_pointer_light(False)
        self.card.refresh()
        self.assertEqual(self.card.motion.currentIndex(), 2)
        self.assertFalse(self.card.gpu.isChecked())
        self.assertFalse(self.card.pointer.isChecked())

    def test_refresh_does_not_write(self):
        before = Path(E.path()).exists()
        self.card.refresh(); app.processEvents()
        self.assertEqual(Path(E.path()).exists(), before)

    def test_accessible_names_describe_each_control(self):
        names = [b.accessibleName() for b in self.card.motion.buttons]
        self.assertEqual(names, ['减弱动画：跟随系统', '减弱动画：开启', '减弱动画：关闭'])
        self.assertEqual(self.card.gpu.accessibleName(), 'GPU 渲染液态玻璃')
        self.assertEqual(self.card.pointer.accessibleName(), '鼠标追光')

    def test_level_choice_defaults_to_full_and_saves(self):
        self.assertEqual(self.card.level.currentIndex(), 0)
        self.assertEqual([b.text() for b in self.card.level.buttons], ['完整', '均衡', '轻量', '关闭'])
        self.card.level.buttons[1].click(); app.processEvents()
        self.assertEqual(E.level(), 'balanced')

    def test_capped_switches_are_disabled_with_reason(self):
        self.card.level.buttons[2].click(); app.processEvents()          # 轻量
        self.assertFalse(self.card.gpu.isEnabled())
        self.assertFalse(self.card.pointer.isEnabled())
        self.assertFalse(self.card.motion.isEnabled())
        self.assertIn('轻量', self.card.level_help.text())
        self.card.level.buttons[0].click(); app.processEvents()          # 完整
        self.assertTrue(self.card.gpu.isEnabled() and self.card.pointer.isEnabled() and self.card.motion.isEnabled())

    def test_level_names_are_accessible(self):
        self.assertEqual([b.accessibleName() for b in self.card.level.buttons],
                         ['效果档位：完整', '效果档位：均衡', '效果档位：轻量', '效果档位：关闭'])

    def test_input_method_is_not_part_of_global(self):
        texts = ' '.join(w.text() for w in self.card.findChildren(QtWidgets.QLabel))
        self.assertIn('输入法', texts)                              # explains it stays separate


if __name__ == '__main__':
    unittest.main()
