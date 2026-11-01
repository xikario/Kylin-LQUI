# Kylin-LQUI 全量代码评审与 UI 设计指导

评审日期：2026-10-31。对照 [阶段总结](KYLIN_LQUI_SUMMARY_20261031.md) 所列的全部模块。评审只读，没有修改源码、没有构建、没有重启任何桌面进程。

## 0. 范围、方法与结论速览

**范围**：约 8 万行自有代码（不含 vendor、build、releases），分 7 路并行逐文件通读，最后由汇总人对关键结论做源码复核。

| 模块 | 路径（相对 `Kylin-LQUI/`） | 自有代码 |
|---|---|---|
| Fences 桌面、小组件、系统监视 | `code-work/ukui-liquid-desktop/apps/ukui-fences` | 约 2.1 万行 C++ |
| 开始菜单 V2 | `…/apps/ukui-kaishicaidan-v2` | 约 7.9 千行 |
| 任务栏插件与公共库 | `…/apps/ukui-panel-liquid`、`…/shared` | 约 8.4 千行 |
| 统一设置与控制中心 | `…/apps/kylin-lqui-settings` | 约 1.6 千行 Python |
| 集成层 | `…/integration`、`framework.py` | 约 0.8 千行 |
| AI 小站 | `code-work/liquid-cli` | 约 3.3 千行 |
| 液态输入法 | `code-work/fcitx-liquid-panel` | 约 2.0 千行 |
| AI 空间 | `code-work/ai-space` | 约 3.2 万行（含 C++ 界面） |
| Peony 管理器与插件 | `peony-manager` | 约 2.6 千行 |

**方法**：

- 逐文件阅读，并在 scratchpad 里写小程序做验证，包括 QSettings 写 .desktop、Markdown 链接渲染、正则耗时、xlsx 解析耗时、FTS 特殊输入。
- 对二进制执行 `readelf`，对数据文件执行 `stat`。
- 对照了 `maintenance/` 下的实机截图。
- git、clang-tidy、cppcheck、ruff、mypy、shellcheck 在本机都不可用，所以没有跑静态检查。

**证据等级**：

- 每条结论都给出 `路径:行号`。
- 标“实测”的条目做过实际复现。
- 标“疑似”的条目是从代码推断，尚未复现。
- 第 1 节 Top 清单里的条目都经过汇总人二次核对源码。

**数量统计**：

| 模块 | CRITICAL | HIGH | MEDIUM | 一句话评价 |
|---|---|---|---|---|
| Fences | 3 | 9 | 36 | 架构好的决策很多，但最关键的 `layout.json` 恰好是唯一的非原子持久化；主线程上 O(N²) 操作明显 |
| 开始菜单 V2 | 0 | 10 | 22 | 用 GIO 启动应用安全；X 错误、`.desktop` 读写、键盘可达性是结构性短板 |
| 任务栏插件与公共库 | 0 | 8 | 19 | 侵入式架构，全部风险压在“宿主符号不变”这一个假设上 |
| AI 小站 | 0 | 3 | 15 | 进程与凭据安全扎实；Codex 路径会卡死，审批卡会截断命令 |
| 输入法前端 | 0 | 2 | 5 | 协议侧稳健；`shared` 副本分叉，设置窗会冻结候选窗 |
| 统一设置与控制中心 | 0 | 2 | 14 | 方向正确、有测试；失败时静默，代码写法过度压缩 |
| 集成层（会话接管） | 0 | 2 | 6 | 身份校验认真；恢复路径存在单点失败 |
| Peony 管理器 | **2** | 2 | 10 | 提权路径是全项目最严重的问题 |
| AI 空间 | 0 | 3 | 14 | 防御意识最强的模块；资源预算不统一，仓库不能独立构建 |
| 横向安全 | 0 | 2 | 7 | 无硬编码密钥、无 `shell=True`；D-Bus 导出面过宽 |

---

## 1. 必须先修的 16 项（跨模块排序，均已复核源码）

| # | 等级 | 问题 | 位置 | 后果 |
|---|---|---|---|---|
| 1 | CRITICAL | pkexec 以 root 执行用户可写目录里的 helper 脚本；没有专用 polkit policy；helper 以 root 复制用户可控的 `.so`，存在 TOCTOU 并跟随符号链接 | `peony-manager/app/operations.py:13,198,203`、`helpers/peony-manager-helper:30-95` | 同 UID 的恶意进程改写 helper 后，用户下次授权即得到 root；安装的 `.so` 会被所有用户的 Peony 加载 |
| 2 | HIGH | 通用 Settings Bridge 在会话总线上开放 `Set`/`Activate`，可以改任意输入框、点任意按钮 | `shared/settings/SettingsBridge.cpp:60,127,155`，被 6 个模块注册 | 借 AI 小站偏好页改写“CLI 命令”再点“刷新”，即可在会话内执行任意命令 |
| 3 | CRITICAL | `layout.json` 先删旧文件再改名，写入返回值不检查；解析失败时静默返回，启动后又无条件用默认值覆盖 | `apps/ukui-fences/src/DesktopCanvas.cpp:5242-5259,744,775` | 磁盘满或崩溃时整个桌面布局永久丢失，且无备份 |
| 4 | CRITICAL | 撤回“新建/复制粘贴”时直接 `removeRecursively`，不进回收站，也不核对文件是否已被修改 | `DesktopCanvas.cpp:4143-4178` | 用户编辑了两小时的副本，误按 Ctrl+Z 即被永久删除 |
| 5 | HIGH | 多处直连 Xlib 却没有 `XSetErrorHandler`；窗口销毁竞态时 Xlib 默认处理器直接 `exit(1)` | Fences `DesktopCanvas.cpp:511-578,1032-1192`、`DesktopLayerWatch.cpp`；开始菜单 `StartMenu.cpp`、`TaskbarDetector.cpp`、`StartButton.cpp`；任务栏 `PanelNative.cpp`、`PanelAiStation.cpp`、`PanelLayoutCompat.cpp`；输入法 `CapsLockMonitor.cpp` | 桌面、开始菜单或整条任务栏偶发“凭空消失”，且难以复现 |
| 6 | HIGH | 任务栏插件用 dlsym 按修饰名调用约 26 个宿主私有方法，没有宿主版本闸门；崩溃后被重启时仍带插件，形成循环 | `apps/ukui-panel-liquid/src/TaskbarPinCompat.cpp:40-47`、`PanelBehavior.cpp`、`PanelDockCompat.cpp`、`PanelSystemControls.cpp`、`PanelLayoutCompat.cpp` | 系统升级 `ukui-panel` 后任务栏崩溃并反复崩溃 |
| 7 | HIGH | 会话接管 `restore()` 在写状态之前就置 `finished=True`，写失败即放弃恢复；守护进程被 SIGKILL 或 OOM 后无人恢复 | `integration/session/session_bridge.py:219-231,254` | 原生桌面已停止，又永远不会被拉回 |
| 8 | HIGH | 用 `QSettings(IniFormat)` 读写 `.desktop`（实测）：写回时段名被转义成 `[Desktop%20Entry]`、本地化键丢失、`Categories` 被截断、`Exec` 被改写；读取时含逗号的 `Name`/`Exec` 变成空串 | 开始菜单 `StartMenu.cpp:4033-4048`（写）、`AppRegistry.cpp:342-363`（读）；Fences `DesktopItem.h:154-171`、`ActivityRecorder.cpp:65` | 改图标后该应用在所有程序里都启动不了；名称含逗号的应用从菜单消失 |
| 9 | HIGH | 固定和最近记录用 `Truncate` 非原子写入；空文件会回退到默认列表 | `AppRegistry.cpp:455-539` | 用户的固定项悄悄丢失（releases 里已有一次修复备份，说明发生过） |
| 10 | MEDIUM | `org.ukui.menu` 在会话总线上导出关机、重启、注销等方法，不要求任何确认 | `StartMenu.h:59-72`、`main.cpp:109-117` | 任意会话内进程可以直接关机 |
| 11 | HIGH | Codex 请求没有超时；“停止”在握手阶段无效；审批卡把命令截到 600 字且不提示已截断；截图的翻译、识别、总结直接送进带工具权限的线程 | `liquid-cli/src/CodexClient.cpp:192-195,53-56`、`Drawer.cpp:274,490-496` | AI 小站永久卡死；用户可能批准自己没看到的命令；截图中的提示注入可以驱动工具 |
| 12 | MEDIUM | 通知历史默认开启，明文保存 200 条（正文最长 8 KB）；任意进程可调用 `List()`；关闭开关时不清除已有记录 | `apps/ukui-panel-liquid/src/PanelNotificationHistory.cpp:18-37`、`PanelController.cpp:173,412` | 验证码、聊天内容被持久化，并可被读取 |
| 13 | HIGH | 任务栏插件在宿主进程装了 10 个 `qApp` 级事件过滤器；面板动画每一帧都会重新采样材质、扫描 `allWidgets`、新开 X 连接 | `PanelController.cpp:469-512`、`PanelAiStation.cpp:385-454`、`PanelLayoutCompat.cpp:314-446` | 实机截图 `cohesive-desktop-20261031/desktop-after.png` 中，14:04 系统监视显示液态任务栏 CPU **79.3%**；总结里的 0.33% 只是 3 秒静置样本，不能代表常态 |
| 14 | HIGH | 宿主进程里调用应用级模态 `exec()`；UI 线程上有同步 D-Bus 内省（默认超时 25 秒）；输入法设置窗同步等待 Python 最长 3 秒 | `shared/liquid-popup/src/LiquidDialog.cpp:411-437`、`PanelLauncherBridge.cpp:21`、`StartButton.cpp:68`、`fcitx-liquid-panel/src/SettingsDialog.cpp:165-181` | 整条任务栏或候选窗冻结 |
| 15 | HIGH | Peony 插件状态读取失败时一律显示为“已启用” | `peony-manager/app/gsettings_store.py:152-162`、`scanner.py:334,391-399`、`models.py` | 与“无法判定的状态不冒充已启用”的设计目标相反 |
| 16 | HIGH | 公共库分叉为两份：输入法用旧的 `code-work/shared`；AI 空间引用仓库外的兄弟目录，因此无法独立构建；任务栏插件 `.so` 的 RUNPATH 带着开发机 SDK 路径（实测） | `fcitx-liquid-panel/CMakeLists.txt:3-4,24`、`ai-space/CMakeLists.txt:188`、`readelf` 检查 `build/plugins/styles/libukuiliquidpanel.so` | 修复不同步；发布包不可复现；宿主进程可能加载错误的库 |

---

## 2. 系统性根因（横切问题）

单个缺陷修完还会再长出来，因为下面这几类问题在多个模块里重复出现。每一类都应该做成公共设施，各模块统一使用。

