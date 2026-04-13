# Tasks: Reduce Audio Streaming Latency

**Input**: Design documents from `specs/001-reduce-audio-latency/`
**Prerequisites**: plan.md 鉁? spec.md 鉁? research.md 鉁? data-model.md 鉁? contracts/ 鉁?
## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)

---

## Phase 1: Setup

**Purpose**: Establish baseline measurement before any code changes

- [X] T001 Run local loopback test per `specs/001-reduce-audio-latency/quickstart.md` Step 0 and record the baseline `e2e_latency_ms` value as a comment in `specs/001-reduce-audio-latency/research.md`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared data structures and APIs that all user stories depend on

**鈿狅笍 CRITICAL**: No user story work can begin until this phase is complete

- [X] T002 [P] Add `startup_skipped_packets` (uint32_t) and `peak_jitter_ms` (uint32_t) fields to `StreamStats` in `libs/audio_base/include/nspeaker/audio/stream_stats.h`
- [X] T003 [P] Declare `[[nodiscard]] std::optional<std::uint32_t> NewestSequence() const;` in `JitterBuffer` in `libs/transport/include/nspeaker/transport/jitter_buffer.h`
- [X] T004 Implement `JitterBuffer::NewestSequence()` in `libs/transport/src/jitter_buffer.cpp` 鈥?return `packets_.empty() ? std::nullopt : std::optional{packets_.rbegin()->first}` (depends on T003)
- [X] T005 Add `PipelineConfig` struct (all fields with defaults per `specs/001-reduce-audio-latency/contracts/player_pipeline.md`) and `PipelineState` enum (`FastLock`, `Steady`) to `client/core/include/nspeaker/client/player_pipeline.h` (depends on T002)
- [X] T006 Add `PipelineConfig`-accepting constructor overload and new private members (`config_`, `state_`, `steady_consecutive_`) to `PlayerPipeline` in `client/core/include/nspeaker/client/player_pipeline.h`; update original `target_packets` constructor to forward to the new one with default `PipelineConfig` (depends on T005)

**Checkpoint**: Foundation ready 鈥?all user story implementation can now begin

---

## Phase 3: User Story 1 - Low-Latency Playback (Priority: P1) 馃幆 MVP

**Goal**: Reduce steady-state `e2e_latency_ms` to < 150 ms via FastLock startup, soft catch-up, and stale packet drop

**Independent Test**: Run `quickstart.md` Steps 1鈥?; observe `e2e_latency_ms` < 150 ms and `startup_skip` count > 0 in the 5-second log output

### Implementation for User Story 1

- [X] T007 [US1] Implement `PipelineConfig`-based constructor body in `client/core/src/player_pipeline.cpp`; update `ResetForStream()` to set `state_ = PipelineState::FastLock` and reset `steady_consecutive_ = 0` on each new stream (depends on T006)
- [X] T008 [US1] Implement FastLock jump logic in `DrainReady()` in `client/core/src/player_pipeline.cpp`: when `state_ == FastLock` and `jitter_.Size() >= config_.startup_buffer_packets`, call `jitter_.NewestSequence()`; if it exceeds `expected_sequence_ + config_.startup_lead_packets`, set `expected_sequence_ = *newest - config_.startup_lead_packets` and accumulate `stats_.startup_skipped_packets`; count consecutive in-order pops and transition to `Steady` when `steady_consecutive_ >= config_.steady_consecutive_threshold` (depends on T007)
- [X] T009 [P] [US1] Implement stale packet drop in `PushPacket()` in `client/core/src/player_pipeline.cpp`: before calling `jitter_.Push()`, if `config_.stale_packet_threshold_ms > 0` and `packet.header.capture_ts_us > 0`, compute `age_ms = (clock_->NowMicros() - packet.header.capture_ts_us) / 1000`; if `age_ms > stale_packet_threshold_ms`, increment `stats_.late_dropped` and return `false` (depends on T007)
- [X] T010 [US1] Implement soft catch-up in `DrainReady()` in `client/core/src/player_pipeline.cpp`: in the `Steady` branch, at the top of each drain loop iteration, if `stats_.e2e_latency_ms > config_.late_frame_drop_threshold_ms` and catchup count for this call is below `config_.max_catchup_per_drain`, skip `expected_sequence_++` without decoding and count toward `stats_.packets_lost` (depends on T008)
- [X] T011 [US1] Track `peak_jitter_ms` in `DrainReady()` in `client/core/src/player_pipeline.cpp`: after computing `stats_.current_jitter_ms`, update `stats_.peak_jitter_ms = std::max(stats_.peak_jitter_ms, stats_.current_jitter_ms)` (depends on T008)
- [X] T012 [P] [US1] Add `NewestSequence()` unit tests to `tests/jitter_buffer_test.cpp`: test empty buffer returns `nullopt`; test that after pushing sequence 10, 12, 15, `NewestSequence() == 15`
- [X] T013 [P] [US1] Add FastLock behavior unit tests to `tests/end_to_end_test.cpp`: simulate 30-packet backlog, call `DrainReady()`, assert `expected_sequence` jumped to `newest - startup_lead_packets`, assert `stats.startup_skipped_packets == 28`; assert transition to Steady after 8 consecutive in-order packets

