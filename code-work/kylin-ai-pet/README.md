# Kylin AI 宠物

Kylin-LQUI 任务栏助手的 Electron 组件，提供克劳德大小姐、大肥鱼、GPT 白龙和 Gemini 星空猫。模型与会话由同一分支的 AI 小站提供，桌宠通过本地 D-Bus 打开原生聊天窗口。

## 本地安装

在 UKUI/X11 上先构建并安装本分支的 [任务栏](../ukui-liquid-desktop/apps/ukui-panel-liquid) 和 [AI 小站](../liquid-cli)。桌宠运行需要 Node.js 18 或以上版本、npm、系统 Python 3 的 PyQt5 和 Gio 绑定，以及 X11、Xfixes、Xext、wmctrl 和 gdbus。

在本组件目录执行：

```sh
npm ci
npm run check
python3 scripts/install-local.py
```

桌面会话的 PATH 需包含 `~/.local/bin`。安装程序在这里写入启动器，在当前用户的应用菜单中注册 `kylin-ai-pet.desktop`，并把角色资源链接到用户数据目录。保留此检出目录；移动后重新运行注册命令。

在 **Kylin-LQUI 设置 → 任务栏 AI 助手 → 入口模式** 选择 **AI 宠物**，再选择形象。单击任务栏按钮呼出或收回宠物，双击开关入口上方的聊天窗口。把文字、图片、文件或文件夹拖到宠物身上，会显示小站对应的操作菜单。宠物右键菜单提供截图和「回家」。开始菜单麒麟的随机动作由面板插件内置，独立于 Electron。

GPT 白龙和 Gemini 星空猫各有 18 段额外的透明动画。右键 **动作** 可选招牌、才艺、日常和点击回应。空闲时会抽取动作，思考、忙碌和庆祝也有对应片段。视频在使用时读取，播放完回到待机。

动画和角色素材已放在本组件内。来源、修改及各自的授权见 [ASSETS_LICENSE.md](ASSETS_LICENSE.md)。重新生成悬停素材可运行 `python3 tools/build_taskbar_greetings.py`，需要 Pillow 和支持 libvpx-vp9 的 ffmpeg。
