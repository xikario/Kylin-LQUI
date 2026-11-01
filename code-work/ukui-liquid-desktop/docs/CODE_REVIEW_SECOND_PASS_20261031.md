# Kylin-LQUI 第二轮代码评审与 UI 设计意见

评审日期：2026-10-31。**仅评审：未修改业务源码、未部署、未重启桌面/AI/输入法进程，未执行提权安装或桌面接管。** 本轮新增内容只有本报告和维护目录中的验证材料。

## 结论

当前框架已经有较完整的日常界面与集成能力，但还不能据此认定具备稳定的跨机器发行、正式桌面接管和完整故障恢复能力。应优先处理**布局与文件数据保护、审批内容完整性、恢复状态机、AI 请求取消以及发布依赖**，再继续扩展效果。

本轮形成 **25 项需要处理的发现：8 项 P1、17 项 P2**。每项均给出当前源码位置、触发条件、证据类型、建议修复与验收方法。未确认任何 P0；“高优先”不代表这些条件已经在用户真实数据上发生。

- **P1**：存在数据损失、授权边界、核心操作无法恢复或发布构建阻断，应在继续扩大交付范围前处理。
- **P2**：特定条件下的功能、性能、状态准确性、可访问性或交付质量问题。
- 风险推断、产品取舍、旧稿中未复现的判断放在后文，**不混入已确认的缺陷数量**。

## 1. 范围与方法

评审对象是当前工作区，包含大量未提交/未跟踪的新代码，不只是 Git HEAD。主仓库基线 HEAD 为 `78c53397d744e6101bd21e98078f8c605ba3ace4`，AI 空间为 `076b89562f53d51ce81312938febb81dc8267a85`。AI 小站、输入法工作目录和 Peony 管理器在本机不是独立 Git 仓库，额外使用文件指纹记录。

建立了 **489 个文件的基线指纹**；剔除其中的 `_vendor` 后，下表统计运行源码、测试和配置。文件数代表清单覆盖，不代表每个文件都逐行人工通读。人工评审采用风险导向，覆盖全部列明运行模块的关键入口、持久化、进程/通信、提权、文件操作、查询、布局及发布链。

| 范围 | 文件数 | 运行代码/配置行数 | 测试行数 |
|---|---:|---:|---:|
| Fences、桌面文件、小组件、系统监视 | 123 | 21783 | 5235 |
| 开始菜单 V2 | 38 | 8219 | 1835 |
| 任务栏、Dock、系统入口 | 64 | 4994 | 3411 |
| 共享材质、弹窗、图标、设置桥 | 45 | 3698 | 1534 |
| 统一设置与控制中心 | 16 | 1649 | 354 |
| 框架状态与会话接管 | 10 | 900 | 221 |
| AI 小站 | 39 | 3410 | 794 |
| 液态输入法前端 | 34 | 2722 | 932 |
| AI 空间与知识库 | 77 | 19475 | 7779 |
| Peony 管理器与插件 | 31 | 3227 | 677 |

本轮执行的检查：

1. 当前源码阅读、调用链追踪、已有报告复核、已有实机截图检查。
2. 117 个自有 Python 文件进行内存中的 AST 语法解析，未发现语法错误；没有生成产品目录中的 `__pycache__`。
3. 临时数据验证 QSettings/GDesktopAppInfo 兼容性、状态写失败的恢复行为、GVariant 数组解析、文件链接复制语义、知识库特殊查询及 PDF/ZIP 类型判断。
4. 隔离 C++ 状态探针验证 AI 的“无 turnId 停止”分支和 Dock 的整数容量边界；只编译探针，没有重新构建或安装产品。
5. 临时独立源树进行 AI 空间 CMake 配置验证；只读检查已安装插件的 RUNPATH。
6. 比对 Fences 的 CI 正则与本机构建元数据；检查图标/设置/系统监视的现有截图。
7. 最后复核已记录的 489 个文件，校验和均未变化。

**未做**：全量 GUI 回归重跑、真实账号/模型联调、真实管理员授权、断网/电源操作、真实文件删除恢复、完整注销登录及跨硬件压力验收。报告不会把此前测试结果冒充本轮重新验证。

## 2. 高优先项总览

| 编号 | 问题 | 主要证据 |
|---|---|---|
| F01 | 桌面布局写入失败时仍会删除旧布局，损坏文件又可能在启动时被覆盖 | [DesktopCanvas.cpp:5244](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:5244) |
| F02 | 撤销新建或复制会永久删除目标，未识别用户后续修改 | [DesktopCanvas.cpp:4143](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:4143) |
| F03 | 手动修改应用图标会把 .desktop 写成不兼容格式 | [StartMenu.cpp:4045](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-kaishicaidan-v2/src/StartMenu.cpp:4045) |
| F04 | 提权安装链信任用户可写 helper 与未校验的运行时文件 | [operations.py:191](/home/zhouzhang/应用/Kylin-LQUI/peony-manager/app/operations.py:191) |
| F05 | 桌面接管恢复被状态文件写失败阻断 | [session_bridge.py:219](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/integration/session/session_bridge.py:219) |
| F06 | AI 请求在获得 turnId 前停住时，“停止”不能解除忙碌 | [CodexClient.cpp:53](/home/zhouzhang/应用/Kylin-LQUI/code-work/liquid-cli/src/CodexClient.cpp:53) |
| F07 | 审批卡只展示前 600 字，用户无法审阅完整授权内容 | [Drawer.cpp:270](/home/zhouzhang/应用/Kylin-LQUI/code-work/liquid-cli/src/Drawer.cpp:270) |
| F08 | AI 空间的独立仓库/CI 配置依赖未提供的兄弟目录 | [CMakeLists.txt:188](/home/zhouzhang/应用/Kylin-LQUI/code-work/ai-space/CMakeLists.txt:188) |

