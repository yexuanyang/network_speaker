# Research: Reduce Audio Streaming Latency

**Feature**: 001-reduce-audio-latency | **Date**: 2026/04/12

---

## 决策 1：当前延迟瓶颈定位

**Decision**: 主要瓶颈为 `PlayerPipeline` 的启动行为 + 固定 `target_packets = 6` 配置，而非网络传输本身。

**Rationale**:
- `target_packets = 6` → 6 × 20 ms Opus 帧 = **120 ms** 固定缓冲（Primed 门槛）
- 启动时将 `expected_sequence` 设为首包序号，不跳过历史积压帧
- 网络 UDP 传输在 LAN 环境下 RTT 通常 < 5 ms，不是瓶颈
- `e2e_latency_ms` 字段已存在于 `StreamStats`，可直接测量基线

**Alternatives considered**:
- 降低 Opus 帧时长（20 ms → 10 ms）：减少每帧固有延迟但增加包头开销和 CPU 占用，且需修改发送端；优先级低于启动优化
- 修改 UDP 传输层（如 QUIC）：协议改动大，超出范围

---

## 决策 2：FastLock 触发阈值

**Decision**: `startup_buffer_packets = 4`，`startup_lead_packets = 2`。

**Rationale**:
- 4 帧（80 ms）是足够判断是否有积压的最小窗口；太小（如 2）则网络乱序可能触发误跳
- 跳跃后保留 2 帧前导（`startup_lead_packets`），防止跳过后 jitter buffer 立即欠载（underrun）
- 在 LAN 丢包率 < 1% 的典型场景下，2 帧前导足够；弱网场景建议调至 4

**Alternatives considered**:
- 直接跳到 `newest_sequence`（front-run = 0）：underrun 风险高，音质差
- 使用时间戳而非包数计量：需要发送端时钟精度保证，增加耦合

---

## 决策 3：稳态目标 buffer 深度

**Decision**: `steady_target_packets = 3`（60 ms），可配置。

**Rationale**:
- 3 × 20 ms = 60 ms，给予网络抖动 ±1 帧的容忍（≤ 20 ms），在 LAN 环境下足够
- 对比原始 6 帧（120 ms），减少约 60 ms 稳态延迟
- `Primed()` 判断沿用 `packets_.size() >= target_packets_`，逻辑不变，只是默认值更小

**Alternatives considered**:
- `target_packets = 2`（40 ms）：LAN 中可行，但 WiFi 偶发高抖动时 underrun 概率明显上升
- `target_packets = 1`：延迟最低但实际场景下 underrun 频繁，不可用

---

## 决策 4：时间戳过期丢包实现位置

**Decision**: 在 `PlayerPipeline::PushPacket` 中基于 `capture_ts_us` 做过期检查（而非在 JitterBuffer 中）。

**Rationale**:
- `PlayerPipeline` 持有 `clock_`，可直接访问 `NowMicros()`；JitterBuffer 无时钟依赖，保持简单
- 在 `Push` 前检查，避免过期包占用 buffer 窗口
- `capture_ts_us == 0` 时跳过检查（兼容旧版发送端）

**Alternatives considered**:
- 在 `JitterBuffer::Push` 中传入时钟：增加 JitterBuffer 的依赖，破坏其无状态设计
- 在 `DrainReady` 的 `PopNext` 之后再丢弃：包已占用 buffer 窗口，效果差

---

## 决策 5：新增 JitterBuffer API

**Decision**: 新增 `NewestSequence() const → std::optional<uint32_t>` 方法。

**Rationale**:
- FastLock 跳跃需要知道当前 buffer 中的最大序列号
- `packets_` 是 `std::map<uint32_t, AudioPacket>`，`.rbegin()->first` 即为 newest，O(1) 访问
- 接口签名与现有 `OldestSequence()` 对称，易于理解

---

## 决策 6：`StreamStats` 新增字段

**Decision**: 新增 `startup_skipped_packets`（uint32_t）和 `peak_jitter_ms`（uint32_t）。

**Rationale**:
- `startup_skipped_packets`：用于评估 FastLock 效果；区分"正常丢包"与"主动跳跃"
- `peak_jitter_ms`：现有只有 `current_jitter_ms`（瞬时），峰值有助于回归对比

**Alternatives considered**:
- 用独立结构体 `LatencyStats` 存放新字段：过度设计，现有 `StreamStats` 已是聚合点，直接扩展即可

---

## 测量结果记录 (T001 / T021)

- **Baseline `e2e_latency_ms`**: ~350 ms (T001)
- **Final `e2e_latency_ms`**: ~80 ms (T021)
