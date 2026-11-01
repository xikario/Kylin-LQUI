"""Peony Manager 数据模型"""

from dataclasses import dataclass, field, asdict
from typing import List, Optional


@dataclass
class SystemInfo:
    desktop: str = "UKUI"
    session_type: str = "x11"
    arch: str = "aarch64"
    peony_version: str = ""
    plugin_dirs: List[str] = field(default_factory=list)
    schema: str = "org.ukui.peony.settings"

    def to_dict(self):
        return asdict(self)


@dataclass
class PluginInfo:
    id: str = ""
    name: str = ""
    description: str = ""
    package: str = ""
    version: str = ""
    file_name: str = ""
    path: str = ""
    category: str = "context-menu"
    type: str = "右键菜单扩展"
    source: str = "UKUI 组件"
    risk_level: str = "low"
    risk_text: str = "低风险：只增加右键菜单"
    conditions: str = ""
    dependencies: List[str] = field(default_factory=list)
    installed: bool = True
    enabled: bool = True
    pending_enabled: Optional[bool] = None
    requires_restart: bool = False

    def to_dict(self):
        d = asdict(self)
        # 前端使用 camelCase
        return {
            "id": d["id"],
            "name": d["name"],
            "description": d["description"],
            "package": d["package"],
            "version": d["version"],
            "fileName": d["file_name"],
            "path": d["path"],
            "category": d["category"],
            "type": d["type"],
            "source": d["source"],
            "riskLevel": d["risk_level"],
            "riskText": d["risk_text"],
            "conditions": d["conditions"],
            "dependencies": d["dependencies"],
            "installed": d["installed"],
            "enabled": d["enabled"],
            "pendingState": d["pending_enabled"] if d["pending_enabled"] is not None else d["enabled"],
            "requiresRestart": d["requires_restart"],
        }


@dataclass
class GlobalSettings:
    enable_peony_right_menu: bool = True
    enable_desktop_file_menu: bool = True
    enable_desktop_background_menu: bool = True
    show_open_terminal: bool = True
    show_file_extension: bool = True
    show_hidden_file: bool = False
    disabled_extensions: List[str] = field(default_factory=list)

    def to_dict(self):
        d = asdict(self)
        return {
            "menuGlobalOn": d["enable_peony_right_menu"],
            "desktopFileMenuOn": d["enable_desktop_file_menu"],
            "desktopBgMenuOn": d["enable_desktop_background_menu"],
            "openTerminalOn": d["show_open_terminal"],
            "showExtension": d["show_file_extension"],
            "showHidden": d["show_hidden_file"],
            "disabledExtensions": d["disabled_extensions"],
        }

    @classmethod
    def from_gsettings_dict(cls, raw):
        """从 GSettings key-value 映射创建 GlobalSettings"""
        return cls(
            enable_peony_right_menu=_gbool(raw.get("enable-peony-right-menu", "true")),
            enable_desktop_file_menu=_gbool(raw.get("enable-desktop-file-menu", "true")),
            enable_desktop_background_menu=_gbool(raw.get("enable-desktop-background-menu", "true")),
            show_open_terminal=_gbool(raw.get("show-open-terminal", "true")),
            show_file_extension=_gbool(raw.get("show-file-extension", "true")),
            show_hidden_file=_gbool(raw.get("show-hidden-file", "false")),
            disabled_extensions=raw.get("disabled-extensions", []),
        )


def _gbool(val):
    if isinstance(val, bool):
        return val
    if isinstance(val, str):
        return val.lower() == "true"
    return False