1. **X11 访问没有统一封装**
   - 现状：Fences、开始菜单、任务栏、输入法各自 `XOpenDisplay`，大多数地方没有错误处理器。少数地方的处理器是进程全局替换，例如 `TaskbarPinCompat.cpp:214`。
   - 建议：
     - 在 `shared/x11` 里提供 `ScopedXErrorTrap`（RAII 方式：`XSync`、安装处理器、结束时还原）。
     - 复用 Qt 自己的 xcb 连接（`QX11Info`）。
     - 所有 `XGetWindowProperty` 调用后都检查 `nitems>0 && format==32`。
2. **持久化写法不统一**
   - 写法正确的：`activity.json`、AI 小站、Peony CLI 配置、通知历史都用 `QSaveFile` 或 `.new`+`replace`。
   - 有问题的：`layout.json`、固定/最近、`.desktop` 图标、`rime.conf`、安装器的 `.desktop` 都是非原子写；多处在读取失败后直接用默认值覆盖原文件。
   - 建议：做一个公共的 `AtomicStore`，统一规则：
     - 读取失败时把原文件改名为 `.invalid-<时间>`，并提示用户；
     - 写入前保留 `.bak`；
     - 带 `version` 字段并走迁移函数；
     - 区分“未配置”和“配置为空”。
3. **`.desktop` 解析重复实现 3 套以上，且都用 QSettings**
   - 涉及开始菜单、Fences、任务栏固定。
   - 建议：写一个约 100 行的规范解析器，放进 `shared/desktop-entry`，支持：
     - 只解析 `[Desktop Entry]` 组；
     - 处理转义序列；
     - 按 locale 回退本地化键；
     - `TryExec`、`OnlyShowIn`/`NotShowIn`、`Hidden`、`NoDisplay`、`Keywords`、`GenericName`。
   - 写回时只替换目标行，永远不整体重写文件。
4. **D-Bus 导出过宽**
   - 现状：`Q_SCRIPTABLE` 槽配合 `ExportScriptableSlots`，把关机、退出、`setVideoWallpaper(path)`、通用 UI 自动化接口都放在会话总线上。
   - 建议：改用 `QDBusAbstractAdaptor` 显式列出白名单；危险操作要么不导出，要么校验调用方（`GetConnectionUnixProcessID` 对应的可执行文件）。
5. **UI 线程上做阻塞操作**
   - 典型情形：同步 `QDBusInterface` 内省、`waitForFinished`、`QProcess::execute`、mime 嗅探、`.desktop` 全量扫描、PNG 编码、壁纸缩放、`grabWindow`、剪贴板读取。
   - 建议：
     - 统一走 `BackgroundTask`（建议加线程池）。
     - CI 增加“主线程单次阻塞不超过 16 ms”的采样门限，例如用 `QElapsedTimer` 给事件循环打点。
6. **运行时依赖源码树路径**
   - 现状：
     - 编译进二进制的回退路径（`UKUI_FENCES_*_PATH`）；
     - 已安装的设置程序通过 `sys.path.insert(0, 源码树)` 加载代码；
     - autostart 指向源码目录，靠兼容链接才能工作；
     - 源码里共有 345 处 `/home/zhouzhang`，其中 3 处影响运行。
   - 建议：每个 release 自包含；没有找到依赖时明确报错，不再回退到源码树。
7. **代码写法过度压缩，并有神类**
   - 压缩写法：分号串联语句、单行 200 到 700 列、单空格缩进。见于 `framework.py`、`design.py`、`SettingsBridge.cpp`、`PanelSystemControls.cpp`、`main.cpp:104`。
   - 神类：`DesktopCanvas.cpp` 5529 行、`StartMenu.cpp` 4294 行、`SmartSpaceWidget.cpp` 3864 行、`FenceWidget.cpp` 2521 行。
   - 建议：先整体跑一遍 clang-format 和 black（这一步不改变行为），再按第 3 节的拆分表重构。
8. **测试与 CI 缺口**
   - 覆盖面广：Xvfb 下多档缩放的 GUI 用例。
   - 但最危险的路径都没有测试：
     - 配置损坏或被截断；
     - 文件操作失败或取消；
     - 撤回时目标已被修改；
     - 提权 helper；
     - `.desktop` 中含逗号、`Hidden` 等情况；
     - Super 键状态机；
     - FTS 特殊输入。
   - Fences 的 CI 存在以下问题：
     - 漏掉了回收站、永久删除、屏幕变化等 15 组测试；
     - `ctest -R` 引用了已删除的测试名，因为没有加 `--no-tests=error` 而静默通过；
     - 只在 x86 和 Qt 5.15 上跑，与目标平台 ARM64 和 Qt 5.12 不一致。
   - liquid-cli、fcitx-liquid-panel、peony-manager 完全没有 CI。
   - AI 空间的 CI（x86 加 arm64、Qt 5.12、sanitizers）最完整，可以作为其余模块的模板。
9. **仓库卫生**
   - 根目录有 4 个过期评审 zip、513 KB 的对话导出 `代码编写全过程.txt`（不在 `.gitignore` 里）、2 个乱码空目录；liquid-cli 下有 19 个 `build-*.log`。
   - 输入法和 Peony 管理器没有 `.git`、`LICENSE`、`.gitignore`。
   - Peony 的 `vendor` 有 92 MB，打包了 GPL 的 QTermWidget，但没有整体许可声明。
   - `os.tsx` 是设计稿，却放在 C++ 应用目录里。
   - 用户数据中的孤儿：`~/.local/share/ukui-fences/knowledge/` 下还有一份约 9.2 GB 的旧库，另有一份 680 MB、权限 0644 的备份。

---

## 3. 分模块问题清单

### 3.1 Fences 桌面（`apps/ukui-fences`）

整体评价：
- 做得好的地方：后台任务统一走 `BackgroundTask`；外部命令全部用参数列表；回收站撤回用 dev:ino、ctime 和 SHA256 回执；CSV 导出有公式注入防护；`/proc` 解析的边界处理正确。
- 问题集中在三处：持久化、撤回的安全网、主线程性能。

**CRITICAL**
- C1、C2：`layout.json` 的保存和加载问题，见 Top 第 3 项。另外 `root["version"]=4` 只写不读，将来没有办法迁移 schema（`DesktopCanvas.cpp:5218`）。
- C3：撤回会永久删除文件，见 Top 第 4 项。

**HIGH**
- H1：文件“暂时不存在”时（U 盘、网络挂载、外部程序先写临时文件再改名），分区成员关系被静默并持久地丢弃。位置：`DesktopCanvas.cpp:5351-5356,3155-3163,3929-3938`、`DesktopItem.h:62`。
- H2：没有 X 错误处理器，见 Top 第 5 项。
- H3：诊断审计在生产环境中依然运行。`kDesktopSyncDiagnostics=false`，但函数体照样执行：O(N²) 的路径比较，外加每项一次 mime 嗅探。位置：`DesktopCanvas.cpp:2996-3097,3279`。
- H4：拖动分区时，每个 mouseMove 都把全屏壁纸平滑缩放一次，用于“壁纸磁吸”。位置：`FenceWidget.cpp:1915-1946`、`DesktopCanvas.cpp:2573-2578`。
- H5：判断某个文件是否已在分区里，每次都构造 `QFileInfo` 做路径比较，复杂度 O(N²)。位置：`FenceWidget.cpp:880-899`、`DesktopCanvas.cpp:4230,3208-3256`。
- H6：批量删除时每删一项就完整重新布局一次所有图标。位置：`DesktopCanvas.cpp:3758-3773,3874-3909`。
- H7：分区和图标的坐标没有钳制到屏幕范围内，分辨率变化后可能拖不回来；`lockToDesktopGeometry` 只处理主屏，并故意越界 1px。位置：`DesktopCanvas.cpp:5327,1230-1287`。
- H8：导入布局时先删除目标文件再复制，导出时也先删除；重置布局不留备份。位置：`DesktopCanvas.cpp:5465-5495,2028-2051`。
- H9：`cp`、`gio move`、`gio trash` 用 `QProcess::execute` 调用，没有超时；全局只有一个文件操作槽。在失效的 NFS 上挂起后，整个文件操作子系统死锁，`quitApp` 也会拒绝退出。位置：`FileClipboard.cpp:251,259,588,463`。

**MEDIUM（摘要）**

文件操作：
- `rename` 的检查与执行之间存在覆盖竞态（`FileClipboard.cpp:252,262`），建议改用 `renameat2(NOREPLACE)`。
- 新建文件会截断同名已有文件（`DesktopCanvas.cpp:3839`），建议用 `QIODevice::NewOnly`。
- 跨文件系统移动失败后留下半成品，并误报为失败。
- 暂存目录 `.ukui-fences-transfer-*` 会残留。
- 错误原因全部被吞掉。
- 操作完成回调依赖发起者存活，发起者被销毁后撤回记录丢失。
- 每次删除都要线性扫描整个回收站来生成回执。
- `archive.tar.gz` 重命名后变成 `archive.tar (1).gz`。
- 内部拖放的 mime 类型可以被伪造。

持久化：
- `ActivityRecorder` 的 JSONL 崩溃后留下半行，会和下一条记录拼接。
- 保存失败后待写日志在内存里无限增长。
- `ActivityLedger::prune` 会删除“未来日期”的数据，开机时钟跳变时会误删。
- 布局里的重复 id 不做处理。
- 找不到桌面目录时回退到整个 HOME，所有文件操作都作用于家目录。
- 回收站路径硬编码，不遵循 `XDG_DATA_HOME`。

/proc 采样：
- 进程组 RSS 直接相加，共享页被重复计入（`ProcessMetrics.cpp:44`），建议对前台应用组读 PSS。
- `id.contains("lock")` 会把 clock、block 之类的应用排除出时长统计（`ActivityRecorder.cpp:143,189`）。
- `entryList` 未加 `NoSort`。
- 每次采样都读取完整的 `maps` 文件。

主线程：
- 图标构造时同步做 mime 嗅探。
- 每个 watcher 事件都执行重活。
- 壁纸缓存在主线程重建。
- `syncDesktopIcons` 里调用了 `processEvents`，可能重入。
- 排序比较器在每次比较时做 `stat`。
- 格子用完后图标全部叠在左上角。
- 分区加载时每加一项就读一次剪贴板。
- 视频壁纸停止时阻塞约 3 秒。
- MPRIS 封面在主线程读取：播放器给出的路径若指向 FIFO 会永久阻塞，http 封面会让桌面发出请求。

其他：
- 壁纸解码的像素上限是 1 亿，约 400 MB 内存。
- 部分 Python 辅助程序启动时没有加 `-I`。
- 没有处理 SIGTERM，注销时最多丢失 60 秒的活动数据。

