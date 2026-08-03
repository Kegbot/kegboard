#include "kegboard_reporter.h"

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <ctime>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome::kegboard_reporter {

static const char *const TAG = "kegboard_reporter";

static constexpr uint32_t MAX_RETRY_INTERVAL_MS = 300000;
/// Fast pairing poll for the first minute after boot, then heartbeat cadence.
static constexpr uint32_t PAIRING_FAST_POLL_MS = 5000;
static constexpr uint32_t PAIRING_FAST_WINDOW_MS = 60000;
static constexpr size_t COMMAND_DEDUP_WINDOW = 8;

/// Fixed-size flash slot for the provisioned bearer token.
struct TokenStore {
  char token[96];
};

void KegboardReporter::setup() {
  this->boot_id_ = kbcore::format_boot_id(random_uint32());

  // Give the rest of the firmware a wall clock without making `time` a
  // dependency of every component. See KegboardHub::set_clock_source().
  if (this->hub_ != nullptr && this->time_ != nullptr) {
    auto *clock = this->time_;
    this->hub_->set_clock_source([clock]() -> uint32_t {
      auto now = clock->now();
      return now.is_valid() ? static_cast<uint32_t>(now.timestamp) : 0;
    });
  }

  this->load_token_();

  this->pairing_started_ms_ = millis();
  this->next_heartbeat_ms_ = millis() + this->heartbeat_ms_;
  this->enqueue_status_(true);
  this->publish_diagnostics_();
}

void KegboardReporter::load_token_() {
  this->token_pref_ = global_preferences->make_preference<TokenStore>(fnv1_hash("kegboard_bearer_token"));
  TokenStore store{};
  if (this->token_pref_.load(&store)) {
    store.token[sizeof(store.token) - 1] = '\0';
    this->bearer_token_ = store.token;
    if (!this->bearer_token_.empty())
      ESP_LOGI(TAG, "Loaded provisioned token from flash");
  }
}

void KegboardReporter::save_token_(const std::string &token) {
  TokenStore store{};
  if (token.size() >= sizeof(store.token)) {
    ESP_LOGE(TAG, "Provisioned token too long (%u bytes); not saving", static_cast<unsigned>(token.size()));
    return;
  }
  memcpy(store.token, token.c_str(), token.size() + 1);
  this->token_pref_.save(&store);
  global_preferences->sync();
}

kbcore::Event KegboardReporter::make_event_(const char *type, std::string data_json) {
  kbcore::Event e;
  e.id = this->next_event_id_++;
  e.type = type;
  e.created_ms = millis();
  e.time = this->rfc3339_now_();
  e.data_json = std::move(data_json);
  return e;
}

std::string KegboardReporter::rfc3339_now_() {
  const uint32_t epoch = this->hub_ != nullptr ? this->hub_->now_unix() : 0;
  if (epoch == 0)
    return "";
  time_t t = epoch;
  struct tm tm_utc;
  gmtime_r(&t, &tm_utc);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return std::string(buf);
}

void KegboardReporter::enqueue_(kbcore::Event &&event, bool reset_backoff) {
  if (!this->queue_.push(event))
    ESP_LOGW(TAG, "Event queue full; dropped the oldest event (%" PRIu32 " total)", this->queue_.dropped());
  // A pour or token event is worth an immediate attempt even mid-backoff;
  // status heartbeats and command acks wait their turn.
  if (reset_backoff)
    this->next_attempt_ms_ = millis();
  this->publish_diagnostics_();
}

void KegboardReporter::add_meter(kegboard_meter::KegboardMeter *meter) {
  this->meters_.push_back(MeterState{meter, 0, ""});
  meter->add_on_pour_callback([this](kegboard_meter::KegboardMeter &m, const kbcore::PourRecord &record) {
    kbcore::PourData d;
    d.meter = m.meter_number();
    d.pour_id = m.pour_id();
    d.volume_ml = record.volume_ml;
    d.duration_ms = record.duration_ms;
    d.user = m.active_user();
    d.auth_device = m.active_auth_device();
    d.auth_token = m.active_auth_token();
    d.ticks = record.ticks;
    d.ml_per_tick = m.ml_per_tick();
    d.tick_series = record.series.to_string();
    this->enqueue_(this->make_event_("pour", kbcore::pour_data_json(d)));
  });
}

void KegboardReporter::add_thermo_sensor(sensor::Sensor *sensor, const std::string &name) {
  sensor->add_on_state_callback([this, name](float value) {
    if (std::isnan(value))
      return;
    this->enqueue_(this->make_event_("temperature", kbcore::temperature_data_json(name, value)));
  });
}

bool KegboardReporter::send_token_ask(const std::string &auth_device, const std::string &token) {
  this->enqueue_(
      this->make_event_("token", kbcore::token_data_json(auth_device, token, true, kbcore::TokenStatus::NONE, "")));
  // The authorization decision rides the response to this send; commands are
  // dispatched inside send_batch_() before it returns.
  return this->send_batch_({});
}

void KegboardReporter::queue_token_event(const std::string &auth_device, const std::string &token, bool attached,
                                         kbcore::TokenStatus status, const std::string &user) {
  this->enqueue_(this->make_event_("token", kbcore::token_data_json(auth_device, token, attached, status, user)));
}

void KegboardReporter::enqueue_status_(bool boot) {
  kbcore::StatusData d;
  d.boot = boot;
  d.fw_version = this->hub_ != nullptr ? this->hub_->version() : "unknown";
  d.uptime_ms = millis();
  d.events_dropped = this->queue_.dropped() + this->extra_dropped_;
  d.heartbeat_ms = this->heartbeat_ms_;
  d.pour_update_ms = this->pour_update_ms_;
  d.queue_capacity = this->queue_.capacity();
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    d.has_rssi = true;
    d.rssi_dbm = wifi::global_wifi_component->wifi_rssi();
  }
#endif
  for (const auto &state : this->meters_) {
    kbcore::StatusMeter m;
    m.meter = state.meter->meter_number();
    m.total_ticks = state.meter->total_ticks();
    m.ml_per_tick = state.meter->ml_per_tick();
    d.meters.push_back(m);
  }
  this->enqueue_(this->make_event_("status", kbcore::status_data_json(d)), false);
}

