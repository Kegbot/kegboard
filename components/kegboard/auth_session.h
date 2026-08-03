#pragma once

// Authorization grant state machine.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstdint>
#include <string>

namespace kbcore {

struct AuthConfig {
  /// How long a grant lasts without further activity. Momentary readers
  /// (RFID) rely on this entirely; presence readers (iButton) usually revoke
  /// sooner by detaching.
  uint32_t grant_duration_ms{30000};
};

/// Tracks who, if anyone, is currently authorized to pour.
///
/// A single grant at a time. A second token simply replaces the first, which
/// is the right behaviour for a kegerator: the person standing at the tap is
/// the person holding the most recently presented token.
///
/// The same model covers both reader styles. A momentary RFID scan grants
/// access that expires on its own; an iButton held to a probe grants access
/// that is revoked the moment it is pulled away, whichever comes first.
class AuthSession {
 public:
  explicit AuthSession(const AuthConfig &config) : config_(config) {}

  void set_config(const AuthConfig &config) { config_ = config; }
  void set_grant_duration_ms(uint32_t ms) { config_.grant_duration_ms = ms; }
  const AuthConfig &config() const { return config_; }

  bool is_authorized() const { return authorized_; }
  const std::string &device() const { return device_; }
  const std::string &token() const { return token_; }

  /// Kegbot username the grant resolved to. Empty means authorized but
  /// unidentified, which Kegbot Server records against the guest user.
  const std::string &username() const { return username_; }
  void set_username(const std::string &username) { username_ = username; }

  /// Begin or replace a grant.
  /// @return true if this started a grant for a token that was not already
  ///         the active one; false if it merely refreshed the current grant.
  bool attach(const std::string &device, const std::string &token, uint32_t now_ms);

  /// Revoke if `token` holds the current grant. A detach for some other token
  /// is ignored, so a stale reader event cannot close someone else's tap.
  /// @return true if a grant was revoked.
  bool detach(const std::string &token);

  /// Revoke unconditionally, e.g. at shutdown or on an explicit lockout.
  /// @return true if a grant was revoked.
  bool revoke();

  /// Push the expiry out. Called while a pour is running so that a long pour
  /// is not cut off mid-glass by a grant that started before it.
  void extend(uint32_t now_ms);

  /// Expire the grant if its window has elapsed.
  /// @return true if this call ended a grant.
  bool poll(uint32_t now_ms);

  /// Milliseconds until expiry, or 0 when not authorized.
  uint32_t remaining_ms(uint32_t now_ms) const;

 private:
  void clear_();

  AuthConfig config_;

  bool authorized_{false};
  std::string device_;
  std::string token_;
  std::string username_;
  uint32_t expires_at_ms_{0};
};

}  // namespace kbcore
