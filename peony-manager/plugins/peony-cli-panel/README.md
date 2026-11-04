# Peony 原生右侧 CLI 终端

默认仅加载约 31 KB 的菜单插件；不创建 PTY、终端控件、后台定时器或子进程。首次点击本地目录右键入口时加载 QTermWidget 模块。没有额外的终端应用、Python 解释器或网页终端进程。

## 使用

Peony Manager → **右侧 CLI 终端**：设置 CLI 名称、右键菜单文字、启动命令、宽度、字号、历史缓存。配置位于用户的 `~/.config/peony-manager/cli-profiles.json`（遵循 XDG_CONFIG_HOME）。保存后用于下一次菜单/新会话，无需重启文管。

目录空白处或单个文件夹右键启动；目录通过启动参数安全传递，不拼进 shell 命令。用户填写的启动命令由 `/bin/bash -lc` 执行，因此支持自己配置的参数和复合命令。名称只控制显示，不用于执行。

默认 Codex 命令使用独立的 `--profile peony-cli`。模型和推理强度保存在 `~/.codex/peony-cli.config.toml`（遵循已有 `CODEX_HOME`），与桌面端的默认模型分开。CLI 内输入 `/model`，选模型和强度后按 **Enter（default）**，后续新会话会继续使用该选择；按 **s（session）** 或直接输入 `/model 模型 --reasoning=强度` 仍遵循 Codex 的仅本次会话语义。已用 Codex CLI 0.166.0 验证。

设置页读取及修改同一份独立配置。首次使用会迁移旧 `cli-profiles.json` 中的模型和强度，之后不再用旧值覆盖 CLI 内的选择；留空的项目继承 Codex 默认。不会修改公共 `config.toml`、认证或现有会话。启动只传 profile 参数，不再附加覆盖 profile 的 `-m`/`-c`；参数仍经独立 argv 通过固定登录 shell 脚本，保留登录环境。自定义启动命令保持原样，不解析或追加参数。

面板“收起”保留会话；终端页签 × 结束该会话。首次展开后 Ctrl+Alt+T 切换面板，Ctrl+Shift+C/V 复制粘贴。每个窗口最多 6 个会话；同目录和相同命令重复点击复用会话。

终端内 Shift+Enter（包括小键盘 Enter）发送 LF，与 Ctrl+J 相同，用于 Codex 输入框换行；普通 Enter 保留 CR/发送行为，Ctrl+J 也继续可用。映射只作用于嵌入终端，不修改系统终端或 Codex 全局快捷键。已用真实 PTY 的原始字节验证 LF 与 CR 的区别，以及文管快捷键不会抢走 Shift+Enter。

文件标签栏右上方使用一个固定位置的按钮，在“收起 CLI”和“展开 CLI”之间切换。展开面板时也保留在窗口的相同坐标，点击恢复同一组会话和进程。关闭最后一个终端标签后隐藏按钮。已在真实 Peony 中验证两个状态的按钮矩形完全一致，以及收起、按钮展开、会话复用、最后标签关闭。

共享按钮属于外层文管窗口：不加入 Peony 隐藏的回收站/搜索工具栏，也不受原预览工具栏固定 26px 宽度限制。缩放窗口时跟随窗口右侧移动，纵向对齐文件标签栏；展开 CLI 导致文件区域变窄时按钮不会跟着左移。收起时为文件标签留出空间，展开时为面板标题留出按钮位置。

CLI 会话标签行的顶部与高度跟随左侧详细列表的列标题（文件名称、修改日期等），面板标题对应文件夹标签行。切换目录、列表布局、字号或窗口尺寸后按实际表头位置重新对齐；图标视图则跟随文件夹标签行高度。真实 Peony 测试同时验证两侧标签/表头矩形对齐、CLI 内容紧接标签行以及收起/展开按钮位置不变。

终端文字区域右键提供“复制”和“粘贴”。复制需要先选中文字；粘贴使用标准剪贴板，保留中文和换行，单行文本不会额外发送回车。右键点击保留现有选区，不会传给 CLI 的鼠标追踪。CLI 接管鼠标时，可按 Shift 拖动选择文本。菜单复制到剪贴板、菜单粘贴到真实 PTY 均已完成集成测试。

成功进入会话后不显示重复的完整路径栏，完整路径保留在终端页签悬停提示。背景与默认文字颜色跟随宿主文管调色板，并响应主题切换。正文字体默认使用 Sarasa Fixed SC（更纱等宽），不存在时回退 Noto；默认跟随系统字号。旧 QTermWidget 强制整数像素，因此对奇数像素字号作一像素对齐，以保持中文宽度恰好等于两个英文字符。正文默认留白 10 px，使用细光标和较紧凑的标题栏。字体、字号和留白均可在管理器中修改。

