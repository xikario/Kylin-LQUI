"""Peony 官方插件映射表

提供插件 .so 文件到显示名、分类、来源、风险等级的映射。
"""

# 插件映射表: {file_name: {metadata}}
# 优先匹配完整的 file_name
PLUGIN_CATALOG = {
    "libpeony-cli-panel.so": {
        "name": "右侧 CLI 终端", "package": "peony-cli-panel", "category": "context-menu",
        "type": "原生终端面板", "risk_level": "medium", "risk_text": "运行你配置的本机命令",
        "source": "用户自定义", "description": "目录右键按需展开轻量 Qt 终端；默认无终端进程，随文管窗口关闭。可配置 CLI 名称、菜单文字和启动命令。",
        "conditions": "本地目录空白处或单个文件夹右键。",
        "dependencies": ["Qt5", "QTermWidget（随插件提供）", "systemd 用户会话"],
    },
    "libpeony-copy-path.so": {
        "name": "复制完整路径",
        "package": "peony-copy-path",
        "category": "context-menu",
        "type": "右键菜单扩展",
        "risk_level": "low",
        "risk_text": "低风险：只增加右键菜单",
        "source": "用户自定义",
        "description": "在右键文件、文件夹或空白处时提供一键复制完整路径，当多选时自动按行拆分，极为实用的高频开发者辅助工具。",
        "conditions": "任何文件、文件夹或目录留白处右键时显示；多选时生效且逐行复制。",
        "dependencies": ["Qt5", "Peony::MenuPluginInterface"],
    },
    "libpeony-menu-plugin-mate-terminal.so": {
        "name": "在终端中打开",
        "package": "peony-open-terminal",
        "category": "context-menu",
        "type": "右键菜单扩展",
        "risk_level": "low",
        "risk_text": "低风险：只增加右键菜单",
        "source": "UKUI 组件",
        "description": "提供右键呼出终端能力，自动映射至当前工作路径，使开发者能迅速接管当前文件层级进行命令行操控。",
        "conditions": "仅选中文件夹，或在当前目录空白处右键时出现。多选不可用。",
        "dependencies": ["ukui-terminal"],
    },
    "libpeony-engrampa-menu-plugin.so": {
        "name": "压缩/解压",
        "package": "parchives",
        "category": "context-menu",
        "type": "右键菜单扩展",
        "risk_level": "low",
        "risk_text": "低风险：只增加右键菜单",
        "source": "UKUI 组件",
        "description": "无缝调用系统归档引擎，右键任意文件直接进行打包压缩、格式转换或快速解压归档到当前文件夹。",
        "conditions": "任何文件或文件夹。支持单选或多选批量归档。",
        "dependencies": ["engrampa"],
    },
    "libpeony-set-wallpaper.so": {
        "name": "设置为壁纸",
        "package": "peony-set-wallpaper",
        "category": "context-menu",
        "type": "右键菜单扩展",
        "risk_level": "low",
        "risk_text": "低风险：只增加右键菜单",
        "source": "UKUI 组件",
        "description": "深度耦合系统背景配置，对选中的主流图片资源，在鼠标右键弹出菜单中提供一键应用为锁屏或桌面背景功能。",
        "conditions": "仅在选中图片格式（如 .png, .jpg, .bmp, .svg）文件时出现。多选不生效。",
        "dependencies": ["ukui-personal-setting"],
    },
    "libpeony-rotate-menu-plugin.so": {
        "name": "图片旋转",
        "package": "peony-rotate-menu-plugin",
        "category": "context-menu",
        "type": "右键菜单扩展",
        "risk_level": "low",
        "risk_text": "低风险：只增加右键菜单",
        "source": "UKUI 组件",
        "description": "极速无损旋转菜单。右键图片可直接顺时针或逆时针旋转物理资源文件，而无须先行开启第三方图片阅读器。",
        "conditions": "仅选中图片格式文件时可用。支持批量选中旋转。",
        "dependencies": ["gdk-pixbuf-2.0"],
    },
    "libpeony-print-pictures.so": {
        "name": "图片打印",
        "package": "peony-print-pictures",
        "category": "context-menu",
        "type": "右键菜单扩展",
        "risk_level": "low",
        "risk_text": "低风险：只增加右键菜单",
        "source": "UKUI 组件",
        "description": "调用本地打印子系统 CUPS，在选中文件右键时呼起一键打印配置，提供最快的物理输出纸张管道。",
        "conditions": "仅选中常见图片格式文件。多选时在打印中心合并队列处理。",
        "dependencies": ["cups"],
    },
    "libpeony-wpsprint-menu-plugin.so": {
        "name": "WPS 打印",
        "package": "wps-office",
        "category": "context-menu",
        "type": "右键菜单扩展",
        "risk_level": "low",
        "risk_text": "低风险：只增加右键菜单",
        "source": "WPS 组件",
        "description": "将 WPS 企业套件底层接口映射至文件管理系统，一键即可向物理打印机关联派发文档作业。",
        "conditions": "仅在选中 WPS 兼容格式文档（如 .docx, .xlsx, .pptx, .pdf）时展现。",
        "dependencies": ["wps-office"],
    },
    "libpeony-bluetooth-plugin.so": {
        "name": "蓝牙发送",
        "package": "peony-bluetooth",
        "category": "device",
        "type": "设备交互扩展",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "调用蓝牙模块进行文件传输，用户可一键唤起附近已配对的移动、办公设备进行便捷的文件无线投递。",
        "conditions": "任意文件或文件夹。多选时调用统一传输面板，顺序发送。",
        "dependencies": ["bluez", "blueman"],
    },
    "libpeony-send-to-device.so": {
        "name": "发送到设备",
        "package": "peony-send-to-device",
        "category": "device",
        "type": "设备交互扩展",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "麒麟多端交互通道。一键向本地同一网段内的麒麟平板、掌上设备投递数据，提供更便捷的数据中继流。",
        "conditions": "任何文件或文件夹。允许在局域网内多选投递。",
        "dependencies": ["peony-user-daemon"],
    },
    "libpeony-send-to-connectivity.so": {
        "name": "发送到多端协同",
        "package": "kylin-connectivity",
        "category": "device",
        "type": "设备交互扩展",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "支持跨桌面环境、移动OS等不同系统之间的剪贴板无缝同步、文件无缝拖拽和镜像桌面协同映射。",
        "conditions": "选中任意物理文件或目录节点有效。",
        "dependencies": ["kylin-synergy-service"],
    },
    "libpeony-vfs-kylin-multiterminal.so": {
        "name": "多端协同文件系统",
        "package": "kylin-connectivity",
        "category": "vfs",
        "type": "虚拟文件系统 (VFS)",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "虚拟文件系统挂载器，实现多端协同场景下的远程文件系统访问。",
        "conditions": "侧边栏及虚拟网络位置映射。",
        "dependencies": ["kylin-connectivity"],
    },
    "libpeony-share-menu-plugin.so": {
        "name": "文件夹共享菜单",
        "package": "peony-share",
        "category": "device",
        "type": "设备交互扩展",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "本地网络共享（Samba/NFS 机制）的可视化配制入口。",
        "conditions": "仅针对物理文件夹节点生效。",
        "dependencies": ["samba-common"],
    },
    "libpeony-share.so": {
        "name": "共享属性页/角标",
        "package": "peony-share",
        "category": "properties",
        "type": "属性页与标记",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "提供文件夹共享属性页和角标显示，方便管理共享状态。",
        "conditions": "文件夹属性页中显示。",
        "dependencies": ["samba-common"],
    },
    "libpeony-drive-rename.so": {
        "name": "设备重命名",
        "package": "peony-device-rename",
        "category": "sys-view",
        "type": "系统视图扩展",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "提供驱动器/卷标重命名功能，支持在计算机视图或侧边栏中对存储设备进行重命名。",
        "conditions": "在计算机视图或侧边栏中选中可移动介质时生效。",
        "dependencies": ["udisks2"],
    },
    "libpeony_menu_plugin_diskenc.so": {
        "name": "磁盘加密",
        "package": "peony-disk-encryption",
        "category": "sys-view",
        "type": "安全加密扩展",
        "risk_level": "high",
        "risk_text": "高风险：影响磁盘、VFS、文件保险箱、系统安全",
        "source": "麒麟安全组件",
        "description": "为存储介质提供高强度底层卷保护。支持国密/LUKS高级卷加密挂载，保障U盘或固态物理丢失时不致泄露文件。",
        "conditions": "仅在选中外部可移动介质驱动器、大容量外接存储、镜像挂载盘等视图时显现。",
        "dependencies": ["cryptsetup", "udisks2"],
    },
    "libpeony-computer-view-plugin.so": {
        "name": "计算机视图增强",
        "package": "peony-extension-computer-view",
        "category": "sys-view",
        "type": "系统视图扩展",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "增强 Peony 计算机视图，提供更丰富的设备信息展示和操作入口。",
        "conditions": "在计算机视图页面中显示。",
        "dependencies": ["peony"],
    },
    "libpeony-filesafe-menu-plugin.so": {
        "name": "文件保险箱菜单",
        "package": "peony-filesafe-extensions",
        "category": "properties",
        "type": "安全中心扩展",
        "risk_level": "high",
        "risk_text": "高风险：影响磁盘、VFS、文件保险箱、系统安全",
        "source": "麒麟安全组件",
        "description": "麒麟安全防护中心的底层关联服务。将选定的敏密文件挪入国密级加密文件箱内，进行硬件盾级解密安全存放。",
        "conditions": "选中任意文件、文件夹右键关联。",
        "dependencies": ["kylin-security-daemon"],
    },
    "libpeony-filesafe-vfs-plugin.so": {
        "name": "文件保险箱文件系统",
        "package": "peony-filesafe-extensions",
        "category": "vfs",
        "type": "虚拟文件系统 (VFS)",
        "risk_level": "high",
        "risk_text": "高风险：影响磁盘、VFS、文件保险箱、系统安全",
        "source": "麒麟安全组件",
        "description": "文件保险箱虚拟文件系统层，提供加密文件箱的挂载和访问能力。",
        "conditions": "文件保险箱侧边栏入口。",
        "dependencies": ["kylin-security-daemon", "fuse"],
    },
    "libpeony-vfs-kylin-kmre.so": {
        "name": "KMRE 安卓文件",
        "package": "peony-vfs-kylin-kmre",
        "category": "vfs",
        "type": "虚拟文件系统 (VFS)",
        "risk_level": "medium",
        "risk_text": "中风险：影响共享、打印、外部服务",
        "source": "UKUI 组件",
        "description": "虚拟文件系统挂载器。用于向 KMRE 安卓容器内部应用文件夹、下载区直接打通文件路径，实现安卓应用与宿主机资源免损同步。",
        "conditions": "侧边栏及虚拟网络位置映射。双击进行内部映射。",
        "dependencies": ["kmre-daemon", "fuse3"],
    },
    "libpeony-ksc-defender-plugin.so": {
        "name": "麒麟安全中心",
        "package": "ksc-defender",
        "category": "context-menu",
        "type": "安全管理套件",
        "risk_level": "high",
        "risk_text": "高风险：影响磁盘、VFS、文件保险箱、系统安全",
        "source": "麒麟安全组件",
        "description": "集成于文件浏览器的后台实时杀毒卫士。右键可对单独的文件执行查杀、隔离和安全等级信任标记配置。",
        "conditions": "任何文件、文件夹。多选时调用批量快速隔离查杀引擎流程。",
        "dependencies": ["ksc-defender"],
    },
}

