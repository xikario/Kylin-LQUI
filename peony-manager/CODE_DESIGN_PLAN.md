# Peony Manager 代码设计方案

更新时间：2026-07-03

## 1. 设计目标

Peony Manager 的第一目标不是做“插件市场”，而是做一个贴合当前麒麟/UKUI/Peony 环境的本地系统工具：

1. 读取本机 Peony 插件目录、软件包、GSettings 配置。
2. 用当前前端界面展示真实插件状态。
3. 管理插件启用/禁用。
4. 管理 Peony 全局右键和视图偏好。
5. 支持安装/卸载自定义插件，第一优先级是 `peony-copy-path`。
6. 通过 `pkexec`/polkit 处理需要管理员权限的操作。
7. 重启 Peony 让变更生效。

当前系统事实：

- 系统：UKUI 桌面，X11 会话
- 架构：`arm64` / `aarch64`
- 文件管理器：`peony`
- 插件目录：
  - `/usr/lib/aarch64-linux-gnu/peony-extensions`
  - `/lib/aarch64-linux-gnu/peony-extensions`
- GSettings schema：`org.ukui.peony.settings`
- 已安装 Python：`python3`
- 已安装 GUI 能力：`python3-pyqt5`
- 未安装但仓库可装：`python3-pyqt5.qtwebengine`
- 权限工具：`pkexec`

## 2. 推荐技术路线

推荐采用 **Python 3 + PyQt5 + QtWebEngine + 本地系统适配层**。

原因：

- PyQt5 已安装，符合 UKUI/Qt 生态。
- 当前前端已经是 HTML，可以作为 Web UI 继续使用。
- Python 调用 `dpkg-query`、`gsettings`、`apt`、`peony -q` 很直接。
- 需要提权时可以用 `pkexec` 调独立 helper，避免整个 GUI 以 root 运行。

需要补的系统包：

```bash
sudo apt install python3-pyqt5.qtwebengine python3-pyqt5.qtwebchannel
```

本项目按桌面 GUI 程序实现，不把 HTTP 服务作为产品路线。

如果当前系统缺少 QtWebEngine，请优先安装依赖；只有在临时调试前端数据契约时，才允许启动本地 HTTP 服务做开发验证。调试 HTTP 服务不能进入正式功能规划，也不能作为交付形态。

## 3. 项目目录结构

建议将当前目录整理为：

```text
/home/zhouzhang/应用/peony-manager/
  TECHNICAL_PLAN.md
  CODE_DESIGN_PLAN.md
  peony_manager.html
  app/
    main.py
    bridge.py
    models.py
    scanner.py
    gsettings_store.py
    package_store.py
    catalog.py
    privilege.py
    operations.py
    paths.py
  helpers/
    peony-manager-helper
  plugins/
    peony-copy-path/
      CMakeLists.txt
      copy_path_plugin.cpp
      copy_path_plugin.h
      metadata.json
  packaging/
    peony-manager.desktop
    org.ukui.peony-manager.policy
```

各模块职责：

| 文件 | 职责 |
| --- | --- |
| `main.py` | 创建 PyQt5 窗口，加载 `peony_manager.html` |
| `bridge.py` | 暴露给前端调用的 API，负责 JSON 输入输出 |
| `models.py` | 定义插件、全局配置、待变更项的数据结构 |
| `scanner.py` | 扫描 Peony 插件目录，读取 `.so` 文件和插件元信息 |
| `gsettings_store.py` | 读取/写入 `org.ukui.peony.settings` |
| `package_store.py` | 调用 `dpkg-query`、`apt-cache` 查询包来源、版本、候选包 |
| `catalog.py` | 本机真实插件映射表，给未知 `.so` 做兜底分类 |
| `privilege.py` | 封装 `pkexec` 调用 |
| `operations.py` | 安装、卸载、启用、禁用、重启 Peony 等动作 |
| `paths.py` | 统一管理系统路径和架构检测 |
| `helpers/peony-manager-helper` | root 权限 helper，只做白名单动作 |