**LOW**
- `m_desktopSyncTimer` 从未 `start()`，是死代码。
- 桌面目录探测有两套实现，候选顺序不同。
- 读取 `quickshell`、`waybar` 等与 UKUI 无关的配置路径。
- MPRIS 默认客户端写死了开发机路径。
- `FenceIconPicker` 的 lambda 以 `[&]` 捕获栈上变量（疑似悬挂）。
- 剪切态视觉效果 3.5 秒后强制消失，与剪贴板状态不一致。
- 没有 `-Wall`。
- 清明节日期只特判了 2026 年。

**拆分方案**：把 `DesktopCanvas` 从 5529 行降到 1200 行以内。

| 新模块 | 来源（`DesktopCanvas.cpp` 行号） | 作用 |
|---|---|---|
| `LayoutStore` | 5142-5518 | 纯数据，可单测；改用 `QSaveFile`，带备份和版本迁移 |
| `X11DesktopLayer` | 483-578、1032-1257 | 共用一个 Display 连接，并加上错误陷阱 |
| `WallpaperController` | 1289-1700、2573-2650 | 磁吸改用降采样缓存 |
| `DesktopSyncService` | 2959-3288 等 | 在后台线程计算增删差异 |
| `LooseIconGrid` | 3441-3756 | 占用数组加批量布局 |
| `FileActionController` + `UndoStack` | 4003-4675 | 与 `FenceWidget` 共用一份实现 |
| `FencesDBusAdaptor` | 2788-2957 | 显式白名单 |
| `DesktopContextMenu` | 4918-5140 | 右键菜单构建 |
| 设置页 | 2073-2689 | 迁到 `FencesSettingsWindow` |

`FenceWidget` 拆出 `FenceSnapping`、`FenceWallpaperMagnet`、`FencePainter`、`FenceIconLayout`、`FenceContextMenu`。

### 3.2 开始菜单 V2（`apps/ukui-kaishicaidan-v2`）

整体评价：
- 做得好的地方：启动应用走 GIO；卸载流程有包名校验、模拟、受保护名单和确认；安装是事务式的。
- 短板：X 错误处理、`.desktop` 数据层、4294 行的 `StartMenu` 神类。

**HIGH**
- H1：没有 `XSetErrorHandler`，见 Top 第 5 项。
- H2：“点击外部关闭”实际只靠每 160ms 一次的 `XQueryPointer` 轮询，`ActivationChange` 分支是空的。位置：`StartMenu.cpp:1935-1940,4172-4235`。后果：
  - 一次点击通常 60-150ms，约一半的外部点击检测不到；
  - Alt+Tab 切走时菜单永远不关；
  - 拖拽到菜单外时，菜单会在拖拽中途关闭。
- H3：改图标时损坏 `.desktop`（实测），见 Top 第 8 项。另外 `QFile::copy` 的返回值没检查，`/usr/local`、`/opt` 下的文件会被直接写。
- H4：用户级的 `Hidden`/`NoDisplay` 覆盖不起作用，原因是先过滤再登记 desktop id。位置：`AppRegistry.cpp:308-314`。
- H5：搜索和“所有应用”没有虚拟化，每次按键都在主线程新建几百到上千个 widget；`iconForDesktopIcon` 没有缓存，未命中时扫描 hicolor 目录，绘制路径里也会调用它。位置：`StartMenu.cpp:2228-2381,728-789,2802`。
- H6：固定和最近记录的持久化问题，见 Top 第 9 项。另外用户永远无法把固定项清空到 0 个，因为空文件会回退成默认列表。
- H7：没拿到 `org.ukui.menu` 名字时仍然拦截 Super 键，一次按键可能弹出两个菜单；名字丢失后也不会恢复。位置：`main.cpp:95-127`。
- H8：剪贴板历史常驻后台，没有上限：最多 20 张完整 QImage（4K 截图可达 600 MB 以上），会记录密码管理器复制的内容，判重用逐像素比较。位置：`StartMenu.cpp:3072-3115`。
- H9：键盘可达性几乎为零，详见第 4 节：
  - 没有方向键导航，Enter 总是启动第一项，空搜索时按 Enter 会启动浏览器；
  - 左栏是手绘加坐标命中测试，键盘无法到达电源等入口；
  - 没有焦点环、没有 `accessibleName`。
- H10：三处同步 `QDBusInterface` 内省，默认超时 25 秒。位置：`StartButton.cpp:68`、`main.cpp:44-50`、`TaskbarDetector.cpp:373`。

**MEDIUM（摘要）**

Super 键：
- 手写的 `XRecordInterceptData` 结构与真实头文件不一致：`client_seq` 应为 `unsigned long`，导致 `category` 过滤实际失效，目前只是碰巧正确（`KeyInterceptor.cpp:53-61`）。
- 回调函数的签名与 Xlib 要求不符，属于未定义行为。
- keycode 只在启动时解析一次，键盘布局变化后不更新。
- Super+鼠标拖窗口会被误判为短按。
- 释放事件丢失后 `m_superDown` 卡死。
- 计时用墙钟，会受时间跳变影响。
- 80/250/500ms 三个时间窗的去重状态机没有单元测试。

D-Bus：
- 导出了电源方法，见 Top 第 10 项。
- 先占名字、后注册对象。
- `--quit` 会先占用 `org.ukui.menu` 再退出。
- launcher 脚本注释写“启动并 toggle”，实际没有传 `--show`。

数据层：
- 含逗号的值解析后丢失（实测）。
- 没有处理 `TryExec`、`OnlyShowIn`、`Keywords`；本地化名硬编码为 `zh_CN`；子目录不递归；判重是 O(n²)。
- 把 `~/Desktop`、`~/应用/*`、`~/apps` 当作应用目录。
- “最近使用”按 Exec 的 basename 匹配，flatpak、python 之类的启动器会让大量应用被误标为最近使用。
- 目录变化时全量重扫，并给每个文件单独加一个 inotify watch。

搜索与排序：
- 没有拼音搜索，搜索还会匹配 exec 全文导致噪声。
- 结果没有相关度排序；排序不用 `QCollator`，中文不按拼音，且不稳定。

界面：
- 多屏时背景快照坐标疑似错位。
- “所有应用”容器高度算错（86 vs 90），末尾约 1.5 行可能滚不到。
- 菜单固定 680×720，没有使用 `availableGeometry`，1280×720 屏幕上会溢出。
- 右键菜单持有的 `app` 引用在异步刷新后会悬空（疑似）。
- 活动窗口追踪在 GUI 线程做全量正则匹配，并把每次焦点变化写盘。
- 关机、重启没有确认，失败也不提示。
- 隐藏时不释放资源；退出时 `drain` 可能被 `apt-get -s` 卡住约 8 秒。
- 生产路径有大量 `qDebug`。

**LOW**
- 魔法数字集中在左栏坐标。
- `main.cpp:104` 有一行 700 字符的 lambda。
- `isX11Platform`、`applyX11Immunity`、壁纸强调色计算各有 3-4 份重复（与 Fences 之间也重复）。
- 约 150 行死代码：`RecentFileButton`、`RecentAppButton`。
- 没有 i18n（`tr()`）。
- `openTerminalAsAdmin` 的 pkexec 回退分支实际无效。
- `os.tsx` 建议移到 `docs/design/` 或直接删除，它已与实现不一致（720 vs 760）。

**拆分方案**：

| 新模块 | 作用 |
|---|---|
| `X11Utils` + `X11ErrorTrap` | 统一 X11 访问 |
| `AppCatalog` | 合并 `AppRegistry` 和监视逻辑；自写解析器；用 `QSaveFile` 保存 |
| `AppListModel` + `AppSearchIndex` | `QListView` 加委托；拼音、打分、`QCollator` 排序 |
| `LeftRail` | 改用真实 `QToolButton` |
| `OutsideClickWatcher` | 事件驱动地检测外部点击 |
| `ClipboardHistory` | 剪贴板历史 |
| `LaunchService` | 应用启动 |
| `PowerActions` | 走 logind 并加确认 |
| `IconProvider` | 图标查找加缓存 |
| `ActiveAppTracker` | 活动窗口追踪 |
| `GlassController` | 玻璃材质 |

建议顺序：先做 `X11ErrorTrap` 和 `AppCatalog`（修实际故障），再做 `AppListModel` 和 `LeftRail`（修可访问性和性能）。

### 3.3 任务栏插件与公共库（`apps/ukui-panel-liquid`、`shared`）

整体评价：
- 分层清楚：材质、居中布局、Dock、托盘、固定、AI 球各自一个组件。
- 但它在宿主进程内装全局事件过滤器，并调用宿主私有符号，任何错误都等于整条任务栏崩溃或冻结。

**HIGH**
- H1：宿主 ABI 没有闸门，见 Top 第 6 项。建议：
  - 安装时把宿主包版本或 sha256 写入插件配置；
  - 运行时不匹配就禁用所有 dlsym 特性；
  - 在包装脚本 `scripts/ukui-panel-liquid` 里加崩溃计数，连续崩溃后自动改走 `--original`。
- H2：10 个 `qApp` 级事件过滤器，很多先做字符串比较、`inherits()`、`property()`，然后才判断事件类型。位置：`PanelSystemControls.cpp:118,136`、`PanelLauncherBridge.cpp:15-59`、`TaskbarPinCompat.cpp:261` 等。建议：
  - 每个过滤器的第一行先 `switch(e->type())`；
  - 字符串比较用 `QLatin1String`；
  - 在 `Polish` 时给目标打标记；
  - 合并成一个分发器。
- H3：面板动画的每一帧都做材质采样、后台任务、`allWidgets` 全量扫描、`XOpenDisplay` 和 shape 重算。位置：`PanelController.cpp:469-512,554`、`PanelAiStation.cpp:385-454`。建议：动画期间复用上一张缓存，结束后防抖 100ms 再重建一次。
- H4：`PanelLayoutCompat` 对面板任意后代的 `LayoutRequest` 都执行一次 apply，每次新建并关闭 X 连接、重新 `dlopen`/`dlsym`、构造 QSettings 读盘。位置：`PanelLayoutCompat.cpp:142-151,314-347,444`。
- H5：在 eventFilter 和 Paint 里同步修改几何，重入只靠一个标志位兜底，没有迭代上限或退避。位置：`PanelLayoutCompat.cpp:426-441`、`PanelDockCompat.cpp:245`、`PanelSystemControls.cpp:138`。
- H6：UI 线程上调用同步的 `isServiceRegistered`。位置：`PanelLauncherBridge.cpp:21`、`ControlCenterLauncher.h:16`、`SettingsBridge.cpp:54`。
- H7：在面板进程里用 `QDialog::exec()` 弹应用级模态对话框（启动失败、固定失败、选择文件）。对话框若被其他窗口挡住，整条任务栏都不再响应输入。位置：`LiquidDialog.cpp:411-437`、`TaskbarPinCompat.cpp:294,323,340`。
- H8（疑似）：退出时 GL 资源可能在平台集成销毁之后才析构，原因是 `PanelController` 是 `qApp` 的子对象。建议在 `aboutToQuit` 时提前释放。

