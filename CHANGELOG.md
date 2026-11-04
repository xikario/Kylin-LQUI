# 更新记录

统一仓库中的组件独立维护版本；历史桌面组件记录见 [桌面更新日志](code-work/ukui-liquid-desktop/CHANGELOG.md)，AI 空间记录见 [AI 空间更新日志](code-work/ai-space/CHANGELOG.md)。

## 主分支修复 · 2026-11-04 · Peony CLI

- 补充旧 QTermWidget 对 OSC 10/11 前景/背景查询的响应，修复浅色主题下用户消息显示为黑块的问题；保持 ANSI 色板不变。
- 文件夹 CLI 改用独立 Codex profile 保存模型与推理强度，避免桌面端切换默认模型时覆盖；设置页与 CLI 的 `/model` 默认保存使用同一份偏好，保留旧配置迁移与仅本次会话语义。

详见 [CLI 终端说明](peony-manager/plugins/peony-cli-panel/README.md)。本次为主分支代码修复，已发布的 Peony Window Tools v1.1.0 标签与附件保持不变。

## Peony Window Tools 1.1.0 · 2026-11-04

标签：`peony-window-tools-v1.1.0`。

- 新增实际右键菜单排序：文件、文件夹、空白处及多选菜单分类保存，支持拖动、上下移动和仅恢复当前菜单默认顺序。
- 新增将其他窗口标签合并到当前窗口，保留重复目录与标签顺序；核对成功后关闭可安全关闭的来源窗口，保留存在终端会话、标签变化或拒绝关闭的窗口。
- 提供独立 ARM64 DEB、校验文件及源码构建/测试/打包入口，随包包含说明与许可证。

完整功能、兼容范围与升级说明见[更新内容](docs/updates/peony-window-tools-v1.1.0.md)。