## 3. 逐项发现与修复验收建议

### F01 · P1 · 桌面布局写入失败时仍会删除旧布局，损坏文件又可能在启动时被覆盖

**位置：** [DesktopCanvas.cpp:5244](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:5244)；[DesktopCanvas.cpp:5258](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:5258)；[DesktopCanvas.cpp:775](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:775)。

**证据：** 源码确认。写入、flush 和 rename 的返回值均未检查；remove 旧文件发生在 rename 之前。loadLayout 对非 JSON 对象直接返回，构造函数之后无条件 saveLayout。

**触发与影响：** 磁盘满、短写、写入后崩溃或配置被截断时，最新分区、图标位置与壁纸设置没有可靠的自动恢复路径。导入/导出也有先删除目的文件的同类问题；这不是说当前用户布局已经丢失。

**建议：** 统一使用 QSaveFile/同文件系统原子替换，验证写入长度与 commit；读取时区分不存在、损坏、版本不兼容，保留损坏文件和最后一份有效备份；导入/导出拒绝源目标为同一文件。

**验收：** 临时配置目录注入短写、ENOSPC、rename 失败、损坏 JSON、同路径导入；旧有效文件必须保留，启动不得静默覆盖。

### F02 · P1 · 撤销新建或复制会永久删除目标，未识别用户后续修改

**位置：** [DesktopCanvas.cpp:4143](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:4143)；[DesktopCanvas.cpp:4167](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:4167)。

**证据：** 源码确认。deletePathForUndo 对普通文件直接 remove，对目录 removeRecursively；撤销复制/新建进入此分支，记录中没有目标内容版本核对。

**触发与影响：** 创建或复制文件后继续编辑，随后 Ctrl+Z；被编辑的目标仍可能被永久删除。目录内新增文件也会被一起删除，绕过正常回收站路径。

**建议：** 为撤销记录目标身份和修改指纹；检测到变化时提示用户，优先移入回收站或隔离恢复区；只把仍与操作回执匹配的对象作为可直接撤销目标。

**验收：** 复制后修改、新建目录后添加文件、同名目标被替换、符号链接变化；验证用户新内容可恢复，失败的撤销不会丢弃有效记录。

### F03 · P1 · 手动修改应用图标会把 .desktop 写成不兼容格式

**位置：** [StartMenu.cpp:4045](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-kaishicaidan-v2/src/StartMenu.cpp:4045)；[AppRegistry.cpp:342](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-kaishicaidan-v2/src/AppRegistry.cpp:342)。

**证据：** 隔离复现。QSettings 写回后出现 [Desktop%20Entry] 和 Name%5Bzh_CN%5D，Categories 的分号列表被截断；Gio.DesktopAppInfo 在写入前接受，写入后拒绝该文件。含逗号的 Name/Exec 被 Qt 解释为列表。

**触发与影响：** 右键应用→修改图标→保存，可能留下覆盖系统条目的无效用户启动器；只读解析也会误读含逗号的名称/命令。自动图标主题使用缓存，评审问题针对手动修改图标的文件写入路径。

**建议：** 使用 Desktop Entry/GKeyFile 解析器，保持本地化键和分号列表；只改 Icon，原子写用户覆盖文件，验证系统仍可解析后发布。

**验收：** 带逗号、分号、本地化字段、空格、引号及 Desktop Actions 的启动器，改图标后须保持可解析、可启动和原字段语义。

### F04 · P1 · 提权安装链信任用户可写 helper 与未校验的运行时文件

**位置：** [operations.py:191](/home/zhouzhang/应用/Kylin-LQUI/peony-manager/app/operations.py:191)；[peony-manager-helper:54](/home/zhouzhang/应用/Kylin-LQUI/peony-manager/helpers/peony-manager-helper:54)；[peony-manager-helper:69](/home/zhouzhang/应用/Kylin-LQUI/peony-manager/helpers/peony-manager-helper:69)。

**证据：** 源码确认，底层文件行为隔离验证。helper 位于用户源码目录并通过 pkexec 执行；运行时文件仅做 -f 检查。用无敏感内容的临时文件验证，GNU install 会跟随源符号链接并生成 0755 普通文件。本轮没有执行 pkexec 或读取特权文件。

**触发与影响：** helper 和 CLI runtime 位于用户可写工作区；若目录被篡改后，用户随后为安装动作授权，代码/材料可能已变化。运行时符号链接可让 root 读取安装材料之外的文件并复制到可读目录。需要用户授权这一前提，不能称作无交互远程提权。

**建议：** 将受信 helper 放在 root 拥有且不可由普通用户改写的位置，使用限定动作的授权策略；以调用用户权限读取材料，拒绝符号链接/特殊文件，校验 ELF 架构和版本，使用受控 staging 原子发布。

**验收：** 普通用户临时目录模拟源链接、替换竞态、错误架构、缺失运行时和半途失败；真实提权验收必须在专用测试环境完成。

### F05 · P1 · 桌面接管恢复被状态文件写失败阻断

**位置：** [session_bridge.py:219](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/integration/session/session_bridge.py:219)。

**证据：** 故障注入复现。write_state 抛出模拟 ENOSPC 后，finished 已为 True，restore_native 从未调用；再次 restore 直接返回 False。没有启动或停止真实桌面。

**触发与影响：** 限时试用已停止原生桌面之后，状态目录写失败或存储异常，可使期限到达时的恢复逻辑被跳过；用户再次请求恢复也被 finished 标志挡住。

**建议：** 恢复原生桌面作为最高优先动作，状态记录失败只降级为日志错误；成功后再完成状态机，失败允许重试，退出路径使用 finally 保证收尾。

**验收：** 注入首次/末次状态写失败、恢复服务暂不可用、重复恢复；确认恢复动作仍执行、失败可重试，不依赖日志文件可写。