**MEDIUM（摘要）**

X11 与宿主接线：
- 多个独立的 Xlib 连接都没有错误处理器。
- 断开宿主 `realigned()` 的全部连接、把宿主托盘按钮和日历重新挂到插件下，关闭功能时都不恢复。
- `allWidgets()` 全量扫描调用频率高。

材质：
- 壁纸解码没有像素上限。
- 平铺模式每次采样都把整图转成 `QPixmap`。
- 三处取整规则不同（`qCeil` 与 `qRound`），1.25/1.5 倍缩放下会多渲染一次。
- `renderPanel` 在 UI 线程做 FBO 回读；CPU 回退逐像素处理全面板。
- 遇到 llvmpipe 软件渲染时没有降级（疑似）。

监视与写盘：
- 监视整个 `dconf/user` 文件，并 fork 3 次 `gsettings`，任何程序写 dconf 都会触发。
- 滑块每次 `valueChanged` 都 `sync()` 写盘。
- 通知历史在 UI 线程同步 fsync。
- 用 QSettings 改写 `ukui-kwinrc`（疑似会丢注释和格式）。

隐私与 D-Bus：
- 通知历史，见 Top 第 12 项。
- `SettingsBridge` 的 `controls` 表从不清理，慢泄漏。

AI 球：
- 每 2 秒在 UI 线程读一次 sysfs 和 procfs。

性能：
- 每次绘制都 `.arg()` 拼接缓存键，并挤占与宿主共享的 `QPixmapCache`。

固定与 Dock：
- 点“固定”时在 UI 线程递归扫描所有 `applications` 目录。
- 原生“固定”菜单项按本地化文本匹配，翻译一变就失效。
- 每次 apply 都对每个按钮 `setStyle`。
- 任务栏上的滚轮事件被吞掉。

生命周期：
- 以裸指针为键的哈希会因地址复用而误判。
- 首次调用时宿主对象为空，`static` 符号会永久保持 `nullptr`。
- 释放 GSettings 前没有断开信号。
- 设置进程全局的动画插值器。

重复代码：
- dl 符号查找的辅助函数有 5 份。
- `liquid-panel.ini` 的路径有 3 种拼法。
- `diffuse()` 有快、慢两份实现：慢的那份（`LiquidPopup.cpp:131`）逐像素处理，每次打开对话框在 UI 线程耗时约百毫秒。

可读性：
- 11 个函数超过 50 行。
- 默认值不一致：`clarity` 有 0、0.65、0.28 三种；README 写“推荐 65%”，“恢复推荐值”实际给的是 28%。

CMake：
- `find_path(... REQUIRED)` 需要 CMake 3.18，但最低版本写的是 3.16；麒麟 V10 常见的 3.16 会静默忽略。
- RUNPATH 泄漏（实测）；安装脚本直接复制 build 产物。
- demo 被强制构建。
- 没有 `-fvisibility=hidden`。
- `shared/settings/tests` 没有接入主构建。

弹层：
- 宿主进程里所有 `QMenu` 都被套上皮肤，鼠标事件被重投递。
- 所有 tooltip 被接管，每次都 `grabWindow`。
- 弹层跨屏时另一半是黑色。

`BackgroundTask`：
- 每个任务新建一个线程，没有线程池。
- `drain` 时工作函数从不检查中断。
- `Q_ASSERT` 只在 debug 构建有效。

多屏：
- 设置和日历都锚定第一个面板。
- 双屏下 Widget 后端居中被禁用，文档没有写明。

文档与代码不一致：
- README 写“缩到 26px 后显示 ⋯”，代码的最小图标是 1px，也没有任何地方画 ⋯（`DockLayoutMath.h:16`）。

**做得好的地方**
- 回调普遍用 `QPointer` 保护。
- 文件监视能处理原子替换。
- 窗口特效的 D-Bus 调用全部异步加超时，并有回滚和事务日志。
- `.desktop` 写入使用 `QSaveFile`。
- 缓存都有上限。
- CTest 覆盖 100%、125%、150%、200% 四档缩放。

### 3.4 AI 小站（`liquid-cli`）

整体评价：
- 安全基础扎实：
  - argv 用 hex 编码，并通过 `SO_PEERCRED` 校验对端；
  - 凭据完全不经手；
  - 所有文件权限为 0600；
  - Markdown 先转义再加标记；
  - 截图只在内存中处理。
- 主要风险：卡死、审批截断、提示注入。

**HIGH**：见 Top 第 11 项，共三条：
- H1：Codex 请求没有超时，“停止”在握手阶段无效。
- H2：审批卡把命令截到 600 字且不提示。
- H3：截图动作进入带工具权限的线程；选择“完全放开”权限时也没有二次确认。

**MEDIUM（摘要）**

协议与进程：
- Codex 的行缓冲没有上限，按行 `remove` 是 O(n²)，解析失败的行被静默丢弃，并会进一步触发 H1。
- ACP 只要遇到一行非 JSON（例如 `.bash_profile` 的输出）就判整个会话失败，`stderr` 尾部也从不展示给用户。
- `stop` 和刷新时在 GUI 线程 `waitForFinished(1500)`。
- 打开设置窗就会启动一次 CLI，ACP 每次还会留下一个会话。
- 命令里含 `FOO=1` 赋值时 `exec` 失败。

持久化：
- 会话文件损坏后被当作空，下一次保存时覆盖原文件。
- 草稿不实时保存。
- 流式回复中途崩溃，已收到的部分不会落盘。

图片：
- 截图和粘贴的图片永不清理（实测已有 1 张孤儿）。
- 图片解码没有像素上限；PNG 编码在 UI 线程，约 400ms。

渲染：
- 链接显示文字可以伪造目标地址（实测），点击后直接打开。
- URL 会被加粗/斜体规则改坏，含括号的 URL 被截断（实测）。
- 状态标签对 stderr 内容使用 AutoText，会被当成富文本渲染。
- 流式回复每 45ms 整段 `setHtml`；`fitUserBubbles` 在每次 resize 时为每个气泡重建一个文档。

工程：
- Codex 与 ACP 的启动逻辑约 50 行几乎逐字复制，已经出现分歧，建议抽出 `AgentProcess` 基类。
- CMake 编进了另一个应用的 `NextKdeGlassView.cpp`，该文件存在 3 个不同版本。

截图：
- 覆盖窗口没有 `BypassWindowManagerHint`（疑似导致坐标偏移）。
- 隐藏侧栏后固定等 140ms 再抓屏。

**LOW**
- 19 个 `build-*.log`，其中两个测试日志的内容是“No tests were found”，建议删除或加入忽略列表。
- `resumedOnce` 是死代码。
- 对整个 `qApp` 装了事件过滤器。

### 3.5 液态输入法（`fcitx-liquid-panel`）

整体评价：
- 协议侧稳健：校验调用方必须是 Fcitx 所有者、不记录输入内容、数组访问有边界保护。
- 最大的问题在工程治理。

**HIGH**
- F-H1：设置窗点“应用”时，在输入法主线程同步运行 `python3 manage.py`，最长 3 秒，失败时的 kill 等待无超时；候选窗在此期间冻结。即使拼音设置没变也会执行。位置：`SettingsDialog.cpp:165-181`。
- F-H2：引用了旧的 `code-work/shared`（`LiquidOpticsRenderer` 332 行，新版是 433 行；新版有共享 GL 上下文、`LiquidDialog` 等），同时又引用新的 `ukui-liquid-desktop/shared/settings`，一个进程混用三个目录。

**MEDIUM（摘要）**

性能：
- `containsCjk` 每次调用都重新编译正则（实测每次 10µs，改成 static 后 0.23µs，一次按键约 100 次调用）。
- 自适应宽度导致几乎每次按键都重渲染材质（GL 上传加 FBO 回读）。
- 每次新的组合输入都同步抓屏。
- 没有任何延迟指标。

可靠性：
- 崩溃后没有自动重启。
- 二进制直接从 `build/` 运行。

定位：
- `SetSpotRect` 不校验坐标。
- 混合 DPI 多屏时不会按屏分别缩放。
- `UpdateLookupTable` 会把光标和布局清零（疑似）。

`manage.py`：
- 覆盖用户自己写的 `comment_format`。
- `restore` 不还原品牌改名，却打印“已恢复”。
- 依赖缺失时直接抛 traceback。
- 日志不轮转。

**LOW**
- CJK 判断范围不含全角标点。
- 每 40ms 调用一次 `XOpenDisplay`。
- `PanelControl` 的 `Quit`、`ReloadRime` 对任意进程开放。
- `tests/real_*.py` 只断言 `DISPLAY != ':0'`。

### 3.6 统一设置与控制中心（`apps/kylin-lqui-settings`）

整体评价：
- 做得好的地方：阻塞调用全部放在 `QRunnable` 或 `QProcess` 里；迟到响应有 revision 保护；隐藏后停止轮询；有测试。
- 问题：失败时静默、轮询偏重、写法过度压缩（40 行超过 300 字符）。

**HIGH**
- H1：刷新失败时没有任何提示，界面一直显示旧数据。`snapshot()` 只捕获部分异常类型，遇到 `TypeError` 等就整体失败并被丢弃。位置：`control_center.py:100-103`、`system_backend.py:65`、`desktop_controls.py:28,38`。
- H2：一次操作就重新读取完整 snapshot，期间禁用所有控件，后续输入被丢弃；滑块随后又被拉回旧值。位置：`system_backend.py:105`、`control_center.py:105`。

**MEDIUM（摘要）**

启动与轮询：
- 先 `registerService` 后 `registerObject`，第二次点击可能丢失。
- 每 2.5 秒一轮的读取包含：2 次 `pactl`、按 AP 数量 N+1 次 NetworkManager 查询、8 个 MPRIS 播放器、6 次会话相关调用。

操作：
- polkit 交互授权配 4 秒超时，用户来不及输入密码。
- 蓝牙扫描 `sleep(5)` 时占住整个界面；配对没有 agent，需要 PIN 时必然失败。
- OWE 等未识别的加密方式可能被判成“开放网络”（疑似）。
- 已保存连接只按 SSID 匹配，不核对安全类型。
- 切换输出设备时会留下部分生效的状态。

安装：
- 安装清单的 hash 不覆盖运行时依赖的源码树文件。
- 回滚时不还原被隐藏的 `.desktop` 别名。

