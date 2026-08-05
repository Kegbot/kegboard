#pragma once

// The authorization engine: composes the grant table with flow accounting
// and the device actions grants imply — validation against the inventory,
// relay energize/release, pour attribution, adoption of in-flight pours,
// and the pour-before-grant_end ordering. Implements the grant semantics
// of docs/authenticated-pouring.md, corner cases included. The ESPHome
// component supplies the device through callbacks; keeping the composition
// here is what lets the host suite exercise those semantics without a
// board.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "grant_table.h"

namespace kbcore {

/// How the engine touches the device. Every callback must be set.
///
/// end_pour must end any in-flight pour synchronously, running the device's
/// normal pour path — including calling AuthEngine::on_pour_end — before it
/// returns, so a pour's event is queued before the grant_end that ended it.
struct AuthDevice {
  std::function<bool(uint8_t meter)> has_meter;
  std::function<bool(uint8_t relay)> has_relay;
  std::function<bool(uint8_t meter)> is_pouring;
  std::function<float(uint8_t meter)> session_volume_ml;
  std::function<void(uint8_t meter)> end_pour;
  std::function<void(uint8_t meter, const GrantSpec &spec)> set_attribution;
  std::function<void(uint8_t meter)> clear_attribution;
  std::function<void(uint8_t relay, bool on)> set_relay;
  /// Queue the grant_end event; called after the pour path has run.
  std::function<void(const GrantEnd &end)> emit_grant_end;
};

class AuthEngine {
 public:
  explicit AuthEngine(AuthDevice device) : device_(std::move(device)) {}

  void set_max_grant_duration_ms(uint32_t v) { this->grants_.set_max_duration_ms(v); }
  uint32_t max_grant_duration_ms() const { return this->grants_.max_duration_ms(); }

  struct Outcome {
    bool ok{true};
    /// True when an existing grant was updated in place.
    bool updated{false};
    /// Error detail, or a note on an ok outcome (e.g. the grant died while
    /// being applied).
    std::string message;
  };

  /// Validate the grant against the device inventory and apply it: a grant
  /// naming a meter or relay the device does not have is rejected in
  /// whole. Applies replacement, in-place updates, adoption of in-flight
  /// pours, and relay changes.
  Outcome authorize(const GrantSpec &spec, uint32_t now_ms);

  /// Revoke by id, or everything when `all`. Unknown ids are ignored.
  /// @return grants ended.
  size_t deauthorize(const std::vector<std::string> &grant_ids, bool all, uint32_t now_ms);

  /// Presence detach. @return grants ended.
  size_t detach(const std::string &auth_device, const std::string &token, uint32_t now_ms);

  void revoke_all(uint32_t now_ms) { this->process_ends_(this->grants_.deauthorize_all(now_ms)); }

  /// A pour completed with `volume_ml`: true up flow accounting with
  /// whatever accrued since the last poll. Call from the device's pour
  /// path, after the pour event is queued.
  void on_pour_end(uint8_t meter, float volume_ml, uint32_t now_ms);

  /// Periodic: live flow deltas feed the volume and idle limits (the valve
  /// closes the moment a limit trips, not at pour end), and due grants
  /// expire.
  void poll(uint32_t now_ms);

  bool any_active() const { return this->grants_.any_active(); }
  const GrantTable &grants() const { return this->grants_; }

 private:
  /// Act on grant endings: end in-flight pours first (the pour event
  /// precedes the grant_end), clear attribution, release relays no other
  /// grant names, emit grant_end events.
  void process_ends_(const std::vector<GrantEnd> &ends);
  void adopt_in_flight_pour_(uint8_t meter);

  AuthDevice device_;
  GrantTable grants_;

  /// Session volume already fed into the grant table per meter, so poll()
  /// can hand the table deltas while a pour runs and on_pour_end can true
  /// up the tail.
  std::map<uint8_t, float> pour_seen_ml_;
};

}  // namespace kbcore
