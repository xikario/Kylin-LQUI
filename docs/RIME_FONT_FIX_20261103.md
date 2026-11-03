# 2026-11-03 Rime 表情缺字修复归仓

来源聊天 `01a19c69-2947-7e61-ab9a-b5d413065eea`（修复 RIME 乱码）于 11 月 2 日完成了本机字体修复。该改动原来只存在用户字体目录和 fontconfig 配置里，没有仓库源码变更。本轮将其整理为可重复执行、可离线验证的用户级工具，再提交到统一仓库。

## 原因与处理

- 系统旧版 Noto Color Emoji 缺少 U+1FAE0（🫠）和 U+1FAE5（🫥）。仅加装新版仍可能被 Qt 5 注册的同名旧副本遮蔽，需要在用户级 fontconfig 中排除缺字的旧副本。
- 另一候选 `𱍂`（U+31342）已有字体覆盖，但当前中文主字体的自动回退没有选中。为配置中的中文字体追加已安装扩展字体的强绑定回退即可，普通中文字体保留。
- 不改 Rime 字典、编码、输入法引擎或候选窗绘制代码。

## 可复用工具

依赖 Python 3、PyQt5 及 fontconfig 的 `fc-list`、`fc-query`、`fc-cache`。

```bash
# 只读检查：按当前中文字体逐个整串排版，检查真实 glyph index。
/usr/bin/python3 code-work/fcitx-liquid-panel/font_support.py check

# 用户级安装；联网获取固定版本，禁止未校验内容落地。
/usr/bin/python3 code-work/fcitx-liquid-panel/font_support.py install

# 离线安装同一哈希的文件。
/usr/bin/python3 code-work/fcitx-liquid-panel/font_support.py install --emoji-font /path/to/NotoColorEmoji.ttf

# 可显式选择当前字体、已安装的扩展回退字体或待检查字典。
/usr/bin/python3 code-work/fcitx-liquid-panel/font_support.py check --font-family 'Noto Sans CJK SC' --dictionary /path/to/emoji.txt
```

工具读取 `Fonts/ChineseFamily`，支持 XDG 用户目录。默认检查 `emoji.txt`、`others.txt` 和五个基础样例；缺字返回非零退出码，只输出缺字码位，不输出个人词库正文。每次检查创建新的 Qt 进程，避免缓存掩盖字体变更。

安装版本固定为 Noto 上游提交 `1ffdd21391dd1f25c081fa93a9dea0c7c029442b`；字体 10,730,124 字节，SHA-256 为 `15671215ab769fdc7162a045d56fd7d7e477c51b04e6b3c761d914d8fdd6cc44`。来源清单及 OFL-1.1 许可证在 `code-work/fcitx-liquid-panel/assets/fonts`；字体二进制不纳入仓库。工具不下载或分发微软字体：扩展回退只使用本机已安装且覆盖 U+31342 的字体，缺少覆盖时由检查报告。

同名自定义规则、符号链接规则或不同版本的目标字体不会被覆盖；语义相同的原规则保留原文。写入采用现有 FileTransaction，失败恢复原文件。重复安装无写入、无重复刷新缓存。前端安装器会将工具、来源清单、许可证和事务帮助模块一起带入新发布目录；普通前端安装不会自动下载字体或改 fontconfig。

新字体需要由新启动的候选前端重新读取。工具只提示需要刷新，没有自动重启候选窗、Fcitx、Rime 或 KWin；不得以重启引擎代替前端字体缓存刷新。本机来源聊天已经完成候选前端刷新，本次无需再次安装或重启。

## 本轮验证

- 5 项安装测试通过：损坏字体拒绝落地、自定义规则保留、缓存刷新失败恢复旧文件、离线安装幂等，以及字体名 XML 转义/旧规则兼容。已接入 `candidate-font-support` CTest。
- 真实已验证字体在隔离 XDG 目录完成离线安装，重复安装写入数为 0；独立打包的工具与事务帮助模块可直接运行。
- 隔离环境以新 Qt 进程排版 `消失`、🫠、🫥、😀、𱍂，均没有 glyph 0。
- 本机当前配置重新检查两份词库共 **9,481 个不同候选**，未发现缺字。检查使用 QTextLayout 整串排版，避免仅用单码位字体度量漏掉序列回退问题。
- 工作区/Python 语法与 Git 空白检查通过。证据只含计数、码位、来源哈希与测试摘要，没有提交用户词库、截图或字体二进制。

检查结论仅覆盖当前字体/Qt/词库组合，不代表所有 Unicode 版本、所有语言或其他机器均已覆盖。
