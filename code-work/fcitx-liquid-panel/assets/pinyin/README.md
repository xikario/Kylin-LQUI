# 离线声调数据

本目录内嵌上游 MIT 许可的原始数据，编译进 Qt 资源，无运行时 Python 或网络依赖。

- `characters.txt`：mozillazg/pinyin-data，单字、多音字，版本 0.15.0。
- `phrases.txt`：mozillazg/phrase-pinyin-data，词组优先读音，版本 0.19.0。
- 精确提交在对应 `*-source.json`；保留各自 `*-LICENSE` 的版权及许可全文。

启动时读取一次；候选注音先匹配最长词组，再匹配单字，并要求无调拼音与 Rime
提供的完整读音一致。不会根据缩写猜音。缺失条目或不匹配部分保留 Rime 注释。
轻声无调号；字典不覆盖的专名以及同音多调读音可能仍有误差。
不修改 Rime 词典、候选顺序或用户学习数据，不缓存用户候选文本。
