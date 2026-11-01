# 2026-11-01 双评审修复记录

本轮基于统一仓库 `c511b99`，逐项处理 DOT 的 **54 项确认缺陷（6 P1、41 P2、7 P3）**，并合入 Argon 的相关稳定性与公共库建议。以下是源码修复交付；测试通过的范围与仍需实机验收的范围分别记录，不能据此宣称所有设备、GPU 和真实模型后端已验收。

**最新状态：源码验证后，已于 20:22 按用户要求部署并重启用户层框架；见 [部署与重启记录](LIVE_ROLLOUT_20261101.md)。** 构建位于 `build-review/{panel,fences,launcher,liquid-cli,ai-space,fcitx}`，未将构建产物、个人数据或 SDK 纳入 Git。旧源码树保留历史用途，没有双向同步。

## 核心变化

- 权限修改在忙时明确拒绝，审批保留完整内容并绑定连接代次；恢复错误不再自动改用空上下文，损坏聊天/活动文件受到写保护。
- HTTP Provider 不再凭返回路径读取本地正文；canonical 排除覆盖别名目录；导出改成有预算的后台原子发布。
- 输入法配置所有写入/恢复入口共用锁，持久保存原件；三个用户安装器与硬件策略安装器统一处理各入口的失败回滚。
- 控制中心按后端域并发操作、合并滑条末次值；保存回执不覆盖新草稿；Wi-Fi 使用原始身份及兼容配置。
- 桌面撤销校验对象身份，链接按链接本身处理；菜单回调绑定对象生命周期；去除桌面同步中的事件循环重入并缓存排序元数据。
- `NextKdeGlassView` 移到 `shared/liquid-glass`，`LiquidIconRenderer` 移到 `shared/icon-style`；输入法与 AI 空间默认使用规范公共实现。旧头文件保留为转发入口，旧实现不再作为默认链接目标。

## 逐项追踪

“完成”指代码修复与下列验证范围；真实设备和协议联调仍受文末限制。