界面与健壮性：
- 集成页每 2.5 秒整页重建一次，滚动位置丢失。
- 全屏截图和模糊在 UI 线程执行。
- 媒体封面在 UI 线程解码。
- `Gio.Settings.new` 遇到不存在的 schema 会让整个进程退出（实测）。
- 没有日志。

**LOW**
- `main.py:62` 的 `replace` 是死代码。
- `XDG_CONFIG_HOME` 为空字符串时得到相对路径 `.`。
- autostart 状态判断忽略 `OnlyShowIn`。
- 图标字形没有缓存。
- `FlowLayout` 遇到 spacer 会出错。
- 没有类型注解。

### 3.7 集成层与 `framework.py`

**HIGH**
- 会话接管的恢复问题，见 Top 第 7 项。建议：
  - 先执行恢复，再写状态；每次写状态各自 try；无论如何都要 `loop.quit()`。
  - 在停止原生桌面之前，先用 `systemd-run --user --on-active=<秒数+N> session_bridge.py --restore` 建一个独立的死人开关，恢复成功后取消。
  - 补上 SIGHUP 处理。

**MEDIUM**
- 恢复只尝试一次，没有重试。
- 准备阶段收到的信号要等原生桌面停止后才处理。
- 会话管理器较慢时可能拉起两个原生桌面（疑似）。
- 注销时最多阻塞 14 秒。
- `install-user.py` 生成的 unit：路径没加引号；导入的是旧的 `DISPLAY`，下次登录后无限重启（疑似）。
- 状态目录没有指定 `0700`。

**framework.py**
- verify 中的 inode 检查对符号链接永远相等，等于空操作；`gitHead` 只检查能否解析，不与记录值比较。所以 `verified: true` 的含义比字面弱得多。
- 单空格缩进加分号串联，可读性差。
- 建议拆成 `status()`、`verify()`、`main()`，并真正比较 inode 和 HEAD。

### 3.8 Peony 管理器（`peony-manager`）

整体评价：
- 业务校验层质量不错：严格的 bool 判断、数值范围、原子写入、0600 权限，并有测试。
- C++ 终端插件设计周到：systemd scope、清理守护、`SO_PEERCRED` 校验。
- 提权路径是全项目最严重的问题。

**CRITICAL**
- C1：pkexec 执行用户可写的 helper，没有 `.policy`，每次运行还 `chmod`（已复核，helper 属主是用户，`helpers/` 权限 775）。
- C2：helper 对 `.so` 只检查文件名和 `file` 输出；`[ -f ]`、`file`、`install` 之间有 TOCTOU；用 `cp -a` 以 root 复制用户目录，并保留符号链接。

修复方案：
1. helper 随包安装到 root 拥有的 `/usr/libexec/kylin-lqui/` 下。
2. 提供按动作拆分的 polkit policy，用 `exec.path` 指向该固定路径。
3. helper 先把源文件以 `O_NOFOLLOW` 方式复制到 root 私有临时目录，只对副本做校验和安装；校验 sha256 清单，或者改为以 `.deb` 安装。
4. 安装 `.so` 统一用 `.new`+`mv`，保证原子性。

**HIGH**
- H1：读取失败时显示为“已启用”，见 Top 第 15 项。
- H2：旧版 WebEngine 界面在 GUI 线程同步执行 `pkexec`，超时 180 秒。

**MEDIUM（摘要）**
- Python 侧的预检查在失败时放行。
- CLI 终端的“启用”有两个来源，会不一致。
- `cli-profiles.json` 损坏后整页不可用，也没有重置入口。
- 并发的读-改-写会丢更新。
- 一次 `Set` 触发 3 到 4 次完整扫描，可能超过 10 秒被强杀（疑似）。
- 安装扩展后强制重启文件管理器，与页面提示“不会关闭已打开的窗口”矛盾。
- HTML 用 `innerHTML` 插入时不转义，桥接对象又带提权入口；CSS 从 3 个 CDN 加载且没有 SRI。
- 每次右键都泄漏一个 `QAction`。
- 全局事件过滤器也作用于 `peony-qt-desktop`。

**LOW**
- helper 输出的 JSON 没有转义。
- `qDebug` 会把 URI 写入日志。
- 复制路径时 `smb://user:pass@` 会把凭据带进剪贴板。
- `.desktop` 写死开发机路径。
- 构建脚本没有加固选项。

### 3.9 AI 空间（`ai-space`）

整体评价：
- 防御意识最强的模块：
  - SQL 全部参数化；FTS 查询加引号包裹；
  - 查询走 `mode=ro`、`query_only`、`trusted_schema=OFF`；
  - 拒绝 DTD 和 ENTITY；zip 不解压到磁盘；
  - 外部工具都用列表参数；有 `setsid` 和 flock 租约；
  - CI 覆盖 x86 加 arm64、Qt 5.12、sanitizers。

**HIGH**
- H1：仓库不能独立构建：`add_subdirectory` 指向 `../ukui-liquid-desktop/shared/settings`，自己的 CI 只 checkout 本仓库。
- H2：增量签名包含 `st_dev`、`st_ino`、`st_ctime`。资料盘每次重新插拔，所有文件都会重新提取，OCR 也要重跑。位置：`smart_space_knowledge.py:604-625`、`smart_space_extract.py:130,609`。建议持久签名只用 `(size, mtime_ns)`。
- H3：Python 解析器没有单文档的总预算。实测一个 459 KB 的 xlsx 解析要 34.6 秒，状态还标为 `complete`。建议：
  - 每个文档加墙钟预算，超时标为 `partial`；
  - zip 设解压总量上限和压缩比上限；
  - 产品主流程（`--metadata-only`）对源文件不限大小，需要补上。

**MEDIUM（摘要）**

提取：
- 带 stored zip 附件的 PDF 被误判为 docx，PDF 正文被忽略（实测）。
- `pdftotext` 没有 FSIZE 和 CPU 限制，会突破磁盘保留空间。
- 旧版 `.doc` 整流读入内存，处理复杂度为二次方。
- 128 MiB 的 XML 整树解析，内存约为 10 倍，而 `RLIMIT_AS` 借用的是“OCR 内存”参数，语义错位。

查询：
- 查询只含 `_` 时 FTS 报语法错误（实测），被降级为“全库查询失败”。
- 一个不可读目录会让整个建库被拒绝，而且报错不给出路径。

隐私：
- 账户主目录回退默认开启，会绕过 Agent 沙箱的隔离。
- 导出的 `.aikb` 含完整本机路径，`library_id` 由路径派生。

死代码：
- 约 1000 行只被测试使用的旧提取代码，防护比主路径弱：会跟随符号链接，xlrd 输出会污染 stdout。

C++：
- 窗口创建依赖迁移线程完成，迁移卡住时进程既无窗口也无托盘。
- 先注册 D-Bus 后初始化。
- 在运行中的槽里 `qunsetenv`，与后台线程存在数据竞争。

数据库：
- 不做损坏检测；v1→v2 迁移前没有备份；不检查 `journal_mode` 的返回值。
- 每次写入都触发对 meta 热点行的更新，造成写放大。

其他：
- provider 子进程继承完整环境，超时只杀直接子进程，孙进程残留。
- vendoring 方式与 `CONTRIBUTING.md` 的说法矛盾，没有校验和。

**LOW**
- `black==19.3b0` 有已知 CVE（仅开发期）。
- Python 子进程启动时没有加 `-I`。
- `pdftoppm` 按页重新解析整本 PDF，复杂度 O(页数²)。
- PDF 文本按严格 UTF-8 读取，异常字节会让整个 PDF 失败。
- 共 39 个函数超过 50 行，其中 `build_locked` 528 行、`knowledge_jobs.main` 419 行，`knowledge_library.main` 嵌套深度 17。
- 没有覆盖率门禁。

---

## 4. UI 设计评审与指导

依据：`maintenance/` 下的实机截图（桌面全景、开始菜单、三套 Dock 主题、固定宽度演示、统一设置、控制中心、AI 小站），以及 `design.py`、`StartMenuTheme.h` 的样式代码。

### 4.1 总体诊断：缺的是设计系统，不是单个控件

| 现象 | 证据 |
|---|---|
| 颜色没有单一来源 | 7 个模块共约 500 种互不相同的硬编码颜色：Fences 134、设置 128、shared 88、开始菜单 57、AI 小站 47、任务栏 29、输入法 26 |
| 强调色至少 5 种蓝 | 设置主按钮 `#348ff6`、任务栏模块色 `#328bf0`、滑条 `#63a8ff`、开始菜单 `rgba(59,130,246)`、焦点色靛蓝 `79,70,229` |
| 开始菜单直接套用 Tailwind 调色板 | `StartMenuTheme.h:120-150`（slate-900、blue-500、red-600），正是项目规范里点名禁止的“未修改的库默认值” |
| 圆角没有刻度 | 设置 QSS 里同时出现 5、7、8、10、12、15、18、19 px |
| 同一语义两种颜色 | 电源按钮：开始菜单用红色 `220,38,38`，控制中心用橙色 |
| 字号没有刻度 | 设置用 12、13、16、27 px；开始菜单有 8 px 的时间标签（`StartMenu.cpp:1006`） |

**建议：建立 `shared/design-tokens`**。用一份 JSON 作为唯一来源，生成 C++ 头文件、Python 字典和 QSS 片段，所有模块只引用 token。

```
color:
  surface/base, surface/raised, surface/overlay
  glass/thin, glass/regular, glass/thick, glass/reading   ← 阅读面必须近不透明
  text/primary, text/secondary, text/tertiary, text/on-glass
  accent (只有一种蓝), accent/hover, accent/pressed
  status/positive, status/warning, status/critical, status/info
  focus-ring
radius:   4 / 8 / 12 / 16 / 24 / pill
space:    4 的倍数：4 8 12 16 24 32 48
type:     12 caption / 13 body / 15 body-strong / 18 title-s / 24 title / 32 display（px@1x）
motion:   120 快 / 200 标准 / 320 强调；ease-out 进入，ease-in 退出
elevation: 0 平面 / 1 卡片 / 2 浮层 / 3 菜单与弹窗（阴影和描边成套定义）
```

明暗两套主题只换 token 的值，不改组件代码。“性能档位”（总结中列为未完成）也通过 token 实现：在 `glass/*` 上切换模糊半径和是否启用 GL。

### 4.2 设计方向：克制的中性玻璃，阅读面用实底

现在的风格大方向是对的：中性灰玻璃，不刻意追求炫彩。问题出在**层数太多、层级太平**。桌面全景截图里同时有 10 个以上的玻璃卡片：时钟、日历、音乐、系统监视、今日应用、5 个分区，它们透明度相同、描边相同、圆角相同，视线没有落点。

建议定三条规则：

