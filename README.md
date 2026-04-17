# Network Speaker

把电脑正在播放的声音实时转发到手机，让手机变成你的无线网络扬声器。

支持 Windows / Linux 作为发送端，Android / HarmonyOS 作为接收端，局域网内即可使用，无需互联网。

## 工作原理

```
电脑 (发送端)                          手机 (接收端)
┌──────────────┐    局域网 UDP     ┌──────────────┐
│ 系统音频采集  │ ──→ Opus 编码 ──→ │ 抖动缓冲     │
│ WASAPI/Pulse │    低延迟传输     │ Opus 解码    │
└──────────────┘                   │ 扬声器播放    │
                                   └──────────────┘
```

**核心链路**：桌面系统音频 → Opus 低延迟编码 → UDP 发包 → 接收端抖动缓冲与重排 → Opus 解码 → 扬声器播放

**关键设计**：
- 使用 Opus 编解码，在低带宽下保持高音质
- UDP 传输配合自定义抖动缓冲区（JitterBuffer），平衡延迟与丢包恢复
- 每次发送会话生成唯一 `stream_id`，接收端可区分发送端重启与普通丢包
- 丢包恢复采用两段式策略：新流重置 + 缓冲区深度恢复，避免单次丢包导致永久静音

## 下载

### Windows 发送端

从 [GitHub Releases](https://github.com/yexuanyang/network_speaker/releases) 下载最新的 MSI 安装包：

- `NetworkSpeaker-<version>-win-x64.msi` — Windows 安装包
- 可选下载 `*.sha256.txt` 校验完整性

### Android 接收端

从 [GitHub Releases](https://github.com/yexuanyang/network_speaker/releases) 下载最新的 APK：

- `NetworkSpeaker-<version>.apk` — Android 安装包

### HarmonyOS 接收端

在华为应用市场搜索 **NetworkSpeaker** 下载安装。

## Quick Start

### Windows + Android 手机

1. 确保电脑和手机连接在**同一局域网**
2. 在 Windows 上安装并启动 **Network Speaker**
3. 在 Android 手机上安装并打开 **NetworkSpeaker**，点击开始接收，确认界面显示 `Listening on UDP ...`
4. 在 Windows GUI 中填写手机的 IPv4 地址，端口保持默认 `50000`
5. 点击 **Start**，电脑上播放的音频将实时传输到手机扬声器
6. 需要停止时点击 **Stop**

### Windows + HarmonyOS 手机

1. 确保电脑和手机连接在**同一局域网**
2. 在 Windows 上安装并启动 **Network Speaker**
3. 在华为应用市场下载 **NetworkSpeaker** 并打开，点击开始接收
4. 在 Windows GUI 中填写手机的 IPv4 地址，端口保持默认 `50000`
5. 点击 **Start** 开始转发
6. 需要停止时点击 **Stop**

### Linux + Android / HarmonyOS 手机

Linux 发送端目前仅提供命令行方式，需从源码构建 `hostd`（参见 [CLAUDE.md](CLAUDE.md) 中的构建说明）。

```bash
./hostd --host <手机IP> --port 50000 --source pulse
```

手机端操作与上述步骤相同。

### Android 模拟器

如果接收端是 Android 模拟器，需要先做 UDP 端口重定向：

```powershell
adb emu redir add udp:50000:50000
```

然后在 Windows GUI 中将目标地址填写为 `127.0.0.1`，端口 `50000`，点击 Start。

## Windows GUI 参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| Target IP | Android 设备的 IPv4 地址 | — |
| UDP Port | 传输端口，需与手机端一致 | `50000` |
| Source | 音频来源：`Wasapi`（系统音频）或 `Sine`（测试蜂鸣声） | `Wasapi` |
| WASAPI Role | 音频角色：`Multimedia` / `Console` / `Communications` / `Auto` | `Multimedia` |
| Seconds | 发送时长，留空表示持续发送直到手动停止 | 留空 |

> 浏览器视频或媒体播放器场景，推荐使用 `Source = Wasapi` + `WASAPI Role = Multimedia`。
>
> 如果只想验证连接是否正常，可以将 `Source` 切换为 `Sine` 发送测试蜂鸣声。

## 常见问题

### 手机端显示 Listening 但没有声音

- 检查手机和电脑是否在**同一局域网**
- 检查手机端监听端口是否与发送端一致（默认均为 `50000`）
- 检查手机端 `Sender IPv4 Filter` 是否填错，不确定时留空
- 浏览器视频场景确认使用 `WASAPI Role = Multimedia`

### GUI 显示 hostd.exe not found

- 确认安装的是完整的 MSI 安装包，而不是单独的 GUI 可执行文件

### 模拟器听不到声音

- 确认已执行 `adb emu redir add udp:50000:50000`
- 发送目标地址应为 `127.0.0.1`
- 模拟器中的接收端应已进入 `Listening on UDP 50000 ...` 状态

### 播放一段时间后无声

接收端已内置丢包恢复机制。如果遇到此问题，请优先更新到最新版本后再验证。

## 许可证

[MIT](LICENSE)

