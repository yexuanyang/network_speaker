# DESIGN

本文档说明 `network_speaker` 当前的系统架构、桌面 GUI 设计、以及分发策略。

## 系统目标

项目目标是把桌面端当前播放的音频以尽可能低的延迟转发到 Android 设备，让 Android 设备作为网络扬声器。

当前主路径是：

- Windows / Linux 发送端
- Android 接收端
- 局域网 UDP 传输
- Opus 低延迟编码

## 高层模块

系统当前分成四层：

1. 音频采集与播放平台层
2. 编解码与传输层
3. 终端应用层
4. 分发层

对应模块：

- `server/hostd`
  - 音频采集、编码、发包
- `client/core`
  - 收包、抖动缓冲、解码、播放回调
- `client/android-app`
  - Android 生命周期、前台服务、`AudioTrack`
- `apps/desktop`（Tauri + Vue）
  - 跨平台桌面 GUI、配置持久化、`hostd` 子进程托管
- `apps/windows-launcher`
  - Windows WPF 图形界面（.NET 10）
- `installer/windows`
  - MSI 安装器
- `.github/workflows/release.yml`
  - GitHub Release 自动化

## 发送端主链路

发送端数据流：

1. 平台采集模块生成 `PcmFrame`
2. `OpusEncoder` 编码为 Opus payload
3. `UdpAudioSender` 组装 `AudioPacket`
4. 通过 UDP 发往目标地址

当前采集源：

- `sine`
  - 直接生成测试蜂鸣声
- `pulse`
  - Linux `PulseAudio` monitor source
- `wasapi`
  - Windows 默认渲染设备 `WASAPI loopback`

## 接收端主链路

接收端数据流：

1. `Receiver` 从 UDP socket 收包
2. `TryParsePacket()` 解析为 `AudioPacket`
3. `PlayerPipeline` 推入 `JitterBuffer`
4. `PlayerPipeline` 在可播放时序下弹出包
5. `OpusDecoder` 解码为 PCM
6. sink 消费 PCM

Android 端 sink 路径：

- JNI 回调到 Kotlin
- Kotlin `AudioOutput` 写入 `AudioTrack`

## 协议关键字段

`AudioPacketHeader` 当前关键字段：

- `stream_id`
- `sequence`
- `capture_ts_us`
- `frame_samples`

语义：

- `stream_id`
  - 标识一次发送会话
  - `hostd` 每次启动生成新的 `stream_id`
- `sequence`
  - 同一条流内单调递增

这样可以区分：

- 发送端重启后的新流
- 同一条流中的普通丢包或乱序

## 丢包恢复设计

真机比模拟器更容易暴露网络抖动问题，因此接收侧现在采用两段式恢复：

1. 新 `stream_id` 到达时，重置播放流水线
2. 同一流中如果缺失某个 `expected_sequence`，但缓冲区已经积累了更高序列号且重新达到目标深度，则尝试恢复并继续播放

丢帧恢复策略（优先级从高到低）：

1. **Opus Inband FEC** — 调用 `DecodeFEC` 从下一个包提取冗余数据重建丢帧；当前编码端使用 `RESTRICTED_LOWDELAY`（CELT-only）模式未启用 inband FEC，故此路径实际退化为 PLC
2. **PLC（Packet Loss Concealment）** — 由解码器根据内部状态外推生成平滑填充帧，作为主要丢帧恢复手段
3. **连续 PLC 限制** — 单次丢包间隙最多生成 `max_plc_frames_per_gap` 个 PLC 帧，超出则硬跳避免持续静音

当前接收侧恢复点位于：

- `client/core/src/player_pipeline.cpp`
- `libs/transport/src/jitter_buffer.cpp`

统计数据通过 `stream_stats` 的 `plc_concealed` 和 `fec_recovered` 字段追踪。

目标是避免：

- 第一次丢包后永久静音
- 发送端重启后，客户端继续把新流当旧包丢弃

## 桌面 GUI 架构

桌面 GUI 是 `hostd` 的图形前端，不嵌入发送逻辑，而是启动 `hostd` 子进程并监控其 stdout/stderr。

### Tauri 桌面应用（跨平台）

`apps/desktop` 基于 Tauri v2 + Vue 3，支持 Windows 和 Linux：

- **前端**（Vue 3）
  - 配置表单、设备枚举、运行状态显示、日志面板
  - 通过 Tauri IPC 调用后端命令
- **后端**（Rust）
  - `hostd_locator` — 运行时定位 `hostd` 二进制
  - `hostd_command` — 构造命令行参数
  - `hostd_process` — 子进程生命周期管理
  - `device_enumerator` — 调用 `hostd --list-devices` 枚举音频设备
  - `settings` — 配置持久化
  - `virtual_audio` — 虚拟音频设备检测
- **构建集成**
  - `build.rs` 自动从 CMake 构建输出复制 `hostd` 到 `binaries/` 目录
  - `tauri.conf.json` 通过 `externalBin` 将 `hostd` 声明为 sidecar

GUI 与 `hostd` 的交互方式：

1. 根据表单生成命令行
2. 启动 `hostd` 子进程
3. 读取 stdout/stderr
4. 监控退出码
5. 用户点击 `Stop` 时终止进程