| 编号 | 级别 | 修复 |
|---|---|---|
| CLI-01 | P1 | 忙时拒绝更改后台、目录、命令和权限；设置表单在写盘前拒绝，空闲修改会断开后恢复线程。 |
| CLI-02 | P1 | ACP 完整保留对象、数组、字符串原始参数，审批详情可滚动和复制。 |
| CLI-03 | P1 | 审批使用连接代次与唯一令牌；验证合法决定，断线/轮次结束关闭全部旧卡；ACP 同样绑定代次。 |
| CLI-04 | P2 | 恢复失败保留原会话 ID 并停止发送；新上下文必须由设置中的明确新建操作触发。 |
| CLI-05 | P2 | 两种客户端均限制 64 MiB 协议缓冲；stderr 和错误通知同时限制条数与每条长度；迟到 turn/start 回执按轮次代次丢弃。 |
| CLI-06 | P3 | 替换不存在的 build.sh，列出可执行的 CMake/CTest 命令与实际登录常驻设置。 |
| CLI-N01 | P2 | 现有聊天读取/解析失败进入写保护；拒绝发送与覆盖；明确新建时先保存唯一恢复副本，备份失败则拒绝清空。 |
| CLI-N03 | P2 | 安装入口、图标、desktop、自启动和 current 统一记录并回滚；提交记录写失败同样回滚，release 名增加唯一后缀。 |
| SPACE-01 | P1 | Provider 只使用返回正文，不凭路径读取本地文件；来源跨 JSON/二进制清单保留。合并和建库都执行 canonical 根/排除检查，显式排除还撤销已有库内副本。 |
| SPACE-02 | P3 | OCR 语言/依赖错误保留文件元数据并显示 unavailable 与原因，不能作为“无文字且已完成”。 |
| SPACE-FULL-01 | P1 | 资料根、排除路径和候选路径统一 canonical 语义，支持符号链接别名及尚不存在的子路径。 |
| SPACE-FULL-02 | P2 | 片段满足中文覆盖条件后才把文档加入已接受集合，保留原候选数与时间预算。 |
| SPACE-CPP-01 | P2 | 设置页只使用已加载条目数和文件修改时间，不再读取、解析完整 index.json。 |
| SPACE-CPP-02 | P2 | 导出拒绝源内/别名目标与符号链接，设置文件数/字节预算；后台暂存、失败清理、原子替换、取消与旧目录恢复。 |
| SPACE-CPP-03 | P2 | 目录包含函数处理根目录与 canonical 别名；保存无关设置不再静默删除原排除规则。 |
| FENCE-01 | P2 | 区分初次缺文件与已有文件读失败；读失败禁写，非法数据先成功备份才允许替换。 |
| FENCE-02 | P2 | 创建、改名、复制、拖入统一记录 lstat 的设备/inode 身份；拒绝撤销替代对象，失败保留原撤销记录。 |
| FENCE-03 | P2 | 使用链接本身的存在/身份语义；悬空链接仍进入桌面数据模型并可被撤销移至回收站。 |
| FENCE-04 | P2 | 静态图片验证/预览改为可合并后台任务，预览独立限制在 1280×720；旧结果不能覆盖新选择，未验证图片不能应用。 |
| FENCE-05 | P3 | 控件销毁时同步移除桥接 ID；AI 隐藏表单增加离页/关闭 Discard 生命周期。 |
| FENCE-06 | P2 | CPU 按精确值降序，再按 RSS 排序，去掉会形成比较环的两两差值阈值。 |
| FENCE-07 | P2 | 两类测试均按实际 failures 返回；修复由此暴露的旧组件序号/数量断言，刷新动画测试使用确定的动画进度验证像素。 |
| FENCE-08 | P2 | 右键动作连接绑定 DesktopIcon 上下文；图标销毁会关闭仍打开的菜单。 |
| SHARED-01 | P2 | 生产端按真实 UTF-8 JSON 字节预算校验，与接收端共用 4 MiB 常量，接收也有界读取。 |
| PF-01 | P2 | 保存、恢复、两个 Rime 写入口、启动同步及默认配置创建共用同一把可重入锁。 |
| PF-02 | P2 | 事务开始前把原始配置写到持久备份目录，不再仅保留临时 staging 里的副本。 |
| PF-03 | P2 | 格式保留替换必须对应准确 patch 键，转换后再校验语义树，否则安全序列化。 |
| PF-04 | P2 | 移除菜单构建时记录完整文件 URI 和用户选择的日志。 |
| PF-05 | P2 | 改写自启动前要求项目 marker；拒绝未托管/损坏/链接条目。 |
| PF-06 | P2 | 拒绝未托管启动器，入口与 current、自启动统一事务回滚；失败首装不保留激活链接。 |
| PF-N01 | P3 | 本地开关不再把宿主 unknown/disabled 覆盖成 enabled。 |
| PF-N02 | P2 | 暴露 packagedPayloadReady/helperReady/installable；界面安装按钮依据系统载荷，不依赖开发 build。 |
| PF-N03 | P2 | Peony -q 非零立即报失败并禁止拉起新实例；安装/卸载反馈分别表示插件结果和重启结果。 |
| INT-01 | P1 | 启动异常后仍运行恢复主循环；仅确认原生桌面恢复后结束守护；恢复状态与重试计时器互斥。 |
| INT-02 | P2 | 设备规则通过持久 enable 链接门禁启动/应用；disable 后不能被 add 规则重新启动。设备已消失时 stop 仍清掉激活标记。 |
| INT-03 | P2 | 同目录原子写与全文件回滚，安装写入与策略/试验共享锁；服务失败恢复原启用/运行状态，故障后可重试。 |
| INT-04 | P2 | 按 systemd 参数语法引用脚本路径，并处理反斜线、引号、百分号和变量符号。 |
| INT-05 | P3 | 说明命令实际执行于 code-work/ukui-liquid-desktop 组件目录。 |
| INT-06 | P2 | 所有策略事件推进代次；试验开始/清理持短锁，清理同时验证代次和频率，不能覆盖同值的新策略。 |
| SHELL-01 | P2 | 保存回执验证页面代次、编辑器对象及提交文本；新草稿和重建的编辑器均不会被旧回执覆盖。 |
| SHELL-02 | P2 | 按目录优先级先占 desktop-file ID，再解析 Hidden/NoDisplay；保留 ID 大小写与子目录语义。 |
| SHELL-03 | P2 | 按原始 SSID、安全类型、设备、接口与 BSSID 选择兼容配置，保留多个候选。 |
| SHELL-04 | P2 | 通知写入前按 UTF-8 总字节淘汰旧记录，读写上限一致；兼容有界迁移先前较大的合法历史。 |
| SHELL-N01 | P2 | 设置安装将入口、desktop、alias 与 current 一起备份/回滚，首装失败删除新增入口。 |
| SHELL-N02 | P2 | 原始 SSID hex 构成身份，不按有损显示文本去重；非 UTF-8 字节用转义形式区分。 |
| SHELL-N03 | P3 | 重建首页会重放同一封面 URL，避免数据缓存命中却遗漏新控件初始化。 |
| SHELL-N04 | P3 | 模块测试默认重启到独立 Xvfb/D-Bus，隔离 HOME/XDG/runtime，调用前核验服务 owner PID。 |
| SHELL-N05 | P2 | 关闭旧开始按钮菜单前保存 QPointer 并清空成员，避免同步 aboutToHide 清零后再解引用。 |
| SHELL-N06 | P2 | 垂直栏开始按钮偏移按栏宽计算，水平栏按栏高计算。 |
| SHELL-N07 | P2 | 卸载目录只接受可验证的 canonical desktop 归属，不再按用户快捷方式同名推断。 |
| SHELL-N08 | P2 | 侧栏菜单保存条目身份与副本，返回后按路径重新定位，失效目标不执行动作。 |
| PANEL-N01 | P2 | 恢复脚本只终止同用户、同 DISPLAY、真实加载液态插件的面板。 |
| PANEL-N02 | P2 | 用必需 objectName 替代控件总数；初始化默认值和恢复推荐预设分别验证，预设对齐共享契约。 |
| CONFIG-01 | P2 | Fences 明确采用 NativeFormat，先一次性导入 .ini 并保留较新的 .conf；Panel 显式读取/监听同一契约；测试使用生产迁移入口。 |

