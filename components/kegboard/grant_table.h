#pragma once

// Per-meter authorization grants.
//
// Implements the device half of docs/authenticated-pouring.md: one active
// grant per meter, server-decided meter sets, and the device-side duration
// clamp. Replaces the earlier single-grant AuthSession.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstdint>
#include <string>
#include <vector>

namespace kbcore {

struct Grant {
  std::string user;
  std::string auth_device;
  std::string token;
  /// Clamped duration this grant was issued with; extend() renews by it.
  uint32_t duration_ms{0};
  uint32_t expires_at_ms{0};
};

/// Tracks which meters are currently authorized, by whom.
///
/// One grant per meter; a new grant for an already-granted meter replaces it
/// (the person at the tap is whoever presented most recently). All mutating
/// calls return the affected meter numbers so the caller can drive toggles
/// and sensors without re-deriving state.
class GrantTable {
 public:
  /// Device-side safety backstop on grant duration (authenticated-pouring
  /// §4.1). Server requests above it are clamped silently.
  void set_max_duration_ms(uint32_t v) { max_duration_ms_ = v; }
  uint32_t max_duration_ms() const { return max_duration_ms_; }

  /// Apply a grant to each listed meter. Duration is clamped to the maximum.
  void authorize(const std::vector<uint8_t> &meters, const std::string &user, const std::string &auth_device,
                 const std::string &token, uint32_t duration_ms, uint32_t now_ms);

  /// Revoke the listed meters. Meters with no grant are ignored.
  /// @return meters that actually had a grant revoked.
  std::vector<uint8_t> deauthorize(const std::vector<uint8_t> &meters);

  /// Revoke everything. @return meters that had a grant.
  std::vector<uint8_t> deauthorize_all();

  /// Revoke every grant held by `token`, for presence-reader detach. Grants
  /// held by other tokens are untouched, so a stale detach cannot close
  /// someone else's tap.
  std::vector<uint8_t> detach(const std::string &token);

  /// Push a meter's expiry out; called while it is actively pouring.
  void extend(uint8_t meter, uint32_t now_ms);

  /// Expire due grants. @return meters whose grant expired on this call.
  std::vector<uint8_t> poll(uint32_t now_ms);

  /// The active grant for a meter, or nullptr. Valid until the next mutation.
  const Grant *grant_for(uint8_t meter) const;

  bool any_active() const { return !entries_.empty(); }
  size_t active_count() const { return entries_.size(); }

 private:
  struct Entry {
    uint8_t meter;
    Grant grant;
  };

  std::vector<Entry> entries_;
  uint32_t max_duration_ms_{300000};
};

}  // namespace kbcore