### F06 · P1 · AI 请求在获得 turnId 前停住时，“停止”不能解除忙碌

**位置：** [CodexClient.cpp:53](/home/zhouzhang/应用/Kylin-LQUI/code-work/liquid-cli/src/CodexClient.cpp:53)；[CodexClient.cpp:164](/home/zhouzhang/应用/Kylin-LQUI/code-work/liquid-cli/src/CodexClient.cpp:164)；[CodexClient.cpp:192](/home/zhouzhang/应用/Kylin-LQUI/code-work/liquid-cli/src/CodexClient.cpp:192)。

**证据：** 状态机级隔离复现：进入 startTurn 后 busy=True、turnId 为空，interrupt 后仍 busy=True，存在一个未完成请求。真实模型和用户会话未被调用。request 本身没有阶段超时；5 秒保护只覆盖 guard 连接。

**触发与影响：** config/read 或 turn/start 确认迟迟不返回时，queued 已清空、turnActive 为真、turnId 仍为空；停止既不取消本地阶段，也不能发送带编号的中断。

**建议：** 补充初始化/配置/首个确认的超时和取消状态；无 turnId 时也能取消并复位，迟到回复不得重新启动任务。运行中的长任务采用可取消/心跳策略，不简单按短固定时限杀掉。

**验收：** 对端分别不回复 initialize、config/read、turn/start，以及停止后迟到回复；验证忙碌解除、可再次发送、不会产生幽灵任务。

### F07 · P1 · 审批卡只展示前 600 字，用户无法审阅完整授权内容

**位置：** [Drawer.cpp:270](/home/zhouzhang/应用/Kylin-LQUI/code-work/liquid-cli/src/Drawer.cpp:270)。

**证据：** 源码确认。QLabel 只保存 detail.left(600)，界面没有“已截断”标记、展开完整命令或完整详情入口。

**触发与影响：** 审批内容的危险参数、目标路径或附加命令位于 600 字之后时，用户看到的内容不足以支持“允许”或“本次对话都允许”的决定。

**建议：** 预览可以折叠，但完整原文必须随请求保留并可查看/复制；显示截断提示与长度，审批按钮与完整命令、工作目录、权限范围建立明确关系。

**验收：** 构造前 600 字相同、尾部动作不同的两项审批；必须能在批准前分辨；拒绝和关闭仍要可靠返回后台。

### F08 · P1 · AI 空间的独立仓库/CI 配置依赖未提供的兄弟目录

**位置：** [CMakeLists.txt:188](/home/zhouzhang/应用/Kylin-LQUI/code-work/ai-space/CMakeLists.txt:188)；[build.yml:35](/home/zhouzhang/应用/Kylin-LQUI/code-work/ai-space/.github/workflows/build.yml:35)。

**证据：** 仅配置阶段复现。提供现有 Qt SDK 后，独立源树在 add_subdirectory(../ukui-liquid-desktop/shared/settings) 处失败；没有构建或安装产品。CI 当前只 checkout 本仓库。

**触发与影响：** 从 AI 空间仓库独立检出、干净机器或 CI 构建时，无法找到设置桥；本机相邻开发目录掩盖了依赖缺口。

**建议：** 明确选择独立包或框架组件：独立包声明/随包提供版本化依赖；框架构建则同步更新 CI checkout、依赖锁定、安装说明，避免未声明兄弟目录。

**验收：** 无开发机 HOME、无兄弟仓库的干净配置；以及显式依赖已提供的成功配置、安装和运行验证。

### F09 · P2 · 暂时不可达的文件会永久失去分区归属

**位置：** [DesktopCanvas.cpp:3155](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:3155)；[DesktopCanvas.cpp:3273](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:3273)。

**证据：** 源码确认。storedItemExists 为假即 removeItem，并将 changed 的布局保存。没有区分卸载/掉线与永久删除。

**触发与影响：** U 盘卸载、网络挂载短暂不可达、外部应用替换文件的间隙，可能让原分区成员从布局中移除，恢复文件后也不再回到原分区。

**建议：** 保留离线成员记录与位置，明确状态；仅在确认删除或用户清理后移除，替换写入使用延迟复核。

**验收：** 卸载/重新挂载、短暂 rename-away、网络目录错误；恢复后成员和位置仍在，不重复创建。

### F10 · P2 · 固定列表为空会被当作“尚未配置”并重新填入默认项

**位置：** [AppRegistry.cpp:389](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-kaishicaidan-v2/src/AppRegistry.cpp:389)；[AppRegistry.cpp:501](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-kaishicaidan-v2/src/AppRegistry.cpp:501)。

**证据：** 源码确认。pinnedApps 和 pin/unpin 都以 paths.isEmpty 判断默认回退，未区分不存在和用户明确保存了空列表。

**触发与影响：** 用户取消最后一个固定应用后，刷新/重开菜单会出现默认固定项；无法表达“我不固定任何应用”。

**建议：** 区分未初始化、有效空列表、读取失败；只有首次未初始化才生成默认值。

**验收：** 清空全部固定项后重开、重启、随后新增一个固定项；列表应始终符合用户操作。

### F11 · P2 · 固定与最近记录使用截断写，写入失败无错误反馈

**位置：** [AppRegistry.cpp:479](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-kaishicaidan-v2/src/AppRegistry.cpp:479)；[AppRegistry.cpp:527](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-kaishicaidan-v2/src/AppRegistry.cpp:527)。

**证据：** 源码确认。WriteOnly|Truncate 直接打开正式文件，未检查 QTextStream/flush 提交结果，也没有原子替换或恢复副本。

**触发与影响：** 磁盘满、进程中断或权限变化时，固定项和最近记录可能丢失；界面仍可能呈现操作已完成的状态。

**建议：** 采用原子存储和错误返回，UI 失败时回滚或明确提示；同一公共存储层处理版本、损坏与备份。

