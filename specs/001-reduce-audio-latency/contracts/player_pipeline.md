# Interface Contract: PlayerPipeline

**File**: `client/core/include/nspeaker/client/player_pipeline.h`
**Feature**: 001-reduce-audio-latency

## New Types

### `PipelineConfig` struct

```cpp
struct PipelineConfig {
    bool     startup_fast_lock_enabled      = true;
    size_t   startup_buffer_packets         = 4;
    size_t   startup_lead_packets           = 2;
    size_t   steady_target_packets          = 3;
    size_t   steady_consecutive_threshold   = 8;
    size_t   max_catchup_per_drain          = 2;
    uint32_t late_frame_drop_threshold_ms   = 200;
    uint32_t stale_packet_threshold_ms      = 500;
};
```

All fields have defaults; callers may override selectively via designated initializers.

### `PipelineState` enum

```cpp
enum class PipelineState { FastLock, Steady };
```

Not exposed in the public API — internal implementation detail. Listed here for design clarity.

## New Constructor Overload

```cpp
PlayerPipeline(std::unique_ptr<codec::IAudioDecoder> decoder,
               std::shared_ptr<audio::IAudioSink>    sink,
               std::shared_ptr<audio::Clock>         clock,
               PipelineConfig                        config);
```

**Contract**:
- `config.steady_target_packets` replaces the `target_packets` argument of the existing constructor
- `config.startup_fast_lock_enabled = false` + `config.late_frame_drop_threshold_ms = UINT32_MAX`
  reproduces identical behavior to the original constructor (regression-safe fallback)

## Existing Constructor (unchanged, kept for compatibility)

```cpp
PlayerPipeline(std::unique_ptr<codec::IAudioDecoder> decoder,
               std::shared_ptr<audio::IAudioSink>    sink,
               std::shared_ptr<audio::Clock>         clock = std::make_shared<audio::SteadyClock>(),
               std::size_t                           target_packets = 6);
```

This constructor now forwards to the new one with a default `PipelineConfig` where
`steady_target_packets = target_packets` and all other fields use struct defaults.

## Unchanged Public API

```cpp
bool        PushPacket(transport::AudioPacket packet);
size_t      DrainReady();
uint32_t    expected_sequence() const noexcept;
const audio::StreamStats& stats() const noexcept;
```

`stats()` now returns a `StreamStats` with two additional fields populated:
- `stats.startup_skipped_packets` — incremented during FastLock jumps
- `stats.peak_jitter_ms` — high-water mark of `current_jitter_ms`

These additions are additive and do not break existing consumers that only read other fields.
