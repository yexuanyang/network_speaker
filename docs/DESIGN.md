# DESIGN

本文档说明 `network_speaker` 当前的整体架构、数据流、关键协议设计与稳定性策略。

## 目标

项目目标是把桌面端当前播放的音频以尽可能低的延迟转发到 Android 设备，让 Android 设备充当网络扬声器。

当前主目标路径是：

- Windows 发送端
- Android 接收端
- 局域网 UDP 传输
- Opus 低延迟编码

## 高层架构

系统可以分成三层：

1. 采集与播放平台层
2. 音频编解码与传输层
3. 终端应用层

对应模块如下：

- `server/hostd`
  - 负责采集音频、编码、发包
- `client/core`
  - 负责收包、抖动缓冲、解码、播放回调
- `client/android-app`
  - 负责 Android 生命周期、前台服务、`AudioTrack`
- `libs/transport`
  - 负责 UDP 包格式、socket、jitter buffer
- `libs/codec_opus`
  - 负责 Opus 编解码封装
- `libs/audio_base`
  - 负责公共 PCM 类型、时钟和统计结构

## 发送端数据流

发送端主路径：

1. 平台采集模块生成 `PcmFrame`
2. `OpusEncoder` 编码为 Opus payload
3. `UdpAudioSender` 组装 `AudioPacket`
4. 通过 UDP 发往目标地址

当前采集源包括：

- `sine`
  - 直接生成测试蜂鸣声
- `pulse`
  - Linux `PulseAudio` monitor source
- `wasapi`
  - Windows 默认渲染设备 `WASAPI loopback`

## 接收端数据流

接收端主路径：

1. `Receiver` 从 UDP socket 收包
2. `TryParsePacket()` 解析为 `AudioPacket`
3. `PlayerPipeline` 推入 `JitterBuffer`
4. `PlayerPipeline` 在可播放时序下弹出包
5. `OpusDecoder` 解码回 PCM
6. 音频 sink 消费 PCM

在桌面端：

- sink 可以是测试用的内存/回调 sink

在 Android 端：

- JNI 回调到 Kotlin
- Kotlin `AudioOutput` 把 PCM 写入 `AudioTrack`

## 音频格式

内部统一 PCM 格式：

- `48 kHz`
- `2 channels`
- `float32`
- 默认帧长 `10 ms`

统一格式的目的：

- 降低平台差异
- 简化 Opus 编解码输入输出
- 简化 Android `AudioTrack` 播放路径

## 包格式设计

`AudioPacketHeader` 当前字段包括：

- `magic`
- `version`
- `flags`
- `stream_id`
- `sequence`
- `capture_ts_us`
- `frame_samples`
- `payload_size`

其中最关键的两个字段是：

- `stream_id`
  - 标识一轮发送会话
  - 发送端每次 `hostd` 启动都会生成新的 `stream_id`
- `sequence`
  - 同一条流内严格递增的包序列号

## `stream_id` 与 `sequence` 的语义

这两个字段是为了区分两类问题：

### 发送端重启

如果 `hostd` 重启，`sequence` 通常会重新从较小值开始。此时客户端不能继续沿用上一轮流的 `expected_sequence`。

当前策略：

- 如果收到了新的 `stream_id`
- 则 `PlayerPipeline` 认为这是新的发送会话
- 重置：
  - `JitterBuffer`
  - Opus 解码器状态
  - `expected_sequence`

### 同一条流中的偶发丢包

如果只是局域网中的单包丢失，不应该要求用户手动重连。

当前策略：

- `JitterBuffer` 负责缓存乱序包
- 如果 `expected_sequence` 对应的包一直没到
- 但缓冲中已经积累了更高序列号的数据，且缓冲重新达到目标深度
- 则播放器会：
  - 认为缺失包已经丢失
  - 把缺失数累加到 `packets_lost`
  - 直接把播放位置跳到当前最老的可用包

这样可以避免“第一次丢包后永久静音”的问题。

## 抖动缓冲设计

`JitterBuffer` 当前是一个基于 `std::map` 的按序缓存。

能力：

- 接收乱序包
- 拒绝重复包
- 拒绝过晚包
- 拒绝超出窗口范围的未来包

当前窗口参数：

- `target_packets`
  - 目标预缓冲深度
- `max_window`
  - 最大可接受乱序窗口

局限：

- 还没有自适应 jitter target
- 还没有 FEC
- 还没有时间驱动的 PLC 或补帧策略

## Android 端设计

Android 端分成两层：

- `SpeakerService`
  - 前台服务
  - 控制 native 接收端启动/停止
  - 发布状态广播
- `AudioOutput`
  - 封装 `AudioTrack`
  - 在收到 PCM 时阻塞写入

JNI 桥负责：

- 启动 `ClientSession`
- 把 native PCM 回调交给 Kotlin
- 在 native 启动失败时把失败状态返回上层

## Windows 端设计

Windows 端的关键点是 `WASAPI loopback`。

当前支持：

- `console`
- `multimedia`
- `communications`
- `auto`

其中：

- 浏览器视频通常更适合 `multimedia`
- `auto` 当前会优先尝试：
  - `multimedia`
  - `console`
  - `communications`

## 当前为什么模拟器更稳定、真机更容易暴露问题

模拟器链路通常是：

- Windows 本机
- `adb emu redir`
- 本机回环式网络路径

这个路径的 UDP 丢包概率很低。

真机链路通常是：

- Windows 主机
- Wi-Fi 局域网
- Android 真机

这个路径上更容易出现：

- 短时丢包
- 乱序
- Wi-Fi 抖动
- 省电/后台调度影响

因此接收端的丢包恢复策略在真机上更关键。

## 当前已知边界

- 还没有握手控制信道
- 还没有加密
- 还没有 FEC
- 还没有多声道
- Android 端保活策略仍有待继续加强

## 相关文档

- 用户说明：[../README.md](../README.md)
- 开发参与：[CONTRIBUTE.md](CONTRIBUTE.md)
- 阶段进展：[../progress.md](../progress.md)
- 调试记录：[../debug.md](../debug.md)
