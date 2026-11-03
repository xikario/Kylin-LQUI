# 紧凑设置中心实施与验收（2026-11-03）

> 2026-11-04：所有者已另行授权二轮评审及推送。本文“未推送”描述上一阶段的本地交付；本轮修复与复核见 [二轮评审记录](COMPACT_SETTINGS_REVIEW_20261104.md)。安装和部署仍未执行。

## 范围与基线

按任务书 v1.0 及 A 兄审核实施独立 `SettingsWindow`。实际起点为 `3fac516`，在本地分支 `work/compact-settings-20261103` 工作，保留原有 `658c52f`、`3fac516` 两项音频/托盘修复。任务书冻结点 `bffa9e1` 之后的已有变更未回退。

本次未执行安装器、部署、推送或 PR，未重启真实 KWin、Fcitx、Peony 或用户组件。原控制中心 `control_center.py`、Dock 材质和 AI 空间 vendored Bridge 未修改。截图、日志和原始协议快照仅保存在工作区旁的私有目录：

`/home/zhouzhang/应用/Kylin-LQUI/maintenance/compact-settings-20261103/`

## 实现

- 连续 48px 标题栏，浅色不透明表面，分组圆角 10、输入 6，保留原窗口按钮名称、拖动、双击、最大化及尺寸手柄。默认侧栏 208、窄屏 184；大字体按度量增长，导航名称可换行。
- `ui_tokens.py` 生成实际像素字号，应用字体变化会重新生成样式；`ui_components.py` 提供原生设置行、开关、单次提交滑条、动作流、说明折叠、插件行、文件行及有界 PNG 预览。内容宽度随 viewport 变化，默认 Peony 单列。
- `ui_schema.py` 校验可选元数据。无效版本或分组使整页退回旧协议；单字段错误局部回退，说明仍可见，未知类型显示提示。诊断只含固定代码与索引。
- C++ 仅在根控件显式设置整数 `settingsUiVersion=1` 时按布局遍历并导出语义，当前只迁移 Fences 外观、壁纸、活动三页。其它页面保留原遍历顺序。表单、buddy、嵌套布局、网格和隐藏标签页均有覆盖。
- 壁纸来自现有 QLabel 的真实图片/视频静态帧，不抓整页，不新增视频循环。PNG 最多 2 MiB，最多 1360×800 物理像素；不放大小图，按 pixmap cacheKey 缓存，服务端最多 4 项、8 MiB 计费上限，客户端解码前再次检查格式、字节和像素尺寸。
- 壁纸保持原草稿/应用回调；主题按钮仍立即执行，分组与页头分别说明。路径可复制、中间省略；原副本、恢复、失败状态和视频警告保留。只有指定文件/信息对话框通过 RAII `SettingsScope` 临时使用紧凑表面，取消后恢复 owner 属性，不改全局 QStyle。
- 保留 generation/revision/request_serial、提交控件实例及提交文字检查。稳定状态键为模块、页面、唯一 name、entity；恢复编辑、UTF-16 光标/选择、列表、滚动锚点、搜索和展开状态。无名或重名字段的本地草稿需确认放弃；AI 原有 Discard 语义保留。secret 不保存到状态、缓存、搜索值或诊断。
- 相同轮询内容不再修改控件或重排分组。快速请求避免中间空表单闪烁；旧表单立即禁用，慢请求 80ms 后显示读取提示，迟到响应仍受原路由保护。无新增后台服务或持续动画。
- 安装器仅扩展 `FILES` 白名单，加入三个 UI 模块；原音频等 17 项全部保留。只在临时 staging 中复制和导入，未调用安装函数。

## 协议与五页字段映射

仍使用原 `Catalog/Page/Set/Activate/Discard`，写入只使用当前响应中的 runtime id。新增的 `page.ui` / `field.ui` 都是可选展示元数据，原扁平 `fields` 保留。`visibleWhen` 只允许唯一 name 和标量等值判断，不执行表达式。