void KegboardReporter::collect_pour_updates_(std::vector<kbcore::Event> &out) {
  if (this->pour_update_ms_ == 0 || !this->healthy_())
    return;

  const uint32_t now = millis();
  for (auto &state : this->meters_) {
    if (!state.meter->is_pouring())
      continue;
    const std::string &pour_id = state.meter->pour_id();
    const bool new_pour = pour_id != state.last_update_pour_id;
    if (!new_pour && (now - state.last_update_ms) < this->pour_update_ms_)
      continue;

    state.last_update_ms = now;
    state.last_update_pour_id = pour_id;

    out.push_back(this->make_event_(
        "pour_update",
        kbcore::pour_update_data_json(state.meter->meter_number(), pour_id, state.meter->session_volume_ml(),
                                      state.meter->session_duration_ms(now))));
  }
}

void KegboardReporter::loop() {
  if (this->denied_ || this->reporting_url_.empty() || this->http_ == nullptr)
    return;

  const uint32_t now = millis();

  if (static_cast<int32_t>(now - this->next_heartbeat_ms_) >= 0) {
    this->next_heartbeat_ms_ = now + this->heartbeat_ms_;
    this->enqueue_status_(false);
  }

  std::vector<kbcore::Event> updates;
  this->collect_pour_updates_(updates);

  const bool due = static_cast<int32_t>(now - this->next_attempt_ms_) >= 0;
  if (!updates.empty() || (due && !this->queue_.empty())) {
    this->send_batch_(std::move(updates));
    this->publish_diagnostics_();
  }
}

