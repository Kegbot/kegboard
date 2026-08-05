#include "kegboard_auth.h"

#include <algorithm>
#include <cinttypes>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::kegboard_auth {

static const char *const TAG = "kegboard_auth";

static bool contains_u8(const std::vector<uint8_t> &v, uint8_t value) {
  return std::find(v.begin(), v.end(), value) != v.end();
}

void KegboardAuth::setup() {
  if (this->reporter_ != nullptr) {
    this->reporter_->set_command_handler([this](const std::string &type, JsonObjectConst data, std::string &message) {
      return this->handle_command_(type, data, message);
    });
  }

  // The device's meter inventory: gates plus the reporter's meters. Grants
  // are validated against it, and every meter feeds flow accounting.
  for (auto &gate : this->gates_) {
    if (std::find(this->meters_.begin(), this->meters_.end(), gate.meter) == this->meters_.end())
      this->meters_.push_back(gate.meter);
  }
  if (this->reporter_ != nullptr) {
    for (auto *meter : this->reporter_->meter_list()) {
      if (std::find(this->meters_.begin(), this->meters_.end(), meter) == this->meters_.end())
        this->meters_.push_back(meter);
    }
  }

  // True up flow accounting at pour end: loop() feeds deltas while the pour
  // runs, this callback feeds whatever accrued after the last loop. It runs
  // after the reporter's own pour callback, so a volume limit tripping here
  // queues its grant_end after the pour event (protocol §5.7).
  for (auto *meter : this->meters_) {
    meter->add_on_pour_callback([this](kegboard_meter::KegboardMeter &m, const kbcore::PourRecord &record) {
      const uint8_t number = m.meter_number();
      float seen = 0.0f;
      auto it = this->pour_seen_ml_.find(number);
      if (it != this->pour_seen_ml_.end()) {
        seen = it->second;
        it->second = 0.0f;
      }
      const float delta = record.volume_ml - seen;
      if (delta > 0.0f)
        this->process_ends_(this->grants_.record_flow(number, delta, millis()));
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

std::vector<uint8_t> KegboardAuth::all_known_meters_() const {
  std::vector<uint8_t> meters;
  meters.reserve(this->meters_.size());
  for (const auto *meter : this->meters_)
    meters.push_back(meter->meter_number());
  return meters;
}

void KegboardAuth::adopt_in_flight_pour_(kegboard_meter::KegboardMeter *meter) {
  // POLICY — a grant arriving mid-pour ADOPTS the pour (authenticated-pouring
  // §9, case 2): the pour keeps running and, at its end, is attributed to
  // the grant in full — the forgot-to-authenticate-first case. Limit
  // accounting is not retroactive: the baseline below excludes the volume
  // already poured. To change the policy to "split into a new pour"
  // instead, call meter->end_pour() here — everything else stays as is.
  if (meter->is_pouring())
    this->pour_seen_ml_[meter->meter_number()] = meter->session_volume_ml();
}

void KegboardAuth::apply_local_grant_(const std::vector<uint8_t> &meters, const std::string &auth_device,
                                      const std::string &token, bool open_gate_relays) {
  if (meters.empty())
    return;

  kbcore::GrantSpec spec;
  // Internal id: unique per grant so a re-presented token gets a fresh grant
  // (fresh limits), not an update. Never reported: `local` omits grant_id
  // from pour and grant_end events.
  spec.grant_id = "local-" + std::to_string(++this->local_grant_counter_);
  spec.local = true;
  spec.meters = meters;
  spec.auth_device = auth_device;
  spec.token = token;
  spec.max_duration_ms = this->local_grant_duration_ms_;

  this->process_ends_(this->grants_.authorize(spec, millis()));

  for (uint8_t m : meters) {
    auto *meter = this->meter_by_number_(m);
    if (meter != nullptr) {
      meter->set_active_auth("", auth_device, token);
      this->adopt_in_flight_pour_(meter);
    }
    if (open_gate_relays) {
      Gate *gate = this->gate_for_(m);
      if (gate != nullptr && gate->relay != nullptr)
        gate->relay->turn_on();
    }
  }
}

void KegboardAuth::token_attached(const std::string &auth_device, const std::string &token) {
  if (token.empty())
    return;

  if (this->mode_ == AuthMode::LOCAL) {
    // Serverless: every token pours as guest on every gate.
    this->apply_local_grant_(this->all_gate_meters_(), auth_device, token, true);
    ESP_LOGI(TAG, "Local grant for %s/%s", auth_device.c_str(), token.c_str());
    for (auto *trigger : this->authorized_triggers_)
      trigger->trigger(auth_device, token);
    if (this->reporter_ != nullptr)
      this->reporter_->queue_token_event(auth_device, token, true, kbcore::TokenStatus::ACCEPTED);
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
      // Attribution-only, over every meter no active grant already covers:
      // an offline server never results in an opened valve, and a failed
      // presentment never disturbs a live grant (authenticated-pouring §6,
      // §9 case 6).
      std::vector<uint8_t> uncovered;
      for (uint8_t m : this->all_known_meters_()) {
        if (this->grants_.grant_for(m) == nullptr)
          uncovered.push_back(m);
      }
      ESP_LOGW(TAG, "Server unreachable; guest grant (no valves) for %s/%s", auth_device.c_str(), token.c_str());
      this->apply_local_grant_(uncovered, auth_device, token, false);
      for (auto *trigger : this->authorized_triggers_)
        trigger->trigger(auth_device, token);
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
    if (this->mode_ == AuthMode::LOCAL) {
      // The device decides in local mode (authenticated-pouring §7).
      return CommandOutcome::UNSUPPORTED;
    }
    return this->handle_authorize_(data, message);
  }

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

  if (!data["grant_id"].is<const char *>() || data["grant_id"].as<const char *>()[0] == '\0') {
    message = "missing grant_id";
    return CommandOutcome::ERROR;
  }

  kbcore::GrantSpec spec;
  spec.grant_id = data["grant_id"].as<const char *>();

  JsonArrayConst meters_json = data["meter_numbers"].as<JsonArrayConst>();
  if (meters_json.isNull() || meters_json.size() == 0) {
    message = "missing meter_numbers";
    return CommandOutcome::ERROR;
  }
  for (JsonVariantConst v : meters_json) {
    if (!v.is<uint8_t>()) {
      message = "bad meter_numbers";
      return CommandOutcome::ERROR;
    }
    const uint8_t meter = v.as<uint8_t>();
    // A grant naming a meter or relay the device does not have is
    // acknowledged `error` and not applied, in whole (protocol §7.1).
    if (this->meter_by_number_(meter) == nullptr) {
      message = "unknown meter " + std::to_string(meter);
      return CommandOutcome::ERROR;
    }
    if (!contains_u8(spec.meters, meter))
      spec.meters.push_back(meter);
  }

  JsonArrayConst relays_json = data["relay_numbers"].as<JsonArrayConst>();
  if (!relays_json.isNull()) {
    for (JsonVariantConst v : relays_json) {
      if (!v.is<uint8_t>()) {
        message = "bad relay_numbers";
        return CommandOutcome::ERROR;
      }
      const uint8_t relay = v.as<uint8_t>();
      if (!this->reporter_->has_relay(relay)) {
        message = "unknown relay " + std::to_string(relay);
        return CommandOutcome::ERROR;
      }
      if (!contains_u8(spec.relays, relay))
        spec.relays.push_back(relay);
    }
  }

  spec.auth_device = data["auth_device"].is<const char *>() ? data["auth_device"].as<const char *>() : "";
  spec.token = data["token"].is<const char *>() ? data["token"].as<const char *>() : "";
  spec.max_volume_ml = data["max_volume_ml"].is<float>() ? data["max_volume_ml"].as<float>() : 0.0f;
  spec.max_duration_ms = data["max_duration_ms"].is<uint32_t>() ? data["max_duration_ms"].as<uint32_t>() : 0;
  spec.max_idle_ms = data["max_idle_ms"].is<uint32_t>() ? data["max_idle_ms"].as<uint32_t>() : 0;

  // Copy before authorize() mutates the table; relays an update drops must
  // release below (unless another grant still names them).
  const kbcore::Grant *existing = this->grants_.grant_by_id(spec.grant_id);
  const bool update = existing != nullptr;
  const std::vector<uint8_t> prev_relays = update ? existing->spec.relays : std::vector<uint8_t>{};

  this->process_ends_(this->grants_.authorize(spec, millis()));

  for (uint8_t relay : prev_relays) {
    if (!contains_u8(spec.relays, relay) && !this->grants_.covers_relay(relay)) {
      auto *sw = this->reporter_->relay_by_number(relay);
      if (sw != nullptr)
        sw->turn_off();
    }
  }

  // Reentrancy: process_ends_ ends replaced grants' pours, whose callbacks
  // feed flow back through record_flow — which can, in principle, end the
  // just-created grant (a tail delta against a tiny max_volume_ml). Its
  // grant_end is queued already; do not energize relays for a dead grant.
  if (this->grants_.grant_by_id(spec.grant_id) == nullptr) {
    ESP_LOGW(TAG, "Grant %s ended while being applied", spec.grant_id.c_str());
    return CommandOutcome::OK;
  }

  for (uint8_t meter : spec.meters) {
    auto *m = this->meter_by_number_(meter);
    if (m != nullptr) {
      m->set_active_auth(spec.grant_id, spec.auth_device, spec.token);
      this->adopt_in_flight_pour_(m);
    }
  }
  for (uint8_t relay : spec.relays) {
    auto *sw = this->reporter_->relay_by_number(relay);
    if (sw != nullptr)
      sw->turn_on();
  }

  ESP_LOGI(TAG, "%s grant %s: %u meter(s), %u relay(s)", update ? "Updated" : "Applied", spec.grant_id.c_str(),
           static_cast<unsigned>(spec.meters.size()), static_cast<unsigned>(spec.relays.size()));
  for (auto *trigger : this->authorized_triggers_)
    trigger->trigger(spec.auth_device, spec.token);
  this->publish_state_();
  return CommandOutcome::OK;
}

kegboard_reporter::CommandOutcome KegboardAuth::handle_deauthorize_(JsonObjectConst data, std::string &message) {
  using kegboard_reporter::CommandOutcome;

  std::vector<kbcore::GrantEnd> ends;
  JsonVariantConst ids_var = data["grant_ids"];
  if (ids_var.isNull()) {
    // Absent means every active grant: the emergency stop (protocol §7.3).
    ends = this->grants_.deauthorize_all(millis());
  } else if (!ids_var.is<JsonArrayConst>()) {
    // Malformed input must not read as the most destructive interpretation.
    message = "bad grant_ids";
    return CommandOutcome::ERROR;
  } else {
    JsonArrayConst ids_json = ids_var.as<JsonArrayConst>();
    std::vector<std::string> ids;
    for (JsonVariantConst v : ids_json) {
      if (v.is<const char *>())
        ids.emplace_back(v.as<const char *>());
    }
    // Ids matching no active grant are ignored; the grant may have already
    // ended on its own, and the grant_end stream is the record.
    ends = this->grants_.deauthorize(ids, millis());
  }
  ESP_LOGI(TAG, "Deauthorized %u grant(s) by server command", static_cast<unsigned>(ends.size()));
  this->process_ends_(ends);
  return CommandOutcome::OK;
}

void KegboardAuth::token_detached(const std::string &auth_device, const std::string &token) {
  // An empty token must not match the grants issued without a token echo.
  if (token.empty())
    return;
  const auto ends = this->grants_.detach(auth_device, token, millis());
  if (this->reporter_ != nullptr)
    this->reporter_->queue_token_event(auth_device, token, false, kbcore::TokenStatus::NONE);
  if (ends.empty())
    return;
  ESP_LOGI(TAG, "Token removed; ending %u grant(s)", static_cast<unsigned>(ends.size()));
  this->process_ends_(ends);
}

void KegboardAuth::revoke_all() { this->process_ends_(this->grants_.deauthorize_all(millis())); }

void KegboardAuth::process_ends_(const std::vector<kbcore::GrantEnd> &ends) {
  if (ends.empty())
    return;
  for (const auto &end : ends) {
    for (uint8_t m : end.meters) {
      auto *meter = this->meter_by_number_(m);
      if (meter != nullptr) {
        // Settle the flow-accounting baseline first: the departing grant's
        // totals are already snapshotted, and the pour's last unobserved
        // dribble must not be credited to whichever grant covers this
        // meter next.
        if (meter->is_pouring())
          this->pour_seen_ml_[m] = meter->session_volume_ml();
        // End any pour in flight before clearing attribution, so the drink
        // still lands on the departing grant — and its pour event precedes
        // the grant_end in the queue (protocol §5.7).
        meter->end_pour();
        meter->clear_active_auth();
      }
      // Gate relays belong to local mode alone; in server mode they are
      // never touched — grants name their relays explicitly (handled
      // below), and a gate wired in a server-mode config may be driven by
      // something else entirely.
      if (this->mode_ == AuthMode::LOCAL) {
        Gate *gate = this->gate_for_(m);
        if (gate != nullptr && gate->relay != nullptr)
          gate->relay->turn_off();
      }
    }
    for (uint8_t relay : end.relays) {
      // A relay stays energized while any other active grant names it.
      if (this->grants_.covers_relay(relay))
        continue;
      auto *sw = this->reporter_ != nullptr ? this->reporter_->relay_by_number(relay) : nullptr;
      if (sw != nullptr)
        sw->turn_off();
    }
    ESP_LOGI(TAG, "Grant %s ended (%s): %u meter(s)", end.grant_id.empty() ? "(local)" : end.grant_id.c_str(),
             kbcore::grant_end_reason_str(end.reason), static_cast<unsigned>(end.meters.size()));
    if (this->reporter_ != nullptr)
      this->reporter_->queue_grant_end(end);
    for (auto *trigger : this->revoked_triggers_)
      trigger->trigger();
  }
  this->publish_state_();
}

void KegboardAuth::fire_denied_(const std::string &reason) {
  for (auto *trigger : this->denied_triggers_)
    trigger->trigger(reason);
}

void KegboardAuth::loop() {
  if (!this->grants_.any_active()) {
    if (!this->pour_seen_ml_.empty())
      this->pour_seen_ml_.clear();
    return;
  }

  const uint32_t now = millis();

  // A pour that ended without its callback settling the counter (e.g. a
  // discarded drip) must not poison the next pour's deltas.
  for (auto *meter : this->meters_) {
    if (!meter->is_pouring()) {
      auto it = this->pour_seen_ml_.find(meter->meter_number());
      if (it != this->pour_seen_ml_.end())
        it->second = 0.0f;
    }
  }

  // Live flow accounting: deltas reset the idle clock and accrue toward
  // max_volume_ml, so the valve closes the moment a limit trips, not at
  // pour end (protocol §7.1).
  for (uint8_t m : this->grants_.active_meters()) {
    auto *meter = this->meter_by_number_(m);
    if (meter == nullptr || !meter->is_pouring())
      continue;
    float &seen = this->pour_seen_ml_[m];
    const float volume = meter->session_volume_ml();
    if (volume > seen) {
      const float delta = volume - seen;
      seen = volume;
      this->process_ends_(this->grants_.record_flow(m, delta, now));
    }
  }

  this->process_ends_(this->grants_.poll(now));
}

void KegboardAuth::publish_state_() {
  if (this->authorized_sensor_ != nullptr)
    this->authorized_sensor_->publish_state(this->grants_.any_active());
}

void KegboardAuth::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegboard Auth:");
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->mode_ == AuthMode::SERVER ? "server" : "local");
  if (this->mode_ == AuthMode::SERVER) {
    ESP_LOGCONFIG(TAG, "  Offline policy: %s",
                  this->offline_policy_ == OfflinePolicy::DENY ? "deny" : "guest (no valves)");
  }
  ESP_LOGCONFIG(TAG, "  Max grant duration: %" PRIu32 " s", this->grants_.max_duration_ms() / 1000);
  ESP_LOGCONFIG(TAG, "  Gates (local mode): %u", static_cast<unsigned>(this->gates_.size()));
  ESP_LOGCONFIG(TAG, "  Meters: %u", static_cast<unsigned>(this->meters_.size()));
}

}  // namespace esphome::kegboard_auth
