# 第三方来源与许可

仓库主体沿用 Fences 已声明的 GPL-3.0-or-later。根目录 LICENSE 提供 GPL v3 正文；第三方子目录的原有声明、作者署名与许可优先适用于对应文件。

| 来源 | 使用范围 | 许可与定位 |
| --- | --- | --- |
| [xikario/NextKde-kylinos](https://github.com/xikario/NextKde-kylinos/tree/0dcd60cbd8723a6da95349a82fbc63385081c22a) | `glass.glsl`、`snells-glass.glsl`，原路径 `vendor/kwin-effects-glass/src/shaders/`；本项目用 Qt5 适配调用 | GPL v3；`shared/liquid-glass/vendor/nextkde-glass/LICENSE` 及两个应用各自 vendor 副本 |
| [SuceV587/NextKde](https://github.com/SuceV587/NextKde/tree/4aeb2be2e20979aa01be5899df0a8fcbf786a5cc) | DeskCenter 时钟、活动、日历设计参考；本地 Qt5 实现 | GPL v3；`apps/ukui-fences/vendor/nextkde-desklets/` |
| [NextKde/NextKde](https://github.com/NextKde/NextKde/tree/3fa1d7006869bbd3404a05d362c6e6028e0fe3da) | `shell/desktop/modules/dock/AdaptiveMath.mjs`、`DockMagnification.mjs` 的比例间距、自适应尺寸与悬停波形适配到 `apps/ukui-panel-liquid/src/DockLayoutMath.h`；原作者 NextKde contributors | GPL v3；本仓库根目录 `LICENSE` |
| 同上 NextKde 固定版本 | 参考 `AppearanceConfigService.qml` 的三类材质、`LiquidGlassSurface.qml` 的薄反光与 `kwin/glass-effect/src/shaders/glass.glsl` 的边缘光设计；在公共渲染器增加显式启用的任务栏材质 | GPL v3；保留现有 shader vendor 署名及许可证 |
| [NateScarlet/holiday-cn](https://github.com/NateScarlet/holiday-cn) | `apps/ukui-fences/scripts/china_holidays_2026.json`，含放假与调休工作日 | MIT；`apps/ukui-fences/vendor/holiday-cn/LICENSE` |
| [Groove Index](https://github.com/NainaKothari-14/groove-index) | 音乐播放时音符上浮、旋转、淡出的动效设计参考 | 未复制其代码、素材或网页运行时；本项目以 Qt QPainter 绘制矢量音符 |

NextKde shader 在公共渲染器、开始菜单与 Fences 既有渲染器中保留副本，暂未合并全部实现。不要删除任一 vendor 的许可证。

Qt、X11、Zlib、ICU、UKUI 系统面板、KWin 和 Strawberry 是外部依赖，本仓库不打包它们的二进制、SDK 或系统资源。AppStream 元数据保留其 CC0-1.0 声明。

## Hong Kong Observatory calendar reference

`apps/ukui-fences/scripts/lunar_hko_reference.json` contains factual lunar month boundaries and solar-term dates derived from the [Hong Kong Observatory conversion tables](https://www.hko.gov.hk/tc/gts/time/conversion.htm). The bundled verified coverage is stated in the JSON range (2020–2035); dates outside it use the existing ICU calendar. Source-table SHA-256 values and a manual regeneration tool are included. These are calendar facts, not mainland public-holiday or compensating-workday declarations.
