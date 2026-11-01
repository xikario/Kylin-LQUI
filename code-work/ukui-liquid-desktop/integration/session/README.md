# 桌面接管实验与恢复

此工具让 Fences 在当前 X11 会话中短时独立承担桌面，并在到期、Fences 退出、隐藏或无响应后恢复原生 Peony 桌面。它不修改登录 provider、自启动或系统二进制；不是永久接管安装器。

Fences 设置 → **桌面接管（实验）**提供 60 秒试运行与恢复。安装后还提供“恢复原生桌面服务”的应用入口，即使 Fences 不可用也能操作。

从主仓库运行：

```sh
/usr/bin/python3 integration/session/session_bridge.py --status
/usr/bin/python3 integration/session/session_bridge.py --trial 60
/usr/bin/python3 integration/session/session_bridge.py --restore
```

只允许 5–300 秒试验。停止前核对 D-Bus 所有者、同一用户、DISPLAY、可执行文件和 PID 启动时间；只针对 `/usr/bin/peony-qt-desktop`。若它还承载普通文件管理器或对话窗口，试验直接拒绝，不关闭这些窗口。恢复优先交给 UKUI 会话管理器，再使用原生桌面程序兜底；恢复必须看到实际桌面窗口，不能只凭进程出现。

试验守护只在试验期间存在。状态位于 `$XDG_STATE_HOME/ukui-liquid-desktop/desktop-trial-<display-hash>.json`，默认目录为 `~/.local/state/ukui-liquid-desktop/`。不同显示会话使用不同锁与状态，文件原子写入且仅用户可读写。恢复后回到原来的运行方式，Fences 可以继续显示；不会擅自清理旧配置或凭据。

## 验证

```sh
env LIQUID_SESSION_ISOLATED=1 QT_SCALE_FACTOR=1 \
  xvfb-run -a -s '-screen 0 1920x1080x24' dbus-run-session -- \
  /usr/bin/python3 integration/session/test_desktop_trial.py \
  --fences apps/ukui-fences/build/ukui-fences \
  --output /tmp/liquid-session-lab
```

测试把实际 KWin、Peony、Fences 和守护放在同一个受限文件系统中，使用私有 X11、会话 D-Bus、配置、缓存、运行与状态目录。覆盖：拒绝同进程普通窗口、独占桌面确实退出原生进程、超时恢复、Fences 退出恢复、隐藏恢复，以及桌面测试文件不变。

2026-10-30 的实机 15 秒试验也完成自动恢复，登录 provider 保持 `peony-desktop`。这不代表注销登录、多屏、休眠和长期稳定性已全部验收；永久接管仍需这些测试。
