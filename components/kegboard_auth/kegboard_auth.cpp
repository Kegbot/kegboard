#include "kegboard_auth.h"

#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::kegboard_auth {

static const char *const TAG = "kegboard_auth";

void KegboardAuth::setup() { this->publish_state_(); }

void KegboardAuth::token_attached(const std::string &device, const std::string &token) {
  if (token.empty())
    return;

  // Re-presenting the token that already holds the grant just extends it. No
  // lookup, no log spam, no re-triggering automations -- which matters because
  // a held iButton produces a steady stream of these.
  if (this->session_.is_authorized() && this->session_.token() == token) {
    this->session_.extend(millis());
    return;
  }

  std::string username;
  bool known = false;
  if (this->kegbot_ != nullptr)
    known = this->kegbot_->lookup_token(device, token, &username);

  if (this->kegbot_ != nullptr && !known && this->require_known_token_) {
    ESP_LOGW(TAG, "Denied %s/%s", device.c_str(), token.c_str());
    for (auto *trigger : this->denied_triggers_)
      trigger->trigger(device, token);
    return;
  }

  this->grant_(device, token, username);
}

void KegboardAuth::grant_(const std::string &device, const std::string &token, const std::string &username) {
  this->session_.attach(device, token, millis());
  this->session_.set_username(username);

  ESP_LOGI(TAG, "Authorized %s/%s as %s", device.c_str(), token.c_str(), username.empty() ? "guest" : username.c_str());

  for (auto *meter : this->meters_)
    meter->set_active_username(username);

  if (this->toggle_ != nullptr)
    this->toggle_->turn_on();

  for (auto *trigger : this->authorized_triggers_)
    trigger->trigger(device, token);

  this->publish_state_();
}

void KegboardAuth::token_detached(const std::string &token) {
  if (!this->session_.detach(token))
    return;
  ESP_LOGI(TAG, "Token removed");
  this->revoke_();
}

void KegboardAuth::revoke() {
  if (!this->session_.revoke())
    return;
  this->revoke_();
}

void KegboardAuth::revoke_() {
  if (this->toggle_ != nullptr)
    this->toggle_->turn_off();

  // End any pour in flight before clearing the username, so the drink is
  // still attributed to whoever poured it rather than to the guest user.
  for (auto *meter : this->meters_) {
    meter->end_pour();
    meter->set_active_username("");
  }

  for (auto *trigger : this->revoked_triggers_)
    trigger->trigger();

  this->publish_state_();
}

void KegboardAuth::loop() {
  if (!this->session_.is_authorized())
    return;

  // An active pour holds the grant open. Otherwise a glass that takes longer
  // than grant_duration to fill would have the valve shut under it.
  for (auto *meter : this->meters_) {
    if (meter->is_pouring()) {
      this->session_.extend(millis());
      return;
    }
  }

  if (this->session_.poll(millis())) {
    ESP_LOGI(TAG, "Authorization expired");
    this->revoke_();
  }
}

void KegboardAuth::publish_state_() {
  if (this->authorized_sensor_ != nullptr)
    this->authorized_sensor_->publish_state(this->session_.is_authorized());

  if (this->user_sensor_ != nullptr) {
    if (!this->session_.is_authorized()) {
      this->user_sensor_->publish_state("");
    } else {
      this->user_sensor_->publish_state(this->session_.username().empty() ? "guest" : this->session_.username());
    }
  }
}

void KegboardAuth::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegboard Auth:");
  ESP_LOGCONFIG(TAG, "  Grant duration: %" PRIu32 " s", this->session_.config().grant_duration_ms / 1000);
  ESP_LOGCONFIG(TAG, "  Gated meters: %u", static_cast<unsigned>(this->meters_.size()));
  ESP_LOGCONFIG(TAG, "  Unknown tokens: %s", this->require_known_token_ ? "denied" : "allowed as guest");
  if (this->kegbot_ == nullptr) {
    ESP_LOGCONFIG(TAG, "  No Kegbot server; every token pours as guest");
  }
}

}  // namespace esphome::kegboard_auth
