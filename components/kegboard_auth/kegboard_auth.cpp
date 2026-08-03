#include "kegboard_auth.h"

#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::kegboard_auth {

static const char *const TAG = "kegboard_auth";

void KegboardAuth::setup() {
  if (this->reporter_ != nullptr) {
    this->reporter_->set_command_handler([this](const std::string &type, JsonObjectConst data, std::string &message) {
      return this->handle_command_(type, data, message);
    });
  }
  this->publish_state_();
}

KegboardAuth::Gate *KegboardAuth::gate_for_(uint8_t meter) {
  for (auto &gate : this->gates_) {
    if (gate.meter->meter_number() == meter)
      return &gate;
  }
  return nullptr;
}

std::vector<uint8_t> KegboardAuth::all_gate_meters_() const {
  std::vector<uint8_t> meters;
  meters.reserve(this->gates_.size());
  for (const auto &gate : this->gates_)
    meters.push_back(gate.meter->meter_number());
  return meters;
}

void KegboardAuth::token_attached(const std::string &auth_device, const std::string &token) {
  if (token.empty())
    return;

  if (this->mode_ == AuthMode::LOCAL) {
    // Serverless: every token pours as guest on every gate.
    this->grants_.authorize(this->all_gate_meters_(), "", auth_device, token, this->local_grant_duration_ms_, millis());
    this->apply_meters_(this->all_gate_meters_(), true);
    ESP_LOGI(TAG, "Local grant for %s/%s", auth_device.c_str(), token.c_str());
    for (auto *trigger : this->authorized_triggers_)
      trigger->trigger("");
    if (this->reporter_ != nullptr)
      this->reporter_->queue_token_event(auth_device, token, true, kbcore::TokenStatus::ACCEPTED, "");
    this->publish_state_();
    return;
  }

  // Server mode: the decision rides the response to the token event. The
  // command handler runs inside send_token_ask() and sets decision_received_.
  if (this->reporter_ == nullptr) {
    ESP_LOGW(TAG, "Server mode with no reporter; denying");
    this->fire_denied_("no reporter configured");
    return;
  }

  this->decision_received_ = false;
  const bool delivered = this->reporter_->send_token_ask(auth_device, token);

  if (!delivered) {
    if (this->offline_policy_ == OfflinePolicy::GUEST) {
      // Attribution only: an offline server never results in an opened valve
      // (authenticated-pouring §5).
      ESP_LOGW(TAG, "Server unreachable; guest grant (no valves) for %s/%s", auth_device.c_str(), token.c_str());
      this->grants_.authorize(this->all_gate_meters_(), "", auth_device, token, this->local_grant_duration_ms_,
                              millis());
      this->apply_meters_(this->all_gate_meters_(), false);
      for (auto *trigger : this->authorized_triggers_)
        trigger->trigger("");
      this->publish_state_();
    } else {
      ESP_LOGW(TAG, "Server unreachable; denying %s/%s", auth_device.c_str(), token.c_str());
      this->fire_denied_("server unreachable");
    }
    return;
  }

  if (!this->decision_received_) {
    // Delivered but the server answered with neither authorize nor deny --
    // a server bug, defensively treated as denial without the user signal.
    ESP_LOGW(TAG, "Server did not decide on %s/%s; treating as denied", auth_device.c_str(), token.c_str());
  }
}

kegboard_reporter::CommandOutcome KegboardAuth::handle_command_(const std::string &type, JsonObjectConst data,
                                                                std::string &message) {
  using kegboard_reporter::CommandOutcome;

  if (type == "authorize") {
    JsonArrayConst meters_json = data["meters"].as<JsonArrayConst>();
    if (meters_json.isNull()) {
      message = "missing meters";
      return CommandOutcome::ERROR;
    }
    if (!data["duration_ms"].is<uint32_t>()) {
      message = "missing duration_ms";
      return CommandOutcome::ERROR;
    }

    std::vector<uint8_t> meters;
    for (JsonVariantConst v : meters_json) {
      if (v.is<uint8_t>())
        meters.push_back(v.as<uint8_t>());
    }

    const std::string user = data["user"].is<const char *>() ? data["user"].as<const char *>() : "";
    const std::string auth_device =
        data["auth_device"].is<const char *>() ? data["auth_device"].as<const char *>() : "";
    const std::string token = data["token"].is<const char *>() ? data["token"].as<const char *>() : "";

    this->decision_received_ = true;
    this->grants_.authorize(meters, user, auth_device, token, data["duration_ms"].as<uint32_t>(), millis());
    this->apply_meters_(meters, true);

    ESP_LOGI(TAG, "Authorized %s on %u meter(s)", user.empty() ? "guest" : user.c_str(),
             static_cast<unsigned>(meters.size()));
    for (auto *trigger : this->authorized_triggers_)
      trigger->trigger(user);
    this->publish_state_();
    return CommandOutcome::OK;
  }

  if (type == "deny") {
    this->decision_received_ = true;
    const std::string reason = data["reason"].is<const char *>() ? data["reason"].as<const char *>() : "";
    ESP_LOGW(TAG, "Denied by server%s%s", reason.empty() ? "" : ": ", reason.c_str());
    this->fire_denied_(reason);
    return CommandOutcome::OK;
  }

  if (type == "deauthorize") {
    JsonArrayConst meters_json = data["meters"].as<JsonArrayConst>();
    std::vector<uint8_t> revoked;
    if (meters_json.isNull()) {
      revoked = this->grants_.deauthorize_all();
    } else {
      std::vector<uint8_t> meters;
      for (JsonVariantConst v : meters_json) {
        if (v.is<uint8_t>())
          meters.push_back(v.as<uint8_t>());
      }
      revoked = this->grants_.deauthorize(meters);
    }
    ESP_LOGI(TAG, "Deauthorized %u meter(s) by server command", static_cast<unsigned>(revoked.size()));
    this->revoke_meters_(revoked);
    return CommandOutcome::OK;
  }

  return CommandOutcome::UNSUPPORTED;
}

