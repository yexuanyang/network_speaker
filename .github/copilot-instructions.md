# Copilot Instructions

## 项目指南
- User does not want to modify the WiX ICE61 warning yet because they are not in the formal release phase.

## Streaming Strategy
- User prefers a low-latency streaming strategy that may drop packets during initial connection to lock onto the newest audio quickly, then switch to stable in-order playback.

## Testing Framework
- On Windows, prefer obtaining GoogleTest via vcpkg and do not use FetchContent/downloaded gtest fallback in CMake.