**验收：** 短写、写入期间退出、只读目录和空列表均覆盖；故障后旧记录完整。

### F12 · P2 · Peony 状态读取失败时把已安装插件误报为启用

**位置：** [gsettings_store.py:64](/home/zhouzhang/应用/Kylin-LQUI/peony-manager/app/gsettings_store.py:64)；[scanner.py:37](/home/zhouzhang/应用/Kylin-LQUI/peony-manager/app/scanner.py:37)；[peony_adapter.py:43](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/peony_adapter.py:43)。

**证据：** 异常注入复现：底层读取失败后 get_disabled_extensions 返回 []，扫描器以“不在禁用列表中”判定 enabled。统一设置也消费此扫描结果。

**触发与影响：** GSettings 不可用或读失败时，用户看到的启用状态是错误的。当前写路径用 Gio.get_strv/set_strv，不能据此进一步声称会自动清空禁用列表。

**建议：** 返回未知/错误状态并在界面禁用写入或提供重试；状态展示区分“已启用”“未确认”“需要重启”。

**验收：** 读超时、schema 缺失、输出异常时显示未知；恢复后重新读取，不能用默认 True 掩盖失败。

### F13 · P2 · 输入法设置保存同步等待外部程序，候选进程可能停顿

**位置：** [SettingsDialog.cpp:165](/home/zhouzhang/应用/Kylin-LQUI/code-work/fcitx-liquid-panel/src/SettingsDialog.cpp:165)；[SettingsDialog.cpp:174](/home/zhouzhang/应用/Kylin-LQUI/code-work/fcitx-liquid-panel/src/SettingsDialog.cpp:174)。

**证据：** 源码确认。GUI 线程顺序 waitForFinished(3000)，失败后又 waitForFinished；该设置和候选前端在同一进程。

**触发与影响：** 保存候选拼音/页数时，Python 启动、YAML 处理或文件系统变慢，会阻塞候选显示与输入响应，两个等待可累加。

**建议：** 使用异步进程和明确的应用中状态，完成后统一提交；候选窗口继续响应，禁止重复提交但允许取消未提交变更。

**验收：** 让临时 helper 延迟/超时/异常退出，测量事件循环响应，并确认真实输入引擎不需要重启。

### F14 · P2 · 输入法一次“应用”实际分多次提交，后一步失败会留下部分修改

**位置：** [SettingsDialog.cpp:161](/home/zhouzhang/应用/Kylin-LQUI/code-work/fcitx-liquid-panel/src/SettingsDialog.cpp:161)。

**证据：** 源码确认。先成功保存候选拼音，再保存页数，最后保存外观；后续失败直接返回，前面已写的 Rime 配置没有事务回滚，部署通知也未发送。

**触发与影响：** 同时修改拼音、页数和外观，其中第二或第三步失败，界面提示未完成，但磁盘已经改变了一部分，下次部署/启动出现意外设置。

**建议：** 先校验全部变更，统一 staging/备份后提交；失败回滚或明确列出已提交项并允许重试，保持磁盘、预览和实际引擎一致。

**验收：** 第一步成功第二步失败、外观文件不可写、重新打开设置；核对三类状态与重部署行为。

### F15 · P2 · 只含下划线的知识库查询触发 FTS 语法错误

**位置：** [knowledge_library.py:992](/home/zhouzhang/应用/Kylin-LQUI/code-work/ai-space/apps/ai-space/scripts/knowledge_library.py:992)；[knowledge_library.py:1023](/home/zhouzhang/应用/Kylin-LQUI/code-work/ai-space/apps/ai-space/scripts/knowledge_library.py:1023)。

**证据：** 使用实际 open_db 创建临时库并调用 search_one 复现：“_”“___”抛出 fts5 syntax error near empty；普通字符串不会触发此异常。

**触发与影响：** TOKEN 接受下划线，但文件名 FTS 表达式过滤掉全部非字母数字，生成空 MATCH 表达式，异常向上变成查询失败。

**建议：** 分词后独立验证每种 FTS 表达式，空表达式返回空结果或走安全文件名匹配；不要把用户标点输入当作库故障。

**验收：** 空白、下划线、连字符、引号、混合中文和拉丁字符；所有输入得到稳定结果或明确输入提示。

### F16 · P2 · 带尾部 ZIP 数据的合法 PDF 会被识别为 DOCX

**位置：** [smart_space_extract.py:100](/home/zhouzhang/应用/Kylin-LQUI/code-work/ai-space/apps/ai-space/scripts/smart_space_extract.py:100)。

**证据：** 合成文件复现。真实 PDF 追加含 word/document.xml 的 ZIP 后，pdftotext 仍成功提取原正文，但 detect_kind 返回 docx。

**触发与影响：** 格式判断先检查 is_zipfile，再检查 PDF 魔数；索引可能读取尾部附件内容并遗漏 PDF 正文，影响知识库答案来源。

**建议：** 优先按确定的主格式魔数判断，附件/复合容器单独处理并记录来源；不靠 ZIP 可读性覆盖明确的 PDF 身份。

**验收：** 普通 PDF、附加 ZIP 的 PDF、扩展名伪装和真正 OOXML；核对主文档正文与定位信息。

### F17 · P2 · 关闭诊断日志并没有停止昂贵的桌面同步审计计算

**位置：** [DesktopCanvas.cpp:2996](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:2996)；[DesktopCanvas.cpp:3276](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:3276)。

**证据：** 源码确认。kDesktopSyncDiagnostics=false 只让 writeSyncDebug 返回；调用前仍排序、遍历所有图标/分区、反复路径比较和构造 DesktopItem。

**触发与影响：** 大量桌面项目或频繁文件变动时，生产主线程承担额外接近二次复杂度的审计工作。这里确认的是计算路径，未把未测量的耗时写成实机结论。

