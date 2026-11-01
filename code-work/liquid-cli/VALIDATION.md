# 设置窗任务栏图标修订 — 2026-10-29

- 设置窗使用独立的正常任务窗口身份，避免从属于隐藏的聊天工具窗；应用退出时仍由 Drawer 回收。
- 明确发布 WM_CLASS、WM_WINDOW_ROLE 和 _KDE_NET_WM_DESKTOP_FILE，统一应用默认图标。
- 桌面入口增加 StartupWMClass，并使用已安装 SVG 的绝对路径。原生任务栏解析短图标名时会回退到通用齿轮；改为明确路径后，实际任务栏已显示 AI 图标。
- 三档聊天 UI 回归、真实 OEM/KWin 的受管理窗口和桌面身份检查通过；草稿和 23 条聊天记录保留。
- 实测图标证据：build/artifacts/window-identity/taskbar-absolute.png。

语音仅作评估：本机识别到物理麦克风及降噪输入源，已有系统 Python 3.8 的 Whisper/Vosk 和 Whisper tiny/base/small 模型文件。未录音、未加载模型、未接入语音功能；中文效果和 ARM 端耗时尚待实测。

---

# 独立小站与极简任务栏验收 — 2026-10-29

本轮完成设置窗锚定、1 秒说明气泡、765K 压缩提醒上限、极简状态栏、AI 小球拖放、输入框增高与 Fences 解耦。

- 聊天端 1 / 1.5 / 2 倍缩放全部通过：设置位置与延迟提示、上限持久化、文件/目录/文字/原始图片接收、草稿保留和恢复、换行自动增高、未发送/未启动后端等。
- 任务栏 1 / 1.25 / 1.5 / 2 倍缩放全部通过：固定列、日期布局、右键传递锚点、拖放数据与锚点传递、原有小球几何与交互。
- 原生 OEM 面板 + KWin 隔离验证通过：右键设置冷启动及窗口复用定位；拖放文字冷启动，再追加文件、目录、原始图片和本地图片；无模型会话、无发送、暂存清理完成。
- Fences 的设置中心三档缩放和启动生命周期回归通过；不再有 AI小站显示/启动/设置入口。

截图与日志位于 build/artifacts/minimal-station/、build/test-complete-final.log；面板证据在 ../ukui-liquid-desktop/apps/ukui-panel-liquid/build/artifacts/minimal-station/。

---

# 界面修订验收 — 2026-10-29

- 聊天 UI 的 1、1.5、2 倍缩放回归全部通过；每组 QtTest 为 24 passed、0 failed、1 skipped（真实模型目录探测为单独显式运行项）。
- 模型目录探测已使用本机 Codex 成功读取 8 个模型、对应强度以及当前默认模型/强度。未创建线程、未发送推理请求；专用 scope 在读取结束后释放。
- 测试替身验证目录分页、即时保存模型/强度、同一对话中切换模型，以及恢复跟随 CLI 默认配置。每次发送前通过 config/read 读取实际默认值。
- 输入区在深浅背景下都验证了文字对比和实际绘制的占位文字像素。Qt 5.12 OEM 样式会覆盖 PlaceholderText，改由编辑器绘制这一条提示，并在输入法组合时隐藏。
- 设置数值统一为横向滑块；设置窗口通过构造参数传入壁纸和颜色选项，避免构造时已安装默认皮肤后，后续安装被忽略。
- 抽屉从底部向上呼出（顶部任务栏相反）；横坐标保持不动，中途反向动画保留。
- 公共提示气泡为无箭头的圆角矩形；AI/指标提示压缩为一到两行。
- 任务栏、角落入口和双球特效验收见 ../ukui-liquid-desktop/docs/AI_STATION_PANEL_20261029.md。

本轮截图、模型目录和测试日志位于 build/artifacts/station-ui-revision/、build/test-ui-final.log、build/test-native-models.log。

---

# 本机验收 — 2026-10-29（0.2.0 对话版）

终端抽屉改为聊天框。0.1.0 的验收记录与源码保留在 `releases/source-terminal-0.1.0/`。

## 自动化回归

`ctest`：在 1、1.5、2 倍缩放下各运行 18 项 QtTest，共 54 项通过。以 `--repeat until-fail:2` 连续跑两轮，均通过。

测试替身 `tests/fixture.cpp` 实现与 `codex app-server` 相同的 JSON lines 协议，不联网，不调用模型。断言内容：

