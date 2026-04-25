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
- 丢包恢复采用多段式策略：新流重置 + 缓冲区深度恢复 + Opus PLC 丢包隐藏 + FEC 前向纠错，弱网环境下避免持续静音

## 下载

从 [GitHub Releases](https://github.com/yexuanyang/network_speaker/releases) 下载对应平台的安装包。

### 桌面发送端（跨平台）

桌面端 GUI 基于 Tauri v2 + Vue 3 构建，支持 Windows 和 Linux：

- **Windows**: `NetworkSpeaker-Desktop-<version>-win-x64.zip` 或 `.msi`
- **Linux (Debian/Ubuntu)**: `Network Speaker_<version>_amd64.deb`
- **Linux (Fedora/RHEL)**: `Network Speaker-<version>.x86_64.rpm`
- **Linux (通用)**: `Network Speaker_<version>_amd64.AppImage`

> 旧版 Windows 专用安装包 `NetworkSpeaker-<version>-win-x64.msi` 仍可在 Releases 中找到。

### Android 接收端

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

Linux 发送端提供两种方式：

**方式一：桌面 GUI**（推荐）

安装上方下载的 `.deb` / `.rpm` / `.AppImage` 包后直接启动，操作方式与 Windows GUI 相同。

**方式二：命令行**

从源码构建 `hostd` 后使用：

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

## 从源码构建

### C++ 组件 (hostd)

#### Windows

需要 Visual Studio 开发者命令行环境和 vcpkg：

```powershell
cmake --preset windows-ninja-vcpkg
cmake --build --preset windows-ninja-vcpkg
ctest --preset windows-ninja-vcpkg
```

#### Linux

```bash
# 安装依赖 (Debian/Ubuntu)
sudo apt-get install build-essential ninja-build pkg-config \
  libopus-dev libpulse-dev libgtest-dev

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### 跨平台桌面 GUI (Tauri + Vue)

桌面 GUI 位于 `apps/desktop`，基于 Tauri v2 + Vue 3 + TypeScript。

#### 前置依赖

- Node.js 20+
- Rust 工具链 (rustup)
- 平台系统库：

```bash
# Debian/Ubuntu
sudo apt-get install libwebkit2gtk-4.1-dev libgtk-3-dev \
  libayatana-appindicator3-dev librsvg2-dev

# Fedora
sudo dnf install webkit2gtk4.1-devel gtk3-devel \
  libayatana-appindicator-gtk3-devel librsvg2-devel
```

#### 开发模式

```bash
cd apps/desktop
npm install
npx tauri dev
```

#### 生产构建

**方式一：CMake 统一构建**（推荐）

从项目根目录执行，CMake 会自动先构建 hostd，再构建桌面 GUI：

```bash
# Linux
cmake --preset linux-desktop
cmake --build --preset linux-desktop

# Windows (需要 Visual Studio 开发者命令行 + vcpkg)
cmake --preset windows-desktop
cmake --build --preset windows-desktop
```

**方式二：手动分步构建**

先构建 C++ 组件（hostd），再构建 Tauri 应用：

```bash
# 1. 构建 hostd（参考上方 C++ 组件构建步骤）
# 2. 构建桌面 GUI
cd apps/desktop
npm install
npx tauri build
```

> Linux 上如果 AppImage 打包时 `strip` 报 `.relr.dyn` 错误（常见于 Fedora 43+ 等新版发行版），加上 `NO_STRIP=true`：
>
> ```bash
> NO_STRIP=true npx tauri build
> ```

构建产出位于 `apps/desktop/src-tauri/target/release/bundle/`。

除安装包外，构建还会生成独立可执行文件 `apps/desktop/src-tauri/target/release/network-speaker-desktop`（Linux）或 `network-speaker-desktop.exe`（Windows），前端资源已嵌入其中，可直接运行而无需安装。

### Android 接收端

用 Android Studio 打开 `client/android-app/`，需要 NDK `27.1.12297006`、CMake `3.22.1`、JDK 17。

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

