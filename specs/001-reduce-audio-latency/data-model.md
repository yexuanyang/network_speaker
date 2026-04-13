# Data Model: Reduce Audio Streaming Latency

**Feature**: 001-reduce-audio-latency | **Date**: 2026/04/12

---

## 新增 / 变更实体

### 1. `PipelineConfig`（新增）

**文件**：`client/core/include/nspeaker/client/player_pipeline.h`

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `startup_fast_lock_enabled` | `bool` | `true` | 是否启用 FastLock 启动态 |
| `startup_buffer_packets` | `size_t` | `4` | 触发跳跃的最小积压帧数 |
| `startup_lead_packets` | `size_t` | `2` | 跳跃后距最新包的安全前导帧数 |
| `steady_target_packets` | `size_t` | `3` | 稳态目标 buffer 深度（帧数） |
| `steady_consecutive_threshold` | `size_t` | `8` | 连续按序包数达到则转稳态 |
| `max_catchup_per_drain` | `size_t` | `2` | 每次 DrainReady 最多软追帧数 |
| `late_frame_drop_threshold_ms` | `uint32_t` | `200` | 触发软追帧的延迟阈值（ms） |
| `stale_packet_threshold_ms` | `uint32_t` | `500` | 判定过期包的延迟阈值（ms） |

**关系**：由 `PlayerPipeline` 持有，通过构造函数注入。

---

### 2. `PipelineState`（新增枚举）

**文件**：`client/core/include/nspeaker/client/player_pipeline.h`（或同 cpp）

```
FastLock  → 启动态：等待积压阈值 → 跳跃到最新
Steady    → 稳态：按序顺序播放 + 软追帧
```

**状态转换**：
- 初始（新 stream_id）→ `FastLock`
- `FastLock` → `Steady`：连续命中 `steady_consecutive_threshold` 个按序包

---

### 3. `StreamStats`（扩展）

**文件**：`libs/audio_base/include/nspeaker/audio/stream_stats.h`

新增字段：

| 字段 | 类型 | 说明 |
|---|---|---|
| `startup_skipped_packets` | `uint32_t` | FastLock 阶段主动跳跃的帧数 |
| `peak_jitter_ms` | `uint32_t` | 历史峰值 jitter buffer 深度（ms） |

现有字段（不变）：`e2e_latency_ms`、`current_jitter_ms`、`packets_lost`、`late_dropped`、
`duplicates_dropped`、`packets_reordered`、`playback_underruns`、`decode_failures`、
`packets_received`、`packets_sent`

---

### 4. `JitterBuffer`（扩展）

**文件**：`libs/transport/include/nspeaker/transport/jitter_buffer.h`

新增方法：

| 方法 | 返回值 | 说明 |
|---|---|---|
| `NewestSequence() const` | `std::optional<uint32_t>` | 返回 buffer 中最大序列号；空 buffer 返回 nullopt |

实现：`packets_.empty() ? nullopt : optional{packets_.rbegin()->first}`（O(1)）

---

### 5. `PlayerPipeline`（扩展）

**文件**：`client/core/include/nspeaker/client/player_pipeline.h`

新增成员：

| 成员 | 类型 | 说明 |
|---|---|---|
| `config_` | `PipelineConfig` | 运行时配置（构造时注入）|
| `state_` | `PipelineState` | 当前状态（FastLock / Steady）|
| `steady_consecutive_` | `size_t` | 当前连续按序包计数 |

新增构造重载：
```cpp
PlayerPipeline(std::unique_ptr<IAudioDecoder>,
               std::shared_ptr<IAudioSink>,
               std::shared_ptr<Clock>,
               PipelineConfig config);
```
保留原有 `target_packets` 构造函数，以 `PipelineConfig{.steady_target_packets = target_packets}` 转发。