**建议：** 在审计入口整体早退，诊断关闭时不构造日志参数；需要诊断时使用路径索引和采样频率控制。

**验收：** 诊断关闭时审计调用计数/文件探测计数为零；大目录刷新和连续保存的事件循环耗时对照。

### F18 · P2 · 磁吸拖动的每次鼠标移动可能重新缩放整张壁纸

**位置：** [FenceWidget.cpp:1925](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/FenceWidget.cpp:1925)；[FenceWidget.cpp:1254](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/FenceWidget.cpp:1254)；[DesktopCanvas.cpp:2573](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/DesktopCanvas.cpp:2573)。

**证据：** 源码确认。拖动/缩放进入 applyWallpaperMagnet，调用 renderedWallpaperImage，执行 toImage、整屏缩放及格式转换；HiDPI 下尤其值得关注。

**触发与影响：** 启用壁纸磁吸时，连续拖动把与壁纸尺寸相关的大工作放在高频 GUI 事件中，可能造成国产集显机器上的掉帧；未在当前桌面进行长时间压力测试。

**建议：** 缓存用于磁吸的低分辨率梯度图，随壁纸/屏幕变化更新；鼠标事件只查局部区域并合并刷新。

**验收：** 150/200% 缩放与高分辨率壁纸下持续拖动，记录 p95/p99 帧时间与图像重建次数。

### F19 · P2 · 文件 I/O 不可中断时，全局任务槽与退出排空没有期限

**位置：** [FileClipboard.cpp:251](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/FileClipboard.cpp:251)；[FileClipboard.cpp:463](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/FileClipboard.cpp:463)；[BackgroundTask.h:44](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/shared/async-work/BackgroundTask.h:44)。

**证据：** 源码链确认。cp/gio 使用无限等待的 QProcess::execute；取消仅检查项目边界；BackgroundTask::drain 等待全部线程，没有退出期限。

**触发与影响：** 挂起的网络文件系统或外部文件工具会占住唯一文件操作槽，并可阻塞正常退出。界面说明了取消后续项目，但没有解决当前项无限阻塞。

**建议：** 给外部文件操作加入进程级超时/取消与可恢复 staging，退出时有明确状态和期限；不要用强杀 QThread 代替安全的 I/O 生命周期。

**验收：** 临时 helper 长时间不返回、用户取消、应用退出；原文件不丢失，工作槽和退出流程能有界收尾。

### F20 · P2 · 系统监视器最后两行与操作按钮相互覆盖

**位置：** [SystemMonitor.cpp:995](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/SystemMonitor.cpp:995)；[SystemMonitor.cpp:1146](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/SystemMonitor.cpp:1146)。

**证据：** 源码几何计算及现有实机截图确认。清单有 10 行，末两行位于 y=432..472；按钮占 y=438..462，存在明确交叠。

**触发与影响：** AI 空间、原生备用桌面状态被“复制快照/查看详情”压住，文字及命中区域难以区分；新增模块后还会加重。

**建议：** 为标题、表体、页脚分别留布局空间；表体按可用高度滚动或自适应，不能把清单长度写死为旧的 8 行。

**验收：** 用 8/10/12 个模块、不同字体和缩放检查，任何数据行与按钮矩形都不相交。

### F21 · P2 · 极端拥挤时仍可能超出应用区，但全部访问通道已被关闭

**位置：** [DockLayoutMath.h:16](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-panel-liquid/src/DockLayoutMath.h:16)；[PanelDockCompat.cpp:164](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-panel-liquid/src/PanelDockCompat.cpp:164)。

**证据：** 纯计算复现：40 个图标、50px 应用区返回 icon=1、content=80、overflow=true；适配层仍将滚动范围设为 0 并拦截滚轮。

**触发与影响：** 窄分辨率、较短基础长度或极多窗口时，尾部任务可能被裁掉。持续缩小是用户明确要求，本项只指出超出整数像素容量后的未处理边界。

**建议：** 保持默认居中宽度、不增加省略号/滚动条；定义容量耗尽的可访问替代通道，例如键盘应用选择、现有开始菜单搜索或分组，并明确状态。

**验收：** 边界组合满足 count×最小单元格>可用宽度时，不静默失去任务可达性；不得擅自修改用户宽度。

### F22 · P2 · 统一设置的版本目录仍依赖开发源码，回退不完整

**位置：** [install.py:20](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/install.py:20)；[appconfig.py:4](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/appconfig.py:4)；[status.py:9](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/status.py:9)；[peony_adapter.py:7](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/peony_adapter.py:7)。

**证据：** 源码确认。runtime.json 记录 sourceRoot；状态和 Peony 适配从源码目录 import，发布目录只复制 UI 文件。

**触发与影响：** 移动源码、清理开发目录或安装到另一台机器会导致页面失败；只切换 current 不能回退被源码树提供的后端逻辑。

**建议：** 将运行时依赖纳入 release 或声明为带版本的软件包，清单记录全部依赖；安装后在不含源码树的环境验收。

**验收：** 临时移除源码访问权限的隔离安装、版本切换和旧版本回退，所有页面仍能读取。

### F23 · P2 · 已安装任务栏插件残留开发机 SDK 的 RUNPATH

**位置：** [install.py:35](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-panel-liquid/scripts/install.py:35)。

**证据：** readelf 只读确认：已安装 .so 的 RUNPATH 包含 /home/zhouzhang/应用/code-work/.../.local-deps/...；安装器直接复制 build 产物。

**触发与影响：** 发布物仍受开发目录和 SDK 布局影响，清理/迁移后加载结果无法保证。该证据不等于当前已经加载了错误的 Qt 库，也不是越权漏洞。

**建议：** 通过正式 install/staging 处理 RPATH，使用系统依赖或受控的相对运行时目录；对最终安装物而不是仅 build 产物做依赖检查。

