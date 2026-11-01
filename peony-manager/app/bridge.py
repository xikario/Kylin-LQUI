"""前后端桥接层。

正式 GUI 通过 QtWebChannel 暴露 PeonyBridge 给内嵌 HTML 调用。
handle_request 保留为内部调试入口，不作为应用的 HTTP 产品形态。
"""

import json
from PyQt5.QtCore import QObject, pyqtSlot
from PyQt5.QtWidgets import QFileDialog

from app import paths, gsettings_store, scanner, operations, cli_settings
from app.models import SystemInfo, GlobalSettings


UI_TO_GSETTINGS_KEY = {
    "menuGlobalOn": "enable-peony-right-menu",
    "desktopFileMenuOn": "enable-desktop-file-menu",
    "desktopBgMenuOn": "enable-desktop-background-menu",
    "openTerminalOn": "show-open-terminal",
    "showExtension": "show-file-extension",
    "showHidden": "show-hidden-file",
}


class PeonyBridge(QObject):
    """QtWebChannel bridge exposed to the embedded HTML UI."""

    @pyqtSlot(result=str)
    def getCliPanelSettings(self) -> str:
        try:
            return json.dumps(dict(success=True, **cli_settings.status()), ensure_ascii=False)
        except (ValueError, OSError) as exc:
            return json.dumps({"success": False, "error": str(exc)}, ensure_ascii=False)

    @pyqtSlot(str, result=str)
    def saveCliPanelSettings(self, payload: str) -> str:
        try:
            settings = cli_settings.save(json.loads(payload))
            return json.dumps({"success": True, "settings": settings}, ensure_ascii=False)
        except (ValueError, OSError) as exc:
            return json.dumps({"success": False, "error": str(exc)}, ensure_ascii=False)

    @pyqtSlot(result=str)
    def installCliPanelPlugin(self) -> str:
        return json.dumps(operations.install_cli_panel_plugin(), ensure_ascii=False)

    @pyqtSlot(result=str)
    def removeCliPanelPlugin(self) -> str:
        return json.dumps(operations.remove_cli_panel_plugin(), ensure_ascii=False)

    @pyqtSlot(result=str)
    def getInitialState(self) -> str:
        return get_initial_state()

    @pyqtSlot(result=str)
    def scanPlugins(self) -> str:
        return json.dumps({
            "plugins": _get_plugins(),
            "pendingChanges": _get_pending_changes(),
        }, ensure_ascii=False)

    @pyqtSlot(result=str)
    def getGlobalSettings(self) -> str:
        return json.dumps(_get_global_settings(), ensure_ascii=False)

    @pyqtSlot(str, bool, result=str)
    def setGlobalSetting(self, key: str, value: bool) -> str:
        result = set_global_setting(key, value)
        return json.dumps(result, ensure_ascii=False)

    @pyqtSlot(bool, result=str)
    def restartPeony(self, kill_only: bool = False) -> str:
        result = operations.restart_peony(kill_only=kill_only)
        return json.dumps(result, ensure_ascii=False)

    @pyqtSlot(result=str)
    def getCopyPathPluginStatus(self) -> str:
        result = operations.copy_path_build_status()
        return json.dumps(result, ensure_ascii=False)

    @pyqtSlot(str, result=str)
    def installCopyPathPlugin(self, source_path: str = "") -> str:
        result = operations.install_copy_path_plugin(source_path)
        return json.dumps(result, ensure_ascii=False)

    @pyqtSlot(result=str)
    def chooseAndInstallCopyPathPlugin(self) -> str:
        file_path, _ = QFileDialog.getOpenFileName(
            None,
            "选择 libpeony-copy-path.so",
            "",
            "Peony 插件 (libpeony-copy-path.so);;共享库 (*.so);;所有文件 (*)",
        )
        if not file_path:
            return json.dumps({"success": False, "code": "cancelled", "error": "已取消选择"}, ensure_ascii=False)
        result = operations.install_copy_path_plugin(file_path)
        return json.dumps(result, ensure_ascii=False)

    @pyqtSlot(result=str)
    def removeCopyPathPlugin(self) -> str:
        result = operations.remove_copy_path_plugin()
        return json.dumps(result, ensure_ascii=False)


