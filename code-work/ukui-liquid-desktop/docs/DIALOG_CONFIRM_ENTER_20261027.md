# 确认消息框的回车焦点修复

日期：2026-10-27。基线 `78c5339`。

## 问题与修复

公共 `LiquidDialog::createMessage` 将消息按钮设成 `autoDefault=false`。因此即使 Tab 已把焦点移到“确定”，Return/Enter 仍会激活原来的默认“取消”；空格则走当前按钮。旧版定向回归可复现：焦点为 Yes，Return 返回 No（65536）。

恢复 QPushButton 的 `autoDefault` 机制，让默认回车动作随焦点移动。消息框创建时仍设置调用者指定的初始默认值和焦点；回收站、永久删除等默认取消的逻辑不变。Esc 和标题关闭也仍走取消。未增加全局快捷键、轮询或额外绘制。

修改位于 `shared/liquid-popup/src/LiquidDialog.cpp`，所以主仓库内共用此模块的确认消息框会在重新构建时获得修复。此次实际部署的是 Fences；没有重启任务栏、开始菜单或独立 AI空间。

## 验证

- 新增 `message_keyboard_test.cpp`：真实消息框、实际 Tab 焦点链和按键事件，验证主回车、小键盘回车、空格、Esc、Yes/No、Ok/Cancel，以及调用者显式默认 Yes 后改选 No。
- 相同场景分别在减少动画与完整开关动画下执行；覆盖 Fusion、UKUI、关闭液态适配和独立 Xvfb 原生窗口。
- 既有对话框与按钮四档缩放、UKUI 样式、关闭效果、动画及最小化回归继续通过。公共模块共 17 个相关 CTest 入口通过；原生焦点检查按对话框的逻辑 `focusWidget()` 判断，避免无窗口管理器时将逻辑焦点误判为无焦点。
- Fences 的 `desktop_permanent_delete` 扩展为实际键盘确认：默认回车取消、Tab 到确定后用 Return/Enter/Space 删除分区内临时文件，再用 Ctrl+Z 检查恢复后的原始内容；桌面入口同样验证 Return 删除与内容恢复。
- `desktop_trash_undo`、`desktop_folder_drop_undo`、`desktop_clipboard_async` 与扩展后的 `desktop_permanent_delete` 四项通过。文件操作只发生在自动创建的临时 HOME/桌面/回收站中。

公共模块证据在 `apps/ukui-panel-liquid/build/message-keyboard-before.log`、`message-keyboard-after.log` 和 `message-keyboard-recheck.log`。Fences 证据在 `apps/ukui-fences/build/message-keyboard-files.log` 与 `message-keyboard-files-recheck.log`；前者第一次运行中的焦点断言已在后者按逻辑焦点复核通过，未放宽文件内容与动作结果断言。

## 部署

Release 主程序构建通过。旧二进制及配置备份在 `apps/ukui-fences/releases/20261027-confirm-enter-ig0r5gjh/`，新二进制 SHA-256 为 `fea8fd1bb54462b4d5201f5e730f4bc2d390377099e28fe082d18c0f4105056a`。

使用正常 `quitApp` 退出后以 `--autostart` 重启；部署前后状态和进程核验记录保存在 `apps/ukui-fences/build/artifacts/message-keyboard-20261027/`。尚未提交或推送 Git。