**验收：** 无 SDK 目录的干净用户/机器运行；readelf/依赖解析结果不包含开发机绝对路径。

### F24 · P2 · 今日应用以 contains("lock") 排除程序，误伤正常应用

**位置：** [ActivityRecorder.cpp:143](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/ActivityRecorder.cpp:143)；[ActivityRecorder.cpp:189](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/src/ActivityRecorder.cpp:189)。

**证据：** 源码确认。前台计时和资源进程归属使用相同子串排除规则；clockify、blockbench 等标识包含 lock。

**触发与影响：** 正常应用的时长与 CPU/内存统计被遗漏，用户可能误以为记录器没有保存记录。

**建议：** 根据准确的锁屏服务/窗口类型或明确标识白名单排除，避免宽泛子串规则。

**验收：** 真实锁屏、桌面窗口、普通含 lock 的 WM_CLASS 分别验证，统计与排除行为一致。

### F25 · P2 · Fences CI 没有选择新增活动指标和监视器资源回归

**位置：** [fences.yml:27](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/.github/workflows/fences.yml:27)；[CMakeLists.txt:363](/home/zhouzhang/应用/Kylin-LQUI/code-work/ukui-liquid-desktop/apps/ukui-fences/CMakeLists.txt:363)。

**证据：** 构建元数据与工作流正则对照：68 个顶层注册用例中有 31 个未被这些正则选中，包含 6 个 activity_metrics/monitor_resources 缩放用例。部分 GPU 用例可合理单独运行，不能把 31 个一概算成缺陷。

**触发与影响：** 新增资源统计、删除/撤销、屏幕变化等行为可能在手工验证后没有持续门禁；旧 monitor_diagnosis 名仍在筛选表达式中。

**建议：** 用标签或显式套件管理 CI；新增测试必须纳入某条流水线，硬件相关测试单独标记；对零用例和意外漏选设置检查。

**验收：** 输出 CI 实际选择清单，与应覆盖清单比对；CPU 场景在最低支持 Qt/架构下运行，GPU 场景另有硬件记录。

## 4. 旧稿的复核、降级与未确认项

原有 [CODE_REVIEW_20261031.md](CODE_REVIEW_20261031.md) 保留未改。本报告是针对当前代码的复核结果，不能把旧稿全部条目直接当作修复任务。

| 原判断/线索 | 本轮结论 |
|---|---|
| 没有局部 XSetErrorHandler 就会使所有 Qt 程序 exit(1) | **未确认，不能照此定级。** 本机最小 QApplication 程序访问已销毁 X 窗口后得到失败状态并返回预设退出码 7，没有被 Xlib 强制终止；项目中 ActivityRecorder、DesktopCoverWatch 等也已有局部保护。仍可统一错误处理，但真实崩溃需要具体宿主/调用链证据。 |
| 同 UID 会话总线存在 Set/Activate 就等于高危 RCE | **权限边界判断过度。** 同 UID 通常已经可以修改自己的配置和启动程序；本轮未证明跨 UID、远程或沙箱逃逸。显式动作白名单、数据最小化、接口版本化值得做，不能凭导出方法直接判定越权。 |
| 模态 exec() 等于整个进程停止处理事件 | **不准确。** Qt 模态窗口有嵌套事件循环；应区分“其他窗口被模态限制”和真正阻塞 GUI 线程。本轮把同步进程等待列为 F13，没有把所有模态窗口都列为冻结缺陷。 |
| 旧截图里 panel 约 79% CPU 证明现在仍常态高占用 | **不能成立。** 截图属于之前布局循环修复阶段；当前代码还会在长度动画时复用材质。现有 0.33% 也只是短静置样本，活跃场景性能仍需新测量。 |
| Peony 数组序列化缺逗号会阻断当前统一设置 | **低层缺陷确实存在，但当前写路径绕开了它。** `_format_value(['one.so','two.so'])` 生成不合法 GVariant；现行 `peony_adapter.py` 通过 Gio.set_strv 写入，旧桥也拒绝直接写 disabled-extensions。列为兼容层清理项，不算当前主路径的 P1/P2 阻断。 |
| AI 空间完全没有内存约束 | **不准确。** `knowledge_jobs.py` 对工作进程设置 RLIMIT_AS，并检查内存压力和存储预算。单文档总墙钟/CPU 预算仍需设计，但应与已有保护一起评估。 |
| 每次重新插拔资料盘必然全部重新 OCR | **未实测，不作确定结论。** 签名包含 dev/inode/ctime 是保守变更检测；迁移可能带来重算，不应未经完整性测试就改成只看 size/mtime。 |
| 截图内容已经可以利用提示注入直接控制系统 | **未进行对抗实测。** 截图 AI 动作沿用当前线程权限是需要评估的能力边界，建议分析类动作采用最小工具能力；不能把潜在风险写成已经复现的攻击。 |
| 通知历史默认保存且同 UID 可读取就是权限漏洞 | **不足以认定。** 这是已有产品功能和同用户数据边界。应明确保存期、清空、锁屏预览策略，不把功能本身当成跨权限漏洞。 |
| 当前还有默认开启的悬停放大或自动延长 | **过时。** 最终默认两项均关闭，61% 居中空间保留；本轮建议不会擅自推翻这些约束。 |

需要继续跟踪但尚未定为确定缺陷：宿主 C++ ABI 升级兼容性、插件崩溃后的独立恢复、后台任务并发上限、超大 Office 文档的单文档预算、多显示器热插拔、GPU/驱动差异、通知独占服务切换。正式登录接管和 Codex 原生语音尚未完成属于已知待办，不冒充本轮新发现。

## 5. UI 设计评审

### 5.1 应保留的方向与用户约束

