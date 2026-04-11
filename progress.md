# 网络扬声器项目进展

此文档记录当前已经完成的任务、验证结果和剩余工作，便于后续继续开发和联调。

## 已完成

### 1. 方案与计划落地

- 已根据 `researsh.md` 和需求整理实施计划，并写入 `plan.md`
- 已将计划生成约束持久化到记忆，便于后续继续按同一标准产出计划

### 2. C++20 主工程骨架

- 已建立顶层 CMake 工程
- 已拆分核心模块：
  - `libs/audio_base`
  - `libs/transport`
  - `libs/codec_opus`
  - `server/hostd`
  - `client/core`
  - `client/android-app`
- 已建立桌面端可编译目标：
  - `hostd`
  - `clientd`
  - `network_speaker_tests`

### 3. 音频公共类型与基础设施

- 已统一 PCM 格式为 `48 kHz / 2 声道 / float32`
- 已实现：
  - `PcmFrame`
  - 单调时钟 `SteadyClock`
  - `StreamStats`
  - 音频采集/播放抽象接口

### 4. Opus 编解码模块

- 已实现低延迟 Opus 编码器和解码器
- 已固定首版策略：
  - `48 kHz`
  - `2 channels`
  - `OPUS_APPLICATION_RESTRICTED_LOWDELAY`
  - `192 kbps`
  - `VBR off`
- 已兼容两类头文件路径：
  - 桌面系统包 `<opus/opus.h>`
  - Android vendored 源码 `<opus.h>`

### 5. 自定义传输协议与抖动缓冲

- 已实现自定义音频包头
- 已实现：
  - 包序列化/反序列化
  - UDP socket 适配层
  - 抖动缓冲
  - 乱序、重复包、过晚包统计

### 6. 服务端最小发送链路

- 已实现 `hostd`
- 已实现：
  - 正弦波测试采集源 `SineWaveCapture`
  - UDP 音频发送器 `UdpAudioSender`
  - 采集 -> Opus 编码 -> 发包 的最小链路
- Linux 已补充：
  - `PulseAudio` 采集
  - 默认输出设备 monitor source 自动发现
- Windows 已补充：
  - `WASAPI loopback` 默认渲染设备采集
  - 输入混音格式到 `48 kHz / 2 channels / float32 / 10 ms` 的归一化

### 7. 客户端核心接收链路

- 已实现 `client/core` 的最小接收处理：
  - `Receiver`
  - `PlayerPipeline`
  - `ClientSession`
  - `CallbackAudioSink`
- 已实现：
  - 收包
  - 抖动缓冲
  - Opus 解码
  - 播放 sink 回调
  - 延迟与丢包统计

### 8. Android 客户端工程

- 已建立 Android Gradle 工程
- 已生成 Gradle Wrapper
- 已接入：
  - Kotlin 薄壳
  - `SpeakerService`
  - `NativeBridge`
  - `AudioTrack` 浮点 PCM 播放
  - JNI -> `client/core` 回调式播放桥
- 已将 Opus 源码 vendored 到：
  - `third_party/opus`
- 已让 Android native 构建直接编译本地 Opus 源码

### 9. Android 构建环境

- 已安装 Android SDK/NDK 到：
  - `/home/yyx/.local/android-sdk`
- 已安装组件：
  - `platform-tools`
  - `platforms;android-35`
  - `build-tools;35.0.0`
  - `build-tools;34.0.0`
  - `cmake;3.22.1`
  - `ndk;27.1.12297006`
- 已安装 Gradle 8.9 到：
  - `/home/yyx/.local/gradle/gradle-8.9`
- 已配置：
  - `client/android-app/local.properties`
  - `client/android-app/gradlew`

### 10. Android SDK 根切换与 16KB Page Size 兼容

- 已将 Android Gradle 工程的 SDK 根切换到：
  - `/home/yyx/Android/Sdk`
- 已在全局 SDK 根下补齐当前项目所需组件：
  - `platforms/android-35`
  - `build-tools/35.0.0`
  - `cmake/3.22.1`
  - `ndk/27.1.12297006`
- 已在 Android CMake 构建中启用：
  - `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON`
- 已让 Android 64 位 native `.so` 满足 `16KB Page Size` 所需的 ELF `LOAD` 段对齐要求

