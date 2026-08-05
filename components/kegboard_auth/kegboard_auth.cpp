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
    this->meters_ = this->reporter_->meter_list();
  }

  kbcore::AuthDevice device;
  device.has_meter = [this](uint8_t meter) { return this->meter_by_number_(meter) != nullptr; };
  device.has_relay = [this](uint8_t relay) { return this->reporter_ != nullptr && this->reporter_->has_relay(relay); };
  device.is_pouring = [this](uint8_t meter) {
    auto *m = this->meter_by_number_(meter);
    return m != nullptr && m->is_pouring();
  };
  device.session_volume_ml = [this](uint8_t meter) {
    auto *m = this->meter_by_number_(meter);
    return m != nullptr ? m->session_volume_ml() : 0.0f;
  };
  device.end_pour = [this](uint8_t meter) {
    auto *m = this->meter_by_number_(meter);
    if (m != nullptr)
      m->end_pour();
  };
  device.set_attribution = [this](uint8_t meter, const kbcore::GrantSpec &spec) {
    auto *m = this->meter_by_number_(meter);
    if (m != nullptr)
      m->set_active_auth(spec.grant_id, spec.auth_device, spec.token);
  };
  device.clear_attribution = [this](uint8_t meter) {
    auto *m = this->meter_by_number_(meter);
    if (m != nullptr)
      m->clear_active_auth();
  };
  device.set_relay = [this](uint8_t relay, bool on) {
    auto *sw = this->reporter_ != nullptr ? this->reporter_->relay_by_number(relay) : nullptr;
    if (sw == nullptr)
      return;
    if (on) {
      sw->turn_on();
    } else {
      sw->turn_off();
    }
  };
  device.emit_grant_end = [this](const kbcore::GrantEnd &end) {
    ESP_LOGI(TAG, "Grant %s ended (%s): %u meter(s)", end.grant_id.c_str(), kbcore::grant_end_reason_str(end.reason),
             static_cast<unsigned>(end.meters.size()));
    if (this->reporter_ != nullptr)
      this->reporter_->queue_grant_end(end);
    for (auto *trigger : this->revoked_triggers_)
      trigger->trigger();
  };

  this->engine_ = std::make_unique<kbcore::AuthEngine>(std::move(device));
  this->engine_->set_max_grant_duration_ms(this->max_grant_duration_ms_);

  // True up flow accounting at pour end. This callback registers after the
  // reporter's (which is added at construction time), so a volume limit
  // tripping here queues its grant_end after the pour event.
  for (auto *meter : this->meters_) {
    meter->add_on_pour_callback([this](kegboard_meter::KegboardMeter &m, const kbcore::PourRecord &record) {
      this->engine_->on_pour_end(m.meter_number(), record.volume_ml, millis());
    });
  }

  this->publish_state_();
}

kegboard_meter::KegboardMeter *KegboardAuth::meter_by_number_(uint8_t meter) {
  for (auto *m : this->meters_) {
    if (m->meter_number() == meter)
      return m;
  }
  return nullptr;
}

