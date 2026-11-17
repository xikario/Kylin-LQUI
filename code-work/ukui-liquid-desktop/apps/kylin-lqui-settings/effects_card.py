"""Global liquid-effects card for the settings overview page.

One place to turn the shared effects down for every Kylin-LQUI module
(taskbar, desktop, start menu, control center). The input method keeps its
own page on purpose.
"""
from PyQt5 import QtCore, QtWidgets
import effects_policy
from design import Switch
from ui_components import SettingsGroup, SettingRow, RadioChoice, StatusLabel, label

MOTION_OPTIONS = (('跟随系统', 'system'), ('开启', 'on'), ('关闭', 'off'))


class GlobalEffectsCard(SettingsGroup):
    def __init__(self):
        super().__init__('液态特效（全局）')
        self.setObjectName('globalEffectsCard')

        self.motion = RadioChoice([text for text, _ in MOTION_OPTIONS])
        for button, (text, _) in zip(self.motion.buttons, MOTION_OPTIONS):
            button.setAccessibleName('减弱动画：' + text)
        self.motion.activated.connect(self.save_motion)
        self.add_row(SettingRow('减弱动画', self.motion,
            '跟随系统时，UKUI「减少动画」或各模块自己的开关任一开启即生效；选择开启或关闭则统一覆盖所有模块。'))

        self.gpu = Switch('GPU 渲染液态玻璃')
        self.gpu.clicked.connect(lambda checked: self.save(effects_policy.set_gpu_allowed, checked))
        self.add_row(SettingRow('GPU 渲染', self.gpu,
            '关闭后所有液态玻璃改用 CPU 绘制，折射效果减弱但更稳定。显卡较弱、投屏或出现卡顿时建议关闭。', boolean=True))

        self.pointer = Switch('鼠标追光')
        self.pointer.clicked.connect(lambda checked: self.save(effects_policy.set_pointer_light, checked))
        self.add_row(SettingRow('鼠标追光', self.pointer,
            '总开关。关闭后任务栏、桌面分区和开始菜单的鼠标追光全部停止，各模块自己的开关保留。', boolean=True))

        note = label('改动立即作用于正在运行的模块。输入法外观仍在「输入法」页单独设置。', True)
        note.setContentsMargins(12, 0, 12, 0)
        self.box.addWidget(note)
        self.status = StatusLabel(); self.status.setProperty('secondary', True); self.status.setContentsMargins(12, 0, 12, 0)
        self.box.addWidget(self.status)
        self.refresh_rows()
        self.refresh()

    def refresh(self):
        """Show the stored policy without writing it (another process may own it)."""
        choice = effects_policy.motion()
        index = [value for _, value in MOTION_OPTIONS].index(choice)
        if self.motion.currentIndex() != index:
            self.motion.setCurrentIndex(index)
        for control, value in ((self.gpu, effects_policy.gpu_allowed()), (self.pointer, effects_policy.pointer_light())):
            if control.isChecked() != value:
                blocker = QtCore.QSignalBlocker(control); control.setChecked(value); del blocker

    def save_motion(self, index):
        self.save(effects_policy.set_motion, MOTION_OPTIONS[index][1])

    def save(self, setter, value):
        ok = setter(value)
        self.status.setText('' if ok else '保存失败：请检查 ~/.config/kylin-lqui 的写入权限。')
        if not ok:
            self.refresh()