## 4. 前端保持现状，但需要改成数据驱动

当前 `peony_manager.html` 可以继续作为主界面。需要把其中的 mock 数据替换为后端返回的数据。

必须移除或改造的前端 mock 点：

- `plugins = [...]` 不再硬编码真实状态，只保留空数组或开发兜底。
- `globalPreferences = {...}` 改由后端 `getGlobalSettings()` 返回。
- `simulateBrowseFile()` 改成真正选择文件，或让用户输入本地路径。
- `triggerPeonyRestart()` 改为调用后端重启接口。
- `refreshPlugins()` 改为调用后端重新扫描接口。
- `submitMockInstall()` 改为调用后端安装接口。
- 所有 `/usr/lib/ukui-peony/plugins/...` 路径改为后端真实路径。

建议前端保留这些状态字段：

```js
let appState = {
  plugins: [],
  globalSettings: {},
  pendingChanges: [],
  selectedPluginId: null,
  currentCategory: "all",
  searchQuery: "",
  loading: false
};
```

## 5. 前后端通信设计

前端通过 PyQt5 QtWebChannel 调用 Python bridge，例如 `window.peonyBridge`。这是正式通信方式。

HTTP/REST 只允许作为临时调试工具，不作为产品架构，不进入用户可见启动流程。

统一数据契约建议如下。

### 5.1 初始化

前端启动后调用：

```js
const state = await api.getInitialState();
```

返回：

```json
{
  "system": {
    "desktop": "UKUI",
    "sessionType": "x11",
    "arch": "aarch64",
    "peonyVersion": "3.26.0.0-0k5.32oemccd3000m2.4.u",
    "pluginDirs": [
      "/usr/lib/aarch64-linux-gnu/peony-extensions",
      "/lib/aarch64-linux-gnu/peony-extensions"
    ],
    "schema": "org.ukui.peony.settings"
  },
  "globalSettings": {},
  "plugins": [],
  "pendingChanges": []
}
```

### 5.2 扫描插件

```js
const plugins = await api.scanPlugins();
```

Python 实现：

1. 枚举插件目录下的 `.so`。
2. 对每个 `.so` 调 `dpkg-query -S <path>`。
3. 调 `dpkg-query -W -f=... <package>` 获取版本和摘要。
4. 根据 `catalog.py` 做分类、风险、显示名、菜单条件补全。
5. 读取 `disabled-extensions` 判断启用状态。

### 5.3 获取全局设置

对应真实 GSettings：

| UI 字段 | GSettings key |
| --- | --- |
| 右键菜单全局开关 | `enable-peony-right-menu` |
| 桌面文件右键菜单 | `enable-desktop-file-menu` |
| 桌面背景右键菜单 | `enable-desktop-background-menu` |
| 在终端中打开 | `show-open-terminal` |
| 显示文件扩展名 | `show-file-extension` |
| 显示隐藏文件 | `show-hidden-file` |

Python 读取：

```bash
gsettings get org.ukui.peony.settings enable-peony-right-menu
gsettings get org.ukui.peony.settings disabled-extensions
```

Python 写入：

```bash
gsettings set org.ukui.peony.settings show-hidden-file true
```

这些是用户级配置，通常不需要 root。

### 5.4 启用/禁用插件

前端动作：

```js
await api.setPluginEnabled(pluginId, enabled);
```

后端策略：

- 不直接删除 `.so`。
- 优先写 `disabled-extensions`。
- 需要确认 Peony 使用的 disabled key 是插件 `name()` 还是文件名。第一版建议先用文件名作为内部状态，实际写入前做验证。

数据层要同时维护：

```json
{
  "enabled": true,
  "pendingEnabled": false,
  "requiresRestart": true
}
```

### 5.5 重启 Peony

前端动作：