### 11. Android 连接配置 UI

- 已移除 `MainActivity` 中启动后立即退出的硬编码地址逻辑
- 已提供可编辑连接配置：
  - 发送端 IPv4 过滤（可选）
  - 本地监听端口
- 已将连接配置持久化到 `SharedPreferences`
- 已在界面展示当前设备 IPv4 对应的 `hostd --host <ip> --port <port>` 提示
- 已提供显式的启动 / 停止接收按钮
- 已让客户端在 Android / 桌面共享接收链路上支持按发送端 IPv4 过滤 UDP 包

### 12. Android 启动与侦听崩溃修复

- 已修复 App 首屏启动崩溃：
  - 原因是查询 `ConnectivityManager` 时缺少 `ACCESS_NETWORK_STATE`
  - 已在 `AndroidManifest.xml` 中补齐该权限
  - 已为当前设备 IPv4 查询增加容错，避免系统服务异常导致首屏闪退
- 已修复点击“开始侦听”后进程直接退出：
  - 原因是 `NativeBridge` 为 Kotlin `object`，但 JNI 入口按静态方法方式使用时未补 `@JvmStatic`
  - 已为 `nativeStart` / `nativeStop` 补齐 `@JvmStatic`
  - 已在模拟器上验证点击“Start Receiver”后不再触发 JNI `SIGABRT`

### 13. Android 前台服务与基础单测补齐

- 已将 `SpeakerService` 改为真正的前台服务启动路径
- 已补齐 Android 前台服务所需权限：
  - `FOREGROUND_SERVICE`
  - `FOREGROUND_SERVICE_MEDIA_PLAYBACK`
- 已实现持续通知与停止动作：
  - 服务运行中展示常驻通知
  - 通知中可直接停止接收
  - 点击“Start Receiver”时改为 `startForegroundService(...)`
- 已将连接配置校验逻辑抽离为纯 Kotlin 模块：
  - 端口解析
  - IPv4 校验
  - 状态文案拼装
- 已新增 Android 本地单元测试：
  - `ConnectionConfigTest`
- 已让 `testDebugUnitTest` 不再是 `NO-SOURCE`

### 14. 服务端持续发送模型

- 已将 `hostd` 的执行模型改为默认持续发送
- 当前行为改为：
  - 未指定 `--seconds` 时持续采集、编码并发送，直到收到中断信号
  - 指定 `--seconds <n>` 时仍可做限时发送，用于测试与联调
- 已为 `hostd` 补齐信号退出处理：
  - `SIGINT`
  - `SIGTERM`
- 当前服务端已更符合“将手机作为电脑网络扬声器长期播放端”的使用方式

### 15. Git 仓库初始化与提交整理

- 已在项目根目录初始化 Git 仓库
- 已新增 `.gitignore`，排除：
  - CMake / Gradle 构建产物
  - IDE 配置目录
  - Android 本地 `local.properties`
- 已按当前 `plan.md` 与 `progress.md` 的里程碑整理提交，拆分为：
  - `docs: add research and implementation plan`
  - `feat(core): add desktop audio transport pipeline`
  - `feat(android): add android receiver app and vendored opus`
  - `docs: record current progress and repository initialization`

## 已完成的验证

### 1. 桌面端构建与测试

- 已执行：
  - `CCACHE_DISABLE=1 cmake -S . -B build-nocache`
  - `CCACHE_DISABLE=1 cmake --build build-nocache -j4`
  - `ctest --test-dir build-nocache --output-on-failure`
- 结果：
  - 构建成功
  - 测试全部通过

### 2. 自动化测试覆盖

- 已实现并通过：
  - `tests/audio_packet_test.cpp`
  - `tests/jitter_buffer_test.cpp`
  - `tests/opus_codec_test.cpp`
  - `tests/callback_audio_sink_test.cpp`
  - `tests/end_to_end_test.cpp`

### 3. Android APK 构建验证

- 已执行：
  - `cd client/android-app`
  - `export http_proxy=http://127.0.0.1:7897`
  - `export https_proxy=http://127.0.0.1:7897`
  - `export JAVA_HOME=/usr/lib/jvm/jdk-17.0.12-oracle-x64`
  - `export GRADLE_USER_HOME=/home/yyx/workspaces/network_speaker/.gradle-home`
  - `./gradlew assembleDebug`
