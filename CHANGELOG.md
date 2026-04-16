# Changelog

## v0.3.0

### Bug Fixes

- **Linux**: 修复 PulseAudio 采集延迟过高的问题，将同步轮询 API 切换为异步流 API，显著降低端到端延迟
- **Android**: 修复 CI 构建的 release APK 无法安装（提示"没有 package info"）的问题，原因是 release 构建缺少签名配置，现暂时使用 debug 签名使 APK 可正常安装
- **Android**: 清理 AndroidManifest.xml 中多余的 `package` 属性（AGP 8+ 使用 `build.gradle.kts` 中的 `namespace`）

### Notes

- Android APK 当前使用 debug 签名，后续正式发布需切换为专用 release keystore
