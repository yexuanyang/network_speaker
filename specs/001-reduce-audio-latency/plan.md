# Implementation Plan: Reduce Audio Streaming Latency

**Branch**: `001-reduce-audio-latency` | **Date**: 2026/04/12 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/001-reduce-audio-latency/spec.md`

## Summary

优化 Windows WASAPI loopback → Opus → UDP → Android AudioTrack 链路的端到端延迟。

当前瓶颈：`PlayerPipeline` 默认 `target_packets = 6`，即 6 × 20 ms = **120 ms** 固定缓冲；
启动时无"追最新"机制，若网络已积压多个包，会顺序播放所有历史帧后才到达实时音频。

目标：通过启动快速锁定 + 稳态软追帧 + 时间戳过期丢弃，将稳态 `e2e_latency_ms` 降至 150 ms 以下，
相比当前基线下降 ≥ 50%，同时保持协议兼容（`AudioPacketHeader` 线格式不变）。

---

## Technical Context

| 项目 | 值 |
|---|---|
| **Language/Version** | C++17（libs/、server/hostd、client/core）; Kotlin（Android）; C# .NET 10（Windows launcher）|
| **Primary Dependencies** | libopus、WASAPI、Android AudioTrack、WinSock2/POSIX UDP |
| **Storage** | N/A（流式管道，无持久化音频存储）|
| **Testing** | CTest / GoogleTest（C++）; dotnet test（GUI）; Android instrumentation |
| **Target Platform** | Windows x64 发送端 → Android 接收端（同 LAN）|
| **Performance Goals** | 稳态 e2e_latency_ms < 150 ms；dropout < 1 次/分钟 |
| **Constraints** | AudioPacketHeader 线格式不变；libopus 版本与 Android NDK 不变 |
| **Scale/Scope** | 单发送端 → 单接收端，家庭/办公室 LAN |

**当前默认参数（代码实测）**：

- `PlayerPipeline` 构造：`target_packets = 6`（120 ms 缓冲）
- `JitterBuffer::max_window = 64`（包序列号窗口）
- `StreamStats` 已有字段：`e2e_latency_ms`、`current_jitter_ms`、`packets_lost`、`late_dropped`、
  `duplicates_dropped`、`packets_reordered`、`playback_underruns`、`decode_failures`

---

## Constitution Check

Constitution 文件为未填充模板，无项目级门控。无违规项。

---

## Project Structure

### Documentation（本特性）

```text
specs/001-reduce-audio-latency/
├── plan.md          ← 本文件
├── research.md      ← Phase 0 输出
├── data-model.md    ← Phase 1 输出
├── quickstart.md    ← Phase 1 输出
├── contracts/       ← Phase 1 输出
│   ├── jitter_buffer.md
│   └── player_pipeline.md
└── tasks.md         ← /speckit.tasks 输出（本命令不生成）
```

### Source Code（涉及文件）

```text
libs/audio_base/include/nspeaker/audio/
└── stream_stats.h                    ← 新增 startup_skipped_packets

libs/transport/include/nspeaker/transport/
└── jitter_buffer.h                   ← 新增 NewestSequence()

libs/transport/src/
└── jitter_buffer.cpp                 ← 实现 NewestSequence()

client/core/include/nspeaker/client/
└── player_pipeline.h                 ← 新增 PipelineConfig；重载构造函数

client/core/src/
└── player_pipeline.cpp               ← FastLock 状态机；软追帧；时间戳过期丢弃

