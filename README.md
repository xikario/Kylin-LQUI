# Kylin-LQUI

面向国产化桌面硬件的统一视觉与交互框架。在 UKUI 的系统服务之上整合桌面分区、小组件、Dock、开始菜单、快捷设置、AI 小站和文件扩展。

**正式源码入口：[xikario/Kylin-LQUI](https://github.com/xikario/Kylin-LQUI)。** 旧仓库作为历史参考，新功能与修复统一在这里维护。

当前验证基线：银河麒麟 / UKUI、X11、Qt 5.12、ARM64；兼容其他发行版与硬件仍需验证。KWin、NetworkManager、音频、电源和 Fcitx/Rime 继续提供系统能力。正式登录接管仍未默认启用，原生桌面保留为恢复路径。

首次使用请先阅读 **[应用框架与使用指南](docs/FRAMEWORK_GUIDE.md)**：包括模块关系、完整仓库目录、构建与安装顺序、配置/数据位置、更新恢复及验证边界。Peony 原生插件提供独立构建脚本，所需开发依赖与系统安装条件也已列明。

## 模块与源码

| 模块 | 目录 |
|---|---|
| Fences 桌面、文件分区、小组件、系统监视 | `code-work/ukui-liquid-desktop/apps/ukui-fences` |
| 开始菜单 | `code-work/ukui-liquid-desktop/apps/ukui-kaishicaidan-v2` |
| Dock、系统托盘、日期与资源组合区 | `code-work/ukui-liquid-desktop/apps/ukui-panel-liquid` |
| 统一设置、快捷控制中心 | `code-work/ukui-liquid-desktop/apps/kylin-lqui-settings` |
| 公共玻璃材质、图标、设置桥接、弹窗 | `code-work/ukui-liquid-desktop/shared` |
| AI 小站（Codex / ACP 客户端） | `code-work/liquid-cli` |
| Fcitx 4 液态候选前端 | `code-work/fcitx-liquid-panel` |
| 输入法兼容材质库 | `code-work/shared` |
| AI 空间、本地索引与检索 | `code-work/ai-space` |
| Peony 管理器与文件扩展 | `peony-manager` |

保留组件之间的相对目录关系，因此构建时请克隆整个仓库。输入法与 AI 空间构建统一引用主框架的玻璃、弹窗和设置桥；历史副本只保留作迁移参考，不再作为默认链接目标。

## 构建与运行

先阅读 [构建入口](docs/BUILD.md) 和 [迁移说明](docs/MIGRATION.md)。各模块独立构建、按需安装；仓库不包含 OEM SDK、用户密钥、词库学习记录、聊天/知识库数据或预编译安装包。

```bash
python3 tools/check_workspace.py
python3 code-work/ukui-liquid-desktop/integration/desktop_status.py --json
# 已安装系统开发依赖后的示例：
cmake -S code-work/liquid-cli -B code-work/liquid-cli/build
cmake --build code-work/liquid-cli/build -j2
```

Dock 默认保持居中宽度，拥挤时缩小应用图标；同应用多窗口合并为一个入口，保留原生窗口预览。快捷设置复用系统后端，使用隐藏驻留与入口悬停预备减少首次延迟；隐藏后停止轮询并释放背景场景。AI 小站常驻 UI 不代表模型进程持续运行。

## 状态与维护

- [紧凑设置中心与设置协议](docs/COMPACT_SETTINGS_20261103.md) · [2026-11-04 二轮评审](docs/COMPACT_SETTINGS_REVIEW_20261104.md)
- [2026-11-03 声音热插拔、音量反馈、Wi-Fi 与电源调整](docs/INTEGRATION_20261103_CONTROLS.md)
- [2026-11-03 Rime 表情缺字与用户字体修复](docs/RIME_FONT_FIX_20261103.md)
- [2026-11-02 跨聊天整合、当前行为与验证范围](docs/INTEGRATION_20261102.md)
- [控制中心自适应烟色玻璃](docs/CONTROL_CENTER_ADAPTIVE_GLASS_20261102.md)
- [声音整框面板与麦克风控制](docs/CONTROL_CENTER_SOUND_PANEL_20261102.md)
- [夜间交互、玻璃与资源收敛](docs/INTERACTION_PERFORMANCE_20261101.md)
- [2026-11-01 双评审源码修复（54 项）](docs/REVIEW_REPAIR_20261101.md)
- [2026-11-01 发布前修复](docs/RELEASE_CHECK_20261101.md)
- [框架设计](code-work/ukui-liquid-desktop/docs/ARCHITECTURE.md)
- [组件清单](code-work/ukui-liquid-desktop/integration/components.json)
- [安全与数据边界](SECURITY.md)

NextKde 是重要的设计参考，感谢 [NextKde/NextKde](https://github.com/NextKde/NextKde)。本项目保留 UKUI 平台服务及兼容接口，不以安装 KDE 桌面为前提。

## 许可证

项目沿用 GPL-3.0；各组件与第三方文件保留其原有版权和许可说明。参见 [LICENSE](LICENSE)、[第三方说明](code-work/ukui-liquid-desktop/THIRD_PARTY_NOTICES.md) 和各组件内的许可文件。
