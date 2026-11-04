# RIME液态输入法

### 紧凑注音与高 DPI（2026-10-19）

拼音/候选文字按实际字形的墨迹范围确定行高和基线，`PinyinGap=0` 不再叠加字体
行框上下空白。声调、下伸字母与候选序号均纳入边界，自动行高保留少量外部留白。
字体度量使用候选窗实际绘制设备的 DPI，像素尺寸不再重复乘桌面缩放比例。
不改变字号、玻璃特效或用户保存的间距值。

### 调整注音高度与间距（2026-10-19）

托盘右键 → RIME液态输入法设置：

- “字体”：分别调整候选拼音字号、候选文字字号。
- “尺寸与间距”：新增“拼音与汉字间距”（0–32，默认 1），调节两行之间的距离；
  “候选整体行高”控制两行合起来的高度，0 为自动。
- 顶部输入栏高度、输入栏与候选栏间距独立控制，并使用明确名称避免与候选注音混淆。

更改立即预览，点“应用”更新实际候选窗。较小行高会自动容纳字体、声调及行间距，
避免裁字。配置键为 `Geometry/PinyinGap`；旧配置未包含此键时保持原来的 1 像素。

### 候选刷新崩溃修复（2026-10-19）

D-Bus 在布局后、重绘前可能更新候选词，原先逐字矩形与新的短词不一致会越界，
导致液态前端退出、Fcitx 回退到 Classic UI，拼音重新显示在词后括号中。
现在每帧保存候选、拼音、序号与显示开关的同一份快照；关闭输入时同时释放布局
和快照。绘制还检查字符列表边界。新增长词→短词、拼音开关与待绘制帧的回归，
真实 Rime 测试加入连续输入/退格及实际候选窗截图。
修改前版本：`backups/20261019-pinyin-frame-before/`。

### 候选词拼音（2026-10-19）

“显示顶部输入栏”与“显示候选词拼音”分开控制。拼音逐字居中显示在汉字上方，
如上行“shì pín”、下行“视 频”；序号对齐汉字。行高自动容纳声调；横排、竖排、
换行及未注音词的汉字基线一致。可在字体页调整候选拼音字号，0 为自动缩小。
预览、点击选择和数字选词保持同一候选索引。

雾凇原词典不带声调，前端用 `assets/pinyin/` 内的 MIT 单字/词组数据补齐，
优先词组、多音字遵循 Rime 给出的无调读音。字典启动时读取一次，不联网、
不启动额外进程、不保存用户输入；没有匹配条目时保留原读音，轻声不加调号。
来源、固定提交与许可证见 `assets/pinyin/README.md`。不改动候选排序。
修改前版本：`backups/20261019-pinyin-above-tones-before/`。

旧版兼容补丁曾清空所有候选注释，现在部署工具会同步拼音显示设置，只改
`translator/always_show_comments` 与 `translator/comment_format`，保留旧 librime
的无 Lua 兼容处理器和过滤器。修改前备份，原子替换，不编辑用户词库。
Fcitx4 Kimpanel 把词与注释拼接到同一个文本并附加空格；前端拆出拼音后用单独
字号绘制，关闭时不残留括号。拼音开关变更保存后会重新部署 Rime。

新增真实雾凇验证入口（私有 D-Bus/Xvfb、临时配置目录）：

```sh
dbus-run-session -- xvfb-run -a -s '-screen 0 1280x800x24' \
  env FCITX_LIQUID_ISOLATED_TEST=1 /usr/bin/python3 tests/real_pinyin_test.py \
  --source /path/to/public/rime-ice
```

`--cache /path/to/rime/build` 可复用同版本词典编译缓存，不复制用户数据库。
前端解析和独立开关由 Qt 回归覆盖，YAML 回归验证原兼容数组及无关设置不变。
修改前版本：`backups/20261019-candidate-pinyin-before/`。

### 设置与命名更新（2026-10-19）

窗口、托盘入口、自启动和设置快捷方式统一使用“RIME液态输入法”，设置图标为“液”。
Fcitx 的内部输入法 ID 仍为 `rime`；用户级名称与图标覆盖保存在
`~/.config/fcitx/inputmethod/rime.conf`，不修改系统包。部署工具 `start`/`restart`
会更新这些用户级入口，`branding` 可单独刷新名称和图标。

设置新增“外观与界面”：候选数量（1–10，默认 6）、横排/竖排/跟随输入方案
（默认横排）、拼音/输入编码、辅助提示、候选序号、翻页按钮和滚轮翻页。
预览随可用宽度自动换行，使用真实候选窗的布局和绘制代码。取消只丢弃预览。