**Checkpoint**: User Story 1 independently testable 鈥?`e2e_latency_ms` should drop significantly vs. baseline

---

## Phase 4: User Story 2 - Latency Visibility & Manual Tuning (Priority: P2)

**Goal**: Users can observe real-time latency metrics and adjust buffer parameters via CLI flags

**Independent Test**: Run `clientd --port 50000 --seconds 10 --steady-target-packets 2`; observe per-5s stats lines on stdout; verify changing the flag visibly alters reported `jitter_buf` depth

### Implementation for User Story 2

- [X] T014 [US2] Add periodic stats logging to `client/core/src/main.cpp`: launch a background thread that every 5 seconds prints `[stats] e2e=Xms jitter_buf=N lost=N startup_skip=N late_drop=N underrun=N` using `session.stats()`
- [X] T015 [P] [US2] Add `--steady-target-packets <n>`, `--disable-fast-lock`, and `--late-drop-threshold-ms <n>` CLI argument parsing in `client/core/src/main.cpp`
- [X] T016 [US2] Add `PipelineConfig pipeline_config` field to `ClientSession::Config` in `client/core/include/nspeaker/client/client_session.h`; remove the now-redundant `jitter_target_packets` field and update all its usages inside `client/core/src/client_session.cpp` to use `config_.pipeline_config.steady_target_packets` (depends on T006)
- [X] T017 [US2] Wire parsed CLI args into `ClientSession::Config.pipeline_config` in `client/core/src/main.cpp` and pass the config to `ClientSession` constructor; update `PrintUsage()` to document the new flags (depends on T015, T016)

**Checkpoint**: User Story 2 independently testable 鈥?stats visible on stdout, `--disable-fast-lock` reverts latency behavior

---

## Phase 5: User Story 3 - Adaptive Latency Management (Priority: P3)

**Goal**: System automatically adjusts jitter buffer depth based on observed network conditions without user intervention

**Independent Test**: Simulate network jitter by adding artificial delay; observe `jitter_buf` depth in stats lines increase automatically; remove jitter source, observe depth decrease back toward minimum after ~10 seconds

### Implementation for User Story 3

- [X] T018 [US3] Add `jitter_variance_us` (uint32_t, rolling 20-packet window variance of inter-packet arrival intervals) to `StreamStats` in `libs/audio_base/include/nspeaker/audio/stream_stats.h`; update `DrainReady()` in `client/core/src/player_pipeline.cpp` to compute this using arrival timestamps from `clock_->NowMicros()`
- [X] T019 [US3] Add `min_steady_packets` (default 2) and `max_steady_packets` (default 8) fields to `PipelineConfig` in `client/core/include/nspeaker/client/player_pipeline.h`; implement adaptive target adjustment in `DrainReady()` in `client/core/src/player_pipeline.cpp`: if `jitter_variance_us > high_threshold`, increase effective target toward `max_steady_packets`; if `jitter_variance_us < low_threshold` for 10 consecutive seconds, decrease effective target toward `min_steady_packets` (depends on T018)