# 虚拟条目（peony-copy-path 可能尚未安装）
VIRTUAL_COPY_PATH = {
    "file_name": "libpeony-copy-path.so",
    "name": "复制完整路径",
    "package": "peony-copy-path",
    "category": "context-menu",
    "type": "右键菜单扩展",
    "risk_level": "low",
    "risk_text": "低风险：只增加右键菜单",
    "source": "用户自定义",
    "description": "在右键文件、文件夹或空白处时提供一键复制完整路径，当多选时自动按行拆分，极为实用的高频开发者辅助工具。",
    "conditions": "任何文件、文件夹或目录留白处右键时显示；多选时生效且逐行复制。",
    "dependencies": ["glib-2.0", "peony-extension"],
}


def lookup(file_name: str, package: str = "") -> dict:
    """根据文件名查找插件元信息，找不到则返回通用数据"""
    if file_name in PLUGIN_CATALOG:
        return dict(PLUGIN_CATALOG[file_name])

    # 尝试不带版本后缀的匹配 (如 libpeony_menu_plugin_diskenc.so.0.0.0)
    base = file_name
    if ".so." in base:
        base = base.split(".so.")[0] + ".so"
    if base in PLUGIN_CATALOG:
        return dict(PLUGIN_CATALOG[base])

    # 未知插件，基于文件名生成元信息
    name = _friendly_name(file_name, package)
    return {
        "name": name,
        "package": package or "未知",
        "category": "context-menu",
        "type": "Peony 扩展",
        "risk_level": "medium",
        "risk_text": "中风险：来源未知",
        "source": package if package and package != "未知" else "未知来源",
        "description": f"Peony 扩展插件: {file_name}",
        "conditions": "依插件实现而定。",
        "dependencies": ["peony-extension"],
    }


def _friendly_name(file_name: str, package: str) -> str:
    """从文件名生成友好显示名"""
    name = file_name.replace(".so", "")
    name = name.replace("libpeony-", "").replace("libpeony_", "")
    name = name.replace("-menu-plugin", "").replace("-plugin", "").replace("-vfs", "")
    name = name.replace("-", " ").replace("_", " ").strip()
    if name:
        return name.title()
    return package or file_name


def is_disabled(file_name: str, disabled_list: list) -> bool:
    """判断插件是否在禁用列表中"""
    if not disabled_list:
        return True  # 默认启用
    return file_name not in disabled_list