候选数量通过 Python 3 的 PyYAML 更新实际 Rime `default.custom.yaml` 中的
`patch.menu/page_size`，保留其他配置，修改前备份并原子替换；应用后通知 Fcitx
重新部署 Rime。只改外观开关时无需重新部署。候选数量变更应在输入结束后应用。
不改动用户词库、学习记录或同步数据。手动配置文件只改 UI 不能重建 Rime 词典，
候选数量请通过设置界面应用。

右键菜单在窗口首次映射前准备 alpha 透明表面，并保留稳定的透明原生窗口；
去掉原生叠加阴影与多余菜单边框，圆角外部保持透明。公共液态材质与图标尺寸不变。

新增验证：界面偏好和配置往返、保存/取消、隐藏拼音后选词索引、窄预览不溢出、
菜单三次开关四角透明，以及真实 Fcitx/Rime 6→9 个候选的数字选词和下一页索引。
`tests/preferences_test.py` 验证 YAML 键/注释保留、备份、非法输入和私人数据库不变。
配置读取支持 `XDG_CONFIG_HOME`，可使用临时配置根运行全部设置持久化测试。
修改前源码、模块、可执行文件和用户配置：`backups/20261019-rime-settings-before/`。

这是一个 Fcitx 4 Kimpanel 前端。中州韵继续负责输入、词库和候选数据；本程序只负责显示候选窗，并复用 `../shared/liquid-glass` 的缓存液态材质。

候选窗在弹出时采样一次 X11 背景，之后只刷新文字、首选项和翻页状态，不在每次按键时抓屏，也不保存输入文本。窗口使用无焦点的置顶工具窗口，点击候选词会把选择信号发回 Fcitx。

## 构建

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/home/zhouzhang/应用/code-work/ukui-kaishicaidan-v2/.local-deps/root/usr/lib/aarch64-linux-gnu/cmake \
  -DUKUI_LIQUID_GL_INCLUDE_DIR=/home/zhouzhang/应用/code-work/ukui-desktop-organizer/apps/desktop-shell-qt/.local/v10-sdk/root/usr/include
cmake --build build -j2
```

## 应用与回退

```sh
python3 manage.py apply
python3 manage.py status
python3 manage.py restart
python3 manage.py restore
```

`apply` 只安装用户级候选窗和自启动入口，会在 `backups/` 保存 Fcitx 配置快照；不会修改 Rime 词库或输入方案。`restore` 删除本项目创建的自启动入口并退出候选窗，Fcitx 回到原来的 Classic UI。

## 手动调节

首次 `start` 或 `apply` 会把 `liquid-panel.conf.example` 复制为
`~/.config/fcitx/liquid-panel.conf`。直接编辑这个文件即可调节中文/英文
字体、字号、加粗、斜体、字距、顶部输入行高度、候选行高度、窗口宽高、内外
边距、候选间距、阴影和圆角。`WindowWidth`、`WindowHeight`、`RowHeight` 和
`HeaderHeight` 填 `0` 时保持自动布局；改完运行 `python3 manage.py restart`。

## 验证

```sh
cd build && ctest --output-on-failure
cd ..
dbus-run-session -- xvfb-run -a -s '-screen 0 1280x800x24' \
  env FCITX_LIQUID_ISOLATED_TEST=1 python3 tests/protocol_test.py