这样做的原因：

- 不需要重构现有 `hostd` 主程序
- GUI 和 CLI 可以共享同一套发送逻辑
- 出问题时容易回退到命令行验证

### Windows WPF 启动器（.NET 10）

`apps/windows-launcher` 是独立的 Windows 原生 GUI：

- `NetworkSpeaker.Launcher` — WPF UI
- `NetworkSpeaker.Launcher.Core` — 参数模型、命令行构造、设置持久化、子进程托管
- `NetworkSpeaker.Launcher.Core.Tests` — 核心逻辑单元测试

## GUI 参数边界

桌面 GUI 暴露的参数按平台区分：

- `host` — 目标地址
- `port` — 目标端口
- `source` — 采集源
  - Windows: `wasapi`, `sine`
  - Linux: `pulse`, `sine`
- `wasapi-role` — Windows WASAPI 角色（仅 Windows）
- `device` — WASAPI 设备 ID（仅 Windows）
- `pulse-source` — PulseAudio source 名称（仅 Linux）
- `seconds` — 限制发送时长（可选）

## 配置持久化

GUI 配置持久化路径按平台区分：

- Windows: `%AppData%\NetworkSpeaker\settings.json`
- Linux: `~/.config/network-speaker/settings.json`（XDG 规范）

持久化内容：

- `host`、`port`、`source`、`wasapi-role`、`pulse-source`、`device-id`、`seconds`

### hostd 二进制定位策略

`hostd` 路径不持久化，由 Tauri 桌面应用在运行时通过 `hostd_locator` 按以下优先级搜索：

1. **环境变量** — `NSPEAKER_HOSTD_PATH` 显式指定路径
2. **Tauri sidecar（生产构建）** — `resource_dir/hostd[.exe]`，Tauri 打包时 sidecar 被提取到资源目录，不带 target triple 后缀
3. **Tauri sidecar（开发模式）** — `resource_dir/binaries/hostd-{target_triple}[.exe]`，`cargo tauri dev` 时 sidecar 保持 `build.rs` 放置的原始命名
4. **当前可执行文件目录** — 如 RPM 安装时 `network-speaker-desktop` 和 `hostd` 都在 `/usr/bin/`
5. **系统 PATH** — 遍历 PATH 环境变量搜索，适配通过包管理器安装的 `hostd`

步骤 2 和 3 覆盖 Tauri 的 `externalBin` sidecar 机制（生产与开发两种命名方式），步骤 4 和 5 覆盖系统级安装场景。

## 分发设计

### Windows

MSI 安装包固定包含：

- GUI 主程序 `NetworkSpeaker.exe`
- 同目录 `hostd.exe`
- Start Menu 快捷方式
- 卸载入口

首版不做：

- 系统托盘
- 桌面快捷方式
- 开机自启
- Windows Service
- 代码签名

### Linux

Tauri 构建产出多种格式：

- **AppImage** — 自包含单文件，无需安装
- **deb** — Debian/Ubuntu 系
- **tar.gz** — 通用归档，包含 `network-speaker-desktop` 和 `hostd`

deb 包（Tauri 自动构建）文件布局：

- `/usr/bin/network-speaker-desktop` — 主程序
- `/usr/lib/network-speaker-desktop/hostd` — sidecar（Tauri `externalBin` 机制）
- `/usr/share/applications/network-speaker-desktop.desktop`
- `/usr/share/icons/hicolor/...`

hostd 作为 sidecar 位于资源目录，由 `hostd_locator` 步骤 2（`resource_dir/hostd`）定位。

RPM 包（独立构建）将两者安装到系统路径：

- `/usr/bin/hostd`
- `/usr/bin/network-speaker-desktop`
- `/usr/share/applications/Network Speaker.desktop`
- `/usr/share/icons/hicolor/...`

hostd 与主程序同在 `/usr/bin/`，由 `hostd_locator` 步骤 4（当前可执行文件目录）定位。

## Release 自动化设计

GitHub Actions Release 流程由 `push` tag `v*` 触发，`staging` 分支 push 生成 pre-release。
`workflow_dispatch` 只做 dry run（构建、打包、上传 artifact，不创建正式 Release）。

各平台构建步骤：

### Windows

1. 初始化 MSVC 工具链
2. 构建 `hostd.exe`
3. 运行 GUI 核心测试
4. `dotnet publish` WPF 启动器
5. 生成 MSI + SHA256
6. 上传到 GitHub Release

### Linux

1. 构建 `hostd` + 运行 C++ 测试
2. 打包为 tar.gz 归档
3. 构建 Tauri 桌面应用（deb / AppImage）
4. 在 Fedora 容器中构建 RPM
5. 上传到 GitHub Release

### Android

1. 构建 Release APK
2. 上传到 GitHub Release

## 当前边界

当前不覆盖：

- 自动发现与配对
- 握手控制信道
- 加密
- Opus inband FEC（需切换到 SILK 混合编码模式，与当前低延迟 CELT-only 模式冲突）
- 多声道
- 更强的 Android 后台保活
- 代码签名和 SmartScreen 体验优化

## 相关文档

- 用户说明：[../README.md](../README.md)
- 开发说明：[CONTRIBUTE.md](CONTRIBUTE.md)
