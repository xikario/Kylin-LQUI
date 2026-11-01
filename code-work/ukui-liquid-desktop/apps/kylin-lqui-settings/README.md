# Kylin-LQUI 统一设置

独立 PyQt5 程序，左侧分类/搜索，右侧原生卡片控件。无需 WebEngine、网页服务或常驻设置守护进程。

配置仍由所属模块写入：模块导出 `org.kylin.Lqui.SettingsModule`，中心调用 Catalog / Page / Set / Activate。每次变更均经过原控件的校验与回调，不维护另一套互相覆盖的配置文件。模块未运行时可显式启动；AI 空间的 settings-service 模式不显示主窗、不载入索引、不开始建库。

覆盖 Fences 全部页面及动态分区、任务栏三个页面、开始菜单、AI 小站、输入法五个分类和预览、AI 空间七个分类、Peony 全局设置/终端配置/扩展管理。原入口自动定位中央页面。`LQUI_SETTINGS_LEGACY=1` 仅供诊断和隔离回归测试保留旧入口。

总览分别展示进程、PSS、登录启动与模块配置；Fences 提供各小组件实时启用状态。设置页后台每 2.5 秒校对当前页；总览每 6 秒刷新，最小化时停止查询。编辑中的字段不会被轮询覆盖。

## 安装

本机依赖 `/usr/bin/python3`、PyQt5 Widgets/DBus、PyGObject，继续使用已有的 UKUI 服务。执行 `install.py` 生成用户目录版本，原子切换 current，并记录文件 SHA-256 和 previous。无需 root。其他模块必须更新到带 SettingsBridge 的版本。

## 检查

- `tests/test_ui.py`：卡片、控件调用、草稿、窄屏、搜索与状态。
- `tests/test_modules.py`：在 Xvfb/独立 D-Bus/临时 HOME 中启动五个真实组件，读取全部分类并切换子页；不调用真实模型。
- `shared/settings/tests`：值范围、列表、秘密字段、标签页、原生回调和隐藏表单。
- Peony 安装/卸载仅在点击后调用现有校验/helper；不自动重启 Peony 或关闭文件窗口。

尚需单独完成原生登录会话、共享采样/全局预设及完整控制中心的后续集成。将设置合并并不表示已经替换这些系统服务。
