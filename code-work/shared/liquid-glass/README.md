# 公共光学材质

从本地已验证的 FenceGlassRenderer 提取，源自开始菜单 NextKdeGlassView 适配器。Shader 为 xikario/NextKde-kylinos commit 0dcd60cbd8723a6da95349a82fbc63385081c22a 中的 glass.glsl、snells-glass.glsl 原样副本；GPL v3 许可证保留在 vendor/nextkde-glass/LICENSE。

共享层负责扩散背景、Snell 折射、色散、边缘反光和焦散；独立适配参数支持窄面板，调用方负责提供无 UI 内容的背景图像并缓存结果。不能获取实时合成器背景，CPU 降级只有模糊/染色，无真实 Snell 折射。

当前由 ukui-panel-liquid 使用。开始菜单和 Fences 既有渲染器未在此次迁移，保持部署版本稳定；本模块保留相同来源和着色器。
