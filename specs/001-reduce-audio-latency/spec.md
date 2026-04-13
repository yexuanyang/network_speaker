# Feature Specification: Reduce Audio Streaming Latency

**Feature Branch**: `001-reduce-audio-latency`
**Created**: 2026/04/12
**Status**: Draft
**Input**: User description: "我想优化这个网络扬声器的音频流传输延迟，现在的延迟比较高。"

## User Scenarios & Testing

<!--
  IMPORTANT: User stories should be PRIORITIZED as user journeys ordered by importance.
  Each user story/journey must be INDEPENDENTLY TESTABLE - meaning if you implement just ONE of them,
  you should still have a viable MVP (Minimum Viable Product) that delivers value.
-->

### User Story 1 - Low-Latency Playback Mode (Priority: P1)

As a user who wants to hear desktop audio on their Android device with minimal delay (for presentations, video conferences, or audio monitoring), I want the system to stream audio with the lowest possible latency so that the audio on my phone closely follows what is playing on the desktop.

**Why this priority**: This is the core request. Reducing the baseline latency is the primary value of the entire feature — without it, all other stories are moot.

**Independent Test**: Can be fully tested by playing audio on the desktop and measuring the perceived time until it is heard on the Android device. Delivers immediate, tangible value: a noticeably more responsive audio experience.

**Acceptance Scenarios**:

1. **Given** audio is playing on the desktop and the stream is active, **When** the user listens on the Android device, **Then** the audio is reproduced within 150ms of when it was produced on the desktop under normal local WiFi conditions
2. **Given** the user starts streaming, **When** the initial connection lock-on completes, **Then** audio begins playing on the Android device within 500ms of starting the stream
3. **Given** a brief network disruption occurred, **When** the connection recovers, **Then** audio resumes within 3 seconds and does not permanently revert to a high-latency buffering state

---

### User Story 2 - Latency Visibility and Manual Tuning (Priority: P2)

As a technically aware user who wants to balance latency against audio stability, I want to see the current end-to-end latency and adjust buffering parameters so that I can tune the experience for my specific network environment.

**Why this priority**: Without feedback, users cannot know whether optimization is working. Visibility enables informed manual tuning and validates the P1 improvements. This story is useful independently even without the adaptive system in P3.

**Independent Test**: Can be tested independently by running the stream, observing a latency metric in the UI or log output, and changing a buffer size parameter — without the adaptive tuning feature from P3 being present.

**Acceptance Scenarios**:

1. **Given** the stream is active, **When** the user checks the launcher or log output, **Then** a latency estimate in milliseconds is visible and updates at least once per second
2. **Given** the user increases the buffer setting to a larger value, **When** the change takes effect, **Then** audio stability improves and the reported latency increases accordingly
3. **Given** the user decreases the buffer setting to a smaller value, **When** the change takes effect, **Then** the reported latency decreases, potentially at the cost of occasional brief audio dropouts on congested networks

---

### User Story 3 - Adaptive Latency Management (Priority: P3)

As a user on a variable WiFi network (e.g., a shared home network with multiple active devices), I want the system to automatically adjust its buffering to maintain low latency while avoiding dropouts, so that I do not have to manually retune settings when network conditions change.

**Why this priority**: Adaptive tuning is a quality-of-life improvement on top of the core feature. P1 and P2 must be solid before automation adds meaningful value.

**Independent Test**: Can be tested by simulating varying network conditions (e.g., running a large download on the same network) and observing whether the system self-corrects without user intervention.

**Acceptance Scenarios**:

1. **Given** the network has been stable for 30 seconds, **When** no packet loss or jitter is observed, **Then** the system automatically settles to the minimum stable buffer size
2. **Given** a sudden increase in network jitter occurs, **When** the system detects higher packet arrival variance, **Then** it increases buffering within 2 seconds to prevent dropouts
3. **Given** network conditions improve after a period of jitter, **When** the system detects stable packet delivery for 10 seconds, **Then** it gradually reduces buffering back toward the minimum configured depth

---

### Edge Cases

- What happens when network jitter exceeds the maximum configured buffer depth?
- How does the system handle sustained packet loss rates above 5%?
- What happens when the Android device's audio hardware requires a minimum buffer larger than the target latency allows?
- How should the system behave when the Windows host is under heavy CPU load that affects audio capture or encoding timing?
- What happens when the user changes the buffer setting while audio is actively playing?

## Requirements

### Functional Requirements

- **FR-001**: System MUST deliver audio from desktop capture to Android playback with an end-to-end latency below 150ms under normal local network conditions (single access point, low interference)
- **FR-002**: System MUST provide a configurable jitter buffer target depth that users can set to favor lower latency or higher stability
- **FR-003**: System MUST drop stale buffered audio and re-sync to the newest available audio when accumulated latency exceeds a threshold (e.g., after a device wake or stream pause)
- **FR-004**: System MUST expose a real-time latency estimate to the user, visible in the launcher UI or log output, updating at least once per second
- **FR-005**: System MUST recover to low-latency playback within 3 seconds of a network disruption that lasted under 2 seconds
- **FR-006**: System MUST NOT introduce audio artifacts (clicks, pops, or silence gaps longer than 50ms) during normal low-latency operation under stable network conditions
- **FR-007**: System SHOULD automatically adjust buffering in response to observed packet jitter — expanding buffer when jitter increases, contracting when conditions stabilize

### Key Entities

- **Jitter Buffer**: Holds received audio packets awaiting playback; its target depth (in packets or milliseconds) is the primary control over the latency vs. stability trade-off
- **Latency Estimate**: A computed real-time value (ms) representing the observed delay from audio capture timestamp to current playback position
- **Buffer Configuration**: A user-settable parameter controlling how aggressively the system trades latency for stability; may be a single value or a min/max range for adaptive mode

## Success Criteria

### Measurable Outcomes

- **SC-001**: End-to-end audio latency is below 150ms on a local WiFi network under normal conditions, representing at least a 50% reduction from the current baseline
- **SC-002**: Audio dropouts occur fewer than once per minute under normal single-access-point WiFi conditions
- **SC-003**: The system recovers to low-latency playback within 3 seconds of a brief network disruption (under 2 seconds)
- **SC-004**: Users can observe a real-time latency estimate that refreshes at least once per second
- **SC-005**: Re-sync after a latency spike (e.g., device returning from sleep) completes within 1 second of the stream resuming

## Assumptions

- The primary network environment is a home or office LAN with a single WiFi access point; multi-hop or WAN configurations are out of scope
- The Android device and Windows host are connected to the same WiFi network with no firewall blocking UDP
- "High latency" refers to a perceived audio delay of roughly 300–800ms or more, based on typical conservative jitter buffer defaults; the exact current baseline is not measured
- The Windows host plays audio continuously (not intermittent short bursts); the optimization targets live streaming, not transfer of pre-recorded files
- iOS support is out of scope; only Android is targeted
- Multiple simultaneous receiver devices are out of scope for this feature
- The existing Opus codec and UDP transport layer are retained; codec or protocol changes are not in scope