bool KegboardReporter::send_batch_(std::vector<kbcore::Event> &&ephemeral) {
  if (this->denied_ || this->reporting_url_.empty() || this->http_ == nullptr)
    return false;
  // Queued events first (oldest-first), then ephemeral updates in whatever
  // room remains. Ephemeral events are never queued: if this send fails they
  // are simply gone, per protocol.
  std::vector<const kbcore::Event *> batch;
  size_t queued_in_batch = 0;
  for (size_t i = 0; i < this->queue_.size() && batch.size() < kbcore::MAX_BATCH_EVENTS; i++) {
    batch.push_back(this->queue_.at(i));
    queued_in_batch++;
  }
  for (const auto &e : ephemeral) {
    if (batch.size() >= kbcore::MAX_BATCH_EVENTS)
      break;
    batch.push_back(&e);
  }
  if (batch.empty())
    return true;

  const std::string body = kbcore::serialize_batch(this->hub_ != nullptr ? this->hub_->serial_number() : "kegboard",
                                                   this->boot_id_, millis(), batch);

  ESP_LOGVV(TAG, "POST %s\n%s", this->reporting_url_.c_str(), body.c_str());

  std::vector<http_request::Header> headers;
  headers.push_back({"Content-Type", "application/json"});
  if (this->is_paired())
    headers.push_back({"Authorization", "Bearer " + this->bearer_token_});

  auto container = this->http_->post(this->reporting_url_, body, headers);
  if (container == nullptr) {
    ESP_LOGW(TAG, "POST failed: no response");
    this->bump_backoff_();
    return false;
  }

  const int status = container->status_code;
  std::string response;
  response.resize(container->content_length);
  if (!response.empty()) {
    const auto read = http_request::http_read_fully(container.get(), reinterpret_cast<uint8_t *>(&response[0]),
                                                    response.size(), 512, this->http_->get_timeout());
    if (read.status != http_request::HttpReadStatus::OK)
      response.clear();
  }
  container->end();

  // Success is otherwise silent; failures get their own warnings below.
  ESP_LOGD(TAG, "POST -> %d (%u events, %u bytes)", status, static_cast<unsigned>(batch.size()),
           static_cast<unsigned>(body.size()));
  if (!response.empty())
    ESP_LOGVV(TAG, "Response: %s", response.c_str());

  this->handle_response_(status, response, queued_in_batch);
  return http_request::is_success(status);
}

void KegboardReporter::handle_response_(int status, const std::string &body, size_t queued_in_batch) {
  if (http_request::is_success(status)) {
    for (size_t i = 0; i < queued_in_batch; i++)
      this->queue_.pop();
    this->consecutive_failures_ = 0;
    this->next_attempt_ms_ = millis();
    this->dispatch_commands_(body);
    return;
  }

  if (status == 401) {
    // Includes a revoked or rotated token: drop it and re-enter pairing.
    if (this->is_paired()) {
      ESP_LOGW(TAG, "Token rejected; re-entering pairing");
      this->bearer_token_.clear();
      this->save_token_("");
      this->pairing_started_ms_ = millis();
    }
    this->handle_pairing_(body);
    return;
  }

  if (status >= 400 && status < 500) {
    // The batch can never succeed; retrying cannot help.
    ESP_LOGE(TAG, "Server rejected batch (%d); dropping %u events", status, static_cast<unsigned>(queued_in_batch));
    for (size_t i = 0; i < queued_in_batch; i++)
      this->queue_.pop();
    this->extra_dropped_ += queued_in_batch;
    this->next_attempt_ms_ = millis();
    return;
  }

  ESP_LOGW(TAG, "Delivery failed (%d)", status);
  this->bump_backoff_();
}

void KegboardReporter::bump_backoff_() {
  this->consecutive_failures_++;
  uint32_t delay_ms = this->retry_interval_ms_;
  for (uint32_t i = 1; i < this->consecutive_failures_ && delay_ms < MAX_RETRY_INTERVAL_MS; i++)
    delay_ms *= 2;
  if (delay_ms > MAX_RETRY_INTERVAL_MS)
    delay_ms = MAX_RETRY_INTERVAL_MS;
  this->next_attempt_ms_ = millis() + delay_ms;
  ESP_LOGW(TAG, "Retrying in %" PRIu32 " s (%" PRIu32 " consecutive failures)", delay_ms / 1000,
           this->consecutive_failures_);
}

