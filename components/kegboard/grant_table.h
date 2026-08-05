#pragma once

// Authorization grants.
//
// Implements the device half of docs/authenticated-pouring.md: server-issued
// grants that each carry their own meter and relay sets and limits
// (protocol §7.1), one active grant per meter, in-place updates by grant id,
// and the device-side duration clamp. Every ending is reported with a
// reason, for the grant_end event (protocol §5.7).
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstdint>
#include <string>
#include <vector>

namespace kbcore {

/// Why a grant (or part of one) ended; protocol §5.7.
enum class GrantEndReason : uint8_t { MAX_VOLUME, MAX_DURATION, MAX_IDLE, DETACH, COMMAND, REPLACED };

/// The protocol string for a reason, e.g. "max_volume".
const char *grant_end_reason_str(GrantEndReason reason);

/// What an authorize command carries (protocol §7.1), plus the `local` flag
/// for device-decided grants (local mode, offline-guest), whose ids are
/// internal and never reported.
struct GrantSpec {
  std::string grant_id;
  bool local{false};
  std::vector<uint8_t> meters;
  std::vector<uint8_t> relays;
  std::string auth_device;
  std::string token;
  /// 0 = unlimited.
  float max_volume_ml{0.0f};
  /// 0 = unbounded by the issuer; the table clamp always applies.
  uint32_t max_duration_ms{0};
  /// 0 = no idle limit.
  uint32_t max_idle_ms{0};
};

struct Grant {
  GrantSpec spec;
  uint32_t created_ms{0};
  uint32_t last_flow_ms{0};
  float poured_ml{0.0f};
  /// max_duration_ms clamped to the table maximum; never 0.
  uint32_t effective_max_duration_ms{0};
};

/// One grant ending, ready to become a grant_end event. For a partial ending
/// (reason REPLACED with the grant surviving) `relays` is empty — the grant
/// keeps its relays. `volume_ml`/`duration_ms` are snapshots of the whole
/// grant, per protocol §5.7.
struct GrantEnd {
  /// Empty for local grants: the protocol omits the field.
  std::string grant_id;
  std::string auth_device;
  std::string token;
  /// Meters released by this ending.
  std::vector<uint8_t> meters;
  /// Relays the ended grant named; empty when the grant survives.
  std::vector<uint8_t> relays;
  GrantEndReason reason{GrantEndReason::COMMAND};
  float volume_ml{0.0f};
  uint32_t duration_ms{0};
};

/// Tracks active grants. One grant per meter; a grant covering an
/// already-covered meter takes it over. All mutating calls return the
/// resulting endings so the caller can drive relays, end pours, and queue
/// grant_end events without re-deriving state.
class GrantTable {
 public:
  /// Device-side safety backstop on total grant lifetime (protocol §7.1).
  /// Applies whatever max_duration_ms says, including "unlimited".
  void set_max_duration_ms(uint32_t v) { max_duration_ms_ = v; }
  uint32_t max_duration_ms() const { return max_duration_ms_; }

  /// Create — or, when a live grant already has spec.grant_id, update — a
  /// grant. Meters entering the scope leave whichever grant covered them;
  /// an update sheds the meters no longer in its scope. Updates replace
  /// sets and limits but carry poured volume and grant age over.
  /// @return REPLACED endings for every meter that left a grant.
  std::vector<GrantEnd> authorize(const GrantSpec &spec, uint32_t now_ms);

  /// Revoke by grant id (reason COMMAND). Unknown ids are ignored.
  std::vector<GrantEnd> deauthorize(const std::vector<std::string> &grant_ids, uint32_t now_ms);

  /// Revoke everything (reason COMMAND).
  std::vector<GrantEnd> deauthorize_all(uint32_t now_ms);

  /// End every grant held by the presentment, for presence-reader detach
  /// (reason DETACH). Matches the grant's token, and its auth_device too
  /// when both sides carry one — so a stale detach, or the same token value
  /// leaving a different reader, cannot close someone else's tap.
  std::vector<GrantEnd> detach(const std::string &auth_device, const std::string &token, uint32_t now_ms);

  /// Flow observed on a meter: resets the covering grant's idle clock and
  /// accrues volume. @return a MAX_VOLUME ending if the limit tripped.
  std::vector<GrantEnd> record_flow(uint8_t meter, float delta_ml, uint32_t now_ms);

  /// Expire due grants (MAX_DURATION / MAX_IDLE).
  std::vector<GrantEnd> poll(uint32_t now_ms);

  /// The active grant covering a meter, or nullptr. Valid until the next
  /// mutation.
  const Grant *grant_for(uint8_t meter) const;

  /// The active grant with this id, or nullptr. Valid until the next
  /// mutation.
  const Grant *grant_by_id(const std::string &grant_id) const;

  /// True while any active grant names this relay (protocol §7.1: a relay is
  /// energized while any grant names it).
  bool covers_relay(uint8_t relay) const;

  /// Every meter covered by an active grant.
  std::vector<uint8_t> active_meters() const;

  bool any_active() const { return !grants_.empty(); }
  size_t active_count() const { return grants_.size(); }

 private:
  GrantEnd make_end_(const Grant &grant, std::vector<uint8_t> meters, GrantEndReason reason, uint32_t now_ms) const;
  /// End the whole grant at `index` and remove it.
  GrantEnd end_grant_(size_t index, GrantEndReason reason, uint32_t now_ms);
  uint32_t effective_max_duration_(uint32_t requested_ms) const;

  std::vector<Grant> grants_;
  uint32_t max_duration_ms_{300000};
};

}  // namespace kbcore
