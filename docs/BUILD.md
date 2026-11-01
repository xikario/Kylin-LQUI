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
