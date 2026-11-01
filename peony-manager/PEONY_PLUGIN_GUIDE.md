# Peony 插件制作与装载手册

本文档记录 Peony Manager 项目内制作、构建、手动导入 Peony 右键菜单插件的固定流程。下次做新插件时优先检索本文件。

## 1. 当前系统结论

- 系统架构：`aarch64 / arm64`
- Peony 插件目录：`/usr/lib/aarch64-linux-gnu/peony-extensions/`
- Peony Qt 菜单接口：`Peony::MenuPluginInterface`
- 右键菜单插件产物形式：Qt plugin shared object，文件名形如 `libpeony-xxx.so`
- 已验证可用样例：`plugins/peony-copy-path/`

## 2. 最小插件目录结构

```text
plugins/<plugin-name>/
  build.sh
  <plugin-name>.pro
  src/
    <plugin-name>.json
    <plugin-class>.h
    <plugin-class>.cpp
  build/
    libpeony-xxx.so
```

示例：

```text
plugins/peony-copy-path/
  build.sh
  peony-copy-path.pro
  src/
    peony-copy-path.json
    copy-path-plugin.h
    copy-path-plugin.cpp
  build/
    libpeony-copy-path.so
```

## 3. 关键代码规则

### 3.1 必须实现菜单接口

右键菜单插件必须继承：

```cpp
Peony::MenuPluginInterface
```

核心方法：

```cpp
QList<QAction *> menuActions(Peony::MenuPluginInterface::Types types,
                             const QString &uri,
                             const QStringList &selectionUris) override;
```

参数含义：

- `types`：菜单来源类型，常见为 `DirectoryView`、`DesktopWindow`、`SideBar`。
- `uri`：当前目录或当前菜单上下文 URI。
- `selectionUris`：选中文件/文件夹 URI 列表。空白处右键时通常为空。

### 3.2 类建议放在 `Peony` 命名空间

这次验证发现：插件库可能被 Peony 加载，但如果类形态和官方插件差异较大，可能出现“已加载但右键菜单不显示”的情况。

推荐写法：

```cpp
namespace Peony {

class CopyPathPlugin : public QObject, public MenuPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID MenuPluginInterface_iid FILE "peony-copy-path.json")
    Q_INTERFACES(Peony::MenuPluginInterface)
    ...
};

}
```

### 3.3 metadata 使用 Peony 官方格式

插件 JSON 建议保持极简，贴近系统已有插件：

```json
{
  "version": "3.2.2"
}
```

不要优先写自定义 `Name`、`Description`、`Version`。这类 metadata Qt 能识别，但 Peony 插件管理器未必按预期登记。

### 3.4 `pluginType()` 必须返回菜单插件

```cpp
Peony::PluginInterface::PluginType CopyPathPlugin::pluginType()
{
    return Peony::PluginInterface::MenuPlugin;
}
```

### 3.5 `name()` 必须稳定

Peony 用插件 `name()` 作为菜单插件管理器里的 key。不要返回空字符串，不要随状态变化。

```cpp
const QString CopyPathPlugin::name()
{
    return QStringLiteral("复制完整路径");
}
```

## 4. 菜单动作逻辑模板

```cpp
QList<QAction *> Plugin::menuActions(Peony::MenuPluginInterface::Types types,
                                     const QString &uri,
                                     const QStringList &selectionUris)
{
    QList<QAction *> actions;

    const bool supported = types.testFlag(Peony::MenuPluginInterface::DirectoryView)
        || types.testFlag(Peony::MenuPluginInterface::DesktopWindow);
    if (!supported) {
        return actions;
    }

    QStringList targets = selectionUris;
    if (targets.isEmpty() && !uri.isEmpty()) {
        targets << uri;
    }
    if (targets.isEmpty()) {
        return actions;
    }

    auto *action = new QAction(QIcon::fromTheme("edit-copy"), QStringLiteral("动作名称"), this);
    connect(action, &QAction::triggered, this, [targets]() {
        // 执行动作
    });

    actions << action;
    return actions;
}
```

## 5. 构建方式

### 5.1 系统安装构建依赖

最稳方式：

```bash
sudo apt install libpeony-dev qtbase5-dev qtbase5-dev-tools qt5-qmake g++
cd /home/zhouzhang/应用/peony-manager/plugins/<plugin-name>
./build.sh
```

### 5.2 使用项目内 vendor 构建

本项目已把 `libpeony-dev` 和 Qt dev 包解包到：

```text
vendor/extract/libpeony-dev/
vendor/extract/qt/
```

`plugins/peony-copy-path/build.sh` 已支持在系统没有 `qmake/moc` 时，使用 vendor 内的 Qt 头文件和 `moc`，再用 `g++` 直接编译。