- 结果：
  - `assembleDebug` 成功
  - 已产出 APK

### 4. 当前构建产物

- 桌面端：
  - `build-nocache/hostd`
  - `build-nocache/clientd`
  - `build-nocache/network_speaker_tests`
- Android：
  - `client/android-app/app/build/outputs/apk/debug/app-debug.apk`

### 5. Android SDK 切换与 16KB 对齐验证

- 已执行：
  - `cd client/android-app`
  - `export JAVA_HOME=/usr/lib/jvm/jdk-17.0.12-oracle-x64`
  - `export GRADLE_USER_HOME=/home/yyx/workspaces/network_speaker/.gradle-home`
  - `./gradlew assembleDebug testDebugUnitTest --stacktrace`
  - `readelf -l app/.cxx/Debug/694i6w27/arm64-v8a/libnetworkspeaker_android.so`
  - `readelf -l app/.cxx/Debug/694i6w27/x86_64/libnetworkspeaker_android.so`
  - `/home/yyx/Android/Sdk/build-tools/35.0.0/zipalign -c -P 16 -v 4 app/build/outputs/apk/debug/app-debug.apk`
  - `ctest --test-dir build-nocache --output-on-failure`
- 结果：
  - 使用 `/home/yyx/Android/Sdk` 作为 SDK 根时，`assembleDebug` 成功
  - Android `testDebugUnitTest` 任务可执行，但当前为 `NO-SOURCE`
  - 桌面端现有测试套件继续全部通过
  - `arm64-v8a` 与 `x86_64` 的 `libnetworkspeaker_android.so` 的 `LOAD` 段 `Align` 已为 `0x4000`
  - `app-debug.apk` 已通过 `zipalign -P 16` 校验

### 6. Android 连接配置 UI 与发送端过滤验证

- 已执行：
  - `export CCACHE_DISABLE=1`
  - `cmake --build build-nocache -j4`
  - `ctest --test-dir build-nocache --output-on-failure`
  - `cd client/android-app`
  - `export JAVA_HOME=/usr/lib/jvm/jdk-17.0.12-oracle-x64`
  - `export GRADLE_USER_HOME=/home/yyx/workspaces/network_speaker/.gradle-home`
  - `./gradlew assembleDebug testDebugUnitTest`
  - `readelf -l app/build/intermediates/cxx/Debug/694i6w27/obj/arm64-v8a/libnetworkspeaker_android.so`
  - `/home/yyx/Android/Sdk/build-tools/35.0.0/zipalign -c -P 16 -v 4 app/build/outputs/apk/debug/app-debug.apk`
- 结果：
  - 新增的 `ReceiverTest` 已覆盖允许 / 拒绝发送端 IPv4 的行为
  - 桌面端 `network_speaker_tests` 全部通过
  - Android `assembleDebug` 继续成功
  - Android `testDebugUnitTest` 仍为 `NO-SOURCE`
  - 最新 Android native `.so` 继续保持 `0x4000` 的 `LOAD` 段对齐
  - 最新 `app-debug.apk` 继续通过 `zipalign -P 16` 校验

### 7. Android 启动崩溃修复与模拟器端到端验证

- 已执行：
  - `adb logcat -d AndroidRuntime:E ActivityManager:I '*:S'`
  - `adb logcat -d | rg "JNI DETECTED ERROR|SIGABRT|com.example.networkspeaker"`
  - `cd client/android-app`
  - `export JAVA_HOME=/usr/lib/jvm/jdk-17.0.12-oracle-x64`
  - `export GRADLE_USER_HOME=/home/yyx/workspaces/network_speaker/.gradle-home`
  - `./gradlew installDebug`
  - `adb shell am start -n com.example.networkspeaker/.MainActivity`
  - `adb shell uiautomator dump ...`
  - `adb shell input tap ...`
  - `adb shell ss -lunp | rg 50000`
  - `./build-nocache/hostd --host 10.0.2.16 --port 50000 --source sine --seconds 2`
