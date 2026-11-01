"""Peony 操作封装：重启、安装、卸载等"""

import os
import json
import shutil
import subprocess
import stat
from pathlib import Path

from app import paths
from app import cli_settings


BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HELPER_PATH = "/usr/libexec/kylin-lqui/peony-manager-helper"
COPY_PATH_FILE_NAME = "libpeony-copy-path.so"
COPY_PATH_BUILD_CANDIDATES = [
    os.path.join(BASE_DIR, "plugins", "peony-copy-path", "build", COPY_PATH_FILE_NAME),
    os.path.join(BASE_DIR, "plugins", "peony-copy-path", COPY_PATH_FILE_NAME),
]
VENDOR_QT_DIR = os.path.join(BASE_DIR, "vendor", "extract", "qt")
VENDOR_MOC = os.path.join(VENDOR_QT_DIR, "usr", "lib", "qt5", "bin", "moc")


def install_cli_panel_plugin() -> dict:
    source = Path('/usr/share/kylin-lqui/peony-plugins') / cli_settings.FILE_NAME
    runtime = source.parent / "peony-cli-panel"
    needed = ("libpeony-cli-terminal.so", "libqtermwidget5.so.0", "libutf8proc.so.2", "peony-cli-session", "data/kb-layouts/default.keytab")
    if not source.is_file() or not all((runtime / part).is_file() for part in needed):
        return {"success": False, "error": "请先安装 Kylin-LQUI 系统插件包，再启用终端扩展。"}
    if not cli_settings.config_path().exists():
        cli_settings.save(cli_settings.defaults())
    result = _run_helper(["install-cli-panel", str(source)])
    if result.get("success"):
        result["requiresRestart"] = True
    return result


def remove_cli_panel_plugin() -> dict:
    result = _run_helper(["remove-cli-panel"])
    if result.get("success"):
        result["requiresRestart"] = True
    return result


