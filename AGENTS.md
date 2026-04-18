# AGENTS.md

This file provides guidance to Qoder (qoder.com) when working with code in this repository.

## Project Overview

`network_speaker` streams Windows desktop audio over LAN UDP to Android devices acting as network speakers. Primary data path: Windows WASAPI loopback -> Opus encode -> UDP -> Android AudioTrack.

## Build Commands

All C++ builds require a Visual Studio Developer PowerShell (or activated `vcvars64.bat`) and `VCPKG_ROOT` set to a vcpkg installation.

### C++ (hostd, clientd, tests)

```powershell
cmake --preset windows-ninja-vcpkg
cmake --build --preset windows-ninja-vcpkg
ctest --preset windows-ninja-vcpkg
```

Output: `out/build/windows-ninja-vcpkg/`

On Linux:

```bash
cmake --preset linux-ninja-system
cmake --build --preset linux-ninja-system
ctest --preset linux-ninja-system
```

### Run a single C++ test

The C++ tests are compiled into a single GoogleTest binary. Filter with `--gtest_filter`:

```powershell
.\out\build\windows-ninja-vcpkg\network_speaker_tests.exe --gtest_filter="JitterBufferTest.*"
```

### Windows GUI (.NET 10 WPF)

```powershell
# Build
dotnet build .\apps\windows-launcher\NetworkSpeaker.Launcher\NetworkSpeaker.Launcher.csproj

# Run GUI core tests
dotnet test .\apps\windows-launcher\NetworkSpeaker.Launcher.Core.Tests\NetworkSpeaker.Launcher.Core.Tests.csproj
```

### MSI Packaging

```powershell
.\tools\package-windows-installer.ps1                    # auto-version
.\tools\package-windows-installer.ps1 -Version 0.1.0     # explicit version
```

### Local Loopback Verification

```powershell
# Terminal 1: receiver
.\out\build\windows-ninja-vcpkg\clientd.exe --port 50000 --seconds 5

# Terminal 2: sender (sine test tone)
.\out\build\windows-ninja-vcpkg\hostd.exe --host 127.0.0.1 --port 50000 --source sine --seconds 2
```

## Architecture

Four-layer design:

### Layer 1 -- Audio Capture (platform-specific)

`server/hostd/` contains platform audio capture implementations behind a common interface:

- `wasapi` -- Windows WASAPI loopback (`platform/windows/wasapi_loopback_capture.cpp`)
- `pulse` -- Linux PulseAudio monitor (`platform/linux/pulse_monitor_capture.cpp`)
- `sine` -- synthetic test tone (`sine_wave_capture.cpp`)

`capture_factory.cpp` selects the source by name at runtime.

### Layer 2 -- Codec & Transport (`libs/`)

Three static libraries shared by sender and receiver:

- `libs/audio_base/` -- header-only: `PcmFrame`, clock, statistics types
- `libs/codec_opus/` -- Opus encode/decode wrappers
- `libs/transport/` -- `AudioPacket` (28-byte header), cross-platform UDP socket, `JitterBuffer`

### Layer 3 -- Applications

- `server/hostd` (`hostd.exe`) -- CLI sender: capture -> encode -> packetize -> UDP send
- `client/core` (`clientd.exe`) -- receiver pipeline: UDP recv -> `JitterBuffer` -> decode -> sink callback
- `client/android-app` -- Kotlin foreground service + JNI bridge to `client/core`; PCM written to `AudioTrack`
- `apps/windows-launcher` -- .NET 10 WPF GUI that spawns a hidden `hostd.exe` subprocess, reads its stdout/stderr, exposes Start/Stop UI. Split into three projects:
  - `NetworkSpeaker.Launcher` -- WPF UI
  - `NetworkSpeaker.Launcher.Core` -- parameter model, command-line construction, settings persistence, subprocess hosting
  - `NetworkSpeaker.Launcher.Core.Tests` -- unit tests for core logic

### Layer 4 -- Distribution

- `installer/windows/` -- WiX v4 MSI (bundles `NetworkSpeaker.exe` + `hostd.exe`)
- `.github/workflows/release.yml` -- triggered by `v*` tag push; builds, tests, packages MSI, uploads to GitHub Release. `workflow_dispatch` for dry-run.

## Key Data Flow

**Sender:** Platform capture -> `PcmFrame` -> `OpusEncoder` -> `UdpAudioSender` (assembles `AudioPacket`) -> UDP socket

**Receiver:** UDP socket -> `Receiver` -> `TryParsePacket()` -> `PlayerPipeline` -> `JitterBuffer` -> `OpusDecoder` -> PCM sink callback

## Protocol Design

`AudioPacketHeader` fields: `stream_id`, `sequence`, `capture_ts_us`, `frame_samples`.

- `stream_id` regenerated each `hostd` run -- receiver uses it to distinguish sender restart from packet loss.
- `sequence` monotonically increases within a stream.

## Packet Loss Recovery

Two-phase recovery in `player_pipeline.cpp` and `jitter_buffer.cpp`:

1. New `stream_id` -> reset pipeline immediately.
2. Same stream gap: if buffer accumulated higher-sequence packets and reached target depth -> skip missing packet, resume playback.

## Project Conventions

- **Streaming strategy:** low-latency first -- drop packets during initial connection to lock onto newest audio, then stable in-order playback.
- **WiX ICE61 warning:** do not suppress -- intentionally deferred until formal release phase.
- **GoogleTest on Windows:** obtain via vcpkg; do not use FetchContent/downloaded gtest fallback.
- **MSVC compile flags:** `/utf-8` is enabled project-wide for correct CJK string handling.
- **GUI does not embed hostd logic** -- it always spawns `hostd.exe` as a subprocess and communicates via stdout/stderr.
- **Settings persistence:** `%AppData%\NetworkSpeaker\settings.json`. `hostd.exe` path is resolved at runtime (same directory first, then known build directories).

## Test Coverage Priorities

- **C++:** `tests/audio_packet_test.cpp`, `tests/jitter_buffer_test.cpp`, `tests/end_to_end_test.cpp`
- **GUI:** parameter-to-command-line mapping, settings persistence, state machine transitions, subprocess abnormal exit

## Release

Push a `v*` tag to trigger the full GitHub Actions release pipeline. Use `workflow_dispatch` for a dry-run build without creating a GitHub Release.
