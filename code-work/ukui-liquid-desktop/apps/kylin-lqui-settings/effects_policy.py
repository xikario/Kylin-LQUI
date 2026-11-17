"""Global liquid-effects switchboard (Python side of shared/effects/EffectsPolicy.h).

Same file, keys and rules as the C++ header, so the settings app, control
center and the native modules always agree:

    ~/.config/kylin-lqui/appearance.ini  [effects]
        motion        system | on | off
        gpu           auto | off
        pointerLight  true | false

Defaults change nothing. The input method keeps its own settings on purpose.
"""
from pathlib import Path
from PyQt5 import QtCore

MOTION_CHOICES = ('system', 'on', 'off')


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


def motion():
    value = str(_settings().value('effects/motion', 'system'))
    return value if value in MOTION_CHOICES else 'system'


def system_reduce():
    return _bool(QtCore.QSettings('org.ukui', 'style').value('reduceAnimation', False), False)


def reduced_motion(module_own=False):
    """True when animations should be skipped. While the global choice is
    'system', the UKUI setting or the module's own toggle still apply."""
    choice = motion()
    if choice == 'on':
        return True
    if choice == 'off':
        return False
    return system_reduce() or bool(module_own)


def gpu_allowed():
    return str(_settings().value('effects/gpu', 'auto')) != 'off'


def pointer_light(module_own=True):
    return bool(module_own) and _bool(_settings().value('effects/pointerLight', True), True)


def _write(key, value):
    Path(path()).parent.mkdir(parents=True, exist_ok=True)
    settings = _settings()
    settings.setValue(key, value)
    settings.sync()
    return settings.status() == QtCore.QSettings.NoError


def set_motion(value):
    if value not in MOTION_CHOICES:
        raise ValueError('motion must be one of %s' % (MOTION_CHOICES,))
    return _write('effects/motion', value)


def set_gpu_allowed(value):
    return _write('effects/gpu', 'auto' if value else 'off')


def set_pointer_light(value):
    return _write('effects/pointerLight', bool(value))