def restart_peony(kill_only: bool = False) -> dict:
    """重启 Peony 文件管理器"""
    try:
        # 先关闭所有 Peony 窗口
        result = subprocess.run(
            ["peony", "-q"],
            capture_output=True, text=True, timeout=10,
        )

        if not kill_only:
            # 重新打开 Peony
            subprocess.Popen(
                ["peony"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )

        return {"success": True, "killOnly": kill_only}

    except subprocess.TimeoutExpired:
        return {"success": False, "error": "Peony 关闭超时"}
    except FileNotFoundError:
        return {"success": False, "error": "peony 命令未找到"}
    except Exception as e:
        return {"success": False, "error": str(e)}


def check_peony_running() -> bool:
    """检查 Peony 是否正在运行"""
    try:
        result = subprocess.run(
            ["pgrep", "-x", "peony"],
            capture_output=True, text=True, timeout=5,
        )
        return result.returncode == 0
    except Exception:
        return False


def copy_path_target_path() -> str:
    """Return the preferred system plugin path for libpeony-copy-path.so."""
    plugin_dirs = paths.get_plugin_dirs()
    if not plugin_dirs:
        arch = paths.get_arch()
        return f"/usr/lib/{arch}-linux-gnu/peony-extensions/{COPY_PATH_FILE_NAME}"
    return os.path.join(plugin_dirs[0], COPY_PATH_FILE_NAME)


def copy_path_build_status() -> dict:
    """Return local build/install status for the copy-path plugin."""
    target = copy_path_target_path()
    candidates = []
    for candidate in COPY_PATH_BUILD_CANDIDATES:
        candidates.append({
            "path": candidate,
            "exists": os.path.isfile(candidate),
            "validName": os.path.basename(candidate) == COPY_PATH_FILE_NAME,
        })

    return {
        "success": True,
        "targetPath": target,
        "installed": os.path.isfile(target),
        "helperPath": HELPER_PATH,
        "helperExists": os.path.isfile(HELPER_PATH),
        "pkexecAvailable": shutil.which("pkexec") is not None,
        "buildCandidates": candidates,
        "defaultSource": next((c["path"] for c in candidates if c["exists"]), ""),
        "devTools": {
            "g++": shutil.which("g++") or "",
            "qmake": shutil.which("qmake") or shutil.which("qmake-qt5") or "",
            "moc": shutil.which("moc") or (VENDOR_MOC if os.path.isfile(VENDOR_MOC) else ""),
            "vendorQt": VENDOR_QT_DIR if os.path.isdir(VENDOR_QT_DIR) else "",
        },
    }


def install_copy_path_plugin(source_path: str = "") -> dict:
    """Install libpeony-copy-path.so through pkexec helper."""
    source = (source_path or "").strip()
    packaged='/usr/share/kylin-lqui/peony-plugins/'+COPY_PATH_FILE_NAME
    if source and source!=packaged:
        return {"success":False,"code":"untrusted-source","error":"用户目录中的插件需要先打包并由管理员安装为系统插件包，不能直接提权复制。"}
    source=packaged
    if not os.path.isfile(source):
        return {
            "success": False,
            "code": "missing-source",
            "error": "请先安装 Kylin-LQUI 系统插件包，再启用复制路径扩展。",
            "status": copy_path_build_status(),
        }

    validation = _validate_copy_path_source(source)
    if not validation["success"]:
        return validation

    helper_result = _run_helper(["install-copy-path", source])
    if not helper_result.get("success"):
        return helper_result

    restart_result = restart_peony(kill_only=False)
    helper_result["restart"] = restart_result
    return helper_result


def remove_copy_path_plugin() -> dict:
    """Remove the managed copy-path plugin through pkexec helper."""
    helper_result = _run_helper(["remove-copy-path"])
    if not helper_result.get("success"):
        return helper_result

    restart_result = restart_peony(kill_only=False)
    helper_result["restart"] = restart_result
    return helper_result


def _find_default_copy_path_source() -> str:
    for candidate in COPY_PATH_BUILD_CANDIDATES:
        if os.path.isfile(candidate):
            return candidate
    return ""


def _validate_copy_path_source(source: str) -> dict:
    if not os.path.isfile(source):
        return {"success": False, "code": "source-not-found", "error": f"插件文件不存在: {source}"}
    if os.path.basename(source) != COPY_PATH_FILE_NAME:
        return {
            "success": False,
            "code": "bad-file-name",
            "error": f"只允许加载名为 {COPY_PATH_FILE_NAME} 的插件文件。",
        }

    try:
        result = subprocess.run(["file", "-b", source], capture_output=True, text=True, timeout=5)
        desc = result.stdout.strip()
        if "shared object" not in desc:
            return {
                "success": False,
                "code": "not-shared-object",
                "error": "选择的文件不是 ELF shared object。",
                "fileDescription": desc,
            }
    except Exception:
        pass

    return {"success": True}


def _run_helper(args: list) -> dict:
    if not os.path.isfile(HELPER_PATH):
        return {"success": False, "code": "missing-helper", "error": f"helper 不存在: {HELPER_PATH}"}
    if shutil.which("pkexec") is None:
        return {"success": False, "code": "missing-pkexec", "error": "系统未找到 pkexec，无法提权安装插件。"}

    # Never ask polkit to execute a script from a user-writable workspace.
    # Check every ancestor, not just the file's current owner/mode.
    try:
        for path in (Path(HELPER_PATH), *Path(HELPER_PATH).parents):
            st=path.lstat()
            if stat.S_ISLNK(st.st_mode) or st.st_uid!=0 or st.st_mode & 0o022:
                raise ValueError('untrusted helper path')
    except (OSError,ValueError):
        return {"success":False,"code":"untrusted-helper","error":"系统安装助手未就绪。请先安装由管理员维护的 Kylin-LQUI 插件包；不会以 root 运行用户目录脚本。"}

    try:
        result = subprocess.run(
            ["pkexec", HELPER_PATH] + args,
            capture_output=True,
            text=True,
            timeout=180,
        )
    except subprocess.TimeoutExpired:
        return {"success": False, "code": "helper-timeout", "error": "授权或安装超时。"}
    except Exception as exc:
        return {"success": False, "code": "helper-error", "error": str(exc)}

    payload = (result.stdout or "").strip()
    try:
        data = json.loads(payload) if payload else {}
    except json.JSONDecodeError:
        data = {"rawOutput": payload}

    if result.returncode != 0:
        return {
            "success": False,
            "code": "helper-failed",
            "error": data.get("error") or (result.stderr or "helper 执行失败").strip(),
            "returnCode": result.returncode,
            "helper": data,
        }

    data["success"] = data.get("success", data.get("status") == "ok")
    data["returnCode"] = result.returncode
    return data