```js
await api.restartPeony();
```

后端执行：

```bash
peony -q
peony
```

第一版可以只执行 `peony -q`，让用户自己重新打开文件管理器。更稳妥，避免误开窗口。

建议 UI 文案：

- 主按钮：`重启 Peony`
- 说明：`将关闭所有 Peony 文件管理器窗口，未完成的文件操作不会继续。`
- 二次确认：必须。

## 6. 真实插件映射表

当前前端里的包名和路径还有不少是演示值。正式版要以本机真实包为准。

建议 `catalog.py` 固化下面这张表：

| 插件文件 | 真实包 | UI 名称 | 分类 | 风险 | 来源 |
| --- | --- | --- | --- | --- | --- |
| `libpeony-menu-plugin-mate-terminal.so` | `peony-open-terminal` | 在终端中打开 | 右键菜单 | 低 | UKUI 组件 |
| `libpeony-engrampa-menu-plugin.so` | `parchives` | 压缩/解压 | 右键菜单 | 低 | UKUI 组件 |
| `libpeony-set-wallpaper.so` | `peony-set-wallpaper` | 设置为壁纸 | 右键菜单 | 低 | UKUI 组件 |
| `libpeony-rotate-menu-plugin.so` | `peony-rotate-menu-plugin` | 图片旋转 | 右键菜单 | 低 | UKUI 组件 |
| `libpeony-print-pictures.so` | `peony-print-pictures` | 图片打印 | 右键菜单 | 低 | UKUI 组件 |
| `libpeony-wpsprint-menu-plugin.so` | `wps-office` | WPS 打印 | 右键菜单 | 低 | WPS 组件 |
| `libpeony-bluetooth-plugin.so` | `peony-bluetooth` | 蓝牙发送 | 设备与协同 | 中 | UKUI 组件 |
| `libpeony-send-to-device.so` | `peony-send-to-device` | 发送到设备 | 设备与协同 | 中 | UKUI 组件 |
| `libpeony-send-to-connectivity.so` | `kylin-connectivity` | 发送到多端协同 | 设备与协同 | 中 | UKUI 组件 |
| `libpeony-vfs-kylin-multiterminal.so` | `kylin-connectivity` | 多端协同文件系统 | 虚拟文件系统 | 中 | UKUI 组件 |
| `libpeony-share-menu-plugin.so` | `peony-share` | 文件夹共享菜单 | 设备与协同 | 中 | UKUI 组件 |
| `libpeony-share.so` | `peony-share` | 共享属性页/角标 | 属性页与标记 | 中 | UKUI 组件 |
| `libpeony-drive-rename.so` | `peony-device-rename` | 设备重命名 | 系统视图 | 中 | UKUI 组件 |
| `libpeony_menu_plugin_diskenc.so` | `peony-disk-encryption` | 磁盘加密 | 系统视图 | 高 | 麒麟安全组件 |
| `libpeony-computer-view-plugin.so` | `peony-extension-computer-view` | 计算机视图增强 | 系统视图 | 中 | UKUI 组件 |
| `libpeony-filesafe-menu-plugin.so` | `peony-filesafe-extensions` | 文件保险箱菜单 | 属性页与标记 | 高 | 麒麟安全组件 |
| `libpeony-filesafe-vfs-plugin.so` | `peony-filesafe-extensions` | 文件保险箱文件系统 | 虚拟文件系统 | 高 | 麒麟安全组件 |
| `libpeony-vfs-kylin-kmre.so` | `peony-vfs-kylin-kmre` | KMRE 安卓文件 | 虚拟文件系统 | 中 | UKUI 组件 |
| `libpeony-ksc-defender-plugin.so` | `ksc-defender` | 麒麟安全中心 | 右键菜单 | 高 | 麒麟安全组件 |
| `libpeony-copy-path.so` | `peony-copy-path` | 复制完整路径 | 右键菜单 | 低 | 用户自定义 |

