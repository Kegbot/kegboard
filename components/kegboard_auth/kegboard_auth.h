#pragma once

#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/kegboard/grant_table.h"
#include "esphome/components/kegboard_meter/kegboard_meter.h"
#include "esphome/components/kegboard_reporter/kegboard_reporter.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::kegboard_auth {

/// Who decides whether a token may pour; docs/authenticated-pouring.md §2.
enum class AuthMode : uint8_t { SERVER, LOCAL };

/// What happens when a token is presented while the server is unreachable.
enum class OfflinePolicy : uint8_t { DENY, GUEST };

/// Fires with the username when a grant is applied ("" = guest).
class AuthorizedTrigger : public Trigger<std::string> {};
/// Fires with the server's reason (may be "") when a presentment is refused.
class DeniedTrigger : public Trigger<std::string> {};
/// Fires when any grant ends (detach, expiry, or deauthorize).
class RevokedTrigger : public Trigger<> {};

/// Applies authorization to taps, per docs/authenticated-pouring.md.
///
/// Holds no token database. In `server` mode every presentment is sent to the
/// server, whose authorize/deny commands ride back in the same round trip; in
/// `local` mode every token is accepted as guest. Grants are per meter, in a
/// kbcore::GrantTable; this class drives the toggles and meter attribution
/// that the grants imply.
class KegboardAuth : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_reporter(kegboard_reporter::KegboardReporter *r) { this->reporter_ = r; }
  void set_mode(AuthMode mode) { this->mode_ = mode; }
  void set_offline_policy(OfflinePolicy p) { this->offline_policy_ = p; }
  void set_max_grant_duration_ms(uint32_t v) { this->grants_.set_max_duration_ms(v); }
  void set_local_grant_duration_ms(uint32_t v) { this->local_grant_duration_ms_ = v; }

  void add_gate(kegboard_meter::KegboardMeter *meter, switch_::Switch *toggle) {
    this->gates_.push_back(Gate{meter, toggle});
  }

  void set_authorized_binary_sensor(binary_sensor::BinarySensor *s) { this->authorized_sensor_ = s; }
  void set_user_text_sensor(text_sensor::TextSensor *s) { this->user_sensor_ = s; }

  void add_on_authorized_trigger(AuthorizedTrigger *t) { this->authorized_triggers_.push_back(t); }
  void add_on_denied_trigger(DeniedTrigger *t) { this->denied_triggers_.push_back(t); }
  void add_on_revoked_trigger(RevokedTrigger *t) { this->revoked_triggers_.push_back(t); }

  /// A reader saw a token arrive.
  void token_attached(const std::string &auth_device, const std::string &token);

  /// A reader saw a token leave (presence readers only).
  void token_detached(const std::string &auth_device, const std::string &token);

  /// Revoke every grant, e.g. a manual lockout.
  void revoke_all();

  bool is_authorized() const { return this->grants_.any_active(); }

 protected:
  struct Gate {
    kegboard_meter::KegboardMeter *meter;
    switch_::Switch *toggle;  // may be nullptr: attribution only
  };

  kegboard_reporter::CommandOutcome handle_command_(const std::string &type, JsonObjectConst data,
                                                    std::string &message);

  /// Sync toggles and meter attribution to the grant table for `meters`,
  /// opening toggles only when `open_toggles` allows (offline-guest grants
  /// never open valves).
  void apply_meters_(const std::vector<uint8_t> &meters, bool open_toggles);
  void revoke_meters_(const std::vector<uint8_t> &meters);
  Gate *gate_for_(uint8_t meter);
  std::vector<uint8_t> all_gate_meters_() const;
  void publish_state_();
  void fire_denied_(const std::string &reason);

  kbcore::GrantTable grants_;
  kegboard_reporter::KegboardReporter *reporter_{nullptr};
  AuthMode mode_{AuthMode::SERVER};
  OfflinePolicy offline_policy_{OfflinePolicy::DENY};
  uint32_t local_grant_duration_ms_{30000};

  std::vector<Gate> gates_;

  /// Set while dispatching commands from a token-ask response, so the absence
  /// of any decision can be detected (treated as deny, per the doc).
  bool decision_received_{false};

  binary_sensor::BinarySensor *authorized_sensor_{nullptr};
  text_sensor::TextSensor *user_sensor_{nullptr};

  std::vector<AuthorizedTrigger *> authorized_triggers_;
  std::vector<DeniedTrigger *> denied_triggers_;
  std::vector<RevokedTrigger *> revoked_triggers_;
};

template<typename... Ts> class TokenAttachedAction : public Action<Ts...>, public Parented<KegboardAuth> {
 public:
  TEMPLATABLE_VALUE(std::string, device)
  TEMPLATABLE_VALUE(std::string, token)

  void play(const Ts &...x) override {
    this->parent_->token_attached(this->device_.value(x...), this->token_.value(x...));
  }
};

template<typename... Ts> class TokenDetachedAction : public Action<Ts...>, public Parented<KegboardAuth> {
 public:
  TEMPLATABLE_VALUE(std::string, device)
  TEMPLATABLE_VALUE(std::string, token)

  void play(const Ts &...x) override {
    this->parent_->token_detached(this->device_.value(x...), this->token_.value(x...));
  }
};

template<typename... Ts> class RevokeAction : public Action<Ts...>, public Parented<KegboardAuth> {
 public:
  void play(const Ts &...x) override { this->parent_->revoke_all(); }
};

template<typename... Ts> class AuthorizedCondition : public Condition<Ts...>, public Parented<KegboardAuth> {
 public:
  bool check(const Ts &...x) override { return this->parent_->is_authorized(); }
};

}  // namespace esphome::kegboard_auth
