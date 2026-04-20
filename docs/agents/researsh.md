# Sunshine 音频传输到 Moonlight 的实现调研

## 结论

Sunshine 将声音传输到 Moonlight，不是把音频复用进视频流，而是单独维护一条 GameStream 兼容的音频链路：

`系统音频采集 -> 48 kHz float PCM -> Opus multistream 编码 -> RTP/UDP 音频流 -> 可选 AES-CBC 加密 -> Reed-Solomon FEC -> Moonlight`

核心源码路径：

- [src/nvhttp.cpp](/home/yyx/workspaces/Sunshine/src/nvhttp.cpp)
- [src/rtsp.cpp](/home/yyx/workspaces/Sunshine/src/rtsp.cpp)
- [src/audio.cpp](/home/yyx/workspaces/Sunshine/src/audio.cpp)
- [src/stream.cpp](/home/yyx/workspaces/Sunshine/src/stream.cpp)
- 平台相关采集实现：
  - [src/platform/linux/audio.cpp](/home/yyx/workspaces/Sunshine/src/platform/linux/audio.cpp)
  - [src/platform/windows/audio.cpp](/home/yyx/workspaces/Sunshine/src/platform/windows/audio.cpp)
  - [src/platform/macos/microphone.mm](/home/yyx/workspaces/Sunshine/src/platform/macos/microphone.mm)

## 1. 会话建立阶段：先从 `/launch` 拿到音频会话信息

Moonlight 在调用 Sunshine 的 `/launch` 或 `/resume` 时，会把音频相关参数带上来。Sunshine 在 `make_launch_session()` 中保存这些信息，包括：

- `host_audio`
- `surround_info`
- `surround_params`
- `continuous_audio`
- `rikey`
- `rikeyid`
- `av_ping_payload`
- `control_connect_data`

对应代码：

