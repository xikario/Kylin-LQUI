# 音量滑条试听反馈（2026-11-03）
> 后续已改用直接 paplay 播放并完成实机鼠标演示，用户确认两处均可听到。当前实现与验证见 CONTROL_CENTER_VOLUME_FEEDBACK_RECHECK_20261103.md；下文保留初版记录。

用户要求控制中心首页和声音详情页的输出音量滑条调整时有声音反馈。

## 行为

- 两处共用音量操作完成回调。点击滑轨、键盘调整或拖动松开提交成功后，播放系统的音量提示音；拖动中的每一帧不播放。
- 快速连续调整只对最后一次成功提交反馈；80 ms 合并窗口，新的操作会中止上一次试听。自动状态刷新、麦克风调节、失败操作不触发试听。
- 静音或音量 0 不播放；关闭面板取消待播和正在播放的试听。
- 音量操作只确认最新音频状态，不再等待 Wi-Fi、电源等整组服务读取完成后才反馈。

## 播放

使用独立、限时的 Python 子进程调用系统 libcanberra 的 PulseAudio 后端，按已确认的 sink ID 播放，不在 GUI 线程连接或等待音频服务。优先使用 org.ukui.sound 中 audio-volume-change 对应的文件，缺失时回退系统 freedesktop 提示音。当前主题音长约 0.72 秒。

不强制设置提示音流的增益，不解除静音，不修改输出设备或音量。子进程最多等待播放完成 4 秒，GUI 侧另设 4.5 秒退出上限，避免音频服务异常留下进程。

API 依据：[libcanberra 官方参考](https://0pointer.de/lennart/projects/libcanberra/gtkdoc/libcanberra-canberra.html)。完成回调仅设置通知对象，不在回调内调用库函数。

## 验证和部署

75 项测试通过，新增两页成功后反馈、失败/隐藏/麦克风不反馈、连续操作只播末次、静音/零音量保持安静及快速音频确认测试。

独立 PulseAudio 的虚拟设备测试加载 stream-restore，分别在 flat-volumes=no/yes、25%/60% 下播放并采样虚拟 monitor，四种情况均存在非零音频，较高音量对应较强信号。全程监测输出音量，没有被提示音抬高。不加载真实声卡，不录制真实麦克风。可用 `dbus-run-session -- /usr/bin/python3 code-work/ukui-liquid-desktop/apps/kylin-lqui-settings/tests/test_volume_playback.py` 复现。

已安装并重启控制中心，包含新 volume_feedback.py；源与安装文件哈希一致。本机在原有 38% 音量播放了一次系统提示音，播放完成返回成功，期间输出音量始终 38%，输出/输入设备、音量及静音状态前后保持一致。验证摘要见 `docs/evidence/20261103-volume-feedback/verification.json`。
