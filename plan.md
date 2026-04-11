# 网络扬声器实施计划

此计划用于实现一个跨平台网络扬声器系统：电脑端服务端采集系统播放音频，经低延迟网络传输到手机端客户端，客户端解码后通过手机扬声器播放；首版目标为局域网内 `48 kHz / 2 声道 / 高音质 / 端到端延迟 <= 200 ms`。

## 摘要

- 首版确认项：
  - 服务端与共享核心模块使用 `C++20`
  - 手机端优先做 `Android APK`，默认依赖鸿蒙对 Android 应用的兼容能力
  - Android 客户端采用 `C++ 核心 + 薄 Kotlin 壳`
  - 首版只做 `立体声`
  - 首版网络协议采用 `自定义 UDP 音频流`，不强制加密，不首发 FEC
- 方案原则：
  - 模块先解耦，再拼装
  - 先验证 PCM 采集与播放正确性，再引入 Opus 和网络
  - 所有阶段都配套单元测试、集成测试和阶段验收
- Sunshine 调研的吸收方式：
  - 继承其核心思路：`系统回环采集 -> 48k PCM -> Opus 低延迟编码 -> 独立 UDP 音频流 -> 客户端抖动缓冲 -> 解码播放`
  - 但首版不直接复制 `RTP + FEC + AES-CBC` 全链路，只保留未来扩展点

## 关键接口与工程拆分

- 建议工程结构：
  - `libs/audio_base`：公共音频类型、时钟、帧格式
  - `libs/transport`：UDP 包头、序列号、时间戳、重排、统计
  - `libs/codec_opus`：Opus 编解码
  - `server/hostd`：Windows/Linux 音频采集与发送
  - `client/core`：接收、抖动缓冲、解码、播放调度
  - `client/android-app`：Kotlin 壳、权限、前后台生命周期、JNI

- 首版公共类型：

```cpp
// libs/audio_base/include/audio/frame.h
#pragma once
#include <cstdint>
#include <vector>

namespace nspeaker::audio {

struct PcmFormat {
    int sample_rate = 48000;
    int channels = 2;
};

struct PcmFrame {
    PcmFormat format{};
    uint64_t capture_ts_us = 0;
    uint32_t samples_per_channel = 0;
    std::vector<float> interleaved;
};

} // namespace nspeaker::audio
```

- 解释：
  - 统一用 `48kHz + float32 + interleaved stereo`，这样 Windows WASAPI loopback、Linux Pulse/PipeWire 采集和 Opus `encode_float()` 都容易对齐
  - `capture_ts_us` 为后续延迟测量、抖动缓冲和 A/V 统计保留统一时基

- 首版 UDP 音频包头：

```cpp
// libs/transport/include/transport/audio_packet.h
#pragma once
#include <cstdint>

namespace nspeaker::transport {

struct AudioPacketHeader {
    uint32_t magic = 0x4E535031;
    uint16_t version = 1;
    uint16_t flags = 0;
    uint32_t stream_id = 1;
    uint32_t sequence = 0;
    uint64_t capture_ts_us = 0;
    uint16_t frame_samples = 480;
    uint16_t payload_size = 0;
};

} // namespace nspeaker::transport
```

- 解释：
  - 不首发 RTP，先用更可控的自定义头；`sequence` 负责丢包/乱序判断，`capture_ts_us` 用于端到端延迟统计
  - `frame_samples = 480` 对应 `10 ms` 帧长，和 Sunshine 的低延迟思路一致，但实现更轻

- 首版服务端发送接口：

```cpp
class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;
    virtual bool Start() = 0;
    virtual bool ReadFrame(nspeaker::audio::PcmFrame& out) = 0;
};

class IAudioEncoder {
public:
    virtual ~IAudioEncoder() = default;
    virtual bool Encode(const nspeaker::audio::PcmFrame& pcm,
                        std::vector<uint8_t>& opus) = 0;
};

class IAudioSender {
public:
    virtual ~IAudioSender() = default;
    virtual bool Send(uint32_t seq,
                      const nspeaker::audio::PcmFrame& pcm,
                      std::span<const uint8_t> opus) = 0;
};
```

- 解释：
  - 采集、编码、传输分离，任何一个模块都可以被 mock，便于逐阶段验收
  - 后续如果引入 FEC 或加密，只扩展 `IAudioSender`，不反向侵入采集与编码层

## 分阶段实施

### 阶段 0：工程骨架与时延基线

- 目标：
  - 建立 CMake 工程、三层模块边界、日志与统计接口
  - 明确首版验收口径：`局域网 p95 端到端延迟 <= 180 ms，峰值 <= 200 ms`
- 代码片段：