现有独立设置、两端圆球、集中状态区、统一图标画布和克制的玻璃风格已有一致性。后续建议继续保持：

- 默认固定居中宽度，拥挤时缩小应用图标；不默认打开延长、悬停强调、滚动或新溢出入口。
- 温度/带宽/日期组合保持完整，文字不跟着应用图标压缩。
- 网络、蓝牙、声音等使用原系统能力，不能为了外观放入无法生效的开关。
- 截图编辑后手动发送，翻译/识别/总结按已确认交互立即处理。

### 5.2 逐界面建议

| 界面 | 当前观察 | 建议与验收 |
|---|---|---|
| 统一设置 | 基础长度出现右侧 61 和下面单独的 61%，重复占一行；每个简单参数重复相同模块图标 | 数字、单位、控件放在同一行。将数值标签明确标为展示角色，避免设置桥重复导出；模块图标优先用于导航/分组。1040 与 720 逻辑宽度下检查行对齐。 |
| 设置禁用态 | 自动延长关闭后，上限滑条在截图中仍呈醒目的蓝色 | 交互禁用之外，还应有明确的禁用视觉和“开启后可调”说明。不要只依赖颜色，也不要直接隐藏已保存的值。 |
| 系统监视 | 已确认最后两行与按钮相交，详见 F20；缩小时数据很密 | 标题、表体、操作区独立布局；表体可滚动，数值右对齐，单位固定。把真实状态完整显示放在装饰前面。 |
| Dock | 固定宽度与不滚动的要求已实现，极端密度会形成很小的目标 | 保留默认规则，为超出可操作密度提供键盘任务选择、名称预览或现有应用搜索的替代路径；不要在未获确认时把省略号或滚动条加回来。蓝色运行标记也应评估与小图标的比例。 |
| 开始菜单 | 玻璃、左侧功能轨道、固定和最近区已经统一；名称较长时省略明显 | 保留完整名称的悬停/键盘焦点说明，明确焦点顺序；把“取消全部固定”做成有效状态。搜索结果应说明应用/文件/设置的类别。 |
| 控制中心 | 卡片整合方向清晰；后端失败统一变“不可用”，原因不足 | 区分关闭、硬件缺失、权限不足、读取失败、正在连接。Wi-Fi/蓝牙保留轻量展开箭头，主开关与详情入口使用清楚的点击区域。空状态要说明原因和下一步。 |
| AI 对话 | 宽表格内部滚动、前景色和截图草稿处理已改进；长审批卡不完整 | 长内容优先可读，审批必须能看全文。保存失败、连接中、等待授权、正在生成、已停止应是不同状态。后台停住时提供有效的重试/断开入口。 |
| 截图工具 | 紧凑工具条与直接 AI 功能符合需求 | 图标名称可通过悬停与键盘焦点获得；编辑工具、AI 动作、取消/完成分区清楚，破坏性动作使用一致颜色；小选区不得把工具条挤出屏幕。 |
| Fences 桌面 | 卡片和分区逐渐协调，壁纸仍有较高视觉纹理 | 阅读区域稳定对比度；拖动磁吸使用缓存，不以每帧全屏计算维持效果。离线文件保留可辨认状态，不能悄悄从分区消失。 |
| 输入法 | 预览和多种字体参数较完整 | 保存过程不能影响候选响应；把“预览中”“已保存”“部署中”“部署失败”明确区分，应用失败时说明哪些配置未提交。 |

这些 UI 意见依据现有实机截图和当前样式/布局代码，未对所有壁纸、显示器和输入设备做像素级验收。

### 5.3 建议形成共享的设计规则

| 类别 | 建议的统一约定 |
|---|---|
| 间距 | 使用同一套 4/8/12/16/24 逻辑像素间距；先统一行高、边距和对齐，避免逐个窗口凭感觉调整。 |
| 圆角 | 控件、卡片、窗口分层设定圆角 token；图标画布保留比例圆角，不把所有层都套同样的大圆角。 |
| 字体 | 统一正文、辅助说明、数值和标题级别；温度、网速、时钟使用稳定数字宽度，状态数据不因刷新改变布局。 |
| 材质 | 桌面浮层和长文本阅读面分别有透明度/压暗规则；保持共同的边缘、色彩和控件语言。 |
| 状态 | 正常、禁用、悬停、焦点、按下、忙碌、失败、未知分别有定义，不能全部依靠半透明或同一种蓝色。 |
| 动效 | 持续动画仅在有明确任务时运行；静置、隐藏、减少动态效果时停止。尺寸、绘制缩放和点击反馈分开管理。 |
| 错误 | 可恢复错误就近呈现并带重试；涉及保存与恢复时显示明确失败，不只写日志。 |
| 主题 | 图标主题、材质预设和全局明暗分别有作用范围；设定覆盖顺序及回退，避免“换图标主题”意外改变正文对比度。 |

普通正文可参考 4.5:1、大字 3:1 的对比度目标；玻璃要在合成后的真实背景上测量，不能仅比较配置里的两个色值。[W3C 对比度说明](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html)

密集按钮应有足够可点击区域及间隔，可参考至少 24×24 CSS 像素或足够间距的设计原则。这里把 Web 可访问性规范作为设计参考，不据此宣称 Qt 桌面已经通过该规范认证。[W3C 目标尺寸说明](https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum.html)

## 6. 架构与工程建议

### 6.1 优先建立的公共能力

