"""Global liquid-effects switchboard (Python side of shared/effects/EffectsPolicy.h).

Same files, keys and rules as the C++ header, so the settings app, control
center, calendar and the native modules always agree:

    ~/.config/kylin-lqui/appearance.ini  [effects]
        level         full | balanced | lite | off   (a cap on everything below)
        motion        system | on | off
        gpu           auto | off
        pointerLight  true | false
        castingLite   true | false   (drop to lite while a casting app runs)

    $XDG_RUNTIME_DIR/kylin-lqui/effects-health-<DISPLAY>-<X server>.json   (this login only)
        failures reported by renderers/helpers; one severe failure or two
        within 10 minutes trip the session to lite. The taskbar records
        whether a casting app runs. The effective tier is the stricter of the
        user's tier and these caps; the user's choice is never changed.

Defaults change nothing. The input method keeps its own settings on purpose.
"""
import json
import os
import tempfile
import threading
import time
from pathlib import Path
from PyQt5 import QtCore

MOTION_CHOICES = ('system', 'on', 'off')
LEVEL_CHOICES = ('full', 'balanced', 'lite', 'off')
FAILURE_WINDOW_SECS = 600
FAILURES_TO_TRIP = 2
_health_lock = threading.Lock()


def path():
    base = QtCore.QStandardPaths.writableLocation(QtCore.QStandardPaths.ConfigLocation)
    return str(Path(base) / 'kylin-lqui' / 'appearance.ini')


def health_path():
    runtime = QtCore.QStandardPaths.writableLocation(QtCore.QStandardPaths.RuntimeLocation)
    if not runtime:
        runtime = str(Path(tempfile.gettempdir()) / ('kylin-lqui-%d' % os.getpid()))
    # One file per X display, same rule as C++: test runs never touch :0.
    display = os.environ.get('DISPLAY') or 'nodisplay'
    # ...and per X server instance (socket ctime), same as C++.
    instance = ''
    if ':' in display:
        number = display.rsplit(':', 1)[1].split('.', 1)[0]
        try:
            if number:
                instance = '-%x' % int(os.stat('/tmp/.X11-unix/X' + number).st_ctime)
        except OSError:
            pass
    display = ''.join(c if (c.isalnum() or c in '.:') else '_' for c in display)
    return str(Path(runtime) / 'kylin-lqui' / ('effects-health-%s%s.json' % (display, instance)))


def _settings():
    return QtCore.QSettings(path(), QtCore.QSettings.IniFormat)


def _bool(value, default):
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return {'true': True, '1': True, 'false': False, '0': False}.get(value.strip().lower(), default)
    return default


# ---- session health --------------------------------------------------------

def _read_health():
    """Unreadable or corrupt health never disables anything."""
    try:
        data = json.loads(Path(health_path()).read_text())
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def _edit_health(edit):
    """Read-modify-write, locked against other processes (QLockFile, same as C++)."""
    target = Path(health_path())
    target.parent.mkdir(parents=True, exist_ok=True)
    with _health_lock:
        lock = QtCore.QLockFile(str(target) + '.lock')
        lock.setStaleLockTime(5000)
        if not lock.tryLock(2000):
            return False
        try:
            health = _read_health()
            before = json.dumps(health, sort_keys=True)
            edit(health)
            if json.dumps(health, sort_keys=True) == before:
                return True
            staged = target.with_name(target.name + '.%d.tmp' % os.getpid())
            staged.write_text(json.dumps(health, ensure_ascii=False, separators=(',', ':')))
            os.replace(str(staged), str(target))
            return True
        except OSError:
            return False
        finally:
            lock.unlock()


def tripped():
    return bool(_read_health().get('tripped'))


def trip_reason():
    return str(_read_health().get('reason', ''))


def casting():
    return bool(_read_health().get('casting'))


def casting_app():
    return str(_read_health().get('castingApp', ''))


def casting_lite():
    return _bool(_settings().value('effects/castingLite', True), True)


def report_failure(source, kind, severe=False):
    """Record a rendering failure; True when this call tripped the session to lite."""
    result = {'tripped': False}

    def edit(health):
        now = int(time.time())
        recent = [f for f in health.get('failures', []) if isinstance(f, dict)
                  and now - int(f.get('t', 0)) <= FAILURE_WINDOW_SECS]
        recent.append({'t': now, 'source': source, 'kind': kind})
        health['failures'] = recent[-20:]
        if not health.get('tripped') and (severe or len(recent) >= FAILURES_TO_TRIP):
            health.update(tripped=True, reason='%s: %s' % (source, kind), since=now)
            result['tripped'] = True

    _edit_health(edit)
    return result['tripped']


def clear_health():
    """'Restore' in the settings card: forget failures for this session."""
    def edit(health):
        for key in ('tripped', 'reason', 'since', 'failures'):
            health.pop(key, None)
    return _edit_health(edit)


def set_casting(active, app):
    def edit(health):
        if active:
            health.update(casting=True, castingApp=app)
        else:
            health.pop('casting', None)
            health.pop('castingApp', None)
    return _edit_health(edit)


# ---- tiers and switches ----------------------------------------------------

def user_level():
    """The tier the user chose (shown in the settings card)."""
    value = str(_settings().value('effects/level', 'full'))
    return value if value in LEVEL_CHOICES else 'full'


def level():
    """Effective tier: the stricter of the user's tier and the session caps."""
    chosen = user_level()
    health = _read_health()
    capped = bool(health.get('tripped')) or (bool(health.get('casting')) and casting_lite())
    if capped and LEVEL_CHOICES.index(chosen) < LEVEL_CHOICES.index('lite'):
        return 'lite'
    return chosen


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


def gpu_switch():
    """The user's own GPU switch, before the tier cap (for the settings UI)."""
    return str(_settings().value('effects/gpu', 'auto')) != 'off'


def pointer_switch():
    """The user's own pointer-light switch, before the tier cap."""
    return _bool(_settings().value('effects/pointerLight', True), True)


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


def set_casting_lite(value):
    return _write('effects/castingLite', bool(value))
