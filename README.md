# network_speaker

跨平台网络扬声器工程骨架。当前仓库已经落下下面这些可编译模块：

- `libs/audio_base`
  - 统一 `48 kHz / 2ch / float32 PCM` 帧格式
  - 单调时钟和流统计结构
- `libs/transport`
  - 自定义 UDP 音频包头
  - 包序列化/反序列化
  - 抖动缓冲
  - UDP socket 适配层
- `libs/codec_opus`
  - Opus 低延迟立体声编解码
- `server/hostd`
  - 发送端 CLI
  - 正弦波采集源
  - Linux `PulseAudio` 采集和默认 monitor source 自动发现
  - Windows `WASAPI` 接口占位
- `client/core`
  - 接收端核心流水线
  - 解码、抖动缓冲、统计
  - `ClientSession` 持续接收线程
  - 控制台客户端 `clientd`
- `client/android-app`
  - Android APK 薄 Kotlin 壳
  - `AudioTrack` 浮点 PCM 播放
  - JNI 到 `client/core` 的桥接
- `tests`
  - 包编解码、抖动缓冲、Opus、端到端协议级集成测试

## 构建

```bash
CCACHE_DISABLE=1 cmake -S . -B build
CCACHE_DISABLE=1 cmake --build build -j4
ctest --test-dir build --output-on-failure
```

`CCACHE_DISABLE=1` 是为了绕开当前环境里的只读 `ccache` 包装。

当前开发机上的 Android SDK 根目录为：

```bash
/home/yyx/Android/Sdk
```

当前项目依赖的组件包括：

```text
build-tools;35.0.0
cmake;3.22.1
ndk;27.1.12297006
platform-tools
platforms;android-35
```

`client/android-app/local.properties` 用于本地 SDK 路径配置，当前已加入 `.gitignore`，
需要在各自环境中自行写入 `sdk.dir=...`。

Android Gradle Wrapper 已生成在 `client/android-app/`，首次成功构建命令如下：

```bash
cd client/android-app
export http_proxy=http://127.0.0.1:7897
export https_proxy=http://127.0.0.1:7897
export JAVA_HOME=/usr/lib/jvm/jdk-17.0.12-oracle-x64
export GRADLE_USER_HOME=/home/yyx/workspaces/network_speaker/.gradle-home
./gradlew assembleDebug
```

构建产物默认在：

```text
client/android-app/app/build/outputs/apk/debug/app-debug.apk
```

## 运行

发送端：

```bash
./build/hostd --host 127.0.0.1 --port 50000 --source sine --seconds 5
```

接收端：

```bash
./build/clientd --port 50000 --seconds 5
```

当前沙箱限制本地 UDP 绑定，因此仓库内的自动化端到端测试使用“协议级内存回环”，而不直接依赖真实 socket。实际机器上运行 `hostd`/`clientd` 时可以验证真实网络链路。

## 当前实现边界

- 已完成：
  - 核心模块拆分
  - 可编译的 C++20 主工程
  - Opus 低延迟编码链路
  - 自定义包头与抖动缓冲
  - Linux 默认 monitor source 自动发现
  - Android 壳工程和 `AudioTrack` 播放桥
- 仍待继续：
  - Windows `WASAPI loopback` 真实实现
  - Android 真机联调和前台通知完善
  - FEC、加密、多声道扩展