- [src/nvhttp.cpp:285](/home/yyx/workspaces/Sunshine/src/nvhttp.cpp#L285)

这里有两个后续非常关键的字段：

- `av_ping_payload`：后面音频/视频 UDP 通道用它来确认客户端是哪一个会话
- `rikey` / `rikeyid`：后面音频负载加密会用到

## 2. RTSP 阶段：协商音频能力和参数

Sunshine 的 RTSP 主要负责“谈参数”，不是直接传输音频数据。

### 2.1 `DESCRIBE`：告诉 Moonlight 支持哪些音频布局

在 `cmd_describe()` 中，Sunshine 会构造 SDP 风格的返回内容，把它支持的音频布局通过 `a=fmtp:97 surround-params=...` 广播给 Moonlight。

这里包含：

- 声道数
- Opus multistream 的 `streams`
- `coupledStreams`
- 声道映射

对应代码：

- [src/rtsp.cpp:753](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L753)
- [src/rtsp.cpp:800](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L800)
- [src/rtsp.cpp:806](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L806)

实现上还有一个兼容 Nvidia GFE / Moonlight 的细节：

- 对普通质量的 5.1 / 7.1 映射做了旋转修正，以适配 Moonlight 对 Nvidia 历史行为的兼容逻辑

对应代码：

- [src/rtsp.cpp:812](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L812)

### 2.2 `SETUP`：分配音频 UDP 端口

在 `cmd_setup()` 中，如果客户端请求的是 `audio`，Sunshine 会返回音频流端口：

- `AUDIO_STREAM_PORT = 11`

同时在 RTSP 返回头里带上：

- `X-SS-Ping-Payload`

这个 payload 后面会被 Moonlight 回显到 UDP PING 包中，Sunshine 依靠它识别“这个 UDP 包属于哪个会话”。

对应代码：

- [src/stream.h:19](/home/yyx/workspaces/Sunshine/src/stream.h#L19)
- [src/rtsp.cpp:836](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L836)
- [src/rtsp.cpp:855](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L855)
- [src/rtsp.cpp:880](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L880)

### 2.3 `ANNOUNCE`：读取 Moonlight 请求的音频参数

在 `cmd_announce()` 中，Sunshine 解析 Moonlight 发来的音频参数，主要包括：

- `x-nv-audio.surround.numChannels`
- `x-nv-audio.surround.channelMask`
- `x-nv-aqos.packetDuration`
- `x-nv-audio.surround.AudioQuality`
- `x-nv-aqos.qosTrafficType`
- `x-ss-general.encryptionEnabled`

对应代码：

- [src/rtsp.cpp:895](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L895)
- [src/rtsp.cpp:967](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L967)

另外还有几个重要逻辑：

- 立体声时，高低质量有一段兼容 Moonlight 的特殊判断
- 如果 launch 阶段已有 `surround_params`，则会构造自定义 multistream 映射
- 如果客户端请求 `continuous_audio`，Sunshine 会启用连续音频模式
- 如果策略要求强制加密，Sunshine 会检查音频/视频加密能力是否满足

对应代码：

- [src/rtsp.cpp:1008](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L1008)
- [src/rtsp.cpp:1019](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L1019)
- [src/rtsp.cpp:1042](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L1042)
- [src/rtsp.cpp:1087](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L1087)

## 3. 真正开始音频传输前：等待 Moonlight 的 UDP PING

RTSP 结束后，Sunshine 不会立即推音频，而是先等客户端在音频端口上发来 PING。

整体流程：

1. Sunshine 创建广播上下文，绑定音频 UDP socket
2. 音频线程调用 `recv_ping()`
3. 收到带 `X-SS-Ping-Payload` 的 UDP 包后，确认 Moonlight 实际的音频 `IP:port`
4. 之后才开始采集和发送音频

对应代码：

- [src/stream.cpp:1700](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1700)
- [src/stream.cpp:1801](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1801)
- [src/stream.cpp:1876](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1876)

这里说明 Sunshine 的音频传输是典型的单独 UDP 媒体流，而不是 RTSP over TCP 推送。

## 4. 音频采集：平台相关实现，统一输出 float PCM

上层统一接口是：

- `platf::audio_control_t`
- `platf::mic_t`

调用关系是：

1. `audio::capture()` 选择 sink
2. 调用 `audio_control->microphone(...)`
3. 得到平台相关 `mic_t`
4. 不断调用 `mic->sample()` 读取一帧 float PCM

对应代码：

- [src/platform/common.h:563](/home/yyx/workspaces/Sunshine/src/platform/common.h#L563)
- [src/audio.cpp:131](/home/yyx/workspaces/Sunshine/src/audio.cpp#L131)
- [src/audio.cpp:196](/home/yyx/workspaces/Sunshine/src/audio.cpp#L196)

### 4.1 Linux：PulseAudio monitor source 回环采集

Linux 实现在 PulseAudio 上：

- 先定位 sink
- 再找这个 sink 对应的 `monitor_source_name`
- 最后通过 `pa_simple_new(..., PA_STREAM_RECORD, monitor_source_name, ...)` 把系统播放音频录出来

对应代码：

- [src/platform/linux/audio.cpp:71](/home/yyx/workspaces/Sunshine/src/platform/linux/audio.cpp#L71)
- [src/platform/linux/audio.cpp:413](/home/yyx/workspaces/Sunshine/src/platform/linux/audio.cpp#L413)
- [src/platform/linux/audio.cpp:444](/home/yyx/workspaces/Sunshine/src/platform/linux/audio.cpp#L444)

如果配置允许，Sunshine 还会把默认输出设备切到自己管理的虚拟 sink，再去录这个虚拟 sink 的 monitor source。这样可以支持：

- 不在主机本地播放声音
- 更稳定地输出 2.0 / 5.1 / 7.1

对应代码：

- [src/audio.cpp:160](/home/yyx/workspaces/Sunshine/src/audio.cpp#L160)
- [src/platform/linux/audio.cpp:468](/home/yyx/workspaces/Sunshine/src/platform/linux/audio.cpp#L468)

### 4.2 Windows：WASAPI loopback

Windows 实现直接使用 WASAPI loopback 抓默认渲染设备输出：

- `AUDCLNT_STREAMFLAGS_LOOPBACK`
- `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`
- `AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY`

对应代码：

- [src/platform/windows/audio.cpp:318](/home/yyx/workspaces/Sunshine/src/platform/windows/audio.cpp#L318)

Windows 路径的特点：

- 默认抓系统播放输出，而不是麦克风输入
- 如果请求的是虚拟 sink，Sunshine 还会尝试把默认输出设备切到虚拟设备
- 支持设备切换后自动重新初始化

对应代码：

- [src/platform/windows/audio.cpp:427](/home/yyx/workspaces/Sunshine/src/platform/windows/audio.cpp#L427)
- [src/platform/windows/audio.cpp:770](/home/yyx/workspaces/Sunshine/src/platform/windows/audio.cpp#L770)
- [src/platform/windows/audio.cpp:857](/home/yyx/workspaces/Sunshine/src/platform/windows/audio.cpp#L857)

### 4.3 macOS：system audio tap 或指定输入设备

macOS 默认优先使用 system-wide audio tap：

- 如果 `config::audio.sink` 为空，则调用 `setupSystemTap()`
- 如果配置了 `audio.sink`，则按“某个输入设备/虚拟麦克风”去采集

对应代码：

- [src/platform/macos/microphone.mm:66](/home/yyx/workspaces/Sunshine/src/platform/macos/microphone.mm#L66)
- [src/platform/macos/microphone.mm:74](/home/yyx/workspaces/Sunshine/src/platform/macos/microphone.mm#L74)

所以 macOS 上的系统声音抓取方式和 Linux / Windows 不同，不是统一的 loopback sink 设计。

## 5. 音频编码：使用 Opus multistream

真正的音频编码逻辑在 `src/audio.cpp`。

### 5.1 固定输出参数

Sunshine 的音频编码固定在：

- 采样率：`48000`
- 输入格式：`float`
- 编码器：`OpusMSEncoder`
- 模式：`OPUS_APPLICATION_RESTRICTED_LOWDELAY`
- VBR：关闭

对应代码：

- [src/audio.cpp:31](/home/yyx/workspaces/Sunshine/src/audio.cpp#L31)
- [src/audio.cpp:97](/home/yyx/workspaces/Sunshine/src/audio.cpp#L97)
- [src/audio.cpp:107](/home/yyx/workspaces/Sunshine/src/audio.cpp#L107)

### 5.2 支持的音频布局

内置支持：

- 2 声道
- 5.1
- 7.1
- 每种布局都有普通质量和高质量两档

对应代码：

- [src/audio.cpp:35](/home/yyx/workspaces/Sunshine/src/audio.cpp#L35)

### 5.3 编码线程模型

`audio::capture()` 负责采样，`encodeThread()` 负责编码：

1. 采集线程从平台后端读取 float PCM
2. 样本进入队列
3. 编码线程调用 `opus_multistream_encode_float()`
4. 编码结果写入 `mail::audio_packets`

对应代码：

- [src/audio.cpp:86](/home/yyx/workspaces/Sunshine/src/audio.cpp#L86)
- [src/audio.cpp:114](/home/yyx/workspaces/Sunshine/src/audio.cpp#L114)
- [src/audio.cpp:210](/home/yyx/workspaces/Sunshine/src/audio.cpp#L210)

## 6. 音频发包：独立 RTP 音频流

`audioBroadcastThread()` 负责把编码后的 Opus 数据发给 Moonlight。

关键点：

- 使用单独的音频 UDP socket
- RTP `packetType = 97`
- 每个音频包有独立的 `sequenceNumber`
- 每个音频包有独立的 `timestamp`

对应代码：

- [src/stream.cpp:1595](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1595)
- [src/stream.cpp:1611](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1611)
- [src/stream.cpp:1642](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1642)

时间戳不是视频时钟，而是按音频包时长递增：

- `timestamp += packetDuration`

对应代码：

- [src/stream.cpp:1645](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1645)

## 7. 音频加密：在发包前对 Opus 负载做 AES-CBC

音频加密和视频加密是分开的。

会话创建时，Sunshine 为音频准备 `CBC` cipher：

- key 来自 `rikey`
- `avRiKeyId` 来自 `rikeyid`

对应代码：

- [src/stream.cpp:2005](/home/yyx/workspaces/Sunshine/src/stream.cpp#L2005)
- [src/stream.cpp:2059](/home/yyx/workspaces/Sunshine/src/stream.cpp#L2059)

发包前：

- 如果 `SS_ENC_AUDIO` 开启
- 就对 Opus 编码后的负载执行 `encode_audio()`
- IV 的前 4 字节由 `avRiKeyId + sequenceNumber` 派生

对应代码：

- [src/stream.cpp:1630](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1630)
- [src/stream.cpp:1634](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1634)

RTSP `DESCRIBE` / `ANNOUNCE` 里协商的是“是否启用音频加密”，真正加密发生在 RTP 发包前。

对应代码：

- [src/rtsp.cpp:768](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L768)
- [src/rtsp.cpp:982](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L982)

## 8. 音频 FEC：额外发送冗余包提高抗丢包能力

Sunshine 对音频流也做了前向纠错。

实现方式：

- 对若干个连续音频包组成一个 FEC block
- 使用 Reed-Solomon 生成 parity shards
- 额外发送 FEC RTP 包，`packetType = 127`

对应代码：

- [src/stream.cpp:1600](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1600)
- [src/stream.cpp:1652](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1652)
- [src/stream.cpp:1662](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1662)
- [src/stream.cpp:1670](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1670)
- [src/stream.cpp:2051](/home/yyx/workspaces/Sunshine/src/stream.cpp#L2051)

还有一个比较有代表性的兼容细节：

- Sunshine 发现自己默认 RS 实现出来的音频 parity matrix 和 Nvidia 不一致
- 所以直接替换成与 OpenFEC / Nvidia 兼容的固定矩阵

对应代码：

- [src/stream.cpp:1603](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1603)

这说明 Sunshine 在音频 FEC 上也是按 GameStream / Moonlight 兼容协议来实现的，而不是自定义一套新规则。

## 9. 一个完整的音频路径时序

可以把 Sunshine 传音频给 Moonlight 理解成下面这条时序：

1. Moonlight 调用 `/launch`
2. Sunshine 保存音频相关参数、加密 key、ping payload
3. Moonlight 发起 RTSP `DESCRIBE`
4. Sunshine 返回支持的 `surround-params`
5. Moonlight 发起 RTSP `SETUP streamid=audio`
6. Sunshine 返回音频 UDP 端口和 `X-SS-Ping-Payload`
7. Moonlight 发起 RTSP `ANNOUNCE`
8. Sunshine 解析客户端请求的声道数、包时长、音频质量、加密等参数
9. Sunshine 启动音频线程，等待音频 UDP PING
10. Moonlight 在音频端口上发送带 payload 的 PING
11. Sunshine 确认客户端音频端口
12. Sunshine 从系统抓音频
13. Sunshine 用 Opus multistream 编码
14. Sunshine 对负载做可选加密
15. Sunshine 封装 RTP 音频包并发送
16. Sunshine 额外发送 FEC 包
17. Moonlight 接收、解密、纠错、解码并播放

## 10. 关键判断

### 10.1 Sunshine 传给 Moonlight 的音频是否独立于视频？

是。音频使用独立的 RTP/UDP 流、独立端口、独立序号和时间戳，不复用到视频流里。

证据：

- [src/stream.h:21](/home/yyx/workspaces/Sunshine/src/stream.h#L21)
- [src/rtsp.cpp:855](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L855)
- [src/stream.cpp:1595](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1595)

### 10.2 Sunshine 用什么编码音频？

Opus multistream。

证据：

- [src/audio.cpp:97](/home/yyx/workspaces/Sunshine/src/audio.cpp#L97)

### 10.3 Sunshine 抓的是系统声音还是麦克风？

默认目标是“系统播放音频”。

- Linux：录 sink 的 monitor source
- Windows：WASAPI loopback
- macOS：优先 system audio tap；也支持显式指定输入设备

### 10.4 Sunshine 如何兼容 Moonlight？

兼容点至少包括：

- RTSP `surround-params` 的组织方式
- 音频 RTP payload type = 97
- 音频 FEC 包格式和 parity matrix
- 音频加密启用方式
- `X-SS-Ping-Payload` 会话识别逻辑

## 11. 最值得继续看的源码入口

如果后续要继续深挖，建议按这个顺序读：

1. [src/nvhttp.cpp:285](/home/yyx/workspaces/Sunshine/src/nvhttp.cpp#L285)
2. [src/rtsp.cpp:753](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L753)
3. [src/rtsp.cpp:895](/home/yyx/workspaces/Sunshine/src/rtsp.cpp#L895)
4. [src/stream.cpp:1801](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1801)
5. [src/audio.cpp:131](/home/yyx/workspaces/Sunshine/src/audio.cpp#L131)
6. [src/stream.cpp:1595](/home/yyx/workspaces/Sunshine/src/stream.cpp#L1595)

## 12. 一句话总结

Sunshine 将声音传到 Moonlight 的本质，是在 RTSP 阶段协商一条 GameStream 兼容的独立音频流参数，然后在运行期按平台方式采集系统音频，统一转成 48 kHz float PCM，用 Opus multistream 编码后，通过单独的 RTP/UDP 音频通道发送给 Moonlight，并配套做可选加密和 FEC。