```cmake
add_library(audio_base INTERFACE)
target_include_directories(audio_base INTERFACE libs/audio_base/include)

add_library(transport STATIC
    libs/transport/src/audio_packet.cpp
    libs/transport/src/jitter_buffer.cpp)
target_link_libraries(transport PUBLIC audio_base)

add_library(codec_opus STATIC
    libs/codec_opus/src/opus_codec.cpp)
target_link_libraries(codec_opus PUBLIC audio_base opus)

add_executable(hostd server/hostd/src/main.cpp)
target_link_libraries(hostd PRIVATE transport codec_opus)

add_library(client_core STATIC
    client/core/src/receiver.cpp
    client/core/src/player_pipeline.cpp)
target_link_libraries(client_core PUBLIC transport codec_opus)
```

- 解释：
  - 先固定模块边界，再落具体平台代码，避免后续 server/client 互相牵扯
- 测试：
  - `ctest` 能跑通空工程
  - 构建 Linux/Windows hostd 和 Android native core 成功
  - 基线脚本输出本机单调时钟精度与日志时间戳正确性

### 阶段 1：服务端本地系统音频采集模块

- 目标：
  - Windows 用 `WASAPI loopback`
  - Linux 首版用 `PulseAudio API`，默认兼容 PipeWire 的 pulse server
  - 只输出统一 `PcmFrame`
- Windows 代码片段：

```cpp
bool WasapiLoopbackCapture::ReadFrame(audio::PcmFrame& out) {
    BYTE* data = nullptr;
    UINT32 frames = 0;
    DWORD flags = 0;
    if (FAILED(capture_client_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
        return false;
    }

    out.format = {.sample_rate = 48000, .channels = 2};
    out.samples_per_channel = frames;
    out.capture_ts_us = clock_->NowMicros();
    out.interleaved.resize(frames * 2);

    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        std::fill(out.interleaved.begin(), out.interleaved.end(), 0.0f);
    } else {
        ConvertToFloatStereo(data, frames, out.interleaved);
    }

    capture_client_->ReleaseBuffer(frames);
    return true;
}
```

- Linux 代码片段：

```cpp
bool PulseMonitorCapture::ReadFrame(audio::PcmFrame& out) {
    out.format = {.sample_rate = 48000, .channels = 2};
    out.samples_per_channel = 480;
    out.capture_ts_us = clock_->NowMicros();
    out.interleaved.resize(480 * 2);

    const int bytes = static_cast<int>(out.interleaved.size() * sizeof(float));
    if (pa_simple_read(pa_, out.interleaved.data(), bytes, &error_) < 0) {
        return false;
    }
    return true;
}
```

- 解释：
  - 该阶段不接编码、不接网络，只验证“系统声音能否稳定回环采到统一 PCM”
  - Linux 先走 `PulseAudio compatible API`，原因是 PipeWire 默认兼容它，跨发行版实现成本最低
- 测试：
  - 单元测试：`ConvertToFloatStereo()` 对 16-bit/32-bit 输入转换正确
  - 平台冒烟：
    - Windows 播放固定 1 kHz 测试音，采集到非零 PCM
    - Linux 播放固定 1 kHz 测试音，采集到非零 PCM
  - 阶段验收：
    - 连续采集 30 分钟无崩溃、无明显缓冲溢出
    - RMS 和峰值统计稳定，无周期性掉零

### 阶段 2：独立 Opus 编解码模块

- 目标：
  - 引入 `Opus`，固定 `48k / 2ch / 10ms / low-delay`
  - 生成纯编解码闭环，不接网络
- 编码代码片段：

```cpp
bool OpusEncoderImpl::Init() {
    int err = 0;
    enc_ = opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (err != OPUS_OK) return false;

    opus_encoder_ctl(enc_, OPUS_SET_BITRATE(192000));
    opus_encoder_ctl(enc_, OPUS_SET_VBR(0));
    opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(10));
    return true;
}

bool OpusEncoderImpl::Encode(const audio::PcmFrame& pcm, std::vector<uint8_t>& opus) {
    opus.resize(1500);
    const int n = opus_encode_float(enc_,
                                    pcm.interleaved.data(),
                                    static_cast<int>(pcm.samples_per_channel),
                                    opus.data(),
                                    static_cast<opus_int32>(opus.size()));
    if (n < 0) return false;
    opus.resize(n);
    return true;
}
```

- 解码代码片段：

```cpp
bool OpusDecoderImpl::Decode(std::span<const uint8_t> opus, audio::PcmFrame& pcm) {
    pcm.format = {.sample_rate = 48000, .channels = 2};
    pcm.samples_per_channel = 480;
    pcm.interleaved.resize(480 * 2);

    const int n = opus_decode_float(dec_,
                                    opus.data(),
                                    static_cast<opus_int32>(opus.size()),
                                    pcm.interleaved.data(),
                                    480,
                                    0);
    return n == 480;
}
```

