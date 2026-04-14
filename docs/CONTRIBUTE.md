# CONTRIBUTE

本文档面向开发者，说明如何继续开发 `network_speaker`，尤其是 Windows GUI、MSI 打包和 GitHub Release 流程。

## 仓库结构

- `libs/audio_base`
  - 公共音频类型、时钟、统计结构
- `libs/transport`
  - 音频包格式、UDP socket、抖动缓冲
- `libs/codec_opus`
  - Opus 编解码封装
- `server/hostd`
  - 命令行发送端
- `client/core`
  - 接收端核心流水线
- `client/android-app`
  - Android 应用壳与 JNI 桥接
- `apps/windows-launcher`
  - Windows GUI 与其测试
- `installer/windows`
  - WiX v4 MSI 工程
- `tools`
  - 打包脚本与辅助脚本
- `.github/workflows`
  - Release 自动化

## 本地开发前提

### C++ 发送端

仓库当前通过 `CMakePresets.json` 统一管理 Windows 和 Linux 的本地构建目录。推荐优先使用 preset，而不是手写 `-B` 路径。

Windows 需要 Visual Studio Developer PowerShell（或已激活 `vcvars64.bat`），以及设置好 `VCPKG_ROOT` 环境变量。

```powershell
$env:VCPKG_ROOT = "D:\tools\vcpkg"   # 按实际路径修改
cmake --preset windows-ninja-vcpkg
cmake --build --preset windows-ninja-vcpkg
ctest --preset windows-ninja-vcpkg
```

Windows 产物位于 `out/build/windows-ninja-vcpkg/`。

Linux 需要系统编译器、CMake、Ninja，以及开发包：

- `cmake`
- `ninja` 或 `ninja-build`
- `pkg-config`
- `opus`
- `libpulse`
- `libpulse-simple`
- 可选：`gtest`，如果本机未安装，开启测试时 CMake 会尝试自动获取

```bash
cmake --preset linux-ninja-system
cmake --build --preset linux-ninja-system
ctest --preset linux-ninja-system
```

Linux 产物位于 `out/build/linux-ninja-system/`。

编译器方面，项目的实际构建支持 GCC 和 Clang；选择哪一个编译器由你运行 `cmake --preset ...` 时所在环境中的 `CC` / `CXX` 或工具链决定。

### VS Code / LSP

如果使用 VS Code，推荐安装：

- CMake Tools
- clangd

仓库中的 `.vscode/settings.json` 默认会：

- 使用 `CMakePresets.json`
- 在 configure 后把 `compile_commands.json` 复制到仓库根目录
- 让 `clangd` 从仓库根目录读取编译数据库

仓库根目录的 `.clangd` 只用于提升 `clangd` 在 GCC/CMake 构建目录上的兼容性，会过滤掉少量 GCC 专用的模块依赖扫描参数，不会影响 Windows 或 Linux 上的实际编译。

### Windows GUI

Windows GUI 使用 `.NET 10 WPF`。

项目：

- `apps/windows-launcher/NetworkSpeaker.Launcher`
- `apps/windows-launcher/NetworkSpeaker.Launcher.Core`
- `apps/windows-launcher/NetworkSpeaker.Launcher.Core.Tests`

本地需要：

- .NET 10 SDK
- 仓库根目录的 `global.json` 会把 SDK 锁定到当前 `.NET 10` feature band
- Visual Studio Community 2026 或带 C++ 工作负载的 Visual Studio Build Tools
- Visual Studio 相关组件优先通过 Visual Studio Installer 安装
- NuGet restore 不由 Visual Studio Installer 管理；首次构建/测试时需要由 Visual Studio 或 `dotnet restore`/`dotnet build`/`dotnet test` 完成包还原

构建 GUI：

```powershell
dotnet build .\apps\windows-launcher\NetworkSpeaker.Launcher\NetworkSpeaker.Launcher.csproj
```

运行 GUI 核心测试：

```powershell
dotnet test .\apps\windows-launcher\NetworkSpeaker.Launcher.Core.Tests\NetworkSpeaker.Launcher.Core.Tests.csproj
```

### Android

推荐使用 Android Studio 直接打开 `client/android-app/`。

本地需要：

- Android SDK
- Android NDK `27.1.12297006`
- CMake `3.22.1`
- JDK 17

`client/android-app/local.properties` 需要自行配置 `sdk.dir=...`，该文件不入库。

## GUI 实现约定

首版 GUI 只做 Windows 常用参数：

- `host`
- `port`
- `source=wasapi/sine`
- `wasapi-role`
- `seconds`

明确不做：

- Linux `pulse` 参数可视化
- 托盘常驻
- 开机自启
- 把 `hostd` 逻辑直接嵌入 GUI 进程

当前实现策略固定为：

- GUI 负责拼接命令行
- GUI 启动隐藏的 `hostd.exe` 子进程
- GUI 读取 stdout/stderr 并更新状态
- 配置持久化到 `%AppData%\NetworkSpeaker\settings.json`

## MSI 打包

MSI 使用 WiX Toolset v4。

安装工程：

- `installer/windows/NetworkSpeaker.Installer.wixproj`
- `installer/windows/Product.wxs`

本地一键打包脚本：

```powershell
.\tools\package-windows-installer.ps1
```

如果需要显式指定发布展示版本：

```powershell
.\tools\package-windows-installer.ps1 -Version 0.1.0-rc1
```

脚本会完成：

1. 构建 `hostd.exe`
2. `dotnet publish` Windows GUI
3. 调用 WiX 生成 MSI
4. 产出 SHA256 文件

默认产物位置：

- `artifacts/windows-launcher/publish`
- `artifacts/release`

## GitHub Release

Release workflow：

- `.github/workflows/release.yml`

触发方式：

- `push` tag：`v*`
- `workflow_dispatch`：用于 dry run

行为：

- 构建 GUI 测试
- 构建 `hostd.exe`
- 发布自包含 GUI
- 生成 MSI
- 生成 SHA256
- tag 场景下自动创建或更新 GitHub Release

Release 说明模板：

- `.github/release-notes-template.md`

## 测试建议

### C++ 回归

优先补到：

- `tests/audio_packet_test.cpp`
- `tests/jitter_buffer_test.cpp`
- `tests/end_to_end_test.cpp`

### GUI 回归

优先覆盖：

- 参数到命令行的映射
- 设置持久化
- 状态机转换
- 子进程异常退出

### Windows 本机验证

命令行最小回环：

```powershell
.\out\build\windows-ninja-vcpkg\clientd.exe --port 50000 --seconds 5
.\out\build\windows-ninja-vcpkg\hostd.exe --host 127.0.0.1 --port 50000 --source sine --seconds 2
```

GUI 最小验证：

- 启动 `Network Speaker`
- 选择 `Source=Sine`
- `Target IP=127.0.0.1`
- `Port=50000`
- `Seconds=3`
- 点击 `Start`
- 确认日志里出现 `Streaming to ...` 和 `Sent 300 frames ...`

### Android 模拟器验证

必须先做：

```powershell
adb emu redir add udp:50000:50000
```

然后发送到：

```text
127.0.0.1:50000
```

## 文档约定

- 用户说明：`README.md`
- 开发说明：`docs/CONTRIBUTE.md`
- 架构设计：`docs/DESIGN.md`
- 实施计划：`plan-1.md`
- 阶段进展：`progress.md`
- 调试记录：`debug.md`