- 结果：
  - 已定位并修复首屏启动时缺少 `ACCESS_NETWORK_STATE` 导致的 `SecurityException`
  - 已定位并修复点击“Start Receiver”后 `NativeBridge.nativeStart` 触发的 JNI `SIGABRT`
  - 模拟器中 App 已可稳定启动，并可进入 `Listening on UDP 50000 ...` 状态
  - 模拟器中已确认 UDP `50000` 端口处于监听状态
  - 桌面端 `hostd` 已成功向模拟器 `10.0.2.16:50000` 发送 `200` 帧测试音频
  - 发包期间 Android 进程持续存活，未出现新的 `AndroidRuntime` 或 native crash
  - 模拟器联调时如需接收桌面端数据，`Sender IPv4 Filter` 需留空或填写真实发送端地址，不能使用历史残留的 `127.0.0.1`

### 8. Android 前台服务与单元测试验证

- 已执行：
  - `cd client/android-app`
  - `export JAVA_HOME=/usr/lib/jvm/jdk-17.0.12-oracle-x64`
  - `export GRADLE_USER_HOME=/home/yyx/workspaces/network_speaker/.gradle-home`
  - `./gradlew assembleDebug testDebugUnitTest`
- 结果：
  - `assembleDebug` 成功
  - `testDebugUnitTest` 成功
  - 新增的 `ConnectionConfigTest` 已覆盖端口解析、IPv4 校验与状态文案逻辑
  - Android 前台服务改动未破坏现有 native 构建链路

### 9. Linux 默认输出 monitor source 静音行为验证

- 已执行：
  - `pactl get-default-sink`
  - `pactl list short sinks`
  - `pactl list short sources`
  - 以默认输出 `alsa_output.pci-0000_00_1f.3.analog-stereo` 的 monitor source
    `alsa_output.pci-0000_00_1f.3.analog-stereo.monitor`
    进行两轮本地采样
  - 使用 `ffmpeg` 生成 2 秒 1 kHz 测试音
  - 使用 `paplay` 播放测试音
  - 使用 `parec` 从 monitor source 录制原始 PCM
  - 对比默认 sink 未静音 / 静音时录制结果的 RMS 与峰值
- 结果：
  - 未静音采样：`rms=1584.73`，`peak=2896`
  - 静音采样：`rms=1591.43`，`peak=2896`
  - 当前 Linux + PipeWire/PulseAudio 兼容层环境下，默认输出设备静音后，
    monitor source 仍能继续采集到基本等价的输出音频副本
  - 这进一步说明当前服务端 Linux 路线是“复制默认输出混音流并发送”，
    不是将本机播放改为只转发到客户端

### 10. 服务端持续发送改造验证

- 已执行：
  - `CCACHE_DISABLE=1 cmake --build build-nocache --target hostd -j4`
- 结果：
  - `hostd` 改造后重新编译成功
  - 代码路径已从“固定发送 5 秒后退出”改为“默认持续发送直到中断”
  - 当前会话中的沙箱限制本地 UDP 发包，因而未在沙箱内记录真实长时间发送日志；
    但限时代码路径和持续运行代码路径均已完成实现

### 11. Android 模拟器端到端播放验证

- 已执行：
  - 在宿主机确认默认输出设备：
    - `alsa_output.pci-0000_00_1f.3.analog-stereo`
  - 在宿主机确认默认输出保持静音：
    - `pactl get-sink-mute alsa_output.pci-0000_00_1f.3.analog-stereo`
    - 结果为 `Mute: yes`
  - 使用 `ffmpeg` 生成 20 秒测试音频 `/tmp/nspeaker-e2e.wav`
  - 在 Android 模拟器中安装并启动最新 APK
  - 在 Android 模拟器中启动接收端，确认状态为：
    - `Listening on UDP 50000 from any sender.`
  - 由于当前宿主机环境无法直接将 UDP 包投递到模拟器的 `10.0.2.16`，
    已使用 Android Emulator redirection：
    - `adb emu redir add udp:50000:50000`
  - 在宿主机执行：
    - `./build-nocache/hostd --host 127.0.0.1 --port 50000 --source pulse --seconds 20`
    - `paplay /tmp/nspeaker-e2e.wav`
  - 在 Android 模拟器中通过 `adb logcat` 观察：
    - `NetworkSpeakerNative: ClientSession started on UDP port 50000 hostFilter=<any>`
    - `NetworkSpeaker: PCM write #1 samplesPerChannel=480`
    - `...`
    - `NetworkSpeaker: PCM write #2000 samplesPerChannel=480`
