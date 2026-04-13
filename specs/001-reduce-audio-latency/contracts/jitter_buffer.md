# Interface Contract: JitterBuffer

**File**: `libs/transport/include/nspeaker/transport/jitter_buffer.h`
**Feature**: 001-reduce-audio-latency

## Changes

### New Method: `NewestSequence()`

```cpp
[[nodiscard]] std::optional<std::uint32_t> NewestSequence() const;
```

**Contract**:
- Returns the highest sequence number currently held in the buffer
- Returns `std::nullopt` when the buffer is empty
- O(1) — uses `packets_.rbegin()->first` (std::map with ordered keys)
- `const` and `noexcept`-safe; does not modify buffer state

**Pre-conditions**: None
**Post-conditions**: Buffer state unchanged

**Relation to existing API**:
- Symmetric to `OldestSequence()` — same pattern, opposite end of the map
- Used by `PlayerPipeline` in `FastLock` state to determine jump target

## Unchanged API

All existing methods retain their current signatures and contracts:
- `Push(packet, expected_sequence, stats)`
- `PopNext(expected_sequence)`
- `OldestSequence()`
- `Reset()`
- `Size()`
- `Primed()`
- `target_packets()`
