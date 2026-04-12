# CONTRIBUTE

本文档面向开发者，说明如何在本仓库中继续开发、验证和提交改动。

## 仓库结构

- `libs/audio_base`
  - 公共音频类型、时钟、统计结构
- `libs/transport`
  - 音频包格式、UDP socket、抖动缓冲
- `libs/codec_opus`
  - Opus 编码器/解码器封装
- `server/hostd`
  - 桌面发送端 CLI
- `client/core`
  - 接收端公共核心逻辑
- `client/android-app`
  - Android 应用壳与 JNI 桥接
- `tests`
  - C++ 单测与协议级回归测试
- `tools`
  - 辅助脚本

## 开发前提

### Windows

建议使用 Visual Studio Developer PowerShell，或者先执行 `vcvars64.bat` 再进入仓库目录。

典型构建命令：

```powershell
cmake -S . -B build-windows -G "NMake Makefiles" -DBUILD_TESTING=OFF
cmake --build build-windows
```

如果需要测试：

```powershell
cmake -S . -B build-windows-verify -G "NMake Makefiles" -DBUILD_TESTING=ON
cmake --build build-windows-verify
```

说明：

- Windows 下默认使用仓库内 vendored 的 `third_party/opus`
- 如果本机没有可用的 GoogleTest，配置阶段会跳过 `network_speaker_tests`

### Linux

```bash
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Linux 当前主要用于：

- C++ 核心功能开发
- `PulseAudio` 路线验证
- 常规单元测试与协议级集成测试

### Android

推荐使用 Android Studio 直接打开 `client/android-app/`。

本地需要：

- Android SDK
- Android NDK `27.1.12297006`
- CMake `3.22.1`
- JDK 17

`client/android-app/local.properties` 需要配置 `sdk.dir=...`，该文件不入库。

## 建议工作流

1. 先明确改动属于哪一层：
   - 协议
   - 发送端
   - 接收端
   - Android 壳层
2. 尽量先补或更新最靠近问题的自动化测试
3. 再做平台联调
4. 更新相关文档

## 测试建议

### C++ 回归

优先补到：

- `tests/audio_packet_test.cpp`
- `tests/jitter_buffer_test.cpp`
- `tests/end_to_end_test.cpp`

其中：

- 包格式变更优先补 `audio_packet_test.cpp`
- 抖动/丢包逻辑优先补 `jitter_buffer_test.cpp`
- 流水线行为优先补 `end_to_end_test.cpp`

### Windows 本机验证

最小回环：

```powershell
.\build-windows-verify\clientd.exe --port 50000 --seconds 5
.\build-windows-verify\hostd.exe --host 127.0.0.1 --port 50000 --source sine --seconds 2
```

浏览器/系统音频：

```powershell
.\build-windows-verify\hostd.exe --host <目标IP> --port 50000 --source wasapi --wasapi-role multimedia
```

### Android 模拟器验证

必须先做：

```powershell
adb emu redir add udp:50000:50000
```

然后发送到：

```text
127.0.0.1:50000
```

不要默认用 `10.0.2.16` 作为 Windows 侧发送目标。

### Android 真机验证

重点观察：

- 前台服务是否持续存活
- UI 是否仍显示 `Listening on ...`
- 播放中断后是否需要手动重连
- 浏览器视频场景下 `--wasapi-role multimedia` 是否更稳定

## 文档约定

- 面向使用者的说明放在根目录 `README.md`
- 面向开发者的参与说明放在 `docs/CONTRIBUTE.md`
- 架构设计放在 `docs/DESIGN.md`
- 阶段进展记录放在 `progress.md`
- 单次调试过程记录放在 `debug.md`

当你做了以下类型改动时，建议同步更新文档：

- 用户可见的使用方式变化
- 协议字段变化
- 新增平台能力
- 关键稳定性问题的根因与修复

## 提交建议

- 一个提交尽量只覆盖一个明确主题
- 提交前至少确认：
  - 相关目标能编译
  - 最贴近本次改动的测试或回归验证已经执行
  - 文档已同步
- 如果当前工作树里有其他未提交改动，提交前先分清哪些属于本次工作，避免混入无关内容
