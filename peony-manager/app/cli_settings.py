"""User-owned, atomically saved configuration for the lazy native CLI panel."""
import json
import os
import fcntl
from contextlib import contextmanager
from pathlib import Path
import shlex
import shutil
import tempfile
from app import codex_profile

BASE = Path(__file__).resolve().parent.parent
FILE_NAME = "libpeony-cli-panel.so"
BUILD = BASE / "plugins" / "peony-cli-panel" / "build"
REASONING_EFFORTS = codex_profile.REASONING_EFFORTS
MODEL_ID = codex_profile.MODEL_ID


def config_path():
    return Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "peony-manager" / "cli-profiles.json"


def lock_path():
    return Path(str(config_path()) + ".lock")


@contextmanager
def _config_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(str(path) + ".lock", os.O_CREAT | os.O_RDWR, 0o600)
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, "r+") as stream:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def _codex_executable():
    return shutil.which("codex") or str(Path.home() / ".local/bin/codex")


def _model_args(model, reasoning_effort):
    args = []
    if model:
        args.extend(("--model", model))
    if reasoning_effort:
        args.extend(("--config", 'model_reasoning_effort="%s"' % reasoning_effort))
    return args


def defaults():
    program = _codex_executable()
    command = shlex.quote(program)
    return {"version": 1, "enabled": True, "name": "Codex CLI",
            "menuLabel": "在此打开 Codex CLI", "command": command,
            "managedCommand": command, "codexExecutable": program,
            "model": "", "reasoningEffort": "", "modelArgs": [], "modelArgsApplicable": True,
            "widthPercent": 40, "fontSize": 11, "historyLines": 2000,
            "defaultVisible": False, "closeWithWindow": True, "followSystemFont": True,
            "fontFamily": "Sarasa Fixed SC", "contentPadding": 10}


def validate(raw):
    if not isinstance(raw, dict):
        raise ValueError("配置必须是对象")
    result = defaults()
    for key, limit in (("name", 80), ("menuLabel", 120), ("command", 4096)):
        value = raw.get(key, result[key])
        if not isinstance(value, str) or not value.strip() or len(value) > limit or "\0" in value:
            raise ValueError("名称、菜单文字和启动命令不能为空或超过长度限制")
        result[key] = value.strip()
    for key, low, high in (("widthPercent", 25, 65), ("fontSize", 8, 24), ("historyLines", 100, 10000), ("contentPadding", 0, 24)):
        value = raw.get(key, result[key])
        if type(value) is not int or not low <= value <= high:
            raise ValueError("%s 应在 %s 到 %s 之间" % (key, low, high))
        result[key] = value
    family = raw.get("fontFamily", result["fontFamily"])
    if family not in ("Sarasa Fixed SC", "Noto Sans Mono CJK SC"):
        raise ValueError("请选择可用的终端字体")
    result["fontFamily"] = family
    model = raw.get("model", result["model"])
    if not isinstance(model, str) or len(model) > 128 or "\0" in model:
        raise ValueError("model 必须是空值或有效的模型标识")
    model = model.strip()
    if model and not MODEL_ID.fullmatch(model):
        raise ValueError("model 必须是空值或由字母、数字、点、下划线、冒号、斜线、加号和连字符组成的模型标识")
    result["model"] = model
    effort = raw.get("reasoningEffort", result["reasoningEffort"])
    if not isinstance(effort, str) or effort not in ("",) + REASONING_EFFORTS:
        raise ValueError("reasoningEffort 必须为空或 none/minimal/low/medium/high/xhigh/max/ultra")
    result["reasoningEffort"] = effort
    result["modelArgs"] = _model_args(model, effort)
    result["modelArgsApplicable"] = result["command"] == result["managedCommand"]
    for key in ("enabled", "followSystemFont"):
        if key in raw:
            if type(raw[key]) is not bool:
                raise ValueError("%s 必须是布尔值" % key)
            result[key] = raw[key]
    # These are product invariants, not user-controlled startup options.
    result["defaultVisible"] = False
    result["closeWithWindow"] = True
    return result


