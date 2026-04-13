# Quickstart: Latency Optimization Validation

**Feature**: 001-reduce-audio-latency | **Date**: 2026/04/12

All commands run from repo root in a Visual Studio Developer PowerShell.

---

## Step 0：测量延迟基线（实施前先跑）

```powershell
# 构建（含测试）
cmake -S . -B build-windows-verify -G "NMake Makefiles" -DBUILD_TESTING=ON
cmake --build build-windows-verify

# Terminal 1：启动接收端（5 秒）
.\build-windows-verify\clientd.exe --port 50000 --seconds 5

# Terminal 2：发送正弦波（2 秒）
.\build-windows-verify\hostd.exe --host 127.0.0.1 --port 50000 --source sine --seconds 2
```

观察 `clientd` 输出中的 `e2e_latency_ms`，记录基线值。

---

## Step 1：验证可观测性（阶段 1）

**预期**：`clientd` 控制台每 5 秒输出一行汇总统计，格式如：

```
[stats] e2e=82ms jitter_buf=3 lost=0 startup_skip=12 late_drop=0 underrun=0
```

**验收**：数值可见，`startup_skip` 在启动后非零。

---

## Step 2：验证 FastLock（阶段 2）

**通过单元测试**：

```powershell
.\build-windows-verify\network_speaker_tests --gtest_filter="*FastLock*"
```

**预期**：
- 模拟 30 帧积压 → `expected_sequence` 跳至 `newest - 2`
- `startup_skipped_packets == 28`
- 连续 8 帧按序后转入 `Steady` 态

**通过集成测试（本地回环）**：

运行同 Step 0，观察首个 `e2e_latency_ms` 稳定值应比基线低 ≥ 50%。

---

## Step 3：验证稳态软追帧（阶段 3）

```powershell
# 发送 10 秒正弦波
.\build-windows-verify\hostd.exe --host 127.0.0.1 --port 50000 --source sine --seconds 10
```

在第 5~10 秒内观察 `e2e_latency_ms` 稳定在 150 ms 以下，无持续增长趋势。

---

## Step 4：验证过期包丢弃（阶段 4）

```powershell
.\build-windows-verify\network_speaker_tests --gtest_filter="*StalePacket*"
```

**预期**：`capture_ts_us` 为 600 ms 前的包被丢弃，计入 `late_dropped`；0 值包不被丢弃。

---

## Step 5：回退回归（阶段 5）

```powershell
# 禁用 FastLock（通过命令行参数或测试 fixture）
.\build-windows-verify\network_speaker_tests
```

**预期**：全部现有测试通过（0 failures），验证回退逻辑无退化。

---

## 可选 A 验证（自适应 jitter，如实现）

在同一 LAN 上运行大文件下载以制造抖动，观察 `jitter_buf` 深度自动扩大；
停止下载后 10 秒内观察 `jitter_buf` 自动缩回至最小值。
