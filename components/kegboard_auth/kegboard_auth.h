#pragma once

#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/kegboard/auth_session.h"
#include "esphome/components/kegboard_kegbot/kegboard_kegbot.h"
#include "esphome/components/kegboard_meter/kegboard_meter.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::kegboard_auth {

/// Fires with (device, token) when a token is accepted.
class AuthorizedTrigger : public Trigger<std::string, std::string> {};

/// Fires with (device, token) when a token is presented but refused.
class DeniedTrigger : public Trigger<std::string, std::string> {};

/// Fires when a grant ends, whether by detach or by expiry.
class RevokedTrigger : public Trigger<> {};

/// Decides who may pour.
///
/// Reader components (rdm6300, wiegand, pn532, kegboard_onewire, ...) call the
/// token_attached/token_detached actions; this turns those events into a
/// grant, opens the flow toggle, and attributes pours to the resolved user.
///
/// Authorization is resolved and enforced on the device rather than by the
/// server. That keeps the tap working during an outage, and it means the valve
/// opens at the speed of a local decision rather than a round trip. The cost
/// is that a revoked token stays valid until its cached lookup is retried,
/// which is the right trade for a kegerator.
class KegboardAuth : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_kegbot(kegboard_kegbot::KegbotReporter *kegbot) { this->kegbot_ = kegbot; }
  void set_toggle(switch_::Switch *toggle) { this->toggle_ = toggle; }
  void set_grant_duration_ms(uint32_t ms) { this->session_.set_grant_duration_ms(ms); }
  void set_require_known_token(bool require) { this->require_known_token_ = require; }

  void add_meter(kegboard_meter::KegboardMeter *meter) { this->meters_.push_back(meter); }

  void set_authorized_binary_sensor(binary_sensor::BinarySensor *s) { this->authorized_sensor_ = s; }
  void set_user_text_sensor(text_sensor::TextSensor *s) { this->user_sensor_ = s; }

  void add_on_authorized_trigger(AuthorizedTrigger *t) { this->authorized_triggers_.push_back(t); }
  void add_on_denied_trigger(DeniedTrigger *t) { this->denied_triggers_.push_back(t); }
  void add_on_revoked_trigger(RevokedTrigger *t) { this->revoked_triggers_.push_back(t); }

  /// A reader saw a token.
  void token_attached(const std::string &device, const std::string &token);

  /// A reader saw a token leave. Momentary readers never call this; their
  /// grants end by expiry instead.
  void token_detached(const std::string &token);

  /// Drop any grant immediately, e.g. for a manual lockout.
  void revoke();

  bool is_authorized() const { return this->session_.is_authorized(); }

 protected:
  void grant_(const std::string &device, const std::string &token, const std::string &username);
  void revoke_();
  void publish_state_();

  kbcore::AuthSession session_{kbcore::AuthConfig{}};

  kegboard_kegbot::KegbotReporter *kegbot_{nullptr};
  switch_::Switch *toggle_{nullptr};
  std::vector<kegboard_meter::KegboardMeter *> meters_;

  /// When true (and a server is configured) an unrecognized token is refused.
  /// When false the token still opens the tap, and the pour is recorded
  /// against the guest user -- which is what a party wants.
  bool require_known_token_{false};

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
  TEMPLATABLE_VALUE(std::string, token)

  void play(const Ts &...x) override { this->parent_->token_detached(this->token_.value(x...)); }
};

template<typename... Ts> class RevokeAction : public Action<Ts...>, public Parented<KegboardAuth> {
 public:
  void play(const Ts &...x) override { this->parent_->revoke(); }
};

template<typename... Ts> class AuthorizedCondition : public Condition<Ts...>, public Parented<KegboardAuth> {
 public:
  bool check(const Ts &...x) override { return this->parent_->is_authorized(); }
};

}  // namespace esphome::kegboard_auth
