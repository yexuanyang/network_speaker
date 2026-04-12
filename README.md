# network_speaker

`network_speaker` 用来把电脑当前播放的声音通过局域网转发到 Android 设备，让手机、平板或 Android 模拟器作为网络扬声器使用。

当前最成熟的使用方式是：

- Windows 电脑作为发送端
- Android 真机或 Android 模拟器作为接收端
- 音频通过 UDP + Opus 低延迟编码传输

## 适合谁

- 想把 Windows 电脑的系统声音、浏览器视频声音转发到 Android 设备的人
- 想验证“电脑到手机的低延迟网络扬声器”方案的人
- 想继续完善这个项目的开发者

如果你是开发者，请直接看 [docs/CONTRIBUTE.md](docs/CONTRIBUTE.md)。

## 当前支持情况

- Windows 发送端：
  - `WASAPI loopback` 采集默认输出设备
  - 支持浏览器/媒体播放器等系统输出音频转发
- Android 接收端：
  - 可配置 UDP 监听端口
  - 可选发送端 IPv4 过滤
  - 前台服务 + 常驻通知
- Android 模拟器：
  - 可以用于联调，但需要先做 UDP 端口重定向
- Linux：
  - 当前有 `PulseAudio` monitor source 路线
  - 更偏实验性质，不是当前主用路径

## 快速开始

### 1. 启动 Android 客户端

在 Android 客户端中：

- 确认监听端口，例如 `50000`
- 如果你不确定发送端地址，先把 `Sender IPv4 Filter` 留空
- 点击 `Start Receiver`
- 看到 `Listening on UDP ...` 再开始发送

### 2. 在 Windows 上启动发送端

真机局域网播放，最常用的是这条命令：

```powershell
.\build-windows-verify\hostd.exe --host <手机IP> --port 50000 --source wasapi --wasapi-role multimedia
```

说明：

- `<手机IP>` 换成 Android 设备在局域网内的 IPv4 地址
- 浏览器视频、网页播放器这类场景，优先使用 `--wasapi-role multimedia`
- 停止发送时，直接 `Ctrl+C`

如果你只是想快速确认链路是否通，可以先发测试蜂鸣声：

```powershell
.\build-windows-verify\hostd.exe --host <手机IP> --port 50000 --source sine --seconds 4
```

## 常见使用场景

### Windows 浏览器视频转发到 Android 真机

```powershell
.\build-windows-verify\hostd.exe --host 10.29.25.86 --port 50000 --source wasapi --wasapi-role multimedia
```

### Windows 默认系统输出转发到 Android 真机

```powershell
.\build-windows-verify\hostd.exe --host 10.29.25.86 --port 50000 --source wasapi
```

### 发送测试蜂鸣声

```powershell
.\build-windows-verify\hostd.exe --host 10.29.25.86 --port 50000 --source sine --seconds 4
```

## Android 模拟器使用说明

如果接收端是 Android 模拟器，不要直接把 `hostd` 发到 `10.0.2.16`。当前仓库里稳定可工作的方式是：

1. 先做 UDP 端口重定向
2. 然后把 `hostd` 发到 `127.0.0.1`

示例：

```powershell
$adb = 'C:\Users\11822\AppData\Local\Android\Sdk\platform-tools\adb.exe'

& $adb emu redir add udp:50000:50000
.\build-windows-verify\hostd.exe --host 127.0.0.1 --port 50000 --source sine --seconds 4
```

## 排查建议

### 客户端显示 `Listening on ...`，但没有声音

优先检查：

- Android 客户端监听端口是否和 `hostd --port` 一致
- `Sender IPv4 Filter` 是否填错，拿不准就先留空
- 真机和电脑是否在同一局域网
- Windows 浏览器场景是否用了 `--wasapi-role multimedia`

### 模拟器听不到声音

优先检查：

- 是否已经执行 `adb emu redir add udp:50000:50000`
- `hostd` 是否发到了 `127.0.0.1` 而不是 `10.0.2.16`
- 模拟器里的接收端是否已经进入 `Listening on UDP 50000 ...`

### 真机播放一段时间后无声

仓库当前已经针对接收侧丢包恢复补了修复。如果你遇到“放一会后没声音，重连又恢复”的情况，请优先使用最新代码重新构建 Android client 后再验证。

当前这部分调试记录见 [debug.md](debug.md)。

## 文档导航

- 用户说明：当前文件
- 开发参与说明：[docs/CONTRIBUTE.md](docs/CONTRIBUTE.md)
- 架构设计：[docs/DESIGN.md](docs/DESIGN.md)
- 里程碑与阶段进展：[progress.md](progress.md)
- 当前调试记录：[debug.md](debug.md)

## 当前边界

- 已验证：
  - Windows 主机到 Android 真机的局域网播放
  - Windows 浏览器视频到 Android 真机的播放
  - Windows 到 Android 模拟器的播放
- 尚未完成：
  - 自动发现与配对
  - FEC
  - 加密
  - 多声道
  - 更完善的 Android 后台保活策略