注意：`peony-copy-path` 当前还不是系统包，是我们要做的自定义插件。UI 中应该显示为：

- 未安装：展示“安装插件”
- 已安装未启用：展示“启用”
- 已启用：展示“禁用”
- 已修改：展示“待重启生效”

## 7. Python 数据模型

`models.py` 建议：

```python
from dataclasses import dataclass, field
from typing import List, Optional

@dataclass
class SystemInfo:
    desktop: str
    session_type: str
    arch: str
    peony_version: str
    plugin_dirs: List[str]
    schema: str = "org.ukui.peony.settings"

@dataclass
class PluginInfo:
    id: str
    name: str
    description: str
    package: str
    version: str
    file_name: str
    path: str
    category: str
    type: str
    source: str
    risk_level: str
    risk_text: str
    conditions: str
    dependencies: List[str] = field(default_factory=list)
    installed: bool = True
    enabled: bool = True
    pending_enabled: Optional[bool] = None
    requires_restart: bool = False

@dataclass
class GlobalSettings:
    enable_peony_right_menu: bool
    enable_desktop_file_menu: bool
    enable_desktop_background_menu: bool
    show_open_terminal: bool
    show_file_extension: bool
    show_hidden_file: bool
    disabled_extensions: List[str]
```

前端字段可以继续用 camelCase，`bridge.py` 负责 snake_case 和 camelCase 转换。

## 8. 系统扫描逻辑

`scanner.py` 伪代码：

```python
PLUGIN_DIRS = [
    "/usr/lib/aarch64-linux-gnu/peony-extensions",
    "/lib/aarch64-linux-gnu/peony-extensions",
]

def scan_plugins():
    files = unique_realpaths(find_so_files(PLUGIN_DIRS))
    disabled = gsettings_store.get_disabled_extensions()
    result = []
    for path in files:
        file_name = os.path.basename(path)
        package = package_store.owner_of(path)
        version = package_store.version_of(package)
        meta = catalog.lookup(file_name, package)
        enabled = catalog.enabled_by_disabled_list(file_name, meta, disabled)
        result.append(PluginInfo(...))
    result.append(copy_path_virtual_item_if_not_installed())
    return result
```

`/usr/lib` 和 `/lib` 可能指向同一批文件，要用 `os.path.realpath()` 去重。

## 9. 权限与安全设计

不要让整个 GUI 以 root 运行。

普通用户可做：

- 扫描插件目录
- 查询软件包
- 读取 GSettings
- 写用户级 GSettings
- 修改前端 pending 状态
- 执行 `peony -q`

需要提权：

- 复制 `.so` 到 `/usr/lib/aarch64-linux-gnu/peony-extensions`
- 删除系统插件 `.so`
- 安装 `.deb`
- 卸载 apt 包
- 修改 root-owned helper 配置

建议通过 `pkexec` 调 helper：

```bash
pkexec /home/zhouzhang/应用/peony-manager/helpers/peony-manager-helper install-so /path/to/libpeony-copy-path.so
pkexec /home/zhouzhang/应用/peony-manager/helpers/peony-manager-helper remove-custom libpeony-copy-path.so
pkexec /home/zhouzhang/应用/peony-manager/helpers/peony-manager-helper install-deb /path/to/peony-copy-path.deb
```

helper 必须做白名单限制：

- 目标目录只能是 `/usr/lib/aarch64-linux-gnu/peony-extensions`
- 文件名必须匹配 `libpeony-*.so`
- 不能删除系统包管理器拥有的插件，除非走 `apt remove`
- 默认只允许管理 `libpeony-copy-path.so` 和用户导入插件

## 10. 安装插件设计

前端“添加本地插件向导”现在三步是对的。工程上分两类处理。

### 10.1 `.deb` 安装

流程：