本机字体来源：[Sarasa Gothic v1.0.43](https://github.com/be5invis/Sarasa-Gothic/releases/tag/v1.0.43)，仅在用户字体目录安装 Regular 和 SemiBold 两个字重，不改变系统默认字体。字体包已对照发布方 SHA-256 文件校验；原包、哈希清单和 OFL 许可保存在项目 `vendor/fonts/`。字体随终端使用，不增加常驻进程。

终端取消内部边距，并用当前底色填充字符网格外的剩余像素，避免旧组件留下灰框。已通过实际像素与预览检查。关闭右侧终端页签 × 已单独测试：所属 CLI、普通子进程以及自行 setsid 的后代均停止，文件管理窗口和其他会话继续运行。

关闭文管窗口结束所属全部终端进程，独立窗口互不影响。每个会话使用独立 systemd 用户 scope；原生监督器通过本地 socket 检测控件销毁及 Peony 异常退出，停止 scope 内的全部后代进程（包括自行 setsid 的子进程）。用户显式通过外部服务启动的任务不在这个 scope 清理范围内。

QTermWidget 0.14 不回复 OSC 10/11 默认前景/背景查询。运行时仅补充这两类标准颜色查询响应，让 CLI 正确识别浅色背景，避免用户消息退回深色气泡。不会重映射 ANSI 黑色/灰色色板，也不会记录终端正文。

## 构建与安装

从本目录运行 `bash build.sh`，默认使用主机已安装的 Qt 5 Core/Gui/Widgets/Network、Peony 开发头文件和 QTermWidget 开发/运行包。需要 C++ 编译器、`pkg-config` 与 Qt 的 `moc`。发行版包名可能不同；安装对应开发包、QTermWidget 运行包和 `qtermwidget5-data` 后再构建。脚本不会安装依赖，也不会调用授权 helper。

若使用单独解包的兼容 SDK，可显式指定包含 `extract/qt/usr`、`extract/libpeony-dev/usr` 与 `qtermwidget/extract/usr` 的 SDK 根目录：

```sh
bash build.sh --sdk-root /path/to/vendor
bash build.sh --sdk-root /path/to/vendor --output-dir /tmp/peony-cli-panel-build
```

不传 `--sdk-root` 时只查找系统开发包；仓库不包含该 SDK。SDK 模式使用 SDK 的 Qt/Peony/QTermWidget 头文件与 moc，并链接宿主已安装的 Qt 5 运行库，因此宿主 Qt 运行库 ABI 必须与 SDK 匹配。构建结果是 Peony 插件 `libpeony-cli-panel.so`、惰性加载的 `peony-cli-panel/libpeony-cli-terminal.so` 和原生 `peony-cli-panel/peony-cli-session` 三个目标。运行时目录还带有 QTermWidget、utf8proc 动态库、键盘布局、颜色方案及可用的许可证文本。终端运行库设置了 `$ORIGIN` RPATH，因此部署时必须把 `peony-cli-panel/` 目录作为插件 `.so` 旁边的整体保留；不要只复制两个原生 `.so`。

QTermWidget 的键盘布局与颜色方案位于运行时目录的 `data/` 下，作为缺少系统 `/usr/share/qtermwidget5` 数据时的回退，并由颜色方案加载使用。构建脚本只写入默认的本目录 `build/` 或 `--output-dir` 指定的目录，不提供系统安装操作；部署/暂存由上层安装器或调用方在明确目标目录中完成，不会自动执行 `pkexec` 或安装 helper。

默认输出包含：

```
build/libpeony-cli-panel.so
build/peony-cli-panel/
  libpeony-cli-terminal.so
  libqtermwidget5.so.0
  libutf8proc.so.2
  peony-cli-session
  data/
  licenses/       # 仅在依赖包提供许可证文本时
```

Peony Manager 的安装按钮调用系统授权 helper 安装完整组件。安装或更新后重启文管生效；只改名称/命令则不用重启。移除入口保留运行组件，以免干扰更新前已有的会话。该构建脚本本身只产生文件，不执行这个安装流程。

QTermWidget 与 utf8proc 使用发行版原始库，许可证位于构建包 `licenses/`（依赖包提供相应版权文件时）。如使用外部 SDK，其二进制和数据由调用方提供；它们没有被伪装成仓库内置依赖。

## 已完成的验证

运行 `python3 -m unittest discover -s tests -v` 验证配置校验、原子保存和默认生命周期。

在项目根目录运行 `python3 tests/run_cli_panel_integration.py`，验证真实 Qt 插件、PTY 和 systemd scopes：按需加载、特殊字符目录、重复点击复用、收起保活、独立窗口关闭、窗口进程突然退出、普通与 setsid 子进程清理。测试不修改用户配置，也不会访问 Codex 服务。

运行 `python3 tests/run_real_peony_cli_probe.py` 会在独立 Xvfb 显示和 D-Bus 会话中启动系统安装的 Peony，用 `/bin/sleep` 替代 Codex，验证真实标签栏中的按钮可见性、裁切、缩放、恢复原会话与关闭最后标签。需要 Xvfb、dbus-run-session 以及正在运行的用户 systemd；不操作桌面上现有的文管窗口或调用 Codex 服务。设置 `PEONY_TEST_CAPTURE=/tmp/peony-collapsed.png` 可以保存测试窗口截图。

运行 `xvfb-run -a /usr/bin/python3 tests/test_cli_manager_gui.py` 验证真实管理器表单通过 QtWebChannel 保存配置。

2026-10-26 初版测试宿主中单个展开终端的 PSS 增量约 9.5 MiB；Noto 字体与主题版约 11–12 MiB，更纱字体和完整混排预览约 24 MiB。不包括 CLI 本身和其他进程。这些数值不是对当前 Peony 实际窗口的性能测量。默认仍不加载终端模块或启动终端进程。
