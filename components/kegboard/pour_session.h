#pragma once

// Pour session state machine.
//
// This file is part of the framework-agnostic kegboard core: it must not
// include any ESPHome (or Arduino, or ESP-IDF) headers, and must remain
// compilable and testable on a host with plain g++. See CORE.md.

#include <cstdint>

#include "tick_series.h"

namespace kegboard {

/// A pour that has ended and is ready to be reported.
struct PourRecord {
  /// Ticks accumulated over the whole pour.
  uint32_t ticks{0};
  /// Volume in milliliters, ticks * ml_per_tick at the time the pour ended.
  float volume_ml{0.0f};
  /// Wall time the pour started, as a unix timestamp. Zero if the clock was
  /// not synchronized when the pour began; consumers must handle that.
  uint32_t start_unix{0};
  /// Duration from first to last tick, in milliseconds.
  uint32_t duration_ms{0};
  /// Per-interval tick counts, oldest first. May be empty if the series was
  /// disabled or overflowed.
  TickSeries series;
};

/// Tunables for a single meter's pour detection. Times are milliseconds.
struct PourConfig {
  /// A pour ends once this long passes with no ticks.
  uint32_t idle_timeout_ms{10000};
  /// Pours shorter than this many ticks are discarded as drips or noise.
  uint32_t min_pour_ticks{3};
  /// A pour is force-ended after this long, to bound a stuck or free-running
  /// meter. Zero disables the cutoff.
  uint32_t max_duration_ms{300000};
  /// Milliliters per tick. The SwissFlow SF800 default is ~0.185 (5.4
  /// ticks/mL); this is the value users calibrate.
  float ml_per_tick{0.185f};
};

/// Tracks one meter's transition between idle and pouring.
///
/// The caller feeds it ticks (from an ISR, batched) and a monotonic clock, and
/// it reports when a pour begins and when one has ended. It owns no I/O and no
/// timers, which is what makes it testable on a host.
///
/// All times passed in are monotonic milliseconds since boot. Wall-clock unix
/// times are passed separately and only used to stamp the resulting record,
/// because a device may pour before its clock is ever synchronized.
class PourSession {
 public:
  explicit PourSession(const PourConfig &config) : config_(config) {}

  const PourConfig &config() const { return config_; }
  void set_config(const PourConfig &config) { config_ = config; }
  void set_ml_per_tick(float ml_per_tick) { config_.ml_per_tick = ml_per_tick; }

  /// True while a pour is in progress.
  bool is_pouring() const { return pouring_; }

  /// Ticks accumulated in the current pour. Zero when idle.
  uint32_t session_ticks() const { return pouring_ ? session_ticks_ : 0; }

  /// Volume of the current pour in mL. Zero when idle.
  float session_volume_ml() const { return session_ticks() * config_.ml_per_tick; }

  /// Lifetime tick count, which survives pour boundaries and never resets
  /// except via reset_total().
  uint32_t total_ticks() const { return total_ticks_; }
  void reset_total() { total_ticks_ = 0; }

  /// Feed ticks observed since the last call.
  ///
  /// @param ticks       Ticks counted since the previous call; may be zero.
  /// @param now_ms      Monotonic milliseconds since boot.
  /// @param now_unix    Current unix time, or 0 if the clock is not synced.
  /// @return true if this call started a new pour.
  bool add_ticks(uint32_t ticks, uint32_t now_ms, uint32_t now_unix);

  /// Advance time without adding ticks, ending the pour if it has gone idle
  /// or hit the duration cutoff.
  ///
  /// @param out  Receives the finished pour when this returns true.
  /// @return true if a pour ended on this call and passed min_pour_ticks.
  ///         A pour that ends below the threshold is discarded silently and
  ///         this returns false, so callers cannot mistake a drip for a pour.
  bool poll(uint32_t now_ms, PourRecord *out);

  /// Force the current pour to end, e.g. because the tap was locked out.
  /// Follows the same min_pour_ticks rule as poll().
  bool end_now(uint32_t now_ms, PourRecord *out);

  /// Configure the tick time series recorded during a pour. A resolution of
  /// zero disables recording.
  void set_series_resolution_ms(uint32_t resolution_ms) { series_resolution_ms_ = resolution_ms; }

 private:
  bool finish_(uint32_t now_ms, PourRecord *out);

  PourConfig config_;

  bool pouring_{false};
  uint32_t session_ticks_{0};
  uint32_t total_ticks_{0};
  uint32_t start_ms_{0};
  uint32_t start_unix_{0};
  uint32_t last_tick_ms_{0};

  uint32_t series_resolution_ms_{TickSeries::DEFAULT_RESOLUTION_MS};
  TickSeries series_;
};

}  // namespace kegboard