**Checkpoint**: All user stories independently functional

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Regression verification and end-to-end acceptance

- [X] T020 [P] Run `.\build-windows-verify\network_speaker_tests` with `--disable-fast-lock` flag equivalent (set `startup_fast_lock_enabled = false` in test fixture); confirm all pre-existing tests pass (0 failures)
- [X] T021 Run `specs/001-reduce-audio-latency/quickstart.md` Steps 1鈥? in full; confirm `e2e_latency_ms` < 150 ms in loopback and record final value in `specs/001-reduce-audio-latency/research.md` alongside the baseline from T001

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies 鈥?start immediately
- **Foundational (Phase 2)**: No code dependencies, but logical 鈥?do after baseline measurement
- **User Stories (Phase 3鈥?)**: All depend on Foundational (Phase 2) completion
  - US1 (P1) can start immediately after Phase 2
  - US2 (P2) can start after Phase 2 (independently of US1)
  - US3 (P3) can start after Phase 2 (independently of US1/US2)
- **Polish (Phase 6)**: Depends on desired user stories being complete (minimum: US1 for MVP)

### User Story Dependencies

- **US1 (P1)**: Depends only on Phase 2 鈥?no dependency on US2 or US3
- **US2 (P2)**: Depends only on Phase 2 (specifically T006 for `PipelineConfig`) 鈥?no dependency on US1
- **US3 (P3)**: Depends only on Phase 2 鈥?no dependency on US1 or US2

### Within User Story 1

- T007 鈫?T008 鈫?T010, T011 (sequential in `player_pipeline.cpp`)
- T009 can run in parallel with T008 (different function: `PushPacket` vs `DrainReady`)
- T012, T013 can run in parallel after T004, T006 respectively

---

## Parallel Opportunities

```
# Phase 2 鈥?all can start in parallel:
T002  Add StreamStats new fields
T003  Declare JitterBuffer::NewestSequence()

# After T003:
T004  Implement NewestSequence()

# After T002:
T005  Add PipelineConfig + PipelineState
T006  Add constructor overload and members

# Phase 3 鈥?after Phase 2:
T009  Stale packet drop (PushPacket) 鈥?parallel with T008
T012  NewestSequence unit tests 鈥?parallel with T007
T013  FastLock unit tests 鈥?parallel with T007

# Phase 4 鈥?after T006:
T015  CLI arg parsing 鈥?parallel with T016
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Baseline measurement
2. Complete Phase 2: Foundational (T002鈥揟006)
3. Complete Phase 3: US1 (T007鈥揟013) 鈥?**this alone delivers <150ms latency**
4. **STOP and VALIDATE**: Run quickstart Step 2, observe FastLock, measure latency
5. Ship US1 if latency target met

### Incremental Delivery

1. Setup + Foundational 鈫?shared types ready
2. US1 鈫?low-latency playback working 鈫?MVP
3. US2 鈫?add visibility and CLI tuning on top of US1
4. US3 鈫?add adaptive management on top of US1+US2

### Parallel Developer Strategy

With two developers after Phase 2 completes:
- **Developer A**: US1 (T007鈥揟013) 鈥?the latency engine
- **Developer B**: US2 (T014鈥揟017) 鈥?CLI and observability layer

---

## Notes

- [P] tasks touch different files or independent functions 鈥?safe to parallelize
- `client/core/src/main.cpp` is the `clientd` entry point (not a separate directory)
- `ClientSession::Config.jitter_target_packets` is superseded by `pipeline_config.steady_target_packets` in T016 鈥?remove the old field entirely (not backward-compat shim needed, single binary)
- After T016, the Android JNI path (`client/android-app`) continues to use `ClientSession` 鈥?verify it compiles after `jitter_target_packets` removal
- Avoid verifying tasks as complete until the build succeeds: `cmake --build build-windows-verify`