1. **三级材质，按用途分配。**
   - thin（几乎透明）：只用于分区标题条、桌面图标底板。
   - regular：小组件。
   - thick 或 reading（近不透明）：任何需要连续阅读的区域，包括系统监视的进程表、AI 对话、通知列表。
   - 文字下方永远要有足够深的背景遮罩（scrim），让对比度不受壁纸影响。
2. **一屏只有一个主角。** 桌面上的小组件应该有主次。建议提供“安静桌面”预设：只保留时钟和一个信息卡，其余折叠成边缘小签。
3. **用尺度对比代替重复的描边。** 一级信息用大字号（日历里的“31”就是好例子），二级信息降低对比度，不要每个块都加同样的 1px 白边。

### 4.3 任务栏与 Dock

- **最小尺寸下限。** 固定宽度演示中，39 个应用时图标只有约 14px，运行指示条连成了一条虚线，已经无法和图标对应。
  - 尊重你“不要 ⋯、不滚动”的决定，同时建议设一个下限，例如 24px@1x。
  - 低于下限时，默认把同一应用的多个窗口合并成一个图标并显示窗口数角标，这样不需要溢出入口。
  - 指示条宽度随图标一起缩放，并改成圆点。
- **README 与实现矛盾。** README 写会显示 ⋯，实际已经去掉（见 3.3 的 M19），需要同步。
- **右侧状态岛的问题：**
  - `TEMP`、`PEAK` 是英文等宽标签，和中文界面混排，建议改为“温度 43°”“峰 44°”或只用图标。
  - 43°C 显示成黄色，暗示“警告”。颜色应该有语义：常态用文字色，超过阈值才变成 warning 或 critical。
  - 下载速度蓝色、上传速度白色，没有理由地不一致。
  - `PEAK` 那一行对比度过低。
  - 托盘岛和统计岛的高度、描边不一致，建议合并成一个岛，或统一参数。
- **性能。** 第 1 节第 13 项：截图中 79.3% 的 CPU 占用需要用长时间采样复核，不能只靠 3 秒的静置样本。
- **三套主题。** “原彩圆角”下有的图标有白色底板（搜索、TXT、草莓），有的没有（微信、蓝色系图标）。统一画布的目标还差最后一步：要么全部加底板，要么只给透明留白特别多的图标加。

### 4.4 统一设置

参考截图：`fluid-dock-20261031/settings-fixed-width.png` 和 `fusion-20261031/settings-narrow.png`。

- **每一行都放同一个模块图标**：Dock 页全是任务栏图标，AI 页全是星形。这些图标不携带信息，只制造噪音。建议去掉行图标，只在分组标题处保留模块色。
- **信息重复**：“基础长度（%）”滑条右侧已经有 61，下面又单独一行“61%”。
- **说明文字放错位置**：“由系统原生接口管理布局…关闭居中恢复全宽”放在“延长上限”下面，实际是在说明“任务栏居中”。说明应当紧贴它所描述的控件。
- **依赖关系没有表达**：“应用增多时自动延长”关闭时，“延长上限”仍然可以拖动。应该禁用或折叠。
- **不需要“刷新”按钮**：设置应当实时生效。只在模块离线时显示“重新连接”。
- **用户语言**：
  - AI 小站页的“分类：一般”下拉框和左侧导航重复，应该删掉；
  - “液态强度”是开发者术语，建议改为“背景透明度”之类。
- **对比度**：
  - 分组标题 `#7c818b`、12px，在浅色背景上约 3.9:1，低于 WCAG AA 的 4.5:1。
  - 说明文字 `#7a808a` 也同样偏低。
- **双层容器**：灰色侧栏里又套了一张白色导航卡片，可以去掉内卡，选中态用胶囊高亮即可。
- **焦点样式**：QSS 只给输入框定义了 `:focus`，按钮、开关、滑条、导航项都没有键盘焦点样式。

### 4.5 开始菜单

参考截图：`cohesive-desktop-20261031/menu-after.png`。

- **左栏图标语义不明**：头像、绿点、青色圆环、绿方块、剪贴板、水滴、紫圆、电源，颜色和形状各不相同，也没有文字。
  - 建议统一成单色线性图标，带 tooltip，并改为真实的可聚焦按钮（同 3.2 的 H9）。
  - 颜色只留给“当前状态”，例如当前主题。
- **留白失衡**：“已固定”只有 2 行半，下面到“最近使用”之间有一大块空白。
  - 可以让最近使用区上移；
  - 或者固定区按行数自适应，空间留给“最近文件”。
- **名称截断**：“Hermes Des…”“Strawberry …”。网格项建议允许两行，第二行才省略。
- **键盘体验应当作为首要特性**：
  - Super 键打开后直接输入搜索；
  - 方向键在结果中移动，并有清晰的焦点环；
  - Enter 启动当前项；
  - Esc 第一次清空搜索，第二次关闭菜单；
  - 左栏可以用 Tab 到达。
- **电源操作**：关机、重启需要确认（可以设为可关闭的二次确认），颜色和控制中心统一。

### 4.6 控制中心

参考截图：`fusion-20261031/control-installed.png`。

- Wi-Fi 和蓝牙卡片上的 `›` 是约 28px 的小方块，声音卡片上的 `›` 却是圆形，样式不统一，点击区域也偏小。
- 显示器图标和橙色电源圆钮没有文字标签，单独放在一行显得突兀；橙色电源看起来像警告。
- “勿扰模式”条没有开或关的状态指示。建议做成和 Wi-Fi 一样的开关卡片，开启时用强调色。
- 通知历史的空态自相矛盾：中间写“较早通知保留在系统通知中”，右下角又显示“系统通知 · 5”和翻页箭头。
  - 空态时隐藏分页；
  - 文案改为“暂无新通知”，下方给一个“查看系统通知（5）”的次级链接。
- 亮度和声音两行的数值位置不对齐，因为声音行多了一个 `›`。

### 4.7 AI 小站

参考截图：`control-center-20261031/ai-installed.png`。

- 内容向上滚动时，“用时 26 秒”被标题栏压住且没有遮罩。建议给滚动区顶部加 16px 渐隐，或让标题栏使用 thick 材质。
- 带图片的用户气泡里，缩略图下方有一大块空白，需要修正气泡高度的计算（与 `fitUserBubbles` 有关）。
- 列表项“•关键事实”：圆点后面缺空格，悬挂缩进也没对齐。
- 审批卡（同 3.4 的 H2）既是安全问题也是 UI 问题：
  - 完整命令放在可滚动的等宽区域；
  - 危险操作用 critical 色描边；
  - “本次对话都允许”只能是次级按钮。
- 选择“完全放开”权限时，需要红色警示加二次确认。

### 4.8 桌面 Fences

参考截图：`cohesive-desktop-20261031/desktop-after.png`。

- **溢出与重叠**：系统监视的 Kylin-LQUI 表格底部，“复制快照/查看详情”按钮压在“AI 空间/原生备用桌面”行上面，内容和按钮重叠了。表格需要限定可见行数并滚动，或者把按钮移到标题栏。
- **图标标签可读性**：在亮色壁纸区域，白色标签几乎看不清；被截断的第二行还留下半行残影（“…玩原版红色警”下方）。建议：
  - 标签最多两行并加省略号；
  - 文字加 1px 暗色阴影，或悬停时显示 thin 底板。
- **分区样式不统一**：折叠的分区只剩一条透明标题，展开的“轨道入口”是完整卡片，两者差别太大。空分区（行业资料、演示与素材）在折叠后仍然占据大片区域，看起来像布局错误。
- **信息密度**：系统监视的表格文字约 11px@2x 缩放，在 2880×1800 的屏幕上偏小。PID、RSS 这类技术列可以默认隐藏。
- **空态面积过大**：音乐小组件在“暂无已配置的播放器”时占了一大块，空态应当收缩成一行。

### 4.9 动效与可访问性（全局）

- **动效**：统一使用 token 里的时长和曲线；提供全局“减少动态效果”开关，开启时所有模块关闭材质追光和放大动画。悬停放大默认关闭，这一点是正确的。
- **最小点击区域**：24px@1x，用于托盘按钮、`›` 按钮、Dock 缩小后的图标。
- **对比度**：正文 4.5:1，大字 3:1。玻璃上的文字要在最亮和最暗两种壁纸上都测一遍，可以加进现有的 Xvfb 截图测试。
- **可访问性**：给所有自绘控件设置 `accessibleName`，并统一绘制焦点环。
- **国际化**：界面字符串现在全是中文字面量。如果将来面向更多场景，建议现在就开始用 `tr()` 包起来，成本最低。

---

## 5. 建议路线图

**第一阶段：数据安全与提权（约 1 周）**
1. Peony helper 迁移到 root 拥有的位置，加 polkit policy，改为私有拷贝后再校验（Top 第 1 项）。
2. Settings Bridge 改为白名单；收窄开始菜单、Fences、通知历史的 D-Bus 导出（Top 第 2、10、12 项）。
3. Fences 的 `layout.json` 改用 `AtomicStore`；撤回改为移入回收站加身份校验；导入和重置前先备份（Top 第 3、4 项）。
4. 开始菜单的 `.desktop` 写回改为只替换目标行；固定和最近改用 `QSaveFile`（Top 第 8、9 项）。
5. `session_bridge` 的恢复顺序，以及 `systemd-run` 死人开关（Top 第 7 项）。
6. 2027 年春节之前，修好农历的边界朔日和清明（第 6.1 节第 17 项，有时限）。
7. 开始菜单启动器失败时回退到原生菜单（第 18 项）；AI 空间的 `.desktop` 信任检查和迁移空库问题（第 19、20 项）。

**第二阶段：稳定性（约 1-2 周）**
1. `shared/x11` 的 `ScopedXErrorTrap`，全模块替换（Top 第 5 项）。
2. 任务栏插件：宿主版本闸门和崩溃计数回退；事件过滤器先判断类型；动画期间节流；去掉模态 `exec()`（Top 第 6、13、14 项）。
3. AI 小站：请求超时、审批卡显示全文、截图动作改为只读线程（Top 第 11 项）。
4. 公共库合并为一份，修正 RUNPATH，让 AI 空间可以独立构建（Top 第 16 项）。
5. 用长时间采样复核任务栏 CPU，并把采样加进验收标准。

**第三阶段：可维护性与体验（持续进行）**
1. 先整体 clang-format 和 black；再按 3.1 和 3.2 的拆分表重构 `DesktopCanvas` 和 `StartMenu`。
2. 建立 `shared/desktop-entry` 和 `shared/design-tokens`；开始菜单改用 `QListView` 并加入拼音搜索。
3. 落实第 4 节的 UI 修改：先做设置页和控制中心（改动小、收益明显），再做开始菜单的键盘体验。
4. CI 补齐：给 `ctest` 加 `--no-tests=error`；加入 ARM64 和 Qt 5.12；补上损坏配置、失败路径、提权 helper 的测试；为 liquid-cli、输入法、Peony 管理器建立 CI。
5. 仓库清理：zip、对话导出、乱码目录、build 日志；输入法和 Peony 管理器补上 `.git` 和 LICENSE；移走 `os.tsx`；在确认迁移完成后清理约 10 GB 的旧知识库。

