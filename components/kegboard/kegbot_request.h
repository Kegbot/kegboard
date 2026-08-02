#pragma once

// Kegbot Server request construction.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstdint>
#include <string>

namespace kbcore {

/// A prepared HTTP call, ready for whatever transport the platform layer uses.
struct HttpCall {
  std::string method;
  std::string url;
  /// Form-encoded (application/x-www-form-urlencoded) request body. Empty for
  /// GET requests.
  std::string body;
};

/// A finished pour, queued for delivery to Kegbot Server.
///
/// Two clocks are carried deliberately. `pour_time_unix` is the wall time the
/// pour started, which is only meaningful if NTP had synced by then;
/// `pour_uptime_s` is monotonic seconds since boot, which is always valid.
/// See KegbotRequestBuilder::drink_post() for how they are used.
struct DrinkReport {
  std::string meter_name;
  uint32_t ticks{0};
  /// Volume in mL. Only sent when the reporter is configured to override the
  /// server's own per-meter calibration; see `send_volume` on the builder.
  float volume_ml{0.0f};
  uint32_t duration_s{0};
  uint32_t pour_time_unix{0};
  uint32_t pour_uptime_s{0};
  /// Username to attribute the pour to. Empty attributes it to the guest user.
  std::string username;
  /// Kegbot `<offset_ms>:<ticks>` series; empty to omit.
  std::string tick_time_series;
};

/// A temperature reading queued for delivery.
struct ThermoReport {
  std::string sensor_name;
  float temp_c{0.0f};
  uint32_t when_unix{0};
  uint32_t when_uptime_s{0};
};

/// Builds Kegbot Server API calls from reports.
///
/// Stateless apart from the base URL and a couple of policy flags, so it is
/// straightforward to unit test the exact bytes we put on the wire.
class KegbotRequestBuilder {
 public:
  /// Set the server root, e.g. "https://kegbot.example.com". A trailing slash
  /// and a trailing "/api" are both tolerated and normalized away.
  void set_base_url(const std::string &base_url);
  const std::string &base_url() const { return base_url_; }

  /// When true, include `volume_ml` in drink posts, overriding the server's
  /// per-meter calibration. Off by default: Kegbot Server already stores
  /// ml_per_tick per meter and exposes a calibration UI, and having two
  /// sources of truth for volume is a reliable way to produce confusing data.
  void set_send_volume(bool send_volume) { send_volume_ = send_volume; }
  bool send_volume() const { return send_volume_; }

  /// POST a finished pour to /api/taps/<meter_name>.
  ///
  /// Kegbot Server reconstructs the pour time as
  /// `server_now - (now - pour_time)`, so only the *difference* between the
  /// two timestamps matters. That lets a queued pour be delivered long after
  /// the fact with a correct timestamp, and it lets a device whose clock never
  /// synced report accurately: when no wall time is available we send the
  /// monotonic uptime pair instead, whose difference is equally valid.
  ///
  /// @param now_unix     Current wall time, or 0 if the clock is not synced.
  /// @param now_uptime_s Current monotonic seconds since boot.
  HttpCall drink_post(const DrinkReport &report, uint32_t now_unix, uint32_t now_uptime_s) const;

  /// POST a reading to /api/thermo-sensors/<sensor_name>. Uses the same
  /// two-clock scheme as drink_post().
  HttpCall thermo_post(const ThermoReport &report, uint32_t now_unix, uint32_t now_uptime_s) const;

  /// GET /api/auth-tokens/<device>/<token>, used to resolve a scanned token
  /// to a Kegbot user.
  HttpCall auth_token_get(const std::string &device, const std::string &token) const;

 private:
  std::string base_url_;
  bool send_volume_{false};
};

/// Percent-encode a string for use in a URL path segment or form body.
std::string url_encode(const std::string &value);

/// Format a float with the given number of decimal places, without pulling in
/// iostreams. Used for volume and temperature fields.
std::string format_float(float value, int decimals);

}  // namespace kbcore