1. 前端选择 `.deb`。
2. 后端检查：
   - 架构是否 `arm64` 或 `all`
   - 包名是否合理
   - 是否依赖 Peony/Qt
3. 展示检查结果。
4. 用户确认风险。
5. `pkexec apt install ./xxx.deb` 或 `pkexec dpkg -i xxx.deb` 后再 `apt -f install`。

建议优先用：

```bash
pkexec apt install /absolute/path/package.deb
```

### 10.2 `.so` 安装

流程：

1. 前端选择 `.so`。
2. 后端检查：
   - 文件名是否 `libpeony-*.so`
   - `file` 输出是否为 `aarch64` ELF shared object
   - `ldd` 是否缺依赖
3. 复制到插件目录。
4. 重扫插件。
5. 提示重启 Peony。

检查命令：

```bash
file /path/to/libpeony-copy-path.so
ldd /path/to/libpeony-copy-path.so
```

## 11. `peony-copy-path` 插件工程

这是第一优先级插件。

目录：

```text
plugins/peony-copy-path/
  CMakeLists.txt
  copy_path_plugin.h
  copy_path_plugin.cpp
  metadata.json
```

行为：

- `DirectoryView` 菜单：
  - 有选中项：复制选中项路径
  - 无选中项：复制当前目录路径
- `DesktopWindow` 菜单：
  - 复制桌面选中项路径或桌面目录
- 多选：
  - 每行一个路径
- 本地 URI：
  - `file:///home/...` 转成本地路径 `/home/...`
- 非本地 URI：
  - 第一版复制 URI 原文，例如 `trash:///...`

菜单文字：

- `复制完整路径`

剪贴板：

- 使用 `QGuiApplication::clipboard()->setText(text)`

通知：

- 第一版可不做。
- 第二版可加 libnotify 或 UKUI 通知。

## 12. 启用/禁用策略

这里要小心。Peony 内置插件管理器写的是：

```text
org.ukui.peony.settings disabled-extensions
```

但还需要确认它保存的是：

- 插件 `name()`
- 插件文件名
- Qt metadata id

验证办法：

1. 打开 Peony 内置“插件管理设置”。
2. 禁用一个低风险插件，例如“在终端中打开”。
3. 读取：

```bash
gsettings get org.ukui.peony.settings disabled-extensions
```

4. 观察写入值。

在没有确认前，Peony Manager 的第一版可以只做“展示真实状态 + 提示由系统插件管理器修改”，或者只管理我们自己的 `peony-copy-path`。

推荐里程碑：

- V0.1：只读扫描 + 全局设置读取。
- V0.2：全局设置可写。
- V0.3：安装/卸载 `peony-copy-path`。
- V0.4：启用/禁用 `peony-copy-path`。
- V0.5：确认 disabled key 后，再支持全部插件开关。

## 13. 前端字段修正清单

当前新版前端规划已经不错，但正式数据要修正这些点：

1. 路径从：

```text
/usr/lib/ukui-peony/plugins/...
```

改成：

```text
/usr/lib/aarch64-linux-gnu/peony-extensions/...
```

2. 真实包名从演示名改为：

```text
parchives
peony-set-wallpaper
peony-rotate-menu-plugin
peony-print-pictures
peony-send-to-device
peony-filesafe-extensions
peony-vfs-kylin-kmre
ksc-defender
kylin-connectivity
wps-office
```

3. `复制完整路径` 的 `source` 应该是：

```text
用户自定义
```

除非未来打包进官方源。

4. `主配置路径: ~/.config/peony` 建议改为：

```text
配置源: org.ukui.peony.settings
```

5. “内核编译架构 AArch64 / x86_64 匹配成功”要改成：

```text
目标架构: arm64 / aarch64
```

不要同时写 x86_64。

6. “依赖检查 peony-extension >= 4.0”要改为：

```text
Peony ABI: libpeony3 3.26
开发接口: libpeony-dev
```

## 14. 开发里程碑

### V0.1 只读版本

