# DESIGN

本文档说明 `network_speaker` 当前的系统架构，以及新增的 Windows GUI / MSI / Release 设计。

## 系统目标

项目目标是把桌面端当前播放的音频以尽可能低的延迟转发到 Android 设备，让 Android 设备作为网络扬声器。

当前主路径是：

- Windows 发送端
- Android 接收端
- 局域网 UDP 传输
- Opus 低延迟编码

## 高层模块

系统当前分成四层：

1. 音频采集与播放平台层
2. 编解码与传输层
3. 终端应用层
4. Windows 分发层

对应模块：

- `server/hostd`
  - 音频采集、编码、发包
- `client/core`
  - 收包、抖动缓冲、解码、播放回调
- `client/android-app`
  - Android 生命周期、前台服务、`AudioTrack`
- `apps/windows-launcher`
  - Windows 图形界面、配置持久化、`hostd.exe` 子进程托管
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
2. 同一流中如果缺失某个 `expected_sequence`，但缓冲区已经积累了更高序列号且重新达到目标深度，则跳过缺失包并继续播放

当前接收侧恢复点位于：

- `client/core/src/player_pipeline.cpp`
- `libs/transport/src/jitter_buffer.cpp`

目标是避免：

- 第一次丢包后永久静音
- 发送端重启后，客户端继续把新流当旧包丢弃

## Windows GUI 架构

首版 Windows GUI 不是重写发送逻辑，而是现有 `hostd.exe` 的图形前端。

当前设计固定为：

- `NetworkSpeaker.Launcher`
  - WPF UI
- `NetworkSpeaker.Launcher.Core`
  - 参数模型
  - 命令行构造
  - 设置持久化
  - `hostd.exe` 子进程托管
- `NetworkSpeaker.Launcher.Core.Tests`
  - GUI 核心逻辑测试

GUI 不通过 native IPC 控制发送端，而是：

1. 根据表单生成命令行
2. 启动隐藏的 `hostd.exe`
3. 读取 stdout/stderr
4. 监控退出码
5. 用户点击 `Stop` 时终止整个进程树

这样做的原因：

- 不需要重构现有 `hostd` 主程序
- GUI 和 CLI 可以共享同一套发送逻辑
- 出问题时容易回退到命令行验证

## GUI 参数边界

GUI 首版只暴露 Windows 常用参数：

- `host`
- `port`
- `source`
  - `wasapi`
  - `sine`
- `wasapi-role`
- `seconds`

明确不在 GUI 中暴露：

- Linux `pulse` 相关参数
- 其他实验性参数

## 配置持久化

GUI 配置持久化到：

- `%AppData%\NetworkSpeaker\settings.json`

持久化内容：

- `host`
- `port`
- `source`
- `wasapi-role`
- `seconds`

`hostd.exe` 路径不持久化，运行时动态发现：

- 优先同目录 `hostd.exe`
- 开发环境下再回退到仓库常见构建目录

## MSI 分发设计

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

## Release 自动化设计

GitHub Actions Release 流程固定为：

1. `push` tag `v*`
2. setup-dotnet 10
3. 初始化 MSVC 工具链
4. 构建 `hostd.exe`
5. 运行 GUI 核心测试
6. `dotnet publish` GUI
7. 生成 MSI
8. 生成 SHA256
9. 上传到 GitHub Release

`workflow_dispatch` 只做 dry run：

- 构建
- 打包
- 上传 artifact
- 不创建正式 Release

## 当前边界

当前不覆盖：

- Linux/macOS GUI
- 自动发现与配对
- 握手控制信道
- 加密
- FEC
- 多声道
- 更强的 Android 后台保活
- 代码签名和 SmartScreen 体验优化

## 相关文档

- 用户说明：[../README.md](../README.md)
- 开发说明：[CONTRIBUTE.md](CONTRIBUTE.md)
- 实施计划：[../plan-1.md](../plan-1.md)
- 阶段进展：[../progress.md](../progress.md)
- 调试记录：[../debug.md](../debug.md)
