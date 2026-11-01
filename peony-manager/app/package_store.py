"""dpkg 软件包查询封装"""

import subprocess
from functools import lru_cache


@lru_cache(maxsize=256)
def owner_of(path: str) -> str:
    """查询 .so 文件所属的软件包名"""
    try:
        result = subprocess.run(
            ["dpkg-query", "-S", path],
            capture_output=True, text=True, timeout=10,
        )
        if result.returncode == 0:
            # 输出: "package-name: /path/to/file.so"
            return result.stdout.split(":")[0].strip()
    except Exception:
        pass
    return "未知"


@lru_cache(maxsize=256)
def version_of(package: str) -> str:
    """查询软件包版本"""
    if not package or package == "未知":
        return ""
    try:
        result = subprocess.run(
            ["dpkg-query", "-W", "-f=${Version}", package],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception:
        pass
    return ""


@lru_cache(maxsize=256)
def description_of(package: str) -> str:
    """查询软件包描述"""
    if not package or package == "未知":
        return ""
    try:
        result = subprocess.run(
            ["dpkg-query", "-W", "-f=${Description}", package],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip().split("\n")[0]
    except Exception:
        pass
    return ""


@lru_cache(maxsize=256)
def is_installed(package: str) -> bool:
    """检查软件包是否已安装"""
    if not package or package == "未知":
        return False
    try:
        result = subprocess.run(
            ["dpkg-query", "-W", "-f=${Status}", package],
            capture_output=True, text=True, timeout=5,
        )
        return "ok installed" in result.stdout
    except Exception:
        return False


def owners_of(paths):
    """Resolve all installed extensions with one package database scan."""
    paths=list(dict.fromkeys(paths))
    if not paths:return {}
    owners={path:"未知" for path in paths}
    try:
        result=subprocess.run(["dpkg-query","-S"]+paths,capture_output=True,text=True,timeout=10)
        # dpkg returns 1 if even one local extension has no owning package;
        # stdout still contains the valid matches for the other extensions.
        for line in result.stdout.splitlines():
            if ": " not in line:continue
            package,path=line.split(": ",1)
            if path in owners:owners[path]=package.split(":")[0].strip()
    except (OSError,subprocess.SubprocessError):pass
    return owners
