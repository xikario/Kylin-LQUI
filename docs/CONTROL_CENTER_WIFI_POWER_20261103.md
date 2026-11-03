# Wi-Fi 实时排序与麒麟电源计划（2026-11-03）

## Wi-Fi

后端本来按已连接、信号强度排序，界面却为了保留行对象而一直沿用旧顺序，新热点追加在末尾。因此新出现的 100% 热点也会排在弱信号之后，与频段无关。

现在每次刷新按已连接置顶、其余强度降序、名称/ID 稳定打平排序；移动既有按钮而非重建列表。已验证新 100% 热点插入、后续强度逆转及按钮连接目标保持正确。安装后实际 Wi-Fi 页面中 目标手机热点已连接并置顶，其余按 100%、87%、79%、74%…排列。没有切换用户的网络连接。

## 电源模式

本机没有运行 power-profiles-daemon，而采用麒麟原生电源管理。原适配器只读通用服务，所以隐藏了模式选择。

现在保留通用接口优先路径，同时在原生守护进程 org.ukui.powerManager 运行且对应 GSettings schema 存在时使用麒麟电源计划。根据当前供电状态读取/写入 power-policy-ac 或 power-policy-battery。上游原生映射为 0=性能、1=均衡、2=省电：[UKUI common.h](https://github.com/ukui/ukui-power-manager/blob/master/PowerManagementDaemon/common.h)。没有安装竞争的电源守护进程，也没有直接写 sysfs 绕过系统管理。

界面改为独立玻璃卡片中的三个整行按钮，标明当前选择。只在系统确认后更新当前标识；写入锁定或不支持的模式禁用，缺失服务时提供说明，不再无提示隐藏整个选项。

本机验证：电池供电时暂时从性能切到均衡，8 个 CPU 的 governor 都从 performance 变为 ondemand，原生 power-policy-current 同步为 1。随后恢复 performance，原来的 AC/电池/当前策略值全部恢复为 0。没有保持测试模式。

79 项测试通过，覆盖新增实时排序、行对象复用、三按钮确认状态、UKUI 参数验证和供电计划隔离、通用服务优先级。实际已安装页面及不可见 GPU 预览核对通过，三个模式完整显示且无需滚动。版本与验证数据见 docs/evidence/20261103-wifi-power/verification.json，私人桌面截图仅在 maintenance。

公开记录中的真实 SSID 已使用描述性名称替代；频段、信号、数量与验证结论保留。