---

## 6. 第二轮补充评审

第一轮有几块没有读到，本轮补齐：

- Fences 其余文件约 3.3k 行：小组件、音乐、日历、设置窗、玻璃绘制、`scripts/*.py`；
- AI 空间的 C++ 界面约 9k 行；
- 各模块的安装、打包、会话脚本约 2.5k 行，按清单检查，用 Sonnet 执行；
- 全部测试代码约 2.3 万行，做机械排查，用 Sonnet 执行；
- 剩余约 30 张 UI 截图。

### 6.1 新增的必修项（按紧急程度排序，均已复核）

| # | 等级 | 问题 | 位置 | 后果 |
|---|---|---|---|---|
| 17 | **HIGH，有时限** | 农历用 ICU 计算，在边界朔日会错一天。实测输出把 2027-02-07 标为“春节”、02-06 标为“除夕”，官方春节是 **2027-02-06**；2030-02-03 也会错。另外清明只写死了 2026-04-05；公历节日会覆盖同一天的农历节日（2020-10-01 的中秋被国庆覆盖，实测）。 | `apps/ukui-fences/scripts/chinese_calendar.py:109-130,126,128` | **约三个月后**，日历上的春节、除夕和休班标记都会错。建议内置 2026–2100 的权威朔日修正表，清明按节气计算。 |
| 18 | HIGH | 开始菜单路由把原生菜单的自启隐藏，并把 `org.ukui.menu` 的 D-Bus 服务指向 V2 启动脚本。V2 二进制缺失或崩溃时，启动脚本直接 `exit 127`，没有回退到原生菜单。 | `apps/ukui-kaishicaidan-v2/packaging/install-menu-routing.py:120-125`、`ukui-kaishicaidan-launcher:22-25` | 用户失去开始菜单，也没有自动恢复。启动脚本应在二进制缺失或启动后 3 秒内退出时，执行 `exec /usr/bin/ukui-menu`。 |
| 19 | HIGH | AI 空间的结果卡片松开鼠标就打开文件，`.desktop` 会直接执行 `Exec=`，没有信任检查；`.sh`、`.jar`、`.deb` 交给默认程序打开。文件名用 AutoText 显示，还能借富文本把 `.desktop` 后缀藏起来。 | `ai-space/.../SmartSpaceWidget.cpp:677-682,2863-2868,2906`、`DesktopItem.h:206-215` | 下载目录被索引后，搜索命中 `发票.pdf.desktop`，单击即执行任意命令。 |
| 20 | HIGH | 迁移时 `python3` 启动失败，此时 `exitCode` 为 0、`exitStatus` 为 NormalExit，代码会把空的临时文件改名成新知识库，并标记“已迁移”。60 秒的硬截止又让大库永远迁移不完。 | `ai-space/.../Migration.cpp:173-198` | 旧知识库被永久忽略，新库是空的。 |
| 21 | MEDIUM（数据安全） | Fences 里最关键的回收站、永久删除、刷新、分区重排测试**不在 CI 中**：66 个测试只跑了 37 个。CI 引用了已删除的 `monitor_diagnosis`，又没有 `--no-tests=error`，所以多年来一直静默通过。 | `.github/workflows/fences.yml:27-36` | 文件操作出回归时不会被发现。 |
| 22 | MEDIUM | 安装脚本一律信任 `$HOME`。在 Claude 桌面环境里 HOME 指向 profile-home，脚本会把入口装进错误的目录，却报告成功。 | 所有用户级 `install*.py`/`.sh` | 显示“已安装”，但真实桌面没有任何变化。应改用 `pwd.getpwuid(os.getuid()).pw_dir`，与 `$HOME` 不一致时拒绝执行。 |

### 6.2 Fences：其余文件

**HIGH**
- 视频壁纸“应用”按钮卡死。应用时用的是 `trimmed()` 之后的路径，完成判定却用未 trim 的路径比较，路径带空格时永远对不上；没有超时，也没有取消按钮。位置：`DesktopCanvas.cpp:2516,2526`。
- `renderedWallpaperImage()` 在壁纸缓存为空时返回内容未初始化的 `QImage`，取色和磁吸会读到随机颜色或堆上残留数据（疑似）。位置：`DesktopCanvas.cpp:2573-2589`。
- 日历每画一个日期格，都把全部待办遍历一遍并解析 JSON，复杂度 O(31×N)。位置：`CalendarDesklet.cpp:214`。
- 分区每次 paint 都做路径布尔运算：`titlePath` 在玻璃模式下根本用不到，却无条件计算；`fenceShapePath()` 每帧计算 4 次。位置：`FenceWidget.cpp:1724,1478-1519`。
- `createWallpaperSettingsPage` 有 342 行，靠 `shared_ptr` 状态包在 12 个以上的 lambda 之间传递状态。建议拆成 `WallpaperSettingsForm`。

**MEDIUM（摘要）**

绘制与定时器：
- 表盘每秒完整重绘，刻度和 `addText` 都没有缓存。
- 倒计时在非运行状态也每秒重绘；定时器没有对齐秒边界，秒针会跳秒。
- 音乐小组件每秒重建图标；封面缩放用的是最近邻插值。
- 分区玻璃缓存的键不含 DPR。
- `paintEvent` 里同步渲染玻璃并输出 `qInfo` 日志。
- 每次 paint 都新建 `QIcon`。
- 每个分区都在 `qApp` 上装一个事件过滤器，并按值拷贝 mask。
- `GlassFinish` 在 GUI 线程上对整张图做 3-4 遍循环。

视频壁纸子进程：
- 任何 `PropertyNotify` 都触发约 10Hz 的全量 X 查询。
- 事件可能留在 Xlib 内部队列里没被处理（疑似）。
- GLib 回调里的异常会让 watch 被移除，父进程崩溃后子进程成为孤儿，继续解码 4K 视频。建议加 `PR_SET_PDEATHSIG`。
- 新版 libmpv 已移除 `mpv_get_wakeup_pipe`（疑似）。

日历：
- 切换月份时，农历和节假日标记会闪一下。
- 每次显示都重新拉起一次 Python。
- 监视 `-wal` 文件的变化，可能形成重载循环（疑似）。
- 全天事件解析失败（疑似）。
- 重复规则只识别中文文案。

节假日同步：
- 只有 GitHub raw 一个数据源，国内网络下常不可用，也没有镜像回退。
- 没有总时限，可能被 Qt 侧 30 秒超时杀掉。
- 没有捕获 `IncompleteRead`。
- `read_text()` 没有指定 encoding。

设置窗：
- 玻璃材质只渲染一次，之后随窗口缩放被拉伸。
- 脏标记不完整：打开对话框再取消也会被标脏。
- 导航里看不出哪些页有未保存修改。
- “即时生效”和“需点应用”的控件混在一起，无法分辨。
- 分区名称按回车不提交。
- 不透明度可以设为 0%，键盘输入时逐位生效。
- 至少 3 处控件与实际状态不同步。
- 删除分区后导航项残留。
- 时钟页只有说明文字，没有控件。

壁纸页：
- 预览在 GUI 线程解码原图，且不按屏幕比例绘制。
- 选“系统默认”后仍显示自选图片的路径。
- 视频的编码限制（H.264、≤60 秒）要等启动后才报错，报错是英文。
- 提示文字的对比度只有约 3.8:1。
- 网格的行列数与图标大小之间没有相互校验。

**LOW**
- `MusicDesklet.h:31` 的成员指针没有初始化。
- `Palette` 的主色在不同次运行之间结果不稳定。
- 启动壁纸遮罩可能提前撤掉而闪一下。

### 6.3 AI 空间：C++ 界面

整体评价：对后端失败路径的处理（截断、超时、部分失败）比同类项目细致，流式索引的完整性校验做得扎实。问题集中在三类：
- 搜索放在 UI 线程全量扫描，结果列表没有虚拟化；
- 对离线或休眠存储的假设不一致；
- 错误提示经常为空，或者用户看了不知道该怎么办。

**HIGH**
- 第 19、20 项，见 6.1。
- 搜索、导航、重排都在 UI 线程全量扫描，最多 100 万条，`maxItems` 没有生效。“加载更多”每次都重建全部卡片。`re:` 正则没有超时保护，`searchFor` 还能经由 D-Bus 触发。位置：`SmartSpaceWidget.cpp:2468-2859,3071,714-783`。
- 打开设置页时，在主线程整读并解析 `index.json`，没有大小上限；解析失败还显示成“0 项”。位置：`SettingsPage.cpp:437-447`。
- 点击结果或在输入框打字时，同步访问磁盘：`mimeTypeForFile`，每次按键都 `exists` 和 `QStorageInfo`。休眠的移动盘会让界面冻结。
- 从设置页打开知识库向导，点“开始建库”后操作被静默丢弃（状态栏在被隐藏的页面上）。位置：`SettingsPage.cpp:566-582`、`KnowledgeOperations.cpp:538`。
- 键盘无法打开结果（没有连接 `itemActivated`），格式白名单无法用键盘勾选，没有焦点样式，列表项没有可访问名称。

**MEDIUM（摘要）**
- 查询期间，列表先显示“没有匹配”，结果到了再整表重建。
- 以 `-` 开头的关键词会被 argparse 当成选项，报错内容为空；查询词出现在 cmdline 里，其他进程可见。
- 长任务输出：最后一行若没有换行会丢失；缓冲区从中间截断，可能截在半行 JSON 上；stderr 只保留开头部分，所以 traceback 只能看到第一行。
- 测试用的环境变量（`AI_SPACE_ROOTS`）会被写进用户配置。
- 保存设置时会**静默删除**当前离线的根目录。
- “返回检索”不提示有未保存的修改。
- “同步到麒麟文件索引”默认勾选，有隐私含义。
- Skill 导出到自身子目录时会无限递归复制。
- 三个对话框是栈对象并调用 `exec()`，在嵌套事件循环中可能被二次析构（疑似）。
- 模态对话框带最小化按钮。
- 向导窗口 780×680 固定尺寸，在小屏幕上会超出屏幕。
- 样式表写死 `-apple-system`、`Segoe UI` 和 px 字号，没有开启 `AA_UseHighDpiPixmaps`。

**SmartSpaceWidget 拆分方案**：拆成以下 11 个类：