void KegboardAuth::token_detached(const std::string &auth_device, const std::string &token) {
  const auto revoked = this->grants_.detach(token);
  if (this->reporter_ != nullptr)
    this->reporter_->queue_token_event(auth_device, token, false, kbcore::TokenStatus::NONE, "");
  if (revoked.empty())
    return;
  ESP_LOGI(TAG, "Token removed; revoking %u meter(s)", static_cast<unsigned>(revoked.size()));
  this->revoke_meters_(revoked);
}

void KegboardAuth::revoke_all() { this->revoke_meters_(this->grants_.deauthorize_all()); }

void KegboardAuth::apply_meters_(const std::vector<uint8_t> &meters, bool open_toggles) {
  for (uint8_t meter : meters) {
    Gate *gate = this->gate_for_(meter);
    if (gate == nullptr) {
      ESP_LOGW(TAG, "Grant names meter %u, which has no gate here", meter);
      continue;
    }
    const kbcore::Grant *grant = this->grants_.grant_for(meter);
    if (grant == nullptr)
      continue;
    gate->meter->set_active_auth(grant->user, grant->auth_device, grant->token);
    if (open_toggles && gate->toggle != nullptr)
      gate->toggle->turn_on();
  }
}

void KegboardAuth::revoke_meters_(const std::vector<uint8_t> &meters) {
  if (meters.empty())
    return;
  for (uint8_t meter : meters) {
    Gate *gate = this->gate_for_(meter);
    if (gate == nullptr)
      continue;
    if (gate->toggle != nullptr)
      gate->toggle->turn_off();
    // End any pour in flight before clearing attribution, so the drink still
    // lands on whoever poured it.
    gate->meter->end_pour();
    gate->meter->clear_active_auth();
  }
  for (auto *trigger : this->revoked_triggers_)
    trigger->trigger();
  this->publish_state_();
}

void KegboardAuth::fire_denied_(const std::string &reason) {
  for (auto *trigger : this->denied_triggers_)
    trigger->trigger(reason);
}

void KegboardAuth::loop() {
  if (!this->grants_.any_active())
    return;

  const uint32_t now = millis();

  // An active pour holds its meter's grant open; a slow glass is never cut
  // off mid-pour.
  for (auto &gate : this->gates_) {
    if (gate.meter->is_pouring() && this->grants_.grant_for(gate.meter->meter_number()) != nullptr)
      this->grants_.extend(gate.meter->meter_number(), now);
  }

  const auto expired = this->grants_.poll(now);
  if (!expired.empty()) {
    ESP_LOGI(TAG, "Grant expired on %u meter(s)", static_cast<unsigned>(expired.size()));
    this->revoke_meters_(expired);
  }
}

void KegboardAuth::publish_state_() {
  if (this->authorized_sensor_ != nullptr)
    this->authorized_sensor_->publish_state(this->grants_.any_active());

  if (this->user_sensor_ != nullptr) {
    // Shows the user of the first active grant; multi-user setups get better
    // detail from per-meter entities later.
    std::string user;
    for (const auto &gate : this->gates_) {
      const kbcore::Grant *grant = this->grants_.grant_for(gate.meter->meter_number());
      if (grant != nullptr) {
        user = grant->user.empty() ? "guest" : grant->user;
        break;
      }
    }
    this->user_sensor_->publish_state(user);
  }
}

void KegboardAuth::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegboard Auth:");
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->mode_ == AuthMode::SERVER ? "server" : "local");
  if (this->mode_ == AuthMode::SERVER) {
    ESP_LOGCONFIG(TAG, "  Offline policy: %s",
                  this->offline_policy_ == OfflinePolicy::DENY ? "deny" : "guest (no valves)");
  }
  ESP_LOGCONFIG(TAG, "  Max grant duration: %" PRIu32 " s", this->grants_.max_duration_ms() / 1000);
  ESP_LOGCONFIG(TAG, "  Gates: %u", static_cast<unsigned>(this->gates_.size()));
}

}  // namespace esphome::kegboard_auth