下次新插件可以复用这个 `build.sh` 模式。

## 6. 手动装载流程

### 6.1 通过 Peony Manager 装载

推荐流程：

1. 构建出 `libpeony-xxx.so`。
2. 打开 Peony Manager。
3. 在对应插件详情页点击“选择 .so”或“安装内置”。
4. 通过 `pkexec` 授权安装。
5. Peony Manager 会安装到系统插件目录并重启 Peony。

当前 helper 只允许安全操作 `libpeony-copy-path.so`。如果要支持新插件，需要扩展 helper 白名单。

### 6.2 手动命令安装

```bash
pkexec /home/zhouzhang/应用/peony-manager/helpers/peony-manager-helper install-copy-path \
  /home/zhouzhang/应用/peony-manager/plugins/peony-copy-path/build/libpeony-copy-path.so
```

目标路径：

```text
/usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-copy-path.so
```

## 7. 验证命令

### 7.1 检查文件和架构

```bash
file /usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-copy-path.so
ldd /usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-copy-path.so
```

预期：

```text
ELF 64-bit LSB shared object, ARM aarch64
```

### 7.2 检查 Qt 插件元数据

```bash
strings -a /usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-copy-path.so \
  | rg 'org.ukui.peony-qt.plugin-iface.MenuPluginInterface|gversione3.2.2|QTMETADATA'
```

必须看到：

```text
org.ukui.peony-qt.plugin-iface.MenuPluginInterface
QTMETADATA
gversione3.2.2
```

### 7.3 检查 Peony 是否加载插件

```bash
peony -q
QT_DEBUG_PLUGINS=1 timeout 8s peony
rg -n -i 'copy-path|CopyPathPlugin|menuActions|loaded library' ~/.log/peony.log | tail -n 80
```

成功加载至少应看到：

```text
loaded library "/usr/lib/aarch64-linux-gnu/peony-extensions/libpeony-copy-path.so"
Peony::CopyPathPlugin loaded
```

右键触发后应看到：

```text
Peony::CopyPathPlugin::menuActions
```

## 8. 常见问题

### 8.1 管理器显示已启用，但右键菜单没有

先看 Peony 日志：

```bash
rg -n -i 'copy-path|CopyPathPlugin|menuActions' ~/.log/peony.log | tail -n 80
```

如果只有 `loaded library`，没有构造函数日志，检查：

- 类是否在 `Peony` 命名空间。
- `Q_PLUGIN_METADATA` 的 IID 是否为 `MenuPluginInterface_iid`。
- JSON 是否为 `{"version":"3.2.2"}`。
- 是否实现了 `pluginType()` 并返回 `MenuPlugin`。

如果有构造函数日志，但没有 `menuActions`，说明插件未进入菜单管理器，重点查：

- `name()` 是否为空或和其它插件重名。
- `pluginType()` 是否返回正确。
- Peony 是否真的重启成新进程。

### 8.2 插件加载了但动作不出现

检查 `menuActions()` 是否返回空列表。常见原因：

- 只处理了 `DesktopWindow`，但实际右键来自 `DirectoryView`。
- 只处理了 `selectionUris`，但空白处右键 `selectionUris` 为空，需要回退到 `uri`。
- 过滤条件过严。

### 8.3 旧窗口没有新菜单

Peony 插件只在进程启动时扫描。安装后必须重启：

```bash
peony -q
peony
```

或者用 Peony Manager 的安装按钮，它会自动重启。

## 9. 新插件开发 checklist

- [ ] 文件名为 `libpeony-xxx.so`
- [ ] ARM64/aarch64 构建
- [ ] 类放入 `namespace Peony`
- [ ] 继承 `QObject, public Peony::MenuPluginInterface`
- [ ] 使用 `Q_PLUGIN_METADATA(IID MenuPluginInterface_iid FILE "...json")`
- [ ] JSON 内容为 `{"version":"3.2.2"}`
- [ ] `Q_INTERFACES(Peony::MenuPluginInterface)`
- [ ] `pluginType()` 返回 `MenuPlugin`
- [ ] `name()` 返回稳定名称
- [ ] `menuActions()` 同时考虑 `DirectoryView` 和 `DesktopWindow`
- [ ] 安装到 `/usr/lib/aarch64-linux-gnu/peony-extensions/`
- [ ] 重启 Peony
- [ ] 查看 `~/.log/peony.log`

## 10. 已验证样例

`plugins/peony-copy-path/` 是当前可工作的参考实现。它提供：

- 文件/文件夹右键复制完整路径
- 多选逐行复制
- 空白处右键复制当前目录
- 通过 Peony Manager 安装/移除