```

隔离测试覆盖 Kimpanel 握手、候选更新、首选项、翻页、点击不抢焦点、缓存次数、屏幕边缘定位、输入法开关和干净退出。真实桌面视觉由用户截图验收。

## 图形设置（2026-09-29）

托盘右键 → **液态外观设置…**，或开始菜单搜索 **中州韵液态外观设置**。
也可运行 `python3 manage.py settings`。

- 字体、尺寸间距、光学效果、颜色四页，下方为嵌入式候选窗预览，与实际窗口共用绘制与布局。
- 使用前先安装 Rime，并在本机 Fcitx 的输入法设置中添加、选择 Rime。
- “外观方案”提供当前设置、默认玻璃、紧凑两行和大字清晰四种快捷选择。
- 控件修改后立即刷新预览；示例背景可选彩色、白色、浅灰、深灰和黑色，不采集桌面或当前输入内容。
- 支持候选字、拼音、序号/光标、选中字、选中框、边框和染色颜色；颜色对话框可调透明度。自动深浅适配默认保留，关闭后始终使用指定文字颜色。
- “恢复默认”载入 CandidateConfig 中的真实默认值并立即更新预览，点击应用后才持久化；关闭未应用的预览不会写配置。
- 应用写入 `~/.config/fcitx/liquid-panel.conf` 并热更新候选窗，无需重启。字体单位为 pt，其余尺寸按桌面 DPI 缩放。
- 菜单不再交给托盘宿主直接打开，主菜单与输入法子菜单均等待鼠标释放。待打开期间使用短时定时器，最多等待 5 秒，无空闲轮询；可见菜单不因属性更新而重建。
- 验证：配置序列化/透明色/边界、预览不修改实际设置、恢复默认、原生右键按住/释放、菜单属性更新、选词/翻页/焦点/缓存/位置协议。实际托盘视觉由用户截图验收。

修改前源码和用户配置备份：`backups/20260929-gui-before/`。

### 右键菜单统一公共材质

托盘主菜单、输入法子菜单及本程序内的 Qt 菜单直接接入 `../shared/liquid-popup`，
删除此前独立的深色 QSS 外框。前景色由宿主提供，圆角、背景采样、折射、高光、
菜单留白与淡入均归公共模块处理。托盘菜单打开时读取任务栏已有的
`~/.config/ukui/liquid-panel.ini` 中 `appearance/highlight` 和 `appearance/reducedMotion`，
不新增独立菜单特效参数。右键释放后弹出与可见菜单不重建的修复继续保留。

接入验证：11 项 Qt 测试及隔离协议回归通过；新增断言确认实际托盘菜单已挂载
公共 skin、透明背景和圆角遮罩，关闭后恢复原生样式；当前运行实例已重新构建并重启。

### 白底可读性（2026-09-30）

开启现有的“自动深浅适配”时，候选窗根据已缓存背景的亮度，在浅色背景上使用
更浅的磨砂玻璃、较柔和的高光与边缘。公共 `LiquidOpticsRenderer::setLightSurface`
提供此能力，默认值为零，其他调用方不启用时保持原来的材质。

文字、拼音、序号和选中字分别依据所在区域的材质、染色及选中底色调整对比度；
自定义颜色足够清晰时保留，浅底选中框适当增加不透明度。关闭自动适配则保留
指定颜色和原有材质。字体、字号、间距与用户配置文件不因升级而改写。

颜色分析只在内容或材质更新时使用缓存图像，绘制时读取缓存颜色，不增加逐键抓屏、
轮询或动画。区域采样以 4.5:1 对比度为目标，但复杂图案的局部像素仍需实际截图验收。

图形设置下方可切换白色、灰色和黑色背景比较效果。验证包含 Qt 测试、
隔离 Kimpanel 协议回归，以及纯合成 GPU 材质检查：

```sh
./build/fcitx-liquid-panel --contrast-check build/contrast-material.png
```

此命令只渲染合成背景并退出，不打开输入窗口、不采集桌面。当前 GPU 检查中，
白底中央亮度由 145 提升至 238（0–255），浅色模式归零后与原材质逐像素一致。
升级前源码、配置及可执行文件保存在 `backups/20260930-light-contrast/`。

### 托盘输入状态（2026-09-30）

托盘由固定“文”图标改为实时状态：蓝色“中”表示 Rime 中文，青色“a”表示
Rime 英文小写，灰色“a”表示键盘直输或输入法未启用，橙色“A”表示 Caps Lock
大写锁定。悬停提示同时说明当前输入法和模式；其他输入法使用其主题图标。

Shift 的中英文切换读取 Kimpanel 的 `/Fcitx/rime-enzh` 属性，Ctrl+空格读取
实际输入法属性及 Enable 状态。启动时异步读取一次当前开关状态，后续事件
优先于尚未返回的旧查询。Caps Lock 通过独立 XKB 连接和 Qt socket notifier
监听指示灯事件，并在启动时读取已有锁定状态；不截获按键、不抓屏、不空闲轮询。
图标更新独立于菜单重建，因此菜单保持打开时也会更新状态。

构建需要 X11 头文件，可由现有 `UKUI_LIQUID_GL_INCLUDE_DIR` 或
`FCITX_LIQUID_X11_INCLUDE_DIR` 提供；运行时复用系统 `libX11.so.6`。
验证覆盖属性替换、Rime 中英文、不同输入法、未启用、准备中、Caps 已开启时启动、
原生 Caps 事件以及真实 Fcitx/Rime 的 Shift/Caps/Ctrl+空格往返切换。
真实快捷键测试只运行于独立 D-Bus/Xvfb，使用临时无词库 Rime 测试方案：

```sh
dbus-run-session -- xvfb-run -a -s '-screen 0 1280x800x24' \
  env FCITX_LIQUID_ISOLATED_TEST=1 python3 tests/real_mode_test.py