## 验证证据

最终验证在本机 ARM64、Qt 5.12.8 环境进行；Python 界面/新增回归使用 `/usr/bin/python3`（3.8），AI 空间 CTest 使用 CMake 选定的 Python 解释器。每个 CTest 数量为注册项，不与内部断言或其他脚本相加。

| 验证 | 最终结果 | 本机日志 |
|---|---|---|
| Panel 全套 | 75 注册项，74 通过，1 GPU 项跳过，0 失败 | `lqui-review-panel-final.log` |
| Launcher 全套 | 19 注册项，18 通过，1 GPU 项跳过，0 失败 | `lqui-review-launcher-final.log` |
| Fences 全套 | 108 注册项，106 通过，2 GPU 项跳过，0 失败 | `lqui-review-fences-verified.log` |
| AI 空间全套 | 42/42 通过 | `lqui-review-space-verified.log` |
| AI 小站 | 100% / 150% / 200% 三套 Qt UI 回归通过 | `lqui-review-cli-verified.log` |
| 输入法 | candidate / diffusion 2/2；配置 15 项通过 | `lqui-review-fcitx-verified.log`、`lqui-review-input-tests.log` |
| 统一设置/控制中心 | 14 / 22 项通过；玻璃后端 7 项、原生桥接 3 项通过 | `lqui-review-settings-tests.log`、`lqui-review-control-tests.log`、`lqui-review-glass-tests.log`、`lqui-review-native-tests.log` |
| 五模块联调 | 独立 Xvfb、D-Bus、HOME/XDG/runtime；116 次标量读写、32 个页面渲染，两种窗口宽度通过 | `lqui-review-modules.log` |
| AI 空间新增边界 | 7 项通过，包含真实 loopback HTTP Provider、JSON/二进制清单、符号链接根排除、真实 SQLite FTS 和 OCR 缺语言 | `lqui-review-space-tests.log` |
| 安装与恢复 | 安装故障套件 7 项；恢复循环 4 项；硬件策略普通文件模拟 6 项；跨 DISPLAY 恢复 1 项 | `lqui-review-install-tests.log`、`lqui-review-recovery-tests.log`、`lqui-review-policy-tests.log`、`lqui-review-restore-tests.log` |
| Peony | 6 项单元回归；真实 QtWebChannel 表单保存通过 | `lqui-review-peony-tests.log`、`lqui-review-peony-gui.log` |
| 仓库 | workspace 检查与 diff 空白检查通过 | `lqui-review-workspace.log` |