- 结果：
  - 已走通“宿主机播放音频 -> 宿主机默认输出保持静音 -> `hostd` 采集默认输出 monitor source
    -> 发送到 Android 模拟器 -> Android 客户端解码并驱动 `AudioTrack` 播放”的完整流程
  - 模拟器端连续写入 `2000` 个 10ms PCM 帧，说明扬声器播放路径被持续驱动
  - 当前环境下，使用 emulator UDP redirection 可以稳定完成该端到端联调

### 12. HDMI 输出到模拟器再回放到宿主机内置声卡验证

- 已执行：
  - 在宿主机确认音频输出设备：
    - `pactl list short sinks`
    - `pactl get-default-sink`
  - 在宿主机确认 Android 模拟器在线：
    - `adb devices`
  - 在 Android 模拟器中确认客户端界面状态为：
    - `Listening on UDP 50000 from any sender.`
  - 为 Android 模拟器补齐 UDP 重定向并确认生效：
    - `adb emu redir add udp:50000:50000`
    - `adb emu redir list`
  - 使用 `ffmpeg` 生成 45 秒测试音频：
    - `/tmp/nspeaker-long.wav`
  - 在宿主机启动服务端：
    - `./build-nocache/hostd --host 127.0.0.1 --port 50000 --source pulse --seconds 45`
  - 在宿主机将原始测试音频显式播放到 HDMI 输出：
    - `paplay --device=alsa_output.pci-0000_01_00.1.hdmi-stereo /tmp/nspeaker-long.wav`
  - 在发送与回放期间检查宿主机音频流路由：
    - `pactl list sink-inputs`
  - 在 Android 模拟器中通过 `adb logcat` 观察持续播放日志
  - 在宿主机确认 HDMI 与内置声卡均未静音：
    - `pactl get-sink-mute alsa_output.pci-0000_00_1f.3.analog-stereo`
    - `pactl get-sink-mute alsa_output.pci-0000_01_00.1.hdmi-stereo`
- 结果：
  - 本轮测试时，宿主机默认输出为 HDMI：
    - `alsa_output.pci-0000_01_00.1.hdmi-stereo`
  - 原始长音频 `paplay` 流已确认路由到 HDMI sink
  - Android 模拟器对应的宿主机音频流 `qemu-system-x86_64`
    已确认路由到内置声卡：
    - `alsa_output.pci-0000_00_1f.3.analog-stereo`
  - `hostd` 已完成：
    - `Sent 4500 frames to 127.0.0.1:50000`
  - Android 客户端日志已连续写入：
    - `PCM write #1 samplesPerChannel=480`
    - `...`
    - `PCM write #4500 samplesPerChannel=480`
  - 这说明当前环境下已再次走通“宿主机 HDMI 播放源
    -> `hostd` 采集默认输出 monitor source -> Android 模拟器接收并解码
    -> 模拟器音频流经宿主机内置声卡回放”的完整链路
  - 从宿主机音频路由与 Android 播放日志看，最终回放目标已切到电脑内置音频输出，
    具备直接通过宿主机内置扬声器听到声音的条件

### 13. Windows `WASAPI loopback` 编译级验证

- 已完成：
  - `WasapiLoopbackCapture` 的真实实现
  - 默认渲染设备 `WASAPI loopback` 接入
  - 常见 `PCM / float` 混音格式到内部固定 PCM 帧格式的转换
  - 空闲或 silent packet 场景下的 10ms 帧补齐
- 已执行：
  - 使用 Windows MSVC 工具链单独编译
    `server/hostd/src/platform/windows/wasapi_loopback_capture.cpp`
  - 使用 Windows MSVC 工具链单独编译
    `server/hostd/src/capture_factory.cpp`
- 结果：
  - Windows 采集实现源码已通过编译级验证
  - `capture_factory` 的 Windows 分支未被新实现破坏
  - 当前仓库默认 `build/` 目录仍受宿主环境同时存在 `Path` / `PATH` 的
    MSBuild 环境变量冲突影响，未在该目录内完成整库构建验证

### 14. Windows 全项目构建链路修正