def _read_unlocked(path):
    if not path.exists():
        return defaults()
    return validate(json.loads(path.read_text(encoding="utf-8")))


def read():
    # Atomic replacement means a lock-free read sees either complete version.
    result = _read_unlocked(config_path())
    profile = codex_profile.profile_path()
    if profile.exists():
        # A present profile is authoritative.  Missing profile keys mean
        # "follow Codex's base configuration", even if old Peony JSON has a
        # stale preference from before profile support.
        preferences = codex_profile.read_preferences()
    else:
        # Before the native launcher creates the dedicated profile, expose
        # old Peony preferences as migration seeds without creating files.
        preferences = {"model": result["model"], "reasoningEffort": result["reasoningEffort"]}
    result.update(preferences)
    result["modelArgs"] = _model_args(result["model"], result["reasoningEffort"])
    result["modelArgsApplicable"] = result["command"] == result["managedCommand"]
    return result


def _write_unlocked(path, result):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=".cli-", suffix=".json", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(result, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, str(path))
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    return result


def _save_locked(raw, profile_changes=None):
    path = config_path()
    explicit_keys = set(raw) if isinstance(raw, dict) else set()
    candidate = dict(raw) if isinstance(raw, dict) else raw
    if isinstance(candidate, dict) and path.exists():
        # Older Peony pages submit their known fields only. Keep the new model
        # preferences unless the caller explicitly sends an empty value.
        previous = _read_unlocked(path)
        for key in ("model", "reasoningEffort"):
            if key not in candidate:
                candidate[key] = previous[key]
    result = validate(candidate)
    if profile_changes is None:
        profile_changes = {key: result[key] for key in ("model", "reasoningEffort")
                           if key in explicit_keys}
    if profile_changes:
        previous = _read_unlocked(path) if path.exists() else defaults()
        codex_profile.update_preferences(
            profile_changes,
            seed={"model": previous["model"], "reasoningEffort": previous["reasoningEffort"]},
        )
    _write_unlocked(path, result)
    # Return the effective profile values (which can differ from legacy
    # modelArgs when Codex's own /model picker has saved a new choice).
    return read()


def _save(raw, profile_changes=None):
    path = config_path()
    with _config_lock(path):
        return _save_locked(raw, profile_changes)


def save(raw):
    return _save(raw)


def update(changes):
    if not isinstance(changes, dict):
        raise ValueError("更新内容必须是对象")
    path = config_path()
    with _config_lock(path):
        current = _read_unlocked(path)
        current.update(changes)
        profile_changes = {key: current[key] for key in ("model", "reasoningEffort") if key in changes}
        return _save_locked(current, profile_changes)


def status():
    from app import paths
    directories = paths.get_plugin_dirs()
    target = Path(directories[0] if directories else "/usr/lib/%s-linux-gnu/peony-extensions" % paths.get_arch()) / FILE_NAME
    payload=Path('/usr/share/kylin-lqui/peony-plugins')
    parts=[payload/FILE_NAME]+[payload/'peony-cli-panel'/name for name in ('libpeony-cli-terminal.so','libqtermwidget5.so.0','libutf8proc.so.2','peony-cli-session','data/kb-layouts/default.keytab')]
    packaged=all(part.is_file() for part in parts)
    helper=Path('/usr/libexec/kylin-lqui/peony-manager-helper').is_file()
    return {"packagedPayloadReady":packaged,"helperReady":helper,"installable":packaged and helper,"configPath": str(config_path()), "installed": target.is_file(),
            "targetPath": str(target), "built": (BUILD / FILE_NAME).is_file(),
            "sourcePath": str(BUILD / FILE_NAME), "engine": "原生 Qt / QTermWidget，点击后加载",
            "settings": read()}