目标：真实展示本机环境。

实现：

- PyQt5 窗口加载 HTML。
- 后端返回系统信息。
- 后端扫描真实插件目录。
- 后端读取真实 GSettings。
- 前端替换 mock 数据。

验收：

- 插件数和 `find /usr/lib/aarch64-linux-gnu/peony-extensions -name '*.so'` 基本一致。
- 路径显示为 `aarch64-linux-gnu/peony-extensions`。
- 全局设置和 `gsettings list-recursively org.ukui.peony.settings` 一致。

### V0.2 全局设置可写

目标：安全修改用户级配置。

实现：

- 写入 6 个 GSettings key。
- 待变更清单展示全局设置改动。
- 提示哪些设置立即生效，哪些需要重启。

验收：

- 切换“显示隐藏文件”后，`gsettings get org.ukui.peony.settings show-hidden-file` 变化。

### V0.3 Copy Path 插件安装

目标：安装 `libpeony-copy-path.so`。

实现：

- 编译插件。
- 前端可显示未安装状态。
- 安装时通过 `pkexec helper` 复制 `.so`。
- 重扫后显示已安装。

验收：

- 插件目录出现 `/usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-copy-path.so`。
- 重启 Peony 后右键出现“复制完整路径”。

### V0.4 Copy Path 插件管理

目标：只管理自己的插件。

实现：

- 启用/禁用 `peony-copy-path`。
- 卸载 `peony-copy-path`。
- 清楚展示风险和重启要求。

验收：

- 禁用后右键项消失。
- 启用后右键项恢复。

### V0.5 全量插件管理

目标：管理系统已有插件。

前提：

- 已确认 `disabled-extensions` 的真实写入标识。

实现：

- 支持所有插件启用/禁用。
- 高风险插件必须二次确认。
- 系统包拥有的插件不允许直接删 `.so`，只能跳转/提示 apt 卸载。

## 15. 测试清单

只读扫描：

```bash
find /usr/lib/aarch64-linux-gnu/peony-extensions -maxdepth 1 -type f -name '*.so'
dpkg -S /usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-menu-plugin-mate-terminal.so
gsettings get org.ukui.peony.settings disabled-extensions
```

全局设置：

```bash
gsettings get org.ukui.peony.settings show-hidden-file
gsettings set org.ukui.peony.settings show-hidden-file true
gsettings set org.ukui.peony.settings show-hidden-file false
```

Peony 重启：

```bash
peony -q
peony
```

插件安装：

```bash
file libpeony-copy-path.so
ldd libpeony-copy-path.so
sudo install -m 0644 libpeony-copy-path.so /usr/lib/aarch64-linux-gnu/peony-extensions/
```

## 16. 风险边界

必须避免：

- 直接删除系统包管理的 `.so`。
- 整个 GUI root 运行。
- 把未知 `.so` 静默复制到插件目录。
- 误把 x86_64 插件装到 aarch64 系统。
- 在未确认 `disabled-extensions` 标识前批量禁用系统插件。

推荐保护：

- 所有高风险插件默认不可卸载。
- 系统插件卸载按钮改为“查看来源包”或“用软件包管理器处理”。
- 用户自定义插件才显示真正“卸载插件”。
- 安装本地插件必须显示架构、依赖、签名、来源、目标路径。

## 17. 近期最小行动

下一步建议按这个顺序做：

1. 把前端 mock 路径和包名先改成真实映射表。
2. 写 `app/scanner.py`，能输出真实插件 JSON。
3. 写 `app/gsettings_store.py`，能输出真实全局设置 JSON。
4. 写一个最小 PyQt5 + QtWebEngine + QtWebChannel 桌面壳，让前端能通过 bridge 调用 `getInitialState()`。
5. 再开始做 `peony-copy-path` 插件本体。

这条路线能最快把当前漂亮的前端稿变成“真的认识你这台系统”的工具。
