# 密集接入点环境中手机热点不显示（2026-11-03）

用户找不到 目标手机热点 热点。Wi-Fi 开关、rfkill 和已有连接正常，系统能够扫描 2.4/5 GHz。用户切换热点到 2.4 GHz 后，内核扫描中出现目标 SSID，但 NetworkManager 和控制中心列表仍缺少它；NetworkManager 的接入点数停在 200，绝大部分属于同一商场网络和隐藏接入点。

wpa_supplicant 上游的默认 BSS 缓存上限为 200：[配置定义](https://w1.fi/wpa_supplicant/devel/config_8h.html)。这是接入点缓存数量，不能与控制中心按 SSID 去重后的显示条数混为一谈。

经系统管理员认证，执行 `wpa_cli -i wlp2s0 set bss_max_count 1024` 返回 OK。重新扫描后 NetworkManager 接入点数升至 391，目标手机热点 随即进入列表：2462 MHz（2.4 GHz），信号 100%。已安装控制中心的后端也返回该热点，30 个去重网络中包含它。

为避免重启后上限恢复，安装 root:root / 0755 的 NetworkManager dispatcher hook：`/etc/NetworkManager/dispatcher.d/80-kylin-lqui-wifi-scan-cache`，源文件在 `code-work/ukui-liquid-desktop/integration/80-kylin-lqui-wifi-scan-cache`。它只在无线接口 up/reapply 时设置缓存上限，不修改连接配置或自动连接目标。已验证 shell 语法、非无线/非目标事件跳过、安装文件内容和权限。未以重启方式验证；当前运行实例已应用同一设置。

整个操作没有断开原连接，也没有尝试连接手机热点或读取密码。恢复旧行为时删除此 hook；如需立即恢复上游默认上限，可在管理员终端执行 `wpa_cli -i wlp2s0 set bss_max_count 200`。

不能据此断言热点此前的 5 GHz 模式也一定可见；本次最终验证的是用户切换后的 2.4 GHz 热点。

公开记录中的真实 SSID 已使用描述性名称替代；频段、信号、数量与验证结论保留。