def get_initial_state() -> str:
    """获取完整初始状态，返回 JSON 字符串"""
    system = _get_system_info()
    plugins = _get_plugins()
    gs = _get_global_settings()
    return json.dumps({
        "system": system,
        "plugins": plugins,
        "globalSettings": gs,
        "pendingChanges": _get_pending_changes(),
    }, ensure_ascii=False)


def handle_request(path: str, query_params: dict = None) -> str:
    """处理 API 请求，返回 JSON 字符串

    Args:
        path: API 路径，如 'getInitialState', 'scanPlugins'
        query_params: URL 查询参数字典
    """
    if query_params is None:
        query_params = {}
    try:
        if path == "getInitialState" or path == "initial-state":
            return get_initial_state()

        elif path == "scanPlugins" or path == "scan-plugins":
            plugins = _get_plugins()
            return json.dumps({
                "plugins": plugins,
                "pendingChanges": _get_pending_changes(),
            }, ensure_ascii=False)

        elif path == "getGlobalSettings":
            return json.dumps(_get_global_settings(), ensure_ascii=False)

        elif path == "setGlobalSetting":
            key = query_params.get("key", "")
            value = query_params.get("value", "true")
            # 转换布尔值
            if value.lower() == "true":
                val = True
            elif value.lower() == "false":
                val = False
            else:
                val = value
            result = set_global_setting(key, val)
            return json.dumps(result, ensure_ascii=False)

        elif path == "restartPeony":
            kill_only = query_params.get("killOnly", "false") == "true"
            result = operations.restart_peony(kill_only=kill_only)
            return json.dumps(result, ensure_ascii=False)

        else:
            return json.dumps({"error": f"未知路径: {path}"}, ensure_ascii=False)

    except Exception as e:
        return json.dumps({"error": str(e)}, ensure_ascii=False)


def _get_system_info() -> dict:
    return SystemInfo(
        desktop=paths.get_desktop(),
        session_type=paths.get_session_type(),
        arch=paths.get_arch(),
        peony_version=paths.get_peony_version(),
        plugin_dirs=paths.get_plugin_dirs(),
    ).to_dict()


def _get_plugins() -> list:
    return [p.to_dict() for p in scanner.scan_plugins()]


def _get_global_settings() -> dict:
    raw = gsettings_store.get_all()
    settings = raw.get("settings", {})
    gs = GlobalSettings.from_gsettings_dict(settings)
    data = gs.to_dict()
    data["_raw"] = settings
    data["_errors"] = raw.get("errors")
    return data


def set_global_setting(key: str, value) -> dict:
    """Set a global Peony preference using either UI or GSettings key names."""
    gsettings_key = UI_TO_GSETTINGS_KEY.get(key, key)
    if gsettings_key not in gsettings_store.INTERESTING_KEYS:
        return {
            "success": False,
            "key": key,
            "mappedKey": gsettings_key,
            "value": value,
            "error": f"不允许写入未知设置项: {key}",
        }

    if gsettings_key == "disabled-extensions":
        return {
            "success": False,
            "key": key,
            "mappedKey": gsettings_key,
            "value": value,
            "error": "当前版本暂不直接写入 disabled-extensions",
        }

    result = gsettings_store.set(gsettings_key, value)
    result["uiKey"] = key
    result["mappedKey"] = gsettings_key
    return result


def _get_pending_changes() -> list:
    # 当前版本的待变更状态保存在前端会话中；后端扫描只返回系统真实状态。
    # 避免为了一个空列表重复扫描插件目录和 dpkg 元数据。
    return []
