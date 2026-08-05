#pragma once

#include <functional>
#include <string>

#include "esphome/core/component.h"

namespace esphome::kegboard {

extern const char *const KEGBOARD_VERSION;

/// Shared board identity for a Kegboard.
///
/// Mostly exists so meters and reporters agree on what this board is called.
/// The protocol identifies a tap by `(device, meter_number)`, so the serial
/// number is what ties a physical board to its taps on the server.
class KegboardHub : public Component {
 public:
  void setup() override;
  void dump_config() override;
  /// Identity must exist before meters or reporters read it.
  float get_setup_priority() const override { return setup_priority::DATA + 10.0f; }

  void set_serial_number(const std::string &serial_number) { this->serial_number_ = serial_number; }

  /// Board serial number, e.g. "kegboard-a1b2c3". Derived from the WiFi MAC
  /// when not set in config, so an unconfigured board still gets a stable,
  /// unique identity that survives reflashing.
  const std::string &serial_number() const { return this->serial_number_; }

  const char *version() const { return KEGBOARD_VERSION; }

  /// Installed by whichever component owns a real-time clock.
  ///
  /// This is a callback rather than a `time::RealTimeClock *` on purpose:
  /// ESPHome only copies headers for components that are actually loaded, so
  /// including the time component's header here would break every config that
  /// does not configure `time:`. A callback keeps `time` a dependency of only
  /// the components that genuinely need wall-clock timestamps.
  void set_clock_source(std::function<uint32_t()> &&source) { this->clock_source_ = std::move(source); }

  /// Current unix time, or 0 when no clock is installed or it has not synced.
  /// Callers must treat 0 as "unknown" rather than as an epoch timestamp.
  uint32_t now_unix() const { return this->clock_source_ ? this->clock_source_() : 0; }

 protected:
  std::string serial_number_;
  std::function<uint32_t()> clock_source_;
};

}  // namespace esphome::kegboard
