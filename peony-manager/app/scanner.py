"""Peony 插件目录扫描器"""

import os
from app import catalog
from app import gsettings_store
from app import package_store
from app import paths
from app.models import PluginInfo


def scan_plugins() -> list:
    """扫描本机 Peony 插件目录，返回 PluginInfo 列表"""
    plugin_dirs = paths.get_plugin_dirs()
    disabled_list = gsettings_store.get_disabled_extensions()

    # 收集所有 .so 文件（去重）
    so_files = _collect_so_files(plugin_dirs)

    # 含 peony-copy-path 虚拟条目
    result = []
    copy_path_found = False
    owners = package_store.owners_of([path for path, name in so_files])
    package_counts = _package_counts(so_files, owners)

    for file_path, file_name in so_files:
        # 检查是否为 peony-copy-path
        if file_name == "libpeony-copy-path.so":
            copy_path_found = True

        # 查询所属包
        pkg = owners.get(file_path, "未知")
        version = package_store.version_of(pkg)

        # 查询目录映射
        meta = catalog.lookup(file_name, pkg)

        # 判断启用状态
        enabled = None if disabled_list is None else file_name not in disabled_list

        info = PluginInfo(
            id=_make_id(file_name, meta, package_counts),
            name=meta.get("name", file_name),
            description=meta.get("description", ""),
            package=meta.get("package", pkg),
            version=version or meta.get("version", ""),
            file_name=file_name,
            path=file_path,
            category=meta.get("category", "context-menu"),
            type=meta.get("type", "Peony 扩展"),
            source=meta.get("source", pkg),
            risk_level=meta.get("risk_level", "medium"),
            risk_text=meta.get("risk_text", "中风险：来源未知"),
            conditions=meta.get("conditions", ""),
            dependencies=meta.get("dependencies", []),
            installed=True,
            enabled=enabled,
            pending_enabled=enabled,
            requires_restart=False,
        )
        result.append(info)

    # 添加 peony-copy-path 虚拟条目
    if not copy_path_found:
        meta = catalog.VIRTUAL_COPY_PATH
        info = PluginInfo(
            id="peony-copy-path",
            name=meta["name"],
            description=meta["description"],
            package=meta["package"],
            version="",
            file_name=meta["file_name"],
            path=os.path.join(plugin_dirs[0], meta["file_name"]) if plugin_dirs else "",
            category=meta["category"],
            type=meta["type"],
            source=meta["source"],
            risk_level=meta["risk_level"],
            risk_text=meta["risk_text"],
            conditions=meta["conditions"],
            dependencies=meta["dependencies"],
            installed=False,
            enabled=False,
            pending_enabled=False,
            requires_restart=False,
        )
        result.append(info)

    if not any(p.file_name == "libpeony-cli-panel.so" for p in result):
        meta = catalog.PLUGIN_CATALOG["libpeony-cli-panel.so"]
        result.append(PluginInfo(
            id="peony-cli-panel", name=meta["name"], description=meta["description"],
            package=meta["package"], file_name="libpeony-cli-panel.so",
            category=meta["category"], type=meta["type"], source=meta["source"],
            risk_level=meta["risk_level"], risk_text=meta["risk_text"], conditions=meta["conditions"],
            dependencies=meta["dependencies"], installed=False, enabled=False, pending_enabled=False))
    from app import cli_settings
    try:
        cli_enabled = cli_settings.read()["enabled"]
        for plugin in result:
            if plugin.id == "peony-cli-panel":
                plugin.enabled = False if not plugin.installed or not cli_enabled else plugin.enabled
                plugin.pending_enabled = plugin.enabled
    except (ValueError, OSError):
        pass

    # 复制完整路径是本工具的核心目标，固定置顶；其余按名称排序。
    result.sort(key=lambda p: (0 if p.id == "peony-copy-path" else 1, p.name))
    return result


def _collect_so_files(plugin_dirs: list) -> list:
    """收集插件目录下所有 .so 文件，返回 [(real_path, file_name)] 去重列表"""
    seen_inodes = set()
    files = []
    for d in plugin_dirs:
        if not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            if not f.endswith(".so"):
                continue
            full_path = os.path.join(d, f)
            if not os.path.isfile(full_path):
                continue
            real_path = os.path.realpath(full_path)
            if real_path not in seen_inodes:
                seen_inodes.add(real_path)
                files.append((real_path, os.path.basename(real_path)))
    return files


def _make_id(file_name: str, meta: dict, package_counts: dict) -> str:
    """生成插件 ID"""
    pkg = meta.get("package", "")
    if file_name == "libpeony-copy-path.so":
        return "peony-copy-path"
    if file_name == "libpeony-cli-panel.so":
        return "peony-cli-panel"
    if pkg and pkg != "未知" and package_counts.get(pkg, 1) == 1:
        return pkg
    return _id_from_file_name(file_name)


def _package_counts(so_files: list, owners: dict) -> dict:
    counts = {}
    for file_path, file_name in so_files:
        pkg = owners.get(file_path, "未知")
        meta = catalog.lookup(file_name, pkg)
        effective_pkg = meta.get("package") or pkg
        if effective_pkg and effective_pkg != "未知":
            counts[effective_pkg] = counts.get(effective_pkg, 0) + 1

    return counts


def _id_from_file_name(file_name: str) -> str:
    name = file_name.replace(".so", "")
    name = name.replace("libpeony-", "peony-").replace("libpeony_", "peony-")
    return name.strip("-")
