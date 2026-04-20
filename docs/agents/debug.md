# 调试记录：真机播放一段时间后无声

## 背景

- 现象：
  - Android 模拟器链路已经跑通，声音表现相对稳定
  - Android 真机通过局域网接收时，开始一段时间可以正常播放
  - 播放一会后会突然无声
  - 此时客户端界面通常仍保持在 `Listening on ...`
  - 重新在客户端断开并重连后，声音会恢复，但过一段时间仍可能再次无声

## 本轮排查结论

- 当前最可能的主因已经定位到接收侧丢包恢复逻辑
- 发送/接收主链路差异如下：
  - 模拟器链路主要走宿主机本地 + `adb emu redir`，UDP 丢包概率很低
  - 真机链路走真实局域网/Wi-Fi，偶发乱序或单包丢失是正常情况
- 现有接收逻辑在 `client/core/src/player_pipeline.cpp` 中存在一个关键问题：
  - 一旦 `expected_sequence` 对应的包没有到达，`DrainReady()` 只会记一次 `playback_underruns`
  - 但不会推进 `expected_sequence`
  - 后续即使更高序列号的数据包已经到达并缓存在 `JitterBuffer` 中，播放器也会一直卡在缺失的那个序列号上
  - 这会造成“第一次丢包后永久静音，直到重连才恢复”
- 这正好解释了两种场景的差异：
  - 模拟器：几乎不丢包，所以不容易触发该问题
  - 真机：局域网下偶发丢包后，播放流水线永久卡死，因此表现为放一会就没声音

## 相关代码位置

- 接收侧卡死点：
  - `client/core/src/player_pipeline.cpp`
- 抖动缓冲仅缓存乱序包，但此前没有“跳过缺失包”的恢复策略：
  - `libs/transport/src/jitter_buffer.cpp`
  - `libs/transport/include/nspeaker/transport/jitter_buffer.h`

## 已完成修复

- 在 `JitterBuffer` 中新增：
  - `OldestSequence()`
- 在 `PlayerPipeline::DrainReady()` 中增加丢包恢复逻辑：
  - 当当前期望包缺失
  - 且缓冲中已经存在更高序列号的数据
  - 且缓冲深度已经重新达到 `target_packets`
  - 则把中间缺失的序列记为 `packets_lost`
  - 直接将 `expected_sequence` 前移到当前缓冲区最老的可用包
  - 从而恢复连续播放，而不是永久卡死
- 已补充回归测试思路到：
  - `tests/end_to_end_test.cpp`
  - 新覆盖场景包括：
    - 首包序列不是 `0`
    - 发送端重启后的新 `stream_id`
    - 单个中间包丢失后播放器继续恢复播放

## 当前验证进度

- 已成功重新构建接收侧相关 Windows 目标：
  - `build-windows-verify/clientd.exe`
- 已完成本机基础回环 sanity check：
  - 接收端：
    - `build-windows-verify/clientd.exe --port 50002 --seconds 5`
  - 发送端：
    - `build-windows/hostd.exe --host 127.0.0.1 --port 50002 --source sine --seconds 2`
  - 结果：
    - `Sent 200 frames to 127.0.0.1:50002`
    - `Decoded frames=200 samples=96000 latency_ms=16`
  - 说明本轮修改未破坏桌面侧基础发送/接收通路
- 当前环境下完整重链 `build-windows-verify/hostd.exe` 时遇到二进制占用：
  - 正在运行的 `hostd.exe` 占用了输出文件，导致链接失败
- 当前 Windows `build/` 目录中的测试目标尚未直接跑通：
  - 该目录重新生成时缺少可用的 C 编译器配置，未在本轮完成自动化测试执行

## 待你侧继续验证

- 使用本轮代码重新在 Android Studio 中构建并安装新版 Android client
- 在真机上复测以下场景：
  - 手机客户端监听 `50000`
  - 电脑浏览器持续播放视频
  - 多次重复：
    - 启动 `hostd --source wasapi --wasapi-role multimedia`
    - 停止 `hostd`
  - 重点观察：
    - 是否仍会在播放一段时间后突然永久无声
    - 如果无声，界面是否仍保持 `Listening on ...`
    - 重新连接前后是否能稳定恢复

## 若修复后仍有问题，下一轮优先排查项

- Android 真机在熄屏/后台/省电策略下的网络与音频线程保活
- Wi-Fi 网络切换、路由器省电、短时漫游导致的 UDP 抖动放大
- Android 侧 `AudioTrack` 是否存在未显式暴露到 UI 的写入异常
