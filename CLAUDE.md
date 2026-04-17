# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`network_speaker` streams desktop audio over LAN UDP to Android devices (phones, tablets, emulators) acting as network speakers. Supports Windows (WASAPI) and Linux (PulseAudio). The primary path is: WASAPI/PulseAudio capture → Opus encode → UDP → Android AudioTrack.

## Build Commands

All C++ builds require a Visual Studio Developer PowerShell (or `ilammy/msvc-dev-cmd` in CI) and `VCPKG_ROOT` set to a vcpkg installation.

### C++ (hostd, clientd, tests) — Windows

```powershell
cmake --preset windows-ninja-vcpkg
cmake --build --preset windows-ninja-vcpkg
ctest --preset windows-ninja-vcpkg
```

Output goes to `out/build/windows-ninja-vcpkg/`.

### C++ (hostd, tests) — Linux

```bash
# Requires: build-essential ninja-build pkg-config libopus-dev libpulse-dev libgtest-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Cross-platform Desktop GUI (Tauri + Vue)

```bash
cd apps/desktop
npm install
npx tauri dev        # development with hot-reload
npx tauri build      # production build
```

Requires: Node.js 20+, Rust toolchain, and platform-specific Tauri dependencies.
On Linux: `libwebkit2gtk-4.1-dev libgtk-3-dev libayatana-appindicator3-dev librsvg2-dev`
Rust check only: `cd apps/desktop/src-tauri && cargo check`

### Windows GUI (.NET 10 WPF) — Legacy

```powershell
# Build
dotnet build .\apps\windows-launcher\NetworkSpeaker.Launcher\NetworkSpeaker.Launcher.csproj

# Run GUI core tests
dotnet test .\apps\windows-launcher\NetworkSpeaker.Launcher.Core.Tests\NetworkSpeaker.Launcher.Core.Tests.csproj
```

### Android

Open `client/android-app/` in Android Studio. Requires NDK `27.1.12297006`, CMake `3.22.1`, JDK 17. Create `client/android-app/local.properties` with `sdk.dir=...` (not committed).

### MSI Packaging

```powershell
.\tools\package-windows-installer.ps1                    # auto-version
.\tools\package-windows-installer.ps1 -Version 0.1.0     # explicit version
```

Output goes to `artifacts/windows-launcher/publish` and `artifacts/release`.

## Local Verification (Loopback)

```powershell
# Terminal 1: start receiver
.\out\build\windows-ninja-vcpkg\clientd.exe --port 50000 --seconds 5

# Terminal 2: send sine test tone
.\out\build\windows-ninja-vcpkg\hostd.exe --host 127.0.0.1 --port 50000 --source sine --seconds 2
```

For Android emulator: run `adb emu redir add udp:50000:50000` first, then target `127.0.0.1`.

## Architecture

The system has four layers:

**1. Audio Capture (platform-specific, in `server/hostd`)**
- `wasapi` — Windows WASAPI loopback capture (`platform/windows/wasapi_loopback_capture.cpp`)
- `pulse` — Linux PulseAudio monitor (`platform/linux/pulse_monitor_capture.cpp`)
- `sine` — synthetic test tone (`sine_wave_capture.cpp`)

**2. Codec & Transport (`libs/`)**
- `libs/codec_opus/` — Opus encode/decode wrappers
- `libs/transport/` — `AudioPacket` 28-byte header format, cross-platform UDP socket, `JitterBuffer` with reorder/loss recovery
- `libs/audio_base/` — shared `PcmFrame`, clock, statistics types

**3. Application layer**
- `server/hostd` — CLI sender: capture → encode → packetize → UDP send
- `client/core` — receiver pipeline: UDP recv → `JitterBuffer` → decode → sink callback
- `client/android-app` — Kotlin foreground service + JNI bridge to `client/core`; PCM delivered to `AudioTrack`
- `apps/desktop` — Cross-platform Tauri v2 + Vue 3 desktop GUI. Rust backend manages hostd subprocess lifecycle (state machine: Stopped → Starting → Running → Stopping → Stopped/Faulted), device enumeration, and settings persistence. Vue frontend with adaptive light/dark theme, smooth CSS transitions. Replaces the legacy Windows-only WPF launcher.
- `apps/windows-launcher` — (Legacy) WPF GUI, Windows-only. Being replaced by `apps/desktop`.

**4. Distribution**
- `installer/windows/` — WiX v4 MSI (bundles `NetworkSpeaker.exe` + `hostd.exe` + Start Menu shortcut)
- `apps/desktop` — Tauri produces platform-native bundles (`.deb`, `.AppImage` on Linux; `.msi`, `.exe` on Windows)
- `.github/workflows/release.yml` — triggered by `v*` tag push; builds C++, desktop app, Android APK, packages, uploads to GitHub Release

## Protocol Design

`AudioPacketHeader` key fields: `stream_id`, `sequence`, `capture_ts_us`, `frame_samples`.

- `stream_id` is regenerated each `hostd` run — lets the receiver distinguish a sender restart from packet loss.
- `sequence` is monotonically increasing within a stream.

## Packet Loss Recovery

Two-phase recovery in the receiver (`client/core/src/player_pipeline.cpp`, `libs/transport/src/jitter_buffer.cpp`):
1. New `stream_id` → reset the pipeline immediately (handles sender restart).
2. Within the same stream, if a gap exists but the buffer has accumulated higher-sequence packets and reached target depth → skip the missing packet and resume (avoids permanent silence after a single drop).

## Streaming Strategy

Prefer a low-latency strategy: drop packets during initial connection lock-on to the newest audio quickly, then switch to stable in-order playback.

## WiX ICE61 Warning

Do not suppress the WiX ICE61 warning — it is intentionally deferred until the formal release phase.

## Test Coverage Priorities

- C++: `tests/audio_packet_test.cpp`, `tests/jitter_buffer_test.cpp`, `tests/end_to_end_test.cpp`
- GUI: parameter-to-command-line mapping, settings persistence, state machine transitions, subprocess abnormal exit

## Release

Push a `v*` tag to trigger the full GitHub Actions release pipeline. Use `workflow_dispatch` for a dry-run build without creating a GitHub Release.