void KegboardReporter::handle_pairing_(const std::string &body) {
  std::string state = "pending";
  std::string token;
  if (!body.empty()) {
    json::parse_json(body, [&](JsonObject root) -> bool {
      JsonObjectConst pairing = root["pairing"].as<JsonObjectConst>();
      if (pairing.isNull())
        return false;
      if (pairing["state"].is<const char *>())
        state = pairing["state"].as<const char *>();
      if (pairing["token"].is<const char *>())
        token = pairing["token"].as<const char *>();
      return true;
    });
  }

  if (state == "allowed" && !token.empty()) {
    ESP_LOGI(TAG, "Pairing allowed; token provisioned");
    this->bearer_token_ = token;
    this->save_token_(token);
    // Deliver the queued backlog under the new identity immediately.
    this->next_attempt_ms_ = millis();
    return;
  }

  if (state == "denied") {
    ESP_LOGW(TAG, "Pairing denied by server; stopping until reboot");
    this->denied_ = true;
    return;
  }

  // Pending: poll fast for the first minute, then at heartbeat cadence.
  const uint32_t since_start = millis() - this->pairing_started_ms_;
  const uint32_t interval = since_start < PAIRING_FAST_WINDOW_MS ? PAIRING_FAST_POLL_MS : this->heartbeat_ms_;
  this->next_attempt_ms_ = millis() + interval;
  ESP_LOGI(TAG, "Pairing pending; approve this device (%s) on the server dashboard",
           this->hub_ != nullptr ? this->hub_->serial_number().c_str() : "kegboard");
}

void KegboardReporter::dispatch_commands_(const std::string &body) {
  if (body.empty())
    return;

  json::parse_json(body, [this](JsonObject root) -> bool {
    JsonArrayConst commands = root["commands"].as<JsonArrayConst>();
    if (commands.isNull())
      return true;

    for (JsonObjectConst cmd : commands) {
      if (!cmd["id"].is<const char *>() || !cmd["type"].is<const char *>())
        continue;
      const std::string id = cmd["id"].as<const char *>();
      const std::string type = cmd["type"].as<const char *>();

      bool seen = false;
      for (const auto &recent : this->recent_command_ids_) {
        if (recent == id) {
          seen = true;
          break;
        }
      }
      if (seen)
        continue;
      this->recent_command_ids_.push_back(id);
      if (this->recent_command_ids_.size() > COMMAND_DEDUP_WINDOW)
        this->recent_command_ids_.erase(this->recent_command_ids_.begin());

      CommandOutcome outcome = CommandOutcome::UNSUPPORTED;
      std::string message;
      if (this->command_handler_) {
        outcome = this->command_handler_(type, cmd["data"].as<JsonObjectConst>(), message);
      }

      const char *result = outcome == CommandOutcome::OK      ? "ok"
                           : outcome == CommandOutcome::ERROR ? "error"
                                                              : "unsupported";
      ESP_LOGD(TAG, "Command %s (%s) -> %s", id.c_str(), type.c_str(), result);
      this->enqueue_(this->make_event_("command_result", kbcore::command_result_data_json(id, result, message)), false);
    }
    return true;
  });
}

void KegboardReporter::publish_diagnostics_() {
  const uint32_t depth = this->queue_.size();
  const uint32_t dropped = this->queue_.dropped() + this->extra_dropped_;
  if (this->queue_depth_sensor_ != nullptr && depth != this->last_published_depth_) {
    this->queue_depth_sensor_->publish_state(depth);
    this->last_published_depth_ = depth;
  }
  if (this->dropped_sensor_ != nullptr && dropped != this->last_published_dropped_) {
    this->dropped_sensor_->publish_state(dropped);
    this->last_published_dropped_ = dropped;
  }
}

void KegboardReporter::dump_config() {
  ESP_LOGCONFIG(TAG, "Kegboard Reporter:");
  ESP_LOGCONFIG(TAG, "  Reporting URL: %s", this->reporting_url_.c_str());
  ESP_LOGCONFIG(TAG, "  Boot id: %s", this->boot_id_.c_str());
  ESP_LOGCONFIG(TAG, "  Token: %s", this->is_paired() ? "provisioned" : "unpaired");
  ESP_LOGCONFIG(TAG, "  Heartbeat: %" PRIu32 " s", this->heartbeat_ms_ / 1000);
  ESP_LOGCONFIG(TAG, "  Pour updates: every %" PRIu32 " ms", this->pour_update_ms_);
  ESP_LOGCONFIG(TAG, "  Meters: %u", static_cast<unsigned>(this->meters_.size()));
}

}  // namespace esphome::kegboard_reporter
