# Clash 托盘菜单重建导致的抖动、残影与占屏修复（2026-10-28）

基线：`78c53397d744e6101bd21e98078f8c605ba3ace4`（工作区另含未提交的“回车确认”修复，见 `DIALOG_CONFIRM_ENTER_20261027.md`，本次未改动其内容）。

## 现象

用户在隐藏托盘弹层里右键 Clash PyKylin、快速切换代理后，再点任务栏，屏幕右下出现持续抖动的菜单和一串向上叠加的灰色残影（内容是隐藏托盘弹层底行图标），输入被占住，只能重启任务栏恢复。用户配置中 `Clash PyKylin` 位于 `hideApp`，弹层为 5 列。

## 根因（已在隔离环境复现）

1. **Clash 在菜单打开时重建菜单。** clash-pykylin 以 `QMenu.isVisible()` 判断托盘菜单是否打开，但导出到 dbusmenu 的菜单在应用进程内始终不可见；切换代理（`_set_primary_proxy` 在 D-Bus 点击回调里同步阻塞调用 REST）后，延迟的“代理快照就绪”刷新照常执行 `clear()` 并重建整个根菜单。
2. **OEM 托盘把每次重建直接作用于打开的菜单。** `ukui-panel 3.26` 的 DBusMenuImporter 每收到一次 `LayoutUpdated` 就对根菜单 `QMenu::clear()` 再逐项添加；`StatusNotifierButton::updataItemMenu` 在菜单可见时还会再 `popup()` 到光标位置（反汇编确认：偏移 0x118 为右键待弹出计数，可见时无条件重新 popup）。20 项左右的根菜单一次重建约产生 1000 次窗口几何变化。
3. **液态层把这串中间状态逐帧放大。** `MenuSkin` 在每次中间 `Resize` 同步重绘玻璃材质并换样式表，`TrayOverflow::anchorMenu` 也在每次 `Resize` 移动菜单，OEM `popup()` 又把它移回光标处。实测一次重建渲染材质 220 次、主线程约 2.5 秒，期间菜单持有弹出抓取（输入被占），各中间帧留下灰色残影；最终位置停在 OEM 光标坐标而不是图标上方。

不是此前已修复的托盘重排循环（`NativeTrayLayoutRouter`），也不是隐藏弹层 `Shell::reanchor` 的问题。

## 改动

- `shared/liquid-popup/src/LiquidPopup.cpp`（`MenuSkin`）：菜单可见期间的 `Resize`、`Move` 和 Action 增删改只启动一个 0ms 合并计时器；在本轮事件处理结束后按最终几何一次性更新条目度量、屏幕约束、遮罩与材质。打开时的 `Show` 仍同步完成首帧。待合并期间绘制把上一份材质拉伸到当前大小，避免空白条。新增 `liquidMenuMaterialRenders` 动态属性供回归测试计数。
- `apps/ukui-panel-liquid/src/PanelBehavior.cpp`（`TrayOverflow`）：根托盘菜单 `Show` 时仍立即锚定；之后的 `Resize`/`Move`（包括 OEM 再次 `popup()`）合并为一次重新锚定，菜单隐藏时取消。

保留：托盘重排循环修复、通知闪动保留格位、3–8 列设置、通用固定、居中、文件拖放；任务栏图标放大/上浮/高光动效保持回退状态。

## 验证

隔离夹具：`apps/ukui-panel-liquid/build/artifacts/tray-menu-loop-20261028/harness/`（Xvfb + 私有 D-Bus + bubblewrap 临时配置 + 真实 `/usr/bin/ukui-panel` 与 sni-daemon；`clash-fixture.py` 按 clash-pykylin 的菜单生命周期实现，`hidden-items.py` 补足 10 个隐藏图标、5 列）。build/artifacts 不进 Git。

同一场景（根菜单约 20 项，切换后再次右键两次，4 秒后点任务栏）修复前后：

| 指标 | 修复前 | 修复后 |
| --- | --- | --- |
| 材质渲染次数 / 耗时 | 220 次 / 1321 ms | 7 次 / 69 ms |
| 切换后到点击前的窗口几何事件 | 414 | 207（剩余为 Qt/OEM 每项增删自身的 resize） |
| 抖动持续 | 约 2.0–4.0 s 持续不断，直到点击关闭 | 0.5 s 内结束 |
| 最终位置 | OEM 光标处 (1428,144)，脱离图标 | 图标上方 (1345,56)，底边 920 < 图标顶 929 |

回归测试（修复前代码均失败，修复后通过）：

- `liquid-popup`：打开的菜单在一次突发内 `clear()` 并重建 24 项，材质渲染不超过 2 次（旧代码 24 次），不截取自身，最终尺寸材质与遮罩完整。
- `panel-behavior-*`：打开的根托盘菜单被清空重建并被 `popup()` 到别处后，回到被点击图标上方。

CTest：任务栏构建 66 项中 65 通过，`liquid-renderer-sharing-gpu` 在 Xvfb 下跳过（不计为显卡实机通过）。Fences 与开始菜单同样静态链接该公共库，结果见交付说明。

CTest 结果（Xvfb 2880×1800 + 私有 D-Bus）：任务栏 66 项中 65 通过、1 项 GPU 跳过；Fences 105 项中 103 通过、2 项 GPU 跳过；开始菜单 51 项中 50 通过、1 项 GPU 跳过。

## 本机部署（2026-10-28 19:00）

- 仅部署任务栏：`scripts/install.py` 事务安装（同时安装同一构建的 `ukui-liquid-effects-trial`），原生 `/usr/bin/ukui-panel` 未替换。安装记录：`build/artifacts/tray-menu-loop-20261028/install.json`。
- 安装前备份：`apps/ukui-panel-liquid/releases/20261028-190036-2yuia575`。
- 插件 SHA256：`02072d9f9fe6bf255bb3c8443d059992eb4cc5460aeee93d7bd9003f04a7863d`。
- 重启：旧面板 PID 27688（上次恢复时脱离会话启动）以 SIGTERM 正常退出，使用其原环境运行 `~/.local/bin/ukui-panel-liquid-session`，新面板 PID 52209 已加载上述插件（映射 inode 与安装文件一致），仅一个面板实例。
- Fences 与开始菜单未重新部署，仍为旧公共弹窗代码。
- 尚未在实机用 Clash 实际切换代理复验。

## 未覆盖与建议

- 未在用户实机复现原始“无限持续”时长；隔离夹具中单次重建的抖动有限，实机更大的菜单、2880×1800 分辨率与更多刷新来源会放大持续时间。修复针对的是每一步的放大成本与错误定位，OEM 打开时重建菜单的行为本身无法在插件内消除。
- clash-pykylin 本身建议改为用 `aboutToShow`/`aboutToHide` 维护“菜单已打开”标志，而不是 `QMenu.isVisible()`，避免菜单打开时重建；本次未改该应用。
