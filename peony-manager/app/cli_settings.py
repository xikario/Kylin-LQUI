"""User-owned, atomically saved configuration for the lazy native CLI panel."""
import json
import os
from pathlib import Path
import shlex
import shutil
import tempfile

BASE = Path(__file__).resolve().parent.parent
FILE_NAME = "libpeony-cli-panel.so"
BUILD = BASE / "plugins" / "peony-cli-panel" / "build"


def config_path():
    return Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "peony-manager" / "cli-profiles.json"


def defaults():
    program = shutil.which("codex") or str(Path.home() / ".local/bin/codex")
    return {"version": 1, "enabled": True, "name": "Codex CLI",
            "menuLabel": "在此打开 Codex CLI", "command": shlex.quote(program),
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
    for key in ("enabled", "followSystemFont"):
        if key in raw:
            if type(raw[key]) is not bool:
                raise ValueError("%s 必须是布尔值" % key)
            result[key] = raw[key]
    # These are product invariants, not user-controlled startup options.
    result["defaultVisible"] = False
    result["closeWithWindow"] = True
    return result


def read():
    path = config_path()
    if not path.exists():
        return defaults()
    return validate(json.loads(path.read_text(encoding="utf-8")))


def save(raw):
    result = validate(raw)
    path = config_path()
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


def status():
    from app import paths
    directories = paths.get_plugin_dirs()
    target = Path(directories[0] if directories else "/usr/lib/%s-linux-gnu/peony-extensions" % paths.get_arch()) / FILE_NAME
    return {"configPath": str(config_path()), "installed": target.is_file(),
            "targetPath": str(target), "built": (BUILD / FILE_NAME).is_file(),
            "sourcePath": str(BUILD / FILE_NAME), "engine": "原生 Qt / QTermWidget，点击后加载",
            "settings": read()}