```

修改前源码及运行文件：`backups/20260930-tray-state-before/`。

### 自定义托盘图片（2026-10-01）

四种模式使用用户提供的透明 PNG：`assets/tray/chinese.png`、`latin.png`、
`uppercase.png`、`direct.png`。原图及对应关系见同目录 `sources.json`；图片原样
内置在 Qt 资源中，移动原始图片文件夹不影响运行。每张图片首次使用时生成并缓存
22–128 像素的托盘尺寸，保留原图比例与透明度。输入法状态判断和快捷键逻辑沿用
现有实现，等待状态和其他输入法保留原有图标处理。

替换前源码和运行文件：`backups/20261001-tray-images-before/`。

### 模糊计算优化（2026-10-02）

公共玻璃模块的两张模糊纹理改为滑动求和：水平方向复用相邻像素的累加值，
垂直方向保留各列累加值并连续处理整行。六遍横纵交替、边缘重复采样、每遍
整数取整以及前后缩放保持原样；折射、高光、色散、分辨率和用户参数沿用现有值。
这一步不依赖屏幕区域缓存，候选窗换位置或尺寸时重新计算也能受益。

`diffusion-test` 使用优化前算法作为独立参考，逐像素比较 3,529 组图片，覆盖
小于模糊核的窄图、随机颜色、透明度、不同格式、DPR 与窗口尺寸。
测试加入 `ctest`；还可单独运行可复现的两张纹理 CPU 对比：

```sh
./build/diffusion-test --benchmark build/diffusion-benchmark.json
```

旧版源码及可执行文件：`backups/20261002-diffusion-before/`。
本次 GPU 候选更新回放与原始测量记录：`build/perf-20261002-diffusion/`。

### 新一轮输入的首次背景（2026-10-03）

候选窗关闭与重新打开的通知可能在同一轮 Qt 刷新中合并。此前仅在刷新时
检测关闭，可能跳过这次关闭并把上一轮的背景带入首次显示。现在协议状态
变为无可显示内容时立即隐藏窗口并释放背景、材质及模糊纹理；随后重新输入
会在窗口显示前采样本次页面。同一轮输入的光标更新和候选尺寸变化继续复用背景。

新增回归模拟通知合并、背景从红色页面切到绿色页面，以及通过可见性或空数据
结束输入两种情况，检查首次画面更新、采样时窗口已隐藏、后续按键不重复采样。
两种情况均通过 CPU 与当前原生 GPU 绘制验证。
修改前文件和可执行文件：`backups/20261003-first-background-before/`。

### 简洁四状态托盘图（2026-10-10）

按用户最新四宫格 JPEG 的结构手工重建白色线条 SVG：中文“中”、英文“英”、
大写“A＋箭头”和直输键盘。按后续要求去掉四张图的外层圆角框和内填充，
保留字样、键盘自身轮廓和大写箭头，图标背景完全透明。移除绿色展示背景和
下方说明；这是按参考图重建的矢量资源，并非像素裁剪。内置 imagegen 抠图尝试
留下了填充及边缘杂点，未采用其结果；尝试提示词另见 `assets/tray/minimal-imagegen-attempts.md`。

可编辑源文件位于 `assets/tray/vector/`，使用 Qt SVG 加载器生成四张 512 像素
透明 PNG。原图及资源校验值见 `assets/tray/sources.json`。四种状态判断、资源
文件名和 22–128 像素缓存沿用现有实现，运行时不加载 SVG 或生成图像。
重新生成 PNG 的方式（CMake 使用与主项目相同的 Qt 路径）：

```sh
cmake -S assets/tray/vector -B build/tray-vector-renderer
cmake --build build/tray-vector-renderer -j2
for mode in chinese latin uppercase direct; do
  QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= build/tray-vector-renderer/render-tray-vector \
    "assets/tray/vector/$mode.svg" "assets/tray/$mode.png"
done
cmake --build build -j2
```

替换前的彩色图标和可执行文件：`backups/20261010-tray-minimal-before/`；更早图标：
`backups/20261010-tray-extracted-before/`。
去除外框前的资源和可执行文件：`backups/20261010-tray-borderless-before/`。
无外框版进一步缩减画布留白：中、英主体放大约 1.58 / 1.50 倍，键盘约 1.28 倍，
大写 A 放大 1.82 倍并调整箭头位置。以系统主题 24 像素无线图标的约 22 像素
有效宽度为参考，四种状态最大有效边约 21–22 像素，透明画布和托盘槽尺寸不变。
放大前资源及可执行文件：`backups/20261010-tray-enlarge-before/`。


### 表情或生僻字显示方框（2026-11-03）

新版表情可能未被系统旧字体覆盖，Qt 5 的同名字体注册也可能继续选中旧文件。新增只读检查和显式用户级修复工具：

```sh
/usr/bin/python3 font_support.py check
/usr/bin/python3 font_support.py install
# 离线：在 install 后加 --emoji-font /path/to/NotoColorEmoji.ttf
```

工具按固定上游提交和 SHA-256 验证字体，只使用已安装的扩展汉字字体作为回退，保留普通中文主字体和 Rime 配置。已有自定义规则不覆盖；普通 install-user.py 不自动联网安装字体。新发布目录会携带 font_support.py、来源清单、许可证和独立事务帮助模块。

本机两份词库 9,481 个不同候选的整串 Qt 排版检查通过。详见 [来源、使用方法和验证边界](../../docs/RIME_FONT_FIX_20261103.md)。
