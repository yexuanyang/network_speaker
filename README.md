# network_speaker

`network_speaker` 用来把 Windows 电脑当前播放的声音通过局域网转发到 Android 设备，让手机、平板或 Android 模拟器作为网络扬声器使用。

当前推荐的用户使用路径是：

- 从 [GitHub Releases](https://github.com/yexuanyang/network_speaker/releases) 下载 Windows 安装包
- 安装 `Network Speaker`
- 在图形界面里填写 Android 设备 IP、端口和音频来源
- 点击 `Start` 开始转发

如果你是开发者，请看 [docs/CONTRIBUTE.md](docs/CONTRIBUTE.md)。如果你想了解系统架构，请看 [docs/DESIGN.md](docs/DESIGN.md)。

Windows GUI 的开发基线当前为 `.NET 10 SDK + Visual Studio Community 2026`。Visual Studio 相关组件优先通过 Visual Studio Installer 管理；NuGet restore 不属于 Visual Studio Installer 管辖，需由 Visual Studio 或 `dotnet` 在构建/测试时完成。

## 从 Releases 下载

首选方式：

- 打开 [GitHub Releases](https://github.com/yexuanyang/network_speaker/releases)
- 下载最新的 `NetworkSpeaker-<version>-win-x64.msi`
- 如需校验完整性，可同时下载对应的 `*.sha256.txt`
- 安装后从开始菜单启动 `Network Speaker`

如果当前 Release 里还没有可用安装包，你也可以继续使用命令行版 `hostd.exe`。

## 图形界面使用

首版 GUI 支持这些参数：

- `Target IP`
- `UDP Port`
- `Source`
  - `Wasapi`
  - `Sine`
- `WASAPI Role`
  - `Auto`
  - `Multimedia`
  - `Console`
  - `Communications`
- `Seconds (optional)`

默认值：

- `UDP Port = 50000`
- `Source = Wasapi`
- `WASAPI Role = Multimedia`
- `Seconds` 留空，表示持续发送直到点击 `Stop`

使用步骤：

1. 在 Android 客户端中启动接收，确认显示 `Listening on UDP ...`
2. 在 Windows GUI 中填写 Android 设备 IPv4 地址
3. 保持端口与手机端一致，默认 `50000`
4. 浏览器视频或媒体播放器场景，优先用 `Source=Wasapi` 且 `WASAPI Role=Multimedia`
5. 点击 `Start`
6. 需要停止时点击 `Stop`

如果你只是想确认链路是否通，可以先把 `Source` 切换为 `Sine`，然后短时间发送蜂鸣声。

## 仍然可用的命令行方式

如果你希望手动运行 `hostd.exe`，最常见的真机场景命令是：

```powershell
.\out\build\windows-ninja-vcpkg\hostd.exe --host <手机IP> --port 50000 --source wasapi --wasapi-role multimedia
```

测试蜂鸣声：

```powershell
.\out\build\windows-ninja-vcpkg\hostd.exe --host <手机IP> --port 50000 --source sine --seconds 4
```

## Android 模拟器使用说明

如果接收端是 Android 模拟器，不要默认把发送目标写成 `10.0.2.16`。当前已验证更稳定的方式是：

1. 先做 UDP 重定向
2. 再把发送目标设为 `127.0.0.1`

示例：

```powershell
$adb = 'C:\Users\11822\AppData\Local\Android\Sdk\platform-tools\adb.exe'

& $adb emu redir add udp:50000:50000
.\out\build\windows-ninja-vcpkg\hostd.exe --host 127.0.0.1 --port 50000 --source sine --seconds 4
```

## 常见问题

### GUI 里显示 `hostd.exe not found`

优先检查：

- 你是否安装的是完整 MSI，而不是只拿到了 GUI 可执行文件
- 如果是开发环境运行 GUI，仓库里是否已经构建出 `out\build\windows-ninja-vcpkg\hostd.exe`

### 客户端显示 `Listening on ...`，但没有声音

优先检查：

- Android 客户端监听端口是否和发送端一致
- `Sender IPv4 Filter` 是否填错，拿不准就先留空
- 真机和电脑是否在同一局域网
- 浏览器视频场景是否使用了 `WASAPI Role = Multimedia`

### 模拟器听不到声音

优先检查：

- 是否已经执行 `adb emu redir add udp:50000:50000`
- 发送目标是否为 `127.0.0.1`
- 模拟器里的接收端是否已经进入 `Listening on UDP 50000 ...`

### 真机播放一段时间后无声

仓库当前已经针对接收侧丢包恢复补了修复。如果你遇到“放一会后无声、重连后又恢复”的情况，请优先使用最新安装包或最新代码重新构建 Android 客户端后再验证。

详细排查记录见 [debug.md](debug.md)。

## 当前边界

已支持：

- Windows `WASAPI loopback` 发送
- Android 真机接收
- Android 模拟器接收
- 浏览器视频与系统音频转发
- Windows 图形界面启动/停止发送
- Windows MSI 安装包与 GitHub Release 分发流程

当前还不做：

- Linux/macOS GUI
- 系统托盘
- 开机自启
- 自动发现与配对
- FEC
- 加密
- 多声道
- 代码签名

## 文档

- 开发参与说明：[docs/CONTRIBUTE.md](docs/CONTRIBUTE.md)
- 架构设计：[docs/DESIGN.md](docs/DESIGN.md)
- 实施计划：[plan-1.md](plan-1.md)
- 阶段进展：[progress.md](progress.md)
- 当前调试记录：[debug.md](debug.md)
