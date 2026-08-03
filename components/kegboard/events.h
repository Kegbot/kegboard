#pragma once

// Kegboard Event Protocol: event construction and batch serialization.
// Implements the wire format in docs/kegboard-event-protocol.md; the host
// tests validate the output of this module against the normative schemas in
// schemas/.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstdint>
#include <string>
#include <vector>

namespace kbcore {

/// Maximum events per batch, per protocol §3.
constexpr size_t MAX_BATCH_EVENTS = 16;

/// A protocol event, payload pre-serialized.
///
/// The `data` object is rendered at creation time (when its inputs are at
/// hand) and stored as JSON text; only the envelope — and in particular
/// `age_ms`, which must be recomputed at every send — is rendered at batch
/// time.
struct Event {
  uint32_t id{0};
  /// Protocol type string, e.g. "pour". Points at a string literal.
  const char *type{""};
  /// Monotonic ms at which the event occurred; age_ms derives from this.
  uint32_t created_ms{0};
  /// RFC 3339 wall time, or empty if the clock was not synced. Informational.
  std::string time;
  /// Serialized `data` object, from one of the builders below.
  std::string data_json;
};

// --- Payload builders ------------------------------------------------------
// Each returns the serialized `data` object for one event type. Optional
// protocol fields are omitted when their inputs are empty/negative, matching
// the schema's required lists exactly.

struct PourData {
  uint8_t meter{0};
  std::string pour_id;
  float volume_ml{0.0f};
  uint32_t duration_ms{0};
  std::string user;
  std::string auth_device;
  std::string auth_token;
  /// UINT32_MAX omits `ticks`.
  uint32_t ticks{UINT32_MAX};
  /// <= 0 omits `ml_per_tick`.
  float ml_per_tick{0.0f};
  std::string tick_series;
};

std::string pour_data_json(const PourData &d);

std::string pour_update_data_json(uint8_t meter, const std::string &pour_id, float volume_ml, uint32_t duration_ms,
                                  float rate_ml_per_min);

std::string temperature_data_json(const std::string &sensor, float temp_c);

/// `status`: pass TOKEN_STATUS_NONE when the server should decide (protocol
/// leaves the field absent), otherwise accepted/denied for local decisions.
enum class TokenStatus : uint8_t { NONE, ACCEPTED, DENIED };

std::string token_data_json(const std::string &auth_device, const std::string &token, bool attached, TokenStatus status,
                            const std::string &user);

struct StatusMeter {
  uint8_t meter{0};
  uint32_t total_ticks{0};
  float ml_per_tick{0.0f};
};

struct StatusData {
  bool boot{false};
  std::string fw_version;
  uint32_t uptime_ms{0};
  /// true includes wifi_rssi_dbm.
  bool has_rssi{false};
  int32_t rssi_dbm{0};
  uint32_t events_dropped{0};
  uint32_t heartbeat_ms{0};
  uint32_t pour_update_ms{0};
  uint32_t queue_capacity{0};
  std::vector<StatusMeter> meters;
};

std::string status_data_json(const StatusData &d);

std::string command_result_data_json(const std::string &command, const char *result, const std::string &message);

// --- Batch serialization ---------------------------------------------------

/// Serialize a batch envelope. `now_ms` is the monotonic clock at send time;
/// each event's age_ms is computed from it (unsigned subtraction, so correct
/// across the 32-bit rollover). Caller limits `events` to MAX_BATCH_EVENTS.
std::string serialize_batch(const std::string &device, const std::string &boot_id, uint32_t now_ms,
                            const std::vector<const Event *> &events);

// --- Identifier formatting -------------------------------------------------

/// 8-hex-char boot id from one random word.
std::string format_boot_id(uint32_t random);

/// Canonical lowercase UUIDv4 from 16 random bytes; sets the version and
/// variant bits. The protocol treats pour ids as opaque, so this format may
/// change without notice — nothing outside this function may assume it.
std::string format_uuid4(const uint8_t random_bytes[16]);

}  // namespace kbcore