- **流式回复**：过程旁白（`phase: commentary`）不进入回复；回复下方注明命令数。
- **请求内容**：`turn/start` 的文本保留 Shift+Enter 换行。
- **权限**：选“跟随 Codex 配置”时不覆盖 approvalPolicy；选“每次询问”时传 `on-request` / `workspace-write`。
- **重启恢复**：重启后聊天记录完整，用同一线程 id 调用 `thread/resume`，不新开线程；线程无法恢复时提示并新开，旧记录保留。
- **截图**：只附加不发送。图片存进对话目录（仅本人可读），发送时以 `localImage` 传入。
- **压缩**：超过阈值出现提示，“立即压缩”调用 `thread/compact/start`，“稍后”不调用。
- **审批与停止**：审批卡片回传 `accept`；停止键发 `turn/interrupt`。
- **进程回收**：清空对话和宿主被 kill 后，Codex 及其子进程（包括自行 setsid 的）全部回收。
- **懒启动**：只展开界面时不启动进程。
- **动画与收起**：浮现动画中途反向；预览状态移开即收，点过之后不因移开而收起；Esc 收起；固定后失焦也不收起。
- **其他**：玻璃缓存不重复生成；截图层与框选像素正确，结束后无残留鼠标捕获。

## 真实 Codex（不发送提示词）

本机 Codex CLI 0.165.0，经 `liquid-cli-session` 守护的 systemd scope 启动 `codex app-server`：

- `initialize` 成功；
- 用不存在的线程 id 调用 `thread/resume`，返回 `-32600 no rollout found`，正是应用切换到新上下文的那条路径；
- 关闭后 scope 变为 inactive。

此前单独探测过 `thread/start`（ephemeral），返回线程 id。测试没有向模型发送任何消息，真实回复、真实压缩未在本机验证。

## Fences 集成

- 桌面右键菜单 → 桌面小组件 → “显示 CLI 助手（Codex 对话）”，仅在已安装时出现。
- Fences 设置新增“CLI 助手”页。Fences 测试用替身启动程序验证了：只合并本页的键（`command` 等其他键保留）、文件权限 600、“打开对话”调用 `--show`、“随 Fences 启动”能持久保存。

## 当前范围

- 只适配 Codex app-server 协议，其他 CLI 不再以终端方式嵌入。
- 上下文用量取最近一次请求的 `last.totalTokens`，与 Codex 自身显示的口径可能略有不同。
- 框选位于单个显示器内；玻璃使用静态壁纸缓存。
- 只在本机 ARM / UKUI / X11 验证。

## 部署（2026-10-29 08:53）

- liquid-cli：先用 `--quit` 退出 0.1.0（PID 36727，其终端 Codex 会话的 scope 已完整回收），再安装 `release-20261029-085317`（主程序 SHA-256 `abe09497…`）。上一版 `release-20261029-034523` 保留。以桌面会话环境启动后为 PID 6850：小把手显示，Codex 未启动。实机展开截图正常，随后收起。旧设置文件未改写，`spring` 动画在读取时按“浮现”处理。
- Fences：旧二进制（`fea8fd1b…`，即 10-27 回车修复版）与 `ukui-fences.ini`、`layout.json` 备份在 `ukui-liquid-desktop/apps/ukui-fences/releases/20261029-cli-assistant/`。新二进制 SHA-256 为 `f42dc2f7…`，经正常 `quitApp` 退出后以 `--autostart` 重启，为 PID 7012，运行文件即新安装版本。
- 部署后用户在实机上发出第一条消息（带一张截图）：Codex 在守护 scope 中启动，回复正常，脚注为“用时 5 秒”；线程 id 已保存，上下文 19.5K，模型窗口 475K。这里只检查了结构，没有读取内容。

## 资源修正（2026-10-29 09:20）

实测面板 PSS 99 MiB，其中 86 MiB 是 malloc 堆。在隔离 Xvfb（2880×1800，1.5 倍缩放）中复现：截图一次后多出一块 19.9 MiB 的匿名内存，恰好是一张全屏图片的大小，之后一直不释放；再截图也不会继续增长，说明不是泄漏，而是 glibc 动态提高 mmap 阈值后把大块内存留在了堆里。启动时固定阈值（`mallopt`）后：

| 状态 | 修正前 | 修正后 |
| --- | ---: | ---: |
| 启动收起 | 8.2 MiB | 8.2 MiB |
| 展开 | 29.3 MiB | 14.4 MiB |
| 截图三次后 | 66.1 MiB | 14.8 MiB |

Codex 一侧：scope 约 420 MiB 常驻内存 + 95 MiB 页缓存（Codex 本体 460 MiB RSS，加上用户 config.toml 中的 MCP 服务约 85 MiB PSS），空闲 CPU 约 0.1%。新增空闲休眠，默认 10 分钟后停止 Codex，下一条消息用同一线程恢复，测试已覆盖。

`ctest`：19 项 × 3 种缩放，以 until-fail:2 连续两轮通过。已安装 `release-20261029-091950`，以桌面会话环境启动（PID 20947，收起时 PSS 9.5 MiB），聊天记录与线程保留，未加入自启动。