| 新类 | 职责 |
|---|---|
| `SmartSpaceTheme` | 主题与配色 |
| `FramelessWindowBehavior` | 无边框窗口行为 |
| `SmartSpaceSettings` | 设置读写 |
| `SearchEngine` | 纯 C++，可放到 worker 线程运行 |
| `ResultListView` | 改为 Model 加 Delegate |
| `FolderNavigator` | 目录导航 |
| `EntryActions` | 文件打开等操作，并加入信任检查 |
| `IndexService` | 索引加载与监视 |
| `KnowledgeTaskRunner` | 知识库任务 |
| 两个对话框类 | 改为堆对象加 `open()` |
| `settings/*Tab` | 设置页各标签 |

另有两个长函数要拆：`KnowledgeManagerDialog` 的构造函数（374 行）和 `readIndexSnapshot`（320 行）。

### 6.4 安装、打包、会话脚本

这一节按 10 项清单逐个检查。除第 18 项 HIGH 外：

**MEDIUM**
- 任务栏安装器先写 autostart，再写包装脚本。若在中间被中断，下次登录没有任务栏。位置：`PL/install.py:130-152`。
- `restore.py` 不从 `before.json` 还原，而是直接删除；`--restart` 可能产生重复面板。
- `session-start.py` 严格比较 DISPLAY 字符串（`:0` 与 `:0.0` 视为不同），最多可能拉起 3 个面板。
- 任务栏包装脚本不校验 manifest 里记录的 ukui-panel hash，没有崩溃回退，与 Top 第 6 项相互印证。
- 菜单路由没有恢复脚本。二次运行时的备份已经不是原始状态。`INSTALL_AND_RESTORE.md` 里关于 `--quit` 的说法与实际行为不符。
- Fences 的 `install-user.sh`：
  - 每次升级都会覆盖用户关闭自启的设置；
  - `awk > file` 会先截断文件；
  - 没有卸载脚本。
- extras 的 `--remove` 不幂等，半路失败会留下一半的卸载状态。
- liquid-cli 的 README 写“默认不自启”，实际默认会自启。
- `resource_probe.py` 没有隔离 `XDG_DATA_HOME`，可能读写真实的聊天记录。

**LOW**
- 大量 `read_text()`/`write_text()` 没有指定 encoding，内容含中文。
- `releases/` 目录无限增长：任务栏已有 46 个，约 57 MB。
- `gdbus` 调用没有 `--timeout`。
- `fcitx-liquid-lite` 的 `restore` 不完整。
- `check_cpp.py` 依赖 git，而本机没有 git。

### 6.5 测试代码

| 模块 | 测试文件 | 行数 | CI 状态 | 主要问题 |
|---|---|---|---|---|
| Fences | 37 | 5235 | 66 个只跑 37 个 | 见第 21 项；`xrandr` 会改动屏幕；`/dev/shm/.Trash-uid` 是固定路径 |
| 开始菜单 | 11 | 1835 | 跑 | `button-follow-test.py` 会改 root 窗口的 `_NET_CLIENT_LIST` 并按 Super 键，没有自保护 |
| 任务栏 | 25 | 3403 | 跑 | 5 个 OEM 脚本没有注册；`oem_smoke.py` 没有任何隔离 |
| 统一设置 | 4 | 354 | **不跑** | 31 个用例没人执行；`test_modules.py` 依赖 build 目录，且可能改动正在运行的实例 |
| shared/settings | — | — | **不跑** | 是孤立工程，没有任何地方 `add_subdirectory` 它 |
| liquid-cli、输入法、Peony | 3、5、6 | — | **都没有 CI** | 输入法 3 个用例默认 QSKIP；Peony 的提权 helper 和 7 个 app 模块没有任何测试 |
| AI 空间 | 25 | 7847 | 三平台矩阵 + sanitizer | 最完整；`unittest discover` 零用例时返回 0 |

**需要尽快注意的问题**：
- 多个测试只靠 `DISPLAY != ':0'` 或自己设的环境变量来防止误操作真实桌面。在真实桌面上手动运行，就会用 xdotool 打字、点击、改 root 属性。建议统一加 `--require-isolated` 检查：父进程必须是 `xvfb-run` 或 `dbus-run-session`。
- 有 723 处固定等待，其中 Fences 约 87 秒。
- 全项目没有任何覆盖率工具，80% 的覆盖率要求无法度量。

**建议最先补的 5 组测试**：
1. Peony helper 的假 root 测试：符号链接、非 ELF 文件、`*cli-panel` 通配被绕过、输出的 JSON 合法。
2. `layout.json` 的损坏、截断、写入失败测试，以及撤回时目标已被修改的测试，并把回收站相关测试加入 CI。
3. `shared/desktop-entry` 的测试：值中含逗号、`Hidden`、`OnlyShowIn`、`TryExec`。
4. liquid-cli 的 fixture 加入 `silent-initialize`（握手不应答）和畸形 JSON 模式。
5. Settings Bridge 的白名单：伪造或过期的控件 id、只读控件、密码控件；`Backend.call` 的 method 白名单。

### 6.6 UI 补充：剩余截图

- **同一组设置有两套界面。** 任务栏插件自带一个深色设置窗：复选框、青色滑条、“居中长度”；统一设置是浅色：开关、蓝色滑条、“基础长度”。控件样式不同，**同一项设置连名字也不一样**。建议任务栏插件的设置窗只保留“在统一设置中打开”，或者两边共用同一套控件和文案。
- **输入法页露出了未翻译的键名 `preset`。** “折射强度 3.50”“底色深度 0.58”这类原始浮点值对用户没有意义，应改为百分比或“弱、中、强”加数值；“分类：玻璃效果”下拉框与左侧导航重复。
- **系统监视的按钮压住内容。** 14:04 和 14:38 两张截图里，“复制快照/查看详情”都压在框架进程表上，说明这是稳定可复现的布局问题。
- **日历弹窗用纯深色实底**，与桌面的玻璃风格和设置的浅色风格都不一致，是第三种风格。“丙午年马年”有冗余，“丙午马年”即可。星期表头用“星期一”全称，可改为“一”。
- **托盘溢出弹层是纯黑实底**，图标显示成灰色占位符（截图可能来自测试夹具，实机需要再确认）。
- **旧的溢出列表直接显示 WM_CLASS**（`wechat`、`lanxin`、`tdxcfv`），而不是 `.desktop` 的本地化名称。该方案已废弃，但如果 tooltip 等地方复用了同一个取名函数，也会有同样问题。

### 6.7 Fences 零散文件（最后一批，约 1.4k 行，Sonnet 执行）

**更正与确认**：视频壁纸“应用”卡死，除了 6.2 节已列的 trim 不一致之外，还有两个成因：外部 stop 和 helper 无进展。“子进程异常退出”这一情形是能正确收尾的，因为 `finished` 槽会发出 `error`，设置页随即调用 `finish(false)`。

**HIGH**
- **外部 stop 之后设置页不收尾**：stop 只发出 `state=stopped`，设置页只识别 `error` 和“已就绪”两种终态。通过 D-Bus 停止、被另一次 `start()` 抢占、在 preparing 阶段被停止，这几种情况下都会一直卡在 applying。位置：`VideoWallpaperTrial.cpp:80-89`、`DesktopCanvas.cpp:2510-2517`。
- **宿主侧没有“helper 活着但没有进展”的看门狗**：helper 自己的 15 秒 deadline 要在初始化完成后才注册；初始化本身卡住（D-Bus 或 X 连接阻塞）时，状态会一直停在 `starting`。位置：`VideoWallpaperTrial.cpp:62,76`、`scripts/video_wallpaper_trial.py:586`。
- **修复**：设置页把 `!active && state ∈ {stopped,error}` 都当作终态；宿主侧给 starting 和 preparing 加 30 秒总超时，超时后走 error 路径。

**MEDIUM（摘要）**

视频壁纸：
- helper 的 stderr 几乎全部丢弃，用户只能看到 `player exited (1)`。
- `start()` 返回 false 时没有给出原因。
- `receive()` 可能重入：`error` 分支里调用 `stop()` 时会 `waitForFinished`。
- 崩溃时退出码是信号号，文案没有区分“崩溃”和“正常退出”。
- poster 缓存没有上限，只在成功时清理；清理在 UI 线程执行，可能误删另一个正在写入的目录（疑似）。
- 遮挡区域计算对每行做 `QRegion` 并集，复杂度接近 O(n²)。
- poster 固定缩到 1920 宽，4K 屏幕上发糊；首帧是黑帧时 poster 全黑。

音乐客户端设置：
- 打开页面就自动识别并标为“有修改”。
- 启动参数按 `\n` 切分，不过滤空行，也不处理 `\r`。
- 校验报错不指出是哪一行。

歌词：
- 输入大小和条目数都没有上限，并且在 UI 线程解析。
- `[offset:]` 的值可能导致有符号整数溢出。
- 文件开头的 BOM 会让第一句歌词丢失。

字体页（`FenceWidget.cpp:2341-2520`）：
- 控件显示的是硬编码的默认值，而不是实际继承的值；只改一个颜色点“应用”，也会把字号和字体族一起写成本地设置。
- “恢复为全局设置”之后控件不刷新，再点“应用”会把旧值写回。
- 两页字体设置约 90 行几乎是复制粘贴。

其他：
- 图标选择器是栈上对话框加 `exec()`，父对象被销毁时存在二次析构风险（疑似）。
- 活动详情表每次采样都整表重建：选中行丢失、列宽抖动、排序不稳定。

**LOW**
- `findChild` 按对象名查找后直接解引用。
- helper 和 ffmpeg 从用户 `PATH` 查找，可能被遮蔽。
- `DesktopCanvas.h` 依赖传递包含 `<functional>`；同时保留 3 份壁纸图像；状态以 JSON 字符串传递，接收方要反复解析。
- 魔法数字较多。

### 6.8 评审覆盖说明

两轮下来，`components.json` 中列出的全部模块的自有代码、脚本和测试都已读过，包括第 6.7 节补上的最后约 1.4k 行零散 Fences 文件。**评审已全部完成。**

明确**不在范围内**的部分：
- `ukui-light-panel`、`liquid-UI`：研究和规划目录，不在组件清单中，也没有运行。
- `code-work/` 下的旧副本：`ukui-fences-git`、`ukui-kaishicaidan`、`*-pin-fix`、`*-x86-final-acceptance-*` 等。
- `rime-ice`、`dependencies/`：第三方代码或 SDK。
- 各 `vendor/`、`_vendor/` 只评估了引入方式和许可证，没有审第三方代码本身。

---

## 附：评审中生成的验证文件

都在 Claude 的 scratchpad 目录里，可以随时删除，不在源码树中：

- QSettings 写 `.desktop` 的复现；
- Markdown 链接渲染与性能测试；
- `Qt::mightBeRichText` 判定；
- `containsCjk` 的性能对比；
- `QSaveFile` 权限保留与 PNG 编码耗时；
- PDF/zip 多态文件和 FTS 特殊输入的 PoC。