- 已完成：
  - 顶层 CMake 不再强依赖 `pkg-config`
  - Windows 默认改为使用仓库内 vendored `third_party/opus`
  - GoogleTest 改为可选依赖，缺失时跳过 `network_speaker_tests`
  - 修正 `UdpSocket` 的 Windows socket 句柄类型，兼容 64 位构建
  - 避免顶层 `BUILD_TESTING=ON` 时误触发第三方 Opus 测试编译
- 已执行：
  - 在 Windows MSVC 环境中配置：
    `cmake -S . -B build-windows -G "NMake Makefiles" -DBUILD_TESTING=OFF`
  - 在 Windows MSVC 环境中构建：
    `cmake --build build-windows`
  - 在 Windows MSVC 环境中配置：
    `cmake -S . -B build-windows-tests2 -G "NMake Makefiles" -DBUILD_TESTING=ON`
- 结果：
  - `build-windows` 已成功产出 `hostd.exe` 与 `clientd.exe`
  - Windows 构建不再依赖 MSYS2 的 `opus` / `gtest` 二进制兼容性
  - `BUILD_TESTING=ON` 且未安装 GoogleTest 时，配置阶段会警告并跳过项目测试目标，
    但不会阻塞主程序构建

### 15. Windows `WASAPI loopback` 本机回环验证

- 已执行：
  - 在显式注入 `vcvars64.bat` 环境后从零配置：
    `cmake -S . -B build-windows-verify -G "NMake Makefiles" -DBUILD_TESTING=ON`
  - 在同一 MSVC 环境中完整构建：
    `cmake --build build-windows-verify`
  - 直接运行：
    `build-windows-verify/hostd.exe --host 127.0.0.1 --port 50000 --source wasapi --seconds 1`
  - 启动本地接收端并再次回环发送：
    `build-windows-verify/clientd.exe --port 50001 --seconds 2`
    `build-windows-verify/hostd.exe --host 127.0.0.1 --port 50001 --source wasapi --seconds 1`
- 结果：
  - 新建的 `build-windows-verify` 目录已可从零完成配置与整库构建
  - `hostd --source wasapi` 已在当前 Windows 主机上真实启动默认渲染设备 loopback 采集，
    并成功发送 `100` 帧 UDP 音频
  - `clientd` 本地回环接收结果为：
    `Decoded frames=100 samples=48000 latency_ms=0`
  - 这说明当前 Windows 路线已经不只是“编译级通过”，而是已验证
    `WASAPI loopback -> Opus 编码 -> UDP 发送 -> UDP 接收 -> Opus 解码`
    的本机真实运行链路

### 16. Windows -> Android 模拟器 `WASAPI loopback` 端到端联调

- 已执行：
  - 在 Windows 宿主机启动 Android 模拟器，并确认：
    - `adb devices -l`
  - 为模拟器添加 UDP 端口重定向：
    - `adb emu redir add udp:50000:50000`
  - 启动 Android 客户端并通过 UI 自动点击 `START RECEIVER`
  - 在宿主机本地生成 `48 kHz / 2ch / 16-bit PCM` 测试 WAV，
    并通过 PowerShell `System.Media.SoundPlayer` 从默认输出设备播放
  - 在宿主机执行：
    - `build-windows-verify/hostd.exe --host 127.0.0.1 --port 50000 --source wasapi --seconds 10`
  - 通过 `adb logcat -d -s NetworkSpeaker NetworkSpeakerNative AndroidRuntime` 观察播放日志
- 结果：
  - Android 客户端 UI 状态已进入：
    - `Listening on UDP 50000 from any sender.`
  - Windows `hostd --source wasapi` 已在本轮验证中成功发送：
    - `Sent 1000 frames to 127.0.0.1:50000`
  - Android `logcat` 已确认：
    - `NetworkSpeakerNative: ClientSession started on UDP port 50000 hostFilter=<any>`
    - `NetworkSpeaker: AudioTrack started sampleRate=48000 channels=2`
    - `NetworkSpeaker: PCM write #1 samplesPerChannel=480`
    - `...`
    - `NetworkSpeaker: PCM write #1000 samplesPerChannel=480`
  - 这说明截至 `2026-04-11`，当前 Windows 开发环境中已经跑通：
    - 电脑上播放音频
    - Windows `WASAPI loopback` 采集默认声卡输出
    - `hostd` 将音频流复制到 Android 模拟器客户端
    - Android 客户端收到音频流并持续驱动 `AudioTrack` 扬声器播放路径
  - 已将上述流程固化为：
    - `tools/windows_android_emulator_e2e.ps1`