完整证据保存在仓库上一级 `maintenance/review-repairs-20261101/`，不上传公共仓库。早期失败和修复后的日志均保留；表中只引用最终对应轮次，没有把旧失败吞成成功。Fences 修复测试返回值后暴露出的旧目录顺序断言、首启活动保存问题和刷新采样时序，均在最终完整 108 项重跑前修正。

撤销新增测试在真实隔离 GUI 中覆盖创建、改名、复制、移动后目标被外部替换；替代对象保持不动，原对象放回后仍可继续撤销。悬空链接测试通过实际 Gio 回收站处理链接自身。

## 需要明确的边界

1. 用户层部署与重启已完成，版本和配置核对见 [部署记录](LIVE_ROLLOUT_20261101.md)。未切换真实 Wi-Fi、蓝牙、电源、注销或休眠，也未写入真实 sysfs，也未改动真实聊天、知识库和 stock-code。
2. GPU 测试因本机测试上下文不可用而跳过；CPU fallback 与材质等价测试通过。不能据此承诺 GPU、多个 OEM ABI、长期 RSS 或所有多屏/分数缩放组合均已验收。
3. Codex/Qwen/DSH 的权限与审批使用协议 fixture 验证；真实模型后端的沙箱执行、审批交错及宿主崩溃仍需联调。64 MiB 超限帧、8K/100MP 预览和超过 64 MiB 设置快照的专项压力测试没有执行。
4. 安装受控异常会回滚；突然中断时保留 journal，下一次安装会拒绝盲目覆盖并给出恢复记录位置。核对后可使用 `tools/file_transaction.py` 的显式恢复入口恢复，再重试。未做断电、磁盘满、外部并发改写的完整矩阵。
5. 设备规则保持与服务 `WantedBy=multi-user.target` 的 enable 链接契约；测试没有运行真实 udev 或改写硬件频率。stop 在设备已消失时也撤销激活标记；同值策略事件会让旧试验失去恢复权限。
6. `CONFIG-01` 修复包含旧配置迁移：OEM 主题确实会将 Qt 默认改成 IniFormat，不能只把 Panel 改读 `.conf`。迁移保留旧 `.ini` 原件，已有 `.conf` 值优先，成功后 Fences 才统一采用 NativeFormat。

## Argon 补充意见的核对

- **构建产物入库**：当前 `git ls-files` 没有跟踪 build、releases 或安装锁，目录存在不代表入库；没有误删本机构建/安装记录。
- **ResidentStartup 反斜线**：原转义在 Gio 实际启动无害 fixture 时，通过空格、引号、反斜线、美元符号和反引号路径；保留两层转义。百分号路径尚有兼容边界，未据静态结论直接删除必要转义。
- **隐藏设置草稿/确认框**：新增明确的 Discard，离页/关闭或外部 Reload 会释放隐藏草稿；隐藏父窗口确认前先显示并激活父窗。仅可见设置窗影响 AI 收起。
- **控制中心**：独立忙域、末值合并、材质显式释放、空渲染失败返回、重采样前隐藏，以及蓝牙/媒体/缺失 GSettings schema 降级已处理。
- **公共库分叉**：生产默认构建已统一，两个被跨应用引用的渲染类已下沉。
- **继续保留的工作**：旧树仍作历史参考；X11 全局错误处理和连接策略、首键截图成本、复杂轮廓性能、搜索重复打分及整套 UI 精修需分别测量后推进。没有仅凭估算宣称性能降低 90%，也没有擅自改动用户确认过的布局与材质。