tests/
├── jitter_buffer_test.cpp            ← 新增 FastLock 序列跳跃测试
└── end_to_end_test.cpp               ← 新增延迟基线测试
```

---

## 目标

- 启动阶段允许小幅丢包，优先快速贴近"最新音频"
- 稳定阶段维持低延迟并尽量平滑播放
- 保持现有协议兼容，不引入破坏性变更

---

## 阶段 1：可观测性（先做）

**当前状态**：`StreamStats` 已有 `e2e_latency_ms`、`current_jitter_ms`、各丢包计数，
但**没有周期性日志输出**，也**没有 `startup_skipped_packets`** 字段。

**本阶段工作**：

- 在 `StreamStats` 新增 `startup_skipped_packets`（启动阶段主动跳跃包数）
- 在 `clientd` 主循环中每 5 秒输出一次汇总日志：

  ```
  [stats] e2e=82ms jitter_buf=3 lost=0 startup_skip=12 late_drop=0 underrun=0
  ```

- `current_jitter_ms` 改为同时记录均值/峰值（可在 `StreamStats` 增加 `peak_jitter_ms`）

**闭环**：运行 `hostd --source sine` + `clientd`，控制台每 5 秒可见 `e2e_latency_ms` 数值输出。

---

## 阶段 2：启动阶段——最新优先锁定（核心）

**问题**：当前 `PushPacket` 在收到新 `stream_id` 时将 `expected_sequence` 设为首包序列号，
之后等 `target_packets`（6 帧）累积后顺序播放。若网络已积压 20+ 帧，
则会先播完历史帧，造成数百毫秒启动延迟。

**本阶段工作**：

- 在 `PlayerPipeline` 引入 `FastLock` 启动态：

  1. 收到首包时进入 `FastLock` 态，记录当前 `expected_sequence`
  2. `JitterBuffer` 新增 `NewestSequence()` 方法，暴露当前最高序列号
  3. 当 `Size() >= startup_buffer_packets` 时触发评估：
     - 若 `NewestSequence() > expected_sequence_ + startup_lead_packets`，
       将 `expected_sequence_` 跳至 `NewestSequence() - startup_lead_packets`
     - 跳过的包数累计到 `stats_.startup_skipped_packets`
  4. 进入稳态条件：连续命中 `steady_consecutive_threshold` 个按序包

- **配置参数**（均提供合理默认值）：
  - `startup_fast_lock_enabled = true`
  - `startup_buffer_packets = 4`（触发跳跃的最小积压帧数）
  - `startup_lead_packets = 2`（跳跃后距最新包的安全前导帧数）
  - `steady_consecutive_threshold = 8`（连续按序包数，达到则转入稳态）

**闭环**：
- 单元测试：模拟 30 帧积压，验证 `expected_sequence` 正确跳至 `newest - 2`，`startup_skipped_packets == 28`
- 集成验证：本地回环测试，观察 `e2e_latency_ms` 首次稳定值相比阶段 1 基线显著下降

---

## 阶段 3：稳定阶段——低延迟优先的顺序播放

**问题**：稳态下如遇延迟累积（如设备短暂繁忙、网络抖动后缓冲未释放），
当前无主动追帧机制，`e2e_latency_ms` 会持续偏高。

**本阶段工作**：

- 新增"软追帧"机制（在 `DrainReady` 稳态分支中）：
  - 每次 `DrainReady` 入口，若 `stats_.e2e_latency_ms > late_frame_drop_threshold_ms`，
    跳过当前 `expected_sequence_` 对应的 1 帧（即使 buffer 中有该帧也跳过）
  - 每次软追帧后重新评估，避免级联触发（加冷却计数，每轮最多追 2 帧）
  - 跳过帧计入 `stats_.packets_lost`（语义上等同于延迟丢包）
- 保留现有重排（reorder）与去重（dedup）逻辑，不修改 `JitterBuffer::Push` 行为

**配置参数**：
- `late_frame_drop_threshold_ms = 200`（触发软追帧的延迟阈值，ms）
- `max_catchup_per_drain = 2`（每次 `DrainReady` 最多软追帧数）

**闭环**：
- 集成验证：稳态 `e2e_latency_ms` 均值低于 150 ms
- 主观听感无持续"越播越晚"现象

---

## 阶段 4：时间戳对齐与过期包丢弃

**问题**：设备从睡眠唤醒后，`JitterBuffer` 中可能积压数秒前的历史帧，
顺序播放这些帧会造成延迟堆积，且无法被软追帧快速消化。

**本阶段工作**：

- 在 `JitterBuffer::Push` 或 `PlayerPipeline::PushPacket` 中，
  基于 `packet.header.capture_ts_us` 与 `clock_->NowMicros()` 的差值判断过期：
  - 若差值 > `stale_packet_threshold_ms`（建议 500 ms），直接丢弃，计入 `late_dropped`
  - 使用单调时钟差值（相对），不依赖主机与设备的绝对时钟同步
- 该策略仅在差值超阈值时触发，常态下不影响正常播放

**配置参数**：
- `stale_packet_threshold_ms = 500`（判定为过期的延迟阈值，ms）

**注意事项**：需处理 `capture_ts_us == 0` 的边界情况（旧版发送端未设置时间戳）。

**闭环**：
- 单元测试：构造 `capture_ts_us` 为 1 秒前的包，验证被丢弃且计入 `late_dropped`
- 集成验证：设备唤醒后，积压音频在 1 秒内自动清空并追上实时

---

## 阶段 5：参数化与回退机制

**本阶段工作**：

- 新增 `PipelineConfig` 结构体（`client/core/include/nspeaker/client/player_pipeline.h`）：

  ```cpp
  struct PipelineConfig {
      bool   startup_fast_lock_enabled      = true;
      size_t startup_buffer_packets         = 4;
      size_t startup_lead_packets           = 2;
      size_t steady_target_packets          = 3;
      size_t steady_consecutive_threshold   = 8;
      size_t max_catchup_per_drain          = 2;
      uint32_t late_frame_drop_threshold_ms = 200;
      uint32_t stale_packet_threshold_ms    = 500;
  };
  ```

- `PlayerPipeline` 新增接受 `PipelineConfig` 的构造函数；保留原有 `target_packets` 构造函数以兼容现有调用侧
- `clientd` 通过命令行参数注入关键参数（`--steady-target-packets`、`--disable-fast-lock`）
- 保留一键回退开关：`startup_fast_lock_enabled = false` + `late_frame_drop_threshold_ms = UINT32_MAX`

**闭环**：关闭 `startup_fast_lock_enabled` 后行为等同原逻辑，所有现有回归测试通过。

---

## 可选项（按优先级）

1. **可选 A（高优先）**：自适应 jitter 目标深度
   - 基于滑动窗口（20 帧）内的包间抖动（inter-packet jitter）动态调整 `steady_target_packets`
   - 网络稳定时减小至 `min_steady_packets`（如 2）；抖动增大时临时放宽至 `max_steady_packets`（如 8）

2. **可选 B（中优先）**：Opus 抗丢包参数
   - 评估 `OPUS_SET_PACKET_LOSS_PERC`、`OPUS_SET_INBAND_FEC` 的收益与码率代价
   - 建议仅在 `packets_lost / packets_received > 2%` 时启用 FEC，常态不增加码率

3. **可选 C（中优先）**：分级延迟策略
   - 预设两档 `PipelineConfig`：`LowLatency`（`steady_target_packets=2`）/ `Balanced`（`=5`）
   - 通过 `clientd --latency-mode low|balanced` 切换；GUI 可选择性暴露

4. **可选 D（低优先）**：发送端轻量打点
   - `hostd` 改用高精度单调时钟（`QueryPerformanceCounter`）生成 `capture_ts_us`，记录发送间隔日志
   - 便于端到端区分捕获侧 / 网络侧 / 播放侧延迟来源

---

## 验收口径（低延迟专项）

- 启动后 500 ms 内完成 FastLock 锁定，进入稳定低延迟状态
- 稳态 `e2e_latency_ms` 均值低于 150 ms（相比 `target_packets=6` 基线下降 ≥ 50%）
- 主观听感无持续"越播越晚"现象
- 弱网下允许轻微瞬时失真，但不出现长时间延迟堆积
- 关闭 FastLock 后，所有现有回归测试（`jitter_buffer_test`、`end_to_end_test`）全部通过