| 页面/原字段 | 新展示与协议保留方式 |
| --- | --- |
| 外观 `pageHeading`、`cardHeading` | 吸收到页标题、分组标题；原 fields 仍在 |
| `fenceLiquidGlass`、原无名开关说明 | 同一行开关及说明；说明补名 `fenceGlassHelp`，关联原开关 |
| `glassScrim`、原无名通透说明 | 标签/下拉及说明；补名 `glassScrimHelp`，原索引和回调不变 |
| `reduceMotion`、原无名说明 | 动态效果组；补名 `reduceMotionHelp` |
| `refreshMaterialButton` | 独立次级动作，原 Activate 不变 |
| 壁纸两个 `cardHeading`、`pageHeading` | 页头、壁纸组与主题组标题，未当作设置删除 |
| 原“预览/图片/类型/视频/显示方式”五个重复 form-label note | 并入对应控件标签，不重复成行 |
| 原图片路径 note | 补名 `wallpaperImagePath`，只读来源；新增 `wallpaperImageStatus` 分离真实缺失/加载状态 |
| 原 previewArea | 新 `wallpaperPreview` 字段，真实静态 PNG；无图时给出缺失状态 |
| 原无名选择图片、清空按钮 | 补名 `wallpaperChooseImage`、`wallpaperClearImage`，原 callbacks 不变 |
| `wallpaperKind`、`wallpaperImageMode` | 原选项、索引、草稿语义不变；条件显示对应行 |
| `videoWallpaperPath`、`videoWallpaperStart`、`videoWallpaperStatus` | 可编辑真实路径、原选择回调和状态；不自动应用 |
| `videoWallpaperExperimentalNotice` | 视频时直接可见的警告；静态回退来源默认折叠 |
| 原一段壁纸范围/副本说明 | `wallpaperScopeNotice` 常显，`wallpaperCopyDetails` 可展开；两段含义均保留 |
| `wallpaperApply` | 唯一壁纸主动作，原 dirty/applying/callback 保留 |
| `wallpaperThemeButton`、原无名配色下拉与三个主题动作 | 补名 `themePreset`、`themeApplyPreset`、`themeApplyExternal`、`openSystemWallpaper`；即时执行的原回调不变 |
| 活动 `pageHeading`、两个 `cardHeading` | 页头及“应用记录/资源采样”标题 |
| `activityRecordingEnabled`、原无名说明 | 原开关与说明；说明补名 `activityRecordingHelp` |
| `activityMetricsEnabled` | 原 CPU、内存、峰值语义完整保留 |
| `activityMetricsInterval`、原“采样间隔”与“10秒” note | 单一标签与读数；5..60/step=5 不变，源 label buddy 与 sliderValue 消重 |
| `activityLogRetention` | 标签“详细日志保留”，选项缩为天数；itemData/索引/回调不变 |
| 两段统计和存储说明 | `activityMetricsDetails`、`activityStorageDetails`，合在同一可展开详情 |
| `activityHistoryDetails` | 原历史/指标/导出动作完整保留 |
| Peony general 六个 `global:*` 开关 | 原 `enable-peony-right-menu`、`enable-desktop-file-menu`、`enable-desktop-background-menu`、`show-open-terminal`、`show-file-extension`、`show-hidden-file` 六项原样保存；分菜单/终端与文件显示两组 |
| Peony plugins `open-manager`、原说明、原四个安装/卸载动作 | 原 ID、确认与启动回调保留；入口和维护组分开 |
| 每个插件原 heading/description/plugin/unknown 字段 | 按稳定文件名 entity 合为一行，补独立 state/path note；已知状态才显示开关，未知/未安装不伪造开关 |

真实静态协议快照计数：外观 9→9、壁纸 23→21（五个重复标签减少，增加真实预览/独立状态/副本段）、活动 13→11（重复标签/读数消重）、文件管理 6→6、插件 69→111（21 个 entity 各补状态和路径，不增加 42 个操作）。合成 fixture 的数量与真实协议不同，截图不会展示真实用户配置。完整不含值的对照保存在私有 `field-mapping-sanitized.json`。

Peony terminal、AI/AI 空间、输入法、任务栏及 Fences 其它动态页使用同一通用渲染器；原字段没有按截图范围裁减。

## 验证结果

| 检查 | 结果与证据 |
| --- | --- |
| 原 UI 14 项 + 新 UI 33 项 + schema 9 项 | 56 项通过，原 `test_ui.py` 断言保留；`final-ui-tests.log` |
| Peony adapter | 4 项通过；`peony-tests.log` |
| 安装白名单 staging | 1 项通过，隔离导入新模块和控制中心，未执行安装器；`packaging-tests.log` |
| C++ canonical Bridge | 私有 `build-bridge` 的 CTest 通过，含旧协议、布局/buddy、回调、秘密字段、预览缓存/无图、白名单、网格/隐藏标签页 |
| 六个 Bridge 消费者重建 | Fences、Panel、liquid-cli、fcitx、launcher、ai-space 全部成功；各 `*-build.log`。AI 空间使用 canonical shared/settings，vendor 未同步 |
| 弹窗/图片选择器回归 | 16 个 CTest 通过，覆盖 1/1.25/1.5/2、UKUI/禁用路径、原生键盘、PNG/取消、SettingsScope 恢复；`popup-tests.log` |
| 原生模块私有会话 | 最新 115 个可用 scalar 无变化往返、32 个页面/动态视图通过，1040/720 下同时检查折叠和展开的实际可见控件；`after-modules.log` |
| 真实 OEM Panel 私有宿主 | 3 页，21 个 scalar 往返、6 个视图通过；`panel-native/settings-protocol.json`。配置 bind mount，禁真实系统/音频总线 |
| 新客户端真实映射 | 私有 Xvfb + KWin，DPR 1 与 1.5 均通过拖动、最大化/还原、Space 开关、下拉键盘、tooltip、真实弹层 Escape 取消无 Activate；`native-client-{100,150}/result.json` |
| 截图与人工查看 | 五页 × 1040×740、720×480、1040×740@1.5、720×480 字体1.25；另视频/失败/草稿、展开说明、长/未知插件、无结果、键盘焦点、未知类型。实际打开 PNG 逐张检查；`screens/*` |
| 截图入口安全 | 参数在 QApplication 前校验，NaN 字号拒绝；断言无 Set/Activate。字体1.25生成实际18px正文，DPR1.5仍为逻辑14px |
| 文本/竞态覆盖 | 迟到和乱序响应、提交期间继续编辑、控件重建、快速切页、打开下拉时轮询、滚动/列表/表情选择恢复、旧无名/重名草稿取消、secret 排除、单次滑条提交、禁用/隐藏取消、安装失败/卸载取消、空插件列表均通过 |