1. **可靠存储**：统一原子保存、版本、备份、读取状态和错误上报。Qt 5 已有 QSaveFile，可直接用于完整文件的原子提交。[Qt QSaveFile](https://doc.qt.io/archives/qt-5.15/qsavefile.html)
2. **文件操作回执**：复制/移动/新建/回收站操作都记录对象身份和可恢复信息；撤销是数据操作，不能只反向执行路径删除。
3. **阶段状态机**：AI 连接、系统接管、输入法部署、安装更新都使用明确阶段、超时、取消、重试和失败后状态。
4. **稳定设置契约**：逐步由“扫描隐藏 QWidget 推断参数”过渡到显式字段 schema。保留现有桥作为兼容层，新增参数优先使用稳定 ID、类型、范围、保存语义和能力说明。
5. **发布清单与依赖**：每个 release 说明其二进制、脚本、共享库和配置 schema 来源；源树外置依赖必须有版本约束，回退要覆盖实际运行逻辑。
6. **任务调度与观测**：共享有界工作队列、取消令牌和执行预算；指标区分主进程/进程树、RSS/PSS/SwapPSS、静置与交互场景。

### 6.2 保留已有有效做法

- 文件复制已有 staging，回收站撤回有身份回执，继续扩展到其他撤销类型。
- AI 历史使用 QSaveFile 和用户权限限制；会话 guard 校验 peer UID/PID，并通过进程组回收后台。
- ACP 路径已有有界请求和取消兜底，可用于统一 Codex 路径的阶段模型。
- AI 空间使用只读查询、参数化 SQL、XML DTD/ENTITY 检查和工作进程资源限制。
- 当前 Dock 默认不因悬停改变布局，材质有缓存，已有多缩放与真实 OEM 隔离测试。
- 统一设置异步读取与代次检查，降低迟到响应覆盖新页面的概率。

### 6.3 维护性与版本管理

DesktopCanvas、StartMenu、SmartSpaceWidget 等大文件承载过多职责，建议按持久化、文件事务、布局、交互和进程接入拆分；避免先进行一次覆盖全库的大规模格式化，让行为修复难以审阅。

AI 小站、输入法和 Peony 当前缺少独立 Git 版本边界；主仓库又有大量未跟踪的运行文件。下一次正式交付前应形成可复现的版本快照、依赖锁定和安装物摘要。这里记录的是当前工作区状态，不把正常开发中的未提交修改当成运行时缺陷。

## 7. 建议的修复顺序与回归矩阵

| 阶段 | 范围 | 放行条件 |
|---|---|---|
| 第一批：数据与恢复 | F01、F02、F03、F05、F10、F11 | 故障注入不丢旧状态；修改后的用户文件可恢复；系统启动器仍认可 .desktop；桌面恢复不依赖日志可写。 |
| 第二批：权限与协议 | F04、F06、F07、F12、F13、F14 | 授权材料可信；审批完整；停止/超时能复位；配置提交与 UI 状态一致。 |
| 第三批：打包与持续验证 | F08、F22、F23、F25 | 干净环境可配置/安装/回退；最终产物不依赖开发树；CI 明确覆盖重要场景。 |
| 第四批：查询、性能与 UI | 其余 P2 及设计意见 | 特殊输入稳定；目录/高 DPI 压力场景有可比较的帧时间；数据行不重叠。 |

应补齐的回归维度：

- 存储：ENOSPC、只读目录、短写、损坏配置、部分提交、源目标相同、旧 schema。
- 文件：修改后的复制件撤销、离线介质、目录替换、PID/文件身份复用、进程退出中的操作。
- 通信：无响应、迟到响应、拒绝、取消、服务退出/重启、长任务、权限失败。
- 布局：窄屏、100/125/150/200% 缩放、长文本、10 个以上模块、极多应用、隐藏/恢复。
- 发布：无开发源码、无 SDK、干净用户、独立仓库、错误架构、升级中断、旧版回退。
- 性能：冷/热启动、静置、持续拖动、密集文件变动、打开设置、长对话流式追加、后台建库。记录 p50/p95/p99 耗时及 CPU/PSS 趋势，而非只报一个瞬时值。

## 8. 验证材料与边界

本轮材料目录：`/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/`。

| 材料 | 内容 |
|---|---|
| [baseline.json](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/baseline.json) | 文件指纹与范围 |
| [integrity-check.json](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/integrity-check.json) | 489 个已记录文件未变化 |
| [findings.json](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/findings.json) | 25 项结构化发现与验收建议 |
| [probe-results.json](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/probe-results.json) | .desktop、恢复故障、GVariant 与链接复制探针 |
| [state-probe-result.txt](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/state-probe-result.txt) | AI 停止状态和 Dock 容量边界 |
| [space-probe-results.json](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/space-probe-results.json) | FTS 特殊输入与 PDF/ZIP |
| [standalone-configure.log](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/standalone-configure.log) | AI 空间独立配置失败证据 |
| [x11-probe-result.txt](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/x11-probe-result.txt) | 未复现 Xlib 自动终止的记录 |
| [monitor-overlap.json](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/monitor-overlap.json) | 监视器末两行交叠计算 |
| [ci-selection.json](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/ci-selection.json) | CI 用例选择清单 |
| [python-parse.json](/home/zhouzhang/应用/Kylin-LQUI/maintenance/review-only-20261031-second-pass/python-parse.json) | 117 个 Python 文件语法解析 |

UI 证据：[当前设置](/home/zhouzhang/应用/Kylin-LQUI/maintenance/fluid-dock-20261031/settings-fixed-width.png)、[桌面与开始菜单](/home/zhouzhang/应用/Kylin-LQUI/maintenance/uniform-icons-20261031/menu-glass.png)、[固定宽度演示](/home/zhouzhang/应用/Kylin-LQUI/maintenance/fluid-dock-20261031/fixed-width-demo.png)。

本机 git、g++ 和现有 Qt SDK 可用；未发现可直接使用的 clang-tidy、cppcheck、ruff、mypy、shellcheck，本轮未安装或声称运行这些工具。本机 CTest 也不支持 `--test-dir` 和 `--no-tests`，后续脚本需针对最低支持版本选择兼容命令。

以上修复和设计均为建议，**本轮没有实施修复**。
