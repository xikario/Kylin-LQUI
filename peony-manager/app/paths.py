"""系统路径和架构检测"""

import os
import platform
import subprocess


def get_arch() -> str:
    """返回当前系统架构"""
    machine = platform.machine().lower()
    return machine


def get_plugin_dirs() -> list:
    """返回 Peony 插件目录列表（已去重）"""
    arch = get_arch()
    dirs = [
        f"/usr/lib/{arch}-linux-gnu/peony-extensions",
        f"/lib/{arch}-linux-gnu/peony-extensions",
    ]
    seen = set()
    result = []
    for d in dirs:
        real = os.path.realpath(d)
        if real not in seen and os.path.isdir(d):
            seen.add(real)
            result.append(d)
    return result


def get_peony_version() -> str:
    """获取 Peony 版本号"""
    try:
        result = subprocess.run(
            ["peony", "--version"],
            capture_output=True, text=True, timeout=5
        )
        # 输出格式: "文件管理器 v3.2.2" 或类似
        for line in result.stdout.splitlines():
            line = line.strip()
            if "v" in line:
                return line.split("v")[-1].strip()
        return result.stdout.strip()
    except Exception:
        return "未知"


def get_session_type() -> str:
    """获取当前会话类型"""
    session = os.environ.get("XDG_SESSION_TYPE", "")
    if session:
        return session.lower()
    desktop = os.environ.get("XDG_CURRENT_DESKTOP", "")
    return desktop.lower() or "x11"


def get_desktop() -> str:
    """获取当前桌面环境"""
    return os.environ.get("XDG_CURRENT_DESKTOP", "UKUI")
