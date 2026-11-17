"""Global liquid-effects switchboard (Python side of shared/effects/EffectsPolicy.h).

Same file, keys and rules as the C++ header, so the settings app, control
center and the native modules always agree:

    ~/.config/kylin-lqui/appearance.ini  [effects]
        level         full | balanced | lite | off   (a cap on everything below)
        motion        system | on | off
        gpu           auto | off
        pointerLight  true | false

Defaults change nothing. The input method keeps its own settings on purpose.
"""
from pathlib import Path
from PyQt5 import QtCore

MOTION_CHOICES = ('system', 'on', 'off')
LEVEL_CHOICES = ('full', 'balanced', 'lite', 'off')


def path():
    base = QtCore.QStandardPaths.writableLocation(QtCore.QStandardPaths.ConfigLocation)
    return str(Path(base) / 'kylin-lqui' / 'appearance.ini')


def _settings():
    return QtCore.QSettings(path(), QtCore.QSettings.IniFormat)


def _bool(value, default):
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return {'true': True, '1': True, 'false': False, '0': False}.get(value.strip().lower(), default)
    return default


def level():
    value = str(_settings().value('effects/level', 'full'))
    return value if value in LEVEL_CHOICES else 'full'


def motion():
    value = str(_settings().value('effects/motion', 'system'))
    return value if value in MOTION_CHOICES else 'system'


def system_reduce():
    return _bool(QtCore.QSettings('org.ukui', 'style').value('reduceAnimation', False), False)


def reduced_motion(module_own=False):
    """True when animations should be skipped. While the global choice is
    'system', the UKUI setting or the module's own toggle still apply."""
    if level() in ('lite', 'off'):
        return True
    choice = motion()
    if choice == 'on':
        return True
    if choice == 'off':
        return False
    return system_reduce() or bool(module_own)


def motion_scale(module_own=False):
    """Multiply animation durations by this; 0 means jump to the end state."""
    if reduced_motion(module_own):
        return 0.0
    return 0.7 if level() == 'balanced' else 1.0


def glass_material():
    return level() != 'off'


def gpu_allowed():
    return gpu_switch() and level() in ('full', 'balanced')


def live_capture():
    """Screen grabs for glass backdrops are allowed."""
    return level() in ('full', 'balanced')


def decorative_frames():
    """Per-frame decoration: pointer light, hover magnification, click ripples."""
    return level() == 'full'


def pointer_light(module_own=True):
    return bool(module_own) and pointer_switch() and decorative_frames()


def gpu_switch():
    """The user's own GPU switch, before the tier cap (for the settings UI)."""
    return str(_settings().value('effects/gpu', 'auto')) != 'off'


def pointer_switch():
    """The user's own pointer-light switch, before the tier cap."""
    return _bool(_settings().value('effects/pointerLight', True), True)


def _write(key, value):
    Path(path()).parent.mkdir(parents=True, exist_ok=True)
    settings = _settings()
    settings.setValue(key, value)
    settings.sync()
    return settings.status() == QtCore.QSettings.NoError


def set_level(value):
    if value not in LEVEL_CHOICES:
        raise ValueError('level must be one of %s' % (LEVEL_CHOICES,))
    return _write('effects/level', value)


def set_motion(value):
    if value not in MOTION_CHOICES:
        raise ValueError('motion must be one of %s' % (MOTION_CHOICES,))
    return _write('effects/motion', value)


def set_gpu_allowed(value):
    return _write('effects/gpu', 'auto' if value else 'off')


def set_pointer_light(value):
    return _write('effects/pointerLight', bool(value))