- 解释：
  - `10 ms` 帧长是延迟和稳定性的平衡点；比 `20 ms` 延迟更低，又比更小帧更节省包头开销
  - 首版用 `CBR 192 kbps` 立体声，优先满足高音质与稳定延迟
- 测试：
  - 单元测试：PCM -> Opus -> PCM 的 SNR/能量偏差在可接受范围
  - 压力测试：连续编码解码 1 小时无泄漏
  - 阶段验收：
    - 对粉噪、正弦、人声样本进行 AB 对比，主观无明显噪声和断裂

### 阶段 3：自定义 UDP 传输模块

- 目标：
  - 实现独立可测的发包、收包、乱序处理、丢包统计
  - 此阶段仍不接 Android 播放
- 发包代码片段：

```cpp
bool UdpAudioSender::Send(uint32_t seq,
                          const audio::PcmFrame& pcm,
                          std::span<const uint8_t> opus) {
    transport::AudioPacketHeader hdr;
    hdr.sequence = seq;
    hdr.capture_ts_us = pcm.capture_ts_us;
    hdr.payload_size = static_cast<uint16_t>(opus.size());

    std::array<uint8_t, sizeof(hdr) + 1500> buf{};
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    std::memcpy(buf.data() + sizeof(hdr), opus.data(), opus.size());

    return socket_.SendTo(peer_, buf.data(), sizeof(hdr) + opus.size());
}
```

- 收包与重排代码片段：

```cpp
std::optional<ReceivedPacket> Receiver::PollOne() {
    auto raw = socket_.Recv();
    if (raw.size() < sizeof(AudioPacketHeader)) return std::nullopt;

    AudioPacketHeader hdr{};
    std::memcpy(&hdr, raw.data(), sizeof(hdr));
    if (hdr.magic != 0x4E535031 || hdr.version != 1) return std::nullopt;

    return ReceivedPacket{
        .sequence = hdr.sequence,
        .capture_ts_us = hdr.capture_ts_us,
        .payload = std::vector<uint8_t>(raw.begin() + sizeof(hdr), raw.end())
    };
}
```

- 解释：
  - 不把抖动缓冲写进 socket 层；传输层只负责包正确到达与顺序信息
  - 后续升级到 RTP/FEC 时，这一层可以被替换，而不影响编解码接口
- 测试：
  - 单元测试：包头序列化/反序列化
  - 集成测试：本机注入乱序、重复包、丢包，统计正确
  - 阶段验收：
    - 10000 包压测下无包头解析错误
    - 能准确统计丢包率、乱序率、重复率

### 阶段 4：客户端抖动缓冲与解码模块

- 目标：
  - 在 `client/core` 中实现可调抖动缓冲、Opus 解码、播放前队列
  - 不依赖 Android UI，只做 native core
- 抖动缓冲代码片段：

```cpp
class JitterBuffer {
public:
    explicit JitterBuffer(size_t target_packets) : target_packets_(target_packets) {}

    void Push(ReceivedPacket pkt) { queue_[pkt.sequence] = std::move(pkt); }

    std::optional<ReceivedPacket> PopNext(uint32_t expected_seq) {
        auto it = queue_.find(expected_seq);
        if (it == queue_.end()) return std::nullopt;
        auto pkt = std::move(it->second);
        queue_.erase(it);
        return pkt;
    }

private:
    size_t target_packets_;
    std::map<uint32_t, ReceivedPacket> queue_;
};
```

- 播放流水线代码片段：

```cpp
bool PlayerPipeline::ProcessOne() {
    auto pkt = jitter_.PopNext(expected_seq_);
    if (!pkt) return false;

    audio::PcmFrame pcm;
    if (!decoder_.Decode(pkt->payload, pcm)) {
        ++decode_failures_;
        ++expected_seq_;
        return false;
    }

    pcm.capture_ts_us = pkt->capture_ts_us;
    sink_->SubmitPcm(pcm);
    ++expected_seq_;
    return true;
}
```

- 解释：
  - 抖动缓冲与实际播放设备解耦；先把“网络恢复能力”单独测稳
  - `target_packets` 首版建议默认 `6`，即约 `60 ms` 网络缓冲，可配置 `40~120 ms`
- 测试：
  - 单元测试：乱序窗口内能恢复正确顺序，超窗包丢弃正确
  - 集成测试：模拟 `5%` 乱序和 `20 ms` 抖动时，播放序列连续
  - 阶段验收：
    - 无音频爆音
    - 连续播放 30 分钟无缓冲失控

### 阶段 5：Android 薄壳与系统播放接入

- 目标：
  - Kotlin 负责生命周期、网络权限、前台服务
  - C++ core 负责收包、抖动缓冲、解码
  - Android 播放优先使用 `AudioTrack` 流式输出；若设备验证后确有收益，再评估 `AAudio`
- Kotlin 代码片段：