早期几何检查把 Qt 不参与布局的隐藏折叠内容也当作可见控件。联调现分别展开/折叠后保留原越界断言，检查范围更符合实际操作。私有 Xvfb 同时启动曾冲突，最终原生检查按会话依次执行。一次模块外层超时后加入阶段进度和超时堆栈记录，最新隔离运行退出码为 0；不把日志存在当作通过。

## 性能与边界

`benchmark.py` 位于私有证据目录。使用 `3fac516` 的独立源码副本（含原 SVG）与当前客户端，重放相同合成数据：每页 6 次、共 30 次切页，无固定等待计入切换耗时；独立进程首屏另计。默认 Page 轮询保留，静态壁纸页 20 秒采样，先稳定 3 秒再计数，并排除 tooltip 的正常绘制。未执行任何 Set/Activate。

| 项目 | 旧版 | 本次 |
| --- | --- | --- |
| 独立进程到首屏布局（ms） | 242.46 | 212.83 |
| 外观 重建中位数（ms） | 33.89 | 60.51 |
| 壁纸 重建中位数（ms） | 46.0 | 77.04 |
| 活动 重建中位数（ms） | 41.8 | 69.89 |
| 文件管理 重建中位数（ms） | 24.23 | 47.28 |
| 插件 重建中位数（ms） | 49.72 | 76.24 |
| 空闲单核 CPU（%） | 0.373 | 0.272 |
| 空闲结束 PSS（MiB） | 30.28 | 31.28 |
| 20s Paint 事件 | 72 | 0 |
| 本次 20s PSS 增量（KiB） | 2362 | 9 |

本次预览解码 1 次、写操作 0 次，20 秒内执行原有 Page 轮询 8 次。上述数值是同机单组对照，受桌面其它负载和 PSS 共享页变化影响，不能推广为精密基准。

性能数值以同目录 `benchmark-before.json` / `benchmark-after.json` 为准。页面更完整的分组/无障碍结构有重建成本，不能宣称比旧表单快；轮询不变时跳过更新，解码计数保持 1，不新增材质或视频循环。缓存有明确容量，隐藏组件不创建预览渲染任务。

这是合成数据、offscreen 客户端采样，不等同于真实模块冷启动，也不代表全桌面进程树或 GPU 基准。新窗口真实映射和键盘检查另有 X11 证据。未验证跨屏热切 DPI、真实用户配置部署后的长期运行和 GPU 驱动路径；这些不在本次本地实施中冒充通过。

## 可复制入口

从仓库根设置 `S=code-work/ukui-liquid-desktop/apps/kylin-lqui-settings`，使用系统 `/usr/bin/python3`（带 PyQt5）。示例：

```bash
QT_QPA_PLATFORM=offscreen /usr/bin/python3 -m unittest discover -s "$S/tests" -p 'test_ui*.py'
QT_QPA_PLATFORM=offscreen /usr/bin/python3 "$S/tests/test_packaging.py"
QT_QPA_PLATFORM=offscreen /usr/bin/python3 "$S/tests/test_peony.py"
LQUI_TEST_BUILD_ROOT="$PWD/build-review" /usr/bin/python3 "$S/tests/test_modules.py"
QT_SCALE_FACTOR=1.5 /usr/bin/python3 "$S/tests/check_native_settings.py" --out /tmp/lqui-native-check
QT_QPA_PLATFORM=offscreen /usr/bin/python3 "$S/tests/capture_settings_pages.py" --out /tmp/lqui-settings-preview --extras
```

模块测试必须走正常隔离入口，不手工传 `--isolated`。原生检查脚本同样自行创建独立 Xvfb/会话/配置。此机 CTest 3.16 不支持 `--test-dir`，在对应构建目录运行 `ctest --output-on-failure`，以实际非零测试数量和退出码判定。

## 本地交付

`0fa6822`：可选源端语义、真实预览、Peony 元数据及局部弹层。

`dff9094`：紧凑客户端、状态保护、合成 fixture、截图与 Python 回归。

后续验收提交包含原生宿主/客户端检查和本记录。只保留本地提交供复核，未推送或安装。

最后核对清理了早期测试退出后留下的 12 条临时 gsettings monitor；模块 fixture 现在为每个宿主创建专用进程组，并在 finally 中清理整组。重跑仍通过 115 次往返、32 个视图，扫描已移除临时配置目录的进程残留为 0。真实用户会话进程未纳入清理。
