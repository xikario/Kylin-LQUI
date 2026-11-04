"""GSettings 配置读写封装"""

import subprocess
import json
import re

SCHEMA = "org.ukui.peony.settings"

# 关注的 key 列表
INTERESTING_KEYS = [
    "enable-peony-right-menu",
    "enable-desktop-file-menu",
    "enable-desktop-background-menu",
    "show-open-terminal",
    "show-file-extension",
    "show-hidden-file",
    "disabled-extensions",
]

# 布尔值 key
BOOLEAN_KEYS = {
    "enable-peony-right-menu",
    "enable-desktop-file-menu",
    "enable-desktop-background-menu",
    "show-open-terminal",
    "show-file-extension",
    "show-hidden-file",
}


def get_all() -> dict:
    """读取所有关注的 GSettings key，返回 {key: value} 字典"""
    result = {}
    errors = {}
    for key in INTERESTING_KEYS:
        try:
            value = _get(key)
            result[key] = value
        except Exception as e:
            errors[key] = str(e)
    return {"settings": result, "errors": errors if errors else None}


def get(key: str):
    """读取单个 GSettings key"""
    return _get(key)


def set(key: str, value) -> dict:
    """写入 GSettings key"""
    try:
        str_val = _format_value(value)
        subprocess.run(
            ["gsettings", "set", SCHEMA, key, str_val],
            check=True, capture_output=True, text=True, timeout=5,
        )
        return {"success": True, "key": key, "value": value}
    except subprocess.CalledProcessError as e:
        return {"success": False, "key": key, "value": value, "error": e.stderr.strip()}
    except Exception as e:
        return {"success": False, "key": key, "value": value, "error": str(e)}


def get_disabled_extensions():
    """获取禁用插件列表"""
    try:
        raw = _get("disabled-extensions")
        if isinstance(raw, list):
            return raw
        return None
    except Exception:
        return None


def reset_boolean_defaults(keys):
    """Restore only displayed file-manager switches, never extension state."""
    from gi.repository import Gio
    if not keys or any(key not in BOOLEAN_KEYS for key in keys):
        raise ValueError('只能恢复文件管理的显示与菜单设置')
    source=Gio.SettingsSchemaSource.get_default()
    schema=source.lookup(SCHEMA,True) if source else None
    if not schema:raise ValueError('文件管理器设置服务不可用')
    settings=Gio.Settings.new_full(schema,None,None)
    if any(not schema.has_key(key) or not settings.is_writable(key) for key in keys):
        raise ValueError('部分设置不可修改，未恢复默认值')
    settings.delay()
    for key in keys:settings.reset(key)
    settings.apply();Gio.Settings.sync()


def _get(key: str):
    """底层 gsettings get 调用"""
    result = subprocess.run(
        ["gsettings", "get", SCHEMA, key],
        capture_output=True, text=True, timeout=5,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip())

    output = result.stdout.strip()

    if key in BOOLEAN_KEYS:
        return output.lower() == "true"

    if key == "disabled-extensions":
        return _parse_string_array(output)

    return output


def _format_value(value) -> str:
    """将 Python 值格式化为 gsettings 命令行参数"""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, list):
        if not value:
            return "@as []"
        from gi.repository import GLib
        return GLib.Variant('as',value).print_(True)
    return str(value)


def _parse_string_array(text: str) -> list:
    """解析 gsettings 的字符串数组输出为 Python list"""
    text = text.strip()
    if text.startswith("@as"):
        text = text[3:].strip()
    if not text or text == "[]":
        return []
    text = text.strip("[]").strip()
    if not text:
        return []
    # 用正则提取引号内的内容
    return re.findall(r"'([^']*)'", text)