```kotlin
class SpeakerService : Service() {
    external fun nativeStart(host: String, port: Int)
    external fun nativeStop()

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val host = intent?.getStringExtra("host") ?: return START_NOT_STICKY
        val port = intent.getIntExtra("port", 50000)
        nativeStart(host, port)
        return START_STICKY
    }

    override fun onDestroy() {
        nativeStop()
        super.onDestroy()
    }
}
```

- JNI 播放桥代码片段：

```cpp
extern "C" JNIEXPORT void JNICALL
Java_com_example_speaker_NativeBridge_onPcmReady(JNIEnv* env, jobject thiz, jfloatArray pcm) {
    // Kotlin/Java 层持有 AudioTrack，native 仅回调或直接写入 direct buffer
}
```

- 解释：
  - Android 宿主只保留最薄的系统交互层，避免核心逻辑被 Kotlin 侵蚀
  - 首版不做复杂 UI，只做连接、断开、状态和延迟显示
- 测试：
  - Android 仪器测试：Service 启停、权限、前后台切换
  - 真机冒烟：屏幕锁定后持续播放 30 分钟不中断
  - 阶段验收：
    - 首次连接成功率高
    - 切到后台后音频不断流

### 阶段 6：端到端拼装

- 目标：
  - 把 `采集 -> 编码 -> UDP -> 接收 -> 抖动缓冲 -> 解码 -> Android 播放` 串起来
  - 增加实时统计与调参能力
- 统计结构代码片段：

```cpp
struct StreamStats {
    uint32_t packets_sent = 0;
    uint32_t packets_lost = 0;
    uint32_t packets_reordered = 0;
    uint32_t decode_failures = 0;
    uint32_t playback_underruns = 0;
    uint32_t current_jitter_ms = 0;
    uint32_t e2e_latency_ms = 0;
};
```

- 解释：
  - 先把可观测性做出来，再做联合调优；否则无法判断延迟超标来自采集、编码、网络还是播放
- 测试：
  - 联合集成测试：PC 本地发送到 Android，验证完整链路有声音
  - 稳定性测试：连续播放 2 小时
  - 阶段验收：
    - 正常局域网下 `p95 <= 180 ms`
    - `playback_underruns == 0` 或极低且不可感知
    - 主观听感无噪声、无明显丢字

### 阶段 7：质量优化与扩展预留

- 目标：
  - 在不破坏模块边界的前提下，为后续 `FEC`、`加密`、`多声道` 保留扩展面
- 预留接口代码片段：

```cpp
struct SenderOptions {
    bool enable_encryption = false;
    bool enable_fec = false;
    uint32_t target_bitrate = 192000;
    uint32_t jitter_target_ms = 60;
};
```

- 解释：
  - 用户已确认首版不强求加密，且优先延迟与音质；因此这里只保留配置面，不在首版强行实现
  - 如果后续局域网稳定但偶发丢包，可优先加 `FEC`，再决定是否补 `AES`
- 测试：
  - 配置回归测试：默认配置不退化
  - 预留接口单测：关闭状态下不引入额外分支副作用

## 测试计划

- 单元测试：
  - PCM 格式转换
  - Opus 编解码正确性
  - UDP 包头序列化与校验
  - 抖动缓冲乱序恢复
- 集成测试：
  - 服务端采集 + 本地编解码回环
  - 本机 UDP 发送 + 接收 + 解码
  - Android native core 接收假流并播放
- 联合测试：
  - Windows 服务端 -> Android 客户端
  - Linux 服务端 -> Android 客户端
  - Wi-Fi 局域网下长时播放
- 端到端验收：
  - 播放音乐、人声、系统提示音三类样本
  - 连续播放 2 小时
  - 记录 `p50/p95/p99` 延迟、丢包率、underrun 次数
  - 通过标准：
    - `p95 端到端延迟 <= 180 ms`
    - 峰值延迟 `<= 200 ms`
    - 无持续噪声、无明显缺音
    - 正常局域网下不出现可重复爆音

## 默认结论与已确认事项

- 已确认使用 `C++20` 作为服务端与共享核心主语言
- 已确认客户端优先做 `Android APK`
- 已确认客户端采用 `C++ 核心 + 薄 Kotlin 壳`
- 已确认首版只做 `48kHz / 2 声道`
- 已确认首版采用 `自定义 UDP`，不直接复刻 Sunshine 的 `RTP/FEC/AES`
- 默认 Linux 音频采集首版走 `PulseAudio compatible API`；若后续要求原生 `PipeWire SPA`，需要再调整平台阶段计划
- 默认 Android 播放首版使用 `AudioTrack`；若后续要求首版直接上 `AAudio/Oboe`，需要再调整客户端阶段计划
- 默认 `plan.md` 文件名冲突时递增为 `plan-2.md`、`plan-3.md`
