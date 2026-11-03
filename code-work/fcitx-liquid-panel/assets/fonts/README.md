# 候选字体支持

本目录保存已验证的 Noto Color Emoji 来源清单及 OFL-1.1 许可证，不保存字体二进制或用户词库。

`noto-emoji.json` 固定到 Google Fonts 上游提交和 SHA-256，使用 `2D/fonts/NotoColorEmoji.ttf`，与本机 Qt 5 / FreeType 路径一致。字体可以按需下载，也可使用同一哈希的离线文件；安装前校验大小、格式、SHA-256 和必要字符覆盖。

字体安装到用户数据目录。只对缺少必需表情的旧版同名字体生成 fontconfig 排除规则，系统字体文件保留。Qt 5 仍可能注册旧的同名字体，所以只加装新版而没有排除旧副本并不足够。

生僻字 U+31342 的补充规则只引用已经安装并覆盖该字符的字体，不分发或下载 SimSun 等微软字体。已有用户规则不一致时停止，原配置保留。普通候选字体和 Rime 词库不改写。

来源与许可证：

- [Noto Emoji 官方仓库](https://github.com/googlefonts/noto-emoji)
- [本次固定版本](https://github.com/googlefonts/noto-emoji/tree/1ffdd21391dd1f25c081fa93a9dea0c7c029442b)
- [许可证](OFL-NotoEmoji.txt)

使用方法见 [字体修复与验证](../../../../docs/RIME_FONT_FIX_20261103.md)。
