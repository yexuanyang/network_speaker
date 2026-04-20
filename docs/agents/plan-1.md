# plan-1

## 标题

Windows 图形界面、MSI 安装与 GitHub Release 实施计划

## 摘要

- 目标：把当前只能源码构建并手动运行的 `hostd.exe`，升级为普通用户可安装、可配置、可从 GitHub Release 直接下载的 Windows 应用。
- 范围：Windows x64、单窗口 GUI、MSI 安装包、GitHub Release 自动发布、客户端低延迟播放优化。
- 技术选型：`.NET 10 WPF`、`hostd.exe` 子进程托管、`WiX Toolset v4`、`GitHub Actions`。

当前 Windows GUI 最低开发基线：

- `.NET 10 SDK`
- `Visual Studio Community 2026`
- Visual Studio 相关组件优先通过 Visual Studio Installer 管理
- NuGet restore 由 Visual Studio 或 `dotnet` 构建链路处理，不属于 Visual Studio Installer 管辖

## 阶段 1：GUI MVP 与 `hostd` 托管

- 新增 `apps/windows-launcher`
- GUI 只暴露 Windows 常用参数：
  - `host`
  - `port`
  - `source=wasapi/sine`
  - `wasapi-role`
  - `seconds`
- GUI 行为：
  - Start 时拼接命令行并启动隐藏的 `hostd.exe`
  - Stop 时终止子进程树
  - 展示状态、命令预览、stdout/stderr 日志
  - 配置保存到 `%AppData%\NetworkSpeaker\settings.json`
- 闭环：
  - 自动化测试覆盖参数映射、配置持久化、状态机和异常退出
  - 手工验证 `source=sine`、`seconds=3`、`127.0.0.1:50000`

## 阶段 2：MSI 与本地打包闭环

- 新增 `installer/windows`
- 打包流程：
  1. CMake 构建 `hostd.exe`
  2. `dotnet publish` 自包含 GUI
  3. WiX 生成 MSI
  4. 输出 SHA256
- 安装包固定包含：
  - GUI
  - `hostd.exe`
  - Start Menu 快捷方式
  - 卸载入口
- 闭环：
  - 本地脚本成功产出 MSI 和 SHA256
  - 干净 Windows 机器可安装、启动、卸载

## 阶段 3：GitHub Release 自动化

- 新增 `.github/workflows/release.yml`
- 触发：
  - `push tags: v*`
  - `workflow_dispatch`
- 自动化行为：
  - 构建 `hostd.exe`
  - 运行 GUI 测试
  - 发布 GUI
  - 生成 MSI
  - 上传 Release asset
- 闭环：
  - dry run 能产出 artifact
  - tag 发布能产出正式 GitHub Release

## 阶段 4：实机验收

- 从 GitHub Release 下载 MSI
- 安装到普通 Windows 机器
- 启动 GUI
- 配置 Android 真机 IP、`port=50000`、`source=wasapi`、`wasapi-role=multimedia`
- 播放浏览器视频
- Android 真机成功出声

## 阶段 5：低延迟优化（先计划，后实施）

### 目标

- 启动阶段允许小幅丢包，优先快速贴近“最新音频”
- 稳定阶段维持低延迟并尽量平滑播放
- 保持现有协议兼容，不引入破坏性变更

### 5.1 可观测性（先做）

- 在客户端补充并输出关键指标：
  - `e2e_latency_ms`
  - `jitter buffer` 深度（当前值/均值/峰值）
  - `packets_lost`、`late_dropped`、`duplicates_dropped`、`packets_reordered`
  - 启动阶段主动丢弃包计数（用于“追最新”评估）
- 增加低频日志或调试状态面板，便于回归对比

### 5.2 启动阶段：最新优先锁定（核心）

- 在 `PlayerPipeline` 增加启动态（`FastLock`）
- 启动时 `jitter buffer` 累积到阈值后，允许将 `expected_sequence` 快速跳到较新位置
  - 例如接近 `newest_sequence - kStartupLead`
- 跳过的旧包计为“启动阶段主动丢弃”，不视为异常
- 进入稳定态条件（建议）：连续命中按序包 + buffer 深度回到目标区间

### 5.3 稳定阶段：低延迟优先的顺序播放

- 稳定态继续按 `sequence` 顺序解码播放
- 当检测到持续高延迟时，执行“软追帧”（小步跳过明显过期帧）
- 保留现有重排与去重逻辑，避免因乱序导致音质抖动

### 5.4 时间戳对齐与过期包丢弃

- 基于 `capture_ts_us` 与本地时钟估算“可播放时间窗口”
- 对明显过期的帧直接丢弃，避免排队播放历史音频造成延迟堆积
- 该策略只在延迟超阈值时触发，避免常态误伤

### 5.5 参数化与回退机制

- 增加可配置参数（先内部配置，后续按需要开放到 GUI）：
  - `startup_fast_lock_enabled`（默认开）
  - `startup_buffer_packets`
  - `startup_lead_packets`
  - `steady_target_packets`（默认较小，如 2~4）
  - `late_frame_drop_threshold_ms`
- 保留一键回退到“纯顺序模式”的开关，降低上线风险

### 5.6 可选项（按优先级）

1. **可选 A（高优先）**：自适应 jitter 目标深度
   - 网络稳定时减小 buffer，抖动增大时临时放宽
2. **可选 B（中优先）**：Opus 抗丢包参数
   - 评估 `OPUS_SET_PACKET_LOSS_PERC`、`OPUS_SET_INBAND_FEC` 的收益与代价
3. **可选 C（中优先）**：分级延迟策略
   - `LowLatency` / `Balanced` 两档，便于不同网络条件切换
4. **可选 D（低优先）**：发送端轻量节流/打点
   - 增加更精细的发送间隔与时间戳观测，便于端到端定位

### 5.7 验收口径（低延迟专项）

- 启动后在可接受时间内进入稳定低延迟状态
- 稳态 `e2e_latency_ms` 相对当前基线显著下降
- 主观听感无持续“越播越晚”现象
- 弱网下允许轻微瞬时失真，但不出现长时间堆积延迟

## 验收标准

- 用户不需要源码构建和手敲命令
- README 明确指向 Release 下载入口
- GUI 参数集与当前 `hostd` 首版支持的 Windows 常用参数一致
- GUI 多次 Start/Stop 不出现僵尸进程和状态错乱
- 首个 Release 能在未安装开发工具链的 Windows 机器上完成安装并成功发送一次音频