void KegboardAuth::token_attached(const std::string &auth_device, const std::string &token) {
  if (token.empty())
    return;

  // The decision rides the response to the token event. The command handler
  // runs inside send_token_ask() and sets decision_received_.
  if (this->reporter_ == nullptr) {
    ESP_LOGW(TAG, "No reporter; denying");
    this->fire_denied_("no reporter configured");
    return;
  }

  this->decision_received_ = false;
  const bool delivered = this->reporter_->send_token_ask(auth_device, token);

  if (!delivered) {
    if (this->offline_policy_ == OfflinePolicy::GUEST) {
      // Nothing opens and nothing is granted: pours proceed as ordinary
      // guest pours, and the queued token event preserves the audit trail.
      // The only difference from `deny` is that the user is not signaled a
      // refusal.
      ESP_LOGW(TAG, "Server unreachable; %s/%s pours as guest", auth_device.c_str(), token.c_str());
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

  if (type == "authorize")
    return this->handle_authorize_(data, message);

  if (type == "deny") {
    this->decision_received_ = true;
    const std::string reason = data["reason"].is<const char *>() ? data["reason"].as<const char *>() : "";
    ESP_LOGW(TAG, "Denied by server%s%s", reason.empty() ? "" : ": ", reason.c_str());
    this->fire_denied_(reason);
    return CommandOutcome::OK;
  }

  if (type == "deauthorize")
    return this->handle_deauthorize_(data, message);

  return CommandOutcome::UNSUPPORTED;
}

kegboard_reporter::CommandOutcome KegboardAuth::handle_authorize_(JsonObjectConst data, std::string &message) {
  using kegboard_reporter::CommandOutcome;

  // Whatever happens below, the server did decide; a malformed grant must
  // not read as "no decision" (which would double-signal a denial).
  this->decision_received_ = true;

  kbcore::GrantSpec spec;
  if (data["grant_id"].is<const char *>())
    spec.grant_id = data["grant_id"].as<const char *>();

  JsonArrayConst meters_json = data["meter_numbers"].as<JsonArrayConst>();
  if (!meters_json.isNull()) {
    for (JsonVariantConst v : meters_json) {
      if (!v.is<uint8_t>()) {
        message = "bad meter_numbers";
        return CommandOutcome::ERROR;
      }
      spec.meters.push_back(v.as<uint8_t>());
    }
  }
  JsonArrayConst relays_json = data["relay_numbers"].as<JsonArrayConst>();
  if (!relays_json.isNull()) {
    for (JsonVariantConst v : relays_json) {
      if (!v.is<uint8_t>()) {
        message = "bad relay_numbers";
        return CommandOutcome::ERROR;
      }
      spec.relays.push_back(v.as<uint8_t>());
    }
  }

  spec.auth_device = data["auth_device"].is<const char *>() ? data["auth_device"].as<const char *>() : "";
  spec.token = data["token"].is<const char *>() ? data["token"].as<const char *>() : "";
  spec.max_volume_ml = data["max_volume_ml"].is<float>() ? data["max_volume_ml"].as<float>() : 0.0f;
  spec.max_duration_ms = data["max_duration_ms"].is<uint32_t>() ? data["max_duration_ms"].as<uint32_t>() : 0;
  spec.max_idle_ms = data["max_idle_ms"].is<uint32_t>() ? data["max_idle_ms"].as<uint32_t>() : 0;

  const auto outcome = this->engine_->authorize(spec, millis());
  if (!outcome.ok) {
    message = outcome.message;
    return CommandOutcome::ERROR;
  }
  if (!outcome.message.empty()) {
    ESP_LOGW(TAG, "Grant %s: %s", spec.grant_id.c_str(), outcome.message.c_str());
  } else {
    ESP_LOGI(TAG, "%s grant %s: %u meter(s), %u relay(s)", outcome.updated ? "Updated" : "Applied",
             spec.grant_id.c_str(), static_cast<unsigned>(spec.meters.size()),
             static_cast<unsigned>(spec.relays.size()));
  }
  for (auto *trigger : this->authorized_triggers_)
    trigger->trigger(spec.auth_device, spec.token);
  this->publish_state_();
  return CommandOutcome::OK;
}

kegboard_reporter::CommandOutcome KegboardAuth::handle_deauthorize_(JsonObjectConst data, std::string &message) {
  using kegboard_reporter::CommandOutcome;

  size_t count = 0;
  JsonVariantConst ids_var = data["grant_ids"];
  if (ids_var.isNull()) {
    // Absent means every active grant: the emergency stop.
    count = this->engine_->deauthorize({}, true, millis());
  } else if (!ids_var.is<JsonArrayConst>()) {
    // Malformed input must not read as the most destructive interpretation.
    message = "bad grant_ids";
    return CommandOutcome::ERROR;
  } else {
    std::vector<std::string> ids;
    for (JsonVariantConst v : ids_var.as<JsonArrayConst>()) {
      if (v.is<const char *>())
        ids.emplace_back(v.as<const char *>());
    }
    count = this->engine_->deauthorize(ids, false, millis());
  }
  ESP_LOGI(TAG, "Deauthorized %u grant(s) by server command", static_cast<unsigned>(count));
  this->publish_state_();
  return CommandOutcome::OK;
}

void KegboardAuth::token_detached(const std::string &auth_device, const std::string &token) {
  // An empty token must not match the grants issued without a token echo.
  if (token.empty())
    return;
  // The token event first, so the wire order is detach, then any ended
  // grant's pour, then its grant_end.
  if (this->reporter_ != nullptr)
    this->reporter_->queue_token_event(auth_device, token, false);
  const size_t count = this->engine_->detach(auth_device, token, millis());
  if (count == 0)
    return;
  ESP_LOGI(TAG, "Token removed; ended %u grant(s)", static_cast<unsigned>(count));
  this->publish_state_();
}

void KegboardAuth::revoke_all() {
  this->engine_->revoke_all(millis());
  this->publish_state_();
}

void KegboardAuth::loop() {
  this->engine_->poll(millis());
  // Cheap: publish_state_ deduplicates via the sensor itself, but avoid
  // the call entirely in the common idle case.
  if (this->authorized_sensor_ != nullptr && this->authorized_sensor_->state != this->engine_->any_active())
    this->publish_state_();
}

void KegboardAuth::publish_state_() {
  if (this->authorized_sensor_ != nullptr && this->engine_ != nullptr)
    this->authorized_sensor_->publish_state(this->engine_->any_active());
}

void KegboardAuth::fire_denied_(const std::string &reason) {
  for (auto *trigger : this->denied_triggers_)
    trigger->trigger(reason);
}

void KegboardAuth::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegboard Auth:");
  ESP_LOGCONFIG(TAG, "  Offline policy: %s", this->offline_policy_ == OfflinePolicy::DENY ? "deny" : "guest");
  ESP_LOGCONFIG(TAG, "  Max grant duration: %" PRIu32 " s", this->max_grant_duration_ms_ / 1000);
  ESP_LOGCONFIG(TAG, "  Meters: %u", static_cast<unsigned>(this->meters_.size()));
}

}  // namespace esphome::kegboard_auth