### 17. Windows -> Android 模拟器短时 PowerShell 联调回归

- 已执行：
  - 在 Windows 宿主机生成 2 秒测试音并从默认输出设备播放
  - 通过 `adb` 启动 Android 模拟器中的接收端，并确认状态：
    - `Listening on UDP 50000 from any sender.`
  - 在宿主机执行短时发送：
    - `build-windows-verify/hostd.exe --host 127.0.0.1 --port 50000 --source wasapi --seconds 3`
  - 通过 `adb logcat -d -s NetworkSpeaker NetworkSpeakerNative AndroidRuntime` 抓取关键日志
- 结果：
  - `hostd` 在 `2026-04-11` 的本轮短测中成功发送：
    - `Sent 300 frames to 127.0.0.1:50000`
  - Android 客户端日志已确认：
    - `NetworkSpeakerNative: ClientSession started on UDP port 50000 hostFilter=<any>`
    - `NetworkSpeaker: AudioTrack started sampleRate=48000 channels=2`
    - `NetworkSpeaker: PCM write #1 samplesPerChannel=480`
    - `...`
    - `NetworkSpeaker: PCM write #300 samplesPerChannel=480`
  - 本轮验证命令在约 8 秒内返回，说明短时内联 PowerShell 命令可以稳定完成联调而不会长时间阻塞
  - 对比之下，`tools/windows_android_emulator_e2e.ps1` 当前环境下仍存在 PowerShell 子进程收尾阻塞风险，后续联调应优先采用短时内联命令

## 当前遗留项

### 1. 平台实现

- Windows `WASAPI loopback` 已完成本机回环验证，并已补 Windows -> Android 模拟器端到端联调记录，但尚未补 Windows -> Android 真机播放联调记录
- Linux 目前只完成了 `PulseAudio` 路线，尚未扩展更多后端策略

### 2. Android 产品化

- Android 前台服务与常驻通知已补齐，但更完善的后台保活策略仍待继续完善
- 尚未做真机联调结果记录
- Android 工程尚未补 instrumentation tests，也尚未覆盖 `Service`/通知交互等组件级测试
- 连接配置仍依赖手动填写发送端 IPv4，尚未做自动发现、配对流程或更明确的过滤提示

### 3. 音频协议扩展

- 尚未实现：
  - FEC
  - 加密
  - 多声道
  - 更完整的会话握手协议

### 4. 端到端联调

- 当前环境里已经验证：
  - 桌面构建
  - 单元测试
  - 协议级集成测试
  - Windows 主机 `hostd --source wasapi` 到本机 `clientd` 的 UDP 回环链路
  - Windows 主机播放音频 -> `hostd --source wasapi` -> Android 模拟器 `AudioTrack` 播放链路
  - Android APK 构建
  - 桌面端 `hostd` 到 Android 模拟器的 UDP 发包链路
  - 宿主机静音状态下，`PulseAudio` monitor source 到 Android 模拟器播放链路
  - 宿主机 HDMI 输出 -> `hostd` -> Android 模拟器 -> 宿主机内置声卡回放链路
- 仍未完成：
  - Windows/Linux 到 Android 真机的端到端听感验证
  - 局域网延迟指标验证
  - 长时间稳定性验证
  - 宿主机环境下 `hostd` 默认无限持续发送的长时间实机运行记录

## 下一步建议

- 在真机上验证前台通知、后台驻留与熄屏播放行为
- 在真实局域网环境中联调：
  - `hostd`
  - Android APK
- 记录 `hostd --source pulse` 默认持续发送模式下的长时间运行稳定性
- 如需再次复现当前 Windows + Android 模拟器链路，优先使用：
  - 短时内联 PowerShell 命令
  - `hostd --source wasapi --seconds 3` 或 `--seconds 4`
- 如果继续使用 Android 模拟器联调，可固化 `adb emu redir add udp:50000:50000` 的测试步骤
- 在 Windows 真机上验证 `hostd --source wasapi` 到 Android 真机的局域网播放效果
- 基于真实丢包与延迟数据决定是否引入 FEC
