#pragma once

// Bounded tick time series.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstddef>
#include <cstdint>
#include <string>

namespace kegboard {

/// A bounded record of when ticks arrived during a pour.
///
/// Kegbot Server accepts this as a Drink's `tick_time_series`, a
/// space-separated sequence of `<offset_ms>:<ticks>` pairs. It is diagnostic
/// data only, so this class prioritizes bounded memory over fidelity: ticks
/// are bucketed at a configurable resolution, and when the buffer fills, the
/// series is coarsened in place (adjacent buckets merged, resolution doubled)
/// rather than truncated. A long pour therefore stays fully represented, at
/// progressively lower time resolution.
class TickSeries {
 public:
  /// Maximum number of buckets held. At 8 bytes per bucket this caps the
  /// series at a few hundred bytes per in-flight pour.
  static constexpr size_t CAPACITY = 64;

  /// Default bucketing resolution, matching the legacy AVR firmware's
  /// KB_METER_UPDATE_INTERVAL_MS.
  static constexpr uint32_t DEFAULT_RESOLUTION_MS = 100;

  struct Bucket {
    uint32_t offset_ms;
    uint32_t ticks;
  };

  /// Discard all data and begin a new series at the given resolution.
  /// A resolution of zero disables recording entirely.
  void reset(uint32_t resolution_ms = DEFAULT_RESOLUTION_MS);

  /// Record ticks observed at `offset_ms` after the start of the pour.
  void add(uint32_t offset_ms, uint32_t ticks);

  /// Serialize to Kegbot's `<offset>:<ticks>` wire format. Returns an empty
  /// string when the series is empty or recording is disabled.
  std::string to_string() const;

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  bool enabled() const { return resolution_ms_ != 0; }

  /// Current bucket width. Grows past the configured value if the series had
  /// to be coarsened to fit.
  uint32_t resolution_ms() const { return resolution_ms_; }

  /// True if the series was coarsened at least once, i.e. its resolution is
  /// no longer the one it started with.
  bool coarsened() const { return coarsened_; }

  const Bucket &operator[](size_t i) const { return buckets_[i]; }

 private:
  /// Halve the bucket count by merging adjacent pairs, doubling resolution.
  void coarsen_();

  Bucket buckets_[CAPACITY]{};
  size_t count_{0};
  uint32_t resolution_ms_{DEFAULT_RESOLUTION_MS};
  bool coarsened_{false};
};

}  // namespace kegboard
