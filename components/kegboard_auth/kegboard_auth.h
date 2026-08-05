#pragma once

#include <memory>
#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/kegboard/auth_engine.h"
#include "esphome/components/kegboard_meter/kegboard_meter.h"
#include "esphome/components/kegboard_reporter/kegboard_reporter.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::kegboard_auth {

/// What happens when a token is presented while the server is unreachable.
enum class OfflinePolicy : uint8_t { DENY, GUEST };

/// Fires with (auth_device, token) when a grant is applied. The device never
/// learns user identity; the server resolves it from the token or grant_id.
class AuthorizedTrigger : public Trigger<std::string, std::string> {};
/// Fires with the server's reason (may be "") when a presentment is refused.
class DeniedTrigger : public Trigger<std::string> {};
/// Fires when a grant ends (limit, detach, replacement, or deauthorize).
class RevokedTrigger : public Trigger<> {};

/// Applies authorization to taps, per docs/authenticated-pouring.md.
///
/// A thin adapter: token presentments go to the server through the
/// reporter, and the server's authorize/deny/deauthorize commands drive a
/// kbcore::AuthEngine, which owns all grant semantics (validation, limits,
/// relays, attribution, adoption, endings). This class supplies the engine
/// its device — meters and relays via the reporter — plus JSON parsing,
/// entities, triggers, and logging.
class KegboardAuth : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_reporter(kegboard_reporter::KegboardReporter *r) { this->reporter_ = r; }
  void set_offline_policy(OfflinePolicy p) { this->offline_policy_ = p; }
  void set_max_grant_duration_ms(uint32_t v) { this->max_grant_duration_ms_ = v; }

  void set_authorized_binary_sensor(binary_sensor::BinarySensor *s) { this->authorized_sensor_ = s; }

  void add_on_authorized_trigger(AuthorizedTrigger *t) { this->authorized_triggers_.push_back(t); }
  void add_on_denied_trigger(DeniedTrigger *t) { this->denied_triggers_.push_back(t); }
  void add_on_revoked_trigger(RevokedTrigger *t) { this->revoked_triggers_.push_back(t); }

  /// A reader saw a token arrive.
  void token_attached(const std::string &auth_device, const std::string &token);

  /// A reader saw a token leave (presence readers only).
  void token_detached(const std::string &auth_device, const std::string &token);

  /// Revoke every grant, e.g. a manual lockout.
  void revoke_all();

  bool is_authorized() const { return this->engine_ != nullptr && this->engine_->any_active(); }

 protected:
  kegboard_reporter::CommandOutcome handle_command_(const std::string &type, JsonObjectConst data,
                                                    std::string &message);
  kegboard_reporter::CommandOutcome handle_authorize_(JsonObjectConst data, std::string &message);
  kegboard_reporter::CommandOutcome handle_deauthorize_(JsonObjectConst data, std::string &message);

  kegboard_meter::KegboardMeter *meter_by_number_(uint8_t meter);
  void publish_state_();
  void fire_denied_(const std::string &reason);

  std::unique_ptr<kbcore::AuthEngine> engine_;
  kegboard_reporter::KegboardReporter *reporter_{nullptr};
  OfflinePolicy offline_policy_{OfflinePolicy::DENY};
  uint32_t max_grant_duration_ms_{300000};

  /// The reporter's meters: the inventory grants are validated against.
  std::vector<kegboard_meter::KegboardMeter *> meters_;

  /// Set while dispatching commands from a token-ask response, so the absence
  /// of any decision can be detected (treated as deny, per the doc).
  bool decision_received_{false};

  binary_sensor::BinarySensor *authorized_sensor_{nullptr};

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
