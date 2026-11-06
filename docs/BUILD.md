# 构建入口

需要 CMake ≥3.16、C++17 编译器、Python ≥3.8、Qt 5.12 开发包。按模块安装 Qt Widgets/DBus/Network/Test、X11/XCB/OpenGL、GIO/PyGObject、PyQt5 等依赖。Peony 扩展另需 Peony/Qt 开发头文件，终端扩展另需 qtermwidget。不要把个人机器的 SDK 绝对路径提交到仓库。

`ukui-panel-liquid` 构建的是加载到已有 `ukui-panel` 中的 Qt 样式插件，不重新编译系统面板，也不需要整套 KDE Frameworks 5 开发包。其构建依赖为 Qt 5 Core/Gui/Widgets/DBus、X11/XCB 与 OpenGL 头文件等；部分测试另用 Qt Test/Network。Dock 通过运行时符号查找复用宿主已有的 `KWindowSystem` 能力，不包含 KF5 头文件或链接 `KF5::` 目标。系统面板及其原生模块自身需要的运行库由系统包管理，不应与本插件的开发依赖混淆。

## 常用模块

在仓库根目录执行：

```bash
cmake -S code-work/ukui-liquid-desktop/apps/ukui-panel-liquid -B code-work/ukui-liquid-desktop/apps/ukui-panel-liquid/build
cmake --build code-work/ukui-liquid-desktop/apps/ukui-panel-liquid/build -j2
cmake -S code-work/liquid-cli -B code-work/liquid-cli/build
cmake --build code-work/liquid-cli/build -j2
cmake -S code-work/fcitx-liquid-panel -B code-work/fcitx-liquid-panel/build
cmake --build code-work/fcitx-liquid-panel/build -j2
cmake -S code-work/ai-space -B code-work/ai-space/build
cmake --build code-work/ai-space/build -j2
```

Fences、开始菜单及具体发行版依赖详见各自 README 和 `code-work/ukui-liquid-desktop/docs/BUILD.md`。若使用外部 Qt SDK，通过 CMake 参数传入路径；仓库不附带从系统提取的 SDK。

Peony CLI 扩展提供独立源码构建入口（需要系统 `pkg-config`、Qt 5、Peony 与 QTermWidget 开发包）：

```bash
bash peony-manager/plugins/peony-cli-panel/build.sh --output-dir build-review/peony-cli-panel
```

该入口只构建插件、终端运行库、会话 helper 及伴随资源，不安装或重启文件管理器。可选外部 SDK 的目录结构、ABI 要求与完整产物布局见 [CLI 扩展说明](../peony-manager/plugins/peony-cli-panel/README.md)。

## 安装与恢复

设置程序：`python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/install.py`。

默认安装器通常读取各模块源码下的 `build/`。如果实际使用 `build-review/` 或其它目录，应先核对安装器输入与本次构建产物一致。支持指定路径的脚本按其说明传入产物；不支持的脚本应先在要求的目录重新构建，或使用经核对的独立暂存流程，不要让旧 `build/` 中的文件混入更新。构建成功不代表已经安装，Peony 系统插件的管理员安装也不由上述构建脚本执行。

Panel：对应 `scripts/install.py` 只更新用户本地插件，宿主 ABI 不匹配时应回退原生 Panel；请遵循该模块的安装与恢复说明。AI 小站与输入法分别使用自己的安装脚本，先完成构建。不要在生成回复期间强行退出 AI 小站，不要为了替换候选窗退出 Fcitx 引擎。

## 测试

```bash
QT_QPA_PLATFORM=offscreen python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/tests/test_glass_regressions.py
xvfb-run -a python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/tests/test_control_center.py
(cd code-work/liquid-cli/build && ctest --output-on-failure)
```

原生 OEM 测试只能使用私有 Xvfb、D-Bus 和测试配置，详见 `apps/ukui-panel-liquid/tests/dock_oem.py`。Qt 离屏通过不能替代窗口管理器下的动画、焦点与分数缩放验收。

## 快捷设置的玻璃桥接

构建 Panel 时会同时生成 `lqui-glass-bridge`，它使用共享 Snell GPU 材质与有界卡片缓存；缺少 OpenGL 时走 CPU 回退。控制中心通过可选 C ABI 启用自适应烟色表面，其他调用方默认材质保持不变。随后运行统一设置安装脚本，桥接库会一并进入用户本地的版本化安装目录。只安装 Python 设置程序、不构建此库时，会使用可读的磨砂降级。

```bash
cmake --build code-work/ukui-liquid-desktop/apps/ukui-panel-liquid/build --target lqui-glass-bridge -j2
python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/install.py
QT_QPA_PLATFORM=offscreen python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/tests/test_native_glass.py
```

安装器会在没有用户覆盖时创建快捷设置登录预加载项，运行 `--resident`。它只在启动时预热一次；隐藏后不持续轮询/渲染，入口悬停预备三秒未使用便释放场景。本机已有发布的隐藏 PSS 约 51 MiB，这是减少冷启动延迟的内存成本。详见 [首次响应与驻留说明](CONTROL_CENTER_RESPONSIVENESS_FIX_20261102.md)。

补充回归命令（原生桥路径可按实际构建目录调整）：

```bash
UKUI_LIQUID_GLASS_NO_GL=1 xvfb-run -a /usr/bin/python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/tests/test_control_center.py
LQUI_NATIVE_GLASS_LIBRARY="$PWD/code-work/ukui-liquid-desktop/apps/ukui-panel-liquid/build/liblqui-glass-bridge.so" /usr/bin/python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/tests/test_adaptive_glass.py
# 在独立进程设置 UKUI_LIQUID_GLASS_NO_GL=1，再运行同一材质套件验证 CPU 回退。
```

`test_control_center_surface.py` 在有合成器的 X11 桌面上验证实际屏幕与完整重绘是否一致，运行时会显示合成测试窗口。GPU 材质通过、控件抓图通过和真实映射画面通过分别记录，不能相互替代。
