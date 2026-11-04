# 菜单排序与标签合并

在文件管理器的实际右键菜单中选择 **自定义菜单顺序…**。编辑器只显示刚才那份菜单里的可见功能，保持相同的文字、图标及顺序；灰色不可用项仍可移动，子菜单带箭头并作为整体排序。不再提供全局功能总表或设置中心里的独立入口。

- 文件夹、空白处分别保存；不同文件类型的菜单分别保存，多选文件夹和多选文件也独立，互不影响。
- 拖动或用上移/下移调整；保存后下次打开这类右键菜单生效。
- “恢复此菜单默认顺序”只清除当前类型的排序。
- 配置存于 `~/.config/peony-manager/menu-order-contexts.json`。旧版总表配置不会套用到新菜单，也不会删除。
- 直接在用户点击排序入口时复制当前菜单，包含 Peony 异步补充的菜单项；不使用历史收集列表或预置目录。

**合并所有标签到此窗口** 保留当前窗口标签在前，再按来源窗口及其中标签的顺序逐个追加，保留重复目录。只有一个窗口时该项置灰。兼容当前安装的 Peony 3.26，使用 Qt 元对象的 `TabWidget::addPage`。

合并通过原生接口重新打开每个标签地址，核对目标标签地址与数量后才关闭来源窗口。来源标签的后退/前进历史及选中项不会迁移。加载超时、目标标签变化或版本接口不匹配时停止，并保留尚未完成的来源窗口。来源标签变化、存在嵌入终端会话或拒绝关闭时，保留该窗口并提示。独立进程/独立 D-Bus 会话及桌面窗口不参与合并。

## 构建和验证

以下命令在统一仓库根目录执行：

```sh
bash peony-manager/plugins/peony-window-tools/build.sh
python3 peony-manager/tests/run_real_peony_window_tools.py
python3 peony-manager/packaging/build-window-tools.py peony-manager/dist/peony-window-tools_1.1.0_arm64.deb
```

构建需要 g++、dpkg-architecture、匹配宿主的 Qt 5/Peony 开发头文件和 moc，优先系统头文件，否则使用 vendor SDK。可通过 `PEONY_SDK_ROOT` 指定包含 `vendor/extract/qt/usr` 和 `vendor/extract/libpeony-dev/usr` 的 SDK 工作目录；SDK 不随源码发布。打包命令的文件名可自行指定，包内架构由 dpkg 检测；此处示例及发布附件为 ARM64。

当前原生测试脚本使用 ARM64 的 Qt/Peony 库和外部 SDK，在隔离 Xvfb、D-Bus 和临时配置目录中运行本机真实 Peony，不接触用户窗口。覆盖文件、文件夹、空白处、图片、多选文件夹和多选文件六类实际菜单与编辑器逐项对应、独立保存与下一次菜单顺序，以及合并标签和终端保护。其他架构、Qt 或 Peony 版本需要另行适配和验证。

安装升级包后，需要重启一次 Peony 进程以加载新版本。安装包不会主动关闭文件窗口。旧版独立 `peony-menu-order` 编辑器已移除。

版本与安装说明见[本次更新记录](../../../docs/updates/peony-window-tools-v1.1.0.md)。本插件沿用仓库 GPL-3.0 许可，DEB 包附带使用说明和许可证。
