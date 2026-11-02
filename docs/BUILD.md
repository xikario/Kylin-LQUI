# 构建入口

需要 CMake ≥3.16、C++17 编译器、Python ≥3.8、Qt 5.12 开发包。按模块安装 Qt Widgets/DBus/Network/Test、X11、KDE Frameworks 5、GIO/PyGObject、PyQt5 等依赖。Peony 扩展另需 Peony/Qt 开发头文件，终端扩展另需 qtermwidget。不要把个人机器的 SDK 绝对路径提交到仓库。

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

## 安装与恢复

设置程序：`python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/install.py`。

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
