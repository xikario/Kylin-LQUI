"""Global liquid-effects card for the settings overview page.

One place to turn the shared effects down for every Kylin-LQUI module
(taskbar, desktop, start menu, control center). The input method keeps its
own page on purpose.
"""
from PyQt5 import QtCore, QtWidgets
import effects_policy
from design import Switch
from ui_components import SettingsGroup, SettingRow, RadioChoice, StatusLabel, label

LEVEL_OPTIONS = (('完整', 'full'), ('均衡', 'balanced'), ('轻量', 'lite'), ('关闭', 'off'))
LEVEL_HELP = {
    'full': '完整：全部液态效果，包括 GPU 折射、鼠标追光、悬停放大和完整动画。',
    'balanced': '均衡：保留 GPU 玻璃质感，停用鼠标追光等逐帧装饰，动画缩短到 70%。适合性能一般的电脑。',
    'lite': '轻量：玻璃改用 CPU 绘制，不再截屏取背景，关闭动画。适合显卡较弱、投屏或卡顿时。',
    'off': '关闭：不绘制液态玻璃，使用纯色底板，关闭动画。最省资源。',
}
# Stored values keep their meaning ('on' = reduce motion); only the labels
# say what the user gets, so the choice cannot be read backwards.
MOTION_OPTIONS = (('跟随系统', 'system'), ('完整', 'off'), ('关闭', 'on'))


class GlobalEffectsCard(SettingsGroup):
    def __init__(self):
        super().__init__('液态特效（全局）')
        self.setObjectName('globalEffectsCard')

        self.level = RadioChoice([text for text, _ in LEVEL_OPTIONS])
        for button, (text, _) in zip(self.level.buttons, LEVEL_OPTIONS):
            button.setAccessibleName('效果档位：' + text)
        self.level.activated.connect(self.save_level)
        level_row = SettingRow('效果档位', self.level, LEVEL_HELP['full'])
        self.level_help = level_row.help
        self.add_row(level_row)

        self.motion = RadioChoice([text for text, _ in MOTION_OPTIONS])
        for button, (text, _) in zip(self.motion.buttons, MOTION_OPTIONS):
            button.setAccessibleName('动画：' + text)
        self.motion.activated.connect(self.save_motion)
        self.motion_row = SettingRow('动画', self.motion,
            '跟随系统：UKUI「减少动画」或各模块自己的开关任一开启时不播放动画。完整或关闭则统一覆盖所有模块。')
        self.add_row(self.motion_row)

        self.gpu = Switch('GPU 渲染液态玻璃')
        self.gpu.clicked.connect(lambda checked: self.save(effects_policy.set_gpu_allowed, checked))
        self.add_row(SettingRow('GPU 渲染', self.gpu,
            '关闭后所有液态玻璃改用 CPU 绘制，折射效果减弱但更稳定。显卡较弱、投屏或出现卡顿时建议关闭。', boolean=True))

        self.pointer = Switch('鼠标追光')
        self.pointer.clicked.connect(lambda checked: self.save(effects_policy.set_pointer_light, checked))
        self.add_row(SettingRow('鼠标追光', self.pointer,
            '总开关。关闭后任务栏、桌面分区和开始菜单的鼠标追光全部停止，各模块自己的开关保留。', boolean=True))

        self.casting_switch = Switch('投屏时自动轻量')
        self.casting_switch.clicked.connect(lambda checked: self.save(effects_policy.set_casting_lite, checked))
        self.add_row(SettingRow('投屏时自动轻量', self.casting_switch,
            '检测到投屏程序（如 MAXHUB 传屏 ExceedShare）运行时，临时切到轻量；投屏结束自动恢复。', boolean=True))

        # Session status: automatic degradation or casting cap, with a way back.
        self.health_row = QtWidgets.QWidget(); line = QtWidgets.QHBoxLayout(self.health_row)
        line.setContentsMargins(12, 4, 12, 4); line.setSpacing(12)
        self.health_text = label('', True); line.addWidget(self.health_text, 1)
        self.restore = QtWidgets.QPushButton('恢复'); self.restore.setAccessibleName('恢复液态特效')
        self.restore.setToolTip('清除本次登录的故障记录，回到你选择的档位；再次出现故障会重新自动降级。')
        self.restore.clicked.connect(lambda: self.save(lambda _: effects_policy.clear_health(), None))
        line.addWidget(self.restore)
        self.box.addWidget(self.health_row)

        note = label('档位是上限，下面三项可在档位之内单独调整。改动立即作用于正在运行的模块。输入法外观仍在「输入法」页单独设置。', True)
        note.setContentsMargins(12, 0, 12, 0)
        self.box.addWidget(note)
        self.status = StatusLabel(); self.status.setProperty('secondary', True); self.status.setContentsMargins(12, 0, 12, 0)
        self.box.addWidget(self.status)
        self.refresh_rows()
        self.refresh()

    def refresh(self):
        """Show the stored policy without writing it (another process may own it)."""
        chosen = effects_policy.user_level()
        level = effects_policy.level()          # effective, after automatic caps
        index = [value for _, value in LEVEL_OPTIONS].index(chosen)
        if self.level.currentIndex() != index:
            self.level.setCurrentIndex(index)
        self.level_help.setText(LEVEL_HELP[chosen])
        motion = [value for _, value in MOTION_OPTIONS].index(effects_policy.motion())
        if self.motion.currentIndex() != motion:
            self.motion.setCurrentIndex(motion)
        # Show the user's own switch values; the tier only disables them.
        for control, value in ((self.gpu, effects_policy.gpu_switch()), (self.pointer, effects_policy.pointer_switch()),
                               (self.casting_switch, effects_policy.casting_lite())):
            if control.isChecked() != value:
                blocker = QtCore.QSignalBlocker(control); control.setChecked(value); del blocker
        self.motion.setEnabled(level in ('full', 'balanced'))
        self.gpu.setEnabled(level in ('full', 'balanced'))
        self.pointer.setEnabled(level == 'full')
        tripped = effects_policy.tripped()
        casting = effects_policy.casting() and effects_policy.casting_lite()
        if tripped:
            self.health_text.setText('已自动降到轻量：%s。本次登录内保持，重新登录或点「恢复」后回到你选的档位。'
                                     % (effects_policy.trip_reason() or '渲染故障'))
        elif casting:
            self.health_text.setText('正在投屏（%s）：临时使用轻量，投屏结束后自动恢复。' % (effects_policy.casting_app() or '投屏程序'))
        capped = (tripped or casting) and level != chosen
        self.health_row.setHidden(not capped)
        self.restore.setVisible(tripped)

    def save_level(self, index):
        self.save(effects_policy.set_level, LEVEL_OPTIONS[index][1])

    def save_motion(self, index):
        self.save(effects_policy.set_motion, MOTION_OPTIONS[index][1])

    def save(self, setter, value):
        ok = setter(value)
        self.status.setText('' if ok else '保存失败：请检查 ~/.config/kylin-lqui 的写入权限。')
        self.refresh()